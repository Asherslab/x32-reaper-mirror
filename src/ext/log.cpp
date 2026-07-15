#include "log.h"

#include <cstdarg>
#include <cstdio>

#include "reaper_api.h"

namespace x32 {

namespace {
LogLevel g_level = LogLevel::Info;
const char* LevelTag(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return "E";
    case LogLevel::Warn:  return "W";
    case LogLevel::Info:  return "I";
    case LogLevel::Debug: return "D";
  }
  return "?";
}
}  // namespace

void SetLogLevel(LogLevel lvl) { g_level = lvl; }
LogLevel GetLogLevel() { return g_level; }

void Log(LogLevel lvl, const char* fmt, ...) {
  if (static_cast<int>(lvl) > static_cast<int>(g_level)) return;
  char body[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  char line[1100];
  std::snprintf(line, sizeof(line), "[X32Mirror][%s] %s\n", LevelTag(lvl), body);
  if (ShowConsoleMsg) ShowConsoleMsg(line);
}

}  // namespace x32
