module;
#include "Optimizer/OptimizerParamFields.h"

export module vf.Optimizer.NavOptimizer.NavOptimizerParams;

import std.compat;
export import vf.Optimizer.OptimizerParamsBase;

export {

struct NavOptimizerParams : OptimizerParamsBase {
  NAV_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

#define VF_PARAMS_SELF NavOptimizerParams
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_WITH_SETTER)
  NAV_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  // C++20 designated init can't reach base-class fields, so chained setters.
  inline static NavOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return NavOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};

}  // export
