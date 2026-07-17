// X32 → REAPER Mirror
// CoalescingQueue: a small mutex-guarded map keyed by (StripId, Param) that
// keeps only the latest value per key. The socket thread pushes decoded strip
// events at up to ~50 Hz per fader; the main-thread timer drains at ~30 Hz.
// Coalescing means a burst (e.g. a scene load) collapses to one apply per
// parameter — REAPER never sees more work than there are distinct params.
#ifndef X32MIRROR_EVENT_QUEUE_H_
#define X32MIRROR_EVENT_QUEUE_H_

#include <mutex>
#include <unordered_map>
#include <vector>

#include "strip_id.h"

namespace x32 {

// A single strip parameter update. value carries the raw console value:
//   * fader: 0..1 float (as double)
//   * mute:  1.0 = ON/unmuted, 0.0 = muted  (X32 convention, inverted vs REAPER)
struct StripEvent {
  StripId id;
  Param param = Param::Fader;
  double value = 0.0;
};

class CoalescingQueue {
 public:
  // Push (or overwrite) the latest value for (id, param). Thread-safe.
  void Push(const StripEvent& ev) {
    std::lock_guard<std::mutex> lock(mu_);
    latest_[Key(ev.id, ev.param)] = ev;
  }

  // Move all pending events out into `out` (cleared first). Thread-safe.
  // Order is unspecified (per-key latest only).
  void Drain(std::vector<StripEvent>* out) {
    out->clear();
    std::lock_guard<std::mutex> lock(mu_);
    out->reserve(latest_.size());
    for (auto& kv : latest_) out->push_back(kv.second);
    latest_.clear();
  }

  size_t SizeApprox() {
    std::lock_guard<std::mutex> lock(mu_);
    return latest_.size();
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    latest_.clear();
  }

 private:
  static uint64_t Key(const StripId& id, Param p) {
    return (static_cast<uint64_t>(id.key()) << 4) |
           static_cast<uint64_t>(p);
  }

  std::mutex mu_;
  std::unordered_map<uint64_t, StripEvent> latest_;
};

}  // namespace x32

#endif  // X32MIRROR_EVENT_QUEUE_H_
