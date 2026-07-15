// X32 → REAPER Mirror
// Central application object: owns the connection, caches, binding store,
// mirror engine and dockable panel, and exposes the high-level operations that
// actions, menus, the panel and the companion API call into. One global
// instance lives for the plugin's lifetime (created in ReaperPluginEntry,
// destroyed in Shutdown()). Everything here runs on the main thread.
#ifndef X32MIRROR_APP_H_
#define X32MIRROR_APP_H_

#include <string>

#include "binding_store.h"
#include "event_queue.h"
#include "mirror_engine.h"
#include "reaper_api.h"
#include "settings.h"
#include "state_cache.h"
#include "strip_id.h"
#include "x32_connection.h"

namespace x32 {

class Panel;

class App {
 public:
  App();
  ~App();

  bool Init(reaper_plugin_info_t* rec, REAPER_PLUGIN_HINSTANCE hinst);
  void Shutdown();

  // ~30 Hz main-thread timer.
  void Timer();

  // Accessors used by the UI / API layers.
  Settings& settings() { return settings_; }
  BindingStore& store() { return store_; }
  MirrorEngine& engine() { return engine_; }
  X32Connection& conn() { return conn_; }
  StateCache& cache() { return cache_; }
  HWND main_hwnd() const { return main_hwnd_; }
  REAPER_PLUGIN_HINSTANCE hinstance() const { return hinstance_; }

  // --- Connection -----------------------------------------------------------
  void ConnectFromSettings();
  void Disconnect();
  void ConnectToggle();
  bool ConnectionActive() const;  // not Disconnected
  void StartDiscovery();

  // --- Master / bulk ---------------------------------------------------------
  void SetMaster(bool on);
  bool Master() const { return engine_.master(); }
  void ForceAll(bool enabled);  // rewrite per-binding enabled flags

  // --- Panel -----------------------------------------------------------------
  void ShowPanel();
  void TogglePanel();
  bool PanelVisible() const;
  void OpenPanelForTrack(const std::string& guid);
  Panel* panel() { return panel_; }
  void RefreshPanel();  // mark panel dirty

  // --- Track-oriented operations --------------------------------------------
  int SelectedTrackCount() const;
  std::string FirstSelectedGuid() const;
  MediaTrack* NthSelectedTrack(int n) const;

  // Bind each selected track to consecutive strips starting at (type,start).
  void BindSelectedTracks(StripType type, int start_index);
  // Toggle a flag on every selected track that already has a binding.
  void ToggleTrackEnabled();
  void ToggleTrackFlag(Param p);
  // Insert the embedded-FX companion on every bound + resolved track.
  void InsertEmbedOnBound();

 private:
  Settings settings_;
  StateCache cache_;
  CoalescingQueue queue_;
  X32Connection conn_;
  BindingStore store_;
  MirrorEngine engine_;
  Panel* panel_ = nullptr;

  reaper_plugin_info_t* rec_ = nullptr;
  REAPER_PLUGIN_HINSTANCE hinstance_ = nullptr;
  HWND main_hwnd_ = nullptr;
};

extern App* g_app;

}  // namespace x32

#endif  // X32MIRROR_APP_H_
