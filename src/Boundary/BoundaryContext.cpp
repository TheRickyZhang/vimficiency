module vf.Boundary.BoundaryContext;

import std.compat;

BoundaryContext::BoundaryContext(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                                 bool hasLinesAbove, bool hasLinesBelow)
    : hasLinesAbove(hasLinesAbove || beginPos.line > 0),
      hasLinesBelow(hasLinesBelow || endPos.line < lines.lastLine()),
      leftColOffset(beginPos.col),
      rightColOffset(lines.getSize(endPos.line) - endPos.col) {
  if (rightColOffset < 0) rightColOffset = 0;
}
