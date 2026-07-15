// X32 → REAPER Mirror
// Tiny logging shim over ShowConsoleMsg. Level is read from the global ini at
// startup; messages below the threshold are dropped cheaply.
#ifndef X32MIRROR_LOG_H_
#define X32MIRROR_LOG_H_

namespace x32 {

enum class LogLevel { Error = 0, Warn, Info, Debug };

void SetLogLevel(LogLevel lvl);
LogLevel GetLogLevel();

// printf-style; always prefixed with "[X32Mirror] ".
void Log(LogLevel lvl, const char* fmt, ...);

#define X32LOGE(...) ::x32::Log(::x32::LogLevel::Error, __VA_ARGS__)
#define X32LOGW(...) ::x32::Log(::x32::LogLevel::Warn, __VA_ARGS__)
#define X32LOGI(...) ::x32::Log(::x32::LogLevel::Info, __VA_ARGS__)
#define X32LOGD(...) ::x32::Log(::x32::LogLevel::Debug, __VA_ARGS__)

}  // namespace x32

#endif  // X32MIRROR_LOG_H_
