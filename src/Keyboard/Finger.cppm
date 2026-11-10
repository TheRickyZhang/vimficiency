module;
#include <cassert>

export module vf.Keyboard.Finger;

import std.compat;
export import vf.Keyboard.Hand;

export {

enum class Finger : int8_t {
#define X(name, str) name,
#include "XMacroFinger.inc"
#undef X
  None
};

inline constexpr int FINGER_COUNT = static_cast<int>(Finger::None);

// 0 = pinky ... 4 = thumb, so increasing position is an inward roll.
inline int fingerToPosition(Finger f) {
  assert(f != Finger::None);
  return static_cast<uint8_t>(f) % 5;
}

inline Hand fingerToHand(Finger f) {
  assert(f != Finger::None);
  return static_cast<uint8_t>(f) < 5 ? Hand::Left : Hand::Right;
}

}  // export
