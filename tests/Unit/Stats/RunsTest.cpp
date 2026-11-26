#include "Unit/Stats/StatsFixture.h"

import vf.Stats.Commands;
import vf.Stats.Score;

using namespace std;
using vf::journal::Sample;
using vf::stats::Filter;
using vf::stats::RunsRequest;

namespace {

class StatsRunsTest : public StatsFixture {
protected:
  // Sessions and two journals with every row kind the summary has.
  void populate() {
    for (int i = 0; i < 12; i++) {
      auto s = session(8, i % 4 ? 4 : 2);
      s.set_finished_wall_epoch(NOW - 60 - i % 2 * DAY);
      s.mutable_query()->set_nav_only(i % 2 == 0);
      addCommand(s, i % 2 ? "cebar<Esc>" : "w");
      if (i % 2) s.set_typed_insert("bar");
      s.set_best_seq(i % 3 ? "e" : "cwbar<Esc>");
      append(stamp, s);
    }
    append(stamp, session(8, 0, vf::stats::Session::no_result));
    append(stamp, session(8, 0, vf::stats::Session::unbeaten));
    journal("a", 10, params, variedSamples(NOW - 60));
    journal("b", 10, params, variedSamples(NOW - 2 * DAY));
  }
};

Filter days(int64_t first, int64_t last) {
  Filter filter;
  filter.mutable_days()->set_first(first);
  filter.mutable_days()->set_last(last);
  return filter;
}

Filter byCommands(initializer_list<string> tokens, Filter::Side side = Filter::either) {
  Filter filter;
  Filter::Commands& match = *filter.add_commands();
  for (const string& token : tokens) match.add_tokens(token);
  match.set_side(side);
  return filter;
}

// The effort-weighted score of the listed runs.
double scoreOf(const vf::stats::RunList& list) {
  double weighted = 0;
  double weights = 0;
  for (const vf::stats::RunRow& row : list.runs()) {
    weighted += min(row.best_cost(), row.user_cost());
    weights += row.user_cost();
  }
  return 100 * weighted / weights;
}

// Uses of `variants` by the user and by the optimizer across the listed runs.
pair<int, int> uses(const vf::stats::RunList& list, const vf::stats::CommandRow& row) {
  Stats::CommandCounts user;
  Stats::CommandCounts optimizer;
  for (const vf::stats::RunRow& run : list.runs()) {
    Stats::countKeys(user, run.user_keys(), true);
    Stats::countKeys(optimizer, run.best_seq(), true);
  }
  pair<int, int> out;
  for (const string& variant : row.drill().commands(0).tokens()) {
    out.first += user.commands[variant];
    out.second += optimizer.commands[variant];
  }
  return out;
}

int64_t today() { return Stats::localDay(NOW); }

}  // namespace

TEST_F(StatsRunsTest, AnExplicitEmptyFilterIsNoFilter) {
  populate();
  EXPECT_EQ(summarize(Filter{}).SerializeAsString(), summarize().SerializeAsString());
}

TEST_F(StatsRunsTest, TheListHoldsTheShownSessionsAndTheChartedRuns) {
  populate();
  const auto list = runs({});
  // 14 sessions; per journal 11 counted runs of 4 (the one scored with no user cost is not).
  EXPECT_EQ(list.total(), 14 + 2 * 11);
  EXPECT_EQ(list.runs_size(), list.total());
  EXPECT_EQ(list.rolled_up(), 0);
  int sessions = 0;
  for (const auto& row : list.runs()) {
    if (row.id().has_session()) {
      sessions++;
      EXPECT_EQ(row.window_size(), 0);
      continue;
    }
    EXPECT_EQ(row.window_size(), 4);
    EXPECT_EQ(row.id().journal().window_size(), 4);
    EXPECT_TRUE(row.id().journal().file() == "a.vfj" || row.id().journal().file() == "b.vfj");
  }
  EXPECT_EQ(sessions, 14);
}

