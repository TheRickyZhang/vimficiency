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

// The journal runs sampled under one params, and the rolled-up ones.
struct JournalRuns {
  // The journal directory exists.
  bool present = false;
  int64_t journalBytes = 0;
  // How much of `journalBytes` the samples cover.
  int64_t scoredBytes = 0;
  // Valid until the next read.
  std::vector<SampledRuns> sampled;
  std::vector<RunTable> rolledUp;
  // Rolled up under other params and left out by `currentOnly`.
  int hiddenRollupRuns = 0;
};

// `params` is a serialized, stamped `vf.journal.SampleParams`. Samples files
// are parsed once per process; each read parses only what was appended since.
// Only samples made under `params` are read: the sampler rescores the rest.
// Rollups are kept under any params unless `currentOnly`, since their
// journals are gone and can never be rescored.
JournalRuns readJournalRuns(const std::string& dir, std::string_view params, bool currentOnly);

}  // namespace Stats
