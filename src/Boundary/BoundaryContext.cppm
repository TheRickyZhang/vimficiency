export module vf.Boundary.BoundaryContext;

import std.compat;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// Shared boundary offset/line info for NavBoundary and TransformBoundary:
// - hasLinesAbove/Below: hidden outside-line context for escape-capable motions
// - leftColOffset/rightColOffset: protected column ranges at line boundaries
struct BoundaryContext {
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  int leftColOffset = 0;   // prefix length: columns < this on line 0 are forbidden
  int rightColOffset = 0;  // suffix length: columns >= (lineLen - this) on end line are forbidden

  BoundaryContext() = default;

  // endPos is exclusive: one past the last valid cursor position on the end line.
  // The flags are OR'ed with the buffer-derived line context.
  BoundaryContext(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                  bool hasLinesAbove, bool hasLinesBelow);
};

}  // export
