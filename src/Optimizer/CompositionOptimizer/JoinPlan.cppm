export module vf.Optimizer.CompositionOptimizer.JoinPlan;

import std.compat;
export import vf.Types.CursorPos;
export import vf.Types.Sequence;

export {

// Composition-owned join escape hatch. The first action must be `J`; plans
// whose useful work starts with a residual edit belong to TransformOptimizer.
struct JoinPlan {
  Sequence sequence;
  CursorPos goalPos;
  double effort;
  int entryLine;
};

}  // export
