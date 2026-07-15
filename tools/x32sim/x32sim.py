#!/usr/bin/env python3
"""x32sim — a minimal Behringer X32 OSC simulator for hardware-free development.

It answers the small OSC subset the X32→REAPER Mirror plugin relies on:

  * no-argument queries of strip parameters  -> replies with the current value
  * ``/info``                                -> ``,ssss`` server/version reply
  * ``/xinfo`` (incl. broadcast)             -> ``,ssss`` (ip, name, model, fw)
  * ``/node`` with a string arg              -> node-dump style reply
  * ``/xremote`` (no args)                   -> registers the sender for live
                                                pushes (10 s expiry, max 4
                                                clients, matching the console)

Every parameter change is pushed to all live ``/xremote`` clients, exactly as
the console does. Meters are intentionally not emitted (the plugin never uses
them).

**One-way guarantee enforcement.** The real plugin must NEVER set a value on
the console. If this simulator ever receives a *value-carrying* message aimed
at a parameter (i.e. an address with arguments that is not a recognised
query), it prints a VIOLATION line and exits with a non-zero status. Wiring
this simulator into CI therefore turns "the plugin is strictly one-way" into an
automated check.

Stimuli (interactive on stdin, or scripted via --script) let you drive the
console side:

  set   <addr|strip> <value>      set a fader (0..1) or on (0/1) and push
  mute  <addr|strip> <0|1>        set the /on flag (1 = ON/unmuted)
  sweep <addr|strip> <a> <b> <s>  ramp a fader from a to b over s seconds
  burst                           re-push every current value at once
  clients                         list live /xremote clients
  quit                            stop the simulator

Strip shorthand: ``ch01``, ``bus3``, ``dca2`` expand to their fader/on
addresses (``set ch01 0.75`` sets ``/ch/01/mix/fader``; ``mute ch01 0`` sets
``/ch/01/mix/on``).
"""

import argparse
import os
import random
import re
import select
import socket
import struct
import sys
import threading
import time

# --------------------------------------------------------------------------
# OSC 1.0 codec (single messages, big-endian, 4-byte padded; no bundles).
# --------------------------------------------------------------------------


def _pad4(n: int) -> int:
    return (n + 3) & ~3


def osc_encode(address: str, args=None) -> bytes:
    args = args or []
    out = bytearray()
    a = address.encode("ascii")
    out += a
    out += b"\x00"
    while len(out) % 4:
        out += b"\x00"
    tags = ","
    payload = bytearray()
    for arg in args:
        if isinstance(arg, int):
            tags += "i"
            payload += struct.pack(">i", arg)
        elif isinstance(arg, float):
            tags += "f"
            payload += struct.pack(">f", arg)
        elif isinstance(arg, str):
            tags += "s"
            s = arg.encode("utf-8") + b"\x00"
            while len(s) % 4:
                s += b"\x00"
            payload += s
        else:
            raise TypeError(f"unsupported OSC arg type: {type(arg)!r}")
    t = tags.encode("ascii") + b"\x00"
    while len(t) % 4:
        t += b"\x00"
    out += t
    out += payload
    return bytes(out)


def _read_string(buf: bytes, pos: int):
    end = buf.index(b"\x00", pos)
    s = buf[pos:end].decode("utf-8", "replace")
    return s, _pad4(end + 1)


def osc_decode(buf: bytes):
    """Return (address, [args]) or None if not a decodable single message."""
    if not buf or buf[:1] == b"#":  # bundle -> unsupported
        return None
    if buf[:1] != b"/":
        return None
    try:
        address, pos = _read_string(buf, 0)
        if pos >= len(buf):
            return address, []
        tags, pos = _read_string(buf, pos)
        if not tags.startswith(","):
            return address, []
        args = []
        for tag in tags[1:]:
            if tag == "i":
                (v,) = struct.unpack_from(">i", buf, pos)
                pos += 4
                args.append(v)
            elif tag == "f":
                (v,) = struct.unpack_from(">f", buf, pos)
                pos += 4
                args.append(float(v))
            elif tag in ("s", "S"):
                v, pos = _read_string(buf, pos)
                args.append(v)
            elif tag == "b":
                (blen,) = struct.unpack_from(">i", buf, pos)
                pos += 4
                args.append(buf[pos : pos + blen])
                pos = _pad4(pos + blen)
            elif tag in ("T", "F"):
                args.append(1 if tag == "T" else 0)
            else:
                # Unknown payload width; stop but keep what we have.
                break
        return address, args
    except Exception:
        return None


# --------------------------------------------------------------------------
# Address helpers
# --------------------------------------------------------------------------

STRIP_RE = re.compile(r"^(ch|bus|dca|auxin|fxrtn|mtx)(\d+)$", re.IGNORECASE)

# Which addresses are pure control/query verbs (no value ever attached).
CONTROL_ADDRS = {"/xremote", "/info", "/xinfo", "/status", "/renew", "/unsubscribe"}


def is_strip_address(addr: str) -> bool:
    parts = addr.strip("/").split("/")
    if len(parts) < 3:
        return False
    fam = parts[0]
    if fam not in ("ch", "bus", "dca", "auxin", "fxrtn", "mtx", "main"):
        return False
    return parts[-1] in ("fader", "on")


