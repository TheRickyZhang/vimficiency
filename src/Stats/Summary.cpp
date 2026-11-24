module;
#include "proto/journal.pb.h"
#include "proto/stats.pb.h"
#include "proto/summary.pb.h"

module vf.Stats.Summary;

import std.compat;
import vf.Stats.Commands;
import vf.Stats.JournalRuns;
import vf.Stats.Query;
import vf.Stats.Rollup;
import vf.Stats.Run;
import vf.Stats.Score;
import vf.Stats.SessionLog;
import vf.Storage.Samples;

using namespace std;

namespace Stats {

namespace {

using vf::journal::Sample;
using vf::stats::Filter;

constexpr int CHART_DAYS = 30;
constexpr int64_t DAY_SECONDS = 86400;

Selection selectionOf(const Filter& filter) {
  Selection selection{
      .journal = filter.source() != Filter::sessions,
      .sessions = filter.source() != Filter::journal && filter.window_size() == 0 &&
                  filter.kind() == Filter::any,
      .windowSize = filter.window_size(),
      .change = filter.kind() == Filter::nav    ? Change::Nav
                : filter.kind() == Filter::edit ? Change::Edit
                                                : Change::Any,
  };
  if (filter.has_days()) {
    selection.firstDay = filter.days().first();
    selection.lastDay = filter.days().last();
  }
  for (const int outcome : filter.outcomes()) selection.outcomes |= 1u << outcome;
  for (const Filter::Commands& commands : filter.commands()) {
    CommandMatch& match = selection.commands.emplace_back();
    match.side = commands.side() == Filter::user        ? Side::User
                 : commands.side() == Filter::optimizer ? Side::Optimizer
                                                        : Side::Either;
    for (const string& token : commands.tokens()) {
      const int id = internedId(token);
      if (id >= 0) match.commands.push_back(id);
    }
  }
  return selection;
}

// Shared by the summary and the run list, so a row's drill filter lists what it counted.
expected<Scope, string> openScope(const vf::stats::SummaryRequest& request) {
  vf::journal::SampleParams params = request.sample_params();
  if (params.window_sizes().empty()) return unexpected("sample_params.window_sizes is empty");
  Scope scope;
  auto history = readSessionLog(request.session_log(), request.stamp().SerializeAsString(),
                                request.current_only());
  if (!history) return unexpected(history.error());
  scope.sessions = std::move(*history);
  scope.sizes.assign(params.window_sizes().begin(), params.window_sizes().end());
  ranges::sort(scope.sizes);
  const int requested = request.charted_size();
  if (requested != 0 && !ranges::contains(scope.sizes, requested)) {
    return unexpected(format("charted_size {} is not one of the window sizes", requested));
  }
  const int filtered = request.filter().window_size();
  scope.charted = filtered != 0    ? filtered
                  : requested != 0 ? requested
                                   : scope.sizes[(scope.sizes.size() - 1) / 2];
  stampSampleParams(params);
  scope.journal =
      readJournalRuns(request.journal_dir(), params.SerializeAsString(), request.current_only());
  scope.selection = selectionOf(request.filter());
  return scope;
}

// `base` narrowed to the runs of `outcomes`.
Filter drillOutcomes(const Filter& base, initializer_list<Sample::Outcome> outcomes) {
  Filter out = base;
  out.clear_outcomes();
  for (const Sample::Outcome outcome : outcomes) {
    if (base.outcomes().empty() || ranges::contains(base.outcomes(), outcome)) {
      out.add_outcomes(outcome);
    }
  }
  return out;
}

Filter drillSource(Filter filter, Filter::Source source) {
  filter.set_source(source);
  return filter;
}

// A score and its per-day chart.
struct Scores {
  ScoreTotals all;
  map<int64_t, ScoreTotals> byDay;

  void add(int64_t day, double userCost, double bestCost, Weight weight) {
    all.add(userCost, bestCost, weight);
    byDay[day].add(userCost, bestCost, weight);
  }

  void merge(int64_t day, const ScoreTotals& totals) {
    all.merge(totals);
    byDay[day].merge(totals);
  }
};

void fillScore(vf::stats::Score& out, const ScoreTotals& totals) {
  out.set_spans(totals.spans);
  if (totals.spans > 0) out.set_score(totals.score());
}

// `drill` selects the runs the score counts.
template <typename Section>
void fillDays(Section& out, const Scores& scores, int64_t today, const Filter& drill) {
  for (const auto& [day, totals] : scores.byDay) {
    const int64_t ago = today - day;
    if (ago < 0 || ago >= CHART_DAYS) continue;
    vf::stats::DayScore& entry = *out.add_days();
    entry.set_days_ago(ago);
    entry.set_score(totals.score());
    *entry.mutable_drill() = drill;
    entry.mutable_drill()->mutable_days()->set_first(day);
    entry.mutable_drill()->mutable_days()->set_last(day);
  }
}

// Nearest rank.
double percentile(const vector<double>& sorted, int p) {
  const size_t rank = size_t(ceil(p / 100.0 * sorted.size()));
  return sorted[clamp<size_t>(rank, 1, sorted.size()) - 1];
}

constexpr array<Sample::Outcome, 4> UNSCORED = {Sample::too_large, Sample::no_result,
                                                Sample::unsupported, Sample::crashed};

struct CommandTally {
  CommandCounts user;
  CommandCounts optimizer;

