// X32 → REAPER Mirror
// Small shims over the handful of Win32/SWELL API differences the panel and
// menu code touches, so the UI sources read the same on all platforms.
#ifndef X32MIRROR_UI_COMPAT_H_
#define X32MIRROR_UI_COMPAT_H_

#include "reaper_api.h"  // pulls in <windows.h> or swell.h

namespace x32 {

// Append a menu item. SWELL has no AppendMenu; InsertMenu at the current item
// count with MF_BYPOSITION appends on both Win32 and SWELL.
inline void MenuAppend(HMENU m, UINT flags, UINT_PTR id, const char* text) {
  InsertMenu(m, static_cast<UINT>(GetMenuItemCount(m)), flags | MF_BYPOSITION,
             id, text);
}

// Store / fetch a per-window pointer. Win32 dialogs use the *Ptr variants and
// GWLP_USERDATA; SWELL exposes GetWindowLong/SetWindowLong taking LONG_PTR and
// GWL_USERDATA (no truncation on 64-bit).
inline void SetWindowUserPtr(HWND h, void* p) {
#ifdef _WIN32
  SetWindowLongPtr(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
#else
  SetWindowLong(h, GWL_USERDATA, reinterpret_cast<LONG_PTR>(p));
#endif
}
inline void* GetWindowUserPtr(HWND h) {
#ifdef _WIN32
  return reinterpret_cast<void*>(GetWindowLongPtr(h, GWLP_USERDATA));
#else
  return reinterpret_cast<void*>(GetWindowLong(h, GWL_USERDATA));
#endif
}

}  // namespace x32

#endif  // X32MIRROR_UI_COMPAT_H_