TEST_F(StatsRunsTest, SourceLeavesTheOtherOut) {
  populate();
  Filter journal;
  journal.set_source(Filter::journal);
  EXPECT_EQ(runs(journal).total(), 22);
  EXPECT_EQ(summarize(journal).sessions().sessions(), 0);
  EXPECT_EQ(summarize(journal).journal().score().spans(), summarize().journal().score().spans());

  Filter sessions;
  sessions.set_source(Filter::sessions);
  EXPECT_EQ(runs(sessions).total(), 14);
  EXPECT_EQ(summarize(sessions).journal().score().spans(), 0);
  EXPECT_EQ(summarize(sessions).sessions().sessions(), 14);
  EXPECT_EQ(summarize(sessions).journal().commands().spans(), 0);
}

TEST_F(StatsRunsTest, DaysAreInclusive) {
  populate();
  const auto list = runs(days(today(), today()));
  for (const auto& row : list.runs()) EXPECT_EQ(row.wall_epoch(), NOW - 60);
  // 8 sessions, and journal a's 4 scored runs of 4 and its 3 others.
  EXPECT_EQ(list.total(), 8 + 4 + 3);

  const auto summary = summarize(days(today() - 1, today()));
  EXPECT_EQ(summary.sessions().sessions(), 14);
  EXPECT_EQ(summary.journal().score().spans(), 9);
  EXPECT_EQ(summary.journal().days_size(), 2);
}

TEST_F(StatsRunsTest, AWindowSizeIsChartedAndLeavesSessionsOut) {
  populate();
  Filter two;
  two.set_window_size(2);
  const auto list = runs(two);
  EXPECT_EQ(list.total(), 8);
  for (const auto& row : list.runs()) EXPECT_EQ(row.window_size(), 2);

  const auto summary = summarize(two);
  EXPECT_EQ(summary.sessions().sessions(), 0);
  EXPECT_EQ(summary.journal().charted_size(), 2);
  EXPECT_EQ(summary.journal().score().spans(), 8);
  EXPECT_EQ(summary.journal().sizes(1).score().spans(), 0);
}

TEST_F(StatsRunsTest, AChartedSizeIsChartedAndKeepsSessions) {
  populate();
  charted = 2;
  const auto list = runs({});
  EXPECT_EQ(list.total(), 14 + 8);
  for (const auto& row : list.runs()) {
    if (!row.id().has_session()) EXPECT_EQ(row.window_size(), 2);
  }

  const auto summary = summarize();
  EXPECT_EQ(summary.sessions().sessions(), 14);
  EXPECT_EQ(summary.journal().charted_size(), 2);
  EXPECT_EQ(summary.journal().score().spans(), 8);
  EXPECT_GT(summary.journal().sizes(1).score().spans(), 0);
  EXPECT_EQ(summary.journal().commands().spans(), 8);
}

TEST_F(StatsRunsTest, AFilteredWindowSizeOutranksTheChartedSize) {
  populate();
  charted = 2;
  Filter eight;
  eight.set_window_size(8);
  EXPECT_EQ(summarize(eight).journal().charted_size(), 8);
  const auto list = runs(eight);
  EXPECT_EQ(list.total(), 2);
  for (const auto& row : list.runs()) EXPECT_EQ(row.window_size(), 8);
}

TEST_F(StatsRunsTest, DrillsMadeBeforeASwitchFollowTheChartedSize) {
  populate();
  const auto before = summarize();
  charted = 2;
  const auto after = summarize();
  const auto& was = before.journal();
  const auto& now = after.journal();
  ASSERT_NE(was.breakdown().nav().score().spans(), now.breakdown().nav().score().spans());

  auto expectFollows = [&](const Filter& drill, const vf::stats::Score& score) {
    const auto list = runs(drill);
    EXPECT_EQ(list.total(), score.spans());
    for (const auto& row : list.runs()) EXPECT_EQ(row.window_size(), 2);
  };
  EXPECT_EQ(was.breakdown().nav().drill().SerializeAsString(), now.breakdown().nav().drill().SerializeAsString());
  expectFollows(was.breakdown().nav().drill(), now.breakdown().nav().score());
  expectFollows(was.breakdown().edit().drill(), now.breakdown().edit().score());
  ASSERT_EQ(was.days_size(), now.days_size());
  for (int i = 0; i < was.days_size(); i++) {
    EXPECT_EQ(was.days(i).drill().SerializeAsString(), now.days(i).drill().SerializeAsString());
    EXPECT_EQ(runs(was.days(i).drill()).total(),
              summarize(was.days(i).drill()).journal().score().spans());
  }
}

