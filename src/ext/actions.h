// X32 → REAPER Mirror
// Registers the plugin's actions (stable _X32MIRROR_* ids) plus the
// hookcommand2 dispatcher and toggleaction state provider that light up
// toolbar buttons. Main-thread only.
#ifndef X32MIRROR_ACTIONS_H_
#define X32MIRROR_ACTIONS_H_

namespace x32 {

class App;

void RegisterActions(App* app);
void UnregisterActions();

// Look up the runtime command id assigned to a registered action by its stable
// idStr (e.g. "_X32MIRROR_TRACK_TOGGLE_MUTE"). Returns 0 if not registered.
// Used by the context-menu hook to wire menu items to actions.
int ActionCommandId(const char* idstr);

}  // namespace x32

#endif  // X32MIRROR_ACTIONS_H_
