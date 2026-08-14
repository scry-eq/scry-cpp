#!/usr/bin/env python3
"""Statically verify opcode tables: wire() bindings resolve, and no 1-byte gates.

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

Handles both call forms and both table formats, so it covers the sibling
codebases too: scry-cpp uses `wire()` + TOML, scry-cpp-quarm uses
`connect2()` + TOML, and legacy showeq uses `connect2()` + XML. Siblings are
checked when present and reported ADVISORY — a pre-existing issue next door
must not fail this repo's hook, or the check guarding this repo gets bypassed.

usage:
  tools/bindcheck.py                     # every target, siblings advisory
  tools/bindcheck.py <wire.cpp> <table> <label>   # one explicit pair

Exit status: 0 clean, 1 on any unbindable call IN THIS REPO.
"""

from __future__ import annotations

import re
import sys
import tomllib
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Each target: (label, wiring TU, [opcode tables]). live and test deliberately
# share a TU — that sharing is what lets their tables drift apart unnoticed.
TARGETS = [
    ("live", "src/backend/live/wire_live.cpp", ["conf/opcodes.toml"]),
    ("test", "src/backend/live/wire_live.cpp", ["conf/test/opcodes.toml"]),
    ("eql", "src/backend/eql/wire_eql.cpp", ["conf/eql/opcodes.toml"]),
]

# Sibling repos, checked when present. They are per-machine clones, so a missing
# one is skipped rather than failed. Both wire via connect2() instead of the
# daemon's wire() lambda, and legacy still ships XML tables — hence the parser
# handling both call forms and both table formats.
SIBLINGS = [
    ("quarm", "../scry-cpp-quarm/src/daemonapp.cpp",
     ["../scry-cpp-quarm/conf/opcodes.toml"]),
    ("legacy", "../showeq/src/interface.cpp",
     ["../showeq/conf/zoneopcodes.xml", "../showeq/conf/worldopcodes.xml"]),
]

DIR_BITS = {"DIR_Client": 1, "DIR_Server": 2}
PAYLOAD_DIR_BITS = {"client": 1, "server": 2, "both": 3}
SZT_NAMES = {"SZC_None": "none", "SZC_Match": "match", "SZC_Modulus": "modulus"}

# Both call forms register the same tuple:
#   wire("OP_X", SP_Zone, DIR_Server | DIR_Client, "typename", SZC_Match, handler)
#   m_packet->connect2("OP_X", SP_Zone, DIR_Server, "typename", SZC_Match, recv, SLOT(..))
# scry-cpp uses the first (typed dispatch); quarm and legacy showeq use the
# second (Qt SLOT dispatch). The resolution rule is identical in both.
WIRE_RE = re.compile(
    r'(?:wire|connect2)\(\s*"(OP_\w+)"\s*,\s*SP_\w+\s*,\s*([A-Za-z_|\s]+?)\s*,\s*'
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
    """Opcode name -> entry, from either a TOML table or a legacy XML one.

    Normalised to the TOML shape so the checks below don't care which format
    they came from — legacy showeq and ShowEQ-Legends still ship XML.
    """
    if path.suffix == ".xml":
        text = path.read_text().replace(
            '<!DOCTYPE seqopcodes SYSTEM "seqopcodes.dtd">', "")
        out = {}
        for op in ET.fromstring(text).findall("opcode"):
            out[op.get("name", "")] = {
                "name": op.get("name", ""),
                "id": op.get("id", "ffff"),
                "payloads": [
                    {"dir": p.get("dir", "both"),
                     "typename": p.get("typename", "uint8_t"),
                     "sizechecktype": p.get("sizechecktype", "none")}
                    for p in op.findall("payload")
                ],
            }
        return out

    doc = tomllib.load(path.open("rb"))
    return {e["name"]: e for sec in ("zone", "world") for e in doc.get(sec, [])}


def load_tables(paths: list[Path]) -> dict[str, dict]:
    """Merge several tables (legacy splits zone/world across two files)."""
    merged: dict[str, dict] = {}
    for p in paths:
        merged.update(load_table(p))
    return merged


def check_gates(label: str, tables: list[Path]) -> int:
    """Flag `uint8_t` + `sizechecktype="match"`, which is always a bug.

    `uint8_t` is the opaque/variable-payload placeholder, but `match` gates on
    `sizeof(uint8_t)` = 1, so every packet that is not exactly one byte is
    DROPPED — with only a stderr size-diagnostic to show for it. That shipped
    on eql's OP_Logout (2026-08-08): the wire sends 2B and 0B, both gated out,
    and no fixture exercised it because you log out after stopping a capture.

    Only MAPPED opcodes matter; an ffff row gates nothing.
    """
    bad = 0
    for entry in load_tables(tables).values():
        if entry.get("id", "ffff") == "ffff":
            continue
        for p in entry.get("payloads", []):
            if p.get("typename") == "uint8_t" and p.get("sizechecktype") == "match":
                print(f"    {entry['name']} (id {entry['id']}) declares "
                      f'uint8_t + sizechecktype="match" — a 1-byte gate that '
                      f'drops every real packet. Use "none".')
                bad += 1
    if bad:
        print(f"{label:5} {bad} payload(s) declare a 1-byte gate")
    return bad


def check(label: str, tu: Path, tables: list[Path]) -> int:
    bindings = parse_bindings(tu)
    table = load_tables(tables)
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

    def rel(p: Path) -> str:
        try:
            return str(p.relative_to(REPO))
        except ValueError:
            return str(p)  # a sibling repo or an explicit out-of-tree path
    shown = ", ".join(rel(p) for p in tables)
    print(f"{label:6} {tu.name} x {shown}: "
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
        targets, siblings = [(argv[3], argv[1], [argv[2]])], []
    elif len(argv) == 1:
        targets, siblings = TARGETS, SIBLINGS
    else:
        print(__doc__, file=sys.stderr)
        return 2

    bad = 0        # in-repo problems — these fail the run
    advisory = 0   # sibling-repo problems — reported, never fatal
    for label, tu, tables in list(targets) + list(siblings):
        is_sibling = any(t[0] == label for t in siblings)
        tu_p = Path(tu) if Path(tu).is_absolute() else REPO / tu
        tm_ps = [Path(t) if Path(t).is_absolute() else REPO / t for t in tables]
        missing = [p for p in [tu_p, *tm_ps] if not p.exists()]
        if missing:
            # Sibling repos are per-machine clones; absence is not a failure.
            # A missing path in THIS repo is.
            if is_sibling:
                print(f"{label:6} skipped (not checked out)")
                continue
            print(f"error: missing {missing[0]}", file=sys.stderr)
            return 1
        n = check(label, tu_p, tm_ps) + check_gates(label, tm_ps)
        if is_sibling:
            advisory += n
        else:
            bad += n

    if advisory:
        # Sibling repos are ADVISORY on purpose. They have their own CI and
        # their own maintenance pace, and failing this repo's hook/CI over a
        # pre-existing issue in a neighbour would just train people to bypass
        # the check that guards THIS repo.
        print(f"\n{advisory} problem(s) in sibling repos (advisory, not fatal here) "
              f"— fix them in their own repo.")
    if bad:
        print(f"\n{bad} problem(s) — the daemon would silently decode nothing for "
              f"the opcodes above.", file=sys.stderr)
        return 1
    print("\nthis repo: all bindings resolve; no bad gates")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
