#include "panel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app.h"
#include "log.h"
#include "mirror_engine.h"
#include "resource.h"
#include "settings.h"
#include "ui_compat.h"

namespace x32 {

namespace {
constexpr UINT_PTR kScanTimer = 0xA32;

// Strip families offered in the "bind" combo, in display order.
const StripType kUiStripTypes[] = {StripType::CH, StripType::BUS,
                                    StripType::DCA};

void SetText(HWND dlg, int id, const char* s) {
  HWND c = GetDlgItem(dlg, id);
  if (c) SetWindowText(c, s);
}
std::string GetText(HWND dlg, int id) {
  char buf[256] = {0};
  HWND c = GetDlgItem(dlg, id);
  if (c) GetWindowText(c, buf, sizeof(buf));
  return buf;
}
}  // namespace

void Panel::Create() {
  if (hwnd_) {
    if (DockWindowActivate) DockWindowActivate(hwnd_);
    return;
  }
  hwnd_ = CreateDialogParam(app_->hinstance(),
                            MAKEINTRESOURCE(IDD_X32PANEL), app_->main_hwnd(),
                            DlgProcStatic, reinterpret_cast<LPARAM>(this));
  if (!hwnd_) {
    X32LOGE("failed to create panel dialog");
    return;
  }
  if (DockWindowAddEx)
    DockWindowAddEx(hwnd_, "X32 Mirror", "x32mirror", true);
  if (DockWindowActivate) DockWindowActivate(hwnd_);
}

void Panel::Destroy() {
  if (!hwnd_) return;
  if (DockWindowRemove) DockWindowRemove(hwnd_);
  DestroyWindow(hwnd_);
  hwnd_ = nullptr;
}

bool Panel::Visible() const {
  return hwnd_ && IsWindowVisible(hwnd_);
}

void Panel::SelectGuid(const std::string& guid) {
  preselect_ = guid;
  if (!hwnd_) return;
  auto it = std::find(row_guids_.begin(), row_guids_.end(), guid);
  if (it != row_guids_.end()) {
    int idx = static_cast<int>(it - row_guids_.begin());
    SendDlgItemMessage(hwnd_, IDC_LIST, LB_SETCURSEL, idx, 0);
  }
}

void Panel::Tick() {
  if (!hwnd_) return;
  unsigned int s = app_->engine().state_serial();
  if (s != last_serial_ || dirty_) {
    last_serial_ = s;
    dirty_ = false;
    RebuildList();
    UpdateStatus();
  } else if ((++subtick_ % 8) == 0) {
    UpdateStatus();  // keep the status line fresh even without a serial bump
  }
}

INT_PTR CALLBACK Panel::DlgProcStatic(HWND h, UINT msg, WPARAM w, LPARAM l) {
  Panel* self;
  if (msg == WM_INITDIALOG) {
    self = reinterpret_cast<Panel*>(l);
    SetWindowUserPtr(h, self);
    self->hwnd_ = h;
  } else {
    self = reinterpret_cast<Panel*>(GetWindowUserPtr(h));
  }
  if (!self) return 0;
  return self->DlgProc(h, msg, w, l);
}

INT_PTR Panel::DlgProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_INITDIALOG:
      OnInit();
      return 1;
    case WM_COMMAND:
      OnCommand(LOWORD(w), HIWORD(w));
      return 0;
    case WM_TIMER:
      if (w == kScanTimer) {
        FinishScan();
        return 0;
      }
      return 0;
    case WM_DESTROY:
      return 0;
    default:
      return 0;
  }
}