  template <typename Counted>
  void add(const Counted& counted) {
    user.merge(counted.user);
    optimizer.merge(counted.optimizer);
  }
};

void summarizeSessions(vf::stats::SessionSummary& out, const Scope& scope, const Filter& base,
                       Weight weight, int64_t now, CommandTally& commands, int& counted) {
  const Filter sessions = drillSource(base, Filter::sessions);
  *out.mutable_no_result_drill() = drillOutcomes(sessions, {Sample::no_result});
  *out.mutable_unbeaten_drill() = drillOutcomes(sessions, {Sample::unbeaten});

  Scores scores;
  vector<double> times;
  for (const Run& session : scope.sessions.runs) {
    if (!matches(scope.selection, session)) continue;
    out.set_sessions(out.sessions() + 1);
    const double user = session.userCost;
    switch (session.outcome) {
      case Sample::no_result:
        out.set_no_result(out.no_result() + 1);
        break;
      case Sample::unbeaten:
        out.set_unbeaten(out.unbeaten() + 1);
        if (user > 0) scores.add(session.day, user, user, weight);
        break;
      default:
        if (user < session.bestCost) out.set_beats(out.beats() + 1);
        if (user > 0) scores.add(session.day, user, session.bestCost, weight);
        commands.add(session);
        counted++;
    }
    if (now - session.wallEpoch < CHART_DAYS * DAY_SECONDS) times.push_back(session.analyzeMs);
  }
  fillScore(*out.mutable_score(), scores.all);
  fillDays(out, scores, localDay(now), drillOutcomes(sessions, {Sample::scored, Sample::unbeaten}));

  if (times.empty()) return;
  ranges::sort(times);
  vf::stats::Timing& timing = *out.mutable_timing();
  timing.set_n(ssize(times));
  timing.set_p50(percentile(times, 50));
  timing.set_p95(percentile(times, 95));
  timing.set_p99(percentile(times, 99));
  timing.set_max(times.back());
}

// The runs of the samples made under the request's params and of the rollups
// in scope. A journal whose samples were made under other params counts as
// unscored until `vimfy_journal` redoes it.
void summarizeJournal(vf::stats::JournalSummary& out, const Scope& scope, const Filter& base,
                      Weight weight, int64_t now, CommandTally& commands, int& counted) {
  const int charted = scope.charted;
  out.set_charted_size(charted);

  map<int, ScoreTotals> bySize;
  Scores scores;
  ScoreTotals nav;
  ScoreTotals edit;
  map<int, int> unscored;
  visitJournal(scope, scope.selection, [&](const RunKey& key, const RunTotals& totals) {
    const bool isCharted = key.windowSize == charted;
    if (!isScoring(key.outcome)) {
      if (isCharted) unscored[key.outcome] += totals.runs;
      return;
    }
    const ScoreTotals& score = totals.score(weight);
    bySize[key.windowSize].merge(score);
    if (!isCharted) return;

    scores.merge(key.day, score);
    (key.navOnly ? nav : edit).merge(score);
    out.set_deadline_hit(out.deadline_hit() + totals.deadlineHit);
    if (key.outcome == Sample::unbeaten) {
      out.set_unbeaten(out.unbeaten() + totals.runs);
      return;
    }
    commands.add(totals);
    counted += totals.runs;
  });

  const JournalRuns& journal = scope.journal;
  out.set_present(journal.present);
  out.set_journal_bytes(journal.journalBytes);
  out.set_scored_bytes(journal.scoredBytes);
  if (!scope.selection.commands.empty()) {
    out.set_rollup_runs_without_commands(rolledUpRuns(scope, chartedSelection(scope)));
  }

  const Filter scoring = drillOutcomes(base, {Sample::scored, Sample::unbeaten});
  for (const int size : scope.sizes) {
    vf::stats::SizeScore& entry = *out.add_sizes();
    entry.set_size(size);
    fillScore(*entry.mutable_score(), bySize[size]);
  }
  fillScore(*out.mutable_score(), scores.all);
  fillScore(*out.mutable_nav(), nav);
  fillScore(*out.mutable_edit(), edit);
  *out.mutable_nav_drill() = scoring;
  out.mutable_nav_drill()->set_kind(Filter::nav);
  *out.mutable_edit_drill() = scoring;
  out.mutable_edit_drill()->set_kind(Filter::edit);

  const Filter runs = drillSource(base, Filter::journal);
  fillDays(out, scores, localDay(now), drillOutcomes(runs, {Sample::scored, Sample::unbeaten}));
  *out.mutable_unbeaten_drill() = drillOutcomes(runs, {Sample::unbeaten});
  for (const Sample::Outcome outcome : UNSCORED) {
    const auto it = unscored.find(outcome);
    if (it == unscored.end()) continue;
    vf::stats::Unscored& entry = *out.add_unscored();
    entry.set_outcome(Sample::Outcome_Name(outcome));
    entry.set_runs(it->second);
    *entry.mutable_drill() = drillOutcomes(runs, {outcome});
  }
}

}  // namespace

expected<string, string> summarize(string_view requestBytes, int64_t now) {
  vf::stats::SummaryRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.stats.SummaryRequest");
  }
  const auto scope = openScope(request);
  if (!scope) return unexpected(scope.error());
  const SessionHistory& history = scope->sessions;
  vf::stats::Summary out;
  vf::stats::Lifetime& lifetime = *out.mutable_lifetime();
  lifetime.set_sessions(history.sessions);
  lifetime.set_mark(history.mark);
  lifetime.set_watch(history.watch);
  lifetime.set_recall(history.recall);
  lifetime.set_suggest(history.suggest);
  lifetime.set_commands(history.commands);
  out.set_scoring_label(history.label);
  out.set_hidden_runs(history.hidden + scope->journal.hiddenRollupRuns);
  const Weight weight =
      request.weight() == vf::stats::SummaryRequest::uniform ? Weight::Uniform : Weight::Effort;

