module vf.Keyboard.PhysicalKeys;

import std.compat;

PhysicalKeys& PhysicalKeys::append(const PhysicalKeys& ks, size_t cnt) {
  if (cnt == 0 || ks.empty()) return *this;
  keys.reserve(keys.size() + ks.size() * cnt);
  for (size_t i = 0; i < cnt; i++) {
    keys.insert(keys.end(), ks.begin(), ks.end());
  }
  return *this;
}

std::ostream& operator<<(std::ostream& os, const PhysicalKeys& ks) {
  for (Key k : ks) {
    os << static_cast<int>(k) << " ";
  }
  return os;
}
