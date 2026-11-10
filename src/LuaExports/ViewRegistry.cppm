module;
#include "Utils/CheckMacro.h"

export module vf.LuaExports.ViewRegistry;

import std.compat;
export import vf.Explore.Explore;
export import vf.Utils.Debug;

export {

namespace VF::LuaExports {

// Owns all live Explore views. Lua holds opaque integer handles handed out
// by `create`; lookup misses are programmer bugs (stale handle, use-after-
// destroy) and abort the process via CHECK.
class ViewRegistry {
 public:
  template <typename... Args>
  int create(Args&&... args) {
    const int id = ++next_id_;
    views_.try_emplace(id, std::forward<Args>(args)...);
    return id;
  }

  inline bool destroy(int id) { return views_.erase(id) > 0; }

  inline Explore::View& get(int id) {
    auto it = views_.find(id);
    CHECK(it != views_.end(), "view_id from Lua not present in registry");
    return it->second;
  }

 private:
  std::unordered_map<int, Explore::View> views_;
  int next_id_ = 0;
};

}  // namespace VF::LuaExports

}  // export
