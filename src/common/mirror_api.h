// X32 → REAPER Mirror
// Versioned C ABI exposed by the extension and consumed by the embedded-FX
// companion (x32mirror_embed). The companion resolves a pointer to
// X32Mirror_Interface via the REAPER extension API registry and calls through
// it. Because the two binaries are built and shipped separately, this vtable
// is strictly append-only and version-tagged: never reorder or repurpose an
// existing slot — bump kX32MirrorApiVersion and append instead.
//
// All functions are main-thread only (the companion's paint/mouse callbacks
// run on REAPER's UI thread, which is the main thread).
#ifndef X32MIRROR_MIRROR_API_H_
#define X32MIRROR_MIRROR_API_H_

#include "binding_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bump when the layout below changes in any way. The companion refuses to use
// a vtable whose version it does not recognise (see extension_link.cpp).
#define X32MIRROR_API_VERSION 1

// Name used with plugin_register("API_X32Mirror_GetInterface", fn) on the
// extension side, and resolved by the companion via audioMaster.
#define X32MIRROR_API_REG_NAME "API_X32Mirror_GetInterface"

// Fallback channel (see PLAN.md "Flagged for verification" #2): if the
// audioMaster API lookup does not resolve, the extension also publishes the
// interface pointer as a hex string under this ext-state key, and the
// companion reads it via GetExtState.
#define X32MIRROR_EXTSTATE_SECTION "x32mirror"
#define X32MIRROR_EXTSTATE_IFACE_KEY "iface_ptr"

typedef struct X32Mirror_Interface {
  int version;      // == X32MIRROR_API_VERSION at construction time
  int struct_size;  // sizeof(X32Mirror_Interface) for extra safety

  // Monotonic counter bumped whenever any binding or connection state
  // changes. The companion caches its view and only re-queries when this
  // moves, so paint stays cheap.
  unsigned int (*GetStateSerial)(void);

  // Fill *out with the binding for the given track GUID. Returns 1 if a
  // binding exists (out populated, including track_resolved), 0 otherwise.
  int (*GetBindingViewByGuid)(const char* guid, x32::BindingView* out);

  // Toggle a binding's per-binding enable flag. Creates nothing. Returns the
  // new enabled value, or -1 if no binding exists for the GUID.
  int (*ToggleBindingEnabled)(const char* guid);

  // Set one mirror flag (Param::Mute or Param::Fader) on the binding for the
  // GUID. value is 0/1. Returns 1 on success, 0 if no binding exists.
  int (*SetMirrorFlag)(const char* guid, int param /*x32::Param*/, int value);

  // Remove the binding for the GUID entirely. Returns 1 if one was removed.
  int (*RemoveBinding)(const char* guid);

  // Open the dockable panel and preselect the binding for the GUID (creates
  // and shows the dock if needed).
  void (*OpenPanelForTrack)(const char* guid);

  // Last-known console value for a strip parameter. For the fader this is the
  // raw 0..1 float; for mute it is 1.0 (unmuted) / 0.0 (muted) in *X32*
  // convention. Returns 1 if a value is known, 0 otherwise.
  int (*GetStripValue)(int strip_type, int strip_index, int param, double* out);

  // Global master switch state (1 = on). Convenience for the companion so it
  // can dim the button when the master is off.
  int (*GetMasterEnabled)(void);

  // Connection up (LIVE) = 1, else 0. Drives the "connection down" red
  // outline on the in-strip button.
  int (*IsConnectionLive)(void);

} X32Mirror_Interface;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // X32MIRROR_MIRROR_API_H_
