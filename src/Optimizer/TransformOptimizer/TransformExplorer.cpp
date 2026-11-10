module vf.Optimizer.TransformOptimizer.TransformExplorer;

import std.compat;

using namespace std;

pair<int, int> TransformExplorer::computeTransformBounds(const Lines& lines, const CursorPos& cursor) const {
  VimCore::WordBoundaryContext boundary = wordBoundaryContext();
  int contentBegin = boundary.contentStartCol(cursor.line);
  int contentEnd = boundary.effectiveLineEnd(
      lines, cursor.line, cursor.line == lines.lastLine());
  return {contentBegin, contentEnd};
}
