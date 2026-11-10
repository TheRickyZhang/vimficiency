export module vf.Types.CharRange;

import std.compat;
export import vf.Types.CursorPos;

export {

// Character-wise half-open: [begin, end).
struct CharRange {
  CursorPos begin;
  CursorPos end;

  CharRange() = default;
  constexpr CharRange(CursorPos beginPos, CursorPos endPos) : begin(beginPos), end(endPos) {}

  // Ensure begin <= end (lexicographic on line/col).
  inline void normalize() {
    if (begin > end) {
      std::swap(begin, end);
    }
  }

  inline bool isValid() const {
    return begin.isValid() && end.isValid() && end >= begin;
  }

  inline bool spansMultiple() const {
    return begin.line != end.line;
  }

  inline bool isEmpty() const {
    return begin == end;
  }
};

constexpr CharRange CHAR_RANGE_OUTSIDE_BOUNDARY{
    POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY};

}  // export
