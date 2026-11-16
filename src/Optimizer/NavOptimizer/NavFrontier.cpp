module;
#include "Utils/CheckMacro.h"

module vf.Optimizer.NavOptimizer.NavFrontier;

import std.compat;
import vf.Effort.RunningEffort;
import vf.Optimizer.NavOptimizer.BestPerPos;
import vf.Keyboard.KeyedSequence;
import vf.Keyboard.ToKeys.MovementToKeys;
import vf.Optimizer.NavOptimizer.BufferIndex;
import vf.Optimizer.NavOptimizer.NavExplorer;
import vf.Optimizer.NavOptimizer.NavHeuristic;
import vf.Optimizer.NavOptimizer.NavOptimizerParams;
import vf.Optimizer.NavOptimizer.NavState;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Utils.Debug;

using namespace std;

vector<Suggestion> rankNavFrontier(
    const NavFrontierQuery& query,
    const Config& config,
    const EffortBank& bank) {
  // Convert from range -> interval
  const CharInterval motionRange(query.targetRange, query.lines);

  const NavOptimizerParams params = query.overrides
      ? query.overrides->resolved(NavOptimizerParams{})
      : NavOptimizerParams{};

  BufferIndex bufferIndex(query.lines);
  NavExplorer explorer(query.lines, query.navContext, query.boundary,
                          params, motionRange, bufferIndex, 0);

  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort();

  NavStateFactory states(
      config, NavHeuristic::StateScorer{motionRange, params.effortWeight, params.distanceWeight});
  NavState base = states.initial(query.cursor, acceptedEffort);

  auto sortValue = [&](const Suggestion& s) {
    return suggestionSortValue(s, query.sortMode);
  };

  // Cap up to `params.maxResultsPerEndPos` distinct tokens per landing
  // cell. Default 1 keeps just the cheapest token per cell; values > 1
  // surface multiple distinct paths to the same cell (e.g. `w` / `W` /
  // `e` all reaching the same word start).
  BestPerPos<Suggestion> resultsByLanding(params.maxResultsPerEndPos);

  auto emit = [&](const NavState& s) {
    Suggestion suggestion{
        .token = Token{s.getSequence().str()},
        .landingPos = s.getPos(),
    };
    updateSuggestionMetrics(
        suggestion, s.getEffort() - acceptedCost,
        NavHeuristic::distanceToRange(motionRange, s.getPos()),
        params.effortWeight, params.distanceWeight);

    resultsByLanding.add(s.getPos().pos(), sortValue(suggestion), std::move(suggestion));
  };

  auto onStatic = [&](KSId motionId, const KeyedSequence& ks, CursorPos endpoint) {
    emit(states.afterMotion(base, ks, bank[motionId], endpoint));
  };
  auto onCounted = [&](KSId /*motionId*/, const KeyedSequence& ks, int count,
                       CursorPos endpoint, double extraPenalty) {
    emit(states.afterCountedMotion(base, ks, count, endpoint, extraPenalty));
  };
  auto onFMotion = [&](const KeyedSequence& motion, int newCol) {
    emit(states.afterFMotion(base, motion, newCol));
  };

  if (params.useDirectionalPruning) {
    explorer.exploreDirectionalStandardMotions(base, onStatic);
  } else {
    explorer.exploreAllStandardMotions(base, onStatic);
  }
  explorer.exploreCountedMotions(base, onCounted, onFMotion);

  vector<Suggestion> items = resultsByLanding.take();
  sortAndCapSuggestions(items, query.maxCount, query.sortMode);

  // Pairwise-distinct tokens. Per-cell dedup above bounds each landing cell,
  // but a global CHECK guards against any future enumeration overlap.
  unordered_set<string> seen;
  for (const Suggestion& s : items) {
    CHECK(seen.insert(string(s.token)).second,
          "duplicate NavFrontier token; fix enumerator overlap");
  }
  return items;
}
