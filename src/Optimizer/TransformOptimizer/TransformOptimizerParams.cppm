module;
#include "Optimizer/OptimizerParamFields.h"

export module vf.Optimizer.TransformOptimizer.TransformOptimizerParams;

import std.compat;
export import vf.Optimizer.OptimizerParamsBase;

export {

struct TransformOptimizerParams : OptimizerParamsBase {
  TRANSFORM_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

#define VF_PARAMS_SELF TransformOptimizerParams
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_WITH_SETTER)
  TRANSFORM_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  inline static TransformOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return TransformOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};

}  // export
