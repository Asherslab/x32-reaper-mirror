// X32 → REAPER Mirror
// Publishes the versioned C vtable (X32Mirror_Interface) that the embedded-FX
// companion resolves at runtime. Registered under API_X32Mirror_GetInterface,
// and mirrored into ext-state as a hex pointer for the audioMaster-lookup
// fallback path.
#ifndef X32MIRROR_API_EXPORT_H_
#define X32MIRROR_API_EXPORT_H_

namespace x32 {

class App;

void RegisterApi(App* app);
void UnregisterApi();

}  // namespace x32

#endif  // X32MIRROR_API_EXPORT_H_
