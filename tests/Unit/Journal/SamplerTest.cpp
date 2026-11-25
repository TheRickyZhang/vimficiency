#include <gtest/gtest.h>

#include "Unit/Journal/JournalBuilder.h"

#include "proto/summary.pb.h"

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Sampler;
import vf.Journal.Windows;
import vf.Storage.RecordFile;
import vf.Storage.Samples;
import vf.Optimizer.Analyze;
import vf.Versions;

using namespace std;
namespace fs = std::filesystem;

namespace {

string readFile(const fs::path& path) {
  ifstream in(path, ios::binary);
  return string(istreambuf_iterator<char>(in), {});
}

void writeFile(const fs::path& path, const string& bytes) {
  ofstream(path, ios::binary | ios::trunc) << bytes;
}

// A journal of `atoms` single `l` atoms on one line.
string journalOf(int atoms) {
  JournalBuilder j;
  j.header(5000).snapshot({"abcdefghijklmnopqrstuvwxyz"}).resync(0, 0);
  for (int i = 1; i <= atoms; i++) j.next().atom("l", 0, i);
  return j.bytes();
}

// Eight atoms moving over and deleting from rows 10 and 11 of twenty, so a
// slice cuts rows off both ends. The filetype changes from cpp to lua in the
// tick of atom 3. Atom 2 is a `gj` that crossed a line, which the search
// scores as `j`.
string editedJournal() {
  vector<string> lines;
  for (int i = 0; i < 20; i++) lines.push_back(format("line {} of the text", i));
  const JournalAtomOptions edit{.kind = "operator"};
  JournalBuilder j;
  j.header(5000).snapshot(lines, JournalBuilder::BUF, "cpp").resync(10, 0);
  j.next().atom("l", 10, 1);
  j.next().atom("gj", 11, 1);
  lines[11].erase(1, 1);
  j.next().delta(11, 1, {lines[11]}).atom("x", 11, 1, edit);
  j.next().snapshot(lines, JournalBuilder::BUF, "lua").atom("k", 10, 1);
  lines[10].erase(1, 1);
  j.next().delta(10, 1, {lines[10]}).atom("x", 10, 1, edit);
  j.next().atom("j", 11, 1);
  j.next().atom("l", 11, 2);
  j.next().atom("l", 11, 3);
  return j.bytes();
}

class JournalSamplerTest : public testing::Test {
protected:
  void SetUp() override {
    dir = fs::temp_directory_path() /
          ("vimfy_journal_" + string(testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    params.add_window_sizes(2);
    params.mutable_stamp()->set_slice_padding(2);
    params.mutable_stamp()->set_max_search_lines(50);
    params.mutable_stamp()->set_results_calculated(3);
    params.set_idle_gap_ms(30000);
  }

  void TearDown() override { fs::remove_all(dir); }

  Journal::SampleRun run() {
    vf::journal::SampleRequest request;
    request.set_journal_dir(dir.string());
    *request.mutable_params() = params;
    request.set_keep_days(keepDays);
    return Journal::sampleJournals(request.SerializeAsString());
  }

  vector<vf::journal::SampleRecord> records(const string& name = "a.samples") {
    const string bytes = readFile(dir / name);
    string_view rest = bytes;
    string_view entry;
    vector<vf::journal::SampleRecord> out;
    while (nextRecord(rest, entry)) {
      EXPECT_TRUE(out.emplace_back().ParseFromArray(entry.data(), entry.size()));
    }
    EXPECT_TRUE(rest.empty());
    return out;
  }

  vector<vf::journal::Sample> samples(const string& name = "a.samples") {
    vector<vf::journal::Sample> out;
    for (const vf::journal::SampleRecord& record : records(name)) {
      if (record.has_sample()) out.push_back(record.sample());
    }
    return out;
  }

  expected<vf::stats::RunDetail, string> detail(const vf::stats::RunId& id) {
    vf::stats::RunRequest request;
    *request.mutable_id() = id;
    request.set_journal_dir(dir.string());
    *request.mutable_sample_params() = params;
    const expected<string, string> bytes = Journal::runDetail(request.SerializeAsString());
    if (!bytes) return unexpected(bytes.error());
    vf::stats::RunDetail out;
    EXPECT_TRUE(out.ParseFromString(*bytes));
    return out;
  }

  static vf::stats::RunId journalRun(const string& file, int size, int lastAtom) {
    vf::stats::RunId id;
    id.mutable_journal()->set_file(file);
    id.mutable_journal()->set_window_size(size);
    id.mutable_journal()->set_last_atom(lastAtom);
    return id;
  }

  fs::path dir;
  vf::journal::SampleParams params;
  int keepDays = 90;
};

}  // namespace

TEST_F(JournalSamplerTest, WritesParamsThenSamplesThenTheJournalSize) {
  const string journal = journalOf(5);
  writeFile(dir / "a.vfj", journal);

  const Journal::SampleRun result = run();
  EXPECT_EQ(result.error, "");
  EXPECT_EQ(result.journals, 1);
  EXPECT_EQ(result.evaluated, 2);

  const vector<vf::journal::SampleRecord> written = records();
  ASSERT_EQ(written.size(), 4u);
  ASSERT_TRUE(written[0].has_params());
  EXPECT_EQ(written[0].params().stamp().scoring_version(), SCORING_VERSION);
  EXPECT_EQ(written[0].params().samples_format(), SAMPLES_FORMAT);
  EXPECT_EQ(written[0].params().window_sizes(0), 2);

  const vf::journal::Sample& first = written[1].sample();
  EXPECT_EQ(first.window_size(), 2);
  EXPECT_EQ(first.first_atom(), 0);
  EXPECT_EQ(first.last_atom(), 1);
  EXPECT_EQ(first.atom_count(), 2);
  EXPECT_EQ(first.wall_epoch(), 5000);
  EXPECT_EQ(first.outcome(), vf::journal::Sample::scored);
  EXPECT_TRUE(first.nav_only());
  EXPECT_GT(first.user_cost(), 0);
  EXPECT_GT(first.best_cost(), 0);
  EXPECT_FALSE(first.best_seq().empty());
  ASSERT_EQ(first.commands_size(), 2);
  EXPECT_EQ(first.commands(1).keys(), "l");
  EXPECT_EQ(first.commands(1).kind(), "motion");

  EXPECT_EQ(written[2].sample().first_atom(), 2);
  EXPECT_EQ(written[3].journal_size(), ssize(journal));
  EXPECT_FALSE(fs::exists(dir / "a.inprogress"));
}

TEST_F(JournalSamplerTest, ASampleKeepsThePressedInsertKeysOnlyWhenEveryAtomHasThem) {
  JournalBuilder j;
  j.header(5000).snapshot({"abcdefghijklmnopqrstuvwxyz"}).resync(0, 0);
  j.next().atom("l", 0, 1, {.typedInsert = "a"});
  j.next().atom("l", 0, 2, {.typedInsert = "b"});
  j.next().atom("l", 0, 3, {.insertTagged = false});
  j.next().atom("l", 0, 4);
  j.next().atom("l", 0, 5);
  writeFile(dir / "a.vfj", j.bytes());
  run();

  const vector<vf::journal::Sample> all = samples();
  ASSERT_EQ(all.size(), 2u);
  EXPECT_TRUE(all[0].has_typed_insert());
  EXPECT_EQ(all[0].typed_insert(), "ab");
  EXPECT_FALSE(all[1].has_typed_insert());
}

TEST_F(JournalSamplerTest, AnUnchangedJournalIsNotSampledAgain) {
  writeFile(dir / "a.vfj", journalOf(5));
  run();
  const string before = readFile(dir / "a.samples");

  EXPECT_EQ(run().evaluated, 0);
  EXPECT_EQ(readFile(dir / "a.samples"), before);
}

TEST_F(JournalSamplerTest, AGrownJournalIsSampledFromItsNewPart) {
  writeFile(dir / "a.vfj", journalOf(5));
  run();
  writeFile(dir / "a.vfj", journalOf(9));

  EXPECT_EQ(run().evaluated, 2);

  const vector<vf::journal::Sample> all = samples();
  ASSERT_EQ(all.size(), 4u);
  for (int i = 0; i < 4; i++) EXPECT_EQ(all[i].first_atom(), 2 * i);
}

TEST_F(JournalSamplerTest, OtherParamsStartTheSamplesOver) {
  writeFile(dir / "a.vfj", journalOf(9));
  run();
  params.set_window_sizes(0, 4);

  EXPECT_EQ(run().evaluated, 2);

  const vector<vf::journal::SampleRecord> written = records();
  ASSERT_EQ(written.size(), 4u);
  EXPECT_EQ(written[0].params().window_sizes(0), 4);
  EXPECT_EQ(written[1].sample().window_size(), 4);
}

TEST_F(JournalSamplerTest, AWindowARunDiedOnIsRecordedAndSkipped) {
  writeFile(dir / "a.vfj", journalOf(5));
  run();
  writeFile(dir / "a.vfj", journalOf(9));
  vf::journal::Sample dying;
  dying.set_window_size(2);
  dying.set_first_atom(4);
  dying.set_last_atom(5);
  dying.set_atom_count(2);
  dying.set_outcome(vf::journal::Sample::crashed);
  writeFile(dir / "a.inprogress", dying.SerializeAsString());

  EXPECT_EQ(run().evaluated, 1);

  const vector<vf::journal::Sample> all = samples();
  ASSERT_EQ(all.size(), 4u);
  EXPECT_EQ(all[2].outcome(), vf::journal::Sample::crashed);
  EXPECT_EQ(all[2].first_atom(), 4);
  EXPECT_EQ(all[3].outcome(), vf::journal::Sample::scored);
  EXPECT_EQ(all[3].first_atom(), 6);
  EXPECT_FALSE(fs::exists(dir / "a.inprogress"));
}

TEST_F(JournalSamplerTest, ACommittedWindowWithACrashMarkerIsCountedOnce) {
  writeFile(dir / "a.vfj", journalOf(5));
  ASSERT_EQ(run().evaluated, 2);

  vf::journal::SampleFile file;
  ASSERT_TRUE(file.ParseFromString(readFile(dir / "a.samples")));
  file.mutable_records()->DeleteSubrange(2, 2);
  writeFile(dir / "a.samples", file.SerializeAsString());
  auto& marker = *file.mutable_records(1)->mutable_sample();
  marker.set_outcome(vf::journal::Sample::crashed);
  writeFile(dir / "a.inprogress", marker.SerializeAsString());

  EXPECT_EQ(run().evaluated, 1);
  const auto all = samples();
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].outcome(), vf::journal::Sample::scored);
  EXPECT_EQ(all[1].first_atom(), 2);
  EXPECT_FALSE(fs::exists(dir / "a.inprogress"));
}

