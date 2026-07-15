#include "binding_store.h"

#include <cstring>

namespace x32 {

std::string TrackGuidString(MediaTrack* tr) {
  if (!tr || !GetTrackGUID || !guidToString) return "";
  GUID* g = GetTrackGUID(tr);
  if (!g) return "";
  char buf[64] = {0};
  guidToString(g, buf);
  return buf;
}

void BindingStore::SetProject(ReaProject* proj) {
  proj_ = proj;
  InvalidateTrackCache();
  Reload();
}

void BindingStore::Reload() {
  map_.clear();
  if (proj_ && EnumProjExtState) {
    char key[256];
    char val[512];
    int idx = 0;
    while (EnumProjExtState(proj_, kProjSection, idx, key, sizeof(key), val,
                            sizeof(val))) {
      ++idx;
      BindingData d;
      if (ParseBinding(key, val, &d)) {
        Binding b;
        static_cast<BindingData&>(b) = d;
        map_[d.guid] = b;
      }
      // Unknown / malformed entries are skipped (kept in ext-state untouched
      // so a newer plugin version could still read them).
    }
  }
  RebuildReverseIndex();
}

Binding* BindingStore::Get(const std::string& guid) {
  auto it = map_.find(guid);
  return it == map_.end() ? nullptr : &it->second;
}

const std::vector<std::string>* BindingStore::BindingsForStrip(
    const StripId& id) const {
  auto it = rev_.find(id.key());
  return it == rev_.end() ? nullptr : &it->second;
}

void BindingStore::AddOrUpdate(const std::string& guid, StripId strip,
                               bool enabled, bool mirror_mute,
                               bool mirror_fader) {
  Binding& b = map_[guid];
  b.guid = guid;
  b.strip = strip;
  b.enabled = enabled;
  b.mirror_mute = mirror_mute;
  b.mirror_fader = mirror_fader;
  WriteThrough(b);
  RebuildReverseIndex();
}

bool BindingStore::Remove(const std::string& guid) {
  auto it = map_.find(guid);
  if (it == map_.end()) return false;
  Erase(guid);
  map_.erase(it);
  RebuildReverseIndex();
  return true;
}

int BindingStore::ToggleEnabled(const std::string& guid) {
  Binding* b = Get(guid);
  if (!b) return -1;
  b->enabled = !b->enabled;
  WriteThrough(*b);
  return b->enabled ? 1 : 0;
}

bool BindingStore::SetFlag(const std::string& guid, Param p, bool value) {
  Binding* b = Get(guid);
  if (!b) return false;
  if (p == Param::Mute) b->mirror_mute = value;
  else b->mirror_fader = value;
  WriteThrough(*b);
  return true;
}

void BindingStore::ForceAll(bool enabled) {
  for (auto& kv : map_) {
    kv.second.enabled = enabled;
    WriteThrough(kv.second);
  }
}

void BindingStore::WriteThrough(const Binding& b) {
  if (!proj_ || !SetProjExtState) return;
  std::string val = SerializeBinding(b);
  SetProjExtState(proj_, kProjSection, b.guid.c_str(), val.c_str());
}

void BindingStore::Erase(const std::string& guid) {
  if (!proj_ || !SetProjExtState) return;
  // Empty value deletes the key.
  SetProjExtState(proj_, kProjSection, guid.c_str(), "");
}

void BindingStore::RebuildReverseIndex() {
  rev_.clear();
  for (auto& kv : map_) {
    rev_[kv.second.strip.key()].push_back(kv.first);
  }
}

void BindingStore::InvalidateTrackCache() {
  track_scan_.clear();
  track_scan_valid_ = false;
  for (auto& kv : map_) {
    kv.second.track = nullptr;
    kv.second.resolved = false;
  }
}

void BindingStore::EnsureTrackScan() {
  if (track_scan_valid_) return;
  track_scan_.clear();
  if (proj_ && CountTracks && GetTrack) {
    int n = CountTracks(proj_);
    for (int i = 0; i < n; ++i) {
      MediaTrack* tr = GetTrack(proj_, i);
      if (!tr) continue;
      std::string g = TrackGuidString(tr);
      if (!g.empty()) track_scan_[g] = tr;
    }
  }
  track_scan_valid_ = true;
}

MediaTrack* BindingStore::ResolveTrack(Binding* b) {
  if (!b) return nullptr;
  // Fast path: a cached pointer that still validates.
  if (b->track && ValidatePtr2 && proj_ &&
      ValidatePtr2(proj_, b->track, "MediaTrack*")) {
    b->resolved = true;
    return b->track;
  }
  // Slow path: (re)scan and look up by GUID.
  EnsureTrackScan();
  auto it = track_scan_.find(b->guid);
  if (it != track_scan_.end() && it->second) {
    b->track = it->second;
    b->resolved = true;
    return b->track;
  }
  b->track = nullptr;
  b->resolved = false;
  return nullptr;
}

}  // namespace x32
