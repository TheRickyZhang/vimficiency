export module vf.Types.Pos;

import std.compat;

export {

struct Pos {
  int line = 0;
  int col = 0;

  Pos() = default;
  constexpr Pos(int l, int c) : line(l), col(c) {}

  constexpr bool isValid() const { return line >= 0; }

  // Lexicographic ordering on (line, col).
  constexpr bool operator==(const Pos&) const = default;
  constexpr auto operator<=>(const Pos&) const = default;

  inline friend std::ostream& operator<<(std::ostream& os, const Pos& p) {
    os << "(" << p.line << ", " << p.col << ")";
    return os;
  }
};

template<>
struct std::hash<Pos> {
  inline size_t operator()(const Pos& k) const {
    size_t h = k.line;
    h ^= k.col + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

}  // export
