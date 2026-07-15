#include "menus.h"

#include <cstring>

#include "actions.h"
#include "app.h"
#include "binding_store.h"
#include "reaper_api.h"
#include "ui_compat.h"

namespace x32 {

namespace {

App* g_menus_app = nullptr;

// Context string for the track control-panel (TCP/MCP) right-click menu.
// SWS-confirmed; unknown menustr values are logged once at runtime elsewhere.
constexpr const char* kTrackCtx = "Track control panel context";

void AddItem(HMENU sub, const char* text, const char* action_idstr,
             bool checked, bool enabled) {
  int cmd = ActionCommandId(action_idstr);
  UINT flags = MF_STRING;
  if (checked) flags |= MF_CHECKED;
  if (!enabled) flags |= MF_GRAYED;
  MenuAppend(sub, flags, static_cast<UINT_PTR>(cmd), text);
}

void OnCustomMenu(const char* menustr, HMENU menu, int flag) {
  if (!g_menus_app || !menu) return;
  if (!menustr || std::strcmp(menustr, kTrackCtx) != 0) return;
  if (flag != 0) return;  // 0 = build the menu

  App* app = g_menus_app;
  std::string guid = app->FirstSelectedGuid();
  Binding* b = guid.empty() ? nullptr : app->store().Get(guid);

  HMENU sub = CreatePopupMenu();
  AddItem(sub, "Bind / edit binding…", "_X32MIRROR_TRACK_BIND_EDIT", false,
          true);
  if (b) {
    MenuAppend(sub, MF_SEPARATOR, 0, nullptr);
    AddItem(sub, "Mirroring enabled", "_X32MIRROR_TRACK_TOGGLE_ENABLE",
            b->enabled, true);
    AddItem(sub, "Mirror mute", "_X32MIRROR_TRACK_TOGGLE_MUTE", b->mirror_mute,
            true);
    AddItem(sub, "Mirror fader", "_X32MIRROR_TRACK_TOGGLE_FADER",
            b->mirror_fader, true);
    MenuAppend(sub, MF_SEPARATOR, 0, nullptr);
    AddItem(sub, "Remove binding", "_X32MIRROR_TRACK_REMOVE", false, true);
  }

  // Attach the submenu under an "X32 Mirror" entry, with a leading separator.
  MenuAppend(menu, MF_SEPARATOR, 0, nullptr);
  MenuAppend(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), "X32 Mirror");
}

}  // namespace

void RegisterMenus(App* app) {
  g_menus_app = app;
  if (plugin_register)
    plugin_register("hookcustommenu", reinterpret_cast<void*>(&OnCustomMenu));
}

void UnregisterMenus() {
  if (plugin_register)
    plugin_register("-hookcustommenu", reinterpret_cast<void*>(&OnCustomMenu));
  g_menus_app = nullptr;
}

}  // namespace x32
