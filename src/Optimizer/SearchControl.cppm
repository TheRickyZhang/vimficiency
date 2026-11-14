export module vf.Optimizer.SearchControl;

import std.compat;

export {

// Cooperative stop signal for an optimizer search on a worker thread. The owner
// (AsyncJob) outlives the search, which takes it by reference; synchronous
// callers pass NEVER_STOP.
//
// A cancel means the result will be discarded; a deadline means return
// best-so-far. Main search loops poll `cancelledOrPastDeadline`. Setup phases
// with no best-so-far (composition planning, per-edit precompute) poll only
// `cancelRequested`, since a deadline there would discard a plan that is about
// to become available.
//
// The deadline is fixed before the worker starts, so only `cancelled` crosses
// threads.
class SearchControl {
public:
  using Clock = std::chrono::steady_clock;

  // Clock reads are far costlier than the atomic cancel check.
  static constexpr int DEADLINE_CHECK_INTERVAL = 1024;

  static const SearchControl NEVER_STOP;

  inline explicit SearchControl(Clock::time_point deadline = Clock::time_point::max())
      : deadline(deadline) {}

  inline void cancel() { cancelled.store(true, std::memory_order_relaxed); }

  inline bool cancelRequested() const {
    return cancelled.load(std::memory_order_relaxed);
  }

  inline bool cancelledOrPastDeadline(int totalPops) const {
    if (cancelRequested()) return true;
    return totalPops % DEADLINE_CHECK_INTERVAL == 0 && Clock::now() >= deadline;
  }

private:
  std::atomic<bool> cancelled{false};
  const Clock::time_point deadline;
};

}  // export

inline const SearchControl SearchControl::NEVER_STOP{};
