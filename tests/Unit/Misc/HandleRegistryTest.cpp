#include <gtest/gtest.h>

#include "LuaExports/Api.h"
#include "proto/common.pb.h"

import std.compat;
import vf.LuaExports.AsyncJob;
import vf.LuaExports.AsyncJobRegistry;
import vf.LuaExports.Common;
import vf.LuaExports.ViewRegistry;
import vf.Optimizer.SearchControl;

using namespace VF::LuaExports;

namespace {

using Job = AsyncJob<int>;

vf::Envelope poll(JobRegistry<Job>& jobs, int id) {
  std::string storage;
  const VFByteSlice slice = guardedPoll(
      storage, jobs, id, [](int& value) { return Proto{std::to_string(value)}; });
  vf::Envelope envelope;
  EXPECT_TRUE(envelope.ParseFromArray(slice.data, slice.len));
  return envelope;
}

TEST(HandleRegistryTest, PollingAnUnknownJobIdIsAnError) {
  JobRegistry<Job> jobs;
  EXPECT_TRUE(poll(jobs, 7).error().contains("job_id"));
}

TEST(HandleRegistryTest, PollIsPendingThenTheValueThenUnknown) {
  std::atomic<bool> release{false};
  JobRegistry<Job> jobs;
  const int id = jobs.create(std::make_unique<Job>(
      0, 0, [&](int& value, const SearchControl&) {
        while (!release.load()) std::this_thread::yield();
        value = 42;
      }));

  EXPECT_TRUE(poll(jobs, id).pending());

  release.store(true);
  while (jobs.running(id)) std::this_thread::yield();
  EXPECT_EQ(poll(jobs, id).value(), "42");
  EXPECT_TRUE(poll(jobs, id).error().contains("job_id"));
}

TEST(HandleRegistryTest, PollReturnsTheWorkersFailure) {
  JobRegistry<Job> jobs;
  const int id = jobs.create(std::make_unique<Job>(
      0, 0, [](int&, const SearchControl&) { throw std::out_of_range("boom"); }));

  while (jobs.running(id)) std::this_thread::yield();
  EXPECT_EQ(poll(jobs, id).error(), "C++ exception: boom");
}

TEST(HandleRegistryTest, CancelFreesARunningJobOnce) {
  JobRegistry<Job> jobs;
  const int id = jobs.create(std::make_unique<Job>(
      0, 0, [](int&, const SearchControl& control) {
        while (!control.cancelRequested()) std::this_thread::yield();
      }));

  EXPECT_TRUE(jobs.cancel(id));
  EXPECT_FALSE(jobs.cancel(id));
}

TEST(HandleRegistryTest, AnUnknownViewIdIsAnErrorAndSkipsTheBody) {
  ViewRegistry views;
  bool ran = false;
  const ProtoResult result = views.with(7, [&](Explore::View&) {
    ran = true;
    return Proto{};
  });

  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(result.error().message.contains("view_id"));
  EXPECT_FALSE(ran);
}

}  // namespace
