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

**Wire is byte-identical to the stock Live structs — no eql decoder needed.** Confirmed by
`--dump-payload` across 15 payloads in 4 post-07/14 captures:

| opcode | layout | evidence |
|--------|--------|----------|
| `OP_NewGuildInZone` | `u32 guildId, u32 serverId, cstring name` | 30B payload = 8 + 21-char name + NUL; sizes 13/23/26/30 all fit exactly |
| `OP_GuildsInZoneList` | `u32 name_len, name (no NUL), u32 count, count×{u32 guildId, u32 serverId, cstring}` | `count` matched the actual entry count in every sample (0,1,2,3,6,7); 12B = the empty case (4+4+4) |

`serverId` is a constant `13` in every capture so far. Unguilded spawns carry
`guildID 0xFFFF` (not 0) — `guildIdToName` misses the map and returns `""`, so the
sentinel is harmless today, but it does reach proto consumers as `guild_id: 65535`.

**Wiring only — the whole downstream chain already existed** (`GuildMgr` handlers, its
`guildTagUpdated` signal, and `SpawnShell::updateGuildTag`'s back-fill). Three gaps closed:
`wire_eql.cpp` binds the two opcodes under `wireGlobalSinks` (the guild map is daemon-wide
and persists to `guilds2.dat`); `SpawnShell::upsertSpawn` gained `guildServerID` (the Rust
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
`OP_GuildMOTD 0x5924` (656B, stock `guildMOTDStruct`, present in 8 of 10 fixtures) and
the popout-only web panel. **`OP_GuildMemberUpdate 0x0717` is deliberately NOT wired** —
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
