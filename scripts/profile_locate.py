#!/usr/bin/env python3
"""Locate a value-bearing region inside an OP_PlayerProfile dump.

Mode C tooling (see .claude/skills/opcode-hunt). The EQL profile is ~40KB of
mostly-unmapped blob with a handful of verified landmarks; this narrows a search
by hunting for values the operator can read off the game UI.

    ./build/scryd --replay cap.vpk --config-dir conf --no-listen \\
        --dump-payload 0x371a:/tmp/pp
    scripts/profile_locate.py /tmp/pp.1.bin --truth truth.json

Ground truth is supplied, never hardcoded — it is per-character and per-session,
and hardcoding it would also put personal data in the repo. Write a JSON file:

    {
      "money":  [10951, 1110, 1601, 1592],   plat, gold, silver, copper
      "u32":    [104, 125, 4, 50, 3, 15],    storage counts, quantities, ...
      "names":  ["Anthemion", "Wooden Flute"]
    }

With two dumps it also diffs them, which is the paired-capture step: bracket ONE
in-game change with two profile fires (the profile fires per zone-in) and the
changed range is the block you are after.

Everything reported is a CANDIDATE. The profile rotates every patch, so confirm
by patching the parser and replaying, per the skill's disambiguation bar.
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path

# Verified landmarks, from seq-backend-eql's parse_player_profile. Offsets in the
# FIXED prefix have held across rotations; anything past ~36000 sits in the
# variable region and drifts with inventory size, so treat those as hints only.
LANDMARKS = [
    (29, "class_mask (u32)"),
    (33687, "carried money P/G/S/C"),
    (33703, "cursor money P/G/S/C"),
    (33777, "stance (u32)"),
    (33781, "invocation (u32)"),
    (36245, "inventory-mirror money (variable region)"),
    (36261, "bank money (variable region)"),
]

EMPTY = 0xFFFFFFFF


def u32s(b):
    n = len(b) // 4
    return struct.unpack_from("<%dI" % n, b, 0)


def show_landmarks(b):
    print("== landmarks ==")
    for off, label in LANDMARKS:
        if off + 4 > len(b):
            print(f"  {off:>7}  {label:<38} (past EOF)")
            continue
        v = struct.unpack_from("<I", b, off)[0]
        print(f"  {off:>7}  {label:<38} = {v}")


def check_money(b, money):
    """The strongest identity check: four known u32s in a row."""
    print("\n== identity (money quadruple) ==")
    want = struct.pack("<4I", *money)
    hits = [m.start() for m in re.finditer(re.escape(want), b)]
    if not hits:
        print(f"  NOT FOUND {money} — wrong character, stale capture, or the")
        print("  denominations are not normalised (EQ sends 101 silver / 281 copper,")
        print("  so read them off the UI exactly, do not carry into the next unit).")
        return
    for h in hits:
        tag = ""
        if h == 33687:
            tag = "  <- carried, the verified fixed offset"
        elif h == 36245:
            tag = "  <- inventory mirror"
        print(f"  @{h}{tag}")


def scan_u32(b, values):
    """Every offset holding one of the wanted u32s, at BYTE granularity.

    Do not word-align this. The profile's verified fields sit at 33687, 33777
    and 33781 — all congruent to 3 mod 4 — so a scan that only looks at
    multiples of 4 misses the very landmarks we already trust.
    """
    print("\n== u32 value scan (byte-granular) ==")
    for want in values:
        pat = struct.pack("<I", want)
        offs = [m.start() for m in re.finditer(re.escape(pat), b)]
        head = ", ".join(str(o) for o in offs[:12])
        print(f"  {want:>8}: {len(offs):>4} hit(s)  @[{head}{' …' if len(offs) > 12 else ''}]")


def scan_names(b, names):
    """ASCII scan. If item names are serialised, this finds the block outright."""
    print("\n== name scan ==")
    for name in names:
        pat = name.encode("ascii", "ignore")
        hits = [m.start() for m in re.finditer(re.escape(pat), b)]
        if hits:
            print(f"  {name!r}: {len(hits)} hit(s) @{hits[:8]}")
            # Context tells you the record shape around a name.
            o = hits[0]
            lo, hi = max(0, o - 16), min(len(b), o + len(pat) + 16)
            print(f"      ctx @{lo}: {b[lo:hi].hex()}")
        else:
            print(f"  {name!r}: none")


def scan_empty_runs(b, min_run):
    """0xffffffff runs — the empty-slot sentinel, so a run brackets a slot array.

    OPCODES_LEGENDS.md records "item-id slot arrays with 0xffffffff empties
    scattered through the blob (e.g. a 10-empty-slot run @35981)". A long run is
    the cheapest way to find where a storage array lives.

    Scanned as a byte run, not a word run: the arrays are not 4-byte aligned to
    the start of the profile (see scan_u32), so word-stepping reports a run
    shifted by up to 3 bytes and one slot short.
    """
    min_bytes = min_run * 4
    print(f"\n== 0xff runs (>= {min_run} empty slots / {min_bytes} bytes) ==")
    found = 0
    for m in re.finditer(b"\xff{%d,}" % min_bytes, b):
        lo, hi = m.start(), m.end()
        found += 1
        # Live entries bracket the run; read them relative to the run edges so
        # alignment follows the ARRAY, not the file.
        before = list(struct.unpack_from("<6I", b, lo - 24)) if lo >= 24 else []
        after = list(struct.unpack_from("<6I", b, hi)) if hi + 24 <= len(b) else []
        print(f"  @{lo:>7}..{hi:<7} {hi - lo:>5} bytes = {(hi - lo) // 4} slot(s)")
        print(f"      before={before}")
        print(f"      after ={after}")
    if not found:
        print("  none")


def diff(a, b):
    """Coalesced changed ranges between paired dumps."""
    print("\n== diff ==")
    if len(a) != len(b):
        print(f"  sizes differ: {len(a)} vs {len(b)} (delta {len(b) - len(a):+})")
        print("  A size change is itself the signal — the profile is variable-length")
        print("  and the inventory block is what moves the tail.")
    n = min(len(a), len(b))
    ranges, start = [], None
    for i in range(n):
        if a[i] != b[i]:
            if start is None:
                start = i
        elif start is not None:
            ranges.append((start, i))
            start = None
    if start is not None:
        ranges.append((start, n))
    if not ranges:
        print("  identical over the common prefix")
        return
    # Merge ranges separated by a small gap — one logical field often shows as
    # several runs when some bytes happen to match.
    merged = [ranges[0]]
    for lo, hi in ranges[1:]:
        if lo - merged[-1][1] <= 8:
            merged[-1] = (merged[-1][0], hi)
        else:
            merged.append((lo, hi))
    print(f"  {len(merged)} changed range(s):")
    for lo, hi in merged[:40]:
        print(f"    @{lo:>7}..{hi:<7} ({hi - lo} bytes)")
        print(f"       before: {a[lo:min(hi, lo + 32)].hex()}")
        print(f"       after : {b[lo:min(hi, lo + 32)].hex()}")
    if len(merged) > 40:
        print(f"    … {len(merged) - 40} more")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dumps", nargs="+", type=Path, help="pp.N.bin (one, or two to diff)")
    ap.add_argument("--truth", type=Path, help="JSON with money / u32 / names")
    ap.add_argument("--min-run", type=int, default=4, help="shortest 0xffffffff run to report")
    args = ap.parse_args()

    blobs = []
    for p in args.dumps:
        if not p.exists():
            sys.exit(f"no such dump: {p}")
        blobs.append(p.read_bytes())
        print(f"{p}: {len(blobs[-1])} bytes")

    truth = json.loads(args.truth.read_text()) if args.truth else {}
    b = blobs[0]

    show_landmarks(b)
    if truth.get("money"):
        check_money(b, truth["money"])
    if truth.get("names"):
        scan_names(b, truth["names"])
    if truth.get("u32"):
        scan_u32(b, truth["u32"])
    scan_empty_runs(b, args.min_run)

    if len(blobs) >= 2:
        diff(blobs[0], blobs[1])


if __name__ == "__main__":
    main()
