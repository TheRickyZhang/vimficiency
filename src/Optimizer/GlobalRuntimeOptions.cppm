export module vf.Optimizer.GlobalRuntimeOptions;

import std.compat;
export import vf.Optimizer.CountPenalty;

export {

struct GlobalRuntimeOptions {
  bool useCountPenaltyOverrides = false;
  CountPenaltyOverrideTable countPenaltyOverrides{};
};

inline GlobalRuntimeOptions& globalRuntimeOptions() {
  static GlobalRuntimeOptions options;
  return options;
}

template<CountClass C>
inline double runtimeCountPenalty(const CountPenaltyInput& in) {
  const auto& options = globalRuntimeOptions();
  if (options.useCountPenaltyOverrides) {
    return countPenalty<C>(in, options.countPenaltyOverrides);
  }
  return countPenalty<C>(in);
}

}  // export
