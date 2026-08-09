# EverQuest Legends — opcode table (work-of-record)

New EQ-family target (first seen 2026-07-02). Same SOE **stream** protocol as
Live EQ (SessionRequest carries the "Everquest" magic; zlib; Combined/Ack/
Oversized framing — the daemon deframes it unchanged), but the **application
opcode table is fully remapped** — every app opcode reads `unknown` against the
Live table. This file tracks the Legends-specific opcode + struct mapping.

> **Credit.** The bulk of the EQ Legends reverse-engineering documented here —
> the opcode re-map, struct layouts, the stream/session findings, and the UCS
> cross-zone-chat protocol — comes from **Xerxes** on the **showeq.net** forums,
> via the community "eql-full-edits" drops for legacy ShowEQ. Our work here
> integrates, verifies against captures, and ports that into the daemon / Rust
> decoder / proto / web stack. Per-item provenance is noted in the dated entries
> below; unless a find says otherwise, assume the wire-format credit is Xerxes'.

Decode captures with the raw-pcap replay path:
```
./build/showeq-daemon --replay-pcap <capture>.pcap --config-dir conf --no-listen \
    --opcode-stats out.opcodestats.txt --list-events out.events.txt \
    --dump-payload 0xXXXX:out/prefix
```
Captures live in the meta-repo `captures/` dir (gitignored — plaintext creds/
session data; never commit, use generic placeholders in this doc).

Server topology (Daybreak netblock `69.174.201.x`): login `:15900`, world
`:9000`, zones on dynamic high ports, chat `:9877`.

**Game-design notes (affect struct layout):**
- A character has **3 simultaneous classes** (not one). Expect the char-profile /
  skills / spell-affinity structs to carry 3 class ids + possibly 3 parallel
  class-data blocks — don't assume the single-`class_` layout of Live
  `charProfileStruct`. Watch for triplicated class/level fields.

## ⚠ PATCHED 2026-08-05 — SIXTH rotation (a "mini patch" that rotated everything)

A mini patch on 08/05 rotated the **entire** table again, one day after the
08/04 rotation. Measured against an 08/06 capture (Rathe Mountains → Toxxulia →
Paineel → Toxxulia, 4 zone-ins, targets, cons, and a rock-click):

| map | mapped ids | fire in the 08/06 capture |
|---|---|---|
| ours (08/04) | 54 | **0** |
| upstream's new | 90 | 34 scoped, far more unscoped |

Ported from upstream `9854199`, then validated. **46 known, 0 unbound handlers,
0 gate warnings.**

### The `updated`-date filter did NOT apply this time

The 08/04 rotation taught "filter upstream's rows on `updated`". That rule is
**inapplicable here and would have been actively misleading**: upstream labelled
every row of the new map `08/04/26` even though it describes the 08/05 wire, and
exactly one mapped row carries an older date (a `?` placeholder, zero fires).
They rewrote the table wholesale rather than partially, so the correct
discriminator this rotation is **"does the id fire in a current capture"**.

Generalised rule: the date tells you what upstream *touched*, never what the
*wire* is. Check dates when they ship a partial rotation; check fires always.

### Struct: `playerSelfPosStruct` rearranged AGAIN (still 42B)

Second consecutive patch to rearrange this body while keeping its size, so no
size gate can ever catch it — only a range check against the `OP_SelfPos`
breadcrumb will. New layout **x@10, y@18, z@22, heading@38** (08/04 was
y@18/z@30/x@38/heading@22).

Pinned the same way as before: over 1054 self-reports vs 22350 breadcrumb
records the ranges match essentially exactly — @10 [-547.8, 488.0] vs the
breadcrumb's [-547.8, 488.0], @18 [-2627.3, 1152.4] vs [-2627.3, 1152.6], @22
[-67.6, 15.5] vs [-67.8, 15.6]. A captured self-report decodes to **0.0000
units** from a breadcrumb point. Heading is 11-bit at @38, scoring **0.64°**
against travel bearing over 469 legs (inverted: 71.19°). The packed structs
(`spawnStruct` position union, `playerSpawnPosStruct`) were untouched.

Upstream still declares the struct 44B; the wire is 42B (1054 C>S bodies, none
at 44), so `PAYLOAD_LEN` stays 42 — same call as 08/04.

### ✅ OP_ClickDoor `581b` — an opcode inherited as DEAD that eql actually uses

Hunted from a user report ("clicked a rock to unlock a door, got rejected, 3
times"). `OP_ClickObject 3a21` fires **zero** times, because a rock that opens a
door is a **door**, not a ground object.

`OP_ClickDoor` sat at `id="ffff" priority="-1"` — *dead / never-hunt* — inherited
from the Live catalog, and upstream still carries it dead. It is live on eql:

```
OP_ClickDoor = 0x581b   16B C>S
  @0  u32  clicker spawn id      12671  (the client's own twin id)
  @4  u32  held item id      0xFFFFFFFF  ("nothing held")
  @8  u32  unknown                    0
  @12 u32  door id                  246
```

Four fires, byte-identical, each **0 ms before** an `OP_SimpleMessage` carrying
eqstr **130** *"It's locked and you're not holding the key."* — the
`0xFFFFFFFF` held-item sentinel is precisely what the server rejected. Door id
246 resolves in the `OP_SpawnDoor` list (`doorStruct` @80, 241 distinct ids
across 376 records) to **`PAROCK103`** — a rock in Paineel, matching where the
click happened. `12671` is carried by 144 self-position reports, confirming @0
is the player.

**Lesson: `priority = -1` means "upstream sees no handler for it", not "this
server does not send it."** The ladder is a triage inheritance from Live and can
be wrong per-backend. When a user reports an in-game action that produces no
decoded event, check the p-1 shelf before concluding the opcode is unmapped.

Note the message text is **not** in the payload — it is an eqstr format id
resolved client-side, so grepping payloads for the string finds nothing. Search
`eqstr_us.txt` for the text, then hunt its id as a u32.

### ✅ OP_ClickObject `3a21` — confirmed, and it is the GROUND-ITEM click

Validated on a purpose-made drop/pickup capture. `OP_ClickObject` is the
ground-item click; `OP_ClickDoor` is the door/switch click. They are different
opcodes for different things, which is why the rock-click capture showed
`3a21` at zero fires.

```
OP_ClickObject = 0x3a21   12B, BOTH directions
  @0  u32  drop id            588
  @4  u32  clicker spawn id  3488   (the player; 102 self-reports carry it)
  @8  u32  unknown              0
```

The whole cycle decodes, and the timeline reads cleanly:

```
t+0s    S  OP_GroundSpawn  62B   pre-existing ground item (drop 587)
t+27s   C  OP_GroundSpawn   0B   the DROP request (zero-length)
t+27s   S  OP_GroundSpawn  62B   server announces drop 588
t+29s   C  OP_ClickObject  12B   the PICKUP request for drop 588
t+29s   S  OP_ClickObject  12B x2  server confirms the removal
```

Both ends surface: `spawn_added id:588 type:DROP` then `spawn_removed id:588`.
The C>S side is 12B, not the 16B the wiring comment claimed — corrected.

### Drops render as `"Drop: Generic"` — and resolving the real name is BLOCKED

`OP_GroundSpawn` carries the 3D model actor-def (`IT63_ACTORDEF`) in `idFile`
and leaves `name` empty, so every ground item renders as `"Drop: Generic"`.

An earlier revision of this section claimed the fix was "correlate the `itemId`
field against `OP_ItemPacket`". **That is wrong — investigated 2026-08-07 on a
purpose-made drop/pickup capture and there is no usable join key.** Recorded so
nobody re-walks it:

Item names ARE on the wire — 402 item-def records, 315 distinct names, ~1195B
each with the name stored twice (offset 988 in the sample), and the dropped
item's own def is present. Every candidate link to the ground spawn fails:

| candidate | result |
|---|---|
| `OP_GroundSpawn.itemId` | reads `0xFFFFFFFF` on BOTH observed drops — no item id on the wire |
| `fieldA` (38), `fieldC` (4500), `dropId` (587/588) | zero u32 occurrences anywhere in the item-def record |
| model string `IT63_ACTORDEF` | item defs contain NO `IT*_ACTORDEF` string at all |
| temporal adjacency | defs arrive ~21% into the capture, the drop at ~90% — they are a zone-in inventory sync, not per-drop |

The only id-shaped field in an item def is a 16-char code (`iGS000e0003e3W00`,
140 distinct), which looks like an item-link serial.

Also note the item-def records do NOT surface as any decoded opcode: they are
fragments matching no app packet the daemon emits, and `OP_ItemPacket` is
`ffff`/priority -1 in both our table and upstream's. So the work is three stacked
unknowns — hunt the opcode, derive the record layout, THEN find a join key that
may not exist — not a wiring job.

Do not attempt it from a single-drop capture. It needs one that drops and
retrieves SEVERAL distinct items, so a join key can be found rather than guessed
from n=1.

Surfacing the actorDef instead of `Generic` was considered and **rejected**
(user, 2026-08-07): `IT63_ACTORDEF` is opaque to a player, so it is no more
useful than `Generic`.

### ⚠ OP_ZoneEntry positions were WRONG — half of every zone sat at x = 0

Found from a user report that the map drew "a line on the x axis at 0". Measured
on the 08/06 golden: **975 of 1922 spawns had x = 0**, x took only 312 distinct
values against y's 630 and z's 1216, and x saturated at ±32767.

Root cause: the parser read the coordinates as the **first three consecutive
words** (Z, X, Y, all low-19). That is not the block's shape. Word 1 is a PAD —
zero in 479 of 952 id-paired records — so `x` was reading the pad.

This is what upstream's struct was for, and reading it earlier would have
short-circuited the whole hunt. `posData[5]` gives the shape, and the consumer
`spawn.cpp: setPos(s->y >> 3, s->x >> 3, s->z >> 3)` gives the EQL transpose —
their `y` field is world X, their `x` field is world Y. **Both halves or
neither**: their struct alone transposes the map, their call site alone misses
that Z lives in the high bits.

Word roles pinned by scoring every (word, half) against the untouched
`OP_MobUpdate` stream over 952 id-paired records — median error, best vs
runner-up:

| axis | word | median err | runner-up |
|---|---|---|---|
| X | w0 low-19 | 161 | 554 |
| Y | w3 low-19 | 182 | 1441 |
| Z | w4 **high**-19 | **9** | 43 |

X/Y carry more error than Z because ZoneEntry reports the *spawn-time* position
while MobUpdate reports the *current* one — mobs walk, terrain does not. Z's
9-unit median is what confirms the alignment.

Result: x zeros **975 → 5** of 1922, x distinct **312 → 1318**, and the ±32767
saturation is gone. The new x range (−2653..3304) equals the OLD z range, which
is the signature of the old read shifting X into z's slot.

Upstream labels w4 `heading:12 | padding00:20`, but 12 + 1 + 19 = 32 and Z
measurably lives in that "padding" — so their Z word index is off by two while
their X/Y ones are right. Heading is still not recoverable: no word at 11- or
12-bit width beats noise against MobUpdate's facing, so it stays zeroed rather
than pointed in a direction taken from the wrong bits.

**Lesson: a size-stable position block needs a positive check every rotation.**
Nothing warned here — no gate, no unbound handler, no size mismatch. The only
signal was a human looking at the map. `OP_ZoneEntry`'s inline position was never
re-derived at 08/04 (only the self and other-PC structs were), so it had been
wrong for at least two rotations.

### Newly available from upstream this rotation

`OP_SendAATable 2fc4` (456 fires — we had it `ffff`; their `68c4 -> 2fc4` fix is
correct), the pet suite (`OP_PetBuffList 64ae` fires 7×; `PetCommands`/
`PetButtons`/`PetTarget` mapped id-only, zero fires), and their
`OP_FormattedMessage` stock-layout revert. `OP_LiteralMessageEQL 0e6f` is
upstream's rename of `OP_LootMessageEQL` (generalised from loot-only to any
color-routed literal message); mapped onto our existing `OP_LootMessage` entry
for now — adopting the neutral rename + color routing is a follow-up.

### Still open

- `OP_ClickObject 3a21` — still zero fires (a rock is a door; a true ground-object
  click has not been captured).
- `OP_BuffList 1aef` — zero fires this capture.
- The pet trio beyond `PetBuffList` — needs a session with a pet.

## ⚠ PATCHED 2026-08-04 — FIFTH full opcode rotation; p10 + p9 CLOSED

Fifth full rotation (07/07, 07/14, 07/28, 07/29, 08/04). Against a same-day
capture (Toxxulia Forest → Paineel → The Hole, with targets and cons) the whole
table read **zone 131 distinct / 0 known, world 15 / 0 known**. All 71 mapped
ids were reset to `ffff` before anything was re-imported; prior ids are kept in
each entry's comment.

Upstream shipped 6.4.25.1 the same day (**51 opcodes re-derived** + position
struct updates). Their table was ported and then validated per-opcode; two p10/p9
opcodes they had **not** re-derived were hunted here.

### The trap this rotation: upstream's un-re-derived rows

Upstream's ChangeLog defers "situational opcodes (Group\*, WearChange, LootDrops,
Random, AA, Inspect) to a later update", and their file still carries those rows
at their **07/29** ids. Measured against the 08/04 capture, **all 16 fire zero
times** — they are dead ids sitting in a current-looking file:

`16ac 784e 3b21 4155 03ce 0cb5 5767 587b 6711 612d 3ed1 3a86 6426 0ad1 3989 4037`

A whole-file port imports every one of them as a live mapping. **`updated="08/04/26"`
is the discriminator** — filter on it, and leave everything else `ffff`.
Their **world table is untouched entirely** (all rows dated 07/07–07/14/26), so
`OP_ZoneServerInfo` had to be hunted too.

### Recon note — the scoped-stats trap cost us the p9 evidence

The first pass read the fixture's paired `.opcodestats.txt`, which was recorded
**primary-box scoped**. EQL opens a fresh world socket per zone-in, so every
zone-in-shaped opcode showed exactly one fire across three zones, and
`OP_Consider` / `OP_GroundSpawn` showed **zero** — indistinguishable from "the
capture doesn't contain cons". Re-running with `--dump-all-sessions` took zone
opcodes 131 → 177 distinct and surfaced all of it. Always re-run unscoped before
concluding an opcode is absent.

### Independent shape prediction vs upstream (pre-registered)

Before reading upstream's table, each old id's fingerprint (size histogram +
direction split + count rank) was aligned against the 08/04 unknowns. That
predicted **10 of 12** p10 ids, all confirmed identical to upstream's independent
derivation. The two misses were the informative ones: `OP_PlayerProfile` had no
shape candidate (it is a single 44979 B fire), and `OP_SelfPos` was a miss
*because upstream is stale there* — the shape lead was right and their id was
dead. Agreement between two independent derivations is corroboration; each
disagreement was resolved against the packet.

### Confirmed — p10 (12/12)

| opcode | id | content anchor that confirmed it |
|---|---|---|
| OP_ZoneEntry | `44cb` | 883 fires; spawn ids reappear in the movement/HP/despawn streams |
| OP_PlayerProfile | `3e67` | 3 fires of 44979 B = exactly 3 zone-ins |
| OP_NewZone | `2570` | 3 fires carrying *different* zone names: `Toxxulia Forest`, `paineel`/`Paineel`, `hole`/`The Ruins of Old Paineel` |
| OP_ZoneChange | `4ab5` | 2 C>S fires of 484 B = exactly the 2 transitions |
| OP_ClientUpdate | `5b7d` | C>S 42 B ×703 + S>C 24 B ×993; both bodies re-derived (below) |
| OP_SelfPos | `61cb` | **hunted.** The ONLY opcode in the capture whose every payload length is `1 + N*17` (18/35/52/1684/1905/2143 → N 1/2/3/99/112/126), C>S only, zero competitors; 96 records decode to a continuous walk (median step 0.38u, **0 discontinuities in 95 steps**) |
| OP_MobUpdate | `0a2a` | u16@0 yields 301 sane spawn ids; used as position ground truth |
| OP_NpcMoveUpdate | `6c7b` | 2785 fires, 18/17/15 B — the bitstream fingerprint |
| OP_HPUpdate | `6854` | 6/21/37/53/7 B multi-size fingerprint |
| OP_SpawnAppearance | `6ef4` | 413 fires at the wide 24 B record, both directions |
| OP_DeleteSpawn | `20a9` | ids overlap the OP_MobUpdate id set |
| OP_RemoveSpawn | `048a` | 5 B ×84 + 4 B ×2, ids in the live set |

### Confirmed — p9 (6/6) and world