TEST_F(StatsRunsTest, AChartedSizeOutsideTheWindowSizesIsAnError) {
  charted = 3;
  EXPECT_FALSE(Stats::summarize(request().SerializeAsString(), NOW));
  vf::stats::RunsRequest req;
  *req.mutable_scope() = request();
  EXPECT_FALSE(Stats::listRuns(req.SerializeAsString()));
}

TEST_F(StatsRunsTest, RowsCarryTheirKeysAsTheListShowsThem) {
  auto s = session(8, 4);
  addCommand(s, "ciwhello world<Esc>");
  addCommand(s, "j");
  s.set_best_seq("cwhi<Esc>j");
  append(stamp, s);
  const auto list = runs({});
  ASSERT_EQ(list.runs_size(), 1);
  EXPECT_EQ(list.runs(0).user_display(), "ciw⟨11⟩<Esc>j");
  EXPECT_EQ(list.runs(0).best_display(), "cw⟨2⟩<Esc>j");
}

TEST_F(StatsRunsTest, ASessionRowCarriesItsSavedFilename) {
  auto saved = session(8, 4);
  saved.set_saved_filename("a-20261126-031401");
  append(stamp, saved);
  append(stamp, session(6, 3));
  const auto list = runs({});
  ASSERT_EQ(list.runs_size(), 2);
  for (const auto& row : list.runs()) {
    EXPECT_EQ(row.saved_filename(), row.user_cost() == 8 ? "a-20261126-031401" : "");
  }
}

TEST_F(StatsRunsTest, NavAndEditPartitionBothSources) {
  populate();
  Filter nav;
  nav.set_kind(Filter::nav);
  Filter edit;
  edit.set_kind(Filter::edit);
  const auto movements = runs(nav);
  const auto edits = runs(edit);
  for (const auto& row : movements.runs()) EXPECT_TRUE(row.nav_only());
  for (const auto& row : edits.runs()) EXPECT_FALSE(row.nav_only());
  EXPECT_EQ(movements.total() + edits.total(), runs({}).total());
  EXPECT_EQ(summarize(nav).sessions().sessions(), 6);
  EXPECT_EQ(summarize(edit).sessions().sessions(), 8);
}

TEST_F(StatsRunsTest, OutcomesAreASet) {
  populate();
  Filter filter;
  filter.add_outcomes(Sample::too_large);
  filter.add_outcomes(Sample::no_result);
  const auto list = runs(filter);
  // Both journals' too_large runs and the no_result session.
  EXPECT_EQ(list.total(), 3);
  const auto summary = summarize(filter);
  EXPECT_EQ(summary.sessions().no_result(), 1);
  EXPECT_EQ(summary.sessions().sessions(), 1);
  EXPECT_EQ(summary.journal().score().spans(), 0);
  ASSERT_EQ(summary.journal().unscored_size(), 1);
  EXPECT_EQ(summary.journal().unscored(0).runs(), 2);
}

