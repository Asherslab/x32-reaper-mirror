# X32 → REAPER Mirror

A native REAPER extension (plus an optional in-strip companion plug-in) that
**mirrors** a Behringer X32 console's per-channel **mute** and **fader** onto
REAPER tracks, **strictly one-way** (X32 → REAPER).

The plugin never sends control data to the console. Its only outbound traffic
is no-argument OSC queries, `/xremote` keepalives, `/info` probes and `/xinfo`
discovery — there is no code path that transmits a parameter value. This is
enforced structurally in `X32Connection` and verified automatically by the
simulator (see [one-way guarantee](#one-way-guarantee)).

## Features

- Mirror **mute** and **fader** from `/ch/01–32`, `/bus/01–16`, `/dca/1–8`
  (the address table is data-driven, so `/auxin`, `/fxrtn`, `/mtx`, `/main`
  are one-row additions later).
- Manual per-track mapping, keyed by **track GUID** (survives reordering and
  rename). Multiple tracks may mirror one strip.
- Mute and fader are individually toggleable per binding; each binding is
  individually enable/disable-able.
- A global **master switch** (`effective = master && binding.enabled && flag`)
  with lit toggle actions, **plus** separate "force all on/off" actions and
  panel buttons.
- Reachable from a **dockable panel**, the **track right-click menu**, and an
  optional **in-strip button** (embedded-FX companion).
- Connection via manual IP/port or **`/xinfo` broadcast discovery**.
- Control-surface behavior for applies (`CSurf_OnVolumeChangeEx` /
  `CSurf_OnMuteChangeEx`): writes automation in write/touch modes, plain moves
  otherwise, and never creates undo points.

## Building

Requires CMake ≥ 3.19 and a C++17 compiler. The REAPER SDK and WDL/SWELL are
fetched automatically (pinned commits — see `cmake/Dependencies.cmake`).

```sh
# Full build (extension + companion). Fetches reaper-sdk + WDL.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Protocol core + tests only, no SDK download:
cmake -S . -B build -DX32MIRROR_BUILD_REAPER=OFF
cmake --build build && ctest --test-dir build
```

Presets are provided for `linux-x64`, `macos-universal`, `win-x64` and
`core-only` (see `CMakePresets.json`).

Outputs:

| Artifact | Install location |
|---|---|
| `reaper_x32mirror.{so,dylib,dll}` | REAPER `UserPlugins/` |
| `x32mirror_embed.{so,dylib,dll}` *(optional)* | any VST folder REAPER scans |

The companion is optional — the extension is fully usable via the dockable
panel and the track context menu without it.

## Usage

1. Copy `reaper_x32mirror.*` into REAPER's `UserPlugins` folder and restart
   REAPER.
2. Open the panel: action **"X32 Mirror: Show/hide panel"** (or dock it).
3. Enter the console IP (or **Scan** for it) and click **Connect**.
4. Select one or more tracks, pick a strip type + starting number, and click
   **Bind** (indices auto-increment across a multi-track selection). You can
   also bind/edit from a track's right-click menu.
5. Move faders / mutes on the console — the bound REAPER tracks follow.

For the in-strip button, run the action **"Insert in-strip button on bound
tracks"**, or add the `x32mirror_embed` FX manually and enable *Show embedded
UI in MCP*. Left-click the button toggles that binding; right-click opens the
panel focused on the track.

## One-way guarantee

The console is never written to. `X32Connection::SendQuery*` is the only
transmit primitive; it encodes messages with `OscEncodeQuery` (no value
arguments) and restricts addresses to strip queries plus `/xremote`, `/info`
and `/xinfo`. `tools/x32sim/x32sim.py` **exits non-zero** if it ever receives a
value-carrying set message, and the `integration_x32conn` ctest drives the real
connection layer against it — so any regression that made the plugin *set* a
console value would fail CI.

## Testing

- `ctest`: OSC codec round-trip/fuzz, Maillot fader-curve fixtures
  (`0.75 → 0 dB`, segment boundaries, `0 → −∞`), address round-trip including
  the DCA single-digit/no-`/mix` quirk, binding serialization.
- `integration_x32conn`: headless, drives `SYNC → LIVE`, a live push, and a
  `LIVE → LOST → LIVE` reconnect against `x32sim.py` on loopback.
- CI builds all three platforms and runs the above (`.github/workflows`).

## Architecture

```
X32 / x32sim ── UDP 10023 ──►
  Socket thread (X32Connection): decode → StateCache + CoalescingQueue
    timers: /xremote 5 s · sync pacing · reconnect backoff        (never sets)
        │ mutex queues
  Main thread (~30 Hz timer): project-switch check → BindingStore reload;
    control events → panel status; CoalescingQueue drain → MirrorEngine:
      StripId → bindings (reverse index) → GUID→track (ValidatePtr2) →
      epsilon gate → CSurf_OnVolumeChangeEx / CSurf_OnMuteChangeEx
```

- Every REAPER API call happens on the main thread; the socket thread touches
  only the socket, codec, `StateCache` and the queues.
- The `CoalescingQueue` keeps only the latest value per `(strip, param)`, so a
  scene-load burst collapses to one apply per parameter.
- Bindings persist per-project in `SetProjExtState` (section `X32MIRROR`,
  value `v1|CH|7|1|1|1`); connection settings live in a global ini at
  `<resource path>/reaper-x32mirror.ini`.

See `PLAN.md` for the full design, decisions and milestone breakdown.

## Layout

```
src/common/   shared, dependency-free (strip ids, versioned companion vtable)
src/ext/      the extension: OSC/UDP/connection, engine, store, panel, actions
src/embed/    the VST2 companion: clean-room VST2 ABI, fx-embed UI
tools/x32sim/ the simulator + docs
tests/        ctest unit + integration
```
