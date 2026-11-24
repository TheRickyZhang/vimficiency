#pragma once

#include <gtest/gtest.h>

#include "proto/journal.pb.h"
#include "proto/stats.pb.h"
#include "proto/summary.pb.h"

import std.compat;
import vf.Journal.Sampler;
import vf.Stats.SessionLog;
import vf.Stats.Summary;
import vf.Storage.Samples;

inline constexpr int64_t NOW = 2'000'000'000;
inline constexpr int64_t DAY = 86400;

inline void appendFile(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary | std::ios::app) << bytes;
}

inline std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

inline vf::stats::Session session(double userCost, double bestCost,
                                  vf::stats::Session::Outcome outcome = vf::stats::Session::scored) {
  vf::stats::Session s;
  s.set_finished_wall_epoch(NOW - 60);
  s.set_kind("mark");
  s.set_outcome(outcome);
  s.set_user_cost(userCost);
  s.set_best_cost(bestCost);
  s.set_key_count(2);
  s.set_analyze_ms(10);
  s.set_typed_insert("");
  return s;
}

inline vf::journal::Sample sample(int size, double userCost, double bestCost,
                                  vf::journal::Sample::Outcome outcome = vf::journal::Sample::scored) {
  vf::journal::Sample s;
  s.set_window_size(size);
  s.set_wall_epoch(NOW - 60);
  s.set_outcome(outcome);
  s.set_user_cost(userCost);
  s.set_best_cost(bestCost);
  s.set_typed_insert("");
  return s;
}

inline void addCommand(vf::stats::Session& s, const std::string& keys) {
  vf::stats::Command& command = *s.add_commands();
  command.set_keys(keys);
  command.set_kind("motion");
  s.set_user_seq(s.user_seq() + keys);
}

inline void addCommand(vf::journal::Sample& s, const std::string& keys) {
  vf::stats::Command& command = *s.add_commands();
  command.set_keys(keys);
  command.set_kind("motion");
}

// Samples of every kind the journal section tells apart, over two days, with
// costs whose ratios are exact in binary so that sums do not depend on order.
inline std::vector<vf::journal::Sample> variedSamples(int64_t epoch) {
  std::vector<vf::journal::Sample> out;
  for (int i = 0; i < 12; i++) {
    auto s = sample(i % 3 == 0 ? 2 : 4, 8, i % 2 ? 4 : 2);
    s.set_wall_epoch(epoch - i % 2 * DAY);
    s.set_nav_only(i % 4 == 0);
    s.set_deadline_hit(i % 5 == 0);
    addCommand(s, i % 2 ? "w" : "cwfoo<Esc>");
    if (i % 2 == 0) s.set_typed_insert("foo");
    s.set_best_seq(i % 3 ? "e" : "ciwab<Esc>");
    out.push_back(s);
  }
  auto unbeaten = sample(4, 16, 0, vf::journal::Sample::unbeaten);
  unbeaten.set_deadline_hit(true);
  out.push_back(unbeaten);
  out.push_back(sample(4, 0, 0, vf::journal::Sample::too_large));
  out.push_back(sample(4, 0, 0, vf::journal::Sample::crashed));
  out.push_back(sample(4, 0, 0));
  out.push_back(sample(8, 16, 4));
  for (auto& s : out | std::views::drop(12)) s.set_wall_epoch(epoch);
  for (int i = 0; i < std::ssize(out); i++) out[i].set_last_atom(i);
  return out;
}

