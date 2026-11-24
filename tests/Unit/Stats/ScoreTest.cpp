#include <gtest/gtest.h>

#include <stdlib.h>
#include <time.h>

import std.compat;
import vf.Stats.Score;

using namespace std;
using namespace std::chrono;

namespace {

int64_t epochOf(sys_days day, hours h, minutes m) {
  return duration_cast<seconds>((day + h + m).time_since_epoch()).count();
}

int64_t dayOf(sys_days day) { return day.time_since_epoch().count(); }

// Sets TZ for one test and restores it after.
class LocalZone : public testing::Test {
protected:
  void use(const char* zone) {
    const char* old = getenv("TZ");
    saved = old ? old : "";
    hadZone = old != nullptr;
    setenv("TZ", zone, 1);
    tzset();
  }

  void TearDown() override {
    if (hadZone) {
      setenv("TZ", saved.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }

  string saved;
  bool hadZone = false;
};

}  // namespace

TEST(StatsScore, EffortWeightingIsBestOverUserSummed) {
  Stats::ScoreTotals totals;
  totals.add(10, 5, Stats::Weight::Effort);
  totals.add(30, 25, Stats::Weight::Effort);
  totals.add(10, 14, Stats::Weight::Effort);
  // Σ min(best, user) / Σ user = (5 + 25 + 10) / 50.
  EXPECT_EQ(totals.spans, 3);
  EXPECT_DOUBLE_EQ(totals.score(), 80);
}

TEST(StatsScore, UniformWeightingIsTheMeanRatio) {
  Stats::ScoreTotals totals;
  totals.add(10, 5, Stats::Weight::Uniform);
  totals.add(30, 30, Stats::Weight::Uniform);
  EXPECT_DOUBLE_EQ(totals.score(), 75);
}

TEST(StatsScore, BeatingTheBestScoresOneHundred) {
  Stats::ScoreTotals totals;
  totals.add(2, 5, Stats::Weight::Effort);
  EXPECT_DOUBLE_EQ(totals.score(), 100);
}

TEST_F(LocalZone, DaysEndAtLocalMidnight) {
  use("America/New_York");
  const sys_days march7 = 2026y / March / 7;
  // 23:30 EST on the 7th is 04:30 UTC on the 8th.
  EXPECT_EQ(Stats::localDay(epochOf(march7 + days(1), 4h, 30min)), dayOf(march7));
  EXPECT_EQ(Stats::localDay(epochOf(march7 + days(1), 5h, 30min)), dayOf(march7 + days(1)));
}

TEST_F(LocalZone, ADayLosingAnHourToDstIsStillOneDay) {
  use("America/New_York");
  const sys_days march8 = 2026y / March / 8;
  // 00:30 EST and 23:30 EDT on the day clocks spring forward.
  EXPECT_EQ(Stats::localDay(epochOf(march8, 5h, 30min)), dayOf(march8));
  EXPECT_EQ(Stats::localDay(epochOf(march8 + days(1), 3h, 30min)), dayOf(march8));
  EXPECT_EQ(Stats::localDay(epochOf(march8 + days(1), 4h, 30min)), dayOf(march8 + days(1)));
}
