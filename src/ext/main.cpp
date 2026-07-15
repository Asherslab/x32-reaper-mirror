// X32 → REAPER Mirror — extension entry point and App implementation.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "actions.h"
#include "api_export.h"
#include "app.h"
#include "log.h"
#include "menus.h"
#include "panel.h"
#include "reaper_api.h"

namespace x32 {

App* g_app = nullptr;

namespace {
ConnConfig DefaultConnConfig() {
  ConnConfig c;  // plan defaults; repoll applied from settings in Init
  return c;
}
void TimerCallback() {
  if (g_app) g_app->Timer();
}
}  // namespace

App::App()
    : conn_(&cache_, &queue_, DefaultConnConfig()),
      engine_(&store_, &queue_, &cache_, &conn_) {}

App::~App() { Shutdown(); }

bool App::Init(reaper_plugin_info_t* rec, REAPER_PLUGIN_HINSTANCE hinst) {
  rec_ = rec;
  if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION) return false;
  if (!LoadReaperApi(rec->GetFunc)) return false;

  hinstance_ = hinst;
  main_hwnd_ = GetMainHwnd ? GetMainHwnd() : nullptr;

  settings_.Load();
  SetLogLevel(static_cast<LogLevel>(settings_.log_level));
  X32LOGI("loading; ini=%s", Settings::IniPath().c_str());

  // Bind the store to the active project.
  store_.SetProject(EnumProjects ? EnumProjects(-1, nullptr, 0) : nullptr);
  engine_.SetMaster(settings_.master_enabled);

  // Apply behavior settings and start the socket thread.
  conn_.set_repoll_interval_ms(settings_.repoll_interval_ms);
  conn_.Start();

  RegisterActions(this);
  RegisterMenus(this);
  RegisterApi(this);
  if (plugin_register)
    plugin_register("timer", reinterpret_cast<void*>(&TimerCallback));

  if (settings_.autoconnect) ConnectFromSettings();
  X32LOGI("loaded (%d bindings)", static_cast<int>(store_.size()));
  return true;
}

void App::Shutdown() {
  static bool done = false;
  if (done) return;
  done = true;

  if (plugin_register)
    plugin_register("-timer", reinterpret_cast<void*>(&TimerCallback));
  UnregisterApi();
  UnregisterMenus();
  UnregisterActions();

  conn_.Stop();

  if (panel_) {
    panel_->Destroy();
    delete panel_;
    panel_ = nullptr;
  }

  settings_.Save();
  X32LOGI("unloaded");
}

void App::Timer() {
  engine_.Tick();
  if (panel_) panel_->Tick();
}

// --- Connection -------------------------------------------------------------

void App::ConnectFromSettings() {
  conn_.set_repoll_interval_ms(settings_.repoll_interval_ms);
  conn_.Connect(settings_.ip, static_cast<uint16_t>(settings_.port));
}
void App::Disconnect() { conn_.Disconnect(); }
void App::ConnectToggle() {
  if (ConnectionActive())
    Disconnect();
  else
    ConnectFromSettings();
}
bool App::ConnectionActive() const {
  return conn_.state() != ConnState::Disconnected;
}
void App::StartDiscovery() {
  conn_.Discover(static_cast<uint16_t>(settings_.port));
}

// --- Master / bulk ----------------------------------------------------------

void App::SetMaster(bool on) {
  engine_.SetMaster(on);
  settings_.master_enabled = on;
  RefreshPanel();
}
void App::ForceAll(bool enabled) {
  store_.ForceAll(enabled);
  engine_.SeedAll();
  engine_.BumpSerial();
  RefreshPanel();
}

// --- Panel ------------------------------------------------------------------

void App::ShowPanel() {
  if (!panel_) panel_ = new Panel(this);
  panel_->Create();
}
void App::TogglePanel() {
  if (panel_ && panel_->Visible())
    panel_->Destroy();
  else
    ShowPanel();
}
bool App::PanelVisible() const { return panel_ && panel_->Visible(); }
void App::OpenPanelForTrack(const std::string& guid) {
  ShowPanel();
  if (panel_ && !guid.empty()) panel_->SelectGuid(guid);
}
void App::RefreshPanel() {
  if (panel_) panel_->MarkDirty();
}