void Panel::OnInit() {
  Settings& st = app_->settings();
  SetText(hwnd_, IDC_IP, st.ip.c_str());
  char portbuf[16];
  std::snprintf(portbuf, sizeof(portbuf), "%d", st.port);
  SetText(hwnd_, IDC_PORT, portbuf);

  // Populate the strip-type combo.
  for (StripType t : kUiStripTypes) {
    SendDlgItemMessage(hwnd_, IDC_STRIPTYPE, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(StripTypeLabel(t)));
  }
  SendDlgItemMessage(hwnd_, IDC_STRIPTYPE, CB_SETCURSEL, 0, 0);
  SetText(hwnd_, IDC_STRIPNUM, "1");

  CheckDlgButton(hwnd_, IDC_MASTER, app_->Master() ? BST_CHECKED : BST_UNCHECKED);

  RebuildList();
  UpdateStatus();
  if (!preselect_.empty()) SelectGuid(preselect_);
}

void Panel::RebuildList() {
  HWND lb = GetDlgItem(hwnd_, IDC_LIST);
  if (!lb) return;
  int prev_idx = static_cast<int>(SendMessage(lb, LB_GETCURSEL, 0, 0));
  std::string prev_guid = (prev_idx >= 0 &&
                           prev_idx < static_cast<int>(row_guids_.size()))
                              ? row_guids_[prev_idx]
                              : std::string();
  SendMessage(lb, LB_RESETCONTENT, 0, 0);
  row_guids_.clear();

  // Snapshot + stable sort by strip so the list does not jump around.
  std::vector<const Binding*> items;
  for (auto& kv : app_->store().all()) items.push_back(&kv.second);
  std::sort(items.begin(), items.end(),
            [](const Binding* a, const Binding* b) {
              return a->strip.key() < b->strip.key();
            });

  for (const Binding* b : items) {
    Binding* mut = app_->store().Get(b->guid);
    MediaTrack* tr = mut ? app_->store().ResolveTrack(mut) : nullptr;
    char strip_lbl[24];
    StripLabel(b->strip, strip_lbl, sizeof(strip_lbl));

    char name[512] = "";
    if (tr && GetSetMediaTrackInfo_String)
      GetSetMediaTrackInfo_String(tr, "P_NAME", name, false);

    char line[320];
    std::snprintf(line, sizeof(line), "%s%s\t%s\t%s %s %s",
                  tr ? "" : "(missing) ",
                  (name[0] ? name : (tr ? "(unnamed)" : b->guid.c_str())),
                  strip_lbl,
                  b->enabled ? "En" : "--",
                  b->mirror_mute ? "M" : "-",
                  b->mirror_fader ? "F" : "-");
    SendMessage(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    row_guids_.push_back(b->guid);
  }

  if (!prev_guid.empty()) {
    auto it = std::find(row_guids_.begin(), row_guids_.end(), prev_guid);
    if (it != row_guids_.end())
      SendMessage(lb, LB_SETCURSEL, it - row_guids_.begin(), 0);
  }
}

void Panel::UpdateStatus() {
  StatusView s = app_->engine().status();
  SetText(hwnd_, IDC_STATUS, s.status_text);
  SetText(hwnd_, IDC_CONNECT,
          app_->ConnectionActive() ? "Disconnect" : "Connect");
  CheckDlgButton(hwnd_, IDC_MASTER,
                 s.master_enabled ? BST_CHECKED : BST_UNCHECKED);
}

std::string Panel::SelectedGuid() {
  int idx = static_cast<int>(SendDlgItemMessage(hwnd_, IDC_LIST, LB_GETCURSEL, 0, 0));
  if (idx < 0 || idx >= static_cast<int>(row_guids_.size())) return "";
  return row_guids_[idx];
}

void Panel::FinishScan() {
  KillTimer(hwnd_, kScanTimer);
  scanning_ = false;
  std::vector<DiscoveredConsole> found = app_->engine().TakeDiscoveries();
  if (found.empty()) {
    SetText(hwnd_, IDC_STATUS, "Scan: no consoles found");
    return;
  }
  HMENU menu = CreatePopupMenu();
  for (size_t i = 0; i < found.size(); ++i) {
    char item[192];
    std::snprintf(item, sizeof(item), "%s — %s (%s)", found[i].ip.c_str(),
                  found[i].name.c_str(), found[i].model.c_str());
    MenuAppend(menu, MF_STRING, static_cast<UINT_PTR>(i + 1), item);
  }
  RECT r;
  GetWindowRect(GetDlgItem(hwnd_, IDC_SCAN), &r);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN,
                           r.left, r.bottom, 0, hwnd_, nullptr);
  DestroyMenu(menu);
  if (cmd >= 1 && cmd <= static_cast<int>(found.size())) {
    const DiscoveredConsole& d = found[cmd - 1];
    SetText(hwnd_, IDC_IP, d.ip.c_str());
    app_->settings().ip = d.ip;
  }
}

