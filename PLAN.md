# X32 → REAPER Mirror — native C++ REAPER extension (plan)

## Context

Greenfield project in the empty repo `asherslab/x32-reaper-mirror` (branch `claude/reaper-x32-osc-plugin-a7n4pd`). The user runs a Behringer X32 console alongside REAPER and wants REAPER tracks to **mirror** the console's per-channel **mute** and **fader**, **strictly one-way** (X32 → REAPER; the plugin must never send control data to the console — only no-arg queries, `/node`, `/xremote` keepalives, `/xinfo` discovery). Mute and fader mirroring are individually toggleable per binding; each binding is individually enable/disable-able; global Enable All / Disable All quick actions exist; controls are reachable from the mixer window. This plan hands off to an implementation agent.

## Confirmed user decisions

| Topic | Decision |
|---|---|
| Mixer UI | **Both**: dockable panel + track right-click context menu, **and** a companion embedded-FX plugin giving a real in-strip button (REAPER "embedded UI in MCP"). Mix and match. |
| Mapping | Manual per-track assignment, keyed by **track GUID**. |
| X32 scope | `/ch/01–32`, `/bus/01–16`, `/dca/1–8` (table-driven so `/auxin`, `/fxrtn`, `/mtx`, `/main` are one-row additions later). |
| Platforms | Windows + macOS + Linux; CMake; SWELL for cross-platform UI. |
| Persistence | Bindings per-project (`SetProjExtState`, section `X32MIRROR`); connection settings in a global ini. |
| Connection | Manual IP/port + `/xinfo` broadcast discovery scan. |
| Enable/Disable All | **Both models**: a global **master switch** (`effective = master && binding.enabled && paramFlag`) with lit toggle actions, **plus** separate "force all bindings on/off" actions & panel buttons that rewrite per-binding flags. |
| Automation | **Control-surface behavior**: apply via `CSurf_OnVolumeChangeEx`/`CSurf_OnMuteChangeEx` (no gang) — writes automation in write/touch modes, plain move otherwise, no undo points ever. |

## Verified technical foundation (from research; treat as ground truth)

**REAPER SDK** (justinfrankel/reaper-sdk + WDL/SWELL; CMake scaffolding modeled on ak5k/reaper-sdk-vscode):
- Extension = `reaper_x32mirror.{dll,dylib,so}` exporting `ReaperPluginEntry(HINSTANCE, reaper_plugin_info_t*)`; `rec==NULL` → unload. Runtime API import: `REAPERAPI_LoadAPI(rec->GetFunc)` with `REAPERAPI_MINIMAL` + `REAPERAPI_WANT_*` (returns 0 on success).
- **No official per-mixer-strip controls for extensions.** Only supported in-strip UI: **embedded FX** (`sdk/reaper_plugin_fx_embed.h`) via VST2 dispatcher `effVendorSpecific`/`effEditDraw` handling `REAPER_FXEMBED_WM_*` (IS_SUPPORTED, PAINT via `REAPER_FXEMBED_IBitmap*`+`DrawInfo*`, GETMINMAXINFO, mouse; return `REAPER_FXEMBED_RETNOTIFY_INVALIDATE` to redraw). Mixer-window subclassing was ruled out (unsupported/fragile).
- Context menu: `plugin_register("hookcustommenu", hook)`, `void hook(const char* menustr, HMENU, int flag)` (flag 0=build, 1=about-to-show); context string `"Track control panel context"` (SWS-confirmed; log unknown menustr at runtime to verify).
- Dock: modeless SWELL `CreateDialog` + `DockWindowAddEx(hwnd, "X32 Mirror", "x32mirror", true)`.
- Actions: `plugin_register("custom_action", ...)` (stable `_X32MIRROR_*` ids) + `"hookcommand2"` + `"toggleaction"` (return 1/0/-1) for lit toolbar buttons.
- Track API (main thread ONLY): `CSurf_OnVolumeChangeEx(tr, vol, false, false)`, `CSurf_OnMuteChangeEx(tr, mute, false)`; `D_VOL` linear gain `= 10^(dB/20)`, 1.0 = 0 dB. GUIDs: `GetTrackGUID` + `guidToString`/`stringToGuid`; validate cached `MediaTrack*` with `ValidatePtr2`. Persistence: `SetProjExtState`/`GetProjExtState`/`EnumProjExtState`. Main-thread pump: `plugin_register("timer")` (~30 Hz).

