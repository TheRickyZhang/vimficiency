export module vf.Types.CharRange;

import std.compat;
export import vf.Types.CursorPos;

export {

// Character-wise half-open: [begin, end).
// TODO
struct CharRange {
  CursorPos begin;
  CursorPos end;

  CharRange() = default;
  constexpr CharRange(CursorPos beginPos, CursorPos endPos) : begin(beginPos), end(endPos) {}

  // Ensure begin <= end (lexicographic on line/col).
  inline void normalize() {
    if (begin > end) {
      begin.swap(end);
    }
  }

  inline bool isValid() const {
    return begin.isValid() && end.isValid() && end >= begin;
  }

  inline bool spansMultiple() const {
    return begin.line != end.line;
  }

  inline bool isEmpty() const {
    return begin.line == end.line && begin.col == end.col;
  }

  inline bool containsPos(const CursorPos& p) const {
    return begin <= p && p < end;
  }

  inline int size() const {
    if (!isValid() || isEmpty()) return 0;
    return end.line - begin.line + 1;
  }
};

constexpr CharRange CHAR_RANGE_OUTSIDE_BOUNDARY{
    POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};

}  // export
