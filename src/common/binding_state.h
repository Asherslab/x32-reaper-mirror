// X32 → REAPER Mirror
// POD view types shared between the extension (producer) and any UI consumer
// (dockable panel or the embedded-FX companion). Kept dependency-free so the
// VST2 companion can include it without pulling in the REAPER API layer.
#ifndef X32MIRROR_BINDING_STATE_H_
#define X32MIRROR_BINDING_STATE_H_

#include "strip_id.h"

namespace x32 {

// Snapshot of one binding, safe to copy across the C ABI. Strings are fixed
// buffers so the struct is trivially copyable and needs no ownership rules.
struct BindingView {
  char guid[64];        // track GUID string, "{....}"
  char strip_label[24]; // e.g. "CH 07"
  StripType strip_type;
  int strip_index;      // 1-based

  int enabled;          // per-binding master enable
  int mirror_mute;      // mirror the mute parameter
  int mirror_fader;     // mirror the fader parameter

  int track_resolved;   // 1 if the GUID currently resolves to a live track
  char track_name[128]; // best-effort track name for display ("" if missing)
};

// Global/connection view for UI status rows.
struct StatusView {
  int master_enabled;     // global master switch
  int connection_state;   // ConnState (see x32_connection.h) as int
  char status_text[128];  // human-readable status line
  char peer_ip[64];       // negotiated console IP ("" if none)
  int peer_port;
  int binding_count;
};

}  // namespace x32

#endif  // X32MIRROR_BINDING_STATE_H_