// --- Track-oriented ---------------------------------------------------------

int App::SelectedTrackCount() const {
  return CountSelectedTracks ? CountSelectedTracks(nullptr) : 0;
}
MediaTrack* App::NthSelectedTrack(int n) const {
  return GetSelectedTrack ? GetSelectedTrack(nullptr, n) : nullptr;
}
std::string App::FirstSelectedGuid() const {
  MediaTrack* tr = NthSelectedTrack(0);
  return tr ? TrackGuidString(tr) : std::string();
}

void App::BindSelectedTracks(StripType type, int start_index) {
  int n = SelectedTrackCount();
  int idx = start_index;
  int count = StripCount(type);
  for (int i = 0; i < n; ++i) {
    MediaTrack* tr = NthSelectedTrack(i);
    if (!tr) continue;
    std::string guid = TrackGuidString(tr);
    if (guid.empty()) continue;
    if (idx > count) break;  // ran out of strips in this family
    StripId sid{type, idx};
    // Preserve existing flags if the binding already exists; else default all.
    Binding* existing = store_.Get(guid);
    bool mm = existing ? existing->mirror_mute : true;
    bool mf = existing ? existing->mirror_fader : true;
    store_.AddOrUpdate(guid, sid, /*enabled=*/true, mm, mf);
    engine_.SeedBinding(guid);
    ++idx;
  }
  engine_.BumpSerial();
  RefreshPanel();
}

void App::ToggleTrackEnabled() {
  int n = SelectedTrackCount();
  for (int i = 0; i < n; ++i) {
    MediaTrack* tr = NthSelectedTrack(i);
    if (!tr) continue;
    std::string guid = TrackGuidString(tr);
    if (guid.empty()) continue;
    if (store_.ToggleEnabled(guid) >= 0) engine_.SeedBinding(guid);
  }
  engine_.BumpSerial();
  RefreshPanel();
}

void App::ToggleTrackFlag(Param p) {
  int n = SelectedTrackCount();
  for (int i = 0; i < n; ++i) {
    MediaTrack* tr = NthSelectedTrack(i);
    if (!tr) continue;
    std::string guid = TrackGuidString(tr);
    Binding* b = guid.empty() ? nullptr : store_.Get(guid);
    if (!b) continue;
    bool cur = p == Param::Mute ? b->mirror_mute : b->mirror_fader;
    store_.SetFlag(guid, p, !cur);
    engine_.SeedBinding(guid);
  }
  engine_.BumpSerial();
  RefreshPanel();
}

void App::InsertEmbedOnBound() {
  if (!TrackFX_AddByName) return;
  int inserted = 0;
  for (auto& kv : store_.all()) {
    Binding* b = store_.Get(kv.first);
    MediaTrack* tr = b ? store_.ResolveTrack(b) : nullptr;
    if (!tr) continue;
    // recFX=false, instantiate: <0 = query, 1 = always add. Use 1.
    int fx = TrackFX_AddByName(tr, "x32mirror_embed", false, 1);
    if (fx >= 0) {
      ++inserted;
      // Best-effort: ask REAPER to show the embedded UI in the MCP strip.
      if (TrackFX_SetNamedConfigParm)
        TrackFX_SetNamedConfigParm(tr, fx, "fx_embed", "1");
    }
  }
  X32LOGI("insert embed: added to %d bound track(s)", inserted);
}

}  // namespace x32

// --- Plugin entry point -----------------------------------------------------

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) {
  if (!rec) {
    // Unload.
    if (x32::g_app) {
      x32::g_app->Shutdown();
      delete x32::g_app;
      x32::g_app = nullptr;
    }
    return 0;
  }

  if (x32::g_app) return 1;  // already loaded

  x32::g_app = new x32::App();
  if (!x32::g_app->Init(rec, hInstance)) {
    delete x32::g_app;
    x32::g_app = nullptr;
    return 0;
  }
  return 1;
}
