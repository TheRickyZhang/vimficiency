export module vf.Optimizer.CompositionOptimizer.CompositionNavParams;

import std.compat;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.NavOptimizer.NavOptimizerParams;

export {

inline NavOptimizerParams navParamsForCompositionMotion(
    const CompositionOptimizerParams& params) {
  return NavOptimizerParams{}
      .withMinCountRepeat(params.minPrefixCount)
      .withMaxCountRepeat(params.maxPrefixCount)
      .withFMotionThreshold(params.fMotionThreshold)
      .withDirectionalPruning(params.useDirectionalPruning);
}

}  // export