TEST_F(StatsRunsTest, SummaryAndRunListRejectUnknownFilterOutcomes) {
  for (const int outcome : {-1, Sample::Outcome_MAX + 1, 32}) {
    auto scope = request();
    scope.mutable_filter()->add_outcomes(Sample::scored);
    scope.mutable_filter()->mutable_outcomes()->Add(outcome);
    const auto summary = Stats::summarize(scope.SerializeAsString(), NOW);
    ASSERT_FALSE(summary.has_value());
    EXPECT_EQ(summary.error(), format("unknown filter outcome {}", outcome));

    RunsRequest list;
    *list.mutable_scope() = scope;
    const auto result = Stats::listRuns(list.SerializeAsString());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), summary.error());
  }
}

TEST_F(StatsRunsTest, UnknownStoredOutcomesCannotMatchAScoredFilter) {
  journal("mixed", 10, params, {sample(4, 8, 4), sample(4, 8, 0, Sample::Outcome(32))});
  Filter filter;
  filter.add_outcomes(Sample::scored);
  const auto list = runs(filter);
  ASSERT_EQ(list.runs_size(), 1);
  EXPECT_EQ(list.runs(0).outcome(), Sample::scored);
}

TEST_F(StatsRunsTest, CommandsMatchOnTheirSide) {
  populate();
  // User `w`: 6 sessions and 4 runs per journal.
  EXPECT_EQ(runs(byCommands({"w"}, Filter::user)).total(), 6 + 8);
  EXPECT_EQ(runs(byCommands({"w"}, Filter::optimizer)).total(), 0);
  // Optimizer `cw`: the 4 sessions with i % 3 == 0.
  EXPECT_EQ(runs(byCommands({"cw"}, Filter::optimizer)).total(), 4);
  // User `cw` is each journal's 4 even runs.
  EXPECT_EQ(runs(byCommands({"cw"})).total(), 4 + 8);
  EXPECT_EQ(runs(byCommands({"ce", "cw"})).total(), 4 + 6 + 8 - 2);
  EXPECT_EQ(runs(byCommands({"never used"})).total(), 0);

  Filter both = byCommands({"w"}, Filter::user);
  *both.add_commands() = byCommands({"cw"}, Filter::optimizer).commands(0);
  // Sessions 0 and 6.
  EXPECT_EQ(runs(both).total(), 2);
}

TEST_F(StatsRunsTest, OverTypedListsTheRunsThatTypedMoreAndTypedOrdersByTheExcess) {
  populate();
  Filter sessions;
  sessions.set_source(Filter::sessions);
  Filter overTyped = sessions;
  overTyped.set_over_typed(true);
  // The odd sessions type `bar`; of those, 3 and 9 are answered with `cwbar<Esc>`.
  const auto list = runs(overTyped);
  EXPECT_EQ(list.total(), 4);
  for (const auto& row : list.runs()) {
    EXPECT_EQ(row.user_typed_chars(), 3);
    EXPECT_EQ(row.best_typed_chars(), 0);
  }

  const auto drill = summarize().sessions().commands().typed_drill();
  EXPECT_EQ(runs(drill).total(), list.total());
  EXPECT_TRUE(drill.over_typed());
  ASSERT_EQ(drill.outcomes_size(), 1);
  EXPECT_EQ(drill.outcomes(0), Sample::scored);

  const auto ordered = runs(sessions, RunsRequest::typed);
  ASSERT_EQ(ordered.runs_size(), 14);
  for (int i = 0; i < 4; i++) EXPECT_EQ(ordered.runs(i).user_typed_chars(), 3) << i;
  for (int i = 0; i < 4; i++) EXPECT_EQ(ordered.runs(i).best_typed_chars(), 0) << i;
  // The session with no result has no excess, so it is last.
  EXPECT_EQ(ordered.runs(13).outcome(), Sample::no_result);
}

