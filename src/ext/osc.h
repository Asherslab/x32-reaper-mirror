// X32 → REAPER Mirror
// Hand-rolled OSC 1.0 codec. The X32 speaks a deliberately small subset:
// single messages (never bundles), big-endian, 4-byte zero-padded strings and
// blobs, and the type tags we care about are ',' (none), 'i', 'f', 's'.
//
// No REAPER or platform dependencies — pure buffer in / buffer out, unit
// tested in tests/test_osc.cpp.
#ifndef X32MIRROR_OSC_H_
#define X32MIRROR_OSC_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace x32 {

// A decoded OSC argument. Only the tags the X32 uses are represented; anything
// else decodes to Unknown and its raw payload is skipped so the message as a
// whole still parses.
struct OscArg {
  enum class Type { Int, Float, String, Blob, Unknown } type = Type::Unknown;
  int32_t i = 0;
  float f = 0.0f;
  std::string s;         // for String
  std::vector<uint8_t> blob;  // for Blob
  char tag = '?';        // raw type tag, for diagnostics
};

struct OscMessage {
  std::string address;          // e.g. "/ch/01/mix/fader"
  std::vector<OscArg> args;
};

// --- Encoding ---------------------------------------------------------------

// Build a no-argument query message ("/ch/01/mix/fader" with an empty ","
// type-tag string). This is the ONLY shape the plugin ever sends to a strip —
// the console replies with the current value. Enforces the one-way guarantee
// at the codec level: there is no encode path that appends a value argument.
std::vector<uint8_t> OscEncodeQuery(const std::string& address);

// --- Decoding ---------------------------------------------------------------

// Decode a single OSC message from buf[0..len). Returns true on success.
// Tolerant of trailing padding; rejects malformed length/padding.
bool OscDecode(const uint8_t* buf, size_t len, OscMessage* out);

// Convenience overload.
inline bool OscDecode(const std::vector<uint8_t>& buf, OscMessage* out) {
  return OscDecode(buf.data(), buf.size(), out);
}

// --- Low-level helpers (exposed for testing) --------------------------------

// Round n up to the next multiple of 4 (OSC alignment).
inline size_t OscPad4(size_t n) { return (n + 3u) & ~static_cast<size_t>(3u); }

}  // namespace x32

#endif  // X32MIRROR_OSC_H_
