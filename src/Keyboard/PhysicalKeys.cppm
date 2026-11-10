module;
#include <cassert>

export module vf.Keyboard.PhysicalKeys;

import std.compat;
export import vf.Keyboard.Key;

export {

// Represents physical key presses for effort calculation.
// Used by RunningEffort to compute typing cost based on hand/finger patterns.
class PhysicalKeys {
  std::vector<Key> keys;

public:
  PhysicalKeys() = default;
  inline PhysicalKeys(std::initializer_list<Key> init) : keys(init) {}
  inline PhysicalKeys(std::vector<Key> init) : keys(std::move(init)) {}

  inline size_t size() const { return keys.size(); }
  inline bool empty() const { return keys.empty(); }
  inline auto begin() const { return keys.begin(); }
  inline auto end() const { return keys.end(); }
  inline void reserve(size_t n) { keys.reserve(n); }

  inline void push_back(const Key& k) { keys.push_back(k); }

  PhysicalKeys& append(const PhysicalKeys& ks, size_t cnt = 1);

  inline PhysicalKeys asChange() const {
    assert(keys[0] == Key::Key_D);
    PhysicalKeys result = *this;
    result.keys[0] = Key::Key_C;
    return result;
  }

  inline PhysicalKeys& operator+=(const PhysicalKeys& other) { return append(other); }
  inline bool operator==(const PhysicalKeys& other) const {
    return keys == other.keys;
  }
};

std::ostream& operator<<(std::ostream& os, const PhysicalKeys& ks);

}  // export
