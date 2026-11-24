#include "Unit/Stats/StatsFixture.h"

import vf.Versions;

using namespace std;
namespace fs = std::filesystem;

namespace {

class StatsSummaryTest : public StatsFixture {
protected:
  // The journal and command sections, less how much journal is on disk.
  string journalPart(vf::stats::SummaryRequest::Weight weight, const string& journalDir = "") {
    vf::stats::Summary s = summarize(false, weight, journalDir);
    s.mutable_journal()->clear_scored_bytes();
    s.mutable_journal()->clear_journal_bytes();
    return s.journal().SerializeAsString() + s.commands().SerializeAsString();
  }
};

}  // namespace

TEST_F(StatsSummaryTest, EmptyLogStillHasTheCurrentScoring) {
  const auto summary = summarize();
  EXPECT_EQ(summary.lifetime().sessions(), 0);
  EXPECT_TRUE(summary.scoring_label().starts_with(format("scoring {}·", SCORING_VERSION)));
  EXPECT_EQ(summary.hidden_runs(), 0);
  EXPECT_EQ(summary.sessions().score().spans(), 0);
  EXPECT_FALSE(summary.sessions().has_timing());
}

TEST_F(StatsSummaryTest, EveryStampsSessionsCountUnlessCurrentOnly) {
  vf::stats::Stamp other = stamp;
  other.set_slice_padding(9);
  append(other, session(10, 1));
  append(stamp, session(10, 5));
  append(stamp, session(10, 10));

  const auto all = summarize();
  EXPECT_EQ(all.lifetime().sessions(), 3);
  EXPECT_EQ(all.sessions().sessions(), 3);
  EXPECT_EQ(all.hidden_runs(), 0);
  EXPECT_NEAR(all.sessions().score().score(), 100.0 * 16 / 30, 1e-9);

  const auto current = summarize(true);
  EXPECT_EQ(current.lifetime().sessions(), 3);
  EXPECT_EQ(current.sessions().sessions(), 2);
  EXPECT_EQ(current.hidden_runs(), 1);
  EXPECT_DOUBLE_EQ(current.sessions().score().score(), 75);
  EXPECT_EQ(current.scoring_label(), all.scoring_label());
}

TEST_F(StatsSummaryTest, OutcomesAreCountedAndOnlySolvedSessionsScore) {
  append(stamp, session(10, 0, vf::stats::Session::no_result));
  append(stamp, session(30, 0, vf::stats::Session::unbeaten));
  append(stamp, session(10, 20));
  append(stamp, session(10, 5));

  const auto s = summarize().sessions();
  EXPECT_EQ(s.no_result(), 1);
  EXPECT_EQ(s.unbeaten(), 1);
  EXPECT_EQ(s.beats(), 1);
  EXPECT_EQ(s.score().spans(), 3);
  // (30 + 10 + 5) / (30 + 10 + 10).
  EXPECT_DOUBLE_EQ(s.score().score(), 90);
}

TEST_F(StatsSummaryTest, UniformWeightingCountsEachSessionOnce) {
  append(stamp, session(30, 0, vf::stats::Session::unbeaten));
  append(stamp, session(10, 5));
  EXPECT_DOUBLE_EQ(summarize(false, vf::stats::SummaryRequest::uniform).sessions().score().score(), 75);
}

TEST_F(StatsSummaryTest, ChartAndTimingCoverTheLast30Days) {
  auto old = session(10, 5);
  old.set_finished_wall_epoch(NOW - 40 * DAY);
  old.set_analyze_ms(999);
  append(stamp, old);
  append(stamp, session(10, 10));

  const auto s = summarize().sessions();
  EXPECT_EQ(s.score().spans(), 2);
  ASSERT_EQ(s.days_size(), 1);
  EXPECT_DOUBLE_EQ(s.days(0).score(), 100);
  EXPECT_EQ(s.timing().n(), 1);
  EXPECT_DOUBLE_EQ(s.timing().max(), 10);
}

