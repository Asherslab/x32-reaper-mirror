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
  b.strip.index = std::atoi(f[2].c_str());
  if (!b.strip.valid()) return false;
  b.enabled = std::atoi(f[3].c_str()) != 0;
  b.mirror_mute = std::atoi(f[4].c_str()) != 0;
  b.mirror_fader = std::atoi(f[5].c_str()) != 0;
  *out = b;
  return true;
}

}  // namespace x32
