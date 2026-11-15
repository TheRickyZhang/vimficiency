export module vf.LuaExports.AsyncJob;

import std.compat;
export import vf.Optimizer.SearchControl;

export {

namespace VF::LuaExports {

// An off-main-thread optimizer search over state the job owns. `run(state,
// control)` executes on the worker; the main thread polls `ready()` and reads
// `state()` only once it is true, or calls `cancel()` and drops the job.
//
// Lifetime: the destructor joins `worker_` before any member is torn down.
// Dropping a job whose worker is still running (the cancel path) blocks until the search observes the flag and returns, so the worker
// can never outlive what it reads or writes. The join is bounded by the
// search's polling granularity: per pop, and between composition setup phases.
//
// Invariant: VimOptions::shiftwidth (the one runtime-mutable VimCore global the
// search reads) must not be reconfigured while a worker is live. The session
// sets it once at setup; single-flight on the suggest side keeps at most one
// suggest worker alive.
template <typename State>
class AsyncJob {
 public:
  // Starts the worker immediately. Construct in place (make_unique): the run
  // body captures `this`.
  template <typename Fn>
  AsyncJob(State state, int deadlineMs, Fn run)
      : control_(deadlineMs > 0
                     ? SearchControl::Clock::now() + std::chrono::milliseconds(deadlineMs)
                     : SearchControl::Clock::time_point::max()),
        state_(std::move(state)) {
    worker_ = std::thread([this, run = std::move(run)]() mutable {
      // An exception leaving a thread body is std::terminate, which would take
      // Neovim down with it.
      try {
        run(state_, control_);
      } catch (const std::exception& e) {
        error_ = std::string("C++ exception: ") + e.what();
      } catch (...) {
        error_ = "unknown C++ exception";
      }
      done_.store(true, std::memory_order_release);
    });
  }

  // std::thread rather than std::jthread: Apple's libc++ does not ship jthread.
  inline ~AsyncJob() {
    if (worker_.joinable()) worker_.join();
  }
  AsyncJob(const AsyncJob&) = delete;
  AsyncJob& operator=(const AsyncJob&) = delete;

  // True once `run` has returned; the release/acquire pair means an observer
  // of true also sees the fully written state.
  inline bool ready() const { return done_.load(std::memory_order_acquire); }
  inline void cancel() { control_.cancel(); }
  // Only meaningful after `ready()`.
  inline State& state() { return state_; }
  // Only meaningful after `ready()`. Non-empty when `run` threw, in which case
  // `state()` is whatever the body had written so far.
  inline const std::string& error() const { return error_; }

 private:
  SearchControl control_;
  std::atomic<bool> done_{false};
  std::string error_;
  State state_;
  std::thread worker_;
};

}  // namespace VF::LuaExports

}  // export
