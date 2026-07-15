// Control / dialog IDs for the dockable panel. Shared by panel_res.rc and
// panel.cpp. Kept in the classic Win32 resource-id layout so the same .rc
// compiles on Windows and feeds swell_resgen on macOS/Linux.
#ifndef X32MIRROR_RESOURCE_H_
#define X32MIRROR_RESOURCE_H_

#define IDD_X32PANEL 200

// Connection row.
#define IDC_IP           1000
#define IDC_PORT         1001
#define IDC_CONNECT      1002
#define IDC_SCAN         1003
#define IDC_STATUS       1004

// Master / bulk row.
#define IDC_MASTER       1010
#define IDC_ENABLE_ALL   1011
#define IDC_DISABLE_ALL  1012
#define IDC_FORCE_ON     1013
#define IDC_FORCE_OFF    1014

// Bindings list + per-row operations.
#define IDC_LIST         1020
#define IDC_TOGGLE_EN    1021
#define IDC_TOGGLE_MUTE  1022
#define IDC_TOGGLE_FADER 1023
#define IDC_DEL          1024

// Add-binding row.
#define IDC_STRIPTYPE    1030
#define IDC_STRIPNUM     1031
#define IDC_BIND         1032
#define IDC_INSERT_EMBED 1033

// Static labels (no runtime use, but named for the .rc).
#define IDC_LBL_CONN     1100
#define IDC_LBL_ADD      1101

#endif  // X32MIRROR_RESOURCE_H_
