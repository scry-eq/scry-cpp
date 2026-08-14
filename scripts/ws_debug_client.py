#!/usr/bin/env python3
"""
ws_debug_client.py — observe the daemon's WebSocket reply stream, focused on
how a character surfaces to the web across a zone change.

It connects as a passive read-only client (the daemon supports many), subscribes
to SPAWNS+ZONE+PLAYER, decodes every Envelope with decode_pbstream's schema-driven
protobuf decoder, and prints a compact timeline that highlights exactly the events
that decide whether the web can show your character after zoning:

  * SNAPSHOT        — carries player_id + the full spawn list (the web latches
                      player_id from here to know which spawn is "you")
  * player_id flip  — 0 at zone-in clear, then the new self-id once re-adopted
  * ZONE_CHANGED    — the daemon telling the web the zone rolled
  * self spawn      — a spawn_added/removed whose id == the current player_id
  * BOX_LIST        — the picker (box_id / name / zone / level / active)

Chatty spawn_added/spawn_updated bursts are aggregated into counts so the zone
transition stands out.

Two ways to drive it:

  # A) spawn a private daemon on :9091 from a capture (never touches :9090)
  scripts/ws_debug_client.py --replay tests/replay/eql/eql-stance.vpk

  # B) attach to an already-running daemon (e.g. one you launched yourself)
  scripts/ws_debug_client.py --url ws://127.0.0.1:9091/

Add --raw to also dump the full protoc-like text of each highlighted envelope.
Requires the `websocket-client` package (import websocket).
"""

import argparse
import os
import signal
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from decode_pbstream import parse_proto, decode_message, render_lines  # noqa: E402

try:
    import websocket  # websocket-client
except ImportError:
    sys.exit("need the 'websocket-client' package: pip install websocket-client")

from pathlib import Path

SCHEMA = parse_proto(sorted((Path(REPO) / "proto" / "seq" / "v1").glob("*.proto")))
# Payload field names of the Envelope oneof (number >= 10) — everything else at
# the top level is bookkeeping (seq, server_ts_ms).
PAYLOAD_NAMES = [f.name for f in SCHEMA.messages["Envelope"] if f.number >= 10]

TOPICS = {"SPAWNS": 1, "ZONE": 2, "PLAYER": 3, "CHAT": 4,
          "COMBAT": 5, "GROUP": 6, "EXP": 7}


def encode_subscribe(topics):
    """Hand-encode ClientEnvelope{subscribe:{topics:[...]}} (all field-1 varints,
    unpacked — proto3 parsers accept that for a packed repeated enum)."""
    sub = b"".join(bytes([0x08, t]) for t in topics)          # Subscribe.topics
    return bytes([0x0A, len(sub)]) + sub                       # ClientEnvelope.subscribe


def payload_of(env):
    """(kind_name, submessage_dict) for the oneof payload present, or (None, {})."""
    for name in PAYLOAD_NAMES:
        if name in env:
            return name, env[name]
    return None, {}


