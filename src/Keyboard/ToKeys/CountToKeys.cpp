module;
#include <cassert>

module vf.Keyboard.ToKeys.CountToKeys;

import std.compat;
import vf.Keyboard.PhysicalKeys;

namespace CountToKeys {
namespace {
constexpr std::array<Key, 10> DIGIT_KEYS = {
    Key::Key_0, Key::Key_1, Key::Key_2, Key::Key_3, Key::Key_4,
    Key::Key_5, Key::Key_6, Key::Key_7, Key::Key_8, Key::Key_9,
};

const std::array<PhysicalKeys, CountPrefixLimits::MAX_PREFIX_COUNT + 1> PREFIX_KEYS = [] {
  std::array<PhysicalKeys, CountPrefixLimits::MAX_PREFIX_COUNT + 1> table;
  for (int count = 1; count <= CountPrefixLimits::MAX_PREFIX_COUNT; ++count) {
    if (count >= 10) {
      table[count].push_back(DIGIT_KEYS[count / 10]);
    }
    table[count].push_back(DIGIT_KEYS[count % 10]);
  }
  return table;
}();

const std::array<std::string, CountPrefixLimits::MAX_PREFIX_COUNT + 1> PREFIX_TEXT = [] {
  std::array<std::string, CountPrefixLimits::MAX_PREFIX_COUNT + 1> table;
  for (int count = 1; count <= CountPrefixLimits::MAX_PREFIX_COUNT; ++count) {
    table[count] = std::to_string(count);
  }
  return table;
}();
}  // namespace

const PhysicalKeys& keysForCount(int count) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  return PREFIX_KEYS[count];
}

std::string_view textForCount(int count) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  return PREFIX_TEXT[count];
}

}  // namespace CountToKeys
