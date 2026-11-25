module;
#include <time.h>

module vf.Stats.Score;

import std.compat;

using namespace std;

namespace Stats {

int64_t localDay(int64_t epoch) {
  const time_t at = epoch;
  tm local{};
  localtime_r(&at, &local);
  const chrono::year_month_day date{chrono::year(local.tm_year + 1900),
                                    chrono::month(local.tm_mon + 1),
                                    chrono::day(local.tm_mday)};
  return chrono::sys_days(date).time_since_epoch().count();
}

}  // namespace Stats
