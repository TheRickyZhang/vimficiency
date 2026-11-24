export module vf.Stats.Score;

import std.compat;

export namespace Stats {

enum class Weight : uint8_t { Effort, Uniform };

// Σ w·min(best/user, 1) / Σ w over spans, with w the user's effort or 1. Under
// effort weighting this is Σ min(best, user) / Σ user: long spans count more.
struct ScoreTotals {
  int spans = 0;
  double weighted = 0;
  double weights = 0;

  // `userCost` must be positive. An unbeaten span passes `bestCost == userCost`.
  inline void add(double userCost, double bestCost, Weight weight) {
    const double w = weight == Weight::Effort ? userCost : 1;
    spans++;
    weighted += w * std::min(bestCost / userCost, 1.0);
    weights += w;
  }

  inline void merge(const ScoreTotals& other) {
    spans += other.spans;
    weighted += other.weighted;
    weights += other.weights;
  }

  // 0..100; only meaningful when `spans > 0`.
  inline double score() const { return 100 * weighted / weights; }
};

// Days since 1970-01-01 in local time, so a day ends at local midnight on
// either side of a DST change.
int64_t localDay(int64_t epoch);

}  // namespace Stats