void Panel::OnCommand(int id, int notify) {
  BindingStore& store = app_->store();
  MirrorEngine& eng = app_->engine();

  switch (id) {
    case IDC_CONNECT: {
      if (app_->ConnectionActive()) {
        app_->Disconnect();
      } else {
        // Push the edited fields into settings, then connect.
        app_->settings().ip = GetText(hwnd_, IDC_IP);
        int port = std::atoi(GetText(hwnd_, IDC_PORT).c_str());
        if (port > 0 && port < 65536) app_->settings().port = port;
        app_->ConnectFromSettings();
      }
      MarkDirty();
      break;
    }
    case IDC_SCAN:
      app_->StartDiscovery();
      scanning_ = true;
      SetText(hwnd_, IDC_STATUS, "Scanning for consoles…");
      SetTimer(hwnd_, kScanTimer, 1600, nullptr);
      break;
    case IDC_MASTER:
      app_->SetMaster(IsDlgButtonChecked(hwnd_, IDC_MASTER) == BST_CHECKED);
      break;
    case IDC_ENABLE_ALL:
      app_->SetMaster(true);
      break;
    case IDC_DISABLE_ALL:
      app_->SetMaster(false);
      break;
    case IDC_FORCE_ON:
      app_->ForceAll(true);
      MarkDirty();
      break;
    case IDC_FORCE_OFF:
      app_->ForceAll(false);
      MarkDirty();
      break;
    case IDC_TOGGLE_EN: {
      std::string g = SelectedGuid();
      if (!g.empty()) {
        store.ToggleEnabled(g);
        eng.SeedBinding(g);
        MarkDirty();
      }
      break;
    }
    case IDC_TOGGLE_MUTE:
    case IDC_TOGGLE_FADER: {
      std::string g = SelectedGuid();
      Binding* b = g.empty() ? nullptr : store.Get(g);
      if (b) {
        Param p = id == IDC_TOGGLE_MUTE ? Param::Mute : Param::Fader;
        bool cur = p == Param::Mute ? b->mirror_mute : b->mirror_fader;
        store.SetFlag(g, p, !cur);
        eng.SeedBinding(g);
        MarkDirty();
      }
      break;
    }
    case IDC_DEL: {
      std::string g = SelectedGuid();
      if (!g.empty()) {
        store.Remove(g);
        eng.BumpSerial();
        MarkDirty();
      }
      break;
    }
    case IDC_BIND: {
      int sel = static_cast<int>(
          SendDlgItemMessage(hwnd_, IDC_STRIPTYPE, CB_GETCURSEL, 0, 0));
      if (sel < 0 || sel >= static_cast<int>(sizeof(kUiStripTypes) /
                                             sizeof(kUiStripTypes[0])))
        sel = 0;
      int num = std::atoi(GetText(hwnd_, IDC_STRIPNUM).c_str());
      if (num < 1) num = 1;
      app_->BindSelectedTracks(kUiStripTypes[sel], num);
      MarkDirty();
      break;
    }
    case IDC_INSERT_EMBED:
      app_->InsertEmbedOnBound();
      break;
    default:
      break;
  }
}

}  // namespace x32

// --- SWELL dialog generation (macOS/Linux) ---------------------------------
// On Windows the .rc is compiled by the resource compiler; elsewhere
// swell_resgen turns panel_res.rc into this generated translation of the
// DIALOG resource into SWELL dialog-creation calls.
#ifndef _WIN32
#include "swell/swell-dlggen.h"
#include "panel_res.rc_mac_dlg"
#endif