TEST_F(StatsSummaryTest, JournalChartsTheMiddleSizeAndListsEvery) {
  auto nav = sample(4, 10, 10);
  nav.set_nav_only(true);
  auto cut = sample(4, 10, 5);
  cut.set_deadline_hit(true);
  journal("a", 100, params,
          {sample(2, 10, 9), nav, cut, sample(4, 0, 0, vf::journal::Sample::too_large),
           sample(4, 10, 0, vf::journal::Sample::unbeaten), sample(8, 10, 2)});

  const auto j = summarize().journal();
  EXPECT_TRUE(j.present());
  EXPECT_EQ(j.scored_bytes(), 100);
  EXPECT_EQ(j.journal_bytes(), 100);
  EXPECT_EQ(j.charted_size(), 4);
  ASSERT_EQ(j.sizes_size(), 3);
  EXPECT_DOUBLE_EQ(j.sizes(0).score().score(), 90);
  EXPECT_DOUBLE_EQ(j.sizes(2).score().score(), 20);
  EXPECT_EQ(j.score().spans(), 3);
  EXPECT_NEAR(j.score().score(), 100.0 * 25 / 30, 1e-9);
  EXPECT_DOUBLE_EQ(j.nav().score(), 100);
  EXPECT_DOUBLE_EQ(j.edit().score(), 75);
  ASSERT_EQ(j.unscored_size(), 1);
  EXPECT_EQ(j.unscored(0).outcome(), "too_large");
  EXPECT_EQ(j.unbeaten(), 1);
  EXPECT_EQ(j.deadline_hit(), 1);
}

TEST_F(StatsSummaryTest, JournalScoredUnderOtherSettingsIsLeftOut) {
  vf::journal::SampleParams stale = params;
  stale.set_deadline_ms(1);
  journal("old", 50, stale, {sample(4, 10, 1)});
  journal("new", 30, params, {sample(4, 10, 5)});

  const auto j = summarize().journal();
  EXPECT_EQ(j.journal_bytes(), 80);
  EXPECT_EQ(j.scored_bytes(), 30);
  EXPECT_DOUBLE_EQ(j.score().score(), 50);
}

TEST_F(StatsSummaryTest, NoJournalDirectoryIsNotPresent) {
  fs::remove_all(dir / "journal");
  EXPECT_FALSE(summarize().journal().present());
}

TEST_F(StatsSummaryTest, CommandTablesCountScoredSessionsAndChartedRuns) {
  for (int i = 0; i < 10; i++) {
    auto s = session(10, 5);
    addCommand(s, "w");
    s.set_best_seq("e");
    append(stamp, s);
  }
  auto failed = session(10, 0, vf::stats::Session::no_result);
  addCommand(failed, "b");
  append(stamp, failed);

  auto charted = sample(4, 10, 5);
  addCommand(charted, "w");
  charted.set_best_seq("e");
  auto other = sample(8, 10, 5);
  addCommand(other, "w");
  other.set_best_seq("e");
  journal("a", 10, params, {charted, other});

  const auto c = summarize().commands();
  EXPECT_EQ(c.sessions(), 10);
  EXPECT_EQ(c.runs(), 1);
  ASSERT_EQ(c.never_size(), 1);
  EXPECT_EQ(c.never(0).text(), "w");
  EXPECT_EQ(c.never(0).user(), 11);
  ASSERT_EQ(c.less_size(), 1);
  EXPECT_EQ(c.less(0).text(), "e");
  EXPECT_EQ(c.less(0).optimizer(), 11);
}

TEST_F(StatsSummaryTest, TheUsersInsertTextIsWhatTheyPressedNotWhatRan) {
  for (int i = 0; i < 10; i++) {
    auto s = session(10, 5);
    // A pairing mapping ran `()<C-g>U<Left>` for the `(` pressed.
    addCommand(s, "if()<C-g>U<Left>x<BS><Esc>");
    s.set_typed_insert("f(x<BS>");
    s.set_best_seq("if()<Esc>");
    append(stamp, s);
  }

  const auto c = summarize().commands();
  EXPECT_EQ(c.user_typed_chars(), 30);
  EXPECT_EQ(c.optimizer_typed_chars(), 30);
  ASSERT_EQ(c.never_size(), 1);
  EXPECT_EQ(c.never(0).text(), "<BS>");
  EXPECT_EQ(c.never(0).user(), 10);
  ASSERT_EQ(c.less_size(), 1);
  EXPECT_EQ(c.less(0).text(), "i");
}

