#include "embed_ui.h"

#include <cstring>

#include "binding_state.h"

namespace x32embed {

namespace {

using x32::BindingView;

// --- compact 3x5 pixel font ------------------------------------------------
// Each glyph is 5 rows; the low 3 bits of each row are the columns
// (bit2 = left). Covers the characters used by strip labels (CH/BUS/DCA + the
// digits) plus 'X' for the inert placeholder.
struct Glyph {
  char c;
  unsigned char rows[5];
};
// Full uppercase alphabet + digits + a few punctuation marks, so strip
// labels, "BIND", "(missing)", track names, and "X32 (no ext)" all render
// instead of falling back to blank/garbled glyphs.
const Glyph kFont[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}},
    {'3', {7, 1, 7, 1, 7}}, {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}},
    {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}}, {'8', {7, 5, 7, 5, 7}},
    {'9', {7, 5, 7, 1, 7}},
    {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}}, {'C', {3, 4, 4, 4, 3}},
    {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}},
    {'J', {1, 1, 1, 5, 2}}, {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 5, 5, 5}}, {'N', {5, 7, 7, 5, 5}}, {'O', {2, 5, 5, 5, 2}},
    {'P', {6, 5, 6, 4, 4}}, {'Q', {2, 5, 5, 7, 1}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {7, 4, 7, 1, 7}}, {'T', {7, 2, 2, 2, 2}}, {'U', {5, 5, 5, 5, 7}},
    {'V', {5, 5, 5, 5, 2}}, {'W', {5, 5, 5, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'(', {2, 4, 4, 4, 2}}, {')', {2, 1, 1, 1, 2}}, {'-', {0, 0, 7, 0, 0}},
    {'.', {0, 0, 0, 0, 2}}, {'_', {0, 0, 0, 0, 7}}, {':', {0, 2, 0, 2, 0}},
    {'/', {1, 2, 2, 4, 4}},
    {' ', {0, 0, 0, 0, 0}},
};

const Glyph* FindGlyph(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (const auto& g : kFont)
    if (g.c == c) return &g;
  return nullptr;
}

inline unsigned int Rgba(int r, int g, int b) {
  return REAPER_FXEMBED_RGBA(r, g, b, 255);
}

struct Canvas {
  unsigned int* bits;
  int span;   // in uint32 units
  int w, h;
  bool flipped;

  void Put(int x, int y, unsigned int color) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int row = flipped ? (h - 1 - y) : y;
    bits[row * span + x] = color;
  }
  void FillRect(int x0, int y0, int x1, int y1, unsigned int color) {
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) Put(x, y, color);
  }
  void Outline(int x0, int y0, int x1, int y1, unsigned int color) {
    for (int x = x0; x < x1; ++x) { Put(x, y0, color); Put(x, y1 - 1, color); }
    for (int y = y0; y < y1; ++y) { Put(x0, y, color); Put(x1 - 1, y, color); }
  }
  // Draw a scaled glyph; returns advance in pixels.
  int DrawChar(char c, int x, int y, int scale, unsigned int color) {
    const Glyph* g = FindGlyph(c);
    if (!g) g = FindGlyph(' ');
    for (int r = 0; r < 5; ++r)
      for (int col = 0; col < 3; ++col)
        if ((g->rows[r] >> (2 - col)) & 1)
          FillRect(x + col * scale, y + r * scale, x + (col + 1) * scale,
                   y + (r + 1) * scale, color);
    return 3 * scale + scale;  // glyph width + 1px (scaled) gap
  }
  int DrawText(const char* s, int x, int y, int scale, unsigned int color) {
    for (; *s; ++s) x += DrawChar(*s, x, y, scale, color);
    return x;
  }
};

Canvas MakeCanvas(REAPER_FXEMBED_IBitmap* bmp, REAPER_FXEMBED_DrawInfo* info) {
  Canvas c;
  c.bits = bmp->getBits();
  c.span = bmp->getRowSpan();
  c.w = (info && info->width > 0) ? info->width : bmp->getWidth();
  c.h = (info && info->height > 0) ? info->height : bmp->getHeight();
  if (c.w > bmp->getWidth()) c.w = bmp->getWidth();
  if (c.h > bmp->getHeight()) c.h = bmp->getHeight();
  c.flipped = bmp->isFlipped();
  return c;
}

}  // namespace

