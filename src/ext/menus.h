// X32 → REAPER Mirror
// Track control-panel context-menu integration via hookcustommenu. Adds an
// "X32 Mirror" submenu whose items are wired to the registered _X32MIRROR_*
// actions, with checkmarks reflecting the selected track's binding flags.
#ifndef X32MIRROR_MENUS_H_
#define X32MIRROR_MENUS_H_

namespace x32 {

class App;

void RegisterMenus(App* app);
void UnregisterMenus();

}  // namespace x32

#endif  // X32MIRROR_MENUS_H_
