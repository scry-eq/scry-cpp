# Architecture

How scry-cpp's subsystems work and why, plus the wire-format knowledge that
doesn't fit in a code comment. For commands and behavior-changing gotchas,
see [`../CLAUDE.md`](../CLAUDE.md). For the patch-day update workflow, see
[`patch-day.md`](patch-day.md). For opcode-hunting procedure, use the
`/opcode-hunt` skill — this doc only covers the scry-cpp-specific technique
notes that supplement it.

## Backend targets

`-DSEQ_TARGET=live|test|eql` (default `live`) selects the backend at
configure time; switching target needs a clean reconfigure (`build.sh
--clean` or a fresh `-B` dir — the include-swap + Corrosion feature
selection can't reconfigure in place).

- **`live` and `test` share `src/backend/live/`** (same `everquest.h`, same
  `wire_live.cpp`). **`eql`** (EverQuest Legends) is the one distinct
  backend, in `src/backend/eql/`.
- **Opcodes per target**: Live stays FLAT at `conf/opcodes.toml`
  (backwards-compatible with legacy showeq's shared conf tree); Test/EQL
  nest under `conf/<target>/`. `--config-dir` is always `conf` — the
  runtime picks the subdirectory via the compiled `SEQ_OPCODE_SUBDIR` (`.`
  for live).
- **Data dirs per target**: Live is flat at `~/.scry`; Test/EQL nest at
  `~/.scry/<target>` (compiled `SEQ_DATA_NAMESPACE`). Renamed from
  `~/.showeq` with the Scry rename — `SEQ_LEGACY_DATA_NAMESPACE` holds the
  old root and `DataLocationMgr::findExistingFile` falls back to it on READ
  ONLY (never write), warning once per file. The Elixir `scry` daemon uses
  the same namespace scheme, so both share this root; only `loot.db` is a
  shared writer, so don't run both against the same target at once.
- **No `#ifdef` in core.** Per-target structs come from the compiled
  `SEQ_STRUCT_DIR` include path: `test` → `src/backend/test/everquest.h`;
  **both `live` AND `eql` → `src/backend/live/everquest.h`** (eql currently
  reuses the shared/live wire structs where the wire is byte-identical —
  see the [decoder](#the-decoder-is-rust-only) section for how it diverges
  where it doesn't). The size registry is the shared `src/s_everquest.h`
  (`AddStruct` rows), included by `packetinfo.cpp`.
- **Opcode dispatch is typed, not Qt-SLOT-based.** Handlers register with
  `EQPacketStream::on(op, payload, szt, PacketHandler)`
  (`PacketHandler = std::function<void(const uint8_t*, size_t, uint8_t)>`),
  built from a manager method via `seqBind(obj, &Class::method)` in the
  per-backend `wire_{live,eql}.cpp`. `EQPacketDispatch` is a plain
  per-payload `std::vector<PacketHandler>` fan-out (no moc); install order
  is fire order (golden-sensitive). There is no `connect2`/string-`SLOT`
  path.
- **EQL handlers live in a backend-only plain (non-QObject) `EqlDispatch`**
  (`src/backend/eql/eqldispatch.{h,cpp}`), owned by a `shared_ptr` the wired
  closures capture. It casts the Legends structs and drives core managers
  via target-NEUTRAL public primitives (`Player::setIdentity`/
  `applySelfPosition`, `ZoneMgr::setZoneByName`,
  `SpawnShell::upsertSpawn`/`moveSpawn`) — those compile on every target and
  go unused on live/test. When adding an EQL handler: add its neutral
  primitive to the core manager, add a method to `EqlDispatch`, wire it in
  `wire_eql.cpp` via `seqBind(eql, &EqlDispatch::method)` — never a Legends
  type in core.

## Opcode + preference tables are TOML, read directly at runtime

`conf/<target>/opcodes.toml` (flat `conf/opcodes.toml` for live) **is** the
opcode table. `EQPacketOPCodeDB::load()` parses it with vendored toml++
(`third_party/tomlplusplus/toml.hpp`, single header, MIT), selecting the
`[[zone]]` or `[[world]]` array by the section the DB was constructed with —
one file, two DBs, two id namespaces.

**Retired 2026-08-08**: the old TOML→`tools/toml_to_xml.py`→
`{zone,world}opcodes.xml`→`QXmlStreamReader` pipeline gave the same data two
on-disk representations and two silent desync modes — the `seq-opcode-xml`
CMake target could serve a *stale* xml (a mapped world table once read as
all-`ffff`, 2026-07-25), and a TOML syntax error left the *old* xml in place
so `check.sh` passed against data nobody had edited. Both failure modes are
now unrepresentable — the daemon reads no XML of its own.

**Preferences moved to TOML the same way (2026-08-14)**:
`conf/seqdef.toml` (defaults, committed) + `~/.scry/<ns>/daemon/scryd.toml`
(user, written by the daemon), read by `src/tomlpreferences.{h,cpp}`. A user
file still in XML is migrated once on load; `seqdef.xml` is gone.
Regenerate defaults with `tools/prefs_xml_to_toml.py` (note only
string/int/bool use `value=`, colors use `name=` or red/green/blue, fonts
`family=`, keys `sequence=`). The store exposes five value types
(string/int/uint64/bool/color) because that's all the daemon calls — the
widget-era QPoint/QRect/QSize/QStringList/QVariant accessors had no callers
and were dropped.

`tools/xml_to_toml.py` still reads XML, but that's *upstream's* (legacy
showeq / ShowEQ-Legends), not ours. The upstream opcode-matrix generator
moved out to the meta repo's `tools/upstream_matrix.py` — it takes either
backend's vendored `conf/eql/opcodes.toml` as input, not just this repo's.

## The decoder is Rust-only

The daemon links the pinned `scry-decoder-rs/` submodule's `seq-bridge` (a cxx
staticlib) via Corrosion as a **hard build dependency**. There is no
`SEQ_USE_RUST` toggle and no C++ fallback path. Every wire handler decodes through
`seq::rust::decode_*`; the old C++ parsers (`fillSpawnStruct` etc.) are
gone. Backend is picked by `-DSEQ_TARGET=live|test|eql` → the decoder-rs
`backend-*` Cargo feature, 1:1. eql's `EqlDispatch` calls the same
`decode_*` names.

CMake uses the submodule revision recorded by this repository. A developer can
point a build at another checkout with
`-DSEQ_DECODER_RS_DIR=/absolute/path/to/scry-decoder-rs`. CI and release builds
do not use that override.

Phase-2 shadow decoding lives in `rustsession.*`. `EQPacket` owns one protocol
registry built from the decoder's embedded catalogs. It creates one stateful
Rust session for each `Box`. A temporary unattributed session covers captures
that begin mid-zone before world traffic creates the first box.

`EQPacketStream` calls the shadow hook after SOE reassembly and before its
decoded-packet observers or legacy handlers. The hook sends the stream, numeric
opcode, direction, payload, and capture timestamp to Rust. The adapter switches
only on `SessionEventKind` and moves the indexed typed payload into an exhaustive
49-alternative `std::variant`. It does no opcode lookup, backend selection, or
correlation. Each session keeps the latest 256 ordered packet and flush records,
with monotonic record and dropped-record counts for diagnostics.

Legacy opcode handlers remain the only path that changes host state. Rust
events, self-stat correlation, and loot rows stay in the shadow journal. The
host flushes sessions on shutdown, box eviction, replay completion, and the
existing `ZoneMgr` transition signals, including EQL's destination-unknown
transition marker.

To add or change an opcode: edit the parser in `scry-decoder-rs` (see its
own `CLAUDE.md`/`docs/architecture.md`), expose it via `seq-bridge`, then
call `seq::rust::decode_X` in the handler. A handful of things stay partly
C++ by design — the *app logic* only, never the parse: `SpellShell::buff`
(spell-DB duration lookup), `GroupMgr::groupMemberList` (roster diff),
`SpawnShell::shroudSpawn` (header framing + self-profile, reuses
`decode_spawn`).

**New-opcode decode, fixed vs. variable layout:**
- For a fixed-layout struct, add it to
  `scry-decoder-rs/tools/gen_eqstructs.py`'s ALLOWLIST. Run the generator for
  both headers:
  `python3 scry-decoder-rs/tools/gen_eqstructs.py live src/backend/live/everquest.h`
  and
  `python3 scry-decoder-rs/tools/gen_eqstructs.py test src/backend/test/everquest.h`.
  `seq-decode` compiles for Live and Test, so regenerate both binding files.
  Decode through the `crate::eqstructs::<Struct>` binding in seq-decode. Keep
  the real struct name in the TOML payload and `wire()`, not `uint8_t`.
- A VARIABLE-layout opcode (LPText / flexible array) → `uint8_t`/`none`
  payload + a hand-rolled `Cursor` walk in `seq-decode` (see
  `seq-decode/src/guild_roster.rs`).

## Wire-format quirks worth not re-deriving

- **Live heading**: the wire field is declared `:12` but carries only an
  11-bit effective range (0–2047 = full turn), so
  `degrees = 360 - ((heading * 360) >> 11)`. eql (`Player::applySelfPosition`)
  is genuinely different — Legends packs an actual 13-bit facing (8192/circle),
  hence `>> 13` there. **Position-struct bitfield order** (`playerSelfPosStruct`,
  `playerSpawnPosStruct`, the `spawnStruct` position union) shifts on nearly
  every patch — re-derive from upstream or capture data each time, don't
  memorize. A reorder keeps the struct's *size* unchanged, so
  `sizechecktype=match` will NOT catch it (the 07/15 rotation silently
  decoded x's value into z until it was caught 2026-07-28).
- **Profile fields `MANA` (offset 950) and `curHp` (offset 954) are STALE
  snapshots**, not authoritative current values — don't seed `m_mana`/
  `m_curHP` from them; wait for `OP_ManaChange`/`OP_HPUpdate`. The profile
  stat block at 956–983 is BASE STR/STA/CHA/DEX/INT/AGI/WIS (the race+class
  roll), not the displayed buffed totals.
- **Modern-Live "stats window" fields** (AC, attack, haste, resists, combat
  regens, accuracy/avoidance, secondary stats, combat skills) are computed
  CLIENT-SIDE on Live and never appear on the wire — don't hunt for them in
  profile or per-tick opcodes.
- **`spawnStruct.equipment[0-6].itemId`** = armor material visual codes
  (0–23, see `src/util.cpp::print_material()`); `equipment[7-8].itemId` =
  weapon model visual codes (0–255, see `src/weapons.h`). Neither is an EQ
  item-database id — they're 3D model selectors. `fillSpawnStruct`'s
  equipment branch: humanoid races (NPC==0 || race<=12 || race∈{128,130,330,522})
  skip 36 color bytes then read all 9 slots; other NPCs skip 20 bytes and
  read only slots 7+8.
- **`OP_Stamina`** is hunger/thirst (`staminaStruct{food, water}`, max 127);
  the run/jump endurance bar EQ paints yellow is `OP_EndUpdate`
  (`endUpdateStruct{spawn_id, cur, max}`) — don't conflate when chasing
  "stamina" issues.
- **`OP_InspectAnswer`** = `0x57f1`, 1956 bytes, `inspectDataStruct`
  (`everquest.h:2513`): 4-byte pad, `spawnId`, `itemNames[23][64]`,
  `icons[23]`, `mytext[200]`; forwarded as proto `InspectAnswer`.
  `OP_InspectRequest` = `0x14b6`, 8 bytes C>S
  (`{u32 target_spawn_id, u32 self_spawn_id}`). Inspect is passive (no
  permission needed since a patch removed it).
- **A payload declared `uint8_t`/`SZC_None` has no size gate**, so a wrong
  opcode id fails silently — "zero warnings" proves nothing for those;
  validate by content + fire count (`OP_ZoneChange` sat mis-mapped across
  two patches this way).

## Opcode-hunting technique notes

The `/opcode-hunt` skill covers the general recon-flag procedure
(`--opcode-stats`/`--list-events`/`--dump-payload`, disambiguation bar, how
to record a find). These notes are scry-cpp-specific techniques that
supplement it:

- **Variable-size struct discovery**: dump-payload across captures, pick
  the smallest variant + a larger variant, locate a known anchor (spell id,
  spawn id) in both — the offset delta reveals an embedded sub-block's size
  and position. Established cracking `OP_Buff` `0x18b4`: spell id `0x019c`
  at offset 15 (30b form) vs. offset 36 (51b form) = a 21-byte
  caster-block insertion.
- **Cracking a position/movement opcode**: `--dump-payload` dumps BOTH
  directions into one counter — split S>C vs. C>S by payload byte-size.
  Correlate the unknown stream's candidate bitfields against a known-good
  stream (`OP_MobUpdate` `0x67e0`) per-spawn *median*, but only
  **stationary** spawns match (sparse streams aren't time-aligned). Clinch
  it with trajectory self-consistency: a real walk decodes to a smooth
  ~8-units/tick path; a wrong bit-extraction gives thousand-unit jumps. A
  spawn seen in both streams should decode to an identical centroid.
  (Established cracking the 28B S>C `OP_ClientUpdate`, 2026-07-10.)
- **Position cross-referencing pitfall**: EQ reuses spawn ids per zone, so
  on a multi-zone capture scope ground truth to one zone visit (match
  within ~120s of the record's own timestamp) or matches get fabricated —
  and score candidate offsets PER AXIS. A summed x+y+z error is dominated
  by wandering NPCs and hides the correct offset entirely.
- **EQL opcode hunting: try Live's existing wire format FIRST** —
  byte-identical so far for `OP_NpcMoveUpdate` (BitStream),
  `OP_MobUpdate` (= `spawnPositionUpdate`), `OP_TargetMouse`
  (`clientTargetStruct`), `OP_EnterWorld` (72B, name@0). A coordinate that
  "wraps" at a power of two, or per-axis divisors that look arbitrary
  (÷8 / ÷64 / unscaled), means a TRUNCATED read of Live's packed 19-bit ×8
  bitfields — not a wire quirk needing unwrap heuristics. Decisive cheap
  test: **sign-fill** — the bits above a signed bitfield must equal its
  sign bit across every captured packet (0 violations over 1665 packets
  settled `OP_MobUpdate`). Float/int32 scans CANNOT detect
  byte-straddling bitfields, so "no wider field found" from such a scan
  proves nothing.
- **Session-scoping pitfall**: recon flags follow the *primary* box by
  default — whichever world session is seen FIRST — so on a capture where
  the client zones (a fresh world socket per zone-in), that session may
  never zone, and `--opcode-stats` reports `zone opcodes (0 distinct)`
  while decode is actually fine. `--dump-all-sessions` forwards every box;
  `--only-session <charname|N|first>` restricts to one. Confirmed
  2026-07-28: a 3-world-session Live capture read 0 zone opcodes scoped,
  191 with `--dump-all-sessions`. Before diagnosing a binding bug from a
  zero zone tally, re-run with `--dump-all-sessions`; the real
  binding-failure signature is 0 zone opcodes *even then* (that means
  `OP_ZoneServerInfo` is unmapped).
- **Cross-client disambiguation**: run two capture instances simultaneously
  (one `--ip` per client, via `scripts/capture.py --ip <EQ_client_ip>`).
  Opcodes appearing only on the acting client's side are personal server
  responses; those on all nearby clients are zone broadcasts. Faster than
  `--list-events` for the personal-vs-broadcast question.
- **A mid-session capture** (missing the SOE session handshake) replays to
  ZERO decoded packets, and its capture-time opcodestats/events artifacts
  are empty too — check them before relying on a fixture. Start recording
  BEFORE the client logs in / zones in (each zone-in opens a fresh world
  socket, so "from zone-in" suffices per session). Partial salvage without
  the daemon is possible: scan the raw `.vpk` for zlib streams (`78 9c`) +
  uncompressed opcode-signature bytes and hand-decode messages (recovered
  216 MobUpdates from an otherwise-dead multibox capture this way).
- **A capture that zone-changes BEFORE the event you care about** decodes
  every app opcode (opcodestats non-zero) but never rebinds
  player-id/box tracking for the later session — `m_player->id()` sticks at
  the earlier session's self-id, so player-id-dependent behavior
  (self-death/corpse, self-pos re-adoption) can't be replay-verified with
  it. Verify those only with a capture that starts before zoning INTO the
  target zone.

## Maps are resolved and rendered server-side

`daemonapp.cpp::loadZoneMap` locates the `.map`/`.txt` (+ numbered `_1`/`_2`
layers) and streams geometry via `seq::encode::fillMapGeometry` in
`ZoneChanged`/`Snapshot`. Clients render that geometry — they never load a
map file by zone name — so fix map / zone-name issues in `loadZoneMap`, not
a client. EQL raid instances arrive named `<base>_solo`/`_multi`/
`_eqlraidgroup` with no per-instance map, so `loadZoneMap` falls back to the
base zone (strip at first `_`) after trying the exact name.

## Remote capture agent (SEQA)

**Remote capture source `--agent host:port`** (default port 9099):
`RemoteCaptureThread` (`src/remotecapture.{h,cpp}`, a
`PacketCaptureProviderThread` sibling of `PacketCaptureThread`) sources
frames from a `../scry-agent` (or scry's `Scry.Agent` — same wire format)
over TCP instead of local libpcap. It dials the agent, sends a SEQA
`ClientHello` naming the BPF filter (the open-UDP auto-detect string +
`--ip` host scope; port-narrowing `setFilter` calls are IGNORED — the agent
stays a dumb forwarder, the daemon narrows sessions downstream), reads the
agent's `Hello`, then pushes raw Ethernet frames into the same
`packetCache` the decode loop drains (decode path identical to local pcap;
expects `DLT_EN10MB`, warns otherwise). Runs as `PLAYBACK_OFF` (live) with
backoff auto-reconnect; needs NO `cap_net_raw` locally (the agent
captures); mutually exclusive with `--device`/`--replay`.

**The SEQA wire format has three implementations** —
`../scry-agent/src/proto.rs` (normative), scry's `Scry.Agent.Protocol`, and
`remotecapture.cpp` here — so a protocol change is a three-repo change.
**SEQA v2** (2026-08-04) wraps every message in
`magic "SQ" | version u8 | type u8 | len u32 | payload` (types `1=Frame`
ts_micros+origlen+data, `2=ClientHello` filter, `3=Hello`
link_type+snaplen+filter, all LE); `readMsg` reads one envelope and
`readHello`/`pumpFrames` skip types they don't know rather than desyncing,
since v1's frames carried no magic at all. Frame caplen is derived
(`payload.size() - 12`), never a field that could contradict the bytes
present. Types `4=AgentHello` and `5=SessionInfo` (2026-08-16) are the
hosted-service handshake (agent dials a service with a token; string
fields are each len-u16-LE + bytes) — this daemon is a plain LAN consumer
and only ever skips them; the constants in `remotecapture.cpp` exist for
grep parity with the other two implementations.

Cross-repo GOTCHA: the agent MUST close its socket gracefully — a plain
close with the daemon's unread `ClientHello` still buffered sends RST,
silently discarding frames still queued for the (slow, decoding) daemon
(fixed in scry-agent's `graceful_close`; a fast consumer masks it).

**Verifying the path with no live traffic**: rebuild a `.pcap` from any
`.vpk` (strip the 40-byte VPacket header — `size u32, pad, time i64,
version i64 (==40101), ms i64, seq i64`, then a raw Ethernet frame) and
compare `--agent` vs. `--replay-pcap` goldens. They are **not**
byte-identical on a large fixture, and that's expected: `--replay-pcap` is
time-paced (16.6s for a 20663-frame fixture) while `--agent` is a live
source that ingests the same frames in ~40ms, so the wallclock-driven 6s
buff-duration timer fires a different number of times. Compare with
`scripts/decode_pbstream.py` tallies instead — every packet-derived kind
matches exactly, and only `buffs`/`spawn_effects` differ (both already on
`check.sh`'s skip list for this reason). Each mode is internally
deterministic (run-to-run within a mode IS reproducible); the transport
itself is proven exact by scry-agent's byte-identical `.pcap` round-trip.

## Proto is a shared, submoduled schema

`proto/` is a git submodule of `scry-proto` — the same schema scry-web and
the Elixir `scry` build against (scry has no submodule of its own; it runs
`protoc` against `../scry-cpp/proto`). Its SHA drifting has cross-repo blast
radius: BEHIND canonical `origin/proto` means the daemon *and* scry build
against a stale schema; a pointer AHEAD/unpushed leaves a dangling
submodule that breaks web+scry clones/CI — push scry-proto FIRST. The
pre-push hook guards both directions (it fetches `origin/proto` first,
since a stale cached ref can otherwise mislead the comparison).

Editing proto is a two-clone dance: edit `scry-proto/seq/v1/*.proto` AND
this repo's `proto/` submodule clone, then bump the SHA (`cd proto && git
fetch <scry-proto-checkout> main && git reset --hard FETCH_HEAD`, then
`git add proto` here). After any new proto message, regenerate every consumer — scry
(`protoc --elixir_out=lib/proto`) and web (`bun run gen`) — or they
silently lag (scry surfaces it as a compile-time
`Seq.V1.<Msg>.__struct__/1 undefined`; web as an unknown-field no-op).

## Provenance

The daemon's packet + state-manager layers were extracted from `showeq-c`
(commit `afc268b`), **not** legacy showeq directly — they silently diverge.
When protocol-layer code (`packetformat`/`packetstream`) misbehaves, diff it
against `legacy/ShowEQ-Legends/src/` — that's the correctness reference
(e.g. the compressed-packet flag test here was once a bitmask `& 0x5a`;
legacy's exact `== 0x5a` was right — `0x5a` is a marker value, not a
bitfield).
