#include "binding_serde.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace x32 {

namespace {
std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}
// Strict integer parse: the whole (optionally signed) field must be digits.
bool ParseInt(const std::string& s, int* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (end != s.c_str() + s.size()) return false;  // trailing junk
  *out = static_cast<int>(v);
  return true;
}

}  // namespace

std::string SerializeBinding(const BindingData& b) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "v1|%s|%d|%d|%d|%d",
                StripTypeLabel(b.strip.type), b.strip.index,
                b.enabled ? 1 : 0, b.mirror_mute ? 1 : 0,
                b.mirror_fader ? 1 : 0);
  return buf;
}

bool ParseBinding(const std::string& guid, const std::string& value,
                  BindingData* out) {
  std::vector<std::string> f = Split(value, '|');
  if (f.size() != 6) return false;
  if (f[0] != "v1") return false;  // unknown schema version

  StripType type;
  if (!StripTypeFromLabel(f[1].c_str(), &type)) return false;

  BindingData b;
  b.guid = guid;
  b.strip.type = type;
  int index, enabled, mute, fader;
  if (!ParseInt(f[2], &index) || !ParseInt(f[3], &enabled) ||
      !ParseInt(f[4], &mute) || !ParseInt(f[5], &fader))
    return false;
  b.strip.index = index;
  if (!b.strip.valid()) return false;
  b.enabled = enabled != 0;
  b.mirror_mute = mute != 0;
  b.mirror_fader = fader != 0;
  *out = b;
  return true;
}

}  // namespace x32
