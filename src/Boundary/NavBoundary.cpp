module vf.Boundary.NavBoundary;

import std.compat;

NavBoundary::NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos)
    : NavBoundary(lines, beginPos, endPos, false, false) {}

NavBoundary::NavBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
                               bool hasLinesAbove, bool hasLinesBelow)
    : ctx_(lines, beginPos, endPos, hasLinesAbove, hasLinesBelow) {}

bool NavBoundary::isPositionInBounds(const CursorPos& pos, int lastLine, int lastLineLength) const {
  if (ctx_.leftColOffset > 0 && pos.line == 0 && pos.col < ctx_.leftColOffset) {
    return false;
  }
  if (ctx_.rightColOffset > 0 && pos.line == lastLine &&
      pos.col >= lastLineLength - ctx_.rightColOffset) {
    return false;
  }
  return true;
}
