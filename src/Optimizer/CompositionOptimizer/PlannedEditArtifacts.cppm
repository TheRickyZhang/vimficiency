export module vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;

import std.compat;
export import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
export import vf.Optimizer.CompositionOptimizer.JoinPlan;
export import vf.Keyboard.Config;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.TransformOptimizer.TransformOptimizer;
export import vf.Types.Lines;

export {

TransformResult computeTransformResultForDiff(
    const DiffState& diff,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);

BracketQuoteContext computeTextObjectContextForDiff(
    const DiffState& diff,
    const Lines& preEditLines);

std::optional<JoinPlan> computeJoinPlanForDiff(
    const DiffState& diff,
    const Lines& preEditLines,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);

}  // export