TEST_F(StatsSummaryTest, RunsRecordedWithoutPressedInsertKeysCountNoInsertText) {
  vector<vf::journal::Sample> samples;
  for (int i = 0; i < 10; i++) {
    auto s = sample(4, 10, 5);
    s.clear_typed_insert();
    s.set_last_atom(i);
    addCommand(s, "ifoo<C-w><Esc>");
    s.set_best_seq("ifo<BS><Esc>");
    samples.push_back(s);
  }
  journal("a", 10, params, samples);

  const auto c = summarize().commands();
  EXPECT_EQ(c.runs(), 10);
  EXPECT_EQ(c.user_typed_chars(), 0);
  EXPECT_EQ(c.optimizer_typed_chars(), 0);
  // `i` alone, on both sides.
  EXPECT_EQ(c.more_size(), 0);
  EXPECT_EQ(c.less_size(), 0);
}

TEST_F(StatsSummaryTest, RollupsCountExactlyAsTheSamplesTheyReplace) {
  journal("old1", 100, params, variedSamples(NOW - 60));
  journal("old2", 80, params, variedSamples(NOW - 3 * DAY));
  journal("recent", 50, params, variedSamples(NOW - DAY));
  const string effort = journalPart(vf::stats::SummaryRequest::effort);
  const string uniform = journalPart(vf::stats::SummaryRequest::uniform);
  const auto before = summarize();
  EXPECT_GT(before.journal().days_size(), 1);
  EXPECT_GT(before.journal().unbeaten(), 0);
  EXPECT_GT(before.journal().deadline_hit(), 0);
  EXPECT_GT(before.journal().unscored_size(), 0);
  EXPECT_GT(before.commands().user_typed_chars(), 0);
  EXPECT_GT(before.commands().more_size() + before.commands().less_size(), 0);

  age("old1");
  age("old2");
  EXPECT_EQ(rollUp(params), 2);

  EXPECT_FALSE(fs::exists(journalPath("old1")));
  EXPECT_FALSE(fs::exists(dir / "journal" / "old1.samples"));
  EXPECT_FALSE(fs::exists(journalPath("old2")));
  EXPECT_TRUE(fs::exists(journalPath("recent")));
  EXPECT_EQ(summarize().journal().journal_bytes(), 50);
  EXPECT_EQ(journalPart(vf::stats::SummaryRequest::effort), effort);
  EXPECT_EQ(journalPart(vf::stats::SummaryRequest::uniform), uniform);
}

TEST_F(StatsSummaryTest, RollUpLeavesRecentAndUnfinishedJournals) {
  journal("recent", 10, params, {sample(4, 10, 5)});
  journal("grown", 10, params, {sample(4, 10, 5)});
  appendFile(journalPath("grown"), "more");
  vf::journal::SampleParams stale = params;
  stale.set_deadline_ms(1);
  journal("stale", 10, stale, {sample(4, 10, 5)});
  age("grown");
  age("stale");

  EXPECT_EQ(rollUp(params), 0);

  for (const string name : {"recent", "grown", "stale"}) {
    EXPECT_TRUE(fs::exists(journalPath(name))) << name;
    EXPECT_TRUE(fs::exists(dir / "journal" / (name + ".samples"))) << name;
  }
  EXPECT_FALSE(fs::exists(dir / "journal" / "rollup.vfr"));
}