def wait_for_port(host, port, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


class Timeline:
    def __init__(self, raw=False):
        self.raw = raw
        self.t0 = None
        self.player_id = None          # last player_id the web would have latched
        self.zone = None
        self.known_self_present = False
        # aggregation buckets so a spawn burst is one line, not hundreds
        self.pending_added = 0
        self.pending_updated = 0
        self.pending_removed = 0
        self.counts = {}

    # -- aggregation ----------------------------------------------------------
    def _flush(self):
        parts = []
        if self.pending_added:
            parts.append(f"+{self.pending_added} added")
        if self.pending_updated:
            parts.append(f"~{self.pending_updated} updated")
        if self.pending_removed:
            parts.append(f"-{self.pending_removed} removed")
        if parts:
            self._line("  · spawns: " + ", ".join(parts), stamp=False)
        self.pending_added = self.pending_updated = self.pending_removed = 0

    def _line(self, msg, stamp=True):
        if stamp and self.t0 is not None:
            print(f"[t+{time.time() - self.t0:7.3f}s] {msg}")
        else:
            print(f"{'':>12} {msg}")

    # -- per-envelope ---------------------------------------------------------
    def feed(self, env):
        if self.t0 is None:
            self.t0 = time.time()
        kind, m = payload_of(env)
        self.counts[kind] = self.counts.get(kind, 0) + 1

        if kind == "spawn_added":
            sp = m.get("spawn", {}) or {}
            sid = sp.get("id")
            if self.player_id and sid == self.player_id:
                self._flush()
                self._line(f"  ★ SELF spawn_added  id={sid} name={sp.get('name','?')!r} "
                           f"(matches player_id) — the web can now mark 'you'")
                self.known_self_present = True
            else:
                self.pending_added += 1
            return
        if kind == "spawn_updated":
            self.pending_updated += 1
            return
        if kind == "spawn_removed":
            sid = m.get("id")
            if self.player_id and sid == self.player_id:
                self._flush()
                self._line(f"  ✗ SELF spawn_removed id={sid} — 'you' left the spawn list")
                self.known_self_present = False
            else:
                self.pending_removed += 1
            return

        # anything else is a landmark event -> flush the burst first
        self._flush()

        if kind == "snapshot":
            pid = m.get("player_id", 0) or 0
            zone = m.get("zone_short", "") or ""
            spawns = m.get("spawns", []) or []
            self_in = any((s or {}).get("id") == pid for s in spawns) if pid else False
            flip = ""
            if pid != (self.player_id or 0):
                flip = f"  <<< player_id {self.player_id} -> {pid}"
            self.player_id = pid or None
            self.zone = zone or self.zone
            self.known_self_present = self_in
            self._line(f"SNAPSHOT zone={zone!r} player_id={pid} spawns={len(spawns)} "
                       f"self_in_snapshot={self_in}{flip}")
            if pid and not self_in:
                self._line("  ⚠ snapshot has a player_id but NO matching spawn — "
                           "web shows no 'you' until a spawn with that id arrives", stamp=False)

        elif kind == "zone_changed":
            self.zone = m.get("zone_short") or self.zone
            self._line(f"ZONE_CHANGED zone_short={m.get('zone_short','')!r} "
                       f"zone_long={m.get('zone_long','')!r} "
                       f"geometry={'yes' if 'geometry' in m else 'no'}")

        elif kind == "box_list_updated":
            boxes = m.get("boxes", []) or []
            active = m.get("active_box_id", "")
            self._line(f"BOX_LIST active={active!r} boxes={len(boxes)}")
            for b in boxes:
                b = b or {}
                mark = "*" if b.get("box_id") == active else " "
                self._line(f"   {mark} box_id={b.get('box_id','')!r} "
                           f"name={b.get('display_name','')!r} zone={b.get('zone','')!r} "
                           f"lvl={b.get('level',0)} pkts={b.get('packet_count',0)}", stamp=False)

        elif kind == "player_stats":
            self._line("PLAYER_STATS (name/level/hp refresh)")

        else:
            self._line(f"{kind}")

        if self.raw:
            for ln in render_lines(SCHEMA, "Envelope", env):
                print("        | " + ln)

    def summary(self):
        self._flush()
        print("\n" + "=" * 68)
        print("SUMMARY")
        print(f"  final player_id      : {self.player_id}")
        print(f"  final zone           : {self.zone!r}")
        print(f"  self spawn present   : {self.known_self_present}")
        print("  envelope kind tally  :")
        for k in sorted(self.counts, key=lambda k: -self.counts[k]):
            print(f"      {self.counts[k]:6d}  {k}")


# Periodic envelopes that trickle in forever (SpellShell's 6s buff-duration
# timer) — don't let them reset the idle clock, or the client never settles.
NON_RESET = {"buffs"}


def run(url, topics, raw, idle_timeout, max_seconds):
    print(f"connecting to {url}  (topics: {', '.join(topics)})")
    ws = websocket.create_connection(url, timeout=min(idle_timeout, 2.0))
    ws.send(encode_subscribe([TOPICS[t] for t in topics]),
            websocket.ABNF.OPCODE_BINARY)
    print("subscribed — waiting for envelopes (Ctrl-C to stop)\n")
    tl = Timeline(raw=raw)
    start = time.time()
    last_interesting = start
    try:
        while True:
            now = time.time()
            if now - start > max_seconds:
                print(f"\n(max {max_seconds:.0f}s reached — stopping)")
                break
            if now - last_interesting > idle_timeout:
                print(f"\n(idle {idle_timeout:.0f}s of non-periodic traffic — replay settled)")
                break
            try:
                op, data = ws.recv_data(control_frame=False)
            except websocket.WebSocketTimeoutException:
                continue                      # loop to re-check the wall clock
            except (websocket.WebSocketConnectionClosedException, OSError):
                print("\n(connection closed by daemon)")
                break
            if op == websocket.ABNF.OPCODE_BINARY and data:
                env = decode_message(bytes(data), 0, len(data), "Envelope", SCHEMA)
                kind, _ = payload_of(env)
                if kind not in NON_RESET:
                    last_interesting = now
                tl.feed(env)
    except KeyboardInterrupt:
        print("\n(interrupted)")
    finally:
        try:
            ws.close()
        except Exception:
            pass
        tl.summary()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--url", help="attach to an already-running daemon, e.g. ws://127.0.0.1:9091/")
    src.add_argument("--replay", metavar="VPK",
                     help="spawn a private daemon on :9091 replaying VPK (waits for this client)")
    ap.add_argument("--port", type=int, default=9091,
                    help="port for --replay's private daemon (default 9091; never 9090)")
    ap.add_argument("--topics", default="SPAWNS,ZONE,PLAYER",
                    help="comma list from " + ",".join(TOPICS))
    ap.add_argument("--raw", action="store_true",
                    help="also dump full protoc-like text of each landmark envelope")
    ap.add_argument("--idle-timeout", type=float, default=6.0,
                    help="stop after this many seconds with no non-periodic envelope (default 6)")
    ap.add_argument("--max-seconds", type=float, default=45.0,
                    help="hard cap on total run time (default 45; the replay's own "
                         "buffs timer means it never goes fully idle)")
    ap.add_argument("--daemon", default=os.path.join(REPO, "build", "scryd"))
    ap.add_argument("--config-dir", default=os.path.join(REPO, "conf"))
    args = ap.parse_args()

    topics = [t.strip().upper() for t in args.topics.split(",") if t.strip()]
    bad = [t for t in topics if t not in TOPICS]
    if bad:
        ap.error(f"unknown topics: {bad}")

    if args.url:
        run(args.url, topics, args.raw, args.idle_timeout, args.max_seconds)
        return

    # --replay: launch a private daemon that pauses until we subscribe.
    if args.port == 9090:
        ap.error("refusing port 9090 (the user's stack); pick another")
    cmd = [args.daemon, "--replay", args.replay, "--config-dir", args.config_dir,
           "--listen", f"127.0.0.1:{args.port}", "--wait-for-client"]
    print("launching: " + " ".join(cmd))
    import tempfile
    log_path = os.path.join(tempfile.gettempdir(), "ws_debug_daemon.log")
    log = open(log_path, "w")
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT,
                            preexec_fn=os.setsid)

    def teardown(*_):
        if proc.poll() is None:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
    # Kill the private daemon even if WE get SIGTERM/SIGINT (e.g. wrapped in
    # `timeout`), so it can never orphan onto :9091.
    for s in (signal.SIGTERM, signal.SIGINT):
        signal.signal(s, lambda *a: (teardown(), sys.exit(1)))
    try:
        if not wait_for_port("127.0.0.1", args.port):
            sys.exit("daemon never opened the port — see ws_debug_daemon.log")
        run(f"ws://127.0.0.1:{args.port}/", topics, args.raw, args.idle_timeout,
            args.max_seconds)
    finally:
        teardown()
        log.close()
        print(f"(daemon stderr in {log_path})")


if __name__ == "__main__":
    main()
