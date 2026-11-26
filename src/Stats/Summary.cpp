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

constexpr int MAX_CHART_BUCKETS = 30;
constexpr int64_t DAY_SECONDS = 86400;

Selection selectionOf(const Filter& filter) {
  Selection selection{
      .journal = filter.source() != Filter::sessions,
      .sessions = filter.source() != Filter::journal && filter.window_size() == 0 &&
                  filter.kind() == Filter::any && !filter.has_query_size(),
      .windowSize = filter.window_size(),
      .change = filter.kind() == Filter::nav    ? Change::Nav
                : filter.kind() == Filter::edit ? Change::Edit
                                                : Change::Any,
  };
  if (filter.has_epoch_seconds()) {
    selection.epochSeconds.emplace(filter.epoch_seconds().first(), filter.epoch_seconds().last());
  }
  if (filter.has_days()) {
    selection.firstDay = filter.days().first();
    selection.lastDay = filter.days().last();
  }
  selection.overTyped = filter.over_typed();
  selection.beats = filter.beats();
  selection.timed = filter.timed();
  if (filter.has_query_size()) {
    selection.querySize.emplace(filter.query_size().first(), filter.query_size().last());
  }
  for (const int outcome : filter.outcomes()) {
    if (!ranges::contains(selection.outcomes, outcome)) selection.outcomes.push_back(outcome);
  }
  for (const Filter::Commands& commands : filter.commands()) {
    CommandMatch& match = selection.commands.emplace_back();
    match.side = commands.side() == Filter::user        ? Side::User
                 : commands.side() == Filter::optimizer ? Side::Optimizer
                                                        : Side::Either;
    match.absent = commands.absent();
    for (const string& token : commands.tokens()) {
      const int id = internedId(token);
      if (id >= 0) match.commands.push_back(id);
    }
  }
  return selection;
}

