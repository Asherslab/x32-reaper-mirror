// X32 → REAPER Mirror
// The main-thread mirror engine. On each ~30 Hz timer tick it:
//   * detects a project-tab switch and reloads the binding store,
//   * drains the connection's control events (status, discovery),
//   * drains the CoalescingQueue and applies each strip event to every bound
//     track via the control-surface API (write/touch aware, no gang, no undo).
//
// Effective mirroring for a parameter is: master && binding.enabled && flag.
// A fader apply is gated by a 0.05 dB epsilon so UDP jitter / re-polls do not
// spam identical volume changes.
#ifndef X32MIRROR_MIRROR_ENGINE_H_
#define X32MIRROR_MIRROR_ENGINE_H_

#include <atomic>
#include <string>
#include <vector>

#include "binding_state.h"
#include "binding_store.h"
#include "event_queue.h"
#include "state_cache.h"
#include "x32_connection.h"

namespace x32 {

struct DiscoveredConsole {
  std::string ip, name, model, firmware;
};

class MirrorEngine {
 public:
  MirrorEngine(BindingStore* store, CoalescingQueue* queue, StateCache* cache,
               X32Connection* conn);

  // Master switch (global). effective = master && binding.enabled && flag.
  void SetMaster(bool on);
  bool master() const { return master_.load(); }

  // Main-thread timer entry point.
  void Tick();

  // Seed an immediate apply for one/all bindings from the StateCache. Called
  // after a binding is created/enabled and when the link (re)enters LIVE.
  void SeedBinding(const std::string& guid);
  void SeedAll();

  // Monotonic change counter for UI consumers (companion / panel).
  unsigned int state_serial() const { return serial_.load(); }
  void BumpSerial() { serial_.fetch_add(1); }

  // Status snapshot for the panel / companion.
  StatusView status() const;
  ConnState conn_state() const { return conn_state_; }

  // Pop accumulated discovery results (clears the internal list).
  std::vector<DiscoveredConsole> TakeDiscoveries();

  // Fill a BindingView for the given GUID (companion API). Returns false if no
  // binding exists.
  bool BuildBindingView(const std::string& guid, BindingView* out);

 private:
  void CheckProjectSwitch();
  void DrainControl();
  void ApplyEvent(const StripEvent& e);
  void ApplyToBinding(Binding* b, Param p, double value, bool force);
  bool EffectiveOn(const Binding& b, Param p) const;

  BindingStore* store_;
  CoalescingQueue* queue_;
  StateCache* cache_;
  X32Connection* conn_;

  std::atomic<bool> master_{true};
  std::atomic<unsigned int> serial_{1};

  ConnState conn_state_ = ConnState::Disconnected;
  std::string status_text_ = "Disconnected";
  Endpoint peer_;

  std::vector<DiscoveredConsole> discoveries_;
  std::vector<StripEvent> scratch_;  // reused drain buffer
};

}  // namespace x32

#endif  // X32MIRROR_MIRROR_ENGINE_H_
