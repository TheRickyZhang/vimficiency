export module vf.Optimizer.TransformOptimizer.TransformOptimizer;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Boundary.TransformBoundary;
export import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
export import vf.Optimizer.TransformOptimizer.TransformResult;

export {

struct TransformOptimizer {
  Config config;

  inline TransformOptimizer(const Config& config) : config(config) {}

  // Historical name retained: this is the transform-layer optimizer.
  // Finds optimal sequences to transform initialLines to goalLines.
  // Uses suffix caching for cross-position sharing: when one starting position
  // finds a path through an intermediate state, the remaining commands are
  // cached so other positions reaching the same state get an instant result.
  // goalLines must not be a pure-deletion goal ({} or {""}); pure deletions
  // should use optimizePureDeletion().
  // Returns results indexed by flattened starting position
  TransformResult optimizeTransform(
      const Lines& initialLines,
      const Lines& goalLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );

  TransformResult optimizePureDeletion(
      const Lines& initialLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params = {},
      int bufferBeginLine = 0,
      int bufferBeginCol = 0,
      CursorPos goalPos = {0, 0}
  );


private:
  // Unified implementation: PureDeletion=true for deletion-only, false for full edit
  template<bool PureDeletion>
  TransformResult optimizeImpl(
      const Lines& initialLines,
      const Lines& goalLines,
      TransformBoundary transformBoundary,
      TransformOptimizerParams params,
      int bufferBeginLine,
      int bufferBeginCol,
      CursorPos goalPos
  );
};

}  // export