| opcode | id | content anchor |
|---|---|---|
| OP_TargetMouse | `4b90` | **hunted.** 18 C>S fires of 4 B; **5/5** distinct nonzero values are live spawn ids (cross-referenced against OP_MobUpdate's 301-id set), `0` = deselect. Competitors killed by the same test: `785f` scored **0/7**, `6cfb` is all zeros |
| OP_Consider | `4635` | 8 fires (4 C>S + 4 S>C) at 24 B; the 8 decoded con events name spawn ids `4227/3561/3685/3627` — **the same mobs OP_TargetMouse independently reported targeting** |
| OP_SpawnDoor | `1333` | 3 fires, one per zone |
| OP_GroundSpawn | `77d1` | 7 fires, 62/63 B |
| OP_CommonMessage | `0b3c` | 25 fires, variable; chat decodes as readable text |
| OP_ClickObject | `2d39` | ported from upstream; **zero fires in this capture** (no objects were clicked) — unvalidated, flagged |
| OP_ZoneServerInfo (world) | `5718` | **hunted.** 3 fires decoding to `lvseqns-livz10/livz09.everquestlegends.com` ports **2966/2779/1761** — exactly the three the daemon's own BoxRegistry fallback reported guessing (`no box announced port N`), in order. Mapping it drove that warning 3 → 0 |

### Structs re-derived

**`playerSpawnPosStruct` (S>C other-PC position): 28 B → 24 B.** An exhaustive
scan of **all 173 candidate 19-bit windows** independently picked the same three
upstream did — each the global best for its axis:

```
/*0000*/ u16 spawnId | u16 spawnId2
/*0004*/ u32 { x:19 (LOW, signed) | hi:13 }
/*0008*/ u32 unknown
/*0012*/ u32 { lo:13 | z:19 (HIGH, signed) }      <- z is high, unlike x/y
/*0016*/ u32 { y:19 (LOW, signed) | heading:11 @bit19 | hi:2 }
/*0020*/ u32 unknown
```

Validated non-confoundedly: per-spawn trajectory smoothness over 931 steps gives
a **4.00-unit median** (p90 21.6, 4 steps >500u); an x/y-transposed control
scores 6× worse against ground truth. Heading is **11-bit** (narrowed from 13) at
bit 19 of the @16 word — the bits upstream labels `deltaY` — scoring **5.77°**
median against travel bearing over 448 legs (next-best window 25.8°, random ~90°).

Do not judge this opcode by absolute error against OP_MobUpdate: it carries
*other PCs* while MobUpdate carries NPCs, so only 57 of 993 records overlap a
ground-truth track at all.

**`playerSelfPosStruct` (C>S self position): stayed 42 B, body rearranged** —
so no size gate could catch it. Offsets moved `y@10/x@22/heading@26/z@34` →
**`y@18 / heading@22 / z@30 / x@38`**. Pinned by range-matching each float
against the (independently re-confirmed) `OP_SelfPos` breadcrumb: @18
`[-2627.32, 1086.62]` vs breadcrumb Y `[-2627.32, 1086.62]`, @38 vs X, @30 vs Z;
the old offsets now read zero or ±4. A captured self-report decodes to **0.0000
units** from a breadcrumb point — cross-opcode agreement between two totally
different encodings.

⚠ **Upstream declares this struct 44 B** (`tail[2]` past `x`). The wire is
**42 B** — 703 C>S bodies, none at 44. Both payloads are gated `none`, so the
over-long declaration would not warn; it would just hand the parser a short
buffer. We keep `PAYLOAD_LEN = 42`.

Heading location moved but **width, scale and sense are unchanged**, so the
existing downstream inversion carried over untouched — **confirmed in-game
2026-08-05**, the reticle tracks the turn rather than mirroring it. The 2.14°
travel-bearing fit pins the *location* only: the sense must be calibrated on a
TURN, since facing-vs-bearing shares the frame and cannot see a mirror.

### Still open

- `OP_ClickObject 2d39` — ported, no fires to validate against.
- `OP_BuffList 6481` — mapped, but 176 B does not match its struct; p6, deferred.
- The 16 stale-upstream ids above stay `ffff` until upstream ships their update
  or we hunt them; they need a capture exercising group/loot/random/AA/inspect.

## ⚠ PATCHED 2026-07-14 — SECOND full opcode re-map (IDs rotated AGAIN), IN PROGRESS

EQL patched again 2026-07-14 and did **another full app-opcode id rotation** — every
id in the tables below (the 2026-07-07 set) now reads `unknown` against a post-patch
capture. Stream layer unchanged. **62 mapped opcodes** (43 zone + 19 world) need new ids.

Post-patch login+zone capture: `tests/replay/eql/eqlegends-patch20260714.vpk` (valid;
`.opcodestats.txt` has a payload-samples byte-prefix section for content anchoring, no
re-replay needed). Re-map is **IN PROGRESS** — working notes + full candidate table live
in the gitignored `captures/eql-remap-20260714-WIP.md` (kept out of git: capture-derived
spawn/player names).

**Method: content/value-match, NOT size** (sizes shift across patches too).

### ✅ Batch 1 — 11 re-mapped + wired (2026-07-14; content-confirmed, replay-verified)

All 11 were **pure id-swaps**: each opcode's decoder + struct binding is unchanged from
the 2026-07-07 set, only the id moved. Content-confirmed from the capture's payload-sample
bytes, then verified by replaying the patch capture — every one reads `known` at the
expected count/size with **no SZC struct-drop warnings**, daemon exit 0.

| new id | was | opcode | content anchor that confirmed it |
|--------|-----|--------|-----------------------------------|
| `0x65dd` | 0x1dbf | OP_NewZone | zone short + long name in payload |
| `0x144f` | 0x4606 | OP_ZoneEntry | S>C ~340-480B spawn-name blob; 92B C>S request both fit |
| `0x0b66` | 0x6805 | OP_ItemPacket | item `Benefit:` descriptor text + item count @4 |
| `0x713a` | 0x77ae | OP_BuffList | spawnId@0 + count@9 + `{spellId,1,remainTicks,0}` records + caster name |
| `0x021e` | 0x7352 | OP_NpcMoveUpdate | 18B movement bitstream, highest-freq (1203×), incl. its own 19B variant |
| `0xa5c0` | 0x2735 | OP_HPUpdate | multiplexed stat-sync channel, self-id@0 (6/21/37/53B) |
| `0x3348` | 0x67e0 | OP_MobUpdate | 14B spawnId@0 + packed position (`spawnPositionUpdate`) |
| `0x76e5` | 0x73de | OP_Action | spell id @4 = real ids (1448/734/700); 64B + 88B alt |
| `0x5446` | 0x1734 | OP_Action2 | 48B, paired with OP_Action for the same spell id |
| `0x2874` | 0x5c62 | OP_WearChange | wearSlot @4 = 7/8, material @8 (INERT, no handler) |
| `0x36bf` | 0x66cb | OP_Death | victim@0 / killer@4 / spellId@16 = `ffffffff` (melee) |

### 🔧 `0x5188` OP_ClientUpdate — S>C spawn positions WIRED (2026-07-14); C>S self deferred

The position channel rotated id (0x7171→0x5188) **and** wire size in both directions.

- **S>C 24B (other-spawn broadcast) — WIRED.** Re-cracked against the OP_MobUpdate
  ground-truth stream + self-trajectory smoothness. New packing (19-bit ×8, coord in the
  LOW bits of each word): **Z = `(u32@8 >> 13) & 0x7FFFF`**, **Y = `u32@16 & 0x7FFFF`**,
  **X = `u32@20 & 0x7FFFF`** (all signed; daemon applies `>>3`). `playerSpawnPosStruct`
  size override → 24 (PAYLOAD_LEN); `parse_player_spawn_pos` rewritten; unchanged handler
  path `EqlDispatch::playerUpdateOther → SpawnShell::moveSpawn`. Replay-verified: 0x5188
  reads `known` (680× S>C), no SZC drops, exit 0. **Confidence: Y & Z locked vs MobUpdate
  ground truth; X strong (smooth self walk + close spawns match) but not yet pinned to a
  `/loc` — final X confirmation + a possible X/Y transpose check pending a ground-truth
  capture.** Because it's derived by matching MobUpdate, spawns place consistently with
  the MobUpdate stream regardless.
- **C>S 38B (self-position report) — CRACKED + WIRED 2026-07-14.** Confirmed against a
  Lavastorm `/loc` ground-truth capture (`eql-locref.vpk`), 3 known points, exact match.
  **IEEE floats: gameX = `float@14`, gameY = `float@26`, gameZ = `float@10`; heading =
  `(u32@18 >> 8) & 0x1FFF`** (13-bit, 8192/circle; re-cracked 2026-07-15 vs a 360° spin —
  the low 8 bits are a sub-fraction, the 11 bits above are NOT a turn rate); `@0` is a float
  counter (~16640, +0.1/tick). Rewrote eql `parse_player_self_pos` for the 38B; size
  override → 38; `playerUpdateSelf` now applies straight to `m_player` (C>S is always
  self), gated only on `id()!=0`, dropping the defunct spawnId adoption/gate;
  `applySelfPosition` heading conv → `360 - ((h*360)>>13)`. **Velocity cracked 2026-07-17**
  vs a run-south-then-west `/loc`: deltaY@6 / deltaX@22 / deltaZ@30 (f32, ±~2.26 = full
  run), now applied. No turn-rate field (spin capture: facing sweeps while every delta
  reads 0 → EQL sends the absolute heading each frame).
  **Two known gaps (both patch-induced, follow-ups):** (1) the 38B carries **no spawnId**
  (old 42B had `spawnId@2`), so the self-id must come from `SpawnShell::zoneEntry`
  name-match — which needs OP_PlayerProfile for `realName()`; a capture/session without the
  profile never adopts a self-id, so the self marker stays hidden (correct, but the
  self-pos can't bootstrap the id anymore). (2) eql's in-zone death respawn sends no self
  OP_ZoneEntry, so post-death self-id recovery has no source post-patch (`death()`/
  `enterWorld()` still sever; `m_awaitingRespawnFromId` is now dormant).

- **`0x4fb6` OP_SelfPos (variable C>S) — SECOND self-pos opcode = position-history
  breadcrumb (characterized 2026-07-17; decoded, not surfaced).** Distinct from the 38B live 0x5188:
  a batched movement journal. Layout = **N × 17-byte record + 1 trailing byte**: `f32 y@0`
  / `f32 x@4` / `f32 z@8` (plain floats, /loc order) / `u8 seq@12` (1..2) / `u32 ts@13`
  (monotonic hi-res timer, ~65543/sample). Sizes 18B=1 rec (settled) … 2415B=142 recs;
  timestamps monotonic, samples trace a smooth walk (XY step ≤0.7); all 3 /loc points match
  exactly. **Xerxes maps this as `OP_SelfPosEQL=0x4fb6`** — correct id, but his
  `playerSelfState` gates on `len==38` so it never parses the 17B-record form; his self-pos
  (like ours) rides 0x5188. **Decoder kept, feature dropped:** a Rust decoder exists
  (`parse_self_pos_breadcrumb` + the `decode_self_pos_breadcrumb` FFI) and the id is mapped
  as `OP_SelfPos` in the eql toml, but it is NOT wired to a handler — a movement-trail
  overlay was built end-to-end (proto SelfPath + daemon emit + web polyline) then removed as
  laggy / low-value (redundant with 0x5188 for position). Recon: `--dump-payload 0x4fb6:PATH`
  → one .bin/fire → float-scan the 18B ones, then the 17-byte stride reveals the record array.

- **`0x6cbd`** (19B S>C) — first guessed an NpcMoveUpdate alt, but its layout is
  `u32@0 + spawnId@4`, not the movement bitstream (whose 19B variant already lives in
  `0x021e`). Different, still-unidentified opcode.
- **`0x2b30`** — a **zone-stream** C>S 2400B XML `<SystemFingerprint …>` hardware-
  attestation blob, NOT the world OP_SendLoginInfo handshake. Wrong stream + wrong shape.

### ✅ Guild-in-zone tier WIRED (2026-07-20) — spawn guild tags

`OP_GuildsInZoneList 0x3959` + `OP_NewGuildInZone 0x6c4c` now feed `GuildMgr`, which
resolves a spawn's `(guildID, guildServerID)` into a guild name. Both were already
content-confirmed in the 07-14 re-map; this entry is the wiring + verification.

**Decoded in eql's OWN Rust parser** (`seq_backend_eql::guild_in_zone`), like every other
eql opcode — even though the wire happens to match the stock Live struct today. "Byte-
identical" is never a licence to ride a shared parser: eql owns its decode so a Live-only
change can't silently corrupt it, and the daemon's rule is that Rust is the only decoder
(no C++ NetStream parse). ⚠️ **The 2026-07-20 version wired this to the shared C++
`GuildMgr::guildsInZoneList`/`newGuildInZone` NetStream parsers — a rule violation, fixed
2026-07-21.** The daemon now calls `seq::rust::decode_guilds_in_zone_list` /
`decode_new_guild_in_zone` in `EqlDispatch` and feeds the neutral `GuildMgr::learnGuilds`;
the C++ wire slots are deleted. Behaviour is byte-identical (goldens unchanged). Layout,
confirmed by `--dump-payload` across 15 payloads in 4 post-07/14 captures:

| opcode | layout | evidence |
|--------|--------|----------|
| `OP_NewGuildInZone` | `u32 guildId, u32 serverId, cstring name` | 30B payload = 8 + 21-char name + NUL; sizes 13/23/26/30 all fit exactly |
| `OP_GuildsInZoneList` | `u32 name_len, name (no NUL), u32 count, count×{u32 guildId, u32 serverId, cstring}` | `count` matched the actual entry count in every sample (0,1,2,3,6,7); 12B = the empty case (4+4+4) |

`serverId` is a constant `13` in every capture so far. Unguilded spawns carry
`guildID 0xFFFF` (not 0) — `guildIdToName` misses the map and returns `""`, so the
sentinel is harmless today, but it does reach proto consumers as `guild_id: 65535`.

**Downstream chain already existed** (`GuildMgr::learnGuilds` map, its `guildTagUpdated`
signal, and `SpawnShell::updateGuildTag`'s back-fill). Gaps closed: `wire_eql.cpp` binds
the two opcodes to `EqlDispatch` per-box (NOT under `wireGlobalSinks` — see the per-box fix
note below; each zone-in opens a fresh box and the guild map is daemon-wide, ignoring known
keys); `SpawnShell::upsertSpawn` gained `guildServerID` (the Rust
parser and cxx bridge already decoded it — the daemon was dropping it, so every eql spawn
keyed as server 0 and could never resolve); and `upsertSpawn` now resolves the tag at all,
which the eql path never did. New proto `Spawn.guild_tag = 25` carries the resolved name.

Verified on replay: 6 of 10 eql fixtures resolve tags (48 in `eql-fighting`), and the
back-fill count matches exactly — `eql-locref` +1 spawn re-add / 1 tag, `eql-spin360`
+2 / 2, `eql-fighting` +12 back-filled of 48 (the rest resolve at spawn time).
`eql-group` resolves 0 correctly: its one guilded spawn is guild 101, never named on the
wire in that capture.

**Golden determinism:** `guilds2.dat` persists and reloads, so a warm cache resolves tags
earlier than a cold one and shifts the envelope stream (two individually-stable states,
12 envelopes apart on `eql-fighting`). `check.sh` now deletes the cache per fixture so
goldens always encode the cold path and reproduce on a fresh clone / in CI.

### ✅ Guild ROSTER tier WIRED (2026-07-21) — `OP_GuildMemberList 0x591d`

The roster now decodes into `GuildShell` and ships as a new `GuildRoster` proto message.
Unlike guild-in-zone, the eql wire here **diverges from the stock struct**, so the shared
NetStream walk cannot be reused — the parser lives in `seq-backend-eql/guild_roster.rs`
and reaches core through a neutral `GuildShell::setRoster()`.

Layout (per the legends branch `guildshell.cpp`, byte-verified against a captured
122-byte 2-member roster):

```
header  LPText requesterName, u32 guildId, u32 unknown, u16, u32 count
record  LPText name, u32 level, u32 bankerFlag, u32 classMask, u32 rank,
        u32 lastOn, u8 tributeOn, u8 trophyOn, u32 tributeDonated,
        u32 tributeLastDonation, u8 fullMember, LPText publicNote,
        u16 zoneId, 4 unread
```

`LPText` = u32 length + unterminated bytes. Three divergences from live: the header is a
field wider (`u32+u32+u16`, not `u32+u32+u8` — that single byte desynced the whole stock
parse), the class slot holds the **multiclass bitmask** (bit N = class N; display the
lowest set bit), and each record ends with a `u16` zone id live left unread (0 = offline).
`bankerFlag` packs two flags: 0 none, 1 banker, 2 alt, 3 alt banker.

**Validation:** both captured records land with zero slack — record 1 ends exactly where
record 2's length prefix begins, record 2 exactly on the payload length. The parser
enforces that end-of-payload landing as a canary and returns nothing if the walk drifts,
so a layout change fails loudly instead of surfacing a half-read roster. Replay-verified
on 3 fixtures: guild 454, 2 members, level 24, masks `0x4006`/`0x0502`, ranks 1/2, one in
zone 50 and one offline.

Roster rows are emitted **sorted by name** — `GuildShell` keys members in a QHash, whose
order is seed-dependent and would flap tier-2 goldens.

`GuildShell` was dead code before this (compiled, never instantiated); it now lives in
`ManagerSet` because a roster is per-character, unlike the daemon-global `GuildMgr`.

### ✅ Guild MOTD tier WIRED (2026-07-25) — `OP_GuildMOTD 0x5924`

The guild message-of-the-day now decodes into `GuildShell` and ships as a new proto
`GuildMotd` message. Fixed layout, matching the stock struct but decoded in eql's own
`seq_backend_eql::guild_motd` parser (isolation rule — no stock struct cast):

```
u32, u32, char target[64], char sender[64], u32, char message[516]   (656B)
```

`sender@72` = who set it, `message@140` = the text, both NUL-terminated within their fixed
width. `target@8` is the recipient (self) — decoded past but not surfaced (a char name, and
redundant). The packet carries **no guild id**, so `GuildShell` stamps the MOTD with the
guild id from the roster it already tracks (0 if the MOTD precedes the roster).

⚠️ **Every captured MOTD is EMPTY** (the fixture guild has none set): message and sender
both zero-length across all 10 fixtures. So the wiring, the empty-state path, and the
`guild_id` association are golden-verified, but **non-empty text is only unit-tested**
(`guild_motd.rs::reads_sender_and_message`), never seen on a real capture. The daemon
plumbing past the parser is trivial string-forwarding.

Emitted on receipt (like the roster, re-sent per zone-in — `eql-fighting` fires it 12× over
its zones) plus once on subscribe when a MOTD has arrived. Deterministic: the 10 goldens
each gained their `guild_motd` envelopes and stay stable over 3 runs, the 3 semantic-
compared captures included.

### 2026-07-24 — OP_LoadoutSwap re-mapped `0x7477` → `0x631c` (post-07/14 rotation), WIRED

The 07/14 remap rotated OP_LoadoutSwap too, but it was left `ffff` (unmapped) in
`opcodes.toml` (dated 07/11) — so it silently decoded for **nobody** (daemon or scry)
since the patch, despite both having a live `decode_loadout_swap` handler.

**Content anchor:** `0x631c` appears in `eql-stance-sweep` as `unknown  S>C 486B` (n=1),
with no competing unknown at that size+dir. 486B sits exactly in the loadout
broadcast-variant range (486–491B) noted at the original 07/11 find. Decoding the payload
with the existing `parse_loadout_swap` is decisive: header `spawnId@0` = 5843, `u8@4`,
`u16 innerLen@5`, then a ZoneEntry-format record whose name field at offset 7 is a **clean
NUL-terminated PC name**, and the parse yields sane fields (level 10, class 1/Warrior,
race 330 — a valid special-humanoid race). A size-only false match cannot produce a
well-formed name + coherent identity, so n=1 is sufficient here (method:
[[feedback_opcode_disambiguation]] — count + zero-competitor + content match).

Only the **broadcast** (nearby-player, ~486B) variant is present in this capture; the
**self** (~118KB, +inventory tail) variant is confirmed by structure/parser-reuse, not
directly re-observed post-07/14. Fix is the id alone (`ffff` → `631c`); the decoder,
struct binding, and both dispatch handlers were already in place. Replay: all 10 eql
goldens unchanged (the one broadcast packet is a no-op — its target spawn isn't tracked at
that instant), daemon exit 0. Also unblocks scry, which reads the same `opcodes.toml` and
now routes it through `EqlBackend`/`Event::LoadoutSwap` → `World.apply_loadout`.

### Dormant-handler audit + legends cross-reference (2026-07-24)

After the LoadoutSwap find (`ffff` → a real id un-dormanted a wired handler), audited the
whole table for the same shape: opcodes **bound to a handler in `wire_eql.cpp` yet sitting
at `ffff`** (inert). Twelve exist. Cross-referenced each against the legends branch
(`upstream/showeqlegends` @905a14f, post-07/16 so its ids are post-remap) — the authority
for EQL. Result: legends has NOT "found everything" — it solved two we lack and is stuck
on the same six we are.

**Legends solved these; we're missing them:**
- [x] `OP_ClickObject` — legends `0x597e`, MAPPED 2026-07-24. It's dual-direction: our
      fixtures only have the C>S 16B click request (`eql-fighting`, ×24), which we ignore
      like legends (S>C-only). Split the payload `server`/remDropStruct (the 12B removal we
      decode) + `client`/uint8_t absorber so the C>S no longer trips the size-diagnostic.
      The S>C removal side wasn't captured, so the decode rests on legends' authority + the
      pre-existing handler, not a local S>C sample.
- [ ] `OP_LevelUpdate` — legends `0x0d68`, and **checking the legends *code* settles it:
      it IS a real level packet, not the wrap heuristic.** `interface.cpp` wires it to both
      `messageShell::updateLevel` (ding text) and `player::updateLevel` (sets level), with
      the comment "the Legends level-up packet is an 80B widened container whose first 12
      bytes are the stock levelUpUpdateStruct (level@0, levelOld@4, exp@8)". Matches our
      capture (`0x0d68` S>C 80B). So **our "no discrete level opcode / exp-wrap heuristic"
      note at the 2026-07-10 entry is the stale one** — 0x0d68 is the real packet and could
      replace the heuristic. Deferred: decode the 80B and decide whether to switch.

**Legends-corroborated — our mapped-but-undated ids are right, mark validated:**
- [x] `OP_GuildList` `0x2efb` — legends agrees.
- [x] `OP_MOTD` `0xf8e0` — legends agrees.

**Naming alignment only (no functional gap):**
- [ ] `OP_SpawnAppearance` — legends' name for `0x4170`, which we already map + wire as
      `OP_SpawnAppearance2`. Optional: rename to match upstream ([[feedback_match_upstream]]).

**`ffff` on legends too — genuinely open for everyone (need a capture with the event):**
- [ ] `OP_CorpseLocResponse` (corpse `/corpse`) · [ ] `OP_DzInfo` · [ ] `OP_DzSwitchInfo`
      (dynamic-zone/instance info) · [ ] `OP_GroupDisband2` · [ ] `OP_Shroud`

**`OP_SpawnRename` = `0x504f` — CONFIRMED but NOT activated (2026-07-24).** Found in
`eql-contarget`: `0x504f` S>C, exactly 195B = `spawnRenameStruct` (3× 64B name fields +
3 bytes), sole 195B unknown, zero competitor. Content-decisive — the payload is a coherent
pet rename (identical `old_name`@0/`old_name_again`@64, the owner's-pet form @128). `ffff`
on legends too, so a genuine new find. **Left `ffff` deliberately:** mapping it activates
`SpawnShell::renameSpawn`, and because the renamed spawn is a summoned PET, its re-emission
aggravates the known load-only summoned-pet count-flap ([[project_eql_golden_spawn_order_flap]]):
serially at low load `eql-contarget` records byte-identical 4/5 WITH the map vs 5/5 for the
untouched baseline — a ~20% flap that would false-fail the pre-push hook. The determinism
fix (`QHashSeed::setDeterministicGlobalSeed` + packet-time `nowMs`) IS present and holds for
the baseline; the rename tips the same close-call pet-spawn ordering the count-flap rides
(the pre-push hook already passes those long captures on a low-load moment, not a lucky
byte-cmp). So: mapping `0x504f` is a one-line toml + `spawnRenameStruct` size_override, but
it needs the underlying count-flap made robust first (or those captures moved to the
wallclock skip list). Nice-to-have display feature; low priority vs the flap fix.

**Intentional `ffff` on both — EQL routes the data elsewhere, LEAVE as-is:**
- `OP_EndUpdate` (endurance rides the stat-sync channel `0xa5c0`) · `OP_GroupMemberList`
  (roster rides `OP_GroupFollow`/`OP_GroupDisband`) · `OP_MobHealth` (HP rides stat-sync).
  All three are `ffff` on legends too, confirming the divergence.

The remaining ~149 `ffff` entries are inherited legacy opcode *names* never EQL-mapped
(bazaar / trade / LFG / housing, 2005–2019 dates) — correctly `ffff`, no handler behind
them, not part of this audit.

### Still open

Ambiguous small-payload clusters (4B / 8B / 12B / 24B), the chat trio, spell casts,
and the rest of the ~40 unmapped opcodes — see the WIP doc. Remaining guild work:
the popout-only web panel + the scry port. **`OP_GuildMemberUpdate 0x0717` is deliberately NOT wired** —
the legends branch found its zone/lastOn tail is uninitialized server memory (a logoff
sends a garbage zone like 5376), so only the roster is authoritative for zone/lastOn and
the update's sole trustworthy field is rank. The new
OP_FormattedMessage (`0x3c0a`) was handled separately (addendum-11 work against the
pre-patch fixtures — left as-is). Next: resolve the clusters → finish the table →
record a golden for `eqlegends-patch20260714` once the table is stable → check.sh.
(Pre-07/14 eql goldens exercise the old ids and will diverge until the table is done +
regenerated — expected during the rotation.)

## ⚠ PATCHED 2026-07-07 — full opcode re-map (IDs shuffled + some layouts changed)

EQL patched 2026-07-07. **Every app opcode ID moved** (stream layer unchanged), and
at least ClientUpdate + ZoneSpawns **struct layouts also changed**. All `0x...` ids in
the pre-patch "Confirmed" sections below are DEAD — kept for method/evidence only.
Re-mapped from a **Nektulos Forest** login+zone capture (server/world name is
"rivervale"; a `.vpk`, so `--list-events` has real capture-time) with in-game ground truth: player `/loc`
`2246.50,-954.77,-4.97`; con targets Dragoon_J`len(25225,L50,amiably),
Sergeant_C`Orm(11626,L50,amiably), Vol_T`Vke(12220,**L60,warmly**); NPC locs
C`Orm `2324.94,-990.11,-4.92` + Vol `2337.45,-802.35,-5.86`.

⚠ **Coordinate gotcha (read before touching positions):** EQ `/loc` prints **(Y, X, Z)**,
not (X, Y, Z). The position offsets in this doc label wire fields by GAME axis
(`gameX`/`gameY`); the `seq-backend-eql` parsers bind `x=gameX, y=gameY` to match the
daemon's neutral Spawn convention (which negates X/Y for screen — `protoencoder`). The
first re-derivation matched decoded values to `/loc` *in order* → bound x/y **backwards**;
everything was self-consistent (cross-checks passed) but X/Y-transposed vs the map. Fixed
`60b2a79`. Live is unaffected — the swap lives in the eql backend, which is the point of
per-backend decoders.

| Opcode | pre-patch | **new id** | status |
|--------|-----------|-----------|--------|
| OP_TargetMouse   | 0x1bfe | **0x2867** | ✅ confirmed 19/19 (value-match; 0=untarget). Layout unchanged (`{u32 spawn_id}`) |
| OP_Consider      | *(new)* | **0x4212** | ✅ NEW. 24B `{u32 self=27090, u32 target, u32 faction, u32 =7}`; C>S req has faction=0, S>C reply fills faction (**2=warmly, 4=amiably**; level comes from spawn). Companions: `0x5b5e` target-HP reveal `{u32 target, u32 cur_hp,…}`, `0x0e54` `{0,target}` |
| OP_ClientUpdate  | 0x0b03 | **0x7171** | ✅ **C>S 42B self FULLY DECODED**; **S>C 28B = other-spawn positions — CRACKED + WIRED 2026-07-10** (x/y/z are 19-bit ×8 packed, NOT the byte-aligned floats the 2026-07-08 note guessed → `EqlDispatch::playerUpdateOther`→`SpawnShell::moveSpawn`; see entry below). C>S: spawnId u16@2; wire has gameY@10 / gameX@18 / z@30 (f32) → bound **x=gameX@18, y=gameY@10**; velocity deltaX@26 / deltaY@6 (f32); **heading = u32@14 & 0x7FF, 11-bit (0–2047, North≈0)**, deltaZ @34. (X/Y-swap fixed `60b2a79`.) **Parser realigned to this layout 2026-07-11** (`player_self_pos.rs` had drifted to x@26/y@6/z@22 = the velocity/zero fields → decoded the PC at ~origin, wrong since ≥07-07). MASKED, so the fix is output-neutral: `applySelfPosition` fires (247×/login-zone) but its `m_player` update never becomes a client `spawn_updated` — the PC marker rides the own-spawn/`NpcMoveUpdate` path. heading re-confirmed R=0.88 vs the PC's own movement direction |
| OP_ZoneEntry     | 0x7475 | **0x4606** | ✅ id (var 343–352B NPC / 486B rich). **Renamed OP_ZoneSpawns → OP_ZoneEntry 2026-07-09** (stock SEQ: the s2c OP_ZoneEntry has carried the per-spawn payload — one fillSpawnStruct per packet — since 2008; OP_ZoneSpawns is the dead bulk-array op). Same id/decoder (EqlDispatch::spawn), stock-live name. level@block+4 OK. **POS from block END**: three u32 words z@(len-95), **y=gameY@(len-91), x=gameX@(len-87)**, each a **signed 19-bit fixed-point (×8) coord in the word's low bits** (Live spawnStruct-style packing; upper 13 bits = other subfields). The earlier `/8 i16` read was the same field truncated — wrapped past ±4095. hp/body offsets TBD. **C>S 92B = client zone-entry / spawn-list request** (name@4 + session token + fixed signature block; identified 2026-07-08, see entry below) |
| OP_MobUpdate     | 0x061b | **0x67e0** | ✅ id (14B, ids 416/416 match spawns). **Byte-identical to Live `spawnPositionUpdate`** (2026-07-08): spawnId u16@0 + 2 zero bytes, then packed `y:19 z:19 u3:7 x:19` (fixed-point ×8, u64@4) + `heading:12`@12 — decoded by the shared `decode_mob_update`, full range, no wrap. The old i16-offset read (y@4/8, z@6/64, x@10 "unscaled") was a truncated window of these bitfields. Sparse full-position sync; continuous movement is OP_NpcMoveUpdate |
| OP_NpcMoveUpdate | 917c (dead) | **0x7352** | ✅ **2026-07-08**. The continuous per-NPC movement stream (var 15–21B, top S>C opcode: 734 fires/51 ids vs MobUpdate's 591). **Byte-identical bit format to Live** OP_NpcMoveUpdate → decoded by the shared `decode_npc_move_update`; only the opcode-id mapping was wrong (917c never appears on the wire). Carries pos **+ velocity + heading**. Confirmed: all 51 ids (16b BE spawnId) + decoded x/y/z match the 0x67e0 stream. Fixes "mobs freeze while moving, jump on stop" |
| OP_ItemPacket    | 0x74b0 | **0x6805** | ✅ id (bulk items; names + `Benefit:`/`Trophy:`) |
| OP_ZoneServerInfo| —      | **0x35d4** | ✅ world 130B, zone-server host `…everquestlegends.com` |
| OP_EnterWorld    | 0x0839 | **0x26bf** | ✅ **2026-07-08**. World C>S 72B, **byte-identical to Live**: char name zero-padded @0 + 64B zeros + `0xffffffff` trailer; server reuses the opcode for a 1B S>C ack. Confirmed 2/2 login captures via dump-payload (name = known char). Mapping it lights up NamePromoter on eql (matches by opcode name + 72B len) → boxes are named at the **world handshake**, before zone-in; `--only-session <charname>` relays from that point. Prior 9bdc was a dead pre-patch guess. **C>S-wired to `EqlDispatch::enterWorld` 2026-07-11 for instance re-entry**: a private instance (or any zone that reuses the world socket) re-handshakes here on the **same** world socket → BoxRegistry keeps the SAME box, so no active-box roll re-primes the web and the instance can share the old zone's short name (its `zone_changed` a client no-op). enterWorld clears the box's spawns + drops the self-id (gated on an established session so the login EnterWorld is skipped); the instance's NEW self-id then re-adopts via `setPlayerID`→`changedID`→`SessionAdapter::onPlayerIdChanged`→a fresh Snapshot — no extra plumbing. Fixes "entering a private instance doesn't refresh zone/spawns/id, mobs still move". Needed a client `[[world.payloads]]` entry in `conf/eql/opcodes.toml` (it had none, so the handler silently didn't bind — `dispatchFor: no matching payload`) |
| OP_ChatServer    | —      | **0x4de7** | world 67B, chat connect `host,9877,rivervale.<char>,token` |
| OP_PlayerProfile | 0x5207 | **0x62f0** | ✅ id (~40KB, embeds char name + inventory). name-stub = `0x46df` (656B). race u32@21 / class u32@25 / level u8@33; **name via anchor-scan → authoritative eql box name (2026-07-09)** |
| OP_NewZone       | 0x5ab6 | **0x1dbf** | ✅ **WIRED 2026-07-08**. Current zone as packed null-terminated `short_name` + `long_name` **text** (S>C, ~340B, once per zone-in) — the earlier "no zone-name text on the wire" sweep missed it. 3-way confirmed: guktop/"The City of Guk", nektulos/"Nektulos Forest", unrest/"The Estate of Unrest" (each a different length). Fires AFTER the profile + spawn bulk → `EqlDispatch::newZone`→`ZoneMgr::setZoneByName` via the new **`zoneResolved`** signal (map/filter/web, no spawn-clear/reset). Replaced the profile-@36211 hack; the old 0x4bc8 was the BIND zone (now `OP_ZoneBindMarker`) |
| OP_Action2       | 0x32a9 | **0x1734** | ✅ **WIRED 2026-07-08**. Combat/damage stream (S>C, 48B, n=481/968). **Byte-identical to Live `action2Struct`**: target u16@0, source u16@2, damage i32@8, spell i32@20 (**-1=melee** for 422/481), type u8@40; @24–39 = knockback floats. Sole 48B S>C op. Lit up the already-wired `CombatRouter::action2` → replay emits **481 CombatEvent envelopes** (real src/tgt/dmg, e.g. 13167→13154 dmg107) |
| OP_Action        | 0x049e | **0x73de** | ✅ **WIRED 2026-07-08**. Spell/special action (S>C). **Live paired send**: 64B `actionStruct` (n=97/117) + 88B `actionAltStruct` (n=22/43); target/source @0/@2, spell u16@4 (real ids 502/445/821…). Sole 64B/88B op → `SpellShell::action` (both payloads), 119 fires |
| OP_DeleteSpawn   | 0x94d4 | **0x59a1** | ✅ **WIRED 2026-07-08**. Spawn removal/death (S>C, 4B `{u32 spawnId}` = `deleteSpawnStruct`, n=9). Time-correlated: **8/9 ids stop receiving MobUpdate at/after the event** (mobs killed in combat). → `SpawnShell::deleteSpawn`; replay emits SpawnRemoved for all 9. ⚠ `0x67a8` is the 4B look-alike but = combat **engage/disengage** (ids keep moving after; id=0 clears) — NOT despawn |
| OP_Death         | 0x1eb2 | **0x66cb** | ✅ **WIRED 2026-07-08**. Death/corpse (S>C, 40B = `newCorpseStruct`, n=8). Decodes field-for-field: victim u32@0 (**all 8 ∈ the DeleteSpawn set**), killer u32@4 (=player 13167 every kill), corpse type@12, killing-blow spell u32@16 (-1=melee), damage u32@24 (9–80). → already-wired `SpawnShell::killSpawn`; replay emits **8 `SpawnKilled`** (0→8). Fires just before the matching DeleteSpawn |
| OP_Animation     | 0x6dba | **0x1293** | ✅ **2026-07-08** (id-only, no handler). Animation broadcast (S>C, 4B, n=354/718). **Live `animationStruct`**: spawnId u16@0, action u8@2 (1–46), speed u8@3 (**=10 across all 354**). Remapped so it resolves in opcode-stats; animation isn't surfaced, so no handler wired |
| *(target HP reveal)* | — | **0x5b5e** | ⚑ 2026-07-08 candidate (unwired). On-target HP reveal (S>C, 13B primary, n=12/22): `{u32 spawn_id, u32 cur_hp, u32 =0x07000001, u8 0}`; cur_hp is **absolute** (=0 for a just-dead spawn). Consider-companion (cf. 0x0e54 `{0,target}`). NOT a continuous %-HP bar stream |

### 2026-07-14 — OP_FormattedMessage = `0x3c0a` (arg-bearing text: spell interrupts/casts + floaters), WIRED

**OP_FormattedMessage = `0x3c0a`** (S>C, variable 13..70+B). Previously `unknown` in
opcode-stats — EQL formatted messages were not decoded at all. The EQL layout **diverges
from Live** (`formattedMessageStruct` puts the format id at offset 5): capture-verified
against `eqlegends-corpsepin` (750 pkts, `--dump-payload 0x3c0a`) the wire is

```
u32 spellId  @0   0xffffffff = non-spell; a real spell id on spell classes (233 Expulse Undead, 74077 Blooming Heal)
u8  msgType  @4   message-class discriminator (multiplexed — see below)
u32 spawnId  @5   actor spawn id (the player self-id sits here on self-directed msgs)
u32 formatId @9   eqstr format-string id (439 interrupt/heal, 173/12478 cast, 15566 floater)
args         @13  NUL-terminated substitution fields; link fields are \x12-bracketed caret EQ links
```

`msgType` **multiplexes** onto one opcode: 7/5/8 = overhead damage/heal **floaters** (a
bare number, fmt 15566, ~89% of volume — suppressed, not chat); 0/1 = spell
cast/heal/interrupt/resist text (fmt 173/439, caret spell-link arg); 1+name = NPC-cast-at-you
(a name arg then a spell-link arg). Layout matches the community f-patch **addendum 11**
(credit Xerxes); our work = capture verification + full daemon/Rust/proto/web integration.

Full stack: `seq-backend-eql` `parse_formatted_message` rewritten to the EQL offsets
(returns spellId/msgType/spawnId/formatId + NUL-split args `Vec<String>`; neutral names —
the crate is the namespace); the shared `FormattedMessage` cxx struct enriched (empty on
live/test) with `decode_formatted_message` cfg-branched (**reuses the existing FFI**, live/test
byte-identical); `EQStr::formatMessage(uint32_t, QStringList)` overload strips the `\x12`
markers and re-encodes to the `{u32 len,bytes}` blob so the proven `%T`/`%N` + caret cleanup
is reused; `MessageShell::formattedMessageEQL` suppresses floaters, routes
`spellId != 0xffffffff → MT_Spell` else `MT_General`, and **synthesizes a ChatColor**
(`CC_User_Spells` 264 / `CC_User_Default` 273) since 0x3c0a carries no wire colour — so the
web's existing `cc:264 → 'Spell'/Spells` mapping renders it with **no web change**. Wired via
`conf/eql/opcodes.toml` (`ffff → 0x3c0a`, SZC_None) + `wire_eql.cpp` rebind to
`formattedMessageEQL`.

Verified: replay of `levelup`/`upperguk`/`chat`/`corpsepin` (none the derivation capture)
produces clean `"X spell is interrupted."` / `"X spell fizzles!"` / `"You regain your
concentration…"` under MT_Spell(26)+cc264, non-spell (forage/quest) under MT_General(19), no
floater spam; a headless WebSocket client received **1151 chat envelopes** live over the wire.
eql `check.sh` **5 pass / 0 fail** (regen'd `upperguk-20260707` + `chat-20260708`, diff purely
additive to MT_Spell); live `check.sh` 14 pass / 0 fail (shared-core safe).

> **Note — post-2026-07-14 patch.** EQL patched again on 2026-07-14 and the app opcode IDs
> rotated, so `0x3c0a` is the **pre-patch** id (correct for the fixtures above). The **layout
> is patch-stable** — only the id moves — so the re-map (next todo) is a one-line
> `conf/eql/opcodes.toml` id change; the parser/handler stay.

### 2026-07-13 — dispatch hardening fallout: SpawnDoor + AAExpUpdate WIRED, mob-lock revived, TargetMouse dedup

`dispatchFor` no longer silently binds a handler to the wrong payload on a
(dir, typename, sizecheck) mismatch — it warns and registers nothing (new tier-1
`packetstream_dispatch_test`). Turning that on surfaced and fixed four latent eql/live
wiring bugs, and unblocked the two deferred decoders:

- **OP_SpawnDoor `0x71ca` WIRED.** eql door rows are **132B**; the first 88B are
  byte-identical to Live's `doorStruct` (name[32]@0, y/x/z/heading f32 + incline u32
  @32–51, 20B field copy, size u32@72, doorId/opentype/spawnstate/invertstate@80,
  zonePoint u32@84 — `0xffffffff` = none), trailing unknown 44B vs Live's 48B. Derived
  from eql-ab-zone dump-payload (2×660B = 5×132 rows, e.g. `GIANTLEV` lever y/x/z
  ≈ -1237/-537/+14, heading 256.0, size 100, doorId 5, opentype 40). eql owns
  `parse_door` (132B, offset-based) + a `doorStruct` size override; the array stride is
  backend-owned via the new bridge `door_stride()` (136 live/test, 132 eql) because
  `newDoorSpawns` otherwise strides `sizeof(doorStruct)`. Replay: ab-zone +10 DOOR
  spawns ("Door: GIANTLEV (5)"), chat capture +102, login-zone +11.
- **OP_AAExpUpdate `0x42d1` WIRED** straight to `Player::updateAltExp` — the "needs a
  0-100000→330 conversion" deferral was stale: the handler has been 0-100000-native
  since `ac918ec`. eql layout `{u32 altexp 0-100000, u32 aaUnspent, u32 tail}`; first 8
  bytes match Live's `altExpUpdateStruct` and the tail (Live: u8 percent + pad) is
  unread. 12B size override declared. Bonus find: `updateAltExp` was wired NOWHERE —
  the live wiring was lost in the showeq-c extraction, so the AA bar only refreshed at
  zone-in on live too; re-wired on both backends (live fixtures gain player_stats
  envelopes, e.g. login-charmgmt 21→36).
- **OP_SpawnAppearance2 `0x1bdc` mob-lock was DEAD on eql** — a stale `ffff` Live-copy
  duplicate entry in `conf/eql/opcodes.toml` shadowed the real `1bdc` entry in the
  name-keyed lookup, so `updateSpawnLock` bound to a never-firing opcode. Duplicate
  deleted (dup-name sweep of all three TOMLs: it was the only one). Live's own entry was
  flipped `uint8_t/none → spawnAppearance2Struct/match` (server-only) so the wire binds
  exactly instead of via the removed fallback.
- **OP_TargetMouse `0x2867` double-fire fixed** — wire_eql carried both the eql
  DIR_Client wire and a dormant Live-copy `DIR_Server|DIR_Client` duplicate; every C>S
  target select fired `clientTarget` twice (goldens: targeted 286→143 / 38→19 / 52→26 /
  22→11 across fixtures). The Live-copy wire is removed.

Also: mapped `SZC_Match` gate-size audit is now CI-enforceable (`--strict-gate-sizes`,
wired into the per-target opcode-load smoke), and `OP_GroundSpawn` decode was revived on
LIVE (its wire asked `makeDropStruct/SZC_Modulus` vs the TOML's `none`; the old fallback
bound it to the client payload = dead handler — live fixtures gain "Drop:" spawns).

### 2026-07-12 — OP_SimpleMessage = `0x50a7` (canned server-string channel), WIRED

**OP_SimpleMessage = `0x50a7`** (S>C, fixed 12B `simpleMessageStruct` `{u32 eqstrId,
u32 color, u32 0}`) — the server telling the client to print a canned `eqstr_us.txt`
string by id (the string never crosses the wire). Was pinned to the dead Live opcode
`ffff`, so the already-wired handlers never fired. Identity is unambiguous: across the
pcap library it is a **12B S>C** op in every capture that carries system messages and
none of the pure-movement ones (`0x50a7` counts: 224 death-respawn, 41 fulllogin, 13
upperguk, …), and it was one of the 13 candidates **tested-and-rejected** as the mob-HP
carrier — consistent with a string channel, not a stat feed. Category-free anchor: the
fast-camp-refusal string (eqstr 10031, color 13). A single general channel, not the
per-category split the earlier pet-only trigger suggested.

Wired by remapping `ffff → 0x50a7` in `conf/eql/opcodes.toml` (SZC_Match) **plus** a
`simpleMessageStruct` entry in `seq-backend-eql` `size_overrides()` so the 12B gate is
eql-owned (no `BACKEND GATE-SIZE` warning; de-piggybacks the Live sizeof). Handlers were
already correct: `MessageShell::simpleMessage` (→ `decode_simple_message` + eqStr lookup
→ `chatMessage` proto) and the second `SpellShell::simpleMessage` receiver (clears a
stuck just-cast timer on spell-failure strings). Verified by replay: `eql-ab-zone` gains
4 `chat` envelopes with real strings (e.g. "You avoid the stunning blow." color 10);
tier-2 eql goldens regenerated (`eql-ab-zone`, `eqlegends-chat-20260708`,
`eqlegends-upperguk-20260707`), check.sh **5 pass / 0 fail** stable. Mirrors the legacy
`showeq/` fix (external port addendum 7, rev 2026-07-12a). Complement: `OP_FormattedMessage`
`0x3c0a` carries argument-bearing strings (e.g. spell-interrupt eqstr 439), a distinct
struct — do not conflate.

### 2026-07-11 — OP_LoadoutSwap = `0x7477` (multiclass class/level refresh), WIRED

**OP_LoadoutSwap = `0x7477`** (S>C, variable). Sent when a player switches loadouts
(the Legends multiclass class/level change) — no `OP_PlayerProfile` follows, so this
is the sole source for the new identity. Header `u32 spawnId | u8 | u16 innerLen |
<record> | <inventory tail>`; the embedded record (`data[7..innerLen]`) is byte-
identical to the `OP_ZoneEntry` (0x4606) spawn record, so `parse_loadout_swap` reuses
**`parse_zone_spawn`** (eql's parser — NOT the Live-format `parse_spawn`, which silently
mis-decodes) and surfaces the fields a swap changes: **level + class**. Two variants:
self ~118 KB (own refresh, with inventory tail) and a short ~490 B **broadcast** the
server sends nearby clients for ANY in-range player's swap (no tail). Confirmed firing
2026-07-11 on login-zone (2×) + levelup (3×), 486–491 B; decode verified against the
same character's 0x4606 record (race/class match, level tracks the swap — e.g. id 27034
race=12 class=12, level 14→10). Wired: `EqlDispatch::loadoutSwap` → self routes to
`Player::setIdentity`, broadcast to the new neutral `SpawnShell::updateSpawnIdentity`
(level+class in place, no position; mirrors `updateSpawnHP`). eql goldens 5/0 (broadcast
targets weren't tracked in these captures, so no golden delta — decode-verified, effect
via real play). Ported from the external legacy port (rev 2026-07-10a); credit Xerxes.

### 2026-07-10 — 28B S>C OP_ClientUpdate = other-spawn position broadcast, POSITION CRACKED

Capture: `tests/replay/eql/eqlegends-levelup.vpk` — the first fixture with a busy zone
(**3572 S>C 28B** fires + 3478 C>S 42B). Method: `--dump-payload 0x7171` → split by size (28 =
S>C, 42 = C>S), then two independent cross-checks against ground truth.

**The 2026-07-08 "byte-aligned pos+vel, NOT MobUpdate's packing" guess was WRONG.** The 28B S>C
is the **same 19-bit ×8 packed family** as MobUpdate / Live `spawnStruct`, with the coordinate in
the **low 19 bits** of its word (Live puts it in the *high* bits) and an extra eql-only u32 vs
Live's 24B `playerSpawnPosStruct`.

**Layout (LSB-first, `#pragma pack(1)`), CONFIDENCE-TIERED:**
```text
/*00*/ u16  spawnId            HIGH  (u16@2 spawnId2 = 0 in every sample)
/*04*/ u32  unknown04          TBD   (eql-only; per-spawn small int, not in Live's 24B)
/*08*/ u32  packed             MED   heading @ bit16 = (w>>16)&0x7FF, 11-bit 0..2047
                                     (low bits = deltaZ/vel; see confidence note)
/*12*/ u32  z:19 (low, signed) HIGH  ÷8   (high 13 bits ≈ 0 in samples)
/*16*/ u32  y:19 (low, signed) HIGH  ÷8   (high 13 bits = a velocity-ish field, varies w/ speed)
/*20*/ u32  x:19 (low, signed) HIGH  ÷8   (high 13 bits = per-spawn constant in samples; TBD)
/*24*/ u32  packed             LOW   vel/heading2 (low13 ±244; alt heading candidate)
```
Extract each coord: `sign_extend(u32@off & 0x7FFFF, 19) / 8.0`  →  z@12, y@16, x@20.

**Evidence (position = HIGH confidence, two independent methods):**
1. **Cross-stream vs MobUpdate (0x67e0).** 49 spawnIds appear in both streams. For spawns
   stationary across both capture windows, z@12 / y@16 / x@20 (low-19 ÷8) reproduce the MobUpdate
   median position exactly — e.g. sid 3014: (x,y,z) decoded (684.6, −329.8, 6.8) vs MobUpdate
   (686.6, −331.3, 6.8); sid 4849 (347.2, 86.6, −29.0) vs (345, 87, −28.9); sid 5270 y=2769.6
   exact. (Movers drift because the two sparse streams aren't time-aligned — that noise, not a bad
   layout, is why the naïve all-49 median match looked weak.)
2. **Trajectory self-consistency (no ground truth needed).** Decoding a dense walk yields a smooth
   path at EQ run speed — spawn 5121 (897 samples): idle (steps 0.0–0.35) → steady walk (7.78,
   8.33, 8.25, 8.60, 8.81, 8.29 units/tick) with z pinned at ground. The two rejected alternatives
   are garbage: Live-style **hi-19** gives 14000–22000-unit jumps; a **shift-by-4** layout gives
   periodic 1024-unit jumps. Median step ≈ 8 for walkers, ≈ 0 for idlers.

**Heading (MED):** `(u32@8 >> 16) & 0x7FF`, 11-bit 0..2047 — full-bit-position scan vs movement
direction gives R=0.65 (moderate: facing ≠ movement exactly; strafe/turn/backpedal), phase ≈ −7°.
Matches the old "angle 0–2047" note. A weaker alt sits in @24 (R=0.56). Velocity/pitch/animation
occupy the remaining high bits of @8/@24 and the coord-word high 13 bits — not pinned, **not needed
to render other spawns on the map**.

**Spawn coverage:** 104 distinct spawnId@0. 49 overlap MobUpdate (also NPC-positioned); the other
55 are ClientUpdate-only (player-style spawns positioned primarily by this op, as on Live).

**Wiring (DONE 2026-07-10):** `seq-backend-eql`'s own `parse_player_spawn_pos` rewritten for the
28B layout (clean-break: Live's 24B copy in `seq-decode` untouched) → reuses the existing
`decode_player_spawn_pos` FFI (no new bridge surface) → `EqlDispatch::playerUpdateOther` applies
`>> 3` and calls the neutral `SpawnShell::moveSpawn` (the exact path OP_MobUpdate uses; guards the
player's own id). Wired in `wire_eql.cpp` at the Live `playerUpdate` slot (fire order). Size override
`("playerSpawnPosStruct", 28)` added to `size_overrides()`; toml payload flipped `uint8_t/none →
playerSpawnPosStruct/match`. **Verified:** `--opcode-stats` shows 0x7171 `known`, 0 "doesn't match";
login-zone `spawn_updated` **2316 → 2542** (+226 ≈ the 230 S>C fires); a spawn seen in BOTH streams
(id 26973) has an identical ClientUpdate vs MobUpdate centroid (**dist 0.0**), median step 8.3 (vs
golden 8.5) — positions agree in sign/scale/frame; 15 spawns (mostly players absent from MobUpdate)
gain smooth ground-level positions. eql tier-2 **4/0** (regenerated the 3 fixtures with S>C fires,
stable ×3); Rust workspace green; Live regression **17/0**. Mirror: **eql-only, do NOT mirror to
legacy.** Heading/velocity still ride the high bits (unused by `moveSpawn`).

### 2026-07-09 — Tier-1 id-sync from the community f-patch (`eql-full-edits-20260709f`)

Mirrored the refreshed community patch's wire-verified ids into `conf/eql/opcodes.toml`,
each **validated against the daemon capture library** before flipping (stock struct names,
no `*EQL` suffix). Also renamed the per-spawn 4606 stream `OP_ZoneSpawns → OP_ZoneEntry`
(stock SEQ: s2c ZoneEntry = per-spawn since 2008).

**Lit up (wired handler; decode confirmed via replay + tier-2 goldens):**
- `OP_RemoveSpawn 0x71ad` (removeSpawnStruct/none, 5B) → spawns despawn (+spawn_removed)
- `OP_SkillUpdate 0x6982` (skillIncStruct, 12B×15) → Skills window (+15 player_stats)
- `OP_SpecialMesg 0x22e1` (specialMessageStruct, 59/60B×8) → NPC speech (+8 chat)
- `OP_CommonMessage 0x55eb` (channelMessageStruct) → /say /tell
- `OP_GroundSpawn 0x0def` (makeDropStruct/none, 62/63B; wire szt Modulus→None) → ground
  items decode via `decode_ground_spawn` (`IT401_ACTORDEF`→"Drop: Red Mushroom", sane coords)
- `OP_ClickObject 0x04d1` (remDropStruct, match-gated), `OP_InspectAnswer 0x17af`
  (inspectDataStruct 1956B) — community-verified, not in current fixtures

**Named for logs (not wired; passive / guild-tier):** `OP_GuildMOTD 0x46df`,
`OP_Emote 0x1cde`, `OP_SwapSpell 0x0fa0`, `OP_RandomReq 0x08cb` (match→none),
`OP_RandomReply 0x6589`, `OP_InspectRequest 0x2cc0`, `OP_HideCorpse 0x1ede` (new).

**Named, decoder DEFERRED (wire diverges from the Live struct; wire removed to avoid a
last-payload mis-bind + OOB read):** `OP_ExpUpdate 0x42d1` (12B, 0-100000 scale vs Live 16B
x/330), `OP_AAExpUpdate 0x6801` (16B, 0-100000 vs Live 12B), `OP_SpawnDoor 0x71ca`
(132B rows, `1452=11*132`, vs Live doorStruct 136B → modulus rejects). Each needs a struct
size override (+ exp scale conversion) before wiring — Tier-2/3.
*(Update: the exp ids above were later found CROSS-WIRED — see the 2026-07-09 entry;
both exp opcodes wired 07-09/07-13 and OP_SpawnDoor cracked + wired 2026-07-13. This
deferred list is now EMPTY.)*

**Divergence kept (daemon finding wins):** `OP_ManaChange 0x07c9` — a real 20B S>C mana
packet (23× in captures), kept over the community "mana only rides 0x2735".

### 2026-07-09 — `0x2735` = the stat-sync channel, FULLY DECODED + WIRED (`OP_HPUpdate`)

Resolves the 2026-07-08 "0x2735 = message/entity-event channel" investigation below: the
per-entity structure that pass correctly *saw* (id@0 + subtype byte@4) but misread as text is
the **stat-sync channel** — real HP / mana / endurance, not eqstr string-ids. Community f-patch
(`SpawnShell::spawnStatEQL`, 6378 packets, zero layout exceptions) + our byte re-verification.

**Wire** (`u32 spawnId | u8 flags | per-stat payload | [optional u32 tail]`):
- `flags`: bit0 = wide, bit1 = HP, bit2 = mana, bit3 = stamina/endurance, bits4-5 = reason.
- Wide form = `{i64 cur, i64 max}` per set stat bit (in bit order HP→mana→endurance); narrow
  form = one `u8 percent` per stat (max=100). Size is exactly `5 + 16n` (wide) or `5 + n`
  (narrow), optionally `+4` (trailing u32). `flags 0x31`, no stat bits = 6s keepalive.
- Byte-verified over the 571 upperguk `0x2735` fires (`sizes=6:370,21:110,7:32,37:29,53:27,5:3`):
  **0 structural-canary failures**, every wide `{cur,max}` sane (cur≤max, max>0).

**Decode** = Rust `seq_backend_eql::parse_stat_sync` → FFI `decode_stat_sync` → `StatSync{spawn_id,
wide, has_hp/hp_cur/hp_max, has_mana/mana_cur/mana_max, has_end/end_cur/end_max}`. The old
6B-percent-only `parse_hp_update` override is gone; the shared `decode_hp_update` FFI is stubbed
inert for eql.

**Wiring** (`EqlDispatch::statSync`, `OP_HPUpdate 0x2735 S>C`):
- **Player HP** (the wide form is exclusively the player's own real cur/max — all 129 wide-HP
  fires in upperguk target the player) → `Player::setHealth` → `player_stats.hp_cur/hp_max`.
  *Daemon adaptation*: the f-patch routes HP to `m_spawns.value(spawnId)`, but here the player is
  **never a `m_spawns` entry** (verified: `updateSpawnHP(13167)` finds nothing 129/129) — the
  daemon surfaces player vitals through the Player object, so the player's HP must go there, not
  to `updateSpawnHP` (which would silently drop it, like the pre-wide feed did).
- **Other-spawn HP** (narrow percent) → `SpawnShell::updateSpawnHP` → `spawn_updated.hp_cur` (NPC
  bars; 174 carry HP in upperguk).
- **Player mana** (wide form only) → `Player::setMana` → `player_stats.mana_cur/mana_max`
  (real numbers, max 743). Plays Live's OP_ManaChange role; coexists with the kept 0x07c9.
- **Player endurance** (wide form only) → `Player::setEndurance` → `player_stats.endurance_cur/max`
  (added 2026-07-10). This channel is eql's SOLE endurance feed — the standalone OP_EndUpdate id is
  `ffff` (unknown), so it never fires; endurance moves constantly as skills/abilities consume it.
- Self-contained: Rust + `EqlDispatch` only, no proto/web change (rides the existing
  `player_stats`/`spawn_updated` plumbing). eql tier-2 goldens regenerated (HP/mana/endurance shift).

### 2026-07-10 — LEVEL-UP: no discrete opcode exists; wired the exp-wrap heuristic instead

**Question settled: EQL sends NO mid-session level packet.** `OP_LevelUpdate` stays `ffff` in
`conf/eql/opcodes.toml`. Hunt fixture: `tests/replay/eql/eqlegends-levelup.vpk` (multi-ding grind,
43× `0x6801`). Method + evidence (all `--dump-payload` over the `--replay` events timeline):

- **2 dings, level 1→3, cross-confirmed three ways.** `0x6801` exp (u32@0 permille) wraps exactly
  twice — `96700→3614` (ev 23167) and `97542→8421` (ev 52196); the profile `0x62f0` `u8@33` reads
  **1** at capture start and **3** at the end; and `@12146` (the parked 2nd-class level, per the
  2026-07-08 multiclass note) went `5→7`. All three agree on 2 level-ups.
- **No opcode carries the ding — five searches, all negative:**
  1. No opcode fires *only* in both ding windows (dedicated level packet). The ding "burst"
     (`0x577f → 0x487e → 0x42d1 → 0x6801`) is just the ordinary **per-kill** burst: `0x487e` (n=41,
     32B) and `0x577f` (n=34, 64/44B) fire on nearly every kill, not per-ding.
  2. No per-kill opcode (`0x487e`/`0x577f`/`0x42d1`) has a field that's constant-then-`+1` at dings.
  3. `OP_SpawnAppearance2 0x1bdc` for the player (id 4791, 137 fires) is `{spawnId, type∈{22,26}}`
     with **param always 0** — no level broadcast (the ding-adjacent `0x1bdc` are the killed mobs).
  4. Profile `0x62f0` fires 4× (login ×3 + once at capture end), **never at a ding**.
  5. **Exhaustive scan of all 228 zone opcodes** (every u8/u16/u32 offset) + an **entity-keyed
     rescan filtered to the player id** (closes the multi-entity blind spot, e.g. the `0x2735` stat
     channel): **zero** `v→v+1→v+2` ding-aligned fields. Matches the l-patch author's "LevelUp is
     missing" and the prior single-ding hunt.

**Wired: the exp-wrap heuristic (`daemon`, eql-only).** The client itself infers a ding from the
exp bar, so the daemon does too. `OP_ExpUpdate 0x6801` now routes through **`EqlDispatch::expUpdate`**
(was `Player::updateExp` direct): it seeds level from the profile (`setIdentity`), and on each wrap
(regular exp `new < last`) bumps the level via a new neutral `Player::applyLevel(uint8_t)` primitive
(sets level + `fillConTable` + `levelChanged`/`changeItem(tSpawnChangedLevel)`), then forwards to
`Player::updateExp`. **EQL has NO death XP penalty** (per-server design, per user), so regular exp
only ever decreases at a ding — a bare `new < last` test needs no death-dip guard. `applyLevel` is
unused on live/test (they get level from `OP_LevelUpdate`); live tier-2 17/0 unregressed. Verified:
replay fires the heuristic exactly twice, **level 1→2→3**, no false dings. eql tier-2 4/0.

### 2026-07-10 — collision audit: eql C++ dispatch still gates on Live struct sizes (0 active bugs, 11 dormant landmines)

Full audit of all 45 `wire_eql.cpp` bindings (struct `sizeof` from `backend/live/everquest.h` + Rust
`size_overrides()` vs eql packet sizes across all 7 captures). **The Rust DECODE is fully separated
(`seq-backend-eql`), but the C++ WIRE layer still borrows Live struct SIZES for the `SZC_Match` gate.**
Result: **0 truly-dead wires, 0 active mis-decodes** — WearChange (fixed above) was the only live landmine.
Buckets: 9 clean (`uint8_t`+Rust), 14 firing-OK (Live size == eql size: byte-identical fmt or override),
3 multi-payload, 11 `SZC_None`+Live-name (gate off, Rust decodes by length → struct name is cosmetic),
**11 DORMANT `ffff`+Live-struct `SZC_Match` = WearChange-class landmines** (safe until mapped).

**Gate size has THREE silent sources** — C++ `sizeof` (Live `everquest.h`), the toml, and the Rust
`size_overrides()` table (`seq-backend-eql/src/lib.rs:717`, applied `packetinfo.cpp:78`; only 2 entries:
considerStruct→24, startCastStruct→40). A static-`sizeof` read alone is WRONG (it false-flagged Consider/
CastSpell as dead). **Before mapping any `ffff` eql opcode, check its collision size below against the real
eql packet size** (see also the [[project_eql_wire_copy_collision]] rule):

```
OP_ZoneChange 100   OP_MobHealth 6    OP_Stamina 8     OP_EndUpdate 10    OP_LevelUpdate 24
OP_SpawnRename 195  OP_Illusion 332   OP_SpawnAppearance 8   OP_CorpseLocResponse 16   OP_SimpleMessage 12
```

**De-piggyback mechanism — BUILT 2026-07-10.** (1) `seq-backend-eql::size_overrides()` extended from the 3
divergent structs to the FULL eql `SZC_Match` gate-size registry (19 entries) — every mapped eql `SZC_Match`
opcode now declares its gate size in the backend (sourced from the crate's own pinned `eqstructs` where a
binding exists, else the capture-confirmed size), so no eql gate silently inherits the daemon's compiled Live
`sizeof`. (2) `EQPacketTypeDB` records which sizes were backend-declared; `EQPacketOPCodeDB::warnUndeclaredBackendGateSizes()`
(called after each opcode-DB load in `packet.cpp`) warns loudly at startup for any MAPPED `SZC_Match` opcode
still gating on a Live `sizeof` — turning the WearChange-class collision from a silent mis-decode into a
visible map-time error. No-op on live/test (`hasOverrides()==false`). The validation proved itself by catching
3 omissions on first run (BuffWindow/RandomReply/SwapSpell, now declared). Verified: eql 4/0, live 17/0, 0
warnings both. The next `ffff→id` map for any of the 11 dormant landmines above will now warn until its eql
size is declared.

### 2026-07-10 — l-patch addendum 3 batch: size-check hardening (already in place) + `OP_WearChange 0x5c62` named-inert

Went through addendum 3's "put the size checks back" list against `conf/eql/opcodes.toml`. **The hardening
is already complete** — no churn needed: `OP_CastSpell 0x10b5`, `OP_Consider 0x4212`,
`OP_SpawnAppearance2 0x1bdc`, `OP_InspectAnswer 0x17af`, `OP_RandomReply 0x6589`, `OP_SwapSpell 0x0fa0`
are all on `match`; `OP_Emote 0x1cde` (S>C variable) and `OP_RandomReq 0x08cb` (8/9B) are correctly `none`.
The prior Tier-1 + cast/con/buff work already set these. Deferred/blocked: `OP_Buff 0x3ada` (skipped by
design — BuffList `0x77ae` covers the player), `OP_ZoneChange 0x76d3` (entangled with the death/respawn
reset path — hold for bug #0), group ops 168B (blocked, no group capture).

**`OP_WearChange = 0x5c62`** (32B, both dirs; n=337 levelup / 3 chat) — mapped from `ffff` and named per
addendum 3, but wired **INERT**. Layout (patch, fully decoded): `{u32 spawnId, u32 wearSlot (7=primary /
8=secondary / 9), u32 material, u8[20] 0}`. **Footgun removed:** the eql wiring carried a byte-for-byte
copy of Live's two `OP_WearChange → SpawnUpdateStruct/updateSpawnInfo` bindings — and `sizeof(SpawnUpdateStruct)
== 32 == this packet's size`, so the moment the id was mapped those bindings would `SZC_Match` and mis-decode
wear bytes as a spawn update (spawn-state corruption). Removed both from `wire_eql.cpp` (kept in
`wire_live.cpp` where they're correct); `updateSpawnInfo` has no other eql use. Stock's WearChange handler
shows nothing for equip changes anyway, so inert = stock parity, just named instead of `unknown`. Verified:
`0x5c62` classifies as `OP_WearChange`, no handler fires, eql tier-2 4/0 stable. Live equip tracking would
need an eql-specific 32B decoder built on the layout above. (Related unnamed pair: `0x2575`/`0x2a0a` 8B
`{u32 spawnId, u32 flag}` appearance-refresh triggers — flag 0x40=wear, 0x04=combat/death.)

### 2026-07-09 — exp opcodes were CROSS-WIRED: `OP_ExpUpdate`=`0x6801`, `OP_AAExpUpdate`=`0x42d1`

Per the community l-patch (`eql-full-edits-20260709l`) + capture verification: the daemon had
the two exp opcodes swapped, which is why BOTH were "deferred" (each size-mismatched its struct).
Corrected in `conf/eql/opcodes.toml`:
- **`OP_ExpUpdate` = `0x6801`** (16B `expUpdateStruct`) — the **regular** exp bar. Layout: `u32 exp
  (0-100000 permille), u32 0, u32 aaUnspent (@8, NOT Live's type), u32 0`. Now **WIRED** →
  `Player::updateExp` (`SZC_Match`). No scale conversion: the daemon already runs the 0-100000
  scale (`reset()`/`loadProfile` set `m_minExp=0/m_maxExp=100000/m_tickExp=1`), so `exp@0` feeds
  straight through. Verified: `player_stats.exp_cur` now populates (upperguk 9017→27751); the
  death-respawn capture's single level-up shows the wrap 99.459%→3.622% at 0x6801 (vs 0x42d1's
  TWO wraps = AA-point earns, which is how the cross-wiring was caught).
- **`OP_AAExpUpdate` = `0x42d1`** (12B `altExpUpdateStruct`) — the AA bar. `u32 altexp (0-100000),
  u32 aapoints, u32 tail`. DEFERRED: needs the 0-100000→330 conversion in `Player::updateAltExp`
  before wiring (the l-patch has it). Id corrected + named for logs; not wired.
- eql tier-2 goldens regenerated (`exp_cur` now present in `player_stats`). eql-only (no shared core).

eql tier-2 **4/0 (1 skip)**, stable over 2 runs.

**RE-WIRED 2026-07-07** (decoder-rs + daemon): all ids updated in `conf/eql/opcodes.toml`;
the `seq-backend-eql` parsers re-derived — ClientUpdate pos `x=gameX@18 / y=gameY@10 / z@30`
(f32); ZoneSpawns pos from the block END (`z@(len-95)/8, x=gameX@(len-87)/8, y=gameY@(len-91)/8`,
both 330B/486B blocks), id@0/level@4/hp@44,45; MobUpdate `x=gameX@10, y=gameY@4/8, z@6/64`;
new `parse_legends_consider` (24B) → shared `Consider` → the neutral
`SpawnShell::consMessage` (passed the real payload len, not `sizeof(considerStruct)`) →
`spawnConsidered`→`Considered`→web. Verified on the capture: opcodes resolve by name,
positions `/loc`-correct, con fires for all 3 con'd mobs (Considered ids 12220/11626/25225),
target 38 envelopes; **live 9/9 unregressed**.

**OP_NpcMoveUpdate = `0x7352`** (2026-07-08): the continuous NPC movement stream — the
missing packet behind "mobs freeze while moving and only jump when they stop". It's the
single highest-frequency S>C zone opcode (734 fires / 51 ids in the Upper Guk capture, vs
OP_MobUpdate's 591), variable 15–21B. **Byte-identical bit layout to Live's OP_NpcMoveUpdate**
(MSB-first BitStream: 16b BE spawnId + 16b pad + 6b fieldmask + 19/19/19 y/x/z sign-magnitude
`>>3` + 12b heading + optional pitch/deltaHeading/animation/deltaX/deltaY/deltaZ per mask).
Method: dumped `0x7352`, saw per-id repeats with a 16-bit BE id that byte-swaps to a known
`0x67e0` id; ran the *existing* `decode_npc_move_update` over every payload — **all 51 ids +
decoded x/y/z land inside their 0x67e0 position ranges**, and the packet lengths match the
field-mask bit math exactly (fs=0x1c→18B, 0x02→15B, 0x3d→21B). Fix was a **one-line opcode-id
correction** in `conf/eql/opcodes.toml` (`917c`→`7352`; the old id never appeared on the wire);
the `SpawnShell::npcMoveUpdate` wire in `wire_eql.cpp` and the Rust decoder were already in
place. Carries velocity + heading, so the web gets smooth motion, not just denser jumps.
Replay-verified: 734 events decode, no length warnings.

**SUPERSEDED 2026-07-08 (same day): the "16-bit wrap" was a mis-read of Live's bit-packed
`spawnPositionUpdate` — the phase-unwrap below is REMOVED.** The 14B payload is byte-identical
to Live's struct: `spawnId u16@0` + 2 zero bytes + packed `y:19 z:19 u3:7 x:19` (fixed-point ×8,
u64@4) + `heading:12`@12. The i16@4/8 read returns the LOW 16 of y:19 → gameY **mod 8192**
(the observed wrap); i16@6/64 returns z bits 3–12 → gameZ **mod 1024** (the old Z unwrap
period); i16@10 lands exactly on x bits 3–18 → gameX unscaled, full i16 range (why X "never
wrapped"). Every observed quirk was an artifact of the truncated reads. Proof over 1665
MobUpdates across 3 captures (upperguk / unrest / nektulos): y's bits 16–18 and z's top 6 bits
are **perfect sign-fill** (0 violations — impossible for independent fields), all 8 sub-unit
fraction values occur on every axis, bytes 2–3 ≡ 0, u3 ≡ 0, heading decodes as clean 12-bit.
The prior "no wider field (float/int32 scan = 0 hits)" conclusion missed it because a 19-bit
field straddles byte boundaries — neither scan could see it. OP_ZoneSpawns positions are the
same packing (19-bit LSB of u32 words at len-95/-91/-87). Fix: eql routes `decode_mob_update`
to the shared Live parser and `parse_legends_zone_spawn` decodes 19-bit words; the
`SpawnShell::spawnPos`/zone-bounds unwrap machinery is deleted. Bonus: MobUpdate now yields
**heading** too. Historical record follows:

**OP_MobUpdate Y is a 16-bit WRAP — fixed by phase-unwrap** (2026-07-08): once
OP_NpcMoveUpdate was live, high-Y mobs flickered — correct while moving (NpcMove, 19-bit)
then snapping ~8192 units **south** on each OP_MobUpdate. Cause: OP_MobUpdate encodes Y as
signed `i16` fixed-point (raw/8), so any coordinate past ±4095 wraps by 65536/8 = **8192
game units** (Z likewise: raw/64 → 1024). Confirmed on Qeynos Hills (zone spans ry≈-611..
+5200, wider than the ±4095 an i16/8 field holds): a mob NpcMove-true at y=4724 reads
i16 ≈ −3400 every MobUpdate, and −3400 **+8192** = ~4790. The OP_ZoneSpawns position wraps
identically (23/32 high-Y spawns match tail±8192); **neither carries a wider field** (float
/int32 scan = 0 hits). Since NpcMoveUpdate is unambiguous 19-bit and fires for every roaming
mob, the fix phase-unwraps each MobUpdate coordinate toward the spawn's last known position
(`EqlDispatch::mobUpdate` + new neutral `SpawnShell::spawnPos`; wrap period Y=8192, Z=1024).
Replay-verified against the capture: every wrapped MobUpdate resolves back to its NpcMove
anchor, in BOTH directions (a genuinely-south mob whose MobUpdate wraps north is pulled back
south too). Residual: a high-Y mob shows wrapped until its FIRST NpcMove anchors it (roaming
mobs self-correct in <1s; a truly-never-moving high-Y NPC would stay wrapped — none observed,
the fixed named NPCs are all low-Y). Recon aid added: `--dump-all-sessions` (forward every
box to the recon dumpers, beating the primary-box limitation that hid the high-Y zone).

**PlayerProfile `0x62f0` VERIFIED** (2026-07-07): the identity header survived the patch —
race u32@21 (=6 DarkElf), class1 u32@25 (=5 SHD), level u8@33 (=12), confirmed against a known
L12 SHD/DRU/MNK char. `class@25` is the **primary of 3** (3-class design: SHD/DRU/MNK = 5/6/7);
2nd/3rd class ids sit in a separate block ~@12094 — not surfaced (neutral `setIdentity` carries
one class). Parser unchanged.

**Current zone = OP_PlayerProfile `0x62f0` `u16@36211`** (corrected 2026-07-08). Found by
cross-diffing a Nektulos vs an Upper Guk capture — the **only** field that flips
`25→65` (nektulos→guktop). It's the `{zoneId, x, y, z}` current-location record in the
profile. Wired in `EqlDispatch::profile` → `ZoneMgr::setZoneById` (resolves via `zones.h`)
→ map loads.

⚠ **Correction:** the first attempt wired `0x4bc8@6` and was WRONG — that's the **BIND**
zone (where the char binds, = nektulos), *identical across zones*, so it showed 'nektulos'
while the player was in Upper Guk. `0x4bc8` is now **UNWIRED** (kept identified as the
zone-in/bind marker). Lesson: a single-zone capture can't tell bind from current — needs
≥2 zones. Caveat on the fix: `@36211` is a deep offset in a ~40KB variable-length profile,
so it may shift with big inventory changes — re-derive if the zone resolves wrong.

**Still TODO:** ClientUpdate heading+deltas (left 0 — no facing arrow / speed-between-updates;
need a `/loc`-while-turning capture). That's the last open piece.

### 2026-07-09 — OP_PlayerProfile `0x62f0` character NAME → authoritative eql box name

The eql player/box name had been coming from **own-spawn adoption**, not the profile:
the char's own `OP_ZoneSpawns` entry lands as a mob; `OP_ClientUpdate` later establishes
the player id; `SpawnShell::playerChangedID` adopts that mob's name. i.e. sourced from the
position/spawn path. The profile carries the name directly.

- **Located by an absolute anchor-scan, not a fixed offset.** `find_profile_name_block`
  (`seq-backend-eql`) scans for the name/surname block signature — `u32 == 64` + 64-byte
  NUL-terminated name buffer + `u32 == 32` + 32-byte NUL-terminated surname buffer — and
  validates the candidate (a real first name is a capitalized, printable, NUL-terminated
  run, so binary won't false-match). From that anchor the whole Live-shaped tail (surname,
  birthday, expansions, languages, current zone, position, guild, money) parses positionally
  (`read_profile_name_and_tail`).
- **Why anchor-scan, not the offset.** The name was *first* found at a fixed offset (`36047`,
  confirmed stable across an L26/41611B and an L30/41891B capture), but that offset sits
  past the inventory block, so a big inventory change or a patch shifts it — same fragility
  class as the removed `@36211` zone read. The anchor-scan is offset-independent (survives
  the inventory/spellbook drift), so it **superseded** the fixed-offset read.

**Wiring.** `EqlDispatch::profile` reads `out.name` → neutral `Player::setPlayerName` (stores
name + emits `Player::identityNameResolved`); `DaemonApp` promotes the box on that signal
**unconditionally** — the eql equivalent of Live's `ZoneMgr::playerProfile` → `promoteByName`
(eql never emits `playerProfile`; its profile is decoded in `EqlDispatch`, not
`fillProfileStruct`). Own-spawn adoption stays as the fallback for when the anchor block
isn't found.

**Verified** against `login-zone` / `upperguk` / `chat` fixtures: the profile names the box
authoritatively, eql tier-2 goldens green. (The anchor-scan parser lives in `seq-backend-eql`;
the fixed-offset first cut is kept above only as the derivation trail.)

### 2026-07-08 — Reverse-direction sweep: OP_ClientUpdate S>C (28B, other players) + OP_ZoneSpawns C>S (92B, zone-entry request)

Read-only sweep of the existing eql fixtures (`fulllogin`, `login-zone`, `chat`, `upperguk`) for
opcodes that fire **both** directions but only had one side wired. Method: replay against the eql
build + `--dump-payload OP:PATH --dump-all-sessions`, split files by dir/size, diff samples.
`dir=1`=C>S, `dir=2`=S>C. No `typename`/struct edits made (struct strategy deferred — see below).

**OP_ClientUpdate `0x7171` — S>C 28B = OTHER-PLAYER position broadcast. Identity CONFIRMED, field layout PARTIAL.**
- Direction split: C>S 42B (self, wired) + **S>C 28B** (1 fire in fulllogin; **230 in login-zone across 24 distinct ids**).
- Confirmed *other players*, not a self-echo: the C>S self id (`0x69d2`) never appears in the S>C
  stream, and **20 of 24 S>C ids are absent from the OP_MobUpdate (NPC) id set** → player-controlled
  spawns. Same split as Live (NPCs → MobUpdate/NpcMoveUpdate; players → ClientUpdate both ways).
- Structure (moving-sample diffs, busiest id n=78): `u16 spawnId@0`, `u16 pad@2 (=0)`, motion/flags
  block @4–11 (`@4=0x1b` marker once moving), state/pose byte `@12` (`0x13` while active), `@13–15=0`,
  then per-axis **`u16 pos` + `i16 vel`** pairs @16–23 (`@16` pos climbs monotonically while walking,
  `@18` vel tracks speed; `@20/@22` the second axis), heading candidate `@24` (small, tracks turning),
  `@26–27=0`. **NOT** MobUpdate's 19-bit MSB packing — its own byte-aligned pos+vel layout.
- **Axis→game-axis binding + scale are UNCONFIRMED and cannot be finished from existing fixtures:**
  self isn't echoed S>C, and the 4 S>C ids that overlap MobUpdate have only 1–2 samples each (ranges
  too wide to disambiguate a field-value scan). **Finish with a purpose capture** — a 2nd known
  character at a known `/loc` walking cardinals — the same method that pinned the 42B C>S self.

**OP_ZoneSpawns `0x4606` — C>S 92B = client zone-entry / spawn-list request. Identity CONFIRMED, framing MOSTLY MAPPED.**
- Fires **exactly once per zone-in**, C>S; the S>C side (spawn blocks) is already wired. 8 samples
  across several zone-ins (same character).
- Framing (offsets/roles only — identity bytes intentionally not transcribed): `u32 @0` = per-session
  token (constant within a login session, differs across sessions); **null-terminated char name @4**;
  `u32 @0x40` varies per zone-in (zone/position-dependent); `@0x44–0x53` = a fixed client/character
  signature block (byte-identical across every session + zone captured); short trailer @0x54–0x5b.
  Carries client **identity**, not spawn data — "I'm here, send the zone" → server replies with the
  S>C spawn stream.

**Also mapped in the sweep (unwired unknowns, for later):**
- `0x1bdc` (24B, bidirectional, S>C-heavy) — **CONFIRMED OP_SpawnAppearance2**, byte-identical to the
  neutral 24B `spawnAppearance2Struct` (`u32 spawnId@0` [hi-16 always 0], `u32 type@4`, `u32 value@8`,
  `u8 pad[12]`). 55 spawn ids; type→value fits SpawnAppearance semantics (type 22→val 0; type 43→val
  0/1/2 flag; type 6→val 100/110/115; type 5→val 7/11/12). A **name-swap** (not a mechanism customer).
- `0x4f7a` (12B S>C ×200, steady in every capture) — periodic self-referential tick: `u32 counter@4`
  (monotonic 0..N), constant self entity-id `@8`, optional other-id `@0`. Low tracker value.
- `0x5c17` (C>S variable + S>C 23B×31) — probable request/response pair (bytes unexamined).

**Struct/decoder architecture — DECIDED (2026-07-08): Rust-sourced sizes ("everything swappable").**
The `SZC_Match` size-gate is a pure `name→size` map (`addStruct`); eql never casts a C++ struct
(Rust decodes), so eql needs only a *number* per opcode. Plan: byte-identical opcodes point their
toml `typename` at the existing **neutral** C++ struct + `match`; eql-unique sizes get a per-backend
size table exported from `seq-bridge`/`seq-backend-eql` and registered via `addStruct(name, size)` —
no C++ eql structs, neutral names, no reshaping, Rust is the reference.

**Landed (byte-identical cleanups, verified regression-free):**
- `OP_MobUpdate` 14B → `typename="spawnPositionUpdate"`, `SZC_Match` (was `uint8_t`/none + in-dispatcher `len==14`).
- `OP_ClientUpdate` C>S 42B → `typename="playerSelfPosStruct"`, `SZC_Match` (size matches even though the eql field layout differs; decode stays Rust).
- `OP_SpawnAppearance2` = **`0x1bdc`** (24B) → reuses the neutral `spawnAppearance2Struct`; adding the id mapping **lit up the already-present-but-dormant `wire_eql.cpp` handler** (`SpawnShell::updateSpawnLock`). No wire/handler change — just the toml id. Now resolves as known; golden byte-identical (handler no-ops on these captures' types; armed for lock-ruleset eql servers).
- Verified: fulllogin replay → MobUpdate 658 / ClientUpdate 706 events (unchanged), 0x1bdc→OP_SpawnAppearance2 known, golden byte-identical run-to-run + across the SpawnAppearance2 change, 0 size warnings.

**Method note (from this pass):** the daemon has a large catalog of neutral structs (`s_everquest.h` size index); many eql "unknown" opcodes are just Live opcodes remapped, so the play is to match eql `(size, dir, behavior)` against an existing neutral struct + reuse the Live opcode name (as with `spawnAppearance2Struct`), NOT to invent structs or deep-crack bytes. `dispatchFor` also has a **last-payload fallback** (a `wire()` typename that doesn't exactly match a toml payload binds to the opcode's LAST payload rather than erroring) — so always exact-match the toml payload; a mis-typed wire silently mis-binds instead of failing loudly.

**Catalog-sweep finds (match eql unknown `(size,dir)` → unmapped Live opcode, confirm by value):**
- `0x07c9` = **OP_ManaChange** (20B S>C) — **LANDED**. Byte-identical `manaDecrementStruct`; value-match: curMana 282–930, endurance ~800, spellId 445/821 cross-ref OP_Action. id `ffff`→`07c9` lit up the dormant `Player::manaChange` — 35 events (this one *adds* real mana updates to the output, unlike the earlier output-neutral size-swaps).
- `0x3c0a` (15B S>C ×30) — size matches `beginCastStruct` (OP_BeginCast) but the fields **don't align** (spellId 344 constant; the spawn-id-looking value sits at a shifted offset, caster u16 reads `0xe207`). NOT a clean name-swap — deferred; the eql cast struct diverges from Live's.
- Ambiguous by size (need dir+behavior to pin): `0x3ada` 24B → {MoneyOnCorpse / LevelUpdate / RequestZoneChange}; `0x793a` 16B → {ExpUpdate / MemorizeSpell / CorpseLocResponse}; plus 8B/12B "fits-many" opcodes.

**Pending (need the Rust size-table mechanism — NOT pure name-swaps):** `OP_Consider` (24B; the C++
`considerStruct` is **32B**, not a match) and `OP_Animation` (4B; **no `animationStruct` exists** in
the daemon header). And the 28B S>C ClientUpdate once its layout is cracked.

### 2026-07-08 — OP_PlayerProfile `0x62f0` internals: multiclass level table (CONFIRMED) + class-data / loadout / storage leads

Capture: `tests/replay/eql/eqlegends-fulllogin-20260708.vpk`. Method: `--dump-payload 0x62f0`
(one 41271-byte profile), structural analysis against player-supplied ground truth — a 3-class
SHD/DRU/MNK char where every *un-played* class is parked at a level-**10** floor and the active
classes level independently. `level u8@33` reads **20** here (the "=12" in the note above was an
earlier/stale capture; the char leveled).

- **Per-class level table — CONFIRMED.** The profile **ends** with a length-prefixed table:
  `u32 count (=16)` then **16× u8**, `byte[i]` = level of class `i+1` (WAR..BER), then a `0x00`
  pad. Here classes 5/6/7 (SHD/DRU/MNK) = **20**, every other class = **10** (the un-played
  floor) — matches ground truth exactly. Tail bytes:
  ```
  ff*28  10 00 00 00  0a 0a 0a 0a 14 14 14 0a 0a 0a 0a 0a 0a 0a 0a 0a  00
         └ count=16 ┘  └ WAR CLR PAL RNG [SHD DRU MNK=20] BRD..BER=10 ┘  pad
  ```
  Locate robustly (absolute offset shifts with profile size): it's the last section —
  `count = u32 @ (len-21)`, `levels = bytes[len-17 : len-1]`; it sits immediately after a run
  of `0xff`. Anchor from EOF / the `0xff` run, not a fixed offset.

- **3 parallel class-data records — STRONG lead.** 3× 20-byte records at `~@40966` (stride 20,
  marker `u32 0x37530004`, constant `0x00020001 0x00110000` header + 2 varying fields) — one per
  active class, i.e. the "3 parallel class-data blocks" the 3-class design predicts (spell
  affinity / per-class state). Not yet decoded field-by-field.

- **Loadout / race+class records — PLAUSIBLE lead.** `{u32 race=6, u32 class=5, …small ids…,
  0xffffffff empty slots}` at `~@41058` and `~@41162` (the `{6,5}` = DarkElf/SHD pair recurs
  there, besides the `@21` header). Shape = {race, primary class, gear-id slot array} → the
  loadout / "be any race+primary class" feature. Count/stride not yet pinned.

- **Magical storage / bank — CANDIDATE.** Item-id slot arrays with `0xffffffff` empties scattered
  through the blob (e.g. a 10-empty-slot run `@35981`; plausible item ids 341/242/252/91/5012/340).
  Char-bound storage vs bag/bank is indistinguishable from a single snapshot — needs a paired diff.

**Next capture (Mode C paired diff) to confirm storage + loadouts:** record ONE `.vpk` that
brackets a single change with two profile fires (OP_PlayerProfile fires per zone-in), then
`--dump-payload 0x62f0` → `pp.1.bin` (before) / `pp.2.bin` (after) and diff the two:
- storage: zone in, deposit/withdraw ONE known item id in magical storage, re-zone.
- loadout: zone in, swap/edit a loadout, re-zone.
The single u32 item-id that appears/vanishes localizes the storage array; the changed
`{race,class,gear}` block localizes loadouts.

### 2026-07-08 — `0x2735` = formatted-message channel (S>C); Sense Heading decoded

> **SUPERSEDED (2026-07-09):** `0x2735` is the **stat-sync channel** (HP/mana/endurance) —
> see the entry above. The "per-entity event stream" structure this pass found (id@0 +
> subtype@4) was right; the "message/text channel" reading was wrong (the `u32@0` is the
> spawn id, the `@4` byte is the stat `flags`, and the "string-ids" were HP/mana values). It
> is now decoded + wired. Kept below as a cautionary tale on the entity-id/string-id
> number-space collision that produced the false "173 text messages" reading.

`0x2735` is the high-volume S>C **message channel** (1026 fires in the Nektulos
capture, 571 in Upper Guk; variable 5–53 B). It's multiplexed — each message type has
its own small struct keyed by an eqstr/dbstr string-id; render via
`~/.showeq/eql/eqstr_us.txt` (id → template) + `dbstr_us.txt`.

**Sense Heading** message = **6 B** `{u32 direction_string_id, u16 unk}`. The direction
id is an eqstr id `12427–12434` = **N / NE / E / SE / S / SW / W / NW** (clockwise), which
the client renders into `12435 "You think you are heading %1."`. Confirmed: two 6-B
payloads `91 30 00 00 …` = `12433` "West".

Notes / gotchas:
- The channel's leading `u32` is often the **entity id**, not a string-id — watch for
  coincidental eqstr collisions (the player id `13167` matches eqstr `13167 "Current
  mouse speed…"`, a false positive). The real string-id offset varies per message type.
- **`net opcode 0000` — TWO sources, neither is dropped EQ data** (2026-07-08):
  1. In a **raw pcap taken with a broad `host` filter**: mDNS / service-discovery
     (`EQBOX1.local`, `_dosvc._tcp.local`, Bonjour) — LAN multicast on port 5353. Fixed
     by excluding `port 5353` + `net 224.0.0.0/4` from the open capture filter (`1d3dceb`).
  2. On the **live/port-restricted stream** (real EQL traffic): an **ENCRYPTED C→S
     channel** multiplexed on the game socket (fires during combat/in-zone activity, not
     zone-in — 0 in the login-zone vpk, 78 in Upper Guk; live Unrest showed 82–482B
     bursts). Rigorously verified opaque (dumped 40): no plaintext EQ structure at any
     offset (no spawn-ids / names / eqstr), payloads high-entropy and 40/40 unique
     (~5.33 bits/byte on 48B ≈ random), plaintext framing header + a per-packet 16-byte
     field = an **AEAD nonce** signature. Not decodable without the client key, and NOT
     the server game-state we decode (that's the S→C SOE channels, intact). Dropped
     silently with a one-time announce (`44f5a76`).
     **⚠ CORRECTED 2026-07-13: source 2 was a MISDIAGNOSIS — it is not EQ traffic at
     all.** 5-tuple audit across 37k+ EQ packets: **0** of the net-0000 packets involve
     the EQ server (69.174.201.x). They are ambient LAN UDP the broad mirror-port `udp`
     capture sweeps in — IPsec NAT-T keepalives (port 4500, byte-identical payloads;
     the "high entropy" was ESP ciphertext, hence the AEAD-shaped read) and other
     non-EQ services (e.g. a cloud/Azure host). The only real EQL encrypted stream is
     the UCS chat session on :9877, which IS decoded. Fixed at the capture layer
     (`0bcf33d`): `--ip` accepts a server CIDR (`net` BPF term) and `scripts/capture.py`
     defaults to the Daybreak block 69.174.0.0/16; the one-time announce now says
     ambient-LAN, not "encrypted channel".
  Separately, the **`calcCRC16 called for length > 1048576`** spam (~every 10s) was a
  **1-byte packet** (netOp `0x00ff`) underflowing `rawPacketLength()-2` (unsigned) into a
  ~4.29GB length — guarded in `calculateCRC` (`c12bf31`). `0x2735` messages are **not**
  dropped by any of this; the missing Sense Headings were client-side text.
- This channel is the foundation for wiring EQL formatted / combat / system message
  **text** into the daemon+web (via eqstr/dbstr) — not yet done.

**2026-07-08 — 0x2735 is NOT cleanly wireable as a chat channel (needs a dedicated
capture).** Attempted to wire it; deep-decoded 571 (upperguk) + 1026 (login-zone) payloads.
Finding: **the channel is predominantly a per-ENTITY event stream, not text.** Of the 571
combat-capture messages, **388 have a spawn/entity id at `u32@0`** and only ~4 are genuine
system text (2× eqstr 12116 "You groan and feel a bit weaker", 2× 12433 "West"/Sense
Heading). The apparent "173 text" is a **false positive**: the player id `13167` collides
with eqstr 13167 "Current mouse speed multiplier is %1." (169×). Root cause — **the entity-id
and string-id number spaces overlap** (the "OTHER" `u32@0` values 11633/11715/… are
simultaneously live spawn ids AND `dbstr_us.txt` entries; 950/1088 resolve in dbstr), so the
leading u32 cannot be classified entity-vs-string from the packet alone. The subtype byte
`@4` (0x02/0x04/0x0f/0x23/…) does NOT separate them either (genuine text sits in @4=0x04
*mixed with* entity + other). **Wiring it blind would spam the web chat with garbage** (the
169× "mouse speed" line). CONCLUSION: to wire EQL chat/system/combat text safely, capture a
**dedicated session with KNOWN content**. For each channel type a **distinctive but
ordinary-looking** phrase — pick natural words you'll remember, **NOT an obvious test marker
like "SEQTEST"** (the text goes to the live server; keep it innocuous and un-botlike) — e.g.
`/say`, `/ooc`, `/tell <box>`, `/shout`, `/auction` each with a different memorable phrase
you jot down locally to grep for; and trigger known combat/system lines ("You have slain …",
"… hits YOU for N", "You gain experience!", a resisted spell). Then `--dump-payload` every
opcode and **string-grep for those literal phrases** to pin the real chat opcode(s) + format
precisely (player chat carries LITERAL text, unlike 0x2735's string-ids). The on-disk captures contain no typed
chat, so the chat opcodes (OP_CommonMessage / OP_SpecialMesg / OP_FormattedMessage — all
handlers pre-wired, awaiting ids) can't be found in them. 0x2735 itself stays unwired until
its entity-event subtypes are separately decoded (they may overlap with already-decoded
spawn state).

**ClientUpdate heading/deltas — DONE (2026-07-08).** heading = `u16@14`, 11-bit
(0–2047 = full circle, North≈0), velocity deltaX `f32@26` / deltaY `f32@6`. Confirmed by
a Sense Heading capture (Dagnor's Cauldron):
turning through N/NE/E/SE/S/SW/W/NW, `u16@14` stepped 2043/1814/1542/1246/1036/782/492/203
(~256 = 45° apart, value falls as compass rises). Note: the Sense Heading *text* is
**client-side** (not on the wire — 0 in the capture), so the in-game log is the direction
ground truth; the packet field is what carries facing. deltas re-derived from running
segments (correlation of f32@6/@26 with Δx/Δy). **This was the last EQL decode TODO.**

### 2026-07-08 — combat opcodes from existing captures: Action2 / Action / Animation

Mined the two rich post-patch captures already on disk (an Upper Guk combat/dungeon
capture + a full-login capture) with `--dump-payload` over the top
unmapped S>C opcodes, then decoded each against Live structs (the "try Live's wire first"
rule — all three are byte-identical). Method: built the live entity-id universe from the
0x67e0 MobUpdate stream (spawnId u16@0) and cross-checked candidate id fields against it.

- **OP_Action2 = `0x1734`** (S>C, 48B fixed, n=481 upperguk / 968 fulllogin). The
  **melee combat / damage-resolution stream**. Byte-identical to Live `action2Struct`:
  `target u16@0` (315/481 ∈ live ids), `source u16@2` (274/481), `damage i32@8`
  (0–107, small melee values; 0 = miss), `spell i32@20` = **-1 (0xffffffff) for 422/481
  melee swings**, real spell-id for the rest, `type u8@40` (combat-type enum). The @24–39
  "unknown" block holds the modern knockback floats (force/heading/pushUp: e.g. f32 0.025,
  146.5). Sole 48B S>C opcode — the only other 48B fires are two singleton-count noise ops.
- **OP_Action = `0x73de`** (S>C). The classic Live **paired action send**: 64B
  `actionStruct` (n=97/117) immediately followed for some by 88B `actionAltStruct`
  (n=22/43). `target/source u16@0/@2`, `spell u16@4` = real spell-ids (502/445/821/267…).
  Sole 64B/88B opcode. This is the spell/special-action channel (vs Action2's melee).
- **OP_Animation = `0x1293`** (S>C, 4B, n=354/718). Byte-identical to Live `animationStruct`:
  `spawnId u16@0` (70% ∈ live ids; rest = doors/objects/self), `action u8@2` (values 1–46),
  `speed u8@3` = **10 in all 354 packets** (constant animation speed — the Live signature).

**Side finding — `0x5b5e` = on-target HP reveal** (S>C, 13B primary, n=12/22):
`{u32 spawn_id, u32 cur_hp, u32 =0x07000001, u8 0}`. cur_hp is **absolute** (=0 for a
just-killed spawn), id 100% ∈ live set. Fires when a spawn is targeted/considered
(companion to `0x0e54 {0,target}`). This is the *on-select* HP reveal, **not** a
continuous %-HP health-bar broadcast — that stream (Live OP_HPUpdate/OP_MobHealth
equivalent) is still unlocated; the health bar may instead be driven client-side off
Action2 `damage@8`. Ruled out for it: `0x4f7a` (12B, n≈200 in EVERY capture regardless of
activity → fixed-rate heartbeat, no id field at any offset).

**Also confirmed the same day — OP_DeleteSpawn = `0x59a1`** (S>C, 4B `{u32 spawnId}` =
`deleteSpawnStruct`, n=9). Found among the 4B-`{id}` S>C candidates and confirmed by
**time-correlation** against the OP_MobUpdate stream: for 8 of 9 fires the spawn stops
receiving any position update at/after the DeleteSpawn event (the 9th never moved) — i.e.
these are mobs dying over the combat session. The look-alike `0x67a8` (also 4B `{id}`,
n=11) is **NOT** despawn: its ids keep moving afterward and it emits `id=0` clears → it's a
combat **engage/disengage** state broadcast; left unmapped.

**KEY REALIZATION — the EQL hunt is mostly ID-supply, not handler-writing.** `wire_eql.cpp`
already wires the full Live handler set **by name** (`OP_Action2`→`CombatRouter::action2`,
`OP_Action`→`SpellShell::action`, `OP_DeleteSpawn`→`SpawnShell::deleteSpawn`, `OP_HPUpdate`,
`OP_Death`, messages, groups, spells…), each with `SZC_Match` against the Live struct size.
They never fired only because `conf/eql/opcodes.toml` still held **stale Live ids that never
appear on the EQL wire** (the "we didn't reset to ffff" problem — verified harmless: 0/67
stale ids collide with a live EQL opcode, so no misdecodes, just dead handlers). For any
EQL opcode whose payload is byte-identical to its Live struct, **supplying the correct id in
the toml is the entire fix** — the wired handler + Rust decoder light up exactly like
OP_TargetMouse did. That is what happened here.

**Status: WIRED + replay-verified 2026-07-08.** Remapped `OP_Action2 0x1734`,
`OP_Action 0x73de`, `OP_DeleteSpawn 0x59a1`, `OP_Animation 0x1293` in `conf/eql/opcodes.toml`,
rebuilt eql. All four now resolve as `known` with correct sizes (**zero `SZC_Match` drops**),
and a recorded golden over the Upper Guk combat capture emits **481 `CombatEvent`**
envelopes (real source/target/damage; melee `spell_id=0xffffffff`) and **11 `SpawnRemoved`**
(covering all 9 confirmed DeleteSpawn ids). `SpawnKilled=0` because **OP_Death's EQL id is
still unmapped** — mobs despawn but without a corpse/death event; that + a continuous %-HP
health-bar stream are the next combat gaps. damage/`type` sub-field *semantics* (some
negative `damage@8` = misses/absorbs?, large `type` values) want a controlled kill capture
(Mode C) to fully pin, but the identification and combat-log wiring are solid.

### 2026-07-08 — OP_Death = `0x66cb`; HP% has no dedicated opcode; ffff cleanup

**OP_Death = `0x66cb`** (S>C, 40B, n=8) — byte-identical to Live `newCorpseStruct`.
Found as the 40B S>C op with n=8 (= the 8 combat kills), confirmed by decoding:
`victim u32@0` (all 8 ∈ the OP_DeleteSpawn `0x59a1` set), `killer u32@4` (=player 13167
every kill), `corpse type i32@12`, killing-blow `spellId u32@16` (-1=melee),
`damage u32@24` (9–80, sane killing blows). Remapped in `conf/eql/opcodes.toml` → the
already-wired `SpawnShell::killSpawn`; replay now emits **8 `SpawnKilled`** envelopes
(was 0) with exactly those 8 victim ids. OP_Death fires just before its matching
OP_DeleteSpawn (death → corpse → remove).

**HP% (continuous per-mob health) — NO dedicated opcode found (well-supported negative).**
Exhaustively searched for a per-spawn field that drains toward 0 at each victim's death
time: tested 13 candidate opcodes (`0x52bc`/`0x5591`/`0x5b5e`/`0x42b5`/`0x6007`/`0x1bdc`/
`0x22e1`/`0x1f55`/`0x6801`/`0x3ada`/`0x6982`/`0x50a7`/`0x18e0`) **plus every internal field of
OP_Action2** — none carries a monotonic HP drain (the apparent Action2 @38–44 "drains" are
the knockback-float bytes read as int32, pure noise). Conclusion: **EQL derives mob health
client-side** from ZoneSpawns initial HP (`0x4606` hp@44/45 = 100%) + the OP_Action2
`damage@8` stream, with the **on-target absolute-HP reveal `0x5b5e`** refreshing the selected
target. A dedicated continuous-%-HP broadcast (Live OP_MobHealth/OP_HPUpdate) either doesn't
exist here or needs a **targeted capture to isolate** — the current capture is a fast
high-kill-rate multi-mob capture that buries any faint HP signal. To settle it: con/target ONE mob and whittle
it down slowly (few hits, pauses) while logging, then re-run the drain test.

**Bonus lead — `0x5591` ≈ OP_BeginCast** (S>C, 19B, n=62). Not HP (was an HP suspect): its
fields read as `{spellId@0` (values 502/445/821/91… = the same spell-ids seen in Action2/
Death), `casterId u16@4` (41/62 ∈ live spawns), `castTime@6` (0/1500/2000/2500 ms)`} — a
spell-cast bar broadcast. Needs a dedicated spell capture to pin the exact layout before
wiring to `SpellShell` (the handler + `beginCastStruct` are already wired, awaiting the id).

**Table hygiene (2026-07-08):** reset the 63 stale Live opcode ids in `conf/eql/opcodes.toml`
to `ffff` (they never appear on the post-patch EQL wire — 0/63 fire in any capture; EQL
remapped every app opcode). No behavior change; the toml + opcode-stats now honestly show
what's unmapped (206 ffff / 13 confirmed) instead of masking gaps behind stale ids. The
handlers stay wired by name — supplying a real id is all that's ever needed.

### 2026-07-08 — OP_BuffWindow = `0x18e0`; level/exp/skill absent from the capture

A Legends zone-in + combat capture (no player chat present) was searched for the
XP/level/skill opcodes.

- **OP_BuffWindow = `0x18e0`** (S>C, 12B, n=10) — byte-identical to Live `buffWindowSlotStruct`:
  `slot u32@0` = 0..9, `spellid u32@4` = 0xffffffff (empty), `pad u32@8` = 0. Fires 10× at
  zone-in (one per short-buff slot). Remapped in `conf/eql/opcodes.toml` for table honesty,
  but **id-only** — no OP_BuffWindow handler is wired on eql, so it resolves in opcode-stats
  without surfacing anything (like OP_Animation).

- **OP_LevelUpdate / OP_ExpUpdate / OP_SkillUpdate — not present in this capture.**
  Value-based search found **no level-up event** (no `levelUpUpdateStruct`-shaped 24B opcode
  with a field rising to the current level, and no `{level=N, levelOld=N-1}` anywhere) and
  **no skill-up** (no `{skillId<75, rising value}` opcode) — the capture window does not span
  those events. Ruled-out exp false positives: `0x6007` (variable 130–346B list packet — a
  fixed offset lands on different fields per size, faking a sawtooth) and `0x4f7a` (a fixed
  200-element indexed stream `{0, counter 0..199, …}`, not per-kill exp; fires exactly 200×
  per capture regardless of activity). **To crack these:** a capture must **span a level-up
  event** — OP_LevelUpdate is then 1 fire per level gain (value = new level), skill-ups
  accompany it, and the pre-wired `Player::updateLevel`/`updateExp`/`increaseSkill` handlers
  light up on the id remaps.

## Confirmed (PRE-PATCH — ids dead as of 2026-07-07, kept for method/evidence)

### 2026-07-05 — OP_ClientUpdate = `0x0b03`

Capture: a char-create + login capture.
Method: `--dump-payload 0x0b03:…` (1160 fires) + differential decode.

- **OP_ClientUpdate = `0x0b03`** (C>S, 42 bytes, n=1160). Client self-position
  report. Sole 42-byte C>S opcode in the capture — zero competing unknowns at
  that size+direction.

**Confirmed 42-byte layout (LE)** — axes pinned by a `/loc` ground-truth clip
(a `/loc`-clip capture): three `/loc` readings
time-correlated (capture-time `--list-events`) to the float fields; spot 1
matched to within 0.5s exactly (X=−858.5→f@22, Z=41.4→f@34, Y=994.3→f@38).

```c
struct legendsPlayerSelfPos {   // OP_ClientUpdate 0x0b03, C>S, 42 bytes
/*00*/ uint16_t sequence;   // monotonic per-update counter
/*02*/ uint16_t entityId;   // player entity/char id (11653 in the sample zone)
/*04*/ uint16_t unknown04;  // 0
/*06*/ uint8_t  unknown06[4]; // near-constant (0xa3ed @08); role TBD
/*10*/ float    deltaY;     // north-south velocity  (Δ of y@38)
/*14*/ float    deltaZ;     // vertical velocity     (Δ of z@34)
/*18*/ uint16_t unknown18;  // angle 0-2047, NOT heading; role TBD
/*20*/ uint16_t unknown20;  // 0
/*22*/ float    x;          // EAST-WEST position   [CONFIRMED /loc]
/*26*/ uint32_t packed;     // heading = (packed>>10)&0x7FF, 11-bit, 0=North
                            //   [CONFIRMED /loc]; other bits = anim/deltaHeading
/*30*/ float    deltaX;     // east-west velocity    (Δ of x@22)
/*34*/ float    z;          // HEIGHT position       [CONFIRMED /loc]
/*38*/ float    y;          // NORTH-SOUTH position  [CONFIRMED /loc]
};
```

Evidence chain: position/velocity pairing by physics-consistency (Δpos vs vel
field, r=.81–.84); heading field located by motion-direction correlation
(R=0.97); **axis names + heading zero-point (0=North, ~1024=South) confirmed by
`/loc`** — running north drove y@38 up 994→2551 while x@22 held ~−830, and
heading read ≈0/2047 through the run vs 1054 (≈180°/South) at the turnaround.
Heading is **11-bit (0-2047 = full circle)** — the *legacy* EQ width, not
modern Live's 12-bit.

**Still open (minor):**
- off18-19: an 11-bit-range angle field, **not** movement heading — camera/look
  yaw? target heading? animation? (falsified as heading, R=0.08).
- off6-9: near-constant (`0xa3ed` @08) — role TBD.
- off26 low bits: animation + deltaHeading sub-fields not yet split out.
- id@2 persistence across zones (single-zone dumps so far — untested).

**Recon notes / `--replay-pcap` limitations found (2026-07-05):**
- `--dump-payload` follows only the daemon's **primary box** (first world
  session seen), so a multi-zone capture yields one zone's opcode stream. To
  decode a later zone, slice the pcap to its time window
  (`tshark -Y "frame.time_relative>=A && <=B" -w slice.pcap`) and replay that.
- `--replay-pcap` stamps `--list-events` with **replay wall-clock, not capture
  time** (the tcpdump path doesn't propagate `pcap_pkthdr.ts` like the .vpk path
  does). Fine for `--dump-payload`/`--opcode-stats`; breaks Mode-B time-window
  correlation. TODO: plumb `ph->ts` through the packet cache into
  `processPackets` when `PLAYBACK_FORMAT_TCPDUMP`.

**Next capture to finalize:** stand at a known `/loc`, face north (heading 0),
then walk a single cardinal direction a known distance; the axis whose absolute
value matches `/loc` names x/y, and heading=0 at north pins the heading zero.

### 2026-07-05 — OP_ItemPacket = `0x74b0`

Capture: loc clip + char-create (byte-identical 0x74b0 stream — same character,
same inventory). Method: `--dump-payload 0x74b0` + string/struct analysis.

- **OP_ItemPacket = `0x74b0`** (S>C, variable ~1.1 KB … 19 KB, n=174). **Bulk
  item transfer** — the character's full item set, sent on zone-in. (Initial
  "zone spawns" hypothesis was WRONG: no position floats; the payload is items.)

Framing (confirmed):
- `u32 @4` = **item count N** (=1 for the ~1.1 KB payloads, =17 for the ~19 KB
  ones — matches the item serial/name count exactly).
- Followed by N item records, **~1122–1130 bytes each** (length varies with the
  name string). A packet carries 1..~17 items.
- Per item record: 16-char ASCII serial (`"0000…0"`), item **name** (appears
  2×), a `"Trophy: …"` or `"Benefit: …"` descriptor, then stat fields. Item-id
  candidate = first u32 of the record (~56000–69000 range).
- `u32 @0` varies per payload (216/36/34… for single-item; 27/29/21 for
  multi-item) — likely a slot/container context; TBD.

Cross-validated: byte-identical 0x74b0 stream across two independent captures
(same character's inventory) — a strong identity check.

**Open:** the full ~1130-byte item record (id, stats, slots, class restrictions,
benefit/trophy ids, flags) — a large struct, its own decode pass. Expect class
fields to reflect the 3-classes-at-once design. `u32 @0` header semantics.

### 2026-07-05 — spawn opcode (OP_ZoneSpawns/OP_NewSpawn) = `0x7475`

Capture: loc clip (Nektulos). Method: `--dump-payload 0x7475` + string/struct
analysis. Found by scanning candidate S>C opcodes for ASCII names.

- **spawn opcode = `0x7475`** (S>C, variable 92…507 B, n=239). One spawn per
  payload — carries the spawn's **name + id**. NPCs present (`a_skeleton09/07/11`
  → ids 14268/14266/13667; `a_large_piranha05`), 163/239 are `a_`-prefixed
  NPCs; the player's own spawn also appears.

Structure: **null-terminated ASCII name** (variable length) + a fixed block —
**326 B for NPCs**, **470 B for players** (richer: equipment / 3-class etc.).

Decoded NPC block fields (offset within the fixed block):

| off | type | field | evidence |
|-----|------|-------|----------|
| 0   | u32  | **spawn id** | unique per mob; CONFIRMED — the player self-spawn's id here (14239) matches the 0x0b03 self id |
| 4   | u8   | **level** | type-appropriate ranges: bears 3–5, Deathfist legionnaires 7–9, piranhas 10–14, skeletons 4–6, snakes/wolves 1 |
| ~26 | —    | race/model | constant within a creature type, differs across types |
| 40  | u8   | **body type** | undead(skeleton)=3, humanoid(legionnaire)=1, animal=21 |
| 44–45 | u8×2 | **HP% (cur/max)** | 100/100 for all sampled spawns (full health) |
| 227 | i16 | **Z / 8** | height (fixed-point, 1/8 unit) |
| 231 | i16 | **X / 8** | east-west (fixed-point, 1/8 unit) |
| 241 | i16 | **Y** (unscaled) | north-south (integer units) |

**Position CONFIRMED** via two stationary named NPCs `/loc`'d point-blank
(Guard E`tru, Captain N`Farre): decoded X/Y/Z match their `/loc` to <0.5 units
AND reproduce the guard→captain deltas (dX=−2, dY=−36, dZ=−0.75). Validated
across all 163 NPC spawns → coherent zone coords (X [−861,1268], Y [−2180,2158],
Z median −5 = terrain height), 155/163 within zone bounds. **Mixed scaling**
(Y unscaled but X/Z ÷8) is just this build's shuffled layout — re-derive per
patch, don't memorize. `/consider` reaches ~200 u but here the player stood on
top of each NPC (Y matched within 1 u), which is what made the crack clean.

**Ruled out for spawns:** `0x3299` (S>C, 130–350 B, n=62) — numeric only
(repeated ids + 0xff padding), no names/coords.

### 2026-07-05 — OP_MobUpdate = `0x061b`

Capture: loc clip. Method: `--dump-payload` + cross-ref vs 0x7475 spawn
ids/positions + single-mob trajectory.

- **OP_MobUpdate = `0x061b`** (S>C, 14 B fixed, n=649). Per-mob position update.
  585/649 (90%) carry a spawn id matching a known 0x7475 spawn.

Layout (14 B):

| off | type | field |
|-----|------|-------|
| 0   | u32  | spawn id (low 16 bits) |
| 4   | i16  | **Y / 8** |
| 6   | i16  | **Z / 64** |
| 10  | i16  | **X** (unscaled) |
| 8–9, 12–13 | — | heading / delta (TBD) |

CONFIRMED: `a_rotting_citizen01`'s first update decodes to its exact 0x7475
spawn (874, 2158, −8); all 585 updates land in-zone; stationary mobs show
constant position bytes across many updates. **Yet another scale set**
(Y/8, Z/64, X unscaled) — different from 0x7475 (X/8, Y, Z/8) and 0x0b03
(floats). Re-derive per opcode; don't assume a shared convention.

**Ruled out:** `0x4566` (S>C, 12 B, n=200) is a periodic heartbeat/counter —
`[u32 0][u32 incrementing 0,1,2…][u32 const 0x6a4add7e]`, no id or position.

### 2026-07-05 — OP_NewZone = `0x5ab6`, OP_PlayerProfile = `0x5207` (both WIRED)

Capture: fresh Nektulos login. Legends **reuses classic EQ ids** (race/class/zone).

- **OP_NewZone = `0x5ab6`** (S>C, ~343 B). Null-terminated **shortName**
  (`"nektulos"`) + **longName** (`"Nektulos Forest"`) + **zonefile**. The short
  name drives `loadZoneMap` → **the map now loads**. This is the CURRENT zone.
- **OP_PlayerProfile = `0x5207`** (S>C, ~38 KB, once at zone-in). Header
  (`legendsCharProfileHdr`): **race u32@21** (6=DarkElf), **class1 u32@25**
  (5=SK), **level u8@33**. 2nd class (Shaman=10) is u32@**147**. A **bind-point
  array** starts @39 (5× {u32 zoneId=25, float x/y/z}) — that zoneId is the
  BIND zone, **not** current (verified: login pos ≠ bind pos), so the map uses
  OP_NewZone. Char name is deep (~@35551). Embeds inventory (item names/serials).
  Verified: race=6/class=5/level=3 matches ground truth.

**Profile-hunt gotcha:** the char name appears only in a name-stub (`0x4048`)
and the inventory bulk (`0x5207`), never a clean "profile" struct — find the
profile by its **id-cluster header** (race/class/level as u32 at unaligned
offsets), not by grepping the name.

### 2026-07-07 — OP_TargetMouse = `0x1bfe` (Target / UnTarget)

Captures: a combat capture (18 fires) +
a Nektulos `/loc`-clip capture (11 fires — different
spawn set). Method: `--dump-payload 0x1bfe` + **value-match** against the `0x7475`
spawn-id set (no timing needed — pure payload cross-check, so robust to the
`--replay-pcap` capture-time gap).

- **OP_TargetMouse = `0x1bfe`** (C>S, 4 bytes). Player target selection.
  Payload = `{u32 spawn_id}`; **spawn_id = 0 = clear target (untarget)** — Target
  and UnTarget are the *same* opcode.

Confirmed: **18/18** fight payloads + **11/11** locclip payloads resolve to either
0 (untarget) or a live `0x7475` spawn id, zero misses. Fight sample hit 6 distinct
named mobs (`a_fire_beetle10`, `a_skeleton05`, `a_decaying_skeleton01`,
`a_moss_snake03`, …); locclip (different zone) hit `Kirak_Vil00` + `a_spiderling02`.
No competing spawn-id-carrying C>S opcode exists (the other small C>S opcodes are a
toggle / constants — see candidates), so the ID is unambiguous.

**WIRED** (`wire_eql.cpp`): the Legends payload is byte-identical to Live's
`clientTargetStruct`, so `0x1bfe` wires straight to the existing neutral
`SpawnShell::clientTarget` — **no Legends-specific code, no new decoder, proto, or
web**. The whole chain was already backend-neutral: `clientTarget` → `emit
targetSpawn` → `SessionAdapter::onTargetSpawn` → `Targeted` envelope → web
(`App.tsx`, `spawn_id=0` = untarget). Verified via golden replay: 36 `Targeted`
envelopes, real ids = the 6 confirmed named-mob targets + untargets.

## Candidates (unconfirmed)

- `0x71fc` (C>S, variable 18…2483 B, n=553) — char-create / inventory upload?
- `0x0dba` (S>C, ~1.1 KB) — large periodic (another item packet? — compare vs 0x74b0)
- **`0x0d9c` + `0x2d07`** (both S>C, 8 B, n≈63/54) — per-mob `{u32 spawn_id, u32 flag}`
  state broadcasts; every id is a live `0x7475` spawn. flag is **not HP** (constant 4,
  occasionally 32/64, doesn't drain during a fight) — looks like an aggro/stance
  state (a given mob flips 4↔32 over time). Two near-identical opcodes carrying the
  same shape — possibly enter-state vs leave-state.
- **`0x6704`** (C>S, 4 B, n=14) — value toggles `2/0/2/0`: auto-attack on/off (carries
  no spawn id, so not a target).
- **`0x26db`** (C>S+S>C, 21–24 B, n=34) — item opcode: `{u32 item_id, u32, u32 namelen,
  ASCII name+'*'}` (`"Short Sword"` id 9998, `"Bandages"` id 21779). Inventory/item, not combat.
- **`0x42fe`** (C>S, 8 B, n=19) — constant `{225, 13}` every fire; periodic client
  keepalive/heartbeat, not a targeted request.
- **`0x2824`** (S>C, ~18 B, **n=2952**) + **`0x7f8a`** (S>C, variable, n=767) — the two
  highest-volume unknowns in a fight; combat/animation per-tick stream? unhunted.

**Con (`/consider`) — NOT present in the fight captures (2026-07-07).** Ruled out by
exhaustion: the only C>S opcode carrying a *varying* target spawn id is `0x1bfe`
(target); there is no second targeted C>S request, and no S>C reply carrying
faction/level/HP for a con color. The player targeted + fought but never `/con`'d.
**To crack Con, capture a dedicated `/consider` session** — con several *named* mobs
of *known, varied* level/faction, ideally without attacking so the reply isn't buried
in combat spam. Expect the reply to carry `{target spawn_id, level, cur_hp%, faction}`;
cross-check level vs the mob's `0x7475` block +4 and HP% vs block +44/45.

### 2026-07-08 — OP_NewZone = `0x1dbf` (WIRED); the profile-@36211 zone hack + 0x4bc8 bind marker are gone

Captures: `login-zone` (nektulos), `upperguk` (guktop), `fulllogin` (unrest).
Method: **size-agnostic value-match** — dumped every low-fire S>C opcode from two
different-zone captures and searched for a field carrying the *current* zone
(guktop=65) vs bind (nektulos=25), then an ASCII zone-name search across the dumps.

- **OP_NewZone = `0x1dbf`** (S>C, ~340B, once per zone-in). Payload is **packed
  null-terminated text**, not fixed-width arrays — the offsets shift per zone:
  ```
  short_name\0  long_name\0  <3 pad>  zonefile\0  <5 bytes>  u32 classic_id  f32 1.0(exp) …
  ```
  guktop / "The City of Guk" (339B), nektulos / "Nektulos Forest" (343B), unrest /
  "The Estate of Unrest" (344B) — each the correct current zone, each a different
  size. The daemon uses `short_name`+`long_name` directly (no id table).
  `parse_legends_new_zone` reads the two C-strings; `EqlDispatch::newZone` →
  `ZoneMgr::setZoneByName`.

**Wiring (the important part).** On eql the zone name arrives **AFTER** the profile
(0x62f0) AND the bulk `OP_ZoneSpawns` list (121 spawns in fulllogin fire before
NewZone, only 9 after). Each zone-in is a **fresh Box** (own ManagerSet), so no
clear/reset is needed. But `zoneChanged` (which the old profile hack emitted) drives
`SpawnShell::clear` + `Player::reset` — firing it at NewZone time would **wipe the
already-loaded spawns + identity**. Fix: a new eql-only **`ZoneMgr::zoneResolved`**
signal drives only map load / filter overlay / web `ZoneChanged` envelope — never the
clear/reset slots. `setZoneByName` now emits `zoneResolved`, not `zoneChanged`.
Verified on the guktop golden: zone=guktop, **259 spawn_added survive**, player
race=6/class=5/level=16 survives, 1 `zone_changed` envelope. Live goldens 17/17 green.

- **`0x4bc8` renamed `OP_ZoneBindMarker`** (was mis-labeled OP_NewZone). 14B S>C, once
  at zone-in, byte-identical across zones: `{…, u32@6 = bind zone (=25 nektulos), u16@10
  = 10, u16@12 = 25}`. Carries only the BIND zone — no current-zone field. Unwired.

**The profile `u16@36211` current-zone read is REMOVED** from `parse_legends_profile`
(it was a fragile deep offset in a ~40KB variable-length payload). Profile is now
identity-only on eql.

**Lesson (carry forward):** don't gate an opcode hunt on matching packet *size* —
the current-zone opcode is a different length in every zone. Search by value /
content across captures, not by fixed offset or fixed size.

**Recon side-notes:** the `login`/`fulllogin` captures have current==bind==25
(char logged out in its bind zone), so they can't distinguish current-vs-bind on their
own — the **guktop capture is the discriminator** (current=65, bind=25). The
`--dump-payload` flag is repeatable; dumping ~100 candidate opcodes across two zones +
a Python offset scan is the fast path.

**Follow-ups (2026-07-09).**

- **Deterministic refilter emission (shared-core fix, daemon `2071b04`).** Wiring
  OP_NewZone via `zoneResolved` makes the zone filter overlay (`FilterMgr::loadZone`)
  load *after* the spawn burst, so it re-filters already-loaded spawns — which exposed
  a latent bug: `SpawnShell::refilterSpawns` / `refilterSpawnsRuntime` iterated the
  spawn `ItemMap` (a QHash) and emitted `changeItem(tSpawnChangedFilter/RuntimeFilter)`
  in per-process-random hash order → a non-deterministic `spawn_added` stream (tier-2
  goldens flapped). Fix: collect the changed items, sort by `(id, name)` (same key as
  `sendSnapshot`), then emit — order-only change to an idempotent stream (clients key
  by id). **Live tier-2 17/17 unchanged.**

- **eql tier-2 goldens started.** 4 byte-stable fixtures recorded (gitignored /
  dev-local): `chat`, `login-zone`, `upperguk`×2 — `check.sh` = 4 pass / 1 skip / 0 fail
  (per-backend auto-detect from `build/CMakeCache.txt`). **`fulllogin` is SKIPPED** — it
  has a rare (~1-3%), load-only replay-harness timing heisenbug (3 extra `spawn_added`
  re-renders for summoned NPCs; binary golden outcome). Ruled out: QHash seed (31
  `QT_HASH_SEED` values identical), the 100ms replay-pump batch window (packet.cpp:607),
  the datetimemgr timer, and a data race (`.vpk` replay is single-threaded); any
  instrumentation suppresses it. Not a decode bug — a flappy golden would false-fail the
  pre-push hook, so it stays ungoldened until the harness is made wallclock-deterministic
  (would also fix the `buffs` skip). See memory `project_eql_golden_spawn_order_flap`.

## OP_Stance (0x0fab) + OP_Invocation (0x3b12) — active stance/invocation (2026-07-16)

A Legends character has one active **STANCE** and one active **INVOCATION** at a
time (swappable). Activating one fires a 4-byte opcode carrying a single
`u32 abilityId` little-endian @0 (`activateAbilityStruct`). The client sends the
activation request C>S and the server echoes it back **S>C (authoritative)** — the
daemon wires only the S>C echo (the C>S copy is acked `uint8_t`/`none` so it
doesn't warn). Both opcodes share the identical 4-byte struct; the **opcode id**
distinguishes stance from invocation.

**Ability id → name** (the `abilityId` is a **stable client enum** from the
`eqgame.exe` `GetAbilityName` switch; the **opcode ids are capture-RE'd and
patch-volatile**, so they live in `conf/eql/opcodes.toml`, not hard-coded):

- **OP_Stance (0x0fab):** 117 Offense · 118 Defense · 119 Evasive · 120 Balanced ·
  121 Mage Hunter · 122 Striker · 123 Berserker · 124 Ranged · 135 Channeler.
- **OP_Invocation (0x3b12):** 125 Recover · 126 Empower · 127 Inversion ·
  128 Spell Blade · 129 Over Channel · 130 Inviolable · 131 Divine · 132 Chained ·
  133 Arcane Mastery · 134 Unyielding.

The eqstr display strings carry a " Stance" suffix; we store the **bare** names
above (e.g. "Defense", "Recover"). Unknown id → `#<id>` fallback so it stays visible.

**Pipeline** (mirrors OP_BeginCast + PlayerStats.class_mask): `parse_activate_ability`
(seq-backend-eql, `("activateAbilityStruct", 4)` in `size_overrides()`) → cxx
`decode_activate_ability` → `EqlDispatch::stance`/`invocation` resolve the id to a
name via backend-only `stanceName`/`invocationName` tables → `Player::setStance`/
`setInvocation` (neutral QStrings) → `PlayerStats.stance` (30) / `.invocation` (31)
→ web PlayerPanel ("Stance: Defense · Invocation: Recover"). Struct declared in
`backend/live/everquest.h` + `s_everquest.h` size registry; SZC_Match gate.

**Verified 2026-07-16** against `eql-stancedance.vpk`: `--opcode-stats` shows both
`known` (S>C 4 / C>S 4, size 4), **0 "doesn't match"** warnings; the handlers decode
+ resolve correctly (stance 117→Offense, 118→Defense; invocation 127→Inversion,
125→Recover). The capture is mid-session, so `player_stats` may not re-emit after the
stance packets (setStance fires no signal — the value surfaces on the next
stat-driven `PlayerStats`); in a live session that is near-immediate.

**Scope confirmed 2026-07-20 — SELF ONLY, not a group/zone broadcast**
(`eql-group-stance.vpk`, two grouped characters, one capture-scoped client IP).
In the 21s between the client's own last swap and the group disband, the grouped
partner swapped stance + invocation while the client decoded 620 packets
(577 S>C, no silent second) and logged **zero** `0x0fab`/`0x3b12` fires. Positive
control: the client's own swaps 4s earlier echoed 1:1 with their C>S requests.

Swept the same window for a *different* carrier and found none:
- 12 distinct unknown S>C opcodes fired; only `0x7167` and `0x334e` have the
  right shape (8B). Both are ambient — normalized per 100 S>C they run
  **higher** in a grouped-but-no-swaps control window (2.2 / 2.5) than in the
  partner-swap window (1.7 / 1.4) — and across 161 fires their second u32 only
  ever takes `{4, 32, 64}` (a bitmask), never an ability id in 117–135.
- `OP_SpawnAppearance2` (the obvious state-broadcast carrier) never carries a
  stance id either: types seen are 1/5/6/8/16/22/26/36/41/43 with unrelated values.

Remaining gap: this cannot exhaustively disprove a stance field buried in a
high-frequency per-spawn opcode, but every structurally plausible carrier is
ruled out. **Do not build group-member stance display on this opcode.**

**C>S request size — 4B here, 9B reported externally (2026-07-20)**

An external report has ~half of C>S activation requests arriving as 9B
(ability id + an extra u32 + a trailing zero), which warns on a size-matched
client payload. We cannot reproduce it. `eql-stance-sweep.vpk` cycled every
stance and invocation the character had — **16 distinct abilities, 16 requests,
all 4B**, matching the 7/16 and 7/17 captures:

- stance `0x0fab`: 117, 118, 119, 120, 121, 122, 124, 135
- invocation `0x3b12`: 125, 126, 127, 128, 129, 131, 133, 134

So the 9B form is **not ability-specific** across the 16 tested. Untested (the
character lacks them): 123 Berserker, 130 Inviolable, 132 Chained. Next
hypotheses are client build and input method (hotbar vs menu vs slash command)
— worth asking how the swaps were performed before hunting further.

Regardless of cause, the client payload stays `uint8_t`/`sizechecktype=none`:
we never decode the request, so the variance costs nothing and a size-match
would only reintroduce the warnings.

---

### 2026-07-28 — third full rotation: p10 core re-mapped from a post-patch capture

The 07/28 patch rotated the table again: **2 of 233 opcodes still resolved**.
Legacy `upstream/showeqlegends` has nothing past the 07/14 patch, so there was
nothing to import — this is hand-derived from a single long post-patch capture.

Method, and why NOT the obvious one: every identification below is CONTENT or
BEHAVIOUR. No size matching, in either its explicit form or its disguised form
(probing with a size-gated parser and calling a small candidate set
"selectivity" — that parser rejects anything of the wrong length, so a struct
that grew hides the true opcode and surfaces a coincidental one).

| opcode | id | evidence |
|---|---|---|
| OP_PlayerProfile | `50e5` | the eql profile parser decodes the capture's character name out of it; 13 fires = 13 zone-ins |
| OP_ZoneEntry | `5c7f` | 8198 fires but only 30 carry the self name — 2 per zone-in, the known eql double-announce. Payload is `cstring name` then `u32 spawn_id`; concentrated in zone-in windows |
| OP_ClientUpdate | `5bfd` | carries a real spawn id at offset 2 in BOTH directions — the self-position signature |
| OP_MobUpdate | `26d8` | spawn id at offset 0; 38301 fires, second-highest rate |
| OP_NpcMoveUpdate | `3e7b` | highest rate in the capture (91869), and its id is NOT byte-aligned — consistent with the BitStream packing |
| OP_HPUpdate | `3139` | spawn id at offset 0; 34237 fires across several payload forms — the multiplexed stat-sync channel |
| OP_Death | `2087` | first in the death→removal chain: 99% of its ids are later named by both removal opcodes, never the reverse; the spawn goes silent (corpse) |
| OP_RemoveSpawn | `27cb` | second in the chain (98% followed by the final removal); fires more often than deaths, so it also covers spawns leaving range |
| OP_DeleteSpawn | `1856` | last in the chain — never followed by either other removal |
| OP_Action2 | `7df9` | quotes TWO spawn ids (offsets 0 and 2) — the source/target pair |

**Verified end-to-end**, not just per-opcode: replaying the capture through the
full pipeline yields 8198 spawns / 234951 position updates / 6542 combat / 4966
removals / 4227 kills / 3470 player-stat updates, 13 undecodable out of 440298
packets. Spawn names, levels, races and positions all read sane.

**Structs did NOT drift this rotation** for any of the above — the existing eql
parsers decode the new ids unchanged. (An earlier pass here claimed ZoneEntry
and HPUpdate had changed; that was a bug in the validator — it rejected spawn
names containing digits, which most are.)

**Method that worked, for the next rotation.** Confirm ONE opcode by decoded
content, then bootstrap off it: a confirmed OP_PlayerProfile is a zone-in clock
(whatever storms after it is the zone-in set), and a confirmed spawn opcode
yields the zone's id set, which every movement / HP / despawn opcode must quote
back — that pins those opcodes AND their id-field offsets in one pass. Ordering
between opcodes that all reference a spawn is recoverable from succession
(death precedes removal, never the reverse).

**Still open at p10:** OP_NewZone, OP_ZoneChange, OP_SelfPos, OP_SpawnAppearance
(zone name is still empty in a replay, so NewZone is the next one to take).
Below p10 the ladder is untouched — 71 opcodes are actionable in total, the
other 149 prioritised entries are p-1 and are not hunted.

#### 2026-07-28, continued — NewZone, SelfPos, and a struct that DID drift

- **OP_NewZone = `47f2`** — decode-verified: yields real zone short/long name
  pairs (`rathemtn`/The Rathe Mountains, then feerrott, innothule, sro, oasis,
  nro …), 13 fires = one per zone-in. Found by anchoring on zone names the
  player reported visiting — zone names are plain text on the wire, so a
  remembered destination is a free anchor. Replays now resolve the zone name.
- **OP_ZoneServerInfo = `4b8f`** — carries the zone server hostname as a leading
  string. Load-bearing for the DAEMON (it binds a box's zone stream off this);
  scry does not need it.
- **OP_SelfPos = `6323`** — the C>S position-history breadcrumb. Identified by
  content: it is the only C>S opcode carrying the player's own coordinates, and
  its records decode to a coherent walking trajectory. NOTE its backend arm
  parses and then deliberately returns `Ignored`, so "it decoded to nothing" is
  the CORRECT result here and is not evidence against the mapping.

**OP_ClientUpdate's position fields drifted this patch.** The id at offset 2 is
right (that is what identified it, in both directions), but the decoded self
position is a degenerate `{0,0,4}` while the breadcrumb from the same session
decodes to real coordinates. So the packed position bitfields moved — a reorder
keeps the struct SIZE identical, so no size check can catch it, exactly as the
Live 07/15 rotation did.

The re-derivation has ground truth available: `6323` gives the player's real
position over time, so `5bfd`'s bitfields can be solved against it rather than
guessed. That is the next piece of work on this backend.

#### 2026-07-28 — p9/p8

| opcode | id | evidence |
|---|---|---|
| OP_CommonMessage | `37fa` | decode-verified: yields real say-channel chat text with sender; both directions |
| OP_Consider | `64c4` | decode-verified: 24B in BOTH directions (request + response) quoting real spawn ids |
| OP_GroundSpawn | `5331` | decode-verified: `id_file` reads `IT###_ACTORDEF`; fires in the zone-in burst where ground items are placed |
| OP_SpawnDoor | `639d` | decode-verified: sequential door ids, `OBJ_*` names, sane coords, 132B rows (record size unchanged) |
| OP_NewGuildInZone | `63a5` | cross-referenced: every payload names a guild ALREADY known from the confirmed OP_GuildsInZoneList |

**Still open at p9/p8: OP_ClickObject, OP_TargetMouse, OP_Illusion,
OP_LevelUpdate.** All four are weakly-constrained: their parsers read a bare id
(or are absent from this capture), so "it parsed" is worthless and the usual
cross-references dilute — the ground-item id set is 113 small integers, which
almost any u32 matches. TargetMouse in particular produced only already-assigned
opcodes (RemoveSpawn/DeleteSpawn), which is the id-read parser accepting
anything id-shaped.

These need a capture with the action isolated: target a few mobs deliberately,
pick up a ground item, cast an illusion, and ding. Each becomes a countable
event with a known time, which is what the priority-10 work had and these lack.

#### 2026-07-28 — reconciled against upstream (legends `7612d72`)

Xerxes published the 07/28 remap. Cross-checking it against the table derived
here from captures: **17 of 17 independently-derived ids match upstream exactly**
— ZoneEntry, PlayerProfile, ClientUpdate, MobUpdate, NpcMoveUpdate, HPUpdate,
Death, RemoveSpawn, DeleteSpawn, Action2, NewZone, GuildsInZoneList,
NewGuildInZone, CommonMessage, Consider, GroundSpawn, SelfPos. That includes the
Death → RemoveSpawn → DeleteSpawn assignment, which was derived purely from
succession (99% of Death ids are later named by both removals, never the
reverse) with no reference to upstream.

Adopted 51 further ids from upstream for opcodes never re-derived here.

**Three where we differ, and why:**

- **OP_SpawnDoor → took upstream's `3f1b`.** Ours (`639d`) decodes as a door and
  is a real door-shaped opcode, but upstream's carries 4092/6732-byte payloads =
  31 and 51 rows of 132, i.e. the zone's door ARRAY. `639d` is single 132-byte
  rows that a chunking parser accepts just as happily. Recorded as a lead: what
  is `639d`?
- **OP_ZoneChange → kept ours (`727c`).** Upstream leaves it unmapped and its
  `3360` fires ONCE at 484 bytes in a capture containing 13 zone-ins. Ours is
  exactly 100 bytes (`zoneChangeStruct`: charName[64] + zoneId + instance) with
  the character name at offset 0, firing 53 times across the transitions on both
  the world and zone streams.
- **OP_ZoneServerInfo → kept ours (`4b8f`).** Upstream's `0861` appears zero
  times in the capture; ours is 130 bytes (`zoneServerInfoStruct`) carrying the
  zone-server hostname, once per zone-in. Upstream's patch remaps the ZONE set
  only, so its world table is still pre-patch.

**Field boundaries taken from upstream** for the self-report facing: bits 20..30
of the dword at 22 (2048/circle), replacing the locally-bounded 15-bit read. The
SENSE remains ours and measured — compass, not inverted — because upstream's
player.cpp contradicts its own struct on the offset, and a spin capture backs
the struct.

#### 2026-07-28 — date-filtered the upstream adoption

Only entries upstream dates `07/28/26` were re-hunted for this patch — 70 of
its ~250 mapped rows. The rest carry dates from 2004-2023, legacy leftovers
never re-verified for EQL. All 51 ids adopted here came from the 07/28 set (the
older rows agree with what we already inherited from legacy, so they never
changed anything).

Separately, **26 of our own entries carried pre-patch provenance and have been
returned to `ffff`**: every one fires ZERO times across the 440k-packet capture
on the current patch. The world set rotated too — upstream's patch remaps the
ZONE set only, so its world table is as stale as ours was. Retiring them is not
cosmetic: a stale id is live ammunition the moment a rotated opcode lands on it.

This also corrects a claim in the entry above. Of the 34 ids that "agreed" with
upstream, only the **17 derived here from captures** are real validation; the
other 17 agreed because BOTH tables carried the same stale legacy value.
Agreement with an unverified source is not evidence.

#### Code fixes ported from upstream (legends `7612d72`)

- **Loadout swap re-creates the spawn.** Legends does delete-then-readd, so the
  embedded record IS the re-add; surfacing only the changed fields drops the
  spawn and the next position update resurrects it as "Unknown".
- **Melee sentinel.** eql's action2 marks "no spell" as -1; the backend arm cast
  that to u32, so 5447 of 6542 combat events reached consumers as spell id
  4294967295 against a contract of `0 = melee`.
- **Spell-id width** needed no change — our eql paths already read them wide,
  and this wire confirms upstream's rationale (BeginCast tops out at 74073).
- Not ported: the buff-window caster filter (our BuffList event is already
  per-owner), and the statlist SIGFPE / Qt4 guards (legacy-UI only).

#### 2026-07-28 — positions were wrong; spawn block re-derived

Prompted by upstream's `spawnStruct posData` change, a cross-check found the
position sources disagreeing with each other: ZoneEntry vs MobUpdate sat a
median **3051 units** apart. Names, levels and races decoded fine throughout,
which is exactly why this went unnoticed.

**Spawn block — FIXED and verified.** The block was rearranged and one word is
now a PAD that always reads 0; the old layout read it as a coordinate, hence a
wall of spawns at y=0. Located against the player's own position (the breadcrumb
is verified ground truth, and the player's own spawn arrives as a ZoneEntry
record), 30 own-records agreeing on:

```
lead | X | Z | pad | Y | trail      X @len-95, Z @len-91, Y @len-83
```

Upstream's struct describes the same 5-word shape including the pad. Verified:
the player's record now decodes to exactly the breadcrumb position, and y=0
spawns fall from a wall to 19 of 8198. The spawn FACING did not survive — the
word it rode is now Y — so it reports 0, as upstream also leaves it unmapped.

**Still wrong — next up:**

- **OP_MobUpdate** sits a median 324 units from the corrected ZoneEntry position
  of the same spawn (18% within 10 units). Partly confounded by mobs genuinely
  moving between an entry record and a later update, so re-measure against
  STATIONARY spawns only (spawns whose entry position never varies) before
  concluding.
- **OP_NpcMoveUpdate** is the higher-volume one (113k vs 50k) and is wrong: a
  bit-offset solve against stationary spawns puts a coordinate at bit 16,
  immediately after the 16-bit spawn id, where the current parser expects 16
  bits of padding plus a 6-bit field mask — i.e. the pre-patch BitStream framing
  is gone. Only one axis was located; x matched nowhere, so the layout is NOT
  solved. Upstream documents x@bit160 / y@bit64 / z@bit96, but bit 160 is byte
  20 and our payloads are 15-19 bytes, so its struct describes a longer variant
  than this wire carries — do not port it blindly.

Both need the same treatment that worked for the spawn block: solve against
stationary spawns whose position is known exactly, rather than porting offsets.

#### 2026-07-28 — position entries removed (they were wrong)

Five entries covering the 07/28 MobUpdate / ZoneEntry position work were deleted
on 2026-08-03. They concluded that "ZoneEntry's x/y are transposed relative to
MobUpdate" and swapped the spawn record and the self position to match. ZoneEntry
was never relative to MobUpdate — MobUpdate was simply mislabelled, so every
conclusion drawn against it as ground truth was wrong, and the chain is misleading
enough that keeping it costs more than it documents. See the 2026-08-03 entry at
the end of this file for what the packets actually say.


### 2026-07-28 — p9/p8 verification of the ids adopted from upstream

Four ids were taken from upstream's 07/28 patch without local confirmation.
Verified three against `eql-patch28july` by CONTENT (not size); the fourth has
no data.

**OP_LevelUpdate = `70c6`** (S>C, 5 fires) — CONFIRMED, and it corrects a
standing wrong conclusion. Reading the payload head as the stock
`levelUpUpdateStruct` gives:

```
level=2 levelOld=1   level=3 levelOld=2   level=4 levelOld=3
level=5 levelOld=4   level=6 levelOld=5
```

Five consecutive dings with `levelOld == level - 1` every time, against a
capture of a character leveling 1→6. **eql DOES have a discrete level packet** —
the earlier "exhaustively confirmed it has none" entry is superseded. It is an
80B widened container; upstream notes the head is the stock layout, which the
byte evidence agrees with.

Two consequences:

1. The binding was gated `sizechecktype = "match"` against
   `sizeof(levelUpUpdateStruct)`, so every one of these was dropped on the size
   check. Now `none` (upstream made the same change). `Player::updateLevel`
   already clamps its read to the struct size, so the wide tail is harmless.
2. `EqlDispatch::expUpdate` was DERIVING dings by treating an exp decrease as a
   level-up — a heuristic adopted only because the level packet was believed not
   to exist. Removed. Note this was never *visibly* broken: the heuristic
   produced the same 1→6 trajectory, so the packet being dropped was invisible
   in the output. What changes is that an authoritative absolute level now
   drives it instead of a guess, the ding lands at the real moment rather than
   at the next exp wrap, and the guess can no longer double-count against the
   wire value (or mis-fire if eql ever gains a death XP penalty).

**OP_TargetMouse = `3897`** (C>S, 483 fires) — CONFIRMED. Read as
`clientTargetStruct` (`u32 newTarget`), 260 values resolve to a spawn id
announced in the same capture and the remaining 223 are all exactly `0`, the
deselect sentinel. No value fails to be either.

**OP_ClickObject = `23ee`** (S>C, 16 fires) — CONFIRMED. Read as `remDropStruct`
(`u16 dropId@0`, `u16 spawnId@4`), `dropId` matches a ground drop previously
announced by OP_GroundSpawn in **16 of 16** records; `spawnId` is either a spawn
in the capture (the picker) or 0.

**OP_Illusion = `5201`** — NOT VERIFIED. It does not fire once in any capture on
hand, so there is nothing to check the id or the layout against. Flagging one
risk for whoever gets a capture with an illusion in it: it is wired
`spawnIllusionStruct` / `SZC_Match`, which is exactly the gate that was silently
eating OP_LevelUpdate. If the eql packet is a widened container like that one,
it will be dropped with no error. Check the wire size before trusting silence.

### 2026-07-28 — OP_Action spell field does NOT truncate (concern retracted)

The open question was whether `OP_Action`'s spell id is wider than the `u16@4`
we read — the lead being a possible `u32` and an apparent signal at an unaligned
`u32@3`. Tested all three candidate reads against 3092 OP_Action packets, scored
by how many distinct values resolve to a real spell-DB name:

| read | valid spell ids |
|------|-----------------|
| `u16@4` (what we do) | **101 / 101** — 89/89 in the 64B form, 12/12 in the 88B form |
| `u32@3` (the old lead) | 133 / 327, max 18962753 — garbage |
| `u32@4` (the widening) | 74 packets yield values above 65535, none of them spell ids |

`u16@4` is correct and complete. Every distinct id in the capture is a real
spell, so nothing is being cut — a truncating read would wrap a wide id to a low
value that either fails to resolve or names the wrong spell, and neither
happens.

The `u32@3` signal was the artifact it looked like: that read straddles `source`'s
high byte, the spell, and the field above it.

The `u32@4` failures pin down what the bytes above the spell actually are. `u16@6`
is `0` in 3018 packets and `1` in 74 — a flag, part of `unknown0006[6]`, NOT a
high half. Reading a `u32` there folds it in as +65536 (74014 = 65536 + 8478),
which is exactly the 74 bad values. So the field must NOT be widened to u32.

Upstream agrees on the layout: the legends branch still declares the field at
offset 4 with the rest of `unknown0006` above it. It declares it `int16_t`, where
our live/test/eql all use `uint16_t` — the correct choice, since a signed read
sign-extends any id above 32767. That divergence is deliberate; keep it.

**Separate gap found while checking this** (not fixed here): the daemon wires
both `OP_Action` payloads to `SpellShell::action`, but `seq-backend-eql`'s
`Backend` impl has an arm for `OP_Action2` only — there is no neutral event for
`OP_Action`. Consumers on the neutral contract (scry) therefore see none of
these 3092 packets. Same shape of gap as OP_LevelUpdate had.

### 2026-07-29 — EQL rotated the whole opcode table again (p10 tier re-mapped)

EQ Legends patched on 07/29, one day after the 07/28 rotation, and renumbered
**every** application opcode. Measured on a capture of a fresh login plus several
zone-ins: **zone 145 distinct / 0 known, world 38 distinct / 0 known.**

The daemon itself was healthy throughout — capture, pcap filter and the SOE
stream layer all worked (sessions decrypted, fragments reassembled, app packets
emerged at correct sizes and directions). Only the ids were stale. The control
that proves this: replaying the previous day's capture through the same build
still decoded 217 zone opcodes clean.

**Presenting symptom**, worth recognising on sight: zoning logged
`SessionDisconnect detected, awaiting next zone session` and then went silent
forever. `OP_ZoneServerInfo` no longer decoded, so no box learned the zone server
port and the next zone session could never bind. It reads like a BoxRegistry
routing bug and is not one — check known-vs-unknown in `--opcode-stats` before
touching routing code.

**Why this was a real rotation and not a coincidence.** Aligning the world
handshake by (direction, size, wire order) against the previous day: 12
size-matched packets, **0 kept their id**. The handshake also gained 13 packet
sizes with no prior-day counterpart (12976, 305, 164, 92, 80, 72, 64, 60, …), so
the protocol changed shape rather than merely renumbering. Per-session
obfuscation was ruled out: the prior capture holds 13 separate world sessions
over 2+ hours and 12 of 13 share a byte-identical id sequence, so ids were stable
before the patch and are not session-keyed.

**Confirmed p10 mappings** (prior id → 07/29 id), each anchored on content, never
on size alone:

| opcode | was | now | evidence |
|--------|-----|-----|----------|
| OP_ZoneEntry | 5c7f | **5aaf** | NUL-terminated spawn name at 0, title string at the same offset as before; layout byte-identical. 902 records, 493 distinct ids |
| OP_NewZone | 47f2 | **0a2e** | leads short name, long name, short name again — prior-day shape |
| OP_ZoneChange | 727c | **439a** | char-name field at 0 plus identical constant block at 0x20 |
| OP_PlayerProfile | 50e5 | **014a** | 40546b once per zone-in; head byte-identical at 0x14, 0x24, 0x30 |
| OP_MobUpdate | 26d8 | **4eda** | 14b, spawn id at 0 in 229/229, cross-referenced to the ZoneEntry id set |
| OP_HPUpdate | 3139 | **0daa** | size histogram matches exactly (6/21/37/53/7/5); id at 0 in 246/246 |
| OP_NpcMoveUpdate | 3e7b | **2f15** | histogram 18/17/15 matches; no byte-aligned id, as expected for the BitStream |
| OP_SpawnAppearance | 384f | **03f8** | 24b, id at 0 in 96% of 157 |
| OP_RemoveSpawn | 27cb | **0113** | 5b (+4b variant), id at 0 in 61/61 |
| OP_DeleteSpawn | 1856 | **1f3d** | 4b bare id — u32 high half zero 39/39, low half a known id 37/39; rules out OP_Animation, same size but non-zero bytes 2-3 |
| OP_SelfPos | 6323 | **16ac** | every length is exactly 1 + N×17 (N = 1, 3, 85, 133, 320) — the breadcrumb fingerprint |
| OP_ClientUpdate | 5bfd | **5380** | id at 0 in the S>C form (146/146), at 2 in the C>S form (161/161) |
| OP_ZoneServerInfo | 4b8f | **5171** | world, 130b, zone server hostname as leading string, structure byte-identical |

Every remaining previously-mapped id (57 of them) was reset to `ffff` with its
prior id preserved in the comment. Zero of those stale ids collided with an id
active on the new wire *in this capture*, so nothing was actively mis-firing —
but 57 wrong ids against ~174 live ones is a collision waiting to happen, and a
mapped-but-wrong id mis-decodes silently.

**Structs did not drift, with one exception.** Every confirmed opcode above
decodes on its existing struct — the patch renumbered without reshaping. The
exception is `OP_ClientUpdate`, which grew **+4 bytes in each direction**
(S>C 24→28, C>S 38→42) and rearranged **both** bodies — no prior-day offset
survives in either direction. Its geometry was left un-derived on the first pass
and the size gate deliberately dropped these; both are now solved — see the
follow-up section below.

**Verified end to end**: zone binding restored (`expects zone server port …`),
zone name and map resolve, and a recorded golden carries 907 spawn_added, 2187
spawn_updated, 40 spawn_removed, 28 player_stats and 3 zone_changed.

**Pre-existing bug surfaced, not introduced here.** eql `OP_SpawnAppearance` is
24 bytes on the wire but its `SZC_Match` gate inherits Live's
`sizeof(spawnAppearanceStruct)` = 8, so every fire is dropped. The prior day's
capture shows 10232 fires, all discarded — this opcode has never decoded on eql.
Read as the wide layout (`u32 spawnId@0, u32 type@4, u32 value@8`, 12b pad) the
157 fires here give 44 valid spawn ids, 12 distinct types {22, 43, 5, 6, 26, 41,
8, 3, …}, values {0, 1, 7, 100, 110} and an all-zero tail in 134/157. Read as
legacy's `u16/u16/u32` shape, `type` is **0 in all 157 packets** with the real
type values landing in `parameter` — the identical failure signature recorded for
Live on 07/28. So eql's OP_SpawnAppearance carries the wide record that we named
`spawnAppearance2Struct` for Live's separate 24-byte opcode; on eql there is only
the one opcode and it is already the wide form. **Fixed 2026-07-30 — see below.**

### 2026-07-30 — OP_SpawnAppearance: backend-owned gate size + eql's own layout

Two bugs were stacked here, and neither is visible while the other stands.

*The gate.* `spawnAppearanceStruct` had no `size_overrides()` entry in
`seq-backend-eql`, so a mapped `SZC_Match` opcode silently inherited the compiled
Live `sizeof` of 8 against a 24-byte wire and dropped every packet. The fix is
**not** to re-point the payload at a Live struct name — eql gate sizes are
backend-owned, and the size now comes from the eql parser's own `PAYLOAD_LEN`.
The daemon's `--strict-gate-sizes` audit already flagged this opcode by name with
that exact instruction; it was the only one outstanding, so the de-piggyback
invariant holds everywhere else. **Run that flag as part of post-patch
verification** — grepping the log for `NOT bound` / `doesn't match` does not
surface this class, because a dropped packet is the *absence* of a symptom.

*The parser.* `seq-backend-eql`'s vendored `spawn_appearance.rs` read the pinned
Live binding's narrow `{u16 spawnId, u16 type, u32 parameter}` shape. Correcting
only the gate would have handed 24 bytes to a parser that wants 8, so both had to
move together. It now reads eql's own wire off `&[u8]` with no Live binding.

*The wiring.* The opcode was bound to `SpawnShell::updateSpawnAppearance`, which
applies Live's type semantics to eql's own type numbering — masked all along by
the drop. It now goes to `EqlDispatch::spawnAppearance`, which already
implemented the wide record for the (now unmapped) `OP_SpawnAppearance2`, and
that handler decodes through eql's Rust parser instead of casting Live's
`spawnAppearance2Struct`.

**Cross-checked against upstream's legends branch**, which solved the same
problem a third way: it declares a separate `spawnEventEQLStruct` rather than
reusing either Live name, and leaves Live's 8-byte struct untouched. Its layout is
field-for-field identical to what was measured here (`u32 spawnId / u32 type /
u32 value / u32 params[3]`), which is independent confirmation. Upstream also
labels five types — 6 = position, 13 = anon, 22 = periodic tick, 36 = LFG,
41 = timestamp. We keep the neutral `spawnAppearanceStruct` name instead of
upstream's, because eql compiles against Live's `everquest.h` here and a
per-backend size override is how this tree expresses divergence without adding a
struct; only type 6 (pose: 110 sit / 100 stand / 111 duck) is wired.

**Verified**: `--strict-gate-sizes` goes 1 flagged → 0, the last size-mismatch
warning disappears (the log is now clean of both classes), and the golden gains
5 `spawn_updated` from the 6 type-6 pose packets in the capture (3 sit, 3 stand;
the sixth is for a spawn not in the list). `check.sh` green on three consecutive
runs, `ctest` 10/10, `cargo test --workspace` green.

**Not re-mapped** — everything below p10. 132 zone opcodes remain unknown. Size-
shape matching against the prior table produces leads for ~65 of them but
collides badly (three separate ids all "match" 12-byte OP_SimpleMessage, and the
highest-volume opcode OP_ZoneEntry scores below threshold because many distinct
sizes dilute the overlap), so those leads are ordering hints only.

### 2026-07-29 (follow-up) — OP_ClientUpdate geometry re-derived, both directions

Same zone-in capture as the section above. The first pass mapped the id but left
the size gate dropping every packet because neither body's layout was known. Both
are now solved and wired; the gate passes and the position channel is live again.

**The method that worked was cross-referencing, not brute force.** Three earlier
attempts failed and are worth recording so they are not retried:

- *Against ZoneEntry spawn-time positions* — wrong by construction. This opcode
  broadcasts spawns that are **moving**, so their current position no longer
  matches the position they entered the zone at. Zero hits, correctly.
- *Range-plausibility scan* — vacuous. The capture spans three zones, so the
  global coordinate bound covers nearly the whole 19-bit field and every offset
  looks "plausible".
- *Trajectory smoothness alone* — underpowered here. Only 12 spawns have ≥4
  samples, and the sweep's top ranks saturate on constant fields (a spawn-id
  window scores a perfect 0 units/sec). It only becomes decisive once constant
  fields are excluded and same-millisecond duplicate samples are filtered out.

What settled it: **OP_MobUpdate and OP_NpcMoveUpdate were untouched by this
patch**, so they stand as map-frame ground truth for the same spawns at the same
moments. Score each candidate window against them and the answer falls out.

**S>C, 28 bytes** — `u16 spawnId@0`, `u16 0@2`, then each coord in the **low 19
bits** (signed, ×8) of its own word: **z@4, x@8, y@12**, plus a 13-bit compass
heading at bit 8 of the `@20` word. The upper 13 bits of the z and y words carry
something velocity-shaped; on the x word they read 0 in all 146 samples.

| axis | window | median err vs ground truth | runner-up |
|------|--------|---------------------------|-----------|
| z | bit 32 (byte 4) | **0.00** | 37.25 |
| x | bit 64 (byte 8) | **0.38** | 54.38 |
| y | bit 96 (byte 12) | **0.50** | 697.00 |

Two independent checks agree. Decoded `z` spans a 144-unit terrain band
(−92.12 … 51.75) where a wrong window spans the whole 19-bit field. And per-spawn
tracks imply 0.9–1.6 units/sec median with **0 of 51** steps above 100 u/s, versus
a median of 2031 u/s for the runner-up set. The longest track (32 samples) decodes
to a textbook walk: smooth 2–14 unit steps with `z` climbing steadily uphill, and
one 21 u/s leg across a 10-second gap.

**C>S, 42 bytes** — `u16 ctr@0`, `u16 spawnId@2`, floats **gameY@10, gameX@22,
gameZ@34**, and an 11-bit compass heading in the low bits at `@26`. Pinned by
range-matching against the `OP_SelfPos` breadcrumb, which reports the player's
real path: over 161 reports the field ranges match the breadcrumb's per-axis
ranges essentially exactly (@10 −1559.64…2552.56 vs −1559.64…2552.56; @22
−197.76…296.00 vs −199.73…296.00; @34 −84.30…−43.72 vs −84.68…−43.38). Every
other float in the packet spans at most ±2 (the velocities) or sits near 0.

**The X-vs-Y assignment was settled physically, not from field labels.** The
breadcrumb reports in `/loc` order, which transposes against the map frame — the
exact trap that produced a silently-swapped read in an earlier patch. Instead:
position updates are range-limited, so the player must sit inside the cloud of
spawns the server is streaming them. Under `@10 = y, @22 = x` the player is within
300 units of a visible spawn in **490 of 518** samples (median 104); transposed,
**0 of 518** (median 946).

Heading was located the same way — a running player faces where they go, so each
movement leg's bearing *is* the facing. 11 bits at `@26` scores a 6.8° median over
46 legs (S>C: 13 bits at bit 8 of `@20`, 3.8° over 26 player legs). Both are
compass values needing **no** inversion, unlike the `heading_deg` convention the
mob/npc streams use. The velocities were **not** located and deliberately read 0
rather than reusing a stale offset, which would smear the marker between updates;
candidates are the three small-range floats at `@14`, `@18` and `@30`.

**A spawnId returned to the C>S body at offset 2 — but do not adopt from it.**
The 38B bodies between 07/14 and 07/29 carried none, and its return initially
looked like a fix for the death-respawn gap (an in-zone respawn issues a new
self-id and sends no self `OP_ZoneEntry`, so name-match alone can never re-adopt).
It is not. The field carries the **phantom twin's** id, not the live copy's: it
read 15707 / 15719 across the capture while name-match adopted 15701 / 15715, and
dumping `OP_ZoneEntry` shows each pair sharing one name — eql's live-plus-phantom
double-announce, the twin's id a few higher, and also what self *stats* are keyed
to. Adopting it would pin the player to the hidden phantom and leave the live copy
loose in the spawn list. The gap therefore stays open; closing it needs the
twin → live mapping that only the ZoneEntry pair establishes.

**Useful for the next rotation**: the 07/29 C>S body is close to the **pre-07/14**
42B form, which also carried `spawnId@2` and `gameY@10` (it had `gameX@18` / `z@30`,
i.e. those two sit 4 bytes later now). The 38B bodies in between were the outlier.
Check the older 42B layout before assuming a from-scratch rearrangement.

**Verified end to end**: the size-mismatch warnings for this opcode are gone in
both directions, and the recorded golden gains exactly **+307 spawn_updated**
(2187 → 2494) — precisely the 146 S>C + 161 C>S packets in the capture — with
every other event kind unchanged. `check.sh` passes on three consecutive runs.

**Fixed alongside**: `Player::applySelfPosition` scaled the 8-bit compass as
`heading >> 5`, left over from when this field was read as 13 bits; against the
11-bit field that squeezed a full turn into the low quarter of the range. Now
`>> 3`. No user-visible effect — `protoencoder` takes the Player's
`headingDegrees` (already 11-bit correct) and never this — but the two must agree.

### 2026-07-30 — OP_ZoneEntry: spawn positions were landing tens of thousands of units off

Every eql spawn was placed wrong on the map. Surfaced while sanity-checking the
golden during the OP_ClientUpdate work: decoded `mapX` spanned ±32768 — the whole
19-bit field — and `mapZ` ±2577, neither of which is a zone.

The 07/29 rotation moved the position block **again** (07/28 had already moved
it). The coordinates are now the **first three of the six words, in packet order
Z, X, Y**. The old read skipped the first as a lead word and took the next two as
Y and Z, so every spawn's decoded Y was really its X and its decoded Z its real Y,
with X coming from a word that reads ~0 — the whole triple shifted one word right.
Note the block did NOT change size, so no size gate could catch this; it is the
same class of silent mis-decode as the 07/15 position rotation.

Worst-axis error against ground truth, over the 234 spawns in the capture that
also appear in a position stream:

| | median | p90 | within 5u | decoded mapX | decoded mapZ |
|---|---|---|---|---|---|
| before | 10506 | 25884 | 0.0% | −32768 … 32641 | −2577 … 2382 |
| after | **8.4** | 140 | **49.6%** | −1747 … 2653 | −122 … 173 |

The residual is NPC wander between a spawn's ZoneEntry (a spawn-time position) and
its later position samples — not decode error. Spawns confirmed stationary decode
near-exact.

**Two methodological points, both of which produced wrong answers first:**

- *Scope ground truth to one zone visit.* The capture crosses three (tox →
  erudnext → tox) and **EQ reuses spawn ids per zone**, so a global id→position
  map merges different mobs and fabricates matches. An unscoped search returned a
  confident 33.5% hit rate on an offset that is simply wrong.
- *Score per axis, not on a summed error.* A joint score over x+y+z is dominated
  by the wanderers and hides the correct offset entirely — the first tail scan
  reported "no anchor scores under 50" for an anchor that was in the list.

With those fixed, the located records agree unanimously on the ordering (**67/67
ZXY**) and on a tail offset of **len-103** (63/67). Block-relative offsets scatter
(227, 371, 215, 383, …), confirming the block tracks the record tail even though
the parser reaches it by a sequential walk — the tail length varies with the
title/suffix string block, so the walk is still the right mechanism.

**Where to look first next rotation**: this block has now moved on three
consecutive patches (07/14, 07/28, 07/29) while keeping its size, and the axis
ordering changed each time. Re-derive it on every patch; never assume it held.

### 2026-08-03 — OP_MobUpdate x/y were transposed; ZoneEntry was right all along

Reported as "mobs showing up outside the normal areas". Only SOME mobs: the ones
that had moved. A mob sat correctly on the map until its first OP_MobUpdate, then
snapped to a transposed position and stayed there.

**Ground truth used: the brewall map geometry**, which the daemon already loads
and ships in the golden. It is independent of every wire derivation, which is
exactly what this problem needed — the 07/28 work went wrong by using one
position stream as the reference for another. Scoring each spawn against the
nearest map-line vertex, over the 8 flip/swap variants:

| spawn set | identity | swap(x,y) |
|---|---|---|
| 170 NPCs never moved (position from OP_ZoneEntry) | **median 8u, 97% within 50u** | 803u |
| 69 NPCs that had moved | 731u | **median 8u, 100% within 50u** |
| player (OP_ClientUpdate C>S) | **24u** | 1196u |
| spawn points (OP_ZoneEntry) | **4–12u** | 1164–1351u |

So ZoneEntry and the self position are correct as they ship; one update stream
transposes. Attributed by decoding the raw payloads off `eql-patch29july.vpk` and
scoring each candidate field per axis against the spawn-struct position:

| stream | field | vs true X | vs true Y | verdict |
|---|---|---|---|---|
| OP_MobUpdate `4eda` | bits 0..18 | 1442 | **0** | is **Y** (parser called it x) |
| | bits 19..37 | 730 | 1727 | is Z (0 vs true Z) |
| | bits 45..63 | **0** | 1442 | is **X** (parser called it y) |
| OP_NpcMoveUpdate `2f15` | slot 1 | 1737 | **45** | is Y — as shipped, correct |
| | slot 2 | **64** | 1676 | is X — as shipped, correct |
| OP_ClientUpdate S>C `5380` | @8 | **28** | 2035 | is X — as shipped, correct |
| | @12 | 2035 | **47** | is Y — as shipped, correct |

Exact per-packet agreement with `bits0=Y, bits45=X`: **11 of 16** (the other 5 are
mobs that walked a few units between their spawn record and the update). With the
shipped `bits0=X, bits45=Y`: **0 of 16**.

**Root cause: we ported half a two-part convention.** Upstream's
`spawnPositionUpdateEQL` (legends `7612d72`) names its bitfields in the **wire
frame** — x at bits 0..18, y at 45..63 — and undoes the transposition **at the
call site**: `SpawnShell::updateSpawns` passes `updates->y` as x and `updates->x`
as y ("same EQ Legends x/y transposition as the spawn struct"), and
`npcMoveUpdateEQL` does the same. **Upstream decodes correctly.** The 07/28
rewrite adopted their struct labels without their call-site swap, and our parsers
name fields in the MAP frame, so the labels landed reversed.

That reversed read was then used as the reference to "correct" ZoneEntry and the
self position, transposing both to match it. The 07/29 and 07/30 rotations
re-derived those two against real ground truth and fixed them, but MobUpdate only
had its **id** re-mapped (`26d8` → `4eda`, validated on size +
spawn-id-at-offset-0), never its internal layout — leaving it the only stream
still carrying the 07/28 error.

Fix is one line each in `seq-backend-eql/src/mob_update.rs`: `y = field(0)`,
`x = field(45)`. Regression guard: `decodes_a_captured_update` pins the axes to
real wire bytes, and `each_coordinate_reads_from_its_own_field` now uses a
distinct value per axis so a transpose fails loudly (the old test used the same
shape for both and could not catch this).

**Upstream needs nothing.** An earlier revision of this entry claimed
`spawnPositionUpdateEQL` was wrong and queued a patch for Xerxes. It is not wrong
— reading the consumer rather than only `everquest.h` shows the point-of-use
swap. Retracted; nothing to submit.

**Two method notes.**

- Prefer a reference that is not itself a wire derivation. The map geometry
  settled in one pass what three rounds of stream-vs-stream comparison got
  backwards, because two streams can agree with each other and both be wrong.
- When porting a position struct from the legends branch, read `spawnshell.cpp`
  as well as `everquest.h`. A field named `x` there may be delivered as y, and
  taking the struct without the call site ports half a convention.

### 2026-08-08 — OP_LootTransaction = `0xbe5b`; both coin channels decoded

Reported as "no coin stats in the loot window". The pipeline was wired end to end
— parser, `adjustMoney`, proto, web accrual — but the opcode had been `ffff` since
`5d8fcac` (2026-07-29, "retire 26 ids that fire zero times"), so nothing reached
it. Two captures settled the id and both coin fields.

**The id trail came from upstream's git history, not their table.** They call this
channel `OP_DeathEQL` and re-mapped it every rotation before deleting it outright
in `cb0de82` (2026-08-06). The deletion is not evidence about the wire: their
handler size-checked the payload and ended in `(void)rec;` — it decoded nothing,
so the mapping was dead weight in a cleanup sweep. Mining `git log -S` for the
name gives the per-rotation trail:

| patch | their id |
|---|---|
| 07/14 | `7d1c` |
| 07/28 | `58ab` |
| 07/29 | `35f6` |
| 08/04 | `6583` |
| 08/05 | deleted |

None of those survive on the current patch, so the id was re-found from the
**subcode size signature** instead: a bidirectional opcode with client 2B/25B and
server 6B/16B/36B. Exactly one unknown matched — `0xbe5b`, `C>S 2 S>C 2,
sizes=2:1,25:1,36:1,6:1` — and its 36B payload decoded against the existing field
map with no changes. The struct survived four rotations; only the id moved.

**Subcode 7 (36B) — item confirmation, sale proceeds at @26.** Four sales in
`eqlegends-loot2` read 71 / 136 / 200 / 114 and match, in order and in full, the
four `sold it for` lines the server states separately as text.

**Subcode 5 (16B) — the corpse's coin pile, `u32 @3` (unaligned).** Previously
logged as "layout unmapped". Fires once per `OP_LootDrops`, i.e. at loot-window
open. Eight samples: 62, 87, 724, 653, 528, 921, 2881, 2923; the 2881 matches
"You receive 2 platinum, 8 gold, 8 silver and 1 copper from the corpse."

**The purse arithmetic pins both at once.** Between two `OP_MoneyUpdate` fires a
minute apart, the four sales (521c) plus the four corpse piles taken in that
window (87+724+653+528 = 1992c) total 2513c. The purse delta over the same span
was +22 gold, +28 silver, +33 copper = 2513c. Exact. A wrong offset on either
coin field, or on the `{plat@0, gold@4, silver@8, copper@12}` purse layout,
breaks the equality — so one sum verifies three field maps. End-to-end the daemon
then reports 8,524,104 copper, which is the last authoritative purse (8,517,379)
plus the three piles taken after it (921+2881+2923 = 6725).

**Method note.** The size signature was worth more than the id trail. Upstream's
last known id (`6583`) was already stale, but the subcodes are a shape the packet
cannot hide: a bidirectional channel with five distinct small sizes is close to
unique in a 143-opcode table. When an opcode rotates faster than it can be
re-derived, hunt its *shape*, not its number.

**Also found, unrelated to coin.** EQL routes a looted item to one of four
destinations, each with its own wording — sold, `tradeskill depot`,
`Dragon Hoard`, and `to create a <upgraded item>`. Only the sale form was
matched by showeq-web's session-window regexes, so depot/hoard/combine items
never appeared there at all. The window now shares the loot recorder's
`parseEqlLootMessage`, which already handled all four.

### 2026-08-09 — profile storage arrays: 10-slot run @35981 reproduced; item NAMES are not in the profile

Capture: `tests/replay/eql/eqlegends-patch-20260806.vpk` (post-08/05 rotation, 4 profile
fires, one character). Method: `--dump-payload 0x371a:` + `--dump-all-sessions`, analysed with
the new `scripts/profile_locate.py`.

**Not resolved — research notes, so the next session starts here instead of re-deriving.**

- **Landmarks still hold on a 46281-byte profile.** Carried money `@33687`, cursor `@33703`,
  stance `@33777`, invocation `@33781`, and the mirror `@36245` reads the SAME value as
  carried — so "inventory mirror" is confirmed as a mirror, not a coincidence.

- **Item names are NOT serialised in the profile.** 17 probes drawn from a live Storage UI
  (equipment, exaltation, activated, currency names) scored zero hits. Storage is
  **item-ID arrays**, as the earlier candidate note assumed — worth knowing before anyone
  greps for a name again. Consequence for ItemCache: the profile can give ids + slots, so a
  NAME still needs another source (loot events already carry name+icon+item_id).

- **The 10-empty-slot run `@35981` reproduced exactly**, on a different capture from the one
  that first suggested it. Extent is `35981..36021` = 10 × `0xffffffff`; the two 0xff bytes
  that follow belong to the next field (`@36021 = 0x9aceffff`), so do not read them as an
  11th slot.

- **A second fixed array sits at `46200..46260` = 15 slots**, all empty, near EOF (profile is
  46281). 15 is the capacity the game UI shows for *Activated Items* (`n/15`) — suggestive,
  unconfirmed. Shorter 2-slot runs at 28271, 28389, 36557, 46160.

- **Profile size was CONSTANT (46281) across all four fires**, so these arrays are fixed-size
  slot tables and do NOT drive the profile's length. That sits awkwardly with the note above
  (`@36047` name offset "sits past the inventory block, so a big inventory change shifts it")
  — either the length-driving block is something else (variable per-item records?), or that
  drift only shows across a large enough inventory delta. Worth settling early; it decides
  whether a fixed-offset read is safe at all.

- **`@35973`, immediately before the 10-slot run, changed between two fires** (a nonzero id-
  shaped value to 0) with no deliberate inventory action. Either the array is one slot longer
  than the 0xff run suggests and uses 0 as a second empty marker, or 35973 is an unrelated
  field. A paired capture settles it.

**What is still missing is a controlled change.** All four fires are zone-ins with no
inventory action, so nothing here isolates a slot. Next capture (Mode C paired diff), one
change per capture so the diff is unambiguous:

```
scripts/capture.py eqlegends-inventory-paired     # start BEFORE zoning in
# in game: zone in -> move/deposit ONE known item -> zone again
./build/showeq-daemon --replay tests/replay/eql/eqlegends-inventory-paired.vpk \
    --config-dir conf --no-listen --dump-all-sessions --dump-payload 0x371a:/tmp/pp
scripts/profile_locate.py /tmp/pp.1.bin /tmp/pp.2.bin --truth truth.json
```

`--dump-all-sessions` is not optional: recon follows the primary box by default, and a
capture that zones opens a fresh world socket, so the scoped default can dump nothing at all.

**Ruled out** (do not re-chase): item names as strings anywhere in the profile.

### 2026-08-09 — storage taxonomy from the client binary; the per-item burst lead

Source: `eqgame.exe` (PE32+ x86-64, 15.5MB, **not packed** — `strings` reads cleanly), plus
prior local RE notes. No capture needed for any of this.

**The client enumerates 47 item containers.** This is the authoritative taxonomy for the
storage work, and it names spaces the profile hunt would otherwise have to guess at:

```
Possessions  Bank  SharedBank  AltStorage  Archived  Limbo  Overflow  Trade  Corpse
Merchant  Bazaar  Mail  Krono  DragonHoard  RealEstate  TradeskillDepot  GuildDepot
GuildTribute  GuildTrophyTribute  Tribute  TrophyTribute  Inspect  World  PetItems
PersonaEquip  Deleted  Invalid  Other
  ...KeyRingItems: Activated, Augment, Equipment, Familiar, Illusion, Mount, Teleportation
  ...ViewMod* mirrors of most of the above
```

Mapping the in-game Storage tabs onto it: **Equipment → `EquipmentKeyRingItems`**,
**Activated Items → `ActivatedKeyRingItems`**. Both are KEY RINGS, not ordinary bags — which
is why they have their own capacity ("n/125", "n/15") and their own purchase button. The
`n/15` capacity matches the 15-slot empty array found at `46200..46260`, making
`ActivatedKeyRingItems` the leading candidate for that array. Supporting symbols:
`AltStorageWnd`, `AltStorageInstanceData`, `ArchiveStorageInstanceData`,
`ActivatedItemKeyRingInstanceData`, `CMD_TOGGLE_KEYRINGACTIVATED`.

Note **Exaltation is NOT a container type** — there is no `eItemContainerExaltation`, and the
strings show `" (Exaltation)"` as a NAME SUFFIX plus a `Max. Items in one stack: %d` template.
So the Exaltation tab is a view over items tagged exalted, not a distinct space. Do not go
looking for an Exaltation array.

**Per-item serialization exists, separate from the bulk profile.** Local notes on the loadout
swap record the sequence: `c2s <request> -> ack -> a burst of SERIALIZED ITEM packets -> a
~118KB full self-refresh -> WearChange broadcasts`. A per-item packet is a far better
ItemCache source than any bulk blob, since it should carry one item template per fire.

**The opcode ids in those notes are DEAD** — they predate the 07/14, 07/28, 07/29, 08/04 and
08/05 rotations (07/14 alone was a full re-map). Keep the SHAPE, re-hunt the ids: the burst is
identifiable by its position in that sequence, which is a Mode B window correlation with the
swap request as the opening landmark.

**What the binary will NOT give cheaply**: the byte offset of anything in the profile. It is
an optimised x86-64 build with no symbols; finding the serializer is far more work than one
paired capture. Use the client for SEMANTICS (which spaces exist, what a slot index means,
capacities) and the wire for LAYOUT.

### 2026-08-09 — 0x05d5 = the character's full ITEM TEMPLATE set (names + stats), request/response

Capture: `tests/replay/eql/eqlegends-inventory-paired.vpk` (deliberate paired capture: zone in,
then one item moved per zone bracket — Apothic Crown, then an activated item, then a belt).
Method: `--opcode-stats`, then `--dump-payload 0x05d5:` + `--dump-all-sessions`.

- **`0x05d5`** (S>C, 200-310KB, **currently `ffff`/unmapped**). Request/response: a **0-byte
  C>S** triggers a single large S>C reply, once per zone-in session. Payload is a flat run of
  per-item records:

  ```
  <16-char serial "iGS000e0002S4000"> NUL <name> NUL <lore name> NUL <stat block>
  ```

  Sample (Apothic Crown), name through +160:
  ```
  41706f746869632043726f776e00 41706f746869632043726f776e00 3f00000000000000
  d7040000 03000000 0100000104000000000000000b0200000000000000000000...
  ```
  → two NUL-terminated copies of the name (name + lore name), then a fixed stat block.
  Descriptions ride the same encoding elsewhere in the record ("Lightweight Bag" /
  "Holds Giant items, Capacity 12").

  **271 records** in the 2026-08-09 capture, each with a unique serial.

**Cross-validated across three captures**, and the size tracks items owned over time:
`eqlegends-patch-20260806` 209644B · `eqlegends-loot2` 271810B · `eqlegends-inventory-paired`
308865B. Same 1×C>S(0) + 1×S>C shape in all three. No other unknown opcode is anywhere near
that size, so there are zero competitors.

**This is the ItemCache source.** It carries exactly what loot events cannot: item NAME plus
the stat block. It is not `OP_ItemPacket` (still `ffff`/priority -1, zero fires) — the door
was somewhere else entirely.

**Negative results, equally load-bearing — the PlayerProfile does NOT carry storage contents:**

- Three deliberate item moves produced **no** change to the 10-slot `0xffffffff` array at
  `@35981`, which is byte-identical across all four profile fires.
- **No count anywhere in the profile decrements** when an item leaves a space. Swept every u16
  and u32 offset for a value that steps down by one and never up: zero candidates.
- So the earlier plan of locating an "inventory block" in the profile is chasing something
  that is not there. The keyring/storage tabs are fetched on demand — consistent with the
  client's `AltStorageWnd` / `eItemContainerViewMod*` symbols.

**A profile trap worth knowing before anyone diffs it again:** of 307 bytes that change between
consecutive profile fires, **219 merely oscillate** (value flips and flips back), spanning
`36211..45815` — roughly 9.6KB of ids in the 11000000 range that reorder every fire. It is an
unordered container serialized in hash order, so it is unusable at fixed offsets AND it swamps
a naive diff. Classify a changed byte as real only when `fire1 != fire3` or `fire2 != fire4`;
that cut the noise from 307 bytes to 88 here.

**Still open**: each item move grew the `0x05d5` payload by **exactly 4 bytes**, yet the set of
strings is identical across all four dumps (553 distinct in every one) — so the growth is not
a new item record. Some 4-byte-per-item list (slot assignment?) elsewhere in the payload. Also
unexplained: "Apothic Crown" and the activated item are present in the BASELINE dump, while
"Kitchen Tool Belt" appears in none — so this payload is not scoped to one container.

**Next**: map `0x05d5` (name it deliberately — `OP_ItemPacket` and `OP_ItemPlayerPacket` are
both free but their upstream semantics are unverified), pin the stat-block field offsets against
a known item's in-game tooltip, then write the parser in `showeq-decoder-rs`.

### 2026-08-09 — OP_ItemPacket (0x05d5) record layout; item_id + icon CONFIRMED against loot.db

Method: Mode C over `eqlegends-inventory-paired.vpk`, validated against an INDEPENDENT source —
`~/.scry/eql/loot.db`, which holds real `(item_name, item_id, icon)` triples recorded from loot
events, so it was produced by a completely different decode path.

**Record layout** (offsets from the record start, i.e. the 16-char serial):

```
+0     char serial[16] NUL          "iGS000e0002S4000" — per-INSTANCE id, unique per record
+17    106 B fixed block            mostly constants + a 0xffff-heavy region; not yet decoded
+123   char name[] NUL              always at +123 — fixed, because the serial is fixed-width
+...   char lore_or_desc[] NUL      lore name (usually == name); a container puts its
                                    description here ("Holds Giant items, Capacity 12")
+TAIL  field block                  TAIL = first byte after the two strings, so it is
                                    RECORD-RELATIVE and varies with name length
```

**Field block, u32 grid from TAIL** (228 named records in the sample):

| idx | off | population | reading |
|---|---|---|---|
| [0] | +0 | 228/228, 44 distinct | type / class code |
| [1] | +4 | always 0 | padding |
| **[2]** | **+8** | **228 distinct** | **`item_id` — CONFIRMED 6/6 vs loot.db** |
| [3] | +12 | 227/228, 38 distinct | — |
| [4] | +16 | 9 distinct (1, 257, 0x01000001, 0x02000101 …) | byte-packed flags |
| [5] | +20 | 205/228, values 0/4/8/18/32/64 | `slot_mask` candidate |
| [6] | +24 | 93/228, 63 distinct | weight or value candidate |
| **[7]** | **+28** | **143 distinct** | **`icon` — CONFIRMED 6/6 vs loot.db** |
| [8] | +32 | 26/228, high-half packed | AC candidate (26 ≈ the armour count) |

**Stat block: signed i32 on a 4-byte grid starting at TAIL+46.** Values run −5..75 and ARE
signed (both −1 and −5 observed), so read i32, not u32. Columns 0-13 carry data; col2 is always
zero; col17/col18 are populated on every record and look structural rather than statistical.

Getting the grid right needed care: the values first appeared in the HIGH half of u32s read on
a TAIL+0 grid, which reads as garbage (655360 rather than 10). The block is offset by 46 from
the tail, not aligned to it.

**Still open — which column is which stat.** Assignment cannot be inferred from the wire alone:
several items carry the same value in multiple columns, and the proto needs a definite
`stats[7]` (STR STA AGI DEX CHA INT WIS) plus `resists[5]` order. It needs ONE in-game tooltip
of a stat-varied item. Best candidates in this capture, most-distinct-values first:

```
Loam Encrusted Cloak   cols0-13: [3, 3, 0, 2, 0, 8, 0, 3, 1, 0, 0, 5, 0, 5]   5 distinct
Blighted Skullcap      cols0-13: [10, 0, 0, 0, 0, 0, 0, 0, 8, 7, 10, 20, 0, 5]
White Satin Gloves     cols0-13: [-5, 30, 0, 0, 0, 0, 0, 0, 0, 0, 25, 25, 25, 4]  (a NEGATIVE)
```

Do NOT assume Live's `ItemStatIndex` order carries over — the Legends record format already
differs from Live's `itemPacketStruct`, and a wrong stat order decodes silently.
