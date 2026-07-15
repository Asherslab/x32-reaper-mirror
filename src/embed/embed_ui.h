// X32 → REAPER Mirror — embedded-FX companion
// In-strip button rendering and interaction. Draws directly into the
// REAPER_FXEMBED_IBitmap (ARGB pixels) — no LICE/text-API dependency — using a
// compact built-in pixel font for the strip label.
#ifndef X32MIRROR_EMBED_UI_H_
#define X32MIRROR_EMBED_UI_H_

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>
typedef intptr_t INT_PTR;
#endif

#include "mirror_api.h"
#include "reaper_plugin_fx_embed.h"

namespace x32embed {

// Paint the button. iface may be null (extension absent / wrong version) — an
// inert "X32 (no ext)" placeholder is drawn in that case.
void PaintButton(REAPER_FXEMBED_IBitmap* bmp, REAPER_FXEMBED_DrawInfo* info,
                 X32Mirror_Interface* iface, const char* guid);

// Handle a mouse message. Returns REAPER_FXEMBED_RETNOTIFY_* flags.
INT_PTR OnMouse(int msg, REAPER_FXEMBED_DrawInfo* info,
                X32Mirror_Interface* iface, const char* guid);

// Fill size hints (preferred aspect). Returns 1.
int FillSizeHints(REAPER_FXEMBED_SizeHints* sh);

}  // namespace x32embed

#endif  // X32MIRROR_EMBED_UI_H_