TEST_F(JournalSamplerTest, ATruncatedSamplesRecordIsRedone) {
  writeFile(dir / "a.vfj", journalOf(5));
  run();
  const string whole = readFile(dir / "a.samples");
  // Cuts into the journal-size record and the sample before it.
  writeFile(dir / "a.samples", whole.substr(0, whole.size() - 12));

  EXPECT_EQ(run().evaluated, 1);

  const vector<vf::journal::SampleRecord> written = records();
  ASSERT_EQ(written.size(), 4u);
  EXPECT_EQ(written[1].sample().first_atom(), 0);
  EXPECT_EQ(written[2].sample().first_atom(), 2);
  EXPECT_TRUE(written[3].has_journal_size());
}

TEST_F(JournalSamplerTest, EachJournalGetsItsOwnSamples) {
  writeFile(dir / "a.vfj", journalOf(5));
  writeFile(dir / "b.vfj", journalOf(3));
  writeFile(dir / "notes.txt", "not a journal");

  const Journal::SampleRun result = run();
  EXPECT_EQ(result.journals, 2);
  EXPECT_EQ(result.evaluated, 3);
  EXPECT_EQ(samples("a.samples").size(), 2u);
  EXPECT_EQ(samples("b.samples").size(), 1u);
}

TEST_F(JournalSamplerTest, RejectsAWindowSizeBelowOne) {
  params.set_window_sizes(0, 0);

  EXPECT_EQ(run().error, "window_sizes must be positive");
}