void PaintButton(REAPER_FXEMBED_IBitmap* bmp, REAPER_FXEMBED_DrawInfo* info,
                 X32Mirror_Interface* iface, const char* guid) {
  if (!bmp || !bmp->getBits()) return;
  Canvas c = MakeCanvas(bmp, info);
  if (c.w <= 0 || c.h <= 0) return;

  // No extension: inert dark placeholder.
  if (!iface) {
    c.FillRect(0, 0, c.w, c.h, Rgba(40, 40, 44));
    c.Outline(0, 0, c.w, c.h, Rgba(90, 90, 96));
    c.DrawText("X32 (no ext)", 4, (c.h - 5) / 2, 1, Rgba(150, 150, 160));
    return;
  }

  BindingView v;
  bool have = guid && guid[0] &&
              iface->GetBindingViewByGuid(guid, &v) != 0;

  bool master = iface->GetMasterEnabled() != 0;
  bool live = iface->IsConnectionLive() != 0;
  bool active = have && v.enabled && master;

  // Background reflects effective enable.
  unsigned int bg = active ? Rgba(28, 96, 52)                 // green: on
                           : (have ? Rgba(70, 70, 74)         // gray: bound/off
                                   : Rgba(48, 48, 52));       // unbound
  c.FillRect(0, 0, c.w, c.h, bg);

  // Connection-down: red outline.
  c.Outline(0, 0, c.w, c.h,
            live ? Rgba(20, 20, 22) : Rgba(200, 40, 40));

  if (!have) {
    c.DrawText("BIND", 4, (c.h - 5 * 2) / 2, 2, Rgba(180, 180, 190));
    return;
  }

  // Strip label, e.g. "CH 07".
  unsigned int fg = active ? Rgba(235, 245, 238) : Rgba(180, 180, 186);
  c.DrawText(v.strip_label, 4, 3, 2, fg);

  // M / F indicator dots (top-right); lit when that param is mirrored.
  int dot = 5;
  int mx = c.w - 2 * dot - 6;
  int my = 3;
  c.FillRect(mx, my, mx + dot, my + dot,
             v.mirror_mute ? Rgba(230, 170, 40) : Rgba(80, 80, 84));
  c.FillRect(mx + dot + 2, my, mx + 2 * dot + 2, my + dot,
             v.mirror_fader ? Rgba(70, 150, 230) : Rgba(80, 80, 84));

  // Track name row (best-effort, small) beneath the strip label.
  if (v.track_name[0])
    c.DrawText(v.track_resolved ? v.track_name : "(missing)", 4, 16, 1, fg);
}

INT_PTR OnMouse(int msg, REAPER_FXEMBED_DrawInfo* info,
                X32Mirror_Interface* iface, const char* guid) {
  if (!iface || !guid || !guid[0]) return 0;
  switch (msg) {
    case REAPER_FXEMBED_WM_LBUTTONUP: {
      // fx-embed auto-captures on mouse-down, so a press-and-drag-off before
      // release must not still toggle the binding on a live mix.
      if (info && (info->mouse_x < 0 || info->mouse_y < 0 ||
                   info->mouse_x >= info->width || info->mouse_y >= info->height))
        return 0;
      iface->ToggleBindingEnabled(guid);
      return REAPER_FXEMBED_RETNOTIFY_INVALIDATE;
    }
    case REAPER_FXEMBED_WM_RBUTTONUP:
      // Open the panel preselected to this track, where mute/fader/remove
      // controls live. (A native in-bitmap popup is not portable without
      // linking a UI toolkit into the companion; the panel is the full editor.)
      iface->OpenPanelForTrack(guid);
      return REAPER_FXEMBED_RETNOTIFY_INVALIDATE;
    default:
      return 0;
  }
}

int FillSizeHints(REAPER_FXEMBED_SizeHints* sh) {
  if (!sh) return 0;
  sh->preferred_aspect = 65536 * 3;  // ~3:1 wide button
  sh->minimum_aspect = 65536 * 2;
  return 1;
}

}  // namespace x32embed