TEST_F(StatsRunsTest, EveryDrillFilterListsWhatItsRowCounted) {
  populate();
  for (const Filter& base : {Filter{}, days(today() - 1, today())}) {
    SCOPED_TRACE(base.has_days() ? "under two days" : "unfiltered");
    vf::stats::Summary summary = summarize(base);
    const auto& sessions = summary.sessions();
    const auto& journal = summary.journal();

    EXPECT_EQ(runs(sessions.no_result_drill()).total(), sessions.no_result());
    EXPECT_EQ(runs(sessions.unbeaten_drill()).total(), sessions.unbeaten());
    EXPECT_EQ(runs(journal.unbeaten_drill()).total(), journal.unbeaten());
    ASSERT_GT(journal.unscored_size(), 0);
    int unscored = 0;
    for (const auto& entry : journal.unscored()) {
      unscored += entry.runs();
      EXPECT_EQ(runs(entry.drill()).total(), entry.runs()) << entry.outcome();
    }

    EXPECT_EQ(runs(journal.unscored_drill()).total(), unscored);

    auto expectScore = [&](const Filter& drill, const vf::stats::Score& score) {
      const auto list = runs(drill);
      EXPECT_EQ(list.total(), score.spans());
      if (score.spans() > 0) EXPECT_NEAR(scoreOf(list), score.score(), 1e-9);
    };
    expectScore(journal.breakdown().nav().drill(), journal.breakdown().nav().score());
    expectScore(journal.breakdown().edit().drill(), journal.breakdown().edit().score());
    for (const auto& size : journal.sizes()) {
      charted = size.size();
      Filter scoring = base;
      scoring.set_source(Filter::journal);
      scoring.add_outcomes(Sample::scored);
      scoring.add_outcomes(Sample::unbeaten);
      expectScore(scoring, size.score());
    }
    charted = 0;

    ASSERT_GT(sessions.days_size(), 0);
    ASSERT_GT(journal.days_size(), 0);
    for (const auto* days : {&sessions.days(), &journal.days()}) {
      for (const auto& day : *days) {
        const auto list = runs(day.drill());
        EXPECT_GT(list.total(), 0);
        EXPECT_NEAR(scoreOf(list), day.score(), 1e-9) << day.days_ago();
        EXPECT_EQ(runs(day.drill()).total(), summarize(day.drill()).sessions().score().spans() +
                                                 summarize(day.drill()).journal().score().spans());
      }
    }

    ASSERT_GT(sessions.commands().more_size(), 0);
    // One journal's runs are too few for a row.
    if (!base.has_days()) ASSERT_GT(journal.commands().less_size(), 0);
    for (const auto* commands : {&sessions.commands(), &journal.commands()}) {
      for (const auto* table : {&commands->more(), &commands->less(), &commands->never()}) {
        for (const auto& row : *table) {
          const auto [user, optimizer] = uses(runs(row.drill()), row);
          EXPECT_EQ(user, row.user()) << row.text();
          EXPECT_EQ(optimizer, row.optimizer()) << row.text();
        }
      }
    }
  }
}

TEST_F(StatsRunsTest, UnbeatenTagsCountTheRunsHoldingTheirCommands) {
  auto unbeaten = [](int lastAtom, initializer_list<string> commands) {
    auto s = sample(4, 10, 0, Sample::unbeaten);
    s.set_last_atom(lastAtom);
    for (const string& keys : commands) addCommand(s, keys);
    return s;
  };
  auto scored = sample(4, 10, 5);
  scored.set_last_atom(3);
  addCommand(scored, "p");
  journal("a", 10, params, {unbeaten(0, {"p"}), unbeaten(1, {"u", "P"}), unbeaten(2, {"w"}), scored});
  auto undone = session(8, 0, vf::stats::Session::unbeaten);
  addCommand(undone, "<C-r>");
  append(stamp, undone);

  const auto summary = summarize();
  const auto& tags = summary.journal().unbeaten_tags();
  ASSERT_EQ(tags.size(), 3);
  EXPECT_EQ(tags[0].name(), "paste");
  EXPECT_EQ(tags[0].runs(), 2);
  EXPECT_EQ(tags[1].name(), "undo or redo");
  EXPECT_EQ(tags[1].runs(), 1);
  EXPECT_EQ(tags[2].name(), "other");
  EXPECT_EQ(tags[2].runs(), 1);
  for (const auto& tag : tags) EXPECT_EQ(runs(tag.drill()).total(), tag.runs()) << tag.name();
  const auto other = runs(tags[2].drill());
  ASSERT_EQ(other.runs_size(), 1);
  EXPECT_EQ(other.runs(0).id().journal().last_atom(), 2);
  EXPECT_TRUE(other.runs(0).tag().empty());
  EXPECT_EQ(runs(tags[1].drill()).runs(0).tag(), "paste, undo or redo");

  Filter scoredOnly;
  scoredOnly.add_outcomes(Sample::scored);
  EXPECT_EQ(summarize(scoredOnly).journal().unbeaten_tags_size(), 0);

  const auto& sessionTags = summary.sessions().unbeaten_tags();
  ASSERT_EQ(sessionTags.size(), 1);
  EXPECT_EQ(sessionTags[0].name(), "undo or redo");
  EXPECT_EQ(runs(sessionTags[0].drill()).total(), 1);
}

