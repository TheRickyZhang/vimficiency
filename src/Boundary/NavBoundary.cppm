export module vf.Boundary.NavBoundary;

import std.compat;
export import vf.Boundary.BoundaryContext;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// NavBoundary constrains the search space for NavOptimizer.
//
// Like TransformBoundary, stores information about what's outside the region:
// - hasLinesAbove/Below: for motions that would target hidden outside lines
// - leftColOffset: prefix length on begin line (0 = no prefix)
// - rightColOffset: suffix length on end line (0 = no suffix)
//
// The default constructor has no outside context, so any motion is possible.

class NavBoundary {
  BoundaryContext ctx_;

public:
  NavBoundary() = default;

  // endPos is exclusive: one past the last valid cursor position on the end line
  NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos);

  // Explicit external context flags (for FFI), OR'ed with the buffer-derived ones
  NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                 bool hasLinesAbove, bool hasLinesBelow);

  // No TransformBoundary→NavBoundary conversion. The two carry the same
  // line-level flags, but TransformBoundary's leftColOffset/rightColOffset
  // describe the prefix/suffix being protected during a transform, which is
  // wrong context for visual-replay nav: the col clip would let motions
  // like `$` appear to land at the slice end while really landing inside
  // the protected suffix on replay. Construct NavBoundary explicitly with
  // only the flags you actually want.

  inline bool hasLinesAbove() const { return ctx_.hasLinesAbove; }
  inline bool hasLinesBelow() const { return ctx_.hasLinesBelow; }
  inline int leftColOffset() const { return ctx_.leftColOffset; }
  inline int rightColOffset() const { return ctx_.rightColOffset; }

  bool isPositionInBounds(const CursorPos& pos, int lastLine, int lastLineLength) const;
};

}  // export
