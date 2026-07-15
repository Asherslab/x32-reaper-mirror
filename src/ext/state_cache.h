// X32 → REAPER Mirror
// StateCache: the last-known console value for every strip parameter we have
// ever seen. Written by the socket thread as events arrive; read by the main
// thread to seed an immediate apply when a binding is created/enabled or when
// the connection (re)enters LIVE. Mutex-guarded, no REAPER dependencies.
#ifndef X32MIRROR_STATE_CACHE_H_
#define X32MIRROR_STATE_CACHE_H_

#include <mutex>
#include <unordered_map>

#include "strip_id.h"

namespace x32 {

class StateCache {
 public:
  void Set(const StripId& id, Param p, double value) {
    std::lock_guard<std::mutex> lock(mu_);
    values_[Key(id, p)] = value;
  }

  // Returns true and fills *out if a value is known for (id, p).
  bool Get(const StripId& id, Param p, double* out) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = values_.find(Key(id, p));
    if (it == values_.end()) return false;
    *out = it->second;
    return true;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    values_.clear();
  }

 private:
  static uint64_t Key(const StripId& id, Param p) {
    return (static_cast<uint64_t>(id.key()) << 4) |
           static_cast<uint64_t>(p);
  }

  mutable std::mutex mu_;
  std::unordered_map<uint64_t, double> values_;
};

}  // namespace x32

#endif  // X32MIRROR_STATE_CACHE_H_
