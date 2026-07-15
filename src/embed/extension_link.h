// X32 → REAPER Mirror — embedded-FX companion
// Resolves the extension's exported vtable and this FX's host track, entirely
// through the VST audioMaster callback (no linkage to the extension). Both the
// selector values below are flagged in PLAN.md for confirmation against the
// vendored REAPER headers; fallbacks are implemented where the plan calls for
// them.
#ifndef X32MIRROR_EXTENSION_LINK_H_
#define X32MIRROR_EXTENSION_LINK_H_

#include "mirror_api.h"
#include "vst2_min.h"

namespace x32embed {

class ExtensionLink {
 public:
  // Resolve (and cache) the extension vtable. Re-checked lazily so that a
  // reloaded/late-loaded extension is picked up. Returns nullptr if the
  // extension is absent or its API version is incompatible.
  X32Mirror_Interface* Interface(AEffect* effect, audioMasterCallback am);

  // The MediaTrack* hosting this FX, as an opaque pointer.
  void* HostTrack(AEffect* effect, audioMasterCallback am);

  // GUID string for the host track, or "" if it cannot be resolved.
  const char* HostGuid(AEffect* effect, audioMasterCallback am);

  bool version_ok() const { return version_ok_; }

 private:
  void ResolveFuncs(AEffect* effect, audioMasterCallback am);

  X32Mirror_Interface* iface_ = nullptr;
  bool version_ok_ = false;
  bool funcs_resolved_ = false;

  // Function pointers resolved from the host (subset we need).
  void* (*get_track_guid_)(void* tr) = nullptr;      // GetTrackGUID
  void (*guid_to_string_)(const void* g, char* out) = nullptr;  // guidToString
  const char* (*get_ext_state_)(const char* sec, const char* key) = nullptr;

  char guid_buf_[64] = {0};
};

}  // namespace x32embed

#endif  // X32MIRROR_EXTENSION_LINK_H_
