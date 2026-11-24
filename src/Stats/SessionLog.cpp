module;
#include "proto/journal.pb.h"
#include "proto/stats.pb.h"

module vf.Stats.SessionLog;

import std.compat;
import vf.Stats.Run;
import vf.Stats.Score;
import vf.Storage.RecordFile;
import vf.Versions;

using namespace std;
namespace fs = std::filesystem;

namespace Stats {

namespace {

using vf::journal::Sample;

vf::stats::Stamp versioned(vf::stats::Stamp stamp) {
  stamp.set_scoring_version(SCORING_VERSION);
  return stamp;
}

// FNV-1a: the id is stored, so it must not change between builds.
uint64_t stampId(const vf::stats::Stamp& stamp) {
  uint64_t hash = 0xcbf29ce484222325;
  for (const unsigned char byte : stamp.SerializeAsString()) {
    hash ^= byte;
    hash *= 0x100000001b3;
  }
  return hash;
}

struct LoggedSoFar {
  uintmax_t bytes = 0;
  int format = 1;
  unordered_set<uint64_t> stamps;
};

string newerFormat(const string& path, int format) {
  return std::format("{} is session log format {}, newer than this plugin's {}", path, format,
                     SESSION_LOG_FORMAT);
}

// Read incrementally: other Neovims append to the log too. A log shorter than
// what was read has been replaced, so it is read again.
LoggedSoFar& loggedSoFar(const string& path) {
  static unordered_map<string, LoggedSoFar> seen;
  LoggedSoFar& log = seen[path];

  error_code ec;
  const uintmax_t size = fs::file_size(path, ec);
  if (ec || size < log.bytes) log = {};
  if (ec) return log;

  const string bytes = readFrom(path, log.bytes);
  string_view rest = bytes;
  string_view entry;
  vf::stats::LogRecord record;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    if (record.has_header()) log.format = record.header().format();
    if (record.has_stamp()) log.stamps.insert(record.stamp_id());
  }
  log.bytes += bytes.size() - rest.size();
  return log;
}

Run sessionRun(const vf::stats::Session& session, int index) {
  const double user = session.user_cost();
  Run run{.source = Source::Session,
          .wallEpoch = session.finished_wall_epoch(),
          .day = localDay(session.finished_wall_epoch()),
          .index = index,
          .userCost = user,
          .bestCost = session.best_cost(),
          .analyzeMs = session.analyze_ms(),
          .userKeys = session.user_seq(),
          .bestSeq = session.best_seq()};
  switch (session.outcome()) {
    case vf::stats::Session::no_result:
      run.outcome = Sample::no_result;
      break;
    case vf::stats::Session::unbeaten:
      run.outcome = Sample::unbeaten;
      run.bestCost = user;
      break;
    default:
      run.outcome = Sample::scored;
  }
  tallyCommands(run, session);
  return run;
}

}  // namespace

// The log's sessions by stamp: `current`'s group first, even when empty, then
// the rest by their latest session, newest first.
expected<SessionHistory, string> readSessionLog(const string& path, string_view stampBytes) {
  vf::stats::Stamp stamp;
  if (!stamp.ParseFromArray(stampBytes.data(), stampBytes.size())) {
    return unexpected("malformed vf.stats.Stamp");
  }
  stamp.set_scoring_version(SCORING_VERSION);
  SessionHistory history;
  const uint64_t currentId = stampId(stamp);
  unordered_map<uint64_t, int> versions{{currentId, stamp.scoring_version()}};
  unordered_map<uint64_t, Group> byId;
  byId[currentId].current = true;

  const string bytes = readFrom(path, 0);
  string_view rest = bytes;
  string_view entry;
  vf::stats::LogRecord record;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    if (record.has_header() && record.header().format() > SESSION_LOG_FORMAT) {
      return unexpected(newerFormat(path, record.header().format()));
    }
    if (record.has_stamp()) {
      versions[record.stamp_id()] = record.stamp().scoring_version();
      continue;
    }
    if (!record.has_session()) continue;
    const vf::stats::Session& session = record.session();
    Group& group = byId[record.stamp_id()];
    group.latest = max(group.latest, session.finished_wall_epoch());
    group.sessions.push_back(sessionRun(session, history.sessions++));

    history.commands += session.key_count();
    const string& kind = session.kind();
    if (kind.starts_with("suggest")) {
      ++history.suggest;
    } else if (kind == "mark") {
      ++history.mark;
    } else if (kind == "watch") {
      ++history.watch;
    } else if (kind == "recall") {
      ++history.recall;
    }
  }

  for (auto& [id, group] : byId) {
    const int version = versions[id];
    group.label = version == 0 ? format("older·{:08x}", id >> 32)
                               : format("scoring {}·{:08x}", version, id >> 32);
    history.groups.push_back(std::move(group));
  }
  ranges::sort(history.groups, [](const Group& a, const Group& b) {
    if (a.current != b.current) return a.current;
    return a.latest > b.latest;
  });
  return history;
}

expected<void, string> appendSession(string_view requestBytes) {
  vf::stats::AppendSessionRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.stats.AppendSessionRequest");
  }
  const vf::stats::Stamp stamp = versioned(request.stamp());
  const uint64_t id = stampId(stamp);
  LoggedSoFar& logged = loggedSoFar(request.path());
  if (logged.format > SESSION_LOG_FORMAT) {
    return unexpected(newerFormat(request.path(), logged.format));
  }

  // Every record in one write, so a concurrent writer cannot split them.
  vf::stats::SessionLog log;
  if (logged.bytes == 0) {
    log.add_records()->mutable_header()->set_format(SESSION_LOG_FORMAT);
  }
  if (!logged.stamps.contains(id)) {
    vf::stats::LogRecord& record = *log.add_records();
    record.set_stamp_id(id);
    *record.mutable_stamp() = stamp;
  }
  vf::stats::LogRecord& record = *log.add_records();
  record.set_stamp_id(id);
  *record.mutable_session() = request.session();

  ofstream out(request.path(), ios::binary | ios::app);
  out << log.SerializeAsString();
  out.flush();
  if (!out) return unexpected("could not append to " + request.path());
  logged.stamps.insert(id);
  return {};
}

}  // namespace Stats
