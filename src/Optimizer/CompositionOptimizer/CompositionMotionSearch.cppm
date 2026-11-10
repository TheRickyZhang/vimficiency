export module vf.Optimizer.CompositionOptimizer.CompositionMotionSearch;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.NavOptimizer.BufferIndex;
export import vf.Optimizer.NavOptimizer.NavOptimizer;
export import vf.Types.Lines;
export import vf.Types.CharRange;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.NavContext;

export {

struct CompositionRangeMotionSearch {
  Lines subset;
  int beginLine;
  CursorPos localCursor;
  CursorPos localRangeBegin;
  CursorPos localRangeEnd;
  CharInterval motionRange;
  NavBoundary subsetBoundary;
  BufferIndex bufferIndex;
  int bufferIndexLineOffset;
};

struct CompositionBufferIndexWindow {
  BufferIndex index;
  int lineOffset;
};

CompositionBufferIndexWindow buildCompositionBufferIndexWindow(
    const Lines& lines, int navBeginLine, int navEndLine);

std::optional<CompositionRangeMotionSearch> buildCompositionRangeMotionSearch(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    const NavBoundary& boundary,
    const CompositionOptimizerParams& params);

LandingNavResult optimizeCompositionRangeMotion(
    NavOptimizer& navOptimizer,
    const CompositionRangeMotionSearch& search,
    const NavContext& navContext,
    const CompositionOptimizerParams& params,
    int maxResults,
    const SearchControl* control = nullptr);

}  // export