TEST_F(StatsSummaryTest, AJournalIsRolledUpOnceEvenWhenItsDeletionWasCutShort) {
  journal("old", 10, params, {sample(4, 10, 5)});
  const string journalBytes = readFile(journalPath("old"));
  const string samplesBytes = readFile(dir / "journal" / "old.samples");
  age("old");
  EXPECT_EQ(rollUp(params), 1);
  const string rollup = readFile(dir / "journal" / "rollup.vfr");

  // As a run that died between the append and the deletion leaves it.
  appendFile(journalPath("old"), journalBytes);
  appendFile(dir / "journal" / "old.samples", samplesBytes);
  age("old");
  EXPECT_EQ(summarize().journal().score().spans(), 1);

  EXPECT_EQ(rollUp(params), 1);
  EXPECT_FALSE(fs::exists(journalPath("old")));
  EXPECT_EQ(readFile(dir / "journal" / "rollup.vfr"), rollup);
  EXPECT_EQ(rollUp(params), 0);
  EXPECT_EQ(summarize().journal().score().spans(), 1);
}

TEST_F(StatsSummaryTest, RollupsUnderOtherParamsCountUnlessCurrentOnly) {
  vf::journal::SampleParams stale = params;
  stale.set_deadline_ms(1);
  journal("old", 10, stale,
          {sample(4, 10, 5), sample(2, 10, 5), sample(4, 0, 0, vf::journal::Sample::too_large)});
  journal("new", 10, params, {sample(4, 10, 5)});
  age("old");
  EXPECT_EQ(rollUp(stale), 1);

  const auto all = summarize();
  EXPECT_EQ(all.hidden_runs(), 0);
  EXPECT_EQ(all.journal().score().spans(), 2);
  EXPECT_EQ(all.journal().sizes(0).score().spans(), 1);
  EXPECT_EQ(all.journal().unscored_size(), 1);

  const auto current = summarize(true);
  EXPECT_EQ(current.hidden_runs(), 3);
  EXPECT_EQ(current.journal().score().spans(), 1);
  EXPECT_EQ(current.journal().sizes(0).score().spans(), 0);
  EXPECT_EQ(current.journal().unscored_size(), 0);
}

TEST_F(StatsSummaryTest, CachedSamplesGrowWithWhatIsAppended) {
  journal("a", 10, params, variedSamples(NOW));
  summarize();

  vf::journal::SampleFile more;
  for (const vf::journal::Sample& s : variedSamples(NOW - 2 * DAY)) {
    *more.add_records()->mutable_sample() = s;
  }
  more.add_records()->set_journal_size(30);
  const string bytes = more.SerializeAsString();
  appendFile(journalPath("a"), string(20, 'x'));
  // A sampler is part way through a record.
  appendFile(dir / "journal" / "a.samples", bytes.substr(0, bytes.size() / 2));
  summarize();
  appendFile(dir / "journal" / "a.samples", bytes.substr(bytes.size() / 2));

  fs::copy(dir / "journal", dir / "fresh");
  for (const auto weight : {vf::stats::SummaryRequest::effort, vf::stats::SummaryRequest::uniform}) {
    const auto cached = summarize(false, weight);
    const auto fresh = summarize(false, weight, (dir / "fresh").string());
    EXPECT_EQ(cached.journal().scored_bytes(), 30);
    EXPECT_EQ(cached.journal().SerializeAsString(), fresh.journal().SerializeAsString());
    EXPECT_EQ(cached.commands().SerializeAsString(), fresh.commands().SerializeAsString());
  }
}

TEST_F(StatsSummaryTest, AReplacedSamplesFileIsReadAgain) {
  journal("a", 10, params, {sample(4, 10, 5)});
  EXPECT_EQ(summarize().journal().score().spans(), 1);

  vf::journal::SampleParams stale = params;
  stale.set_deadline_ms(1);
  fs::remove(dir / "journal" / "a.samples");
  samplesOf("a", 10, stale, {sample(4, 10, 5), sample(4, 10, 5), sample(4, 10, 5)});
  EXPECT_EQ(summarize().journal().score().spans(), 0);

  fs::remove(dir / "journal" / "a.samples");
  samplesOf("a", 10, params, {sample(4, 10, 5), sample(4, 10, 5)});
  EXPECT_EQ(summarize().journal().score().spans(), 2);
}