TEST_F(StatsRunsTest, UserBetterTagsIncludeScreenLineMotionsAcrossBufferLines) {
  auto wrapped = sample(4, 3, 6);
  addCommand(wrapped, "2gk");
  wrapped.add_within_line(0);
  addCommand(wrapped, "p");
  auto crossed = sample(4, 3, 6);
  crossed.set_last_atom(4);
  addCommand(crossed, "gj");
  crossed.set_best_seq("p");
  auto tied = wrapped;
  tied.set_last_atom(8);
  tied.set_best_cost(tied.user_cost());
  auto ordinary = sample(4, 3, 6);
  ordinary.set_last_atom(12);
  addCommand(ordinary, "j");
  ordinary.set_best_seq("gk");
  journal("a", 16, params, {wrapped, crossed, tied, ordinary});
  auto undone = session(2, 4);
  addCommand(undone, "u");
  append(stamp, undone);

  const auto summary = summarize();
  EXPECT_EQ(summary.journal().beats(), 3);
  const auto& tags = summary.journal().beats_tags();
  ASSERT_EQ(tags.size(), 3);
  EXPECT_EQ(tags[0].name(), "screen-line movement (gj/gk)");
  EXPECT_EQ(tags[0].runs(), 2);
  EXPECT_EQ(tags[1].name(), "paste");
  EXPECT_EQ(tags[1].runs(), 1);
  EXPECT_EQ(tags[2].name(), "other");
  EXPECT_EQ(tags[2].runs(), 1);
  for (const auto& tag : tags) {
    const auto list = runs(tag.drill());
    ASSERT_EQ(list.runs_size(), tag.runs());
    for (const auto& row : list.runs()) {
      if (tag.name() == "other") {
        EXPECT_EQ(row.id().journal().last_atom(), 12);
      } else {
        EXPECT_TRUE(row.tag().contains(tag.name()));
      }
    }
  }
  for (bool ascending : {false, true}) {
    const auto list = runs({}, RunsRequest::tag, 0, 5, false, ascending);
    ASSERT_EQ(list.runs_size(), 5);
    EXPECT_EQ(list.runs(ascending ? 0 : 2).tag(), "screen-line movement (gj/gk)");
    EXPECT_EQ(list.runs(1).tag(), "screen-line movement (gj/gk), paste");
    EXPECT_EQ(list.runs(ascending ? 2 : 0).tag(), "undo or redo");
    EXPECT_TRUE(list.runs(3).tag().empty());
    EXPECT_TRUE(list.runs(4).tag().empty());
  }
  const auto& sessions = summary.sessions().beats_tags();
  ASSERT_EQ(sessions.size(), 1);
  EXPECT_EQ(sessions[0].name(), "undo or redo");
  EXPECT_EQ(runs(sessions[0].drill()).total(), 1);
}

