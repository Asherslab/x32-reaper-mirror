#include "menus.h"

#include <cstring>

#include "actions.h"
#include "app.h"
#include "binding_store.h"
#include "log.h"
#include "reaper_api.h"
#include "ui_compat.h"

namespace x32 {

namespace {

App* g_menus_app = nullptr;

// Context string for the track control-panel (TCP/MCP) right-click menu.
constexpr const char* kTrackCtx = "Track control panel context";

void LogUnknownMenustrOnce(const char* menustr) {
  static bool logged = false;
  if (logged || !menustr) return;
  logged = true;
  // Logged at Info (not Debug): this exists specifically to verify the
  // PLAN.md-flagged assumption that REAPER's context string is exactly
  // kTrackCtx, so it should be visible at the default log_level=2 rather
  // than requiring the ini to be bumped to Debug first.
  X32LOGI("hookcustommenu: unhandled menustr \"%s\"", menustr);
}

void AddItem(HMENU sub, const char* text, const char* action_idstr,
             bool checked, bool enabled) {
  int cmd = ActionCommandId(action_idstr);
  UINT flags = MF_STRING;
  if (checked) flags |= MF_CHECKED;
  if (!enabled) flags |= MF_GRAYED;
  MenuAppend(sub, flags, static_cast<UINT_PTR>(cmd), text);
}

// Tracks the "X32 Mirror" entry (+ its leading separator) appended to the
// last menu we touched, so a flag==1 refresh pass can drop and rebuild it
// without needing GetMenuString/RemoveMenu (neither exists in SWELL).
HMENU g_last_menu = nullptr;
int g_last_base_count = -1;  // item count of g_last_menu before we appended

void OnCustomMenu(const char* menustr, HMENU menu, int flag) {
  if (!g_menus_app || !menu) return;
  if (!menustr || std::strcmp(menustr, kTrackCtx) != 0) {
    LogUnknownMenustrOnce(menustr);
    return;
  }
  // flag 0 = build the menu; flag 1 = about to show a cached menu (refresh
  // checkmarks). Handled identically: any previously appended "X32 Mirror"
  // entry (and its separator) is dropped and rebuilt from current state.
  if (flag != 0 && flag != 1) return;

  int base_count = static_cast<int>(GetMenuItemCount(menu));
  if (g_last_menu == menu && g_last_base_count >= 0 &&
      g_last_base_count < base_count) {
    // Confirm item[base_count] is actually the separator we appended before
    // trusting the heuristic — guards against a freed HMENU being reused for
    // an unrelated, larger menu and us deleting its real trailing items.
    MENUITEMINFO mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_TYPE;
    bool is_our_separator =
        GetMenuItemInfo(menu, g_last_base_count, TRUE, &mii) &&
        (mii.fType & MFT_SEPARATOR) != 0;
    if (is_our_separator) {
      // Remove the previously appended trailing items (separator + popup).
      while (base_count > g_last_base_count) {
        DeleteMenu(menu, base_count - 1, MF_BYPOSITION);
        --base_count;
      }
    } else {
      g_last_menu = nullptr;
      g_last_base_count = -1;
    }
  }

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

  g_last_menu = menu;
  g_last_base_count = base_count;
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
  g_last_menu = nullptr;
  g_last_base_count = -1;
}

}  // namespace x32