def expand_strip(token: str, param: str) -> str:
    """ch01 -> /ch/01/mix/<param>; dca2 -> /dca/2/<param> (no /mix)."""
    m = STRIP_RE.match(token)
    if not m:
        raise ValueError(f"bad strip token: {token}")
    fam = m.group(1).lower()
    idx = int(m.group(2))
    if fam == "dca":
        return f"/dca/{idx}/{param}"
    return f"/{fam}/{idx:02d}/mix/{param}"


def resolve_target(token: str, param_hint: str) -> str:
    """Accept a full address or a strip shorthand and return an address."""
    if token.startswith("/"):
        return token
    return expand_strip(token, param_hint)


# --------------------------------------------------------------------------
# Simulator
# --------------------------------------------------------------------------


class X32Sim:
    XREMOTE_TTL = 10.0  # seconds
    MAX_CLIENTS = 4

    def __init__(self, args):
        self.args = args
        self.state = {}          # address -> value (float for fader, int for on)
        self.clients = {}        # (ip, port) -> last_renew_monotonic
        self.lock = threading.Lock()
        self.running = True
        self.violation = False
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except OSError:
            pass
        self.sock.bind((args.listen, args.port))
        self._rng = random.Random(args.seed)
        self._seed_defaults()

    def _seed_defaults(self):
        for i in range(1, 33):
            self.state[f"/ch/{i:02d}/mix/fader"] = 0.75  # unity
            self.state[f"/ch/{i:02d}/mix/on"] = 1
        for i in range(1, 17):
            self.state[f"/bus/{i:02d}/mix/fader"] = 0.75
            self.state[f"/bus/{i:02d}/mix/on"] = 1
        for i in range(1, 9):
            self.state[f"/dca/{i}/fader"] = 0.75
            self.state[f"/dca/{i}/on"] = 1

    # --- logging ---------------------------------------------------------
    def log(self, *a):
        if not self.args.quiet:
            print("[sim]", *a, file=sys.stderr, flush=True)

    # --- transmit with optional loss/latency -----------------------------
    def _raw_send(self, data, addr):
        if self.args.loss > 0.0:
            if self._rng.random() < self.args.loss:
                return
        if self.args.latency > 0:
            threading.Timer(
                self.args.latency / 1000.0, self._do_send, args=(data, addr)
            ).start()
        else:
            self._do_send(data, addr)

    def _do_send(self, data, addr):
        try:
            self.sock.sendto(data, addr)
        except OSError:
            pass

    def reply(self, address, value, addr):
        args = [value] if isinstance(value, (int, float)) else [value]
        self._raw_send(osc_encode(address, [value]), addr)

    def push_all_clients(self, address, value):
        now = time.monotonic()
        with self.lock:
            dead = [c for c, t in self.clients.items() if now - t > self.XREMOTE_TTL]
            for c in dead:
                del self.clients[c]
            targets = list(self.clients.keys())
        for c in targets:
            self._raw_send(osc_encode(address, [value]), c)

    # --- state mutation (stimuli) ----------------------------------------
    def set_value(self, address, value):
        with self.lock:
            if address.endswith("/on"):
                value = 1 if int(round(float(value))) else 0
            else:
                value = float(value)
            self.state[address] = value
        self.log(f"set {address} = {value}")
        self.push_all_clients(address, value)

    def burst(self):
        with self.lock:
            items = list(self.state.items())
        self.log(f"burst: pushing {len(items)} values")
        for address, value in items:
            self.push_all_clients(address, value)

    def sweep(self, address, a, b, secs):
        steps = max(2, int(secs / 0.03))  # ~30 Hz
        for k in range(steps):
            if not self.running:
                break
            v = a + (b - a) * (k / (steps - 1))
            self.set_value(address, v)
            time.sleep(secs / steps)

    def list_clients(self):
        now = time.monotonic()
        with self.lock:
            for c, t in self.clients.items():
                print(f"  {c[0]}:{c[1]}  age={now - t:.1f}s")

    # --- receive ---------------------------------------------------------
    def register_xremote(self, addr):
        now = time.monotonic()
        with self.lock:
            dead = [c for c, t in self.clients.items() if now - t > self.XREMOTE_TTL]
            for c in dead:
                del self.clients[c]
            if addr not in self.clients and len(self.clients) >= self.MAX_CLIENTS:
                # Console tops out at 4 clients; drop the request.
                self.log(f"/xremote from {addr}: at client limit, ignoring")
                return
            self.clients[addr] = now
        self.log(f"/xremote client {addr[0]}:{addr[1]} registered")

    def handle_packet(self, data, addr):
        decoded = osc_decode(data)
        if decoded is None:
            return
        address, args = decoded

        # --- one-way guarantee enforcement ---
        # A value-carrying message aimed at a parameter is a SET attempt and
        # must never happen. /node with string args is a query (allowed).
        if args:
            if address in CONTROL_ADDRS:
                pass  # control verbs never legitimately carry values, but be lenient
            elif address == "/node" and all(isinstance(x, str) for x in args):
                pass  # node query
            else:
                self.violation = True
                print(
                    f"[sim] VIOLATION: received value-carrying message "
                    f"{address} args={args} from {addr[0]}:{addr[1]} — the "
                    f"plugin must be strictly one-way!",
                    file=sys.stderr,
                    flush=True,
                )
                self.running = False
                return

        if address == "/xremote":
            self.register_xremote(addr)
            return
        if address == "/info":
            # ,ssss : server-ip, server-name, console-model, fw-version
            self._raw_send(
                osc_encode(
                    "/info",
                    ["V2.07", self.args.name, self.args.model, self.args.fw],
                ),
                addr,
            )
            return
        if address == "/xinfo":
            local_ip = self._local_ip(addr)
            self._raw_send(
                osc_encode(
                    "/xinfo",
                    [local_ip, self.args.name, self.args.model, self.args.fw],
                ),
                addr,
            )
            return
        if address == "/node":
            # Reply with a node dump for the requested path(s).
            for path in args:
                p = "/" + str(path).lstrip("/")
                with self.lock:
                    val = self.state.get(p)
                if val is not None:
                    self._raw_send(osc_encode("node", [f"{p} {val}"]), addr)
            return

        # Strip (or other) no-arg query -> reply with current value.
        if is_strip_address(address):
            with self.lock:
                val = self.state.get(address)
            if val is None:
                return
            self.reply(address, val, addr)
            return

        # Unknown no-arg address: ignore quietly.

    def _local_ip(self, peer):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(peer)
            ip = s.getsockname()[0]
            s.close()
            return ip
        except OSError:
            return "127.0.0.1"

    # --- main loops ------------------------------------------------------
    def recv_loop(self):
        while self.running:
            r, _, _ = select.select([self.sock], [], [], 0.25)
            if not r:
                continue
            try:
                data, addr = self.sock.recvfrom(65536)
            except OSError:
                continue
            self.handle_packet(data, addr)

    def run(self):
        self.log(
            f"listening on {self.args.listen}:{self.args.port} "
            f"(model={self.args.model} fw={self.args.fw})"
        )
        t = threading.Thread(target=self.recv_loop, daemon=True)
        t.start()

        if self.args.script:
            self.run_script(self.args.script)
            if self.args.keep:
                self.repl()
            else:
                # Give late replies a moment to flush, then stop.
                time.sleep(self.args.linger)
                self.running = False
        else:
            self.repl()

        t.join(timeout=1.0)
        return 1 if self.violation else 0

    def run_script(self, path):
        with open(path, "r") as f:
            for raw in f:
                if not self.running:
                    break
                self.exec_command(raw)

    def repl(self):
        try:
            while self.running:
                r, _, _ = select.select([sys.stdin], [], [], 0.25)
                if not r:
                    continue
                line = sys.stdin.readline()
                if not line:
                    break
                self.exec_command(line)
        except (KeyboardInterrupt, EOFError):
            pass
        self.running = False

    def exec_command(self, raw):
        line = raw.strip()
        if not line or line.startswith("#"):
            return
        parts = line.split()
        cmd = parts[0].lower()
        try:
            if cmd == "set":
                addr = resolve_target(parts[1], "fader")
                self.set_value(addr, parts[2])
            elif cmd == "mute":
                addr = resolve_target(parts[1], "on")
                self.set_value(addr, parts[2])
            elif cmd == "sweep":
                addr = resolve_target(parts[1], "fader")
                self.sweep(addr, float(parts[2]), float(parts[3]), float(parts[4]))
            elif cmd == "burst":
                self.burst()
            elif cmd == "clients":
                self.list_clients()
            elif cmd in ("sleep", "wait"):
                time.sleep(float(parts[1]))
            elif cmd in ("quit", "exit", "stop"):
                self.running = False
            else:
                self.log(f"unknown command: {line}")
        except (IndexError, ValueError) as e:
            self.log(f"bad command '{line}': {e}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Behringer X32 OSC simulator")
    ap.add_argument("--listen", default="0.0.0.0", help="bind address")
    ap.add_argument("--port", type=int, default=10023, help="UDP port (default 10023)")
    ap.add_argument("--model", default="X32SIM")
    ap.add_argument("--name", default="x32sim")
    ap.add_argument("--fw", default="4.06")
    ap.add_argument("--loss", type=float, default=0.0,
                    help="outgoing packet loss fraction 0..1")
    ap.add_argument("--seed", type=int, default=None,
                    help="seed for --loss randomness (default: unseeded/random)")
    ap.add_argument("--latency", type=float, default=0.0,
                    help="added outgoing latency in ms")
    ap.add_argument("--script", help="run a stimulus script then exit")
    ap.add_argument("--keep", action="store_true",
                    help="with --script, keep running after the script")
    ap.add_argument("--linger", type=float, default=0.3,
                    help="seconds to flush replies before exit in script mode")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    sim = X32Sim(args)
    rc = sim.run()
    if rc:
        print("[sim] exiting non-zero due to one-way VIOLATION", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main())
