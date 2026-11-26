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

string_view sessionError(const vf::stats::Session& session, int logFormat) {
  if (session.kind().empty()) return "malformed session: missing kind";
  if (!vf::stats::Session::Outcome_IsValid(session.outcome())) return "malformed session: unknown outcome";
  if (session.key_count() < 0) return "malformed session: negative key count";
  if (!isfinite(session.user_cost()) || session.user_cost() < 0 ||
      !isfinite(session.best_cost()) || session.best_cost() < 0) return "malformed session: invalid effort";
  if (!isfinite(session.analyze_ms()) || session.analyze_ms() < 0) return "malformed session: invalid duration";
  if (!session.has_query()) {
    return logFormat == 1 ? "outdated session: recorded before query measurements"
                          : "malformed session: missing query";
  }
  if (session.query().query_size() <= 0) return "malformed session: query_size must be positive";
  return {};
}

struct LogCursor {
  uintmax_t bytes = 0;
  int format = 1;
};

template <class Visit>
expected<bool, string> readRecords(const string& path, LogCursor& cursor, Visit visit) {
  const string bytes = readFrom(path, cursor.bytes);
  string_view rest = bytes;
  vf::stats::LogRecord record;
  while (!rest.empty()) {
    const auto remaining = rest.size();
    string_view entry;
    const auto frame = nextRecord(rest, entry);
    if (!frame && frame.error() == RecordError::Incomplete) return false;
    if (!frame || !record.ParseFromArray(entry.data(), entry.size()) ||
        record.body_case() == vf::stats::LogRecord::BODY_NOT_SET) {
      return unexpected(format("{} at byte {}: malformed session log record", path, cursor.bytes));
    }
    if (record.has_header()) {
      const int version = record.header().format();
      if (version > SESSION_LOG_FORMAT) {
        return unexpected(format("{} is session log format {}, newer than this plugin's {}", path,
                                 version, SESSION_LOG_FORMAT));
      }
      if (version < 1) {
        return unexpected(format("{} at byte {}: malformed session log header", path, cursor.bytes));
      }
      cursor.format = version;
    } else if (record.has_session()) {
      const auto error = sessionError(record.session(), cursor.format);
      if (!error.empty()) return unexpected(format("{} at byte {}: {}", path, cursor.bytes, error));
    }
    visit(record);
    cursor.bytes += remaining - rest.size();
  }
  return true;
}

struct LoggedSoFar {
  LogCursor cursor;
  uintmax_t writtenSize = 0;
  unordered_set<uint64_t> stamps;
};

LoggedSession sessionRun(const vf::stats::Session& session, int index) {
  const double user = session.user_cost();
  Run run{.source = Source::Session,
          .wallEpoch = session.finished_wall_epoch(),
          .day = localDay(session.finished_wall_epoch()),
          .index = index,
          .navOnly = session.query().nav_only(),
          .userCost = user,
          .bestCost = session.best_cost(),
          .analyzeMs = session.analyze_ms(),
          .querySize = session.query().query_size(),
          .userKeys = session.user_seq(),
          .bestSeq = session.best_seq(),
          .savedFilename = session.saved_filename()};
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
  return {std::move(run), session.kind(), session.key_count()};
}

}  // namespace

expected<SessionHistory, string> readSessionLog(const string& path, string_view stampBytes,
                                                bool currentOnly) {
  vf::stats::Stamp stamp;
  if (!stamp.ParseFromArray(stampBytes.data(), stampBytes.size())) {
    return unexpected("malformed vf.stats.Stamp");
  }
  const uint64_t currentId = stampId(versioned(stamp));
  SessionHistory history;
  history.label = format("scoring {}·{:08x}", SCORING_VERSION, currentId >> 32);

  LogCursor cursor;
  const auto read = readRecords(path, cursor, [&](const vf::stats::LogRecord& record) {
    if (!record.has_session()) return;
    const vf::stats::Session& session = record.session();
    const int index = history.sessions++;
    if (currentOnly && record.stamp_id() != currentId) {
      history.hidden++;
    } else {
      history.runs.push_back(sessionRun(session, index));
    }
  });
  if (!read) return unexpected(read.error());
  return history;
}

expected<void, string> appendSession(string_view requestBytes) {
  vf::stats::AppendSessionRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.stats.AppendSessionRequest");
  }
  const auto error = sessionError(request.session(), SESSION_LOG_FORMAT);
  if (!error.empty()) return unexpected(string(error));

  static unordered_map<string, LoggedSoFar> seen;
  const auto& path = request.path();
  LoggedSoFar& logged = seen[path];
  error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size < max(logged.cursor.bytes, logged.writtenSize)) logged = {};
  const auto read = readRecords(path, logged.cursor, [&](const vf::stats::LogRecord& record) {
    if (record.has_stamp()) logged.stamps.insert(record.stamp_id());
  });
  if (!read) return unexpected(read.error());
  if (!*read) return unexpected(format("{} at byte {}: incomplete session log record", path, logged.cursor.bytes));

  const vf::stats::Stamp stamp = versioned(request.stamp());
  const uint64_t id = stampId(stamp);

  // Every record in one write, so a concurrent writer cannot split them.
  vf::stats::SessionLog log;
  if (logged.cursor.bytes == 0) {
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

  const string bytes = log.SerializeAsString();
  ofstream out(path, ios::binary | ios::app);
  out << bytes;
  out.flush();
  if (!out) return unexpected("could not append to " + path);
  logged.writtenSize = logged.cursor.bytes + bytes.size();
  logged.stamps.insert(id);
  return {};
}

}  // namespace Stats