// Shared by the summary and the run list, so a row's drill filter lists what it counted.
expected<Scope, string> openScope(const vf::stats::SummaryRequest& request) {
  for (const int outcome : request.filter().outcomes()) {
    if (!Sample::Outcome_IsValid(outcome)) return unexpected(format("unknown filter outcome {}", outcome));
  }
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
  scope.journal = readJournalRuns(request.journal_dir(), params.SerializeAsString(),
                                  request.current_only() ? RollupScope::Current : RollupScope::All);
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

// The listable runs `filter` selects.
int listedRuns(const Scope& scope, const Filter& filter) {
  Selection selection = selectionOf(filter);
  selection.windowSize = scope.charted;
  int runs = 0;
  for (const Run& session : scope.sessions.runs) runs += matches(selection, session);
  for (const SampledRuns& sampled : scope.journal.sampled) {
    for (const Run& run : sampled.runs) runs += matches(selection, run);
  }
  return runs;
}

struct UnmodeledCommands {
  string_view name;
  span<const string_view> commands;
};

constexpr array<string_view, 4> PUT_COMMANDS = {"p", "P", "gp", "gP"};
constexpr array<string_view, 3> UNDO_COMMANDS = {"u", "U", "<C-r>"};
// Only the bare commands: a count or register in front goes untagged.
constexpr array<UnmodeledCommands, 2> UNBEATEN_TAGS = {{
    {"paste", PUT_COMMANDS},
    {"undo or redo", UNDO_COMMANDS},
}};

// Needs `out.unbeaten_drill()`.
template <typename Section>
void fillUnbeatenTags(Section& out, const Scope& scope) {
  if (out.unbeaten() == 0) return;
  auto add = [&](string_view name, const Filter& drill) {
    const int runs = listedRuns(scope, drill);
    if (runs == 0) return;
    vf::stats::UnbeatenTag& entry = *out.add_unbeaten_tags();
    entry.set_name(string(name));
    entry.set_runs(runs);
    *entry.mutable_drill() = drill;
  };
  Filter untagged = out.unbeaten_drill();
  Filter::Commands& none = *untagged.add_commands();
  none.set_side(Filter::user);
  none.set_absent(true);
  for (const UnmodeledCommands& tag : UNBEATEN_TAGS) {
    Filter drill = out.unbeaten_drill();
    Filter::Commands& held = *drill.add_commands();
    held.set_side(Filter::user);
    for (const string_view command : tag.commands) {
      held.add_tokens(string(command));
      none.add_tokens(string(command));
    }
    add(tag.name, drill);
  }
  add("other", untagged);
}

struct Chart {
  vf::stats::ChartUnit unit;
  int64_t now;
  int64_t bucketSize;
  int buckets;

  bool subday() const { return unit == vf::stats::minutes || unit == vf::stats::hours; }
  int64_t origin() const { return unit == vf::stats::weeks ? -3 : 0; }
  int64_t width() const {
    switch (unit) {
      case vf::stats::minutes: return 60 * bucketSize;
      case vf::stats::hours: return 3600 * bucketSize;
      case vf::stats::weeks: return 7 * bucketSize;
      default: return bucketSize;
    }
  }
  int64_t bucket(int64_t day, int64_t epoch) const {
    const auto offset = (subday() ? epoch : day) - origin();
    return offset / width() - (offset % width() < 0);
  }
  int64_t begin(int64_t bucket) const { return origin() + bucket * width(); }
};

struct Scores {
  explicit Scores(Chart chart)
      : current(chart.bucket(localDay(chart.now), chart.now)), bucketCount(chart.buckets) {}

  ScoreTotals all;
  array<ScoreTotals, MAX_CHART_BUCKETS> buckets;
  int64_t current;
  int bucketCount;

  int64_t oldest() const { return current - bucketCount + 1; }

  ScoreTotals* at(int64_t bucket) {
    const int64_t ago = current - bucket;
    return ago >= 0 && ago < bucketCount ? &buckets[ago] : nullptr;
  }

  void add(int64_t bucket, double userCost, double bestCost, Weight weight) {
    all.add(userCost, bestCost, weight);
    if (auto* totals = at(bucket)) totals->add(userCost, bestCost, weight);
  }
};

void fillScore(vf::stats::Score& out, const ScoreTotals& totals) {
  out.set_spans(totals.spans);
  if (totals.spans > 0) out.set_score(totals.score());
}

// `base` narrowed to the chart's buckets `first` through `last`.
Filter drillBuckets(const Filter& base, Chart chart, int64_t first, int64_t last) {
  Filter out = base;
  const bool limited = chart.subday() ? base.has_epoch_seconds() : base.has_days();
  auto& range = chart.subday() ? *out.mutable_epoch_seconds() : *out.mutable_days();
  const auto begin = chart.begin(first);
  const auto end = chart.begin(last) + chart.width() - 1;
  range.set_first(limited ? max(begin, range.first()) : begin);
  range.set_last(limited ? min(end, range.last()) : end);
  return out;
}

// `drill` selects the runs the score counts.
template <typename Section>
void fillChart(Section& out, const Scores& scores, Chart chart, const Filter& drill) {
  ScoreTotals visible;
  for (int ago = scores.bucketCount - 1; ago >= 0; --ago) {
    const auto& totals = scores.buckets[ago];
    if (totals.spans == 0) continue;
    const auto bucket = scores.current - ago;
    vf::stats::DayScore& entry = *out.add_days();
    entry.set_days_ago(ago);
    entry.set_score(totals.score());
    entry.set_spans(totals.spans);
    visible.merge(totals);
    *entry.mutable_drill() = drillBuckets(drill, chart, bucket, bucket);
  }
  fillScore(*out.mutable_chart_score(), visible);
}

// Nearest rank.
double percentile(const vector<double>& sorted, int p) {
  const size_t rank = size_t(ceil(p / 100.0 * sorted.size()));
  return sorted[clamp<size_t>(rank, 1, sorted.size()) - 1];
}

void fillTiming(vf::stats::Timing& out, vector<double>& times) {
  if (times.empty()) return;
  ranges::sort(times);
  out.set_n(ssize(times));
  out.set_p50(percentile(times, 50));
  out.set_p95(percentile(times, 95));
  out.set_p99(percentile(times, 99));
  out.set_max(times.back());
}

constexpr array QUERY_SIZE_ENDS{64, 128, 256, 512, 1024, 2048, numeric_limits<int>::max()};

struct BucketTotals {
  ScoreTotals score;
  vector<double> times;
  int deadlineHit = 0;
};

void fillTimingBuckets(google::protobuf::RepeatedPtrField<vf::stats::TimingBucket>& out,
                       span<BucketTotals> totals, span<const int> ends, const Filter& base,
                       Filter::Kind kind) {
  int first = 1;
  for (size_t i = 0; i < ends.size(); i++) {
    auto& bucket = *out.Add();
    const int last = ends[i];
    bucket.set_first(first);
    if (i + 1 < ends.size()) bucket.set_last(last);
    bucket.set_deadline_hit(totals[i].deadlineHit);
    Filter sized = base;
    sized.set_kind(kind);
    auto& range = *sized.mutable_query_size();
    range.set_first(base.has_query_size() ? max<int64_t>(first, base.query_size().first()) : first);
    range.set_last(base.has_query_size() ? min<int64_t>(last, base.query_size().last()) : last);
    fillScore(*bucket.mutable_score(), totals[i].score);
    if (totals[i].score.spans != 0) {
      *bucket.mutable_drill() = drillOutcomes(sized, {Sample::scored, Sample::unbeaten});
    }
    auto& timing = *bucket.mutable_timing();
    fillTiming(timing, totals[i].times);
    if (timing.n() != 0) {
      *timing.mutable_drill() = sized;
      timing.mutable_drill()->set_timed(true);
    }
    if (i + 1 < ends.size()) first = last + 1;
  }
}

constexpr array<Sample::Outcome, 4> UNSCORED = {Sample::too_large, Sample::no_result,
                                                Sample::unsupported, Sample::crashed};

struct CommandTally {
  CommandCounts user;
  CommandCounts optimizer;
  int spans = 0;

  template <typename Counted>
  void add(const Counted& counted, int runs) {
    user.merge(counted.user);
    optimizer.merge(counted.optimizer);
    spans += runs;
  }
};

// `scored` selects the spans tallied.
void fillCommands(vf::stats::CommandSummary& out, const CommandTally& tally, const Filter& scored) {
  out.set_spans(tally.spans);
  out.set_user_typed_chars(tally.user.typedChars);
  out.set_optimizer_typed_chars(tally.optimizer.typedChars);
  *out.mutable_typed_drill() = scored;
  out.mutable_typed_drill()->set_over_typed(true);
  auto fillRow = [&](vf::stats::CommandRow& entry, const CommandRow& row) {
    entry.set_text(row.text);
    entry.set_user(row.user);
    entry.set_optimizer(row.optimizer);
    entry.set_user_share(row.userShare);
    entry.set_optimizer_share(row.optimizerShare);
    entry.set_ratio(row.ratio);
    entry.set_split(row.split);
    *entry.mutable_drill() = scored;
    Filter::Commands& match = *entry.mutable_drill()->add_commands();
    for (const string& variant : row.variants) match.add_tokens(variant);
  };
  const CommandComparison compared = compareCommands(tally.user, tally.optimizer);
  for (const CommandRow& row : compared.more) fillRow(*out.add_more(), row);
  for (const CommandRow& row : compared.less) fillRow(*out.add_less(), row);
  for (const CommandRow& row : compared.never) fillRow(*out.add_never(), row);
}

void summarizeSessions(vf::stats::SessionSummary& out, const Scope& scope, const Filter& base,
                       Weight weight, Chart chart) {
  const Filter sessions = drillSource(base, Filter::sessions);
  *out.mutable_no_result_drill() = drillOutcomes(sessions, {Sample::no_result});
  *out.mutable_unbeaten_drill() = drillOutcomes(sessions, {Sample::unbeaten});
  *out.mutable_beats_drill() = drillOutcomes(sessions, {Sample::scored});
  out.mutable_beats_drill()->set_beats(true);

  Scores scores{chart};
  CommandTally commands;
  vector<double> times;
  const int64_t firstTimed = chart.now - 30 * DAY_SECONDS + 1;
  for (const Run& session : scope.sessions.runs) {
    if (!matches(scope.selection, session)) continue;
    out.set_sessions(out.sessions() + 1);
    const double user = session.userCost;
    const int64_t bucket = chart.bucket(session.day, session.wallEpoch);
    switch (session.outcome) {
      case Sample::no_result:
        out.set_no_result(out.no_result() + 1);
        break;
      case Sample::unbeaten:
        out.set_unbeaten(out.unbeaten() + 1);
        if (user > 0) scores.add(bucket, user, user, weight);
        break;
      default:
        if (user < session.bestCost) out.set_beats(out.beats() + 1);
        if (user > 0) scores.add(bucket, user, session.bestCost, weight);
        if (scores.at(bucket)) commands.add(session, 1);
    }
    if (session.wallEpoch >= firstTimed && session.wallEpoch <= chart.now && session.hasTiming()) {
      times.push_back(session.analyzeMs);
    }
  }
  fillScore(*out.mutable_score(), scores.all);
  fillChart(out, scores, chart, drillOutcomes(sessions, {Sample::scored, Sample::unbeaten}));
  fillCommands(*out.mutable_commands(), commands,
               drillBuckets(drillOutcomes(sessions, {Sample::scored}), chart, scores.oldest(), scores.current));
  fillUnbeatenTags(out, scope);

  if (times.empty()) return;
  vf::stats::Timing& timing = *out.mutable_timing();
  fillTiming(timing, times);
  auto& drill = *timing.mutable_drill();
  drill = sessions;
  drill.set_timed(true);
  auto& range = *drill.mutable_epoch_seconds();
  range.set_first(base.has_epoch_seconds() ? max(firstTimed, base.epoch_seconds().first()) : firstTimed);
  range.set_last(base.has_epoch_seconds() ? min(chart.now, base.epoch_seconds().last()) : chart.now);
}

// The runs of the samples made under the request's params and of the rollups
// in scope. A journal whose samples were made under other params counts as
// unscored until `vimfy_journal` redoes it.
void summarizeJournal(vf::stats::JournalSummary& out, const Scope& scope, const Filter& base,
                      Weight weight, Chart chart) {
  const int charted = scope.charted;
  out.set_charted_size(charted);

  map<int, ScoreTotals> bySize;
  Scores scores{chart};
  CommandTally commands;
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

    scores.all.merge(score);
    (key.navOnly ? nav : edit).merge(score);
    out.set_deadline_hit(out.deadline_hit() + totals.deadlineHit);
    const bool unbeaten = key.outcome == Sample::unbeaten;
    if (unbeaten) out.set_unbeaten(out.unbeaten() + totals.runs);
    if (chart.subday()) return;
    auto* bucket = scores.at(chart.bucket(key.day, 0));
    if (!bucket) return;
    bucket->merge(score);
    if (!unbeaten) commands.add(totals, totals.runs);
  });

  array<BucketTotals, QUERY_SIZE_ENDS.size()> navTiming;
  array<BucketTotals, QUERY_SIZE_ENDS.size()> editTiming;
  const Selection chartedRuns = chartedSelection(scope);
  for (const auto& sampled : scope.journal.sampled) {
    for (const Run& run : sampled.runs) {
      if (!matches(chartedRuns, run)) continue;
      const bool scoring = isScoring(run.outcome) && isCounted(run);
      if (chart.subday() && scoring) {
        if (auto* bucket = scores.at(chart.bucket(run.day, run.wallEpoch))) {
          bucket->add(run.userCost, run.bestCost, weight);
          if (run.outcome != Sample::unbeaten) commands.add(run, 1);
        }
      }
      if (run.querySize <= 0) continue;
      const size_t index =
          ranges::lower_bound(QUERY_SIZE_ENDS, run.querySize) - QUERY_SIZE_ENDS.begin();
      auto& bucket = run.navOnly ? navTiming[index] : editTiming[index];
      if (scoring) bucket.score.add(run.userCost, run.bestCost, weight);
      if (!run.hasTiming()) continue;
      bucket.times.push_back(run.analyzeMs);
      bucket.deadlineHit += run.deadlineHit;
    }
  }
  const JournalRuns& journal = scope.journal;
  out.set_present(journal.present);
  out.set_journal_bytes(journal.journalBytes);
  out.set_scored_bytes(journal.scoredBytes);
  if (scope.selection.readsRuns()) {
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
  fillTimingBuckets(*out.mutable_nav_timing(), navTiming, QUERY_SIZE_ENDS, runs, Filter::nav);
  fillTimingBuckets(*out.mutable_edit_timing(), editTiming, QUERY_SIZE_ENDS, runs, Filter::edit);
  fillChart(out, scores, chart, drillOutcomes(runs, {Sample::scored, Sample::unbeaten}));
  fillCommands(*out.mutable_commands(), commands,
               drillBuckets(drillOutcomes(runs, {Sample::scored}), chart, scores.oldest(), scores.current));
  *out.mutable_unbeaten_drill() = drillOutcomes(runs, {Sample::unbeaten});
  fillUnbeatenTags(out, scope);
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
  if (request.chart_buckets() > MAX_CHART_BUCKETS) {
    return unexpected(format("chart_buckets exceeds {}", MAX_CHART_BUCKETS));
  }
  const Chart chart{
      .unit = request.chart_unit(),
      .now = now,
      .bucketSize = max(1u, request.chart_bucket_size()),
      .buckets = request.chart_buckets() == 0 ? MAX_CHART_BUCKETS : int(request.chart_buckets()),
  };
  const auto scope = openScope(request);
  if (!scope) return unexpected(scope.error());
  const SessionHistory& history = scope->sessions;
  vf::stats::Summary out;
  out.set_chart_unit(chart.unit);
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

  summarizeSessions(*out.mutable_sessions(), *scope, request.filter(), weight, chart);
  summarizeJournal(*out.mutable_journal(), *scope, request.filter(), weight, chart);
  return out.SerializeAsString();
}

expected<string, string> journalCoverage(string_view requestBytes) {
  vf::journal::SampleRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.journal.SampleRequest");
  }
  stampSampleParams(*request.mutable_params());
  const auto journal = readJournalRuns(request.journal_dir(), request.params().SerializeAsString(),
                                       RollupScope::None);
  vf::stats::JournalCoverage coverage;
  coverage.set_scored_bytes(journal.scoredBytes);
  coverage.set_journal_bytes(journal.journalBytes);
  return coverage.SerializeAsString();
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
    row.set_saved_filename(run.savedFilename);
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
    row.set_user_typed_chars(run.user.typedChars);
    row.set_best_typed_chars(run.optimizer.typedChars);
  }
  return out.SerializeAsString();
}

}  // namespace Stats