**X32 OSC** (Maillot unofficial protocol doc, corroborated):
- UDP **10023**; console replies to the **source IP:port** → one socket for send+recv. OSC 1.0, **no bundles**, big-endian, 4-byte padded.
- Live updates: `/xremote` (no args), re-send every **5 s** (10 s timeout, silent lapse; max 4 clients — X32-Edit etc. compete). Event-driven pushes for all parameter changes; fader moves stream at ~20–50 ms. Meters not included (not needed).
- Addresses — fader `,f` 0.0–1.0; on `,i` **1 = ON/unmuted, 0 = muted (inverted vs REAPER mute)**:
  `/ch/NN/mix/fader|on` (NN=01..32, zero-padded), `/bus/NN/mix/fader|on` (01..16), `/dca/N/fader|on` (1..8, **single digit, no `/mix`**).
- Fader float→dB (verified piecewise): `f≥0.5: 40f−30`; `f≥0.25: 80f−50`; `f≥0.0625: 160f−70`; else `480f−90`. `f=0.75`→0 dB unity; `f=0`→−∞ → `D_VOL 0.0`.
- Initial sync: send address with no args → console replies current value (pace ~2 ms apart); re-poll on reconnect. Discovery: `/xinfo` to broadcast:10023 (`SO_BROADCAST`) → `,ssss` (IP, name, model, firmware).
- Gotchas: scene load = burst (SO_RCVBUF 256 KB, drain-until-EWOULDBLOCK); UDP lossy (optional periodic re-poll, ini default 30 s); epsilon-compare fader values.

## Architecture

Three deliverables from one repo:

1. **`reaper_x32mirror`** (extension): socket thread + OSC codec + X32 connection state machine, binding store, mirror engine, dockable panel, context menu, actions, settings, persistence. Exposes a versioned C vtable to the companion via `plugin_register("API_X32Mirror_GetInterface", ...)`.
2. **`x32mirror_embed`** (VST2 companion, clean-room minimal VST2 header — no Steinberg SDK): audio passthrough, 0 params, `canDo("hasCockosEmbeddedUI")→0xbeef0000`; paints an in-strip button (strip label, enabled color, M/F indicator dots, red outline when connection down), left-click toggles binding enable, right-click popup for mute/fader flags + open panel + remove. Resolves the extension vtable via `audioMaster(effect, 0xdeadbeef, 0xdeadf00d, 0, "X32Mirror_GetInterface", 0.0)`; gets its track via `audioMaster(effect, 0xdeadbeef, 0xdeadf00e, 1, NULL, 0.0f)` (re-query lazily each paint/click). Draws inert "X32 (no ext)" if the extension is absent; version-checks the vtable.
3. **`tools/x32sim/x32sim.py`** (Python 3, stdlib): X32 simulator for hardware-free development — answers `/info`, `/xinfo` (incl. broadcast), no-arg queries, `/node`; honors `/xremote` (10 s expiry, max 4 clients); interactive/scripted stimuli (`set`, `mute`, `sweep`, `burst`), `--loss`, `--latency`; **exits nonzero if it ever receives a value-carrying set message** — automating the one-way guarantee.

### Threading / data flow

```
X32 (or x32sim.py)  ── UDP 10023 ──►
┌─ Socket thread (X32Connection) ─────────────────────────────┐
│ select(250ms) → drain recvfrom → OscDecode → StripEvent     │
│  → StateCache (mutex) + CoalescingQueue (latest-wins/key)   │
│ timers: /xremote 5s · sync pacing · reconnect backoff       │
│ in: CommandQueue(connect/disconnect/resync/discover)        │
│ out: ControlQueue(conn state, discovery results)            │
└─────────────────────────────────────────────────────────────┘
                    │ mutex queues
┌─ Main thread ───────────────────────────────────────────────┐
│ "timer" @~30Hz: project-switch check → BindingStore reload; │
│  ControlQueue → panel status; CoalescingQueue.drain →       │
│  MirrorEngine: StripId → bindings (reverse index) →         │
│  GUID→track (ValidatePtr2) → epsilon gate →                 │
│  CSurf_OnVolumeChangeEx / CSurf_OnMuteChangeEx              │
│ panel WndProc · actions · menu hook · FX-embed paint/mouse  │
└─────────────────────────────────────────────────────────────┘
```
Rule: every REAPER API call on the main thread; socket thread touches only socket, codec, StateCache, queues. CoalescingQueue keyed `(StripId, Param)` → latest value, so bursts/stalls collapse to one apply per param.

