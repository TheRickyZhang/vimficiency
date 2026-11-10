export module vf.Types.QuoteFlags;

import std.compat;

export {

class QuoteFlags {
  uint8_t flags = 0;

  static constexpr uint8_t DoubleQuote = 1 << 0;
  static constexpr uint8_t SingleQuote = 1 << 1;
  static constexpr uint8_t Backtick = 1 << 2;

  static constexpr uint8_t maskFor(char c) {
    switch (c) {
      case '"':
        return DoubleQuote;
      case '\'':
        return SingleQuote;
      case '`':
        return Backtick;
      default:
        return 0;
    }
  }

public:
  inline void add(char c) { flags |= maskFor(c); }
  inline bool seen(char c) const { return flags & maskFor(c); }

  static constexpr std::array<char, 3> ALL_QUOTES = {'"', '\'', '`'};
};

}  // export
