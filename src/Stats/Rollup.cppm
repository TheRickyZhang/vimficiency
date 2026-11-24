export module vf.Stats.Rollup;

import std.compat;
import vf.Stats.Commands;
import vf.Stats.Run;
import vf.Stats.Score;

export namespace Stats {

// In the journal directory; written by `vimfy_journal`, read by the summary.
inline constexpr std::string_view ROLLUP_FILE = "rollup.vfr";

// A `vf.journal.Sample.Outcome` with a score: `scored` or `unbeaten`.
bool isScoring(int outcome);

struct RunKey {
  int64_t day = 0;
  int windowSize = 0;
  bool navOnly = false;
  // A `vf.journal.Sample.Outcome`.
  int outcome = 0;

  auto operator<=>(const RunKey&) const = default;
};

// Journal runs reduced to what the summary shows of them.
struct RunTotals {
  int runs = 0;
  ScoreTotals effort;
  ScoreTotals uniform;
  int deadlineHit = 0;
  // Of `scored` runs only.
  CommandCounts user;
  CommandCounts optimizer;

  inline const ScoreTotals& score(Weight weight) const {
    return weight == Weight::Effort ? effort : uniform;
  }
};

using RunTable = std::map<RunKey, RunTotals>;

// A journal run's.
RunKey keyOf(const Run& run);

// False for a scoring journal run with no user cost: no count includes it.
bool isCounted(const Run& run);

// Adds a journal run, unless it is not counted.
void addRun(RunTable& runs, const Run& run);

struct RolledJournal {
  // The journal's file name.
  std::string journal;
  // A serialized `vf.journal.SampleParams`.
  std::string params;
  RunTable runs;
};

// A `vf.journal.RollupFile`'s rollups in the order appended, less those of a
// newer `ROLLUP_FORMAT`; none when the file is missing.
std::vector<RolledJournal> readRollups(const std::filesystem::path& path);

std::expected<void, std::string> appendRollup(const std::filesystem::path& path,
                                              const RolledJournal& rolled);

}  // namespace Stats