  CommandTally commands;
  int sessions = 0;
  int runs = 0;
  summarizeSessions(*out.mutable_sessions(), *scope, request.filter(), weight, now, commands,
                    sessions);
  summarizeJournal(*out.mutable_journal(), *scope, request.filter(), weight, now, commands, runs);

  vf::stats::CommandSummary& table = *out.mutable_commands();
  table.set_sessions(sessions);
  table.set_runs(runs);
  table.set_user_typed_chars(commands.user.typedChars);
  table.set_optimizer_typed_chars(commands.optimizer.typedChars);
  const Filter scored = drillOutcomes(request.filter(), {Sample::scored});
  auto fillRow = [&](vf::stats::CommandRow& entry, const CommandRow& row) {
    entry.set_text(row.text);
    entry.set_user(row.user);
    entry.set_optimizer(row.optimizer);
    entry.set_ratio(row.ratio);
    entry.set_split(row.split);
    *entry.mutable_drill() = scored;
    Filter::Commands& match = *entry.mutable_drill()->add_commands();
    for (const string& variant : row.variants) match.add_tokens(variant);
  };
  const CommandComparison compared = compareCommands(commands.user, commands.optimizer);
  for (const CommandRow& row : compared.more) fillRow(*table.add_more(), row);
  for (const CommandRow& row : compared.less) fillRow(*table.add_less(), row);
  for (const CommandRow& row : compared.never) fillRow(*table.add_never(), row);
  table.set_never_cut(compared.neverCut);
  return out.SerializeAsString();
}

expected<string, string> listRuns(string_view requestBytes) {
  vf::stats::RunsRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.stats.RunsRequest");
  }
  if (request.offset() < 0 || request.limit() < 0) {
    return unexpected("offset and limit must not be negative");
  }
  const auto scope = openScope(request.scope());
  if (!scope) return unexpected(scope.error());
  const RunPage page = pageRuns(*scope, RunOrder(request.order()), request.ascending(),
                                request.offset(), request.limit());

  vf::stats::RunList out;
  out.set_total(page.total);
  out.set_rolled_up(page.rolledUp);
  out.set_rollup_runs_without_commands(page.rollupRunsWithoutCommands);
  for (const ListedRun& listed : page.rows) {
    const Run& run = *listed.run;
    vf::stats::RunRow& row = *out.add_runs();
    if (run.source == Source::Session) {
      row.mutable_id()->set_session(run.index);
    } else {
      vf::stats::RunId::Journal& id = *row.mutable_id()->mutable_journal();
      id.set_file(listed.journal);
      id.set_window_size(run.windowSize);
      id.set_last_atom(run.index);
    }
    row.set_wall_epoch(run.wallEpoch);
    row.set_user_keys(run.userKeys);
    row.set_best_seq(run.bestSeq);
    row.set_user_cost(run.userCost);
    row.set_best_cost(run.bestCost);
    row.set_outcome(Sample::Outcome(run.outcome));
    row.set_nav_only(run.navOnly);
    row.set_window_size(run.windowSize);
    row.set_analyze_ms(run.analyzeMs);
    row.set_user_display(displayKeys(run.userKeys));
    row.set_best_display(displayKeys(run.bestSeq));
  }
  return out.SerializeAsString();
}

}  // namespace Stats
