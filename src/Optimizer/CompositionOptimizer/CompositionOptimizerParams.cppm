module;
#include "Optimizer/OptimizerParamFields.h"

export module vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;

import std.compat;
export import vf.Optimizer.OptimizerParamsBase;

export {

struct CompositionOptimizerParams : OptimizerParamsBase {
  COMPOSITION_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)
  enum class Field { COMPOSITION_ALL_FIELDS(VF_PARAMS_FIELD_ENUM) COUNT };

#define VF_PARAMS_SELF CompositionOptimizerParams
  COMPOSITION_ALL_FIELDS(VF_PARAMS_WITH_SETTER)
#undef VF_PARAMS_SELF

  inline static CompositionOptimizerParams dijkstra(int maxResults = 20, int maxNodesPopped = 50000) {
    return CompositionOptimizerParams{}
        .withMaxResults(maxResults)
        .withMaxNodesPopped(maxNodesPopped)
        .withDistanceWeight(0.0);
  }
};

}  // export
