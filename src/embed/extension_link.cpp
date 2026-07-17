#include "extension_link.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace x32embed {

namespace {
// REAPER's VST audioMaster vendor opcode and selectors (see PLAN.md flagged
// items 1 & 2). 0xdeadbeef = vendor call; 0xdeadf00d = resolve a named API
// function (like GetFunc); 0xdeadf00e = query FX context, index 1 = MediaTrack*.
constexpr int32_t kReaperVendor = static_cast<int32_t>(0xdeadbeef);
constexpr intptr_t kSelGetFunc = static_cast<intptr_t>(0xdeadf00d);
constexpr intptr_t kSelGetContext = static_cast<intptr_t>(0xdeadf00e);
constexpr int32_t kContextTrack = 1;

void* HostGetFunc(AEffect* effect, audioMasterCallback am, const char* name) {
  if (!am) return nullptr;
  return reinterpret_cast<void*>(
      am(effect, kReaperVendor, static_cast<int32_t>(kSelGetFunc), 0,
         const_cast<char*>(name), 0.0f));
}
}  // namespace

void ExtensionLink::ResolveFuncs(AEffect* effect, audioMasterCallback am) {
  if (funcs_resolved_) return;
  funcs_resolved_ = true;
  get_track_guid_ = reinterpret_cast<void* (*)(void*)>(
      HostGetFunc(effect, am, "GetTrackGUID"));
  guid_to_string_ = reinterpret_cast<void (*)(const void*, char*)>(
      HostGetFunc(effect, am, "guidToString"));
  get_ext_state_ = reinterpret_cast<const char* (*)(const char*, const char*)>(
      HostGetFunc(effect, am, "GetExtState"));
}

X32Mirror_Interface* ExtensionLink::Interface(AEffect* effect,
                                              audioMasterCallback am) {
  // Primary path: resolve the registered API function and call it.
  using GetIfaceFn = X32Mirror_Interface* (*)();
  GetIfaceFn fn = reinterpret_cast<GetIfaceFn>(
      HostGetFunc(effect, am, "X32Mirror_GetInterface"));
  X32Mirror_Interface* iface = fn ? fn() : nullptr;

  // Fallback path: read the hex pointer the extension published to ext-state.
  if (!iface) {
    ResolveFuncs(effect, am);
    if (get_ext_state_) {
      const char* hex = get_ext_state_(X32MIRROR_EXTSTATE_SECTION,
                                       X32MIRROR_EXTSTATE_IFACE_KEY);
      if (hex && hex[0]) {
        void* p = nullptr;
        if (std::sscanf(hex, "%p", &p) == 1) p = p;  // parse
        iface = reinterpret_cast<X32Mirror_Interface*>(p);
      }
    }
  }

  iface_ = iface;
  version_ok_ = iface_ && iface_->version == X32MIRROR_API_VERSION &&
                iface_->struct_size ==
                    static_cast<int>(sizeof(X32Mirror_Interface));
  return version_ok_ ? iface_ : nullptr;
}

void* ExtensionLink::HostTrack(AEffect* effect, audioMasterCallback am) {
  if (!am) return nullptr;
  return reinterpret_cast<void*>(
      am(effect, kReaperVendor, static_cast<int32_t>(kSelGetContext),
         kContextTrack, nullptr, 0.0f));
}

const char* ExtensionLink::HostGuid(AEffect* effect, audioMasterCallback am) {
  guid_buf_[0] = 0;
  void* tr = HostTrack(effect, am);
  if (!tr) return guid_buf_;
  ResolveFuncs(effect, am);
  if (get_track_guid_ && guid_to_string_) {
    void* g = get_track_guid_(tr);
    if (g) guid_to_string_(g, guid_buf_);
  }
  return guid_buf_;
}

}  // namespace x32embed