TEST_F(StatsRunsTest, AMergedRowDrillsIntoEitherVariant) {
  populate();
  const auto summary = summarize();
  const auto& more = summary.sessions().commands().more();
  const auto merged = ranges::find(more, "ce|cw", &vf::stats::CommandRow::text);
  ASSERT_NE(merged, more.end());
  EXPECT_EQ(merged->split(), "ce 6");
  const auto list = runs(merged->drill());
  int ce = 0;
  int cw = 0;
  for (const auto& row : list.runs()) {
    EXPECT_EQ(row.outcome(), Sample::scored);
    EXPECT_TRUE(row.id().has_session());
    ce += row.user_keys().starts_with("ce");
    cw += row.best_seq().starts_with("cw");
  }
  EXPECT_EQ(ce, 6);
  EXPECT_EQ(cw, 4);
  // The optimizer's `cw` answers two of the `ce` sessions.
  EXPECT_EQ(list.total(), 6 + 4 - 2);
}

TEST_F(StatsRunsTest, RollupsCountButAreNotListedAndCommandsLeaveThemOut) {
  populate();
  journal("old", 10, params, variedSamples(NOW - 200 * DAY));
  age("old");
  ASSERT_EQ(rollUp(params), 1);

  const auto list = runs({});
  EXPECT_EQ(list.rolled_up(), 11);
  EXPECT_EQ(list.total(), 14 + 22 + 11);
  EXPECT_EQ(list.runs_size(), 14 + 22);
  EXPECT_EQ(list.rollup_runs_without_commands(), 0);

  const Filter w = byCommands({"w"});
  const auto byW = runs(w);
  EXPECT_EQ(byW.rolled_up(), 0);
  EXPECT_EQ(byW.rollup_runs_without_commands(), 11);
  EXPECT_EQ(byW.total(), byW.runs_size());
  EXPECT_EQ(summarize(w).journal().rollup_runs_without_commands(), 0);
  auto wide = request();
  *wide.mutable_filter() = w;
  wide.set_chart_unit(vf::stats::weeks);
  EXPECT_EQ(summarize(wide).journal().rollup_runs_without_commands(), 11);
  EXPECT_EQ(summarize().journal().rollup_runs_without_commands(), 0);

  Filter recentW = w;
  *recentW.mutable_days() = days(today(), today()).days();
  EXPECT_EQ(runs(recentW).rollup_runs_without_commands(), 0);
  EXPECT_EQ(runs(days(today() - 200, today() - 200)).rolled_up(), 7);
}

TEST_F(StatsRunsTest, OrdersAndTheirTieBreaks) {
  auto run = [](int lastAtom, double user, double best, int64_t ago, double ms) {
    auto s = sample(4, user, best);
    s.set_last_atom(lastAtom);
    s.set_wall_epoch(NOW - ago);
    s.set_analyze_ms(ms);
    return s;
  };
  auto large = sample(4, 50, 0, Sample::too_large);
  large.set_last_atom(9);
  journal("a", 10, params,
          {run(1, 10, 9, 5, 3), run(2, 10, 2, 9, 1), run(3, 10, 6, 1, 2), run(4, 10, 6, 1, 2), large});
  journal("b", 10, params, {run(3, 10, 6, 1, 2)});

  auto order = [&](RunsRequest::Order by, bool ascending = false) {
    vector<string> out;
    for (const auto& row : runs({}, by, 0, 1000, false, ascending).runs()) {
      out.push_back(row.id().journal().file() + ":" + to_string(row.id().journal().last_atom()));
    }
    return out;
  };
  EXPECT_EQ(order(RunsRequest::worst),
            (vector<string>{"a.vfj:2", "a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:1", "a.vfj:9"}));
  EXPECT_EQ(order(RunsRequest::newest),
            (vector<string>{"a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:1", "a.vfj:2", "a.vfj:9"}));
  EXPECT_EQ(order(RunsRequest::slowest),
            (vector<string>{"a.vfj:1", "a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:2", "a.vfj:9"}));
  EXPECT_EQ(order(RunsRequest::best_cost),
            (vector<string>{"a.vfj:1", "a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:2", "a.vfj:9"}));
  EXPECT_EQ(order(RunsRequest::saved),
            (vector<string>{"a.vfj:2", "a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:1", "a.vfj:9"}));
  // The unscored run has nothing saved, so it is last in either direction.
  EXPECT_EQ(order(RunsRequest::saved, true),
            (vector<string>{"a.vfj:1", "a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:2", "a.vfj:9"}));
  EXPECT_EQ(order(RunsRequest::user_cost, true),
            (vector<string>{"a.vfj:3", "a.vfj:4", "b.vfj:3", "a.vfj:1", "a.vfj:2", "a.vfj:9"}));
}

