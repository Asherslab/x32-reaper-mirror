// X32 → REAPER Mirror
// Global (not per-project) connection + behavior settings, persisted to
// <resource path>/reaper-x32mirror.ini. Hand-rolled key=value parser so this
// stays free of any platform profile API.
#ifndef X32MIRROR_SETTINGS_H_
#define X32MIRROR_SETTINGS_H_

#include <string>

namespace x32 {

struct Settings {
  std::string ip = "192.168.0.2";  // last console IP
  int port = 10023;                // X32 OSC port
  bool autoconnect = false;        // connect on load
  int repoll_interval_ms = 0;      // 0 = off; periodic full re-poll to heal loss
  int log_level = 2;               // 0=Error 1=Warn 2=Info 3=Debug
  int dock_state = 1;              // 1 = docked (passed to DockWindowAddEx)
  bool master_enabled = true;      // global master switch (persisted)

  // Load from the ini (missing file / keys keep defaults).
  void Load();
  // Write back to the ini.
  void Save() const;

  // Full path to the ini (for logging / diagnostics).
  static std::string IniPath();
};

}  // namespace x32

#endif  // X32MIRROR_SETTINGS_H_
