// X32 → REAPER Mirror
// Table-driven mapping between StripId + Param and X32 OSC addresses, plus the
// Maillot fader float→dB curve and the conversion to REAPER's linear D_VOL.
//
// Adding a strip family later (/auxin, /fxrtn, /mtx, /main) is a single row in
// the table in the .cpp — no call-site changes. Pure math + string work, unit
// tested in tests/test_addresses.cpp and tests/test_fader_math.cpp.
#ifndef X32MIRROR_X32_ADDRESSES_H_
#define X32MIRROR_X32_ADDRESSES_H_

#include <string>

#include "strip_id.h"

namespace x32 {

// Build the OSC address for a strip parameter, e.g.
//   {CH, 7},  Param::Fader -> "/ch/07/mix/fader"
//   {DCA, 3}, Param::Mute  -> "/dca/3/on"       (single digit, no /mix)
// Returns empty string if the StripId is out of range.
std::string AddressFor(const StripId& id, Param p);

// Parse an incoming address into (StripId, Param). Returns true if the address
// is a strip fader/on address we track. Ignores anything else (e.g. /xinfo,
// /meters, node dumps) by returning false.
bool ParseAddress(const std::string& address, StripId* id, Param* p);

// --- Fader curve ------------------------------------------------------------

// Maillot piecewise float(0..1) -> dB. f=0.75 -> 0 dB (unity); f=0 -> -inf
// (returned as a large negative sentinel, see kMinusInfDb).
double FaderFloatToDb(float f);

// Sentinel used for -inf dB (fader fully closed).
constexpr double kMinusInfDb = -144.0;

// dB -> REAPER linear track volume (D_VOL). 0 dB -> 1.0. -inf -> 0.0.
double DbToVol(double db);

// Convenience: float(0..1) -> D_VOL directly (f<=0 -> 0.0 exactly).
double FaderFloatToVol(float f);

}  // namespace x32

#endif  // X32MIRROR_X32_ADDRESSES_H_
