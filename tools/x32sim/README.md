# x32sim — Behringer X32 OSC simulator

A dependency-free (Python 3 stdlib) simulator of the small OSC surface the
**X32 → REAPER Mirror** plugin uses, so you can develop and test with no
console on the bench.

## Run

```sh
python3 x32sim.py                 # listen on 0.0.0.0:10023 (X32 default), REPL
python3 x32sim.py --port 10099    # different port
python3 x32sim.py --listen 127.0.0.1
python3 x32sim.py --script my.script          # run stimuli then exit
python3 x32sim.py --loss 0.05 --latency 20    # 5% loss, +20 ms out latency
```

## What it answers

- **no-argument strip queries** (`/ch/NN/mix/fader`, `/bus/NN/mix/on`,
  `/dca/N/fader`, …) → replies to the sender with the current value
- **`/info`** → `,ssss` server/model/firmware
- **`/xinfo`** (including broadcast) → `,ssss` (ip, name, model, firmware)
- **`/node "path"`** → node-dump style reply
- **`/xremote`** → registers the sender for live pushes (10 s expiry, max 4
  clients, exactly like the console)

Every value change is pushed to all live `/xremote` clients.

## The one-way guarantee, enforced

If the simulator ever receives a **value-carrying** message aimed at a
parameter (an address with arguments that is not a recognised query), it prints
a `VIOLATION` line and **exits non-zero**. The plugin is designed so this can
never happen — the only transmit path in `X32Connection` emits no-argument OSC
messages. Wiring the simulator into CI (see `integration_x32conn` and the
`build` workflow) turns "the plugin is strictly one-way" into an automated
test.

## Stimuli (REPL or script)

```
set   <addr|strip> <value>      set a fader (0..1) or /on (0/1) and push
mute  <addr|strip> <0|1>        set the /on flag (1 = ON / unmuted)
sweep <addr|strip> <a> <b> <s>  ramp a fader from a to b over s seconds
burst                           re-push every current value at once (scene load)
clients                         list live /xremote clients
sleep <s>                       pause (scripts)
quit                            stop
```

Strip shorthand: `ch01`, `bus3`, `dca2` expand to the right addresses
(`set ch01 0.75` → `/ch/01/mix/fader`; `mute ch01 0` → `/ch/01/mix/on`).

Remember the X32 mute convention: `/on = 1` means **unmuted**, `0` means
**muted** — inverted from REAPER's mute.
