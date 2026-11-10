module vf.Boundary.TransformBoundary;

import std.compat;

TransformBoundary TransformBoundary::joinResidualBoundary(
    bool hasLinesAbove, bool hasLinesBelow) {
  TransformBoundary boundary;
  boundary.hasLinesAbove_ = hasLinesAbove;
  boundary.hasLinesBelow_ = hasLinesBelow;
  return boundary;
}

TransformBoundary::TransformBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos)
    : prefix_(lines[beginPos.line].substr(0, beginPos.col)),
      // endPos is exclusive and may sit one past an empty line (col 1)
      suffix_(lines[endPos.line].substr(std::min(endPos.col, lines.getSize(endPos.line)))),
      hasLinesAbove_(beginPos.line > 0),
      hasLinesBelow_(endPos.line < lines.lastLine()) {}