TEST_F(StatsRunsTest, PagesAreSlicesOfTheWholeOrder) {
  populate();
  const auto all = runs({}, RunsRequest::newest);
  const auto page = runs({}, RunsRequest::newest, 5, 7);
  EXPECT_EQ(page.total(), all.total());
  ASSERT_EQ(page.runs_size(), 7);
  for (int i = 0; i < 7; i++) {
    EXPECT_EQ(page.runs(i).SerializeAsString(), all.runs(5 + i).SerializeAsString());
  }
  EXPECT_EQ(runs({}, RunsRequest::newest, all.total() - 2, 7).runs_size(), 2);
  const auto past = runs({}, RunsRequest::newest, all.total() + 3, 7);
  EXPECT_EQ(past.runs_size(), 0);
  EXPECT_EQ(past.total(), all.total());
  EXPECT_EQ(runs({}, RunsRequest::newest, 0, 0).runs_size(), 0);

  vf::stats::RunsRequest negative;
  *negative.mutable_scope() = request();
  negative.set_offset(-1);
  EXPECT_FALSE(Stats::listRuns(negative.SerializeAsString()));
}

TEST_F(StatsRunsTest, JournalColumnsSortNumericallyBeforePagingWithSessionsLast) {
  auto first = sample(4, 8, 4);
  first.set_last_atom(2);
  auto second = first;
  second.set_last_atom(10);
  journal("m", 10, params, {first, second});
  append(stamp, session(8, 4));

  for (bool ascending : {false, true}) {
    const auto page = runs({}, RunsRequest::last_atom, 1, 1, false, ascending);
    ASSERT_EQ(page.runs_size(), 1);
    EXPECT_EQ(page.runs(0).id().journal().last_atom(), ascending ? 10 : 2);
    for (auto order : {RunsRequest::last_atom, RunsRequest::window_size}) {
      const auto list = runs({}, order, 0, 3, false, ascending);
      ASSERT_EQ(list.runs_size(), 3);
      EXPECT_EQ(list.runs(0).window_size(), 4);
      EXPECT_EQ(list.runs(1).window_size(), 4);
      EXPECT_TRUE(list.runs(2).id().has_session());
    }
  }
}

TEST_F(StatsRunsTest, SessionsComeFromEveryStampUnlessCurrentOnlyAndRunsFromTheCurrentParams) {
  vf::stats::Stamp other = stamp;
  other.set_slice_padding(9);
  append(other, session(10, 5));
  append(stamp, session(10, 6));
  append(other, session(10, 7));
  vf::journal::SampleParams stale = params;
  stale.set_deadline_ms(1);
  journal("old", 10, stale, {sample(4, 10, 1)});
  journal("new", 10, params, {sample(4, 10, 5)});

  auto ids = [](const vf::stats::RunList& list) {
    vector<string> out;
    for (const auto& row : list.runs()) {
      out.push_back(row.id().has_session() ? to_string(row.id().session())
                                           : row.id().journal().file());
    }
    ranges::sort(out);
    return out;
  };
  EXPECT_EQ(ids(runs({})), (vector<string>{"0", "1", "2", "new.vfj"}));
  EXPECT_EQ(ids(runs({}, RunsRequest::newest, 0, 10, true)), (vector<string>{"1", "new.vfj"}));
}
