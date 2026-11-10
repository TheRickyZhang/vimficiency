export module vf.Optimizer.SearchFrontier;

import std.compat;

export {

namespace Search {

// Shared A* admission for range-goal searches: effort cap, then cost-map
// relaxation. Terminal states are never cached so distinct paths to the same
// goal cell can each emit a result.
template<class State, class CostMap, class IsTerminalFn>
inline bool admitRangeState(const State& state,
                            CostMap& costMap,
                            double maxEffort,
                            IsTerminalFn&& isTerminal) {
  if (state.getEffort() > maxEffort) return false;

  const double newCost = state.getCost();
  auto it = costMap.find(state.getKey());

  if (it == costMap.end()) {
    if (!isTerminal(state)) {
      costMap.emplace(state.getKey(), newCost);
    }
    return true;
  }

  if (newCost <= it->second) {
    it->second = newCost;
    return true;
  }

  return false;
}

template<class State, class PriorityQueue, class CostMap, class IsTerminalFn>
inline bool enqueueRangeState(State&& state,
                              PriorityQueue& pq,
                              CostMap& costMap,
                              double maxEffort,
                              IsTerminalFn&& isTerminal) {
  if (!admitRangeState(state, costMap, maxEffort, isTerminal)) return false;
  pq.push(std::move(state));
  return true;
}

template<class State, class PriorityQueue, class CostMap, class PendingByStart,
         class StartActive>
inline bool enqueueImprovedTransformState(State&& state,
                                     PriorityQueue& pq,
                                     CostMap& costMap,
                                     PendingByStart& pendingByStart,
                                     const StartActive& startActive) {
  const int startIndex = state.getStartIndex();
  if (!startActive[startIndex]) return false;

  const auto key = state.getKey();
  const double newCost = state.getCost();
  auto it = costMap.find(key);
  if (it != costMap.end() && newCost > it->second) return false;

  costMap[key] = newCost;
  pendingByStart[startIndex]++;
  pq.push(std::move(state));
  return true;
}

}  // namespace Search

}  // export
