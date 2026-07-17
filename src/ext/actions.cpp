#include "actions.h"

#include <cstring>

#include "app.h"
#include "binding_store.h"
#include "reaper_api.h"

namespace x32 {

namespace {

enum ActionId {
  A_MASTER_TOGGLE = 0,
  A_ENABLE_ALL,
  A_DISABLE_ALL,
  A_FORCE_ALL_ON,
  A_FORCE_ALL_OFF,
  A_SHOW_PANEL,
  A_TRACK_TOGGLE_ENABLE,
  A_TRACK_TOGGLE_MUTE,
  A_TRACK_TOGGLE_FADER,
  A_CONNECT_TOGGLE,
  A_INSERT_EMBED,
  A_TRACK_BIND_EDIT,
  A_TRACK_REMOVE,
  A_COUNT
};

struct ActionDef {
  const char* idstr;
  const char* name;
  bool toggles;  // has a meaningful toggle state
};

const ActionDef kDefs[A_COUNT] = {
    {"_X32MIRROR_MASTER_TOGGLE", "X32 Mirror: Toggle master mirror", true},
    {"_X32MIRROR_ENABLE_ALL", "X32 Mirror: Enable all (master on)", true},
    {"_X32MIRROR_DISABLE_ALL", "X32 Mirror: Disable all (master off)", true},
    {"_X32MIRROR_FORCE_ALL_ON", "X32 Mirror: Force all bindings enabled", false},
    {"_X32MIRROR_FORCE_ALL_OFF", "X32 Mirror: Force all bindings disabled", false},
    {"_X32MIRROR_SHOW_PANEL", "X32 Mirror: Show/hide panel", true},
    {"_X32MIRROR_TRACK_TOGGLE_ENABLE",
     "X32 Mirror: Toggle mirroring on selected track(s)", true},
    {"_X32MIRROR_TRACK_TOGGLE_MUTE",
     "X32 Mirror: Toggle mute mirroring on selected track(s)", true},
    {"_X32MIRROR_TRACK_TOGGLE_FADER",
     "X32 Mirror: Toggle fader mirroring on selected track(s)", true},
    {"_X32MIRROR_CONNECT_TOGGLE", "X32 Mirror: Connect/disconnect", true},
    {"_X32MIRROR_INSERT_EMBED_ON_BOUND",
     "X32 Mirror: Insert in-strip button on bound tracks", false},
    {"_X32MIRROR_TRACK_BIND_EDIT",
     "X32 Mirror: Bind/edit binding for selected track", false},
    {"_X32MIRROR_TRACK_REMOVE",
     "X32 Mirror: Remove binding for selected track", false},
};

int g_cmd[A_COUNT] = {0};
custom_action_register_t g_reg[A_COUNT];
App* g_actions_app = nullptr;

int IdFor(int cmd) {
  for (int i = 0; i < A_COUNT; ++i)
    if (g_cmd[i] && g_cmd[i] == cmd) return i;
  return -1;
}

// Toggle state for a track flag action, from the first selected track's
// binding. Returns 1/0, or -1 if there is no binding to reflect.
int TrackFlagState(int which) {
  if (!g_actions_app) return -1;
  std::string g = g_actions_app->FirstSelectedGuid();
  if (g.empty()) return -1;
  Binding* b = g_actions_app->store().Get(g);
  if (!b) return -1;
  switch (which) {
    case A_TRACK_TOGGLE_ENABLE: return b->enabled ? 1 : 0;
    case A_TRACK_TOGGLE_MUTE: return b->mirror_mute ? 1 : 0;
    case A_TRACK_TOGGLE_FADER: return b->mirror_fader ? 1 : 0;
  }
  return -1;
}

void RefreshToggles() {
  if (!RefreshToolbar) return;
  for (int i = 0; i < A_COUNT; ++i)
    if (kDefs[i].toggles && g_cmd[i]) RefreshToolbar(g_cmd[i]);
}

bool OnAction(KbdSectionInfo* /*sec*/, int command, int /*val*/, int /*val2*/,
              int /*relmode*/, HWND /*hwnd*/) {
  if (!g_actions_app) return false;
  int a = IdFor(command);
  if (a < 0) return false;
  App* app = g_actions_app;
  switch (a) {
    case A_MASTER_TOGGLE: app->SetMaster(!app->Master()); break;
    case A_ENABLE_ALL: app->SetMaster(true); break;
    case A_DISABLE_ALL: app->SetMaster(false); break;
    case A_FORCE_ALL_ON: app->ForceAll(true); break;
    case A_FORCE_ALL_OFF: app->ForceAll(false); break;
    case A_SHOW_PANEL: app->TogglePanel(); break;
    case A_TRACK_TOGGLE_ENABLE: app->ToggleTrackEnabled(); break;
    case A_TRACK_TOGGLE_MUTE: app->ToggleTrackFlag(Param::Mute); break;
    case A_TRACK_TOGGLE_FADER: app->ToggleTrackFlag(Param::Fader); break;
    case A_CONNECT_TOGGLE: app->ConnectToggle(); break;
    case A_INSERT_EMBED: app->InsertEmbedOnBound(); break;
    case A_TRACK_BIND_EDIT:
      app->OpenPanelForTrack(app->FirstSelectedGuid());
      break;
    case A_TRACK_REMOVE: {
      std::string g = app->FirstSelectedGuid();
      if (!g.empty() && app->store().Remove(g)) app->engine().BumpSerial();
      app->RefreshPanel();
      break;
    }
    default: return false;
  }
  RefreshToggles();
  return true;
}

int OnToggleAction(int command) {
  if (!g_actions_app) return -1;
  int a = IdFor(command);
  if (a < 0 || !kDefs[a].toggles) return -1;
  App* app = g_actions_app;
  switch (a) {
    case A_MASTER_TOGGLE: return app->Master() ? 1 : 0;
    case A_ENABLE_ALL: return app->Master() ? 1 : 0;
    case A_DISABLE_ALL: return app->Master() ? 0 : 1;
    case A_SHOW_PANEL: return app->PanelVisible() ? 1 : 0;
    case A_CONNECT_TOGGLE: return app->ConnectionActive() ? 1 : 0;
    case A_TRACK_TOGGLE_ENABLE:
    case A_TRACK_TOGGLE_MUTE:
    case A_TRACK_TOGGLE_FADER:
      return TrackFlagState(a);
    default: return -1;
  }
}

}  // namespace

int ActionCommandId(const char* idstr) {
  for (int i = 0; i < A_COUNT; ++i)
    if (std::strcmp(kDefs[i].idstr, idstr) == 0) return g_cmd[i];
  return 0;
}

void RegisterActions(App* app) {
  g_actions_app = app;
  if (!plugin_register) return;
  for (int i = 0; i < A_COUNT; ++i) {
    std::memset(&g_reg[i], 0, sizeof(g_reg[i]));
    g_reg[i].uniqueSectionId = 0;  // main section
    g_reg[i].idStr = kDefs[i].idstr;
    g_reg[i].name = kDefs[i].name;
    g_cmd[i] = plugin_register("custom_action", &g_reg[i]);
  }
  plugin_register("hookcommand2", reinterpret_cast<void*>(&OnAction));
  plugin_register("toggleaction", reinterpret_cast<void*>(&OnToggleAction));
}

void UnregisterActions() {
  if (!plugin_register) return;
  plugin_register("-hookcommand2", reinterpret_cast<void*>(&OnAction));
  plugin_register("-toggleaction", reinterpret_cast<void*>(&OnToggleAction));
  for (int i = 0; i < A_COUNT; ++i)
    plugin_register("-custom_action", &g_reg[i]);
  g_actions_app = nullptr;
}

}  // namespace x32
