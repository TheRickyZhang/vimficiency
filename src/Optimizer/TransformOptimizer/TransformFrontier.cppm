export module vf.Optimizer.TransformOptimizer.TransformFrontier;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Effort.EffortBank;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.FrontierCommon;
export import vf.VimCore.VimOptions;

export {

struct TransformFrontierQuery : FrontierQuery {
  const DiffState& diff;
  bool hasHiddenLinesAbove = false;
  IndentOptions indent;
};

std::vector<Suggestion> collectTransformEdits(
    const TransformFrontierQuery& query,
    const Config& config,
    const EffortBank& bank);

}  // export
