export module vf.LuaExports.AsyncJobRegistry;

import std.compat;

export {

namespace VF::LuaExports {

// Owns pending async jobs keyed by an integer id handed back to Lua. Like
// ViewRegistry, only ever touched on the main (Lua) thread; workers touch only
// their own job object. `JobT` supplies the worker/cancel/done machinery.
template <typename JobT>
class JobRegistry {
 public:
  inline int create(std::unique_ptr<JobT> job) {
    const int id = ++next_id_;
    jobs_.emplace(id, std::move(job));
    return id;
  }
  inline bool running(int id) const {
    auto it = jobs_.find(id);
    return it != jobs_.end() && !it->second->ready();
  }
  inline std::unique_ptr<JobT> take(int id) {
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return nullptr;
    auto job = std::move(it->second);
    jobs_.erase(it);
    return job;
  }
  // False when `id` is not live. Dropping the job joins its worker (see AsyncJob).
  inline bool cancel(int id) {
    const std::unique_ptr<JobT> job = take(id);
    if (!job) return false;
    job->cancel();
    return true;
  }

 private:
  std::unordered_map<int, std::unique_ptr<JobT>> jobs_;
  int next_id_ = 0;
};

}  // namespace VF::LuaExports

}  // export
