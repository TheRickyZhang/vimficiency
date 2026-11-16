module;
#include <cassert>

module vf.Optimizer.NavOptimizer.NavOptimizer;

import std.compat;
import vf.Optimizer.NavOptimizer.BestPerPos;
import vf.Optimizer.NavOptimizer.NavHeuristic;
import vf.Optimizer.NavOptimizer.NavExplorer;
import vf.Optimizer.SearchFrontier;
import vf.Optimizer.SearchStats;
import vf.Optimizer.NavOptimizer.BufferIndex;
import vf.Types.Lines;
import vf.Types.Pos;
import vf.Utils.Debug;

using namespace std;

using NavPriorityQueue =
    priority_queue<NavState, vector<NavState>, greater<NavState>>;

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& initialPos,
    const CursorPos& goalPos,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext,
    const SearchControl& control) {
  return optimize(lines, initialPos, CharInterval(goalPos, goalPos),
                  params, userSequence, boundary, navContext, control);
}

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& startPos,
    const CharInterval& range,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext,
    const SearchControl& control) {
  BufferIndex localIndex(lines);
  return optimize(lines, startPos, range, params, userSequence,
                  boundary, navContext, localIndex, 0, control);
}

LandingNavResult NavOptimizer::optimize(
    const Lines& lines,
    const CursorPos& startPos,
    const CharInterval& range,
    NavOptimizerParams params,
    string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navContext,
    const BufferIndex& bufferIndex,
    int lineOffset,
    const SearchControl& control) {
  assert(range.isValid() && "target interval must be non-empty");

  // startPos already in the goal interval — nothing to do
  if (range.containsPos(startPos)) {
    NavSearchStats stats;
    std::vector<LandingResult> results;
    results.emplace_back(Sequence{}, 0.0, startPos);
    stats.finalizeRangeGoal(
        static_cast<int>(results.size()),
        1,
        0,
        lines.spanSize(range),
        params.maxResults,
        params.maxNodesPopped,
        0,
        true);
    return LandingNavResult(std::move(results), stats);
  }

  double userEffort = userSequence.empty()
      ? numeric_limits<double>::max()
      : getEffort(userSequence, config);
  const double maxEffort = userEffort * params.exploreFactor;

  NavExplorer explorer(lines, navContext, boundary, params,
                       range, bufferIndex, lineOffset);

  NavStateFactory states(config,
    NavHeuristic::StateScorer{
                       range,
                       params.effortWeight,
                       params.distanceWeight
    }
  );

  // Frontier and best-known cost per position, seeded with the start state.
  NavPriorityQueue pq;
  unordered_map<Pos, double> costMap;
  NavState initialState = states.initial(startPos);
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  // Goal landings, recorded at pop time. Also manages landing stopping conditions
  BestPerPos<LandingResult> landings(
    params.maxResultsPerEndPos,
    params.maxResults,
    lines.spanSize(range)
  );

  // Search accounting, reported via stats.finalizeRangeGoal.
  NavSearchStats stats;
  int totalPops = 0;

  auto isInGoalRange = [&](const NavState& s) {
    return range.containsPos(s.getPos());
  };

  while (!pq.empty() && totalPops < params.maxNodesPopped) {
    if (control.cancelledOrPastDeadline(totalPops)) break;
    NavState s = pq.top(); pq.pop();
    totalPops++;

    CursorPos pos = s.getPos();
    Pos stateKey = s.getKey();
    bool isGoal = range.containsPos(pos);

    if (isGoal) {
      stats.incrementNodesExplored();
      const double cost = s.getEffort();
      landings.add(stateKey, cost, LandingResult(s.getSequence(), cost, pos));
      if (landings.complete()) {
        debug("optimize: landing limit reached (", landings.size(), " landings, ",
              landings.positions(), "/", landings.getTotalPositions(), " positions)");
        break;
      }
      continue;
    }

    auto it = costMap.find(s.getKey());
    if (it != costMap.end() && it->second < s.getCost()) {
      stats.incrementStatesSkipped();
      continue;
    }

    stats.incrementNodesExplored();
    debug("\"" + s.getSequence().str() + "\"", s.getCost());
    stats.maybeRecordExploredState(pos.line, pos.col, s.getEffort(), s.getSequence().str());

    auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
          NavState next = states.afterMotion(s, ks, bank[motionId], endpoint);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onCounted = [&](KSId /*motionId*/, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double extraPenalty) {
          NavState next = states.afterCountedMotion(s, ks, count, endpoint, extraPenalty);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };
    auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
          NavState next = states.afterFMotion(s, motion, newCol);
          stats.incrementMotionsEmitted();
          Search::enqueueRangeState(std::move(next), pq, costMap, maxEffort, isInGoalRange);
        };

    if (params.useDirectionalPruning) {
      explorer.exploreDirectionalStandardMotions(s, onStatic);
    } else {
      explorer.exploreAllStandardMotions(s, onStatic);
    }
    explorer.exploreCountedMotions(s, onCounted, onFMotion);
  }

  if constexpr (DEBUG_ENABLED) {
    debug("---costMap---");
    map<Pos, double> tempMap(costMap.begin(), costMap.end());
    for (auto [state, cost] : tempMap) {
      auto [l, c] = state;
      debug(l, c, cost);
    }
  }

  stats.finalizeRangeGoal(
      landings.size(),
      landings.positions(),
      static_cast<int>(pq.size()),
      landings.getTotalPositions(),
      params.maxResults,
      params.maxNodesPopped,
      totalPops,
      pq.empty());

  vector<LandingResult> results = landings.take();
  ranges::stable_sort(results, {}, &LandingResult::getCost);
  return LandingNavResult(std::move(results), stats);
}
