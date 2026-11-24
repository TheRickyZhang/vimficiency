module;
#include "proto/journal.pb.h"

module vf.Stats.JournalRuns;

import std.compat;
import vf.Storage.RecordFile;
import vf.Stats.Rollup;
import vf.Stats.Run;

using namespace std;
namespace fs = std::filesystem;

namespace Stats {

namespace {

// The records of a samples file that follow one params record.
struct SampleSegment {
  // Serialized.
  string params;
  vector<Run> runs;
  RunTable totals;
  int64_t journalSize = 0;
};

struct ParsedSamples {
  // The file's first record: a file that no longer starts with it was replaced.
  string head;
  uintmax_t bytes = 0;
  vector<SampleSegment> segments;
};

// Every samples file read, by journal directory, then by the name of the
// journal sampled.
unordered_map<string, unordered_map<string, ParsedSamples>>& sampleCache() {
  static unordered_map<string, unordered_map<string, ParsedSamples>> cache;
  return cache;
}

void catchUp(ParsedSamples& parsed, const fs::path& path) {
  error_code ec;
  const uintmax_t size = fs::file_size(path, ec);
  string head(parsed.head.size(), '\0');
  ifstream(path, ios::binary).read(head.data(), head.size());
  if (ec || size < parsed.bytes || head != parsed.head) parsed = {};
  if (ec) return;

  const string bytes = readFrom(path, parsed.bytes);
  string_view rest = bytes;
  string_view entry;
  vf::journal::SampleRecord record;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    if (parsed.head.empty()) parsed.head = bytes.substr(0, bytes.size() - rest.size());
    if (record.has_params()) {
      parsed.segments.push_back({.params = record.params().SerializeAsString()});
      continue;
    }
    if (parsed.segments.empty()) continue;
    SampleSegment& segment = parsed.segments.back();
    if (record.has_sample()) {
      Run run = journalRun(record.sample());
      if (!isCounted(run)) continue;
      addRun(segment.totals, run);
      segment.runs.push_back(std::move(run));
    } else if (record.has_journal_size()) {
      segment.journalSize = record.journal_size();
    }
  }
  parsed.bytes += bytes.size() - rest.size();
}

}  // namespace

JournalRuns readJournalRuns(const string& dir, string_view params, bool currentOnly) {
  JournalRuns out;
  out.present = fs::is_directory(dir);
  if (!out.present) return out;

  // A rolled-up journal still on disk is one whose deletion was cut short.
  unordered_set<string> rolledUp;
  for (RolledJournal& rolled : readRollups(fs::path(dir) / ROLLUP_FILE)) {
    rolledUp.insert(rolled.journal);
    if (!currentOnly || rolled.params == params) {
      out.rolledUp.push_back(std::move(rolled.runs));
      continue;
    }
    for (const auto& [key, totals] : rolled.runs) out.hiddenRollupRuns += totals.runs;
  }

  unordered_map<string, ParsedSamples>& cached = sampleCache()[dir];
  unordered_set<string> seen;
  for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
    const string name = entry.path().filename().string();
    if (entry.path().extension() != ".vfj" || rolledUp.contains(name)) continue;
    out.journalBytes += entry.file_size();

    fs::path samplesPath = entry.path();
    samplesPath.replace_extension(".samples");
    auto& [journal, parsed] = *cached.try_emplace(name).first;
    seen.insert(name);
    catchUp(parsed, samplesPath);
    int64_t scored = 0;
    for (const SampleSegment& segment : parsed.segments) {
      if (segment.params != params) continue;
      out.sampled.push_back({.journal = journal, .runs = segment.runs, .totals = &segment.totals});
      scored = segment.journalSize;
    }
    out.scoredBytes += scored;
  }
  erase_if(cached, [&](const auto& entry) { return !seen.contains(entry.first); });
  return out;
}

}  // namespace Stats