TEST_F(JournalSamplerTest, RejectsKeepDaysBelowOne) {
  keepDays = 0;

  EXPECT_EQ(run().error, "keep_days must be positive");
}

TEST_F(JournalSamplerTest, AnOldJournalIsScoredThenRolledUpAndDeleted) {
  writeFile(dir / "a.vfj", journalOf(5));
  writeFile(dir / "b.vfj", journalOf(5));
  fs::last_write_time(dir / "a.vfj", fs::file_time_type::clock::now() - chrono::days(keepDays + 1));

  const Journal::SampleRun result = run();
  EXPECT_EQ(result.error, "");
  EXPECT_EQ(result.evaluated, 4);
  EXPECT_EQ(result.rolledUp, 1);
  EXPECT_FALSE(fs::exists(dir / "a.vfj"));
  EXPECT_FALSE(fs::exists(dir / "a.samples"));
  EXPECT_TRUE(fs::exists(dir / "b.vfj"));
  EXPECT_EQ(samples("b.samples").size(), 2u);

  const string bytes = readFile(dir / "rollup.vfr");
  string_view rest = bytes;
  string_view entry;
  ASSERT_TRUE(nextRecord(rest, entry));
  EXPECT_TRUE(rest.empty());
  vf::journal::Rollup rollup;
  ASSERT_TRUE(rollup.ParseFromArray(entry.data(), entry.size()));
  EXPECT_EQ(rollup.journal(), "a.vfj");
  EXPECT_EQ(rollup.params().stamp().scoring_version(), SCORING_VERSION);
  int runs = 0;
  for (const vf::journal::RollupEntry& e : rollup.entries()) {
    EXPECT_EQ(e.window_size(), 2);
    runs += e.runs();
  }
  EXPECT_EQ(runs, 2);
}

