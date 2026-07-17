#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "reaper_api.h"

namespace x32 {

namespace {
std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
}  // namespace

std::string Settings::IniPath() {
  const char* rp = GetResourcePath ? GetResourcePath() : nullptr;
  std::string base = rp ? rp : ".";
#if defined(_WIN32)
  return base + "\\reaper-x32mirror.ini";
#else
  return base + "/reaper-x32mirror.ini";
#endif
}

void Settings::Load() {
  std::ifstream f(IniPath());
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#' || line[0] == '[') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Trim(line.substr(0, eq));
    std::string val = Trim(line.substr(eq + 1));
    if (key == "ip") ip = val;
    else if (key == "port") port = std::atoi(val.c_str());
    else if (key == "autoconnect") autoconnect = std::atoi(val.c_str()) != 0;
    else if (key == "repoll_interval_ms") repoll_interval_ms = std::atoi(val.c_str());
    else if (key == "log_level") log_level = std::atoi(val.c_str());
    else if (key == "dock_state") dock_state = std::atoi(val.c_str());
    else if (key == "master_enabled") master_enabled = std::atoi(val.c_str()) != 0;
  }
}

void Settings::Save() const {
  std::ofstream f(IniPath(), std::ios::trunc);
  if (!f) return;
  f << "# X32 → REAPER Mirror global settings\n";
  f << "[connection]\n";
  f << "ip=" << ip << "\n";
  f << "port=" << port << "\n";
  f << "autoconnect=" << (autoconnect ? 1 : 0) << "\n";
  f << "[behavior]\n";
  f << "repoll_interval_ms=" << repoll_interval_ms << "\n";
  f << "log_level=" << log_level << "\n";
  f << "dock_state=" << dock_state << "\n";
  f << "master_enabled=" << (master_enabled ? 1 : 0) << "\n";
}

}  // namespace x32
