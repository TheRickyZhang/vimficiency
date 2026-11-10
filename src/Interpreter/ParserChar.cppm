export module vf.Interpreter.ParserChar;

import std.compat;

export {

// Parser inputs can be arbitrary bytes at FFI/fuzz boundaries. Avoid
// std::isdigit: it is undefined for negative signed char values, and Vim
// count prefixes are ASCII-only.
inline bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

}  // export
