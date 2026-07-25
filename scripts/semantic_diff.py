#!/usr/bin/env python3
"""Semantic golden compare, robust to the summoned-pet spawn_added flap.

The eql replay/golden harness has a load-only heisenbug (see
project_eql_golden_spawn_order_flap / OPCODES_LEGENDS.md). Around multi-zone
transitions the *emission* of spawn deltas is non-deterministic under CPU load
in several ways that all converge to the same client state:

  * an idempotent duplicate `spawn_added` (same id + content, no removal since),
  * spawn_added emitted in a different ORDER,
  * a spawn's async-back-filled `guild_tag` present in one run's delta but not
    another's (the guild resolves + re-emits at a load-dependent point).

A byte `cmp` can't tolerate any of these (each also shifts every later `seq`).
So instead of comparing the delta STREAM, this replays both streams into the
STATE they produce and compares that:

  * spawn deltas (snapshot / spawn_added / spawn_updated / spawn_removed) are
    applied to a running spawn map; the map is CHECKPOINTED (sorted by id, with
    async fields stripped) at every `snapshot` boundary and at end. A map is
    order-independent, so emission-order and idempotent-duplicate flaps vanish;
    a real difference (a spawn missing / extra / wrong content) still shows.
  * every NON-spawn envelope (chat, combat, loot, exp, player_stats, zone,
    time, buffs, group, …) is compared as an ordered stream — those don't flap.

Exit 0 on semantic match, 1 on difference (printing the first).

Usage: semantic_diff.py <golden.pbstream> <produced.pbstream>
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decode_pbstream import parse_proto, iter_records, decode_message, PROTO_DIR  # noqa: E402

# Spawn fields resolved ASYNCHRONOUSLY (a later opcode back-fills + re-emits the
# spawn), so whether a given delta already carries them is part of the same
# emission-timing flap. Strip before comparing. guild_tag: OP_GuildsInZoneList /
# OP_NewGuildInZone back-fill.
_ASYNC_SPAWN_FIELDS = ("guild_tag",)


def _canon(v):
    return json.dumps(
        v, sort_keys=True,
        default=lambda b: b.hex() if isinstance(b, (bytes, bytearray)) else str(b),
    )


def _strip(sp):
    return {k: v for k, v in sp.items() if k not in _ASYNC_SPAWN_FIELDS}


def _checkpoint(spawn_map):
    return _canon(sorted(spawn_map.values(), key=lambda s: s.get("id", 0)))


def normalize(path, schema):
    """Return (checkpoints, events): the spawn-map states + the non-spawn stream."""
    data = Path(path).read_bytes()
    running = {}       # spawn id -> spawn record (async fields stripped)
    checkpoints = []   # spawn-map state at each snapshot + at end
    events = []        # ordered non-spawn envelopes

    for _idx, s, e in iter_records(data):
        env = decode_message(data, s, e, "Envelope", schema)
        env.pop("seq", None)
        env.pop("server_ts_ms", None)

        if "snapshot" in env:
            # Close the prior segment, then reset the map to the snapshot's state.
            checkpoints.append(_checkpoint(running))
            running = {sp.get("id"): _strip(sp)
                       for sp in (env["snapshot"].get("spawns") or [])}
        elif "spawn_added" in env:
            sp = env["spawn_added"].get("spawn", {})
            running[sp.get("id")] = _strip(sp)
        elif "spawn_updated" in env:
            u = env["spawn_updated"]
            sid = u.get("id")
            if sid in running:  # daemon ignores updates for unknown ids
                running[sid].update({k: v for k, v in u.items() if k != "id"})
        elif "spawn_removed" in env:
            running.pop(env["spawn_removed"].get("id"), None)
        else:
            events.append(_canon(env))

    checkpoints.append(_checkpoint(running))  # final segment
    return checkpoints, events


def _first_diff(a, b, label):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return (f"{label} DIFFER at {i}:\n"
                    f"  golden:   {a[i][:240]}\n"
                    f"  produced: {b[i][:240]}")
    if len(a) != len(b):
        longer, who = (a, "golden") if len(a) > len(b) else (b, "produced")
        return (f"{label} count DIFFER: golden {len(a)} vs produced {len(b)}\n"
                f"  first extra ({who}): {longer[min(len(a), len(b))][:240]}")
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <golden.pbstream> <produced.pbstream>")
    schema = parse_proto(sorted(PROTO_DIR.glob("*.proto")))

    a_cp, a_ev = normalize(sys.argv[1], schema)
    b_cp, b_ev = normalize(sys.argv[2], schema)

    diff = _first_diff(a_cp, b_cp, "spawn-state checkpoint") or _first_diff(a_ev, b_ev, "non-spawn event")
    if diff:
        print(diff)
        return 1
    print(f"semantic-match: {len(a_cp)} spawn checkpoints, {len(a_ev)} non-spawn events")
    return 0


if __name__ == "__main__":
    sys.exit(main())