TEST_F(JournalSamplerTest, EverySampledRunIsSlicedAgainAsItWasScored) {
  params.add_window_sizes(3);
  const string journal = editedJournal();
  writeFile(dir / "a.vfj", journal);
  ASSERT_EQ(run().error, "");

  map<pair<int, int>, Journal::Window> windows;
  Journal::Replay replay(journal);
  Journal::WindowCutter cutter(
      span<const int>(params.window_sizes().data(), params.window_sizes_size()),
      params.idle_gap_ms());
  Journal::Event event;
  vector<Journal::Window> cut;
  while (replay.next(event)) {
    cut.clear();
    cutter.push(event, cut);
    for (Journal::Window& w : cut) windows.emplace(pair(w.size, w.lastAtom), std::move(w));
  }
  const Journal::EvaluateParams sliceParams{
      .slicePadding = params.stamp().slice_padding(),
      .maxSearchLines = params.stamp().max_search_lines(),
  };

  const vector<vf::journal::Sample> all = samples();
  ASSERT_EQ(all.size(), 5u);
  for (const vf::journal::Sample& sample : all) {
    SCOPED_TRACE(format("size {}, last atom {}", sample.window_size(), sample.last_atom()));
    const auto got = detail(journalRun("a.vfj", sample.window_size(), sample.last_atom()));
    ASSERT_TRUE(got.has_value()) << got.error();

    string typed;
    string scored;
    for (const vf::stats::Command& command : sample.commands()) {
      typed += command.keys();
      scored += command.keys() == "gj" ? "j" : command.keys();
    }
    EXPECT_EQ(got->keys(), scored);
    EXPECT_EQ(got->typed(), typed);
    EXPECT_EQ(got->filetype(), sample.first_atom() >= 4 ? "lua" : "cpp");

    const Journal::WindowSlice sliced =
        Journal::sliceWindow(windows.at({sample.window_size(), sample.last_atom()}), sliceParams);
    ASSERT_EQ(sliced.outcome, Journal::Outcome::Scored);
    const SearchInputs& in = sliced.inputs;
    EXPECT_LT(got->lines_size(), 20);
    ASSERT_EQ(got->lines_size(), ssize(in.initialLines));
    for (int i = 0; i < got->lines_size(); i++) EXPECT_EQ(got->lines(i), string(in.initialLines[i]));
    ASSERT_EQ(got->goal_lines_size(), ssize(in.goalLines));
    for (int i = 0; i < got->goal_lines_size(); i++) EXPECT_EQ(got->goal_lines(i), string(in.goalLines[i]));
    EXPECT_EQ(got->start_row(), in.initialPos.line);
    EXPECT_EQ(got->start_col(), in.initialPos.col);
    EXPECT_EQ(got->end_row(), in.goalPos.line);
    EXPECT_EQ(got->end_col(), in.goalPos.col);
    const auto& recording = got->recording();
    EXPECT_EQ(recording.row(), in.initialPos.line);
    EXPECT_EQ(recording.col(), in.initialPos.col);
    ASSERT_EQ(recording.steps_size(), sample.commands_size());
    vector<string> rendered(got->lines().begin(), got->lines().end());
    for (int i = 0; i < recording.steps_size(); i++) {
      const auto& step = recording.steps(i);
      EXPECT_EQ(step.keys(), sample.commands(i).keys());
      EXPECT_EQ(step.mode(), "n");
      for (const auto& delta : step.deltas()) {
        ASSERT_GE(delta.first(), 0);
        ASSERT_LE(delta.first() + delta.old_count(), ssize(rendered));
        auto pos = rendered.begin() + delta.first();
        pos = rendered.erase(pos, pos + delta.old_count());
        rendered.insert(pos, delta.lines().begin(), delta.lines().end());
      }
      ASSERT_GE(step.row(), 0);
      ASSERT_LT(step.row(), ssize(rendered));
    }
    EXPECT_TRUE(ranges::equal(rendered, got->goal_lines()));
    EXPECT_EQ(recording.steps(recording.steps_size() - 1).row(), in.goalPos.line);
    ASSERT_LT(got->end_row(), ssize(in.goalLines));
    EXPECT_LT(got->end_col(), max<int>(1, ssize(in.goalLines[got->end_row()])));
  }
}

