#include "api_export.h"

#include <cstdio>

#include "app.h"
#include "binding_store.h"
#include "mirror_api.h"
#include "reaper_api.h"

namespace x32 {

namespace {

App* g_api_app = nullptr;
X32Mirror_Interface g_iface;

unsigned int Api_GetStateSerial() {
  return g_api_app ? g_api_app->engine().state_serial() : 0;
}

int Api_GetBindingViewByGuid(const char* guid, BindingView* out) {
  if (!g_api_app || !guid || !out) return 0;
  return g_api_app->engine().BuildBindingView(guid, out) ? 1 : 0;
}

int Api_ToggleBindingEnabled(const char* guid) {
  if (!g_api_app || !guid) return -1;
  int v = g_api_app->store().ToggleEnabled(guid);
  if (v >= 0) {
    g_api_app->engine().SeedBinding(guid);
    g_api_app->engine().BumpSerial();
    g_api_app->RefreshPanel();
  }
  return v;
}

int Api_SetMirrorFlag(const char* guid, int param, int value) {
  if (!g_api_app || !guid) return 0;
  Param p = param == static_cast<int>(Param::Mute) ? Param::Mute : Param::Fader;
  bool ok = g_api_app->store().SetFlag(guid, p, value != 0);
  if (ok) {
    g_api_app->engine().SeedBinding(guid);
    g_api_app->engine().BumpSerial();
    g_api_app->RefreshPanel();
  }
  return ok ? 1 : 0;
}

int Api_RemoveBinding(const char* guid) {
  if (!g_api_app || !guid) return 0;
  bool ok = g_api_app->store().Remove(guid);
  if (ok) {
    g_api_app->engine().BumpSerial();
    g_api_app->RefreshPanel();
  }
  return ok ? 1 : 0;
}

void Api_OpenPanelForTrack(const char* guid) {
  if (g_api_app) g_api_app->OpenPanelForTrack(guid ? guid : "");
}

int Api_GetStripValue(int strip_type, int strip_index, int param, double* out) {
  if (!g_api_app || !out) return 0;
  StripId id{static_cast<StripType>(strip_type), strip_index};
  Param p = param == static_cast<int>(Param::Mute) ? Param::Mute : Param::Fader;
  return g_api_app->cache().Get(id, p, out) ? 1 : 0;
}

int Api_GetMasterEnabled() {
  return g_api_app && g_api_app->Master() ? 1 : 0;
}

int Api_IsConnectionLive() {
  return g_api_app && g_api_app->engine().conn_state() == ConnState::Live ? 1
                                                                          : 0;
}

// The function REAPER hands back to callers of GetFunc("X32Mirror_GetInterface").
X32Mirror_Interface* GetInterface() { return &g_iface; }

}  // namespace

void RegisterApi(App* app) {
  g_api_app = app;

  g_iface.version = X32MIRROR_API_VERSION;
  g_iface.struct_size = static_cast<int>(sizeof(X32Mirror_Interface));
  g_iface.GetStateSerial = &Api_GetStateSerial;
  g_iface.GetBindingViewByGuid = &Api_GetBindingViewByGuid;
  g_iface.ToggleBindingEnabled = &Api_ToggleBindingEnabled;
  g_iface.SetMirrorFlag = &Api_SetMirrorFlag;
  g_iface.RemoveBinding = &Api_RemoveBinding;
  g_iface.OpenPanelForTrack = &Api_OpenPanelForTrack;
  g_iface.GetStripValue = &Api_GetStripValue;
  g_iface.GetMasterEnabled = &Api_GetMasterEnabled;
  g_iface.IsConnectionLive = &Api_IsConnectionLive;

  if (plugin_register) {
    // Consumers resolve this via GetFunc("X32Mirror_GetInterface").
    plugin_register(X32MIRROR_API_REG_NAME,
                    reinterpret_cast<void*>(&GetInterface));
  }

  // Fallback channel: publish the interface pointer as a hex string so the
  // companion can find it via GetExtState if the audioMaster API lookup path
  // is unavailable in its host build.
  if (SetExtState) {
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%p", static_cast<void*>(&g_iface));
    SetExtState(X32MIRROR_EXTSTATE_SECTION, X32MIRROR_EXTSTATE_IFACE_KEY, hex,
                false);
  }
}

void UnregisterApi() {
  if (plugin_register)
    plugin_register("-" X32MIRROR_API_REG_NAME,
                    reinterpret_cast<void*>(&GetInterface));
  if (SetExtState)
    SetExtState(X32MIRROR_EXTSTATE_SECTION, X32MIRROR_EXTSTATE_IFACE_KEY, "",
                false);
  g_api_app = nullptr;
}

}  // namespace x32
