#include "mirror_engine.h"

#include <cmath>
#include <cstring>

#include "reaper_api.h"
#include "x32_addresses.h"

namespace x32 {

namespace {
constexpr double kFaderEpsilonDb = 0.05;
}

MirrorEngine::MirrorEngine(BindingStore* store, CoalescingQueue* queue,
                           StateCache* cache, X32Connection* conn)
    : store_(store), queue_(queue), cache_(cache), conn_(conn) {}

void MirrorEngine::SetMaster(bool on) {
  if (master_.exchange(on) != on) {
    BumpSerial();
    if (on) SeedAll();  // pushing values back out when re-enabled
  }
}

bool MirrorEngine::EffectiveOn(const Binding& b, Param p) const {
  if (!master_.load()) return false;
  if (!b.enabled) return false;
  return p == Param::Mute ? b.mirror_mute : b.mirror_fader;
}

void MirrorEngine::Tick() {
  CheckProjectSwitch();
  DrainControl();

  queue_->Drain(&scratch_);
  for (const StripEvent& e : scratch_) ApplyEvent(e);
}

void MirrorEngine::CheckProjectSwitch() {
  if (!EnumProjects) return;
  ReaProject* cur = EnumProjects(-1, nullptr, 0);
  if (cur != store_->project()) {
    store_->SetProject(cur);  // reloads bindings + invalidates track cache
    SeedAll();
    BumpSerial();
  }
}

void MirrorEngine::DrainControl() {
  X32Connection::ControlEvent ev;
  while (conn_->PollControl(&ev)) {
    if (ev.type == X32Connection::ControlEvent::Type::StateChanged) {
      bool was_live = conn_state_ == ConnState::Live;
      conn_state_ = ev.state;
      status_text_ = ev.status_text;
      peer_ = ev.peer;
      BumpSerial();
      if (ev.state == ConnState::Live && !was_live) SeedAll();
    } else {  // Discovered
      DiscoveredConsole d{ev.disc_ip, ev.disc_name, ev.disc_model,
                          ev.disc_firmware};
      // De-dup by IP.
      bool seen = false;
      for (auto& e2 : discoveries_)
        if (e2.ip == d.ip) seen = true;
      if (!seen) {
        discoveries_.push_back(d);
        BumpSerial();
      }
    }
  }
}

void MirrorEngine::ApplyEvent(const StripEvent& e) {
  const std::vector<std::string>* guids = store_->BindingsForStrip(e.id);
  if (!guids) return;
  for (const std::string& g : *guids) {
    Binding* b = store_->Get(g);
    if (b) ApplyToBinding(b, e.param, e.value, /*force=*/false);
  }
}

void MirrorEngine::ApplyToBinding(Binding* b, Param p, double value,
                                  bool force) {
  if (!EffectiveOn(*b, p)) return;
  MediaTrack* tr = store_->ResolveTrack(b);
  if (!tr) return;  // unresolved GUID → drop silently (shown "missing" in UI)

  if (p == Param::Fader) {
    double db = FaderFloatToDb(static_cast<float>(value));
    if (!force && b->applied_db < 1e8 &&
        std::fabs(db - b->applied_db) <= kFaderEpsilonDb) {
      return;  // within epsilon; skip
    }
    double vol = FaderFloatToVol(static_cast<float>(value));
    if (CSurf_OnVolumeChangeEx)
      CSurf_OnVolumeChangeEx(tr, vol, /*relative=*/false, /*allowGang=*/false);
    b->applied_db = db;
  } else {  // Mute: X32 on(1)=unmuted / 0=muted → REAPER mute is inverted.
    int mute = value < 0.5 ? 1 : 0;
    if (!force && b->applied_mute == mute) return;
    if (CSurf_OnMuteChangeEx)
      CSurf_OnMuteChangeEx(tr, mute, /*allowgang=*/false);
    b->applied_mute = mute;
  }
}

void MirrorEngine::SeedBinding(const std::string& guid) {
  Binding* b = store_->Get(guid);
  if (!b) return;
  double v;
  if (EffectiveOn(*b, Param::Fader) &&
      cache_->Get(b->strip, Param::Fader, &v))
    ApplyToBinding(b, Param::Fader, v, /*force=*/true);
  if (EffectiveOn(*b, Param::Mute) && cache_->Get(b->strip, Param::Mute, &v))
    ApplyToBinding(b, Param::Mute, v, /*force=*/true);
  BumpSerial();
}

void MirrorEngine::SeedAll() {
  for (auto& kv : const_cast<std::unordered_map<std::string, Binding>&>(
           store_->all())) {
    Binding* b = &kv.second;
    double v;
    if (EffectiveOn(*b, Param::Fader) &&
        cache_->Get(b->strip, Param::Fader, &v))
      ApplyToBinding(b, Param::Fader, v, /*force=*/true);
    if (EffectiveOn(*b, Param::Mute) && cache_->Get(b->strip, Param::Mute, &v))
      ApplyToBinding(b, Param::Mute, v, /*force=*/true);
  }
}

StatusView MirrorEngine::status() const {
  StatusView s;
  std::memset(&s, 0, sizeof(s));
  s.master_enabled = master_.load() ? 1 : 0;
  s.connection_state = static_cast<int>(conn_state_);
  std::snprintf(s.status_text, sizeof(s.status_text), "%s", status_text_.c_str());
  std::snprintf(s.peer_ip, sizeof(s.peer_ip), "%s", peer_.ip.c_str());
  s.peer_port = peer_.port;
  s.binding_count = static_cast<int>(store_->size());
  return s;
}

std::vector<DiscoveredConsole> MirrorEngine::TakeDiscoveries() {
  std::vector<DiscoveredConsole> out;
  out.swap(discoveries_);
  return out;
}

bool MirrorEngine::BuildBindingView(const std::string& guid, BindingView* out) {
  Binding* b = store_->Get(guid);
  if (!b) return false;
  std::memset(out, 0, sizeof(*out));
  std::snprintf(out->guid, sizeof(out->guid), "%s", b->guid.c_str());
  char lbl[24];
  StripLabel(b->strip, lbl, sizeof(lbl));
  std::snprintf(out->strip_label, sizeof(out->strip_label), "%s", lbl);
  out->strip_type = b->strip.type;
  out->strip_index = b->strip.index;
  out->enabled = b->enabled ? 1 : 0;
  out->mirror_mute = b->mirror_mute ? 1 : 0;
  out->mirror_fader = b->mirror_fader ? 1 : 0;

  MediaTrack* tr = store_->ResolveTrack(b);
  out->track_resolved = tr ? 1 : 0;
  if (tr && GetSetMediaTrackInfo_String) {
    char name[128] = {0};
    if (GetSetMediaTrackInfo_String(tr, "P_NAME", name, false))
      std::snprintf(out->track_name, sizeof(out->track_name), "%s", name);
  }
  return true;
}

}  // namespace x32
