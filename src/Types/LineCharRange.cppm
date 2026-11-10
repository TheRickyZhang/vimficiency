export module vf.Types.LineCharRange;

import std.compat;
export import vf.Types.CursorPos;

export {

// Half-open: [line(beginLine), end).
struct LineCharRange {
  int beginLine;
  CursorPos end;

  constexpr LineCharRange(int beginLine, CursorPos endPos)
      : beginLine(beginLine), end(endPos) {}

  inline bool isValid() const {
    return beginLine >= 0 && end.isValid() && end.line >= beginLine;
  }

  inline int lineCountTouched() const {
    if (!isValid()) return 0;
    return end.line == beginLine ? 1 : (end.line - beginLine + 1);
  }
};

constexpr LineCharRange LINE_CHAR_RANGE_OUTSIDE_BOUNDARY(
    -1, POSITION_OUTSIDE_BOUNDARY);

}  // export
