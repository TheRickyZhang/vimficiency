module vf.Stats.Query;

import std.compat;
import vf.Stats.Commands;
import vf.Stats.JournalRuns;
import vf.Stats.Rollup;
import vf.Stats.Run;

using namespace std;

namespace Stats {

namespace {

bool matchesDayAndOutcome(const Selection& selection, int64_t day, int outcome) {
  return day >= selection.firstDay && day <= selection.lastDay &&
         (selection.outcomes == 0 || (selection.outcomes & (1u << outcome)));
}

bool matchesCommands(const Selection& selection, const Run& run) {
  return ranges::all_of(selection.commands, [&](const CommandMatch& match) {
    return ranges::any_of(match.commands, [&](int command) {
      return (match.side != Side::Optimizer && run.user.uses(command)) ||
             (match.side != Side::User && run.optimizer.uses(command));
    });
  });
}

double orderKey(const Run& run, RunOrder order) {
  switch (order) {
    case RunOrder::Worst:
      return isScoring(run.outcome) ? run.userCost - run.bestCost
                                    : -numeric_limits<double>::infinity();
    case RunOrder::Newest:
      return run.wallEpoch;
    case RunOrder::Slowest:
      return run.analyzeMs;
  }
  return 0;
}

}  // namespace

bool matches(const Selection& selection, const RunKey& key) {
  if (!selection.journal || !matchesDayAndOutcome(selection, key.day, key.outcome)) return false;
  if (selection.windowSize != 0 && key.windowSize != selection.windowSize) return false;
  return selection.change == Change::Any || key.navOnly == (selection.change == Change::Nav);
}

bool matches(const Selection& selection, const Run& run) {
  if (run.source == Source::Session) {
    if (!selection.sessions || !matchesDayAndOutcome(selection, run.day, run.outcome)) return false;
  } else if (!matches(selection, keyOf(run))) {
    return false;
  }
  return matchesCommands(selection, run);
}

Selection chartedSelection(const Scope& scope) {
  Selection selection = scope.selection;
  selection.windowSize = scope.charted;
  return selection;
}

void visitJournal(const Scope& scope, const Selection& selection,
                  const function<void(const RunKey&, const RunTotals&)>& visit) {
  if (selection.commands.empty()) {
    auto visitTable = [&](const RunTable& table) {
      for (const auto& [key, totals] : table) {
        if (matches(selection, key)) visit(key, totals);
      }
    };
    for (const RunTable& rolled : scope.journal.rolledUp) visitTable(rolled);
    for (const SampledRuns& sampled : scope.journal.sampled) visitTable(*sampled.totals);
    return;
  }
  for (const SampledRuns& sampled : scope.journal.sampled) {
    RunTable table;
    for (const Run& run : sampled.runs) {
      if (matches(selection, run)) addRun(table, run);
    }
    for (const auto& [key, totals] : table) visit(key, totals);
  }
}

int rolledUpRuns(const Scope& scope, const Selection& selection) {
  int runs = 0;
  for (const RunTable& rolled : scope.journal.rolledUp) {
    for (const auto& [key, totals] : rolled) {
      if (matches(selection, key)) runs += totals.runs;
    }
  }
  return runs;
}

RunPage pageRuns(const Scope& scope, RunOrder order, int offset, int limit) {
  const Selection selection = chartedSelection(scope);
  vector<ListedRun> listed;
  for (const Run& session : scope.sessions.runs) {
    if (matches(selection, session)) listed.push_back({.run = &session});
  }
  for (const SampledRuns& sampled : scope.journal.sampled) {
    for (const Run& run : sampled.runs) {
      if (matches(selection, run)) listed.push_back({.run = &run, .journal = sampled.journal});
    }
  }

  RunPage page;
  const int rolled = rolledUpRuns(scope, selection);
  (selection.commands.empty() ? page.rolledUp : page.rollupRunsWithoutCommands) = rolled;
  page.total = ssize(listed) + page.rolledUp;

  auto before = [order](const ListedRun& a, const ListedRun& b) {
    const Run& x = *a.run;
    const Run& y = *b.run;
    const double kx = orderKey(x, order);
    const double ky = orderKey(y, order);
    if (kx != ky) return kx > ky;
    if (x.wallEpoch != y.wallEpoch) return x.wallEpoch > y.wallEpoch;
    return tie(x.source, a.journal, x.windowSize, x.index) <
           tie(y.source, b.journal, y.windowSize, y.index);
  };
  const int end = min<int64_t>(ssize(listed), int64_t(offset) + limit);
  if (offset >= end) return page;
  ranges::partial_sort(listed, listed.begin() + end, before);
  page.rows.assign(listed.begin() + offset, listed.begin() + end);
  return page;
}

}  // namespace Stats