TEST_F(JournalSamplerTest, ARunWithoutASliceIsAnError) {
  writeFile(dir / "a.vfj", editedJournal());
  params.mutable_stamp()->set_max_search_lines(1);

  EXPECT_EQ(detail(journalRun("a.vfj", 2, 3)).error(), "a run that was too_large has no slice");
}

TEST_F(JournalSamplerTest, ARunTheJournalDoesNotHoldIsAnError) {
  writeFile(dir / "a.vfj", journalOf(5));
  ASSERT_TRUE(detail(journalRun("a.vfj", 2, 1)).has_value());

  vf::stats::RunId session;
  session.set_session(0);
  EXPECT_EQ(detail(session).error(), "a session run has no journal window");
  EXPECT_EQ(detail(journalRun("b.vfj", 2, 1)).error(),
            format("journal {} is gone", (dir / "b.vfj").string()));
  EXPECT_EQ(detail(journalRun("../a.vfj", 2, 1)).error(),
            "journal file name '../a.vfj' holds a path separator");
  const string path = (dir / "a.vfj").string();
  EXPECT_EQ(detail(journalRun("a.vfj", 2, 2)).error(),
            format("no window of 2 atoms ends at atom 2 of {}", path));
  EXPECT_EQ(detail(journalRun("a.vfj", 2, 99)).error(),
            format("no window of 2 atoms ends at atom 99 of {}", path));

  params.set_window_sizes(0, 4);
  EXPECT_EQ(detail(journalRun("a.vfj", 2, 1)).error(),
            format("no window of 2 atoms ends at atom 1 of {}", path));
}
