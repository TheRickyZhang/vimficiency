export module vf.LuaExports.ViewRegistry;

import std.compat;
import vf.LuaExports.Common;
export import vf.Explore.Explore;

export {

namespace VF::LuaExports {

// Owns all live Explore views. Lua holds opaque integer handles handed out
// by `create`; a handle that is not live (stale, used after destroy) is an
// error for Lua to raise.
class ViewRegistry {
 public:
  template <typename... Args>
  int create(Args&&... args) {
    const int id = ++next_id_;
    views_.try_emplace(id, std::forward<Args>(args)...);
    return id;
  }

  inline bool destroy(int id) { return views_.erase(id) > 0; }

  template <typename Body>
  ProtoResult with(int id, Body body) {
    auto it = views_.find(id);
    if (it == views_.end()) return unexpectedError("unknown view_id " + std::to_string(id));
    return body(it->second);
  }

 private:
  std::unordered_map<int, Explore::View> views_;
  int next_id_ = 0;
};

}  // namespace VF::LuaExports

}  // export
