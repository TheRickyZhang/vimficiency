module vf.Keyboard.ToKeys.CommandToKeys;

import std.compat;

using namespace std;

CommandToKeys combineAll(initializer_list<reference_wrapper<const CommandToKeys>> maps) {
  CommandToKeys res;

  for (const auto& mpref : maps) {
    const auto& mp = mpref.get();
    for (const auto& [k, v] : mp) {
      auto [it, inserted] = res.try_emplace(k, v);
      if (!inserted) {
        if (it->second != v) {
          cerr << "combineAll conflict for key: " << k
               << " old=" << it->second << " new=" << v << "\n";
        }
        it->second = v;
      }
    }
  }
  return res;
}
