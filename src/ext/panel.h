// X32 → REAPER Mirror
// Dockable "X32 Mirror" panel. Modeless SWELL/Win32 dialog added to a REAPER
// dock via DockWindowAddEx. Repaints on a 4 Hz sub-tick and whenever the
// engine's state serial changes. Main-thread only.
#ifndef X32MIRROR_PANEL_H_
#define X32MIRROR_PANEL_H_

#include <string>
#include <vector>

#include "reaper_api.h"

namespace x32 {

class App;

class Panel {
 public:
  explicit Panel(App* app) : app_(app) {}

  // Create the dialog and dock it (idempotent — brings to front if it exists).
  void Create();
  void Destroy();
  bool Visible() const;
  HWND hwnd() const { return hwnd_; }

  void MarkDirty() { dirty_ = true; }
  void Tick();  // called from App::Timer (~30 Hz)

  // Preselect a binding row (used by "bind/edit binding…" entry points).
  void SelectGuid(const std::string& guid);

 private:
  static INT_PTR CALLBACK DlgProcStatic(HWND, UINT, WPARAM, LPARAM);
  INT_PTR DlgProc(HWND, UINT, WPARAM, LPARAM);

  void OnInit();
  void OnCommand(int id, int notify);
  void RebuildList();
  void UpdateStatus();
  void FinishScan();
  std::string SelectedGuid();

  App* app_;
  HWND hwnd_ = nullptr;
  bool dirty_ = true;
  bool scanning_ = false;
  unsigned int last_serial_ = 0;
  int subtick_ = 0;
  std::vector<std::string> row_guids_;  // parallels listbox rows
  std::string preselect_;
};

}  // namespace x32

#endif  // X32MIRROR_PANEL_H_
