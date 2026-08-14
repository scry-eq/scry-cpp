# EQL chat-opcode capture runbook

Goal: find the EQL ids for the chat/message opcodes so the web chat panel
lights up. All the handlers are **already wired by name** in
`src/backend/eql/wire_eql.cpp` (OP_CommonMessage → `MessageShell::channelMessage`,
OP_SpecialMesg, OP_FormattedMessage, OP_SimpleMessage) — they just need the
right opcode ids in `conf/eql/opcodes.toml`.

**Why a dedicated capture:** `0x2735` turned out to be a per-*entity* event
stream, not chat — its leading u32 is a spawn id as often as a string id, and
the two number spaces overlap, so it can't be classified from the packet alone
(see `../OPCODES_LEGENDS.md`). Player chat is different: it carries **literal
text** on the wire. So we seed known phrases and string-grep for them — that
pins the opcode + direction + field offsets unambiguously, no eqstr guessing.

---

## Pre-flight (at the box)

- Build stays `eql` (it is). If unsure: `./build.sh --target eql`.
- Pick a **quiet zone** (less wire noise = cleaner grep) with a **killable mob**.
- Log a **second box** so /tell captures both the C>S send and the S>C receive.

## In-game sequence

**0. Start recording BEFORE logging in** — the SOE session handshake must be in
the capture or it replays to zero packets:

```sh
scripts/capture.py eql-chat-YYYYMMDD      # auto-routes to tests/replay/eql/
```

Then log in (both boxes) and zone in.

**1. One distinct, ordinary-looking phrase per channel.** Keep them natural —
NOT obvious tool markers (nothing like "SEQTEST"); the text goes to the live
server. Space each ~2 s apart. **Write down the channel → phrase map** (kept
local, never committed):

| Channel  | Command          | (write your phrase here) |
|----------|------------------|--------------------------|
| Say      | `/say …`         |                          |
| OOC      | `/ooc …`         |                          |
| Shout    | `/shout …`       |                          |
| Auction  | `/auction …`     |                          |
| Tell →   | `/tell <box> …`  |  (from main to box)      |
| Tell ←   | `/tell <main> …` |  (from box back to main) |
| Group    | `/gsay …`        |  (if grouped)            |

Each phrase must be **unique** so the grep is unambiguous.

**2. NPC / system text:** `/say hail` to an NPC (captures its spoken reply);
`/con` a mob.

**3. Combat + loot:** melee a mob to death (→ "you hit / it hits you / you have
slain / you gain experience"), cast one spell, loot the corpse. The Phase-1
channels are the priority — don't stress hitting every combat line.

**4. `Ctrl-C`** to finalize the `.vpk`.

## Hygiene

- Generic, natural phrases only — nothing identifiable sent to the server.
- Findings get documented by **channel/role**, never the literal phrases or box
  names (same rule as every other opcode entry).
- The `.vpk` + artifacts are gitignored / local-only.

---

## Analysis (what happens with the capture)

1. Confirm it decoded — non-empty `tests/replay/eql/eqlegends-chat-*.opcodestats.txt`
   (empty ⇒ recording started mid-session; the SOE handshake was missed).
2. Batch-dump every opcode in one replay pass:
   ```sh
   args=(); for op in $(grep -oE '^0x[0-9a-f]{4}' <stats>); do args+=(--dump-payload "$op:dumps/$op"); done
   ./build/scryd --replay tests/replay/eql/<name>.vpk --config-dir conf --no-listen "${args[@]}"
   ```
3. `grep -rl "<phrase>" dumps/` → the exact opcode carrying each phrase, for both
   the C>S send and the S>C broadcast.
4. Decode each: direction + channel field + from/to-name offsets → map to
   `OP_CommonMessage` (`channelMessageStruct`) etc. Any combat/system line that is
   NOT found as a literal is a string-id → cross-ref `~/.scry/eql/eqstr_us.txt`
   / `dbstr_us.txt`.
5. Set the ids in `conf/eql/opcodes.toml` → the pre-wired `MessageShell` handlers
   fire → `ChatMessage` envelopes → web chat panel. Verify via a recorded golden
   (chat envelopes with the right channel bytes).

Multibox note: `capture.py` records all traffic on the device, so both boxes are
in the one `.vpk`; at analysis, `--only-session <charname>` narrows to one box if
the streams tangle.
