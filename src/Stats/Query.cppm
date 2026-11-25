export module vf.Stats.Query;

import std.compat;
import vf.Stats.JournalRuns;
import vf.Stats.Rollup;
import vf.Stats.Run;
import vf.Stats.SessionLog;

export namespace Stats {

enum class Change : uint8_t { Any, Nav, Edit };
enum class Side : uint8_t { Either, User, Optimizer };

// Any of `commands`, interned, on `side`; none of them when `absent`.
struct CommandMatch {
  std::vector<int> commands;
  Side side = Side::Either;
  bool absent = false;
};

// A `vf.stats.Filter`.
struct Selection {
  bool journal = true;
  // False too when a field sessions lack is set.
  bool sessions = true;
  int64_t firstDay = std::numeric_limits<int64_t>::min();
  int64_t lastDay = std::numeric_limits<int64_t>::max();
  // Of journal runs; 0 for any.
  int windowSize = 0;
  std::optional<std::pair<int64_t, int64_t>> epochSeconds;
  Change change = Change::Any;
  // Selected `vf.journal.Sample.Outcome` values; empty includes all outcomes.
  std::vector<int> outcomes;
  // Each must hold.
  std::vector<CommandMatch> commands;
  // Only runs whose user typed more insert text than the best sequence.
  bool overTyped = false;
  // Only runs whose keys cost less than the best sequence found.
  bool beats = false;
  bool timed = false;
  std::optional<std::pair<int64_t, int64_t>> querySize;

  // Whether it reads each run on its own, which rollups do not keep.
  inline bool readsRuns() const { return !commands.empty() || overTyped || beats || timed || querySize; }
};

// A journal total's fields; commands are not checked.
bool matches(const Selection& selection, const RunKey& key);
bool matches(const Selection& selection, const Run& run);

// What a summary and a run list reduce: the logged sessions and the journal
// runs, every stamp's or the current one's only, narrowed by `selection`.
struct Scope {
  SessionHistory sessions;
  JournalRuns journal;
  // The requested window sizes, ascending.
  std::vector<int> sizes;
  // The window size the journal is summarized at: the filter's, else the
  // requested one, else the middle one.
  int charted = 0;
  Selection selection;
};

// The scope's selection with its window size settled to the charted one.
Selection chartedSelection(const Scope& scope);

// Calls `visit` with each journal total `selection` selects. Rollups keep no
// per-run commands, so a selection by commands leaves them out.
void visitJournal(const Scope& scope, const Selection& selection,
                  const std::function<void(const RunKey&, const RunTotals&)>& visit);

// Rolled-up runs `selection` selects, but for its commands.
int rolledUpRuns(const Scope& scope, const Selection& selection);

// What a run list sorts by. A run with no value there (the best cost of an
// unscored run) goes last in either direction.
enum class RunOrder : uint8_t {
  // User cost − best cost.
  Worst,
  Newest,
  // Analysis time.
  Slowest,
  UserCost,
  BestCost,
  // The share of the user's cost the best sequence saves.
  Saved,
  // Characters the user typed in insert mode − the best sequence's.
  Typed,
};

struct ListedRun {
  const Run* run = nullptr;
  // A journal run's file name.
  std::string_view journal;
};

struct RunPage {
  // Including `rolledUp`.
  int total = 0;
  int rolledUp = 0;
  int rollupRunsWithoutCommands = 0;
  std::vector<ListedRun> rows;
};

// The scope's sessions and charted journal runs that its selection selects, in
// `order`, largest first unless `ascending`, from `offset`. Ties go newest
// first, then by run id.
RunPage pageRuns(const Scope& scope, RunOrder order, bool ascending, int offset, int limit);

}  // namespace Stats
