export module vf.Keyboard.Key;

import std.compat;

export {

enum class Key : int {
#define X(name, str) name,
#include "XMacroKey.inc"
#undef X
  None
};

inline constexpr int KEY_COUNT = static_cast<int>(Key::None);

}  // export
