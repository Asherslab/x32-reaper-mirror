// X32 → REAPER Mirror
// REAPER API import layer. We use the minimal-import scheme: declare exactly
// the functions we call via REAPERAPI_WANT_* and REAPERAPI_MINIMAL, so the
// loader only resolves those. Every other translation unit that needs a REAPER
// API includes this header and gets extern function pointers; reaper_api.cpp
// is the single TU that defines them (REAPERAPI_IMPLEMENT).
#ifndef X32MIRROR_REAPER_API_H_
#define X32MIRROR_REAPER_API_H_

#define REAPERAPI_MINIMAL

// --- The functions this plugin calls ---------------------------------------
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_GetResourcePath
#define REAPERAPI_WANT_GetMainHwnd

// Track control (control-surface behavior; no gang, no undo).
#define REAPERAPI_WANT_CSurf_OnVolumeChangeEx
#define REAPERAPI_WANT_CSurf_OnMuteChangeEx

// Track identity / validation.
#define REAPERAPI_WANT_GetTrackGUID
#define REAPERAPI_WANT_guidToString
#define REAPERAPI_WANT_stringToGuid
#define REAPERAPI_WANT_ValidatePtr2
#define REAPERAPI_WANT_GetSetMediaTrackInfo
#define REAPERAPI_WANT_GetSetMediaTrackInfo_String

// Selection / iteration.
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_CountSelectedTracks
#define REAPERAPI_WANT_GetSelectedTrack
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_GetTrack

// Persistence (per-project bindings).
#define REAPERAPI_WANT_SetProjExtState
#define REAPERAPI_WANT_GetProjExtState
#define REAPERAPI_WANT_EnumProjExtState

// Global ext-state fallback channel for the companion.
#define REAPERAPI_WANT_SetExtState
#define REAPERAPI_WANT_GetExtState

// Docking.
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowActivate

// Toggle action state / refresh.
#define REAPERAPI_WANT_RefreshToolbar

// Embedded-FX insertion helper.
#define REAPERAPI_WANT_TrackFX_AddByName
#define REAPERAPI_WANT_TrackFX_SetNamedConfigParm

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

// SWELL's swell-types.h (pulled in by reaper_plugin.h on macOS/Linux) and the
// Win32 <windows.h> both define min()/max() function-like macros that collide
// with libstdc++'s <limits>/<chrono>. We never use those macros, so undefine
// them here to keep this header safe to include before any STL header.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace x32 {
// Resolve all wanted API pointers from the host. Returns true on success
// (REAPERAPI_LoadAPI returned 0). Call once from the plugin entry point.
bool LoadReaperApi(void* (*getFunc)(const char*));
}  // namespace x32

#endif  // X32MIRROR_REAPER_API_H_