// A session log and a journal directory under a fresh temporary directory.
class StatsFixture : public testing::Test {
protected:
  void SetUp() override {
    const testing::TestInfo& test = *testing::UnitTest::GetInstance()->current_test_info();
    dir = std::filesystem::temp_directory_path() /
          ("vimfy_stats_" + std::string(test.test_suite_name()) + "_" + test.name());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "journal");
    stamp.set_slice_padding(5);
    params.add_window_sizes(2);
    params.add_window_sizes(4);
    params.add_window_sizes(8);
    *params.mutable_stamp() = stamp;
  }

  void TearDown() override { std::filesystem::remove_all(dir); }

  std::string log() const { return (dir / "sessions.vfs").string(); }

  void append(const vf::stats::Stamp& under, const vf::stats::Session& s) {
    vf::stats::AppendSessionRequest request;
    request.set_path(log());
    *request.mutable_stamp() = under;
    *request.mutable_session() = s;
    ASSERT_TRUE(Stats::appendSession(request.SerializeAsString()));
  }

  // A journal of `bytes` bytes and its samples, made under `under`.
  void journal(const std::string& name, int64_t bytes, const vf::journal::SampleParams& under,
               const std::vector<vf::journal::Sample>& samples) {
    appendFile(journalPath(name), std::string(bytes, 'x'));
    samplesOf(name, bytes, under, samples);
  }

  void samplesOf(const std::string& name, int64_t bytes, vf::journal::SampleParams under,
                 const std::vector<vf::journal::Sample>& samples) {
    stampSampleParams(under);
    vf::journal::SampleFile file;
    *file.add_records()->mutable_params() = under;
    for (const vf::journal::Sample& s : samples) *file.add_records()->mutable_sample() = s;
    file.add_records()->set_journal_size(bytes);
    appendFile(dir / "journal" / (name + ".samples"), file.SerializeAsString());
  }

  std::filesystem::path journalPath(const std::string& name) const {
    return dir / "journal" / (name + ".vfj");
  }

  void age(const std::string& name) {
    std::filesystem::last_write_time(
        journalPath(name), std::filesystem::file_time_type::clock::now() - std::chrono::days(100));
  }

  int rollUp(vf::journal::SampleParams under) {
    stampSampleParams(under);
    const auto rolled = Journal::rollUpJournals(dir / "journal", under.SerializeAsString(), 90);
    EXPECT_TRUE(rolled) << rolled.error();
    return rolled.value_or(0);
  }

  vf::stats::SummaryRequest request(
      bool currentOnly = false,
      vf::stats::SummaryRequest::Weight weight = vf::stats::SummaryRequest::effort,
      const std::string& journalDir = "") const {
    vf::stats::SummaryRequest out;
    out.set_session_log(log());
    *out.mutable_stamp() = stamp;
    out.set_current_only(currentOnly);
    out.set_journal_dir(journalDir.empty() ? (dir / "journal").string() : journalDir);
    *out.mutable_sample_params() = params;
    out.set_weight(weight);
    out.set_charted_size(charted);
    return out;
  }

  vf::stats::Summary summarize(const vf::stats::SummaryRequest& req) {
    const auto bytes = Stats::summarize(req.SerializeAsString(), NOW);
    EXPECT_TRUE(bytes) << bytes.error();
    vf::stats::Summary out;
    EXPECT_TRUE(out.ParseFromString(*bytes));
    return out;
  }

  vf::stats::Summary summarize(bool currentOnly = false,
                               vf::stats::SummaryRequest::Weight weight = vf::stats::SummaryRequest::effort,
                               const std::string& journalDir = "") {
    return summarize(request(currentOnly, weight, journalDir));
  }

  vf::stats::Summary summarize(const vf::stats::Filter& filter) {
    vf::stats::SummaryRequest req = request();
    *req.mutable_filter() = filter;
    return summarize(req);
  }

  vf::stats::RunList runs(const vf::stats::Filter& filter,
                          vf::stats::RunsRequest::Order order = vf::stats::RunsRequest::worst,
                          int offset = 0, int limit = 1000, bool currentOnly = false) {
    vf::stats::RunsRequest req;
    *req.mutable_scope() = request(currentOnly);
    *req.mutable_scope()->mutable_filter() = filter;
    req.set_order(order);
    req.set_offset(offset);
    req.set_limit(limit);
    const auto bytes = Stats::listRuns(req.SerializeAsString());
    EXPECT_TRUE(bytes) << bytes.error();
    vf::stats::RunList out;
    EXPECT_TRUE(out.ParseFromString(*bytes));
    return out;
  }

  std::filesystem::path dir;
  vf::stats::Stamp stamp;
  vf::journal::SampleParams params;
  // Every request's `charted_size`.
  int charted = 0;
};