Connection state machine: `DISCONNECTED → CONNECTING (/info, 2 s timeout) → SYNCING (paced no-arg queries) → LIVE (/xremote 5 s; optional re-poll) → LOST (silence + 3 failed /info probes) → backoff reconnect 1,2,4..15 s`. On (re)entering LIVE and on binding create/enable: seed an immediate apply from StateCache.

## File layout

```
CMakeLists.txt, CMakePresets.json          # win-x64 MSVC, macOS universal, linux-x64
cmake/Dependencies.cmake                   # FetchContent: reaper-sdk + WDL, pinned commits
src/common/                                # shared, no REAPER deps
  strip_id.h                               # StripType enum {CH,BUS,DCA,...}, index, counts, labels
  mirror_api.h                             # versioned C vtable: GetStateSerial, GetBindingView,
                                           #  ToggleBindingEnabled, SetMirrorFlag, RemoveBinding,
                                           #  OpenPanelForTrack, GetStripValue
  binding_state.h                          # POD BindingView for UI consumers
src/ext/                                   # target reaper_x32mirror (MODULE)
  main.cpp                                 # entry, register/unregister everything, Shutdown()
  reaper_api.h/.cpp                        # REAPERAPI_MINIMAL + WANT list, REAPERAPI_IMPLEMENT
  osc.h/.cpp                               # hand-rolled OSC 1.0 codec (~150 lines, no bundles)
  udp_socket.h/.cpp                        # winsock2/BSD RAII wrapper, broadcast, 256KB rcvbuf
  x32_addresses.h/.cpp                     # table-driven StripId↔address + Maillot fader math
  event_queue.h / state_cache.h/.cpp       # CoalescingQueue; last-known strip values
  x32_connection.h/.cpp                    # socket thread + state machine (one-way guarantee lives here)
  binding_store.h/.cpp                     # GUID-keyed bindings, proj-ext-state write-through,
                                           #  value format "v1|CH|7|1|1|1", reverse StripId index
  mirror_engine.h/.cpp                     # timer drain + CSurf apply + epsilon (0.05 dB)
  settings.h/.cpp                          # global ini at GetResourcePath()/reaper-x32mirror.ini
  actions.h/.cpp                           # all _X32MIRROR_* actions + toggleaction states
  menus.h/.cpp                             # "Track control panel context" submenu
  panel.h/.cpp + panel_res.rc              # dockable panel (swell-dlggen for mac/linux)
  api_export.h/.cpp                        # builds vtable, plugin_register("API_X32Mirror_GetInterface")
  log.h/.cpp                               # ShowConsoleMsg logging, level from ini
src/embed/                                 # target x32mirror_embed (MODULE, VST2)
  vst2_min.h                               # clean-room minimal VST2 ABI (vestige-style)
  embed_main.cpp                           # VSTPluginMain + fx_embed dispatcher
  extension_link.h/.cpp                    # 0xdeadf00d API lookup + 0xdeadf00e track context
  embed_ui.h/.cpp                          # IBitmap painting + mouse + right-click popup
tools/x32sim/x32sim.py (+README)           # simulator (see above)
tests/                                     # ctest: test_osc, test_fader_math, test_addresses,
                                           #  integration_x32conn (headless, vs x32sim loopback)
.github/workflows/build.yml                # 3-OS matrix: build + unit tests + sim integration
```

CMake targets: `x32common` (OBJECT lib) → linked by both MODULEs; the embed target must NOT link the REAPER API import layer (runtime pointers only).

## UI specifics

**Panel** (top→bottom): connection row (IP, port default 10023, Connect/Disconnect, **Scan** popup from `/xinfo` broadcast, status text); master row (master checkbox + Enable All/Disable All master actions + "force all bindings on/off" buttons); bindings listview (Track [red "missing" if GUID unresolved], Strip, En, Mute, Fader checkbox columns; Del removes); add row ("Bind selected track(s) →" + strip type/number combos, auto-increment across multi-selection). Repaint at 4 Hz sub-tick + dirty flag.

**Context menu** (`"Track control panel context"`): submenu "X32 Mirror" → Bind/edit binding… (opens panel, preselects track), ✓ Mirroring enabled, ✓ Mirror mute, ✓ Mirror fader, Remove binding — checkmarks refreshed at flag==1.