namespace {

class StatsFormatTest : public StatsFixture {
protected:
  fs::path rollupPath() const { return dir / "journal" / "rollup.vfr"; }

  void rewrite(const fs::path& path, const string& bytes) {
    fs::remove(path);
    appendFile(path, bytes);
  }

  vf::journal::RollupFile rollups() {
    vf::journal::RollupFile file;
    EXPECT_TRUE(file.ParseFromString(readFile(rollupPath())));
    return file;
  }

  void setRollupFormat(int format) {
    vf::journal::RollupFile file = rollups();
    for (vf::journal::Rollup& rollup : *file.mutable_records()) rollup.set_format(format);
    rewrite(rollupPath(), file.SerializeAsString());
  }

  vf::stats::SessionLog sessionLog() {
    vf::stats::SessionLog out;
    EXPECT_TRUE(out.ParseFromString(readFile(log())));
    return out;
  }

  void rolledUpJournal() {
    journal("old", 10, params, {sample(4, 10, 5)});
    age("old");
    ASSERT_EQ(rollUp(params), 1);
  }
};

}  // namespace

TEST_F(StatsFormatTest, ARollupIsWrittenWithItsFormat) {
  rolledUpJournal();
  const auto file = rollups();
  ASSERT_EQ(file.records_size(), 1);
  EXPECT_EQ(file.records(0).format(), ROLLUP_FORMAT);
}

TEST_F(StatsFormatTest, ARollupWrittenBeforeFormatsStillReads) {
  rolledUpJournal();
  setRollupFormat(0);
  EXPECT_EQ(summarize().journal().score().spans(), 1);
}

TEST_F(StatsFormatTest, ARollupOfANewerFormatIsSkipped) {
  rolledUpJournal();
  setRollupFormat(ROLLUP_FORMAT + 1);
  EXPECT_EQ(summarize().journal().score().spans(), 0);
  EXPECT_EQ(summarize(true).hidden_runs(), 0);
}

TEST_F(StatsFormatTest, ANewLogStartsWithItsHeaderOnce) {
  append(stamp, session(10, 5));
  append(stamp, session(10, 5));
  const auto records = sessionLog().records();
  ASSERT_EQ(records.size(), 4);
  ASSERT_TRUE(records[0].has_header());
  EXPECT_EQ(records[0].header().format(), SESSION_LOG_FORMAT);
  EXPECT_TRUE(records[1].has_stamp());
  EXPECT_TRUE(records[2].has_session());
  EXPECT_TRUE(records[3].has_session());
}

TEST_F(StatsFormatTest, ALogWithoutAHeaderStillReadsAndGainsNone) {
  append(stamp, session(10, 5));
  append(stamp, session(10, 10));
  vf::stats::SessionLog headerless = sessionLog();
  headerless.mutable_records()->DeleteSubrange(0, 1);
  rewrite(log(), headerless.SerializeAsString());

  EXPECT_EQ(summarize().sessions().sessions(), 2);
  append(stamp, session(10, 5));
  const auto records = sessionLog().records();
  ASSERT_EQ(records.size(), 4);
  EXPECT_TRUE(records[0].has_stamp());
  EXPECT_EQ(summarize().sessions().sessions(), 3);
}

TEST_F(StatsFormatTest, ALogOfANewerFormatIsAnError) {
  vf::stats::SessionLog newer;
  newer.add_records()->mutable_header()->set_format(SESSION_LOG_FORMAT + 1);
  appendFile(log(), newer.SerializeAsString());

  const auto summary = Stats::summarize(request().SerializeAsString(), NOW);
  ASSERT_FALSE(summary);
  EXPECT_TRUE(summary.error().contains("newer")) << summary.error();

  vf::stats::AppendSessionRequest append;
  append.set_path(log());
  *append.mutable_session() = session(10, 5);
  EXPECT_FALSE(Stats::appendSession(append.SerializeAsString()));
  EXPECT_EQ(sessionLog().records_size(), 1);
}
