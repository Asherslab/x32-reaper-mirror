// X32 → REAPER Mirror
// Shared strip identity: which X32 object (channel / bus / dca / ...) a binding
// mirrors. No REAPER or platform dependencies — safe to include from the
// extension, the embedded-FX companion, and the test binaries alike.
#ifndef X32MIRROR_STRIP_ID_H_
#define X32MIRROR_STRIP_ID_H_

#include <cstdint>
#include <cstdio>

namespace x32 {

// Strip families the plugin can mirror. The plan scopes the shipping set to
// CH/BUS/DCA; the remaining rows are declared here (table-driven, see
// x32_addresses.cpp) so /auxin, /fxrtn, /mtx and /main become one-line
// additions later without touching call sites.
enum class StripType : int {
  CH = 0,   // /ch/NN     NN = 01..32
  BUS,      // /bus/NN    NN = 01..16
  DCA,      // /dca/N     N  = 1..8   (single digit, no /mix segment)
  AUXIN,    // /auxin/NN  (not yet wired into the UI)
  FXRTN,    // /fxrtn/NN
  MTX,      // /mtx/NN
  MAIN,     // /main/st | /main/m
  kCount
};

// Which parameter of a strip a binding mirrors.
enum class Param : int {
  Fader = 0,
  Mute,
  kCount
};

// Number of strips available for each family, per the X32 model.
inline int StripCount(StripType t) {
  switch (t) {
    case StripType::CH:    return 32;
    case StripType::BUS:   return 16;
    case StripType::DCA:   return 8;
    case StripType::AUXIN: return 8;
    case StripType::FXRTN: return 8;
    case StripType::MTX:   return 6;
    case StripType::MAIN:  return 2;   // st + m
    default:               return 0;
  }
}

// Short, stable, human-readable family label ("CH", "BUS", ...). Also used as
// the persisted token in project ext-state, so do not change casually.
inline const char* StripTypeLabel(StripType t) {
  switch (t) {
    case StripType::CH:    return "CH";
    case StripType::BUS:   return "BUS";
    case StripType::DCA:   return "DCA";
    case StripType::AUXIN: return "AUXIN";
    case StripType::FXRTN: return "FXRTN";
    case StripType::MTX:   return "MTX";
    case StripType::MAIN:  return "MAIN";
    default:               return "?";
  }
}

// Parse a persisted family token back to a StripType. Returns false on an
// unknown token (forward/backward-compat safe: caller drops the binding).
inline bool StripTypeFromLabel(const char* s, StripType* out) {
  for (int i = 0; i < static_cast<int>(StripType::kCount); ++i) {
    StripType t = static_cast<StripType>(i);
    const char* lbl = StripTypeLabel(t);
    // case-sensitive exact match; tokens we write are always upper-case
    const char* a = lbl;
    const char* b = s;
    while (*a && *b && *a == *b) { ++a; ++b; }
    if (*a == 0 && *b == 0) { *out = t; return true; }
  }
  return false;
}

// A concrete strip: family + 1-based index within that family.
struct StripId {
  StripType type = StripType::CH;
  int index = 1;  // 1-based (matches the console's own numbering)

  bool operator==(const StripId& o) const {
    return type == o.type && index == o.index;
  }
  bool operator!=(const StripId& o) const { return !(*this == o); }

  // Stable ordering / hash key: pack family and index into one integer.
  int key() const { return static_cast<int>(type) * 1000 + index; }

  bool valid() const {
    return index >= 1 && index <= StripCount(type);
  }
};

// Compose a display label such as "CH 07" or "DCA 3".
inline void StripLabel(const StripId& id, char* buf, int buflen) {
  if (id.type == StripType::DCA || id.type == StripType::MAIN) {
    std::snprintf(buf, buflen, "%s %d", StripTypeLabel(id.type), id.index);
  } else {
    std::snprintf(buf, buflen, "%s %02d", StripTypeLabel(id.type), id.index);
  }
}

}  // namespace x32

#endif  // X32MIRROR_STRIP_ID_H_