**Actions** (all with toggle state where meaningful): `_X32MIRROR_MASTER_TOGGLE`, `_X32MIRROR_ENABLE_ALL` / `_X32MIRROR_DISABLE_ALL` (master semantics), `_X32MIRROR_FORCE_ALL_ON` / `_X32MIRROR_FORCE_ALL_OFF` (rewrite per-binding flags), `_X32MIRROR_SHOW_PANEL`, `_X32MIRROR_TRACK_TOGGLE_ENABLE|MUTE|FADER` (selected tracks), `_X32MIRROR_CONNECT_TOGGLE`, `_X32MIRROR_INSERT_EMBED_ON_BOUND` (TrackFX_AddByName on bound tracks + attempt `TrackFX_SetNamedConfigParm(.., "fx_embed", "1")`).

## Edge cases (decided)

- Track deleted/reordered: GUID-only keys; unresolved bindings kept, shown "missing"; their events dropped silently.
- Project tab switch: detect via `EnumProjects(-1)` each tick → reload store, rebuild caches, seed from StateCache. Connection is global, survives tabs.
- Multiple tracks bound to one strip: allowed (reverse-index fan-out).
- Disconnect: LOST → status in panel, capped-backoff reconnect, full resync on recovery.
- Unload/exit: single `Shutdown()` — signal+join socket thread (≤500 ms), close socket, unregister all hooks (`-timer`, `-hookcustommenu`, `-hookcommand2`, `-toggleaction`, `-API_...`), `DockWindowRemove`, destroy dialog, write ini.
- Feedback loops: impossible by construction — no code path emits value-carrying OSC.

## Milestones (each testable)

- **M0** Skeleton: CMake+presets+FetchContent; empty extension loads/logs/unloads cleanly on all 3 OSes.
- **M1** Protocol core (no REAPER coupling): osc, udp_socket, x32_addresses, fader math, unit tests; x32sim.py; headless integration binary shows LIVE→LOST→LIVE against the sim.
- **M2** Mirror engine with one hardcoded binding (track 1 ↔ ch/01): sim fader/mute moves track 1; `f=0.75 → D_VOL 1.0`; responsive under `burst`.
- **M3** BindingStore + persistence: bind/save/reopen resumes; delete-track safe; two tracks on one strip; master + force-all semantics.
- **M4** Panel + settings + actions + menus: full manual checklist vs sim; toolbar toggle lights; dock position persists.
- **M5** Embedded FX companion: verify the two audioMaster mechanisms early (see flags), paint/mouse UI, insert-helper action; inert without extension.
- **M6** Hardening: burst+loss soak, overnight reconnect soak, autoconnect, re-poll option, README, CI artifacts, ext-state format versioned.

## Verification

1. `ctest` unit tests: OSC codec round-trip/fuzz, Maillot curve fixtures (0.75→0 dB, boundaries, 0→−∞), address mapping incl. DCA single-digit/no-`/mix` quirk.
2. Headless `integration_x32conn` vs `x32sim.py` on loopback (CI on Linux): sync, live events, reconnect; sim exits nonzero if the plugin ever *sets* a console value (one-way guarantee as an automated check).
3. Manual in-REAPER checklist per milestone against the sim (`sweep`, `burst`, `--loss 5`).
4. Optional final pass against a real console / X32-Edit — same protocol.

## Flagged for verification during implementation (fallbacks designed)

1. `0xdeadf00e` audioMaster context selector for `MediaTrack*` (mechanism confirmed in iPlug2/ReaShader/ninjam; confirm selector value `1` from vendored SDK headers).
2. `0xdeadf00d` audioMaster lookup resolving an **extension-registered** `API_*` name from a VST. Fallback: extension publishes the vtable pointer via `SetExtState("x32mirror","iface_ptr",hex,false)`; VST reads it via `GetExtState`.
3. Exact `hookcustommenu` menustr for the track/mixer strip right-click (`"Track control panel context"` expected) — log unknown menustr once at runtime.
4. `TrackFX_SetNamedConfigParm(.., "fx_embed", "1")` key for auto-showing embedded UI in MCP; if absent, document the manual "Show embedded UI in MCP" step.

## Delivery

Develop on branch `claude/reaper-x32-osc-plugin-a7n4pd`; commit per milestone with descriptive messages; push with `git push -u origin claude/reaper-x32-osc-plugin-a7n4pd`. No PR unless requested.
