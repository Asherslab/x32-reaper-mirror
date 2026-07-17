// X32 → REAPER Mirror
// Serialization for a single binding's persisted value. REAPER-free so it can
// live in x32core and be unit tested directly. The GUID is the project
// ext-state *key*, so it is passed alongside rather than embedded in the value.
//
// Value format (versioned):  "v1|CH|7|1|1|1"
//   field 0: schema version tag ("v1")
//   field 1: strip family label (CH/BUS/DCA/...)
//   field 2: 1-based strip index
//   field 3: enabled          (0/1)
//   field 4: mirror_mute      (0/1)
//   field 5: mirror_fader     (0/1)
#ifndef X32MIRROR_BINDING_SERDE_H_
#define X32MIRROR_BINDING_SERDE_H_

#include <string>

#include "strip_id.h"

namespace x32 {

struct BindingData {
  std::string guid;   // ext-state key
  StripId strip;
  bool enabled = true;
  bool mirror_mute = true;
  bool mirror_fader = true;
};

// Serialize the value portion (does not include the GUID).
std::string SerializeBinding(const BindingData& b);

// Parse a stored value into *out (with out->guid set from the key argument).
// Returns false on an unknown version, malformed value, or an out-of-range /
// unknown strip — callers drop such entries (forward-compat safe).
bool ParseBinding(const std::string& guid, const std::string& value,
                  BindingData* out);

}  // namespace x32

#endif  // X32MIRROR_BINDING_SERDE_H_
