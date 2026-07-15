// X32 → REAPER Mirror
// GUID-keyed binding store with write-through to per-project ext-state
// (section "X32MIRROR") and a reverse index (StripId → GUIDs) for the mirror
// engine's fan-out. Main-thread only (all REAPER API calls are).
#ifndef X32MIRROR_BINDING_STORE_H_
#define X32MIRROR_BINDING_STORE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "binding_serde.h"
#include "reaper_api.h"
#include "strip_id.h"

namespace x32 {

// Project ext-state section for persisted bindings.
constexpr const char* kProjSection = "X32MIRROR";

struct Binding : BindingData {
  // Runtime-only cache of the resolved track (not persisted). Revalidated
  // with ValidatePtr2 before every use; nulled when the GUID no longer
  // resolves so the UI can show the binding as "missing".
  MediaTrack* track = nullptr;
  bool resolved = false;

  // Last value actually applied to REAPER, for the epsilon / change gate.
  // Sentinels chosen so the first apply always goes through.
  double applied_db = 1e9;
  int applied_mute = -1;
};

class BindingStore {
 public:
  // Point the store at a project and (re)load all bindings from its ext-state.
  void SetProject(ReaProject* proj);
  ReaProject* project() const { return proj_; }

  // Reload from the current project's ext-state, rebuilding indices.
  void Reload();

  // Access.
  Binding* Get(const std::string& guid);
  const std::unordered_map<std::string, Binding>& all() const { return map_; }
  size_t size() const { return map_.size(); }

  // GUIDs bound to a strip (reverse index); nullptr if none.
  const std::vector<std::string>* BindingsForStrip(const StripId& id) const;

  // Mutations (each writes through to project ext-state).
  void AddOrUpdate(const std::string& guid, StripId strip, bool enabled,
                   bool mirror_mute, bool mirror_fader);
  bool Remove(const std::string& guid);
  int ToggleEnabled(const std::string& guid);           // new value or -1
  bool SetFlag(const std::string& guid, Param p, bool value);

  // Rewrite the per-binding enabled flag on every binding (force-all on/off).
  void ForceAll(bool enabled);

  // Resolve (and cache) the MediaTrack* for a binding, revalidating any cached
  // pointer. Returns nullptr and marks the binding unresolved if the GUID does
  // not match any track in the current project. Rebuilds the GUID→track scan
  // cache lazily.
  MediaTrack* ResolveTrack(Binding* b);

  // Invalidate the GUID→track scan cache (call on project structure change).
  void InvalidateTrackCache();

 private:
  void WriteThrough(const Binding& b);
  void Erase(const std::string& guid);
  void RebuildReverseIndex();
  void EnsureTrackScan();

  ReaProject* proj_ = nullptr;
  std::unordered_map<std::string, Binding> map_;
  std::unordered_map<int, std::vector<std::string>> rev_;  // strip.key() → guids

  // GUID string → MediaTrack*, rebuilt by scanning the project's tracks.
  std::unordered_map<std::string, MediaTrack*> track_scan_;
  bool track_scan_valid_ = false;
};

// Compute the GUID string for a track (helper around GetTrackGUID/guidToString).
std::string TrackGuidString(MediaTrack* tr);

}  // namespace x32

#endif  // X32MIRROR_BINDING_STORE_H_
