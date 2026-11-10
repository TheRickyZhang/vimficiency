export module vf.Types.CharLineRange;

import std.compat;
export import vf.Types.CursorPos;

export {

// Half-open: [begin, line(end)).
struct CharLineRange {
  CursorPos begin;
  int endLine;

  constexpr CharLineRange(CursorPos beginPos, int endLine)
      : begin(beginPos), endLine(endLine) {}

  inline bool isValid() const {
    return begin.isValid() && endLine > begin.line;
  }

  inline int lineCountTouched() const {
    if (!isValid()) return 0;
    return endLine - begin.line;
  }
};

constexpr CharLineRange CHAR_LINE_RANGE_OUTSIDE_BOUNDARY(
    POSITION_OUTSIDE_BOUNDARY, -1);

}  // export
