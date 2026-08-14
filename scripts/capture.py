#!/usr/bin/env python3
"""Record a tier-2 replay fixture: runs setcap then the daemon with
--record-vpk + --opcode-stats + --list-events + --no-listen, writing to
tests/replay/.

Usage:  scripts/capture.py <name>          # e.g. aa_progress, inventory-worn

Ctrl-C to stop the recording — the daemon finalizes the .vpk, opcode-stats
report, and per-packet event timeline on shutdown.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

DAEMON_DIR = Path(__file__).resolve().parent.parent
DAEMON_BIN = DAEMON_DIR / "build" / "scryd"
BUILD_DIR = DAEMON_DIR / "build"
CONF_DIR = DAEMON_DIR / "conf"
REPLAY_DIR = DAEMON_DIR / "tests" / "replay"
DEVICE = "sniff0"


def detect_target() -> str:
    """The backend the daemon in build/ was compiled for (-DSEQ_TARGET).

    Parsed from build/CMakeCache.txt, same as tests/replay/check.sh and
    build.sh; an unset value means the default 'live'. Captures are routed
    into tests/replay/<target>/ so an eql capture never lands next to the
    Live goldens (and check.sh finds it under the matching target).
    """
    cache = BUILD_DIR / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("SEQ_TARGET:"):
                _, _, val = line.partition("=")
                if val.strip():
                    return val.strip()
    return "live"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", help="capture name (e.g. aa_progress, inventory-worn)")
    parser.add_argument("--device", default=DEVICE, help=f"capture device (default: {DEVICE})")
    parser.add_argument("--ip", default=None,
                        help="EQ client IP to capture (default: auto-detect). "
                             "Set explicitly when two EQ clients share the LAN. "
                             "Overrides --net.")
    parser.add_argument("--net", default="69.174.0.0/16",
                        help="server netblock (CIDR) to scope the capture to, so "
                             "a mirror port's ambient LAN UDP (IPsec NAT-T, cloud "
                             "services) stays out of the .vpk. Default is the "
                             "Daybreak/EQ block (covers login/world/zone/UCS/"
                             "voice). Pass 'all' to capture unscoped.")
    parser.add_argument("--no-stats", action="store_true",
                        help="skip --opcode-stats output")
    parser.add_argument("--no-events", action="store_true",
                        help="skip --list-events output")
    parser.add_argument("--force", "-f", action="store_true",
                        help="overwrite existing fixture without prompting")
    args = parser.parse_args()

    if not DAEMON_BIN.is_file():
        print(f"error: daemon not built at {DAEMON_BIN}", file=sys.stderr)
        print(f"run: (cd {DAEMON_DIR} && cmake --build build -j)", file=sys.stderr)
        return 2

    target = detect_target()
    target_dir = REPLAY_DIR / target
    target_dir.mkdir(parents=True, exist_ok=True)
    print(f"=> target={target} (from build/CMakeCache.txt) — writing to "
          f"{target_dir.relative_to(DAEMON_DIR)}/")
    vpk = target_dir / f"{args.name}.vpk"
    stats = target_dir / f"{args.name}.opcodestats.txt"
    events = target_dir / f"{args.name}.events.txt"

    if vpk.exists() and not args.force:
        resp = input(f"{vpk.relative_to(DAEMON_DIR)} exists — overwrite? [y/N] ")
        if resp.strip().lower() not in ("y", "yes"):
            print("aborted")
            return 1

    print(f"=> setcap on {DAEMON_BIN.relative_to(DAEMON_DIR)}")
    setcap = subprocess.run(
        ["cmake", "--build", "build", "--target", "setcap"],
        cwd=DAEMON_DIR,
    )
    if setcap.returncode != 0:
        print("error: setcap target failed", file=sys.stderr)
        return setcap.returncode

    cmd = [
        str(DAEMON_BIN),
        "--device", args.device,
        "--config-dir", str(CONF_DIR),
        "--record-vpk", str(vpk),
        "--no-listen",
    ]
    # Scope the BPF filter: an explicit client --ip wins; else the --net server
    # block (a CIDR the daemon turns into a `net` filter), unless 'all'. Both go
    # through the daemon's --ip (host vs net chosen by the '/').
    scope = args.ip or (None if args.net.strip().lower() in ("", "all", "none")
                        else args.net)
    if scope:
        cmd.extend(["--ip", scope])
    if not args.no_stats:
        cmd.extend(["--opcode-stats", str(stats)])
    if not args.no_events:
        cmd.extend(["--list-events", str(events)])

    print()
    print(f"=> recording to {vpk.relative_to(DAEMON_DIR)}")
    if not args.no_stats:
        print(f"=> opcode stats to {stats.relative_to(DAEMON_DIR)}")
    if not args.no_events:
        print(f"=> event timeline to {events.relative_to(DAEMON_DIR)}")
    print(f"=> device: {args.device}    scope: {scope or 'unscoped (all UDP)'}"
          f"    Ctrl-C to stop")
    print()

    try:
        return subprocess.call(cmd, cwd=DAEMON_DIR)
    except KeyboardInterrupt:
        # daemon handles SIGINT itself and finalizes outputs
        return 0


if __name__ == "__main__":
    sys.exit(main())
