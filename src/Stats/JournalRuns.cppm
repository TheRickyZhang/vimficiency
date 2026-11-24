export module vf.Stats.JournalRuns;

import std.compat;
import vf.Stats.Rollup;
import vf.Stats.Run;

export namespace Stats {

// One journal's runs sampled under the params read.
struct SampledRuns {
  // The journal's file name.
  std::string_view journal;
  // Counted ones only.
  std::span<const Run> runs;
  const RunTable* totals = nullptr;
};

// The journal runs sampled or rolled up under one params.
struct JournalRuns {
  // The journal directory exists.
  bool present = false;
  int64_t journalBytes = 0;
  // How much of `journalBytes` the samples cover.
  int64_t scoredBytes = 0;
  // Valid until the next read.
  std::vector<SampledRuns> sampled;
  std::vector<RunTable> rolledUp;
  // Rolled up under other params: their journals are gone, so they are never rescored.
  int otherRollupRuns = 0;
};

// `params` is a serialized, stamped `vf.journal.SampleParams`. Samples files
// are parsed once per process; each read parses only what was appended since.
JournalRuns readJournalRuns(const std::string& dir, std::string_view params);

}  // namespace Stats
