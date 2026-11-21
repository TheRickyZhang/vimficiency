#include <gtest/gtest.h>

#include "Unit/Journal/JournalBuilder.h"

import std.compat;
import vf.Journal.Replay;
import vf.Journal.Sampler;

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

class JournalSamplerTest : public testing::Test {
protected:
  void SetUp() override {
    dir = fs::temp_directory_path() /
          ("vimfy_journal_" + string(testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    params.add_window_sizes(2);
    params.set_slice_padding(2);
    params.set_max_search_lines(50);
    params.set_results_calculated(3);
    params.set_idle_gap_ms(30000);
  }

  void TearDown() override { fs::remove_all(dir); }

  Journal::SampleRun run() {
    vf::journal::SampleRequest request;
    request.set_journal_dir(dir.string());
    *request.mutable_params() = params;
    return Journal::sampleJournals(request.SerializeAsString());
  }

  vector<vf::journal::SampleRecord> records(const string& name = "a.samples") {
    const string bytes = readFile(dir / name);
    string_view rest = bytes;
    string_view entry;
    vector<vf::journal::SampleRecord> out;
    while (Journal::nextRecord(rest, entry)) {
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

  fs::path dir;
  vf::journal::SampleParams params;
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
  EXPECT_EQ(written[0].params().sampler_version(), Journal::SAMPLER_VERSION);
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

  EXPECT_EQ(written[2].sample().first_atom(), 2);
  EXPECT_EQ(written[3].journal_size(), ssize(journal));
  EXPECT_FALSE(fs::exists(dir / "a.inprogress"));
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
