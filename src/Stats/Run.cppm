export module vf.Stats.Run;

import std.compat;
import vf.Stats.Commands;
import vf.Stats.Score;

export namespace Stats {

enum class Source : uint8_t { Journal, Session };

// A journal sample or a logged session, as every stats query reads it.
struct Run {
  Source source = Source::Journal;
  int64_t wallEpoch = 0;
  // localDay(wallEpoch): every query filters by it.
  int64_t day = 0;
  // A journal run's; 0 for a session.
  int windowSize = 0;
  // A journal run's `last_atom`, or a session's index among the log's sessions.
  int index = 0;
  // A `vf.journal.Sample.Outcome`; a session's is the like-named one.
  int outcome = 0;
  bool navOnly = false;
  bool deadlineHit = false;
  double userCost = 0;
  // An unbeaten run's is its user cost.
  double bestCost = 0;
  double analyzeMs = 0;
  std::string userKeys;
  std::string bestSeq;
  RunCommands user;
  RunCommands optimizer;
};

// Tallies a `vf.journal.Sample` or `vf.stats.Session` and `run.bestSeq`. One
// recorded before `typed_insert` existed counts no insert text on either side.
// A template because generated headers stay out of module interfaces.
template <typename Recorded>
void tallyCommands(Run& run, const Recorded& recorded) {
  const bool insertText = recorded.has_typed_insert();
  CommandCounts user;
  for (const auto& command : recorded.commands()) countKeys(user, command.keys(), false);
  if (insertText) countInsertKeys(user, recorded.typed_insert());
  CommandCounts optimizer;
  countKeys(optimizer, run.bestSeq, insertText);
  run.user = intern(user);
  run.optimizer = intern(optimizer);
}

// A `vf.journal.Sample`.
template <typename Sample>
Run journalRun(const Sample& sample) {
  Run run{.wallEpoch = sample.wall_epoch(),
          .day = localDay(sample.wall_epoch()),
          .windowSize = sample.window_size(),
          .index = sample.last_atom(),
          .outcome = sample.outcome(),
          .navOnly = sample.nav_only(),
          .deadlineHit = sample.deadline_hit(),
          .userCost = sample.user_cost(),
          .bestCost = sample.outcome() == Sample::unbeaten ? sample.user_cost() : sample.best_cost(),
          .analyzeMs = sample.analyze_ms(),
          .bestSeq = sample.best_seq()};
  for (const auto& command : sample.commands()) run.userKeys += command.keys();
  tallyCommands(run, sample);
  return run;
}

}  // namespace Stats
