#include "x32_addresses.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace x32 {

namespace {

// One row per strip family. The X32 address grammar is regular except for two
// wrinkles this table captures:
//   * DCA uses a single-digit index and has NO "/mix" segment.
//   * MAIN uses named indices ("st"/"m") rather than numbers.
struct FamilyRow {
  StripType type;
  const char* prefix;   // segment after the leading '/', e.g. "ch"
  bool has_mix;         // whether "/mix" sits between index and param
  int pad;              // zero-pad width for numeric index (0 = single digit)
  const char* names[2]; // named indices for MAIN ({"st","m"}); nullptr otherwise
};

// NOTE: order is irrelevant; lookups are by StripType / by prefix match.
const FamilyRow kFamilies[] = {
    {StripType::CH,    "ch",    true,  2, {nullptr, nullptr}},
    {StripType::BUS,   "bus",   true,  2, {nullptr, nullptr}},
    {StripType::DCA,   "dca",   false, 0, {nullptr, nullptr}},
    {StripType::AUXIN, "auxin", true,  2, {nullptr, nullptr}},
    {StripType::FXRTN, "fxrtn", true,  2, {nullptr, nullptr}},
    {StripType::MTX,   "mtx",   true,  2, {nullptr, nullptr}},
    {StripType::MAIN,  "main",  true,  0, {"st", "m"}},
};

const FamilyRow* RowFor(StripType t) {
  for (const auto& r : kFamilies)
    if (r.type == t) return &r;
  return nullptr;
}

const FamilyRow* RowForPrefix(const char* prefix, size_t len) {
  for (const auto& r : kFamilies) {
    if (std::strlen(r.prefix) == len && std::strncmp(r.prefix, prefix, len) == 0)
      return &r;
  }
  return nullptr;
}

const char* ParamSegment(Param p) { return p == Param::Fader ? "fader" : "on"; }

}  // namespace

std::string AddressFor(const StripId& id, Param p) {
  const FamilyRow* row = RowFor(id.type);
  if (!row || !id.valid()) return std::string();

  char idxseg[16];
  if (row->names[0] != nullptr) {
    // Named index (MAIN).
    if (id.index < 1 || id.index > 2) return std::string();
    std::snprintf(idxseg, sizeof(idxseg), "%s", row->names[id.index - 1]);
  } else if (row->pad > 0) {
    std::snprintf(idxseg, sizeof(idxseg), "%0*d", row->pad, id.index);
  } else {
    std::snprintf(idxseg, sizeof(idxseg), "%d", id.index);
  }

  std::string addr = "/";
  addr += row->prefix;
  addr += "/";
  addr += idxseg;
  if (row->has_mix) addr += "/mix";
  addr += "/";
  addr += ParamSegment(p);
  return addr;
}

bool ParseAddress(const std::string& address, StripId* id, Param* p) {
  // Expected shapes:
  //   /<prefix>/<idx>/mix/<fader|on>
  //   /<prefix>/<idx>/<fader|on>          (DCA)
  if (address.empty() || address[0] != '/') return false;

  // Split into segments.
  const char* s = address.c_str() + 1;  // skip leading '/'
  const char* seg[8];
  size_t seglen[8];
  int n = 0;
  const char* start = s;
  for (const char* c = s;; ++c) {
    if (*c == '/' || *c == 0) {
      if (n >= 8) return false;
      seg[n] = start;
      seglen[n] = static_cast<size_t>(c - start);
      ++n;
      if (*c == 0) break;
      start = c + 1;
    }
  }

  if (n < 3) return false;

  const FamilyRow* row = RowForPrefix(seg[0], seglen[0]);
  if (!row) return false;

  // Determine the parameter segment (last segment) and validate /mix presence.
  int last = n - 1;
  Param param;
  if (seglen[last] == 5 && std::strncmp(seg[last], "fader", 5) == 0) {
    param = Param::Fader;
  } else if (seglen[last] == 2 && std::strncmp(seg[last], "on", 2) == 0) {
    param = Param::Mute;
  } else {
    return false;
  }

  if (row->has_mix) {
    // Need exactly: prefix / idx / mix / param  -> 4 segments.
    if (n != 4) return false;
    if (!(seglen[2] == 3 && std::strncmp(seg[2], "mix", 3) == 0)) return false;
  } else {
    // prefix / idx / param -> 3 segments.
    if (n != 3) return false;
  }

  // Parse the index segment.
  int index = 0;
  if (row->names[0] != nullptr) {
    index = 0;
    for (int i = 0; i < 2; ++i) {
      if (std::strlen(row->names[i]) == seglen[1] &&
          std::strncmp(row->names[i], seg[1], seglen[1]) == 0) {
        index = i + 1;
        break;
      }
    }
    if (index == 0) return false;
  } else {
    // Numeric (accepts zero-padded and bare forms).
    if (seglen[1] == 0 || seglen[1] > 3) return false;
    for (size_t i = 0; i < seglen[1]; ++i) {
      char ch = seg[1][i];
      if (ch < '0' || ch > '9') return false;
      index = index * 10 + (ch - '0');
    }
  }

  StripId out;
  out.type = row->type;
  out.index = index;
  if (!out.valid()) return false;

  *id = out;
  *p = param;
  return true;
}

double FaderFloatToDb(float f) {
  if (f <= 0.0f) return kMinusInfDb;
  double d = static_cast<double>(f);
  if (d >= 0.5)     return 40.0 * d - 30.0;
  if (d >= 0.25)    return 80.0 * d - 50.0;
  if (d >= 0.0625)  return 160.0 * d - 70.0;
  return 480.0 * d - 90.0;
}

double DbToVol(double db) {
  if (db <= kMinusInfDb) return 0.0;
  return std::pow(10.0, db / 20.0);
}

double FaderFloatToVol(float f) {
  if (f <= 0.0f) return 0.0;
  return DbToVol(FaderFloatToDb(f));
}

}  // namespace x32
