#!/usr/bin/env python3
"""Statically verify that every wire() binding resolves against its opcode table.

`EQPacketStream::dispatchFor` binds a handler only when the opcode has a payload
whose direction overlaps, AND whose `typename` and `sizechecktype` match the
wire() call EXACTLY. Any mismatch logs

    dispatchFor: opcode 'OP_X' has NO payload matching dir N typename 'Y' szt Z
    — handler NOT bound

and the daemon then decodes nothing for that opcode, forever, exit code 0.

CI already fails on that warning (see .github/workflows/ci.yml), but only for a
target it actually builds and runs. This script answers the same question for
EVERY target from source alone — no build, no capture, no runtime — and names
the mismatch instead of leaving you to read it out of a log:

    OP_SpawnAppearance2  wire wants spawnAppearance2Struct/match
                         table has  both:uint8_t/none

That is exactly the drift that made OP_GuildsInZoneList, OP_NewGuildInZone and
OP_SpawnAppearance2 dead on SEQ_TARGET=test for weeks (fixed a089d21): live and
test SHARE wire_live.cpp, the live table was corrected and the test one was not.

usage:
  tools/bindcheck.py                     # check every target (default)
  tools/bindcheck.py <wire.cpp> <opcodes.toml> <label>   # one explicit pair

Exit status: 0 clean, 1 on any unbindable wire() call.
"""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Each target: (wiring TU, opcode table). live and test deliberately share a TU —
# that sharing is what lets their tables drift apart unnoticed.
TARGETS = [
    ("live", "src/backend/live/wire_live.cpp", "conf/opcodes.toml"),
    ("test", "src/backend/live/wire_live.cpp", "conf/test/opcodes.toml"),
    ("eql", "src/backend/eql/wire_eql.cpp", "conf/eql/opcodes.toml"),
]

DIR_BITS = {"DIR_Client": 1, "DIR_Server": 2}
PAYLOAD_DIR_BITS = {"client": 1, "server": 2, "both": 3}
SZT_NAMES = {"SZC_None": "none", "SZC_Match": "match", "SZC_Modulus": "modulus"}

# wire("OP_X", SP_Zone, DIR_Server | DIR_Client, "typename", SZC_Match, handler)
WIRE_RE = re.compile(
    r'wire\(\s*"(OP_\w+)"\s*,\s*SP_\w+\s*,\s*([A-Za-z_|\s]+?)\s*,\s*'
    r'"(\w+)"\s*,\s*(SZC_\w+)',
)
# for (const char* op : {"OP_A", "OP_B"}) wire(op, SP_Zone, DIR_Server, "t", SZC_None, …)
LOOP_RE = re.compile(
    r'for\s*\(const char\* op\s*:\s*\{([^}]*)\}\)\s*\n\s*wire\(\s*op\s*,\s*SP_\w+\s*,\s*'
    r'([A-Za-z_|\s]+?)\s*,\s*\n?\s*"(\w+)"\s*,\s*(SZC_\w+)',
    re.M,
)


def parse_bindings(tu: Path) -> list[tuple[str, int, str, str]]:
    """Every (opcode, dir_bits, typename, sizechecktype) the TU registers."""
    src = tu.read_text()
    out = []

    def dir_bits(expr: str) -> int:
        bits = 0
        for tok in expr.split("|"):
            bits |= DIR_BITS.get(tok.strip(), 0)
        return bits

    for name, dirs, typ, szt in WIRE_RE.findall(src):
        out.append((name, dir_bits(dirs), typ, SZT_NAMES.get(szt, szt)))
    for names, dirs, typ, szt in LOOP_RE.findall(src):
        for name in re.findall(r'"(OP_\w+)"', names):
            out.append((name, dir_bits(dirs), typ, SZT_NAMES.get(szt, szt)))
    return out


def load_table(path: Path) -> dict[str, dict]:
    doc = tomllib.load(path.open("rb"))
    return {e["name"]: e for sec in ("zone", "world") for e in doc.get(sec, [])}


def check(label: str, tu: Path, toml: Path) -> int:
    bindings = parse_bindings(tu)
    table = load_table(toml)
    problems = []

    for name, want_dir, want_type, want_szt in bindings:
        entry = table.get(name)
        if entry is None:
            problems.append((name, "?", want_type, want_szt, "opcode name absent from the table"))
            continue
        for p in entry.get("payloads", []):
            pdir = PAYLOAD_DIR_BITS.get(p.get("dir", "both"), 3)
            if (pdir & want_dir) and p.get("typename") == want_type \
               and p.get("sizechecktype") == want_szt:
                break
        else:
            have = "; ".join(
                f'{p.get("dir")}:{p.get("typename")}/{p.get("sizechecktype")}'
                for p in entry.get("payloads", [])
            ) or "(no payloads declared)"
            problems.append((name, entry.get("id", "?"), want_type, want_szt, have))

    try:
        shown = toml.relative_to(REPO)
    except ValueError:
        shown = toml  # an explicit path outside the repo (e.g. a historical table)
    print(f"{label:5} {tu.name} x {shown}: "
          f"{len(bindings) - len(problems)}/{len(bindings)} bindings resolve")
    for name, oid, want_type, want_szt, have in problems:
        # An ffff opcode never fires, so the misbind is latent rather than live —
        # but it goes live the moment a rotation maps that id (see OP_Logout).
        state = "MAPPED" if oid not in ("ffff", "?") else "unmapped"
        print(f"    {name} (id {oid}, {state})")
        print(f"        wire wants : {want_type}/{want_szt}")
        print(f"        table has  : {have}")
    return len(problems)


def main(argv: list[str]) -> int:
    if len(argv) == 4:
        targets = [(argv[3], argv[1], argv[2])]
    elif len(argv) == 1:
        targets = TARGETS
    else:
        print(__doc__, file=sys.stderr)
        return 2

    bad = 0
    for label, tu, toml in targets:
        tu_p = Path(tu) if Path(tu).is_absolute() else REPO / tu
        tm_p = Path(toml) if Path(toml).is_absolute() else REPO / toml
        if not tu_p.exists() or not tm_p.exists():
            print(f"error: missing {tu_p if not tu_p.exists() else tm_p}", file=sys.stderr)
            return 1
        bad += check(label, tu_p, tm_p)

    if bad:
        print(f"\n{bad} wire() binding(s) would NOT bind — the daemon would decode "
              f"nothing for them, silently.", file=sys.stderr)
        return 1
    print("\nall bindings resolve")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
