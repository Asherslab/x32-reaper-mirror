#include "osc.h"

#include <cstring>

namespace x32 {

namespace {

// Append a 4-byte-aligned, NUL-terminated OSC string.
void PutString(std::vector<uint8_t>& out, const std::string& s) {
  out.insert(out.end(), s.begin(), s.end());
  // At least one NUL terminator, then pad to a 4-byte boundary.
  out.push_back(0);
  while (out.size() % 4 != 0) out.push_back(0);
}

void PutBigEndian32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>(v & 0xff));
}

// Read a NUL-terminated string starting at *pos, advancing *pos past the
// 4-byte-aligned padding. Returns false if the string is unterminated within
// the buffer.
bool ReadString(const uint8_t* buf, size_t len, size_t* pos, std::string* out) {
  size_t start = *pos;
  size_t i = start;
  while (i < len && buf[i] != 0) ++i;
  if (i >= len) return false;  // no terminator
  out->assign(reinterpret_cast<const char*>(buf + start), i - start);
  // Advance past the terminator, then to the next 4-byte boundary.
  size_t after = i + 1;
  *pos = OscPad4(after);
  return *pos <= len;
}

bool ReadBigEndian32(const uint8_t* buf, size_t len, size_t* pos, uint32_t* out) {
  if (*pos + 4 > len) return false;
  const uint8_t* p = buf + *pos;
  *out = (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
  *pos += 4;
  return true;
}

}  // namespace

std::vector<uint8_t> OscEncodeQuery(const std::string& address) {
  std::vector<uint8_t> out;
  PutString(out, address);
  // Empty type-tag string: just "," (then padded). No value arguments — this
  // is the crux of the one-way guarantee at the codec level.
  PutString(out, ",");
  return out;
}

std::vector<uint8_t> OscEncodeString(const std::string& address,
                                     const std::string& value) {
  std::vector<uint8_t> out;
  PutString(out, address);
  PutString(out, ",s");
  PutString(out, value);
  return out;
}

bool OscDecode(const uint8_t* buf, size_t len, OscMessage* out) {
  if (!buf || len < 4) return false;
  // Bundles ("#bundle") are not used by the X32 and not supported here.
  if (buf[0] == '#') return false;
  if (buf[0] != '/') return false;

  size_t pos = 0;
  if (!ReadString(buf, len, &pos, &out->address)) return false;

  out->args.clear();

  // Type-tag string is optional in the wild; if absent, treat as no args.
  if (pos >= len) return true;

  std::string tags;
  if (!ReadString(buf, len, &pos, &tags)) return false;
  if (tags.empty() || tags[0] != ',') {
    // Not a valid type-tag string — but the address parsed, so treat as a
    // no-argument message rather than failing hard.
    return true;
  }

  for (size_t t = 1; t < tags.size(); ++t) {
    char tag = tags[t];
    OscArg arg;
    arg.tag = tag;
    switch (tag) {
      case 'i': {
        uint32_t v;
        if (!ReadBigEndian32(buf, len, &pos, &v)) return false;
        arg.type = OscArg::Type::Int;
        arg.i = static_cast<int32_t>(v);
        break;
      }
      case 'f': {
        uint32_t v;
        if (!ReadBigEndian32(buf, len, &pos, &v)) return false;
        float f;
        std::memcpy(&f, &v, sizeof(f));  // IEEE-754, already host order after BE read
        arg.type = OscArg::Type::Float;
        arg.f = f;
        break;
      }
      case 's':
      case 'S': {  // 'S' = symbol, decode identically
        std::string s;
        if (!ReadString(buf, len, &pos, &s)) return false;
        arg.type = OscArg::Type::String;
        arg.s = std::move(s);
        break;
      }
      case 'b': {
        uint32_t blen;
        if (!ReadBigEndian32(buf, len, &pos, &blen)) return false;
        if (pos + blen > len) return false;
        arg.type = OscArg::Type::Blob;
        arg.blob.assign(buf + pos, buf + pos + blen);
        pos = OscPad4(pos + blen);
        if (pos > len) return false;
        break;
      }
      case 'T': arg.type = OscArg::Type::Int; arg.i = 1; break;  // true, no payload
      case 'F': arg.type = OscArg::Type::Int; arg.i = 0; break;  // false, no payload
      case 'N':  // nil
      case 'I':  // infinitum
        arg.type = OscArg::Type::Unknown;
        break;
      default:
        // Unknown tag with no way to know its payload width: we cannot safely
        // continue past it, so stop here but keep what we parsed.
        out->args.push_back(arg);
        return true;
    }
    out->args.push_back(std::move(arg));
  }
  return true;
}

}  // namespace x32
