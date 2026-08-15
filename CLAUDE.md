# scry-cpp

Headless packet-capture and state-tracking daemon extracted from the legacy
`showeq` Qt monolith. Reference implementation for the daemon side of Scry.
See [`README.md`](README.md) for what it does and how to run it,
[`docs/architecture.md`](docs/architecture.md) for backend/build design,
wire-format quirks, and opcode-hunting technique notes.

## Stack

- C++20, Qt6 (Core, Network, Xml, WebSockets — headless, no Gui/Widgets),
  CMake 3.20+, libpcap, protobuf, zlib.
- Rust decoder: the sibling `../scry-decoder-rs` repo, linked via Corrosion
  as a **hard build dependency** — no `SEQ_USE_RUST` toggle, no C++
  fallback.
- Target server: **Live EQ**. `../legacy/ShowEQ-Legends/` is the
  correctness reference for protocol-layer code. `../EQMacEmu/` and Quarm
  opcodes/structs do **not** apply here — that's `scry-cpp-quarm`.

## Structure

- `src/` — daemon sources; packet layer + managers; per-target backends in
  `src/backend/{live,eql}/`
- `proto/` — git submodule → `scry-proto`
- `conf/` — opcode + preference TOML (flat for live, `conf/<target>/` for
  test/eql), read directly at runtime — no generated XML
- `docs/` — `architecture.md`, `patch-day.md`, plan docs
- `tests/` — tier-1 ctest suite + tier-2 replay (`tests/replay/{live,eql,test}/`,
  gitignored fixtures)
- `packaging/` — systemd unit + env example
- `tools/` — `bindcheck.py`, TOML/XML migration scripts (upstream-facing
  ones only — see `docs/architecture.md`)
- `scripts/` — `capture.py` (record a fixture), `decode_pbstream.py`
  (inspect a golden)

## Commands

- Build: see [`README.md`](README.md#build) for the full dependency list.
  `cmake -B build -DSEQ_TARGET=live -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j`
- Switch backend: `-DSEQ_TARGET=live|test|eql` needs a clean reconfigure.
- Restore capture caps after a rebuild: `cmake --build build --target setcap`
- Record a tier-2 fixture: `scripts/capture.py <name>` (add `--ip <client-ip>`
  when multiple EQ clients share the LAN)
- Tier-2 replay check: `tests/replay/check.sh` (auto-detects backend from
  `build/CMakeCache.txt`; override with `SEQ_CHECK_TARGET=<t>` /
  `SEQ_BUILD_DIR=<dir>`)
- Validate opcode tables: `tools/bindcheck.py` (wired into CI + the hook)
- Inspect a golden: `scripts/decode_pbstream.py <golden.pbstream>` (`--kind`,
  `--grep`, `--buffs`, `--spawns`, `--limit` — see the script's `--help`)
- Regenerate Rust struct bindings after an `everquest.h` change:
  `(cd ../scry-decoder-rs && python3 tools/gen_eqstructs.py all)`

## Conventions

- Target server is Live EQ — never cite `EQMacEmu/` paths or symbol names in
  committed comments here (that's `scry-cpp-quarm`'s domain).
- Re-derive an opcode/struct against the packet, not against the entry's
  newest comment — a self-contradicting comment history means the mapping
  already drifted. "modern: NN bytes ... at offset M" XML/TOML comments are
  working notes from prior hunts, not authoritative.
- Don't add a Legends (`EQL`) type or suffix to anything core owns; cite
  upstream's `<name>EQLStruct` in comments only, and pick a neutral name
  from upstream's packet *description* if a collision would occur.
- Split opcode-table changes, struct changes, and handler/behavior changes
  into separate commits — keeps each cherry-pickable into legacy showeq.

## Gotchas

- **New struct from `everquest.h`?** Also `AddStruct(<name>);` in
  `s_everquest.h` (without it, `connect2 SZC_Match` silently drops every
  packet with `OP_X (...) doesn't match: sizeof():0` in stderr) **and** a
  `StructHint` row in `opcodestats.cpp` for `--opcode-stats` candidate
  matching.
- **Wiring bind gotcha**: the `wire(op, payload, szt, ...)` payload
  TYPENAME string must exactly match the opcode's declared payload typename
  in the target's opcode TOML, or the handler silently doesn't bind
  (stderr: `dispatchFor: opcode 'OP_X' has NO payload matching dir N
  typename 'Y' szt Z — handler NOT bound`). Smoke-test any new/changed
  wiring: `./build/scryd --replay <any>.vpk --config-dir conf --no-listen 2>&1 | grep 'NOT bound'`
  (empty = all bound).
- **A payload declared `uint8_t`/`SZC_None` has no size gate** — a wrong
  opcode id fails silently, so "zero warnings" proves nothing for those;
  validate by content + fire count. Post-patch verification MUST include
  `--strict-gate-sizes` — grepping for `NOT bound`/`doesn't match` cannot
  see a gate-dropped opcode, because a dropped packet is the *absence* of a
  symptom.
- **`BoxRegistry` routing keys on the zone 5-tuple, not box identity** —
  never skip `is_merged()` boxes in the routing lookups
  (`lookupBoundZone`/`lookupByExpectedZone`/`lookupByWorld`). `merged_into`
  is UI/identity grouping only, set at `OP_PlayerProfile` *after* the new
  zone session binds — a merged-skip makes the just-bound live session
  unroutable (symptom: name+zone decode once, then no spawns / frozen
  position). Any client that zones ≥1× has 2+ boxes (fresh world socket per
  zone), so this hits single-client multi-zone decode too.
- **`Spawn::update` is `virtual`** so `Player::update` can snapshot/restore
  HP+maxHP across the base call — `Spawn::update(spawnStruct*)` writes
  percentage HP (maxHP=100), which would clobber the local PC's raw
  `OP_HPUpdate` values otherwise. Don't un-virtual-ize it.
- **`OP_ClientUpdate` dual-fire**: both `Player::playerUpdateSelf`
  (`playerSelfPosStruct`, DIR_Server|DIR_Client) and
  `SpawnShell::playerUpdate` (`playerSpawnPosStruct`, DIR_Server) fire on
  the same DIR_Server packet for the player's own spawn, each emitting a
  `spawnUpdated` proto message 0ms apart with slightly different
  coordinates (float-cast vs. bit-shift). Fixed via an early-return in
  `SpawnShell::playerUpdate` when `pupdate->spawnId == m_player->id()` — if
  wiring changes ever reintroduce a dual-fire on any opcode, watch for 0ms
  dt bursts in a client's position log.
- **Bump `magicStr`** (e.g. `plr2`→`plr3`) whenever you change the width or
  layout of any field `savePlayerState`/`restorePlayerState` serializes —
  an older `Player.dat` would deserialize the wrong byte count and corrupt
  every following field.
- **`SessionAdapter::sendSnapshot` sorts spawns by `(id, name)`** — EQ
  reuses ids across spawn types (a Door and a NPC can both have id 199), so
  an id-only sort leaves duplicate-id pairs in hash-randomized order and
  tier-2 goldens flap.
- **No proto3 `map<>` in committed schemas** — protobuf 3.21's
  `SetSerializationDeterministic` doesn't reliably sort nested map entries,
  so the on-disk pbstream flips hash ordering per run. Use parallel
  `repeated` key/value arrays sorted by key instead (see `WornSet`).
- **Proto field name `slots` collides with Qt's `slots:` access-label
  keyword** in generated `events.pb.h` — use `slot_indices` (or any
  non-keyword) and rename local C++ vars to match.
- **Proto `Spawn` changes invalidate every tier-2 golden** (every fixture
  has Snapshot/SpawnAdded events) — after `cp *.check.pbstream *.pbstream`,
  run `check.sh` once more before trusting green; a fixture can capture a
  transient divergence (wallclock-sensitive paths, e.g. `SpellShell`'s 6s
  buff-duration timer — why `buffs` is on the skip list).
- If `AutoMoc warning: includes the moc file "X.moc"...` reappears, it's a
  qmake-era `#ifndef QMAKEBUILD / #include "X.moc" / #endif` block sneaking
  back in — CMake AutoMoc generates `moc_X.cpp` from the header's
  `Q_OBJECT`, and the daemon doesn't use inline-in-cpp `Q_OBJECT`. Delete
  the block.

## Before Committing

- `tests/replay/check.sh` (tier-2 replay). The pre-push hook runs this,
  verifies `proto/` is in sync with `origin/proto`, and checks Rust
  bindings are fresh — bypass with `--no-verify` for docs-only commits from
  a non-live `build/`.
- Fixture sets are gitignored and per-machine — check what this checkout
  actually holds before calling a pass real; read the per-fixture lines
  (`SKIP <name>` vs. "no fixtures at all"), never just the summary.
- Touched a wire handler? Run the `grep 'NOT bound'` smoke test above, and
  for opcode-table changes run with `--strict-gate-sizes`.
- Touched shared core (spawnshell/packet/daemonapp, player/sessionadapter/
  protoencoder) during backend-specific work? Do a genuine Live pass too
  (`SEQ_CHECK_TARGET=live` + `SEQ_BUILD_DIR` at a live build) — that's real
  coverage, not hook appeasement.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — backend/build-target
  design, the TOML opcode/preference pipeline, Rust decoder integration,
  wire-format quirks, opcode-hunting technique notes, the SEQA remote
  capture agent link, proto sync.
- [`docs/patch-day.md`](docs/patch-day.md) — the patch-day struct/opcode
  update workflow.
- [`OPCODES_LIVE_TODO.md`](OPCODES_LIVE_TODO.md) — unresolved zone-opcode
  backlog (226 as of writing); append a dated entry per find.
- [`TEST_OPCODE.md`](TEST_OPCODE.md) — test-target opcode notes.
- `/opcode-hunt` skill (`.claude/skills/opcode-hunt/`) — the general
  opcode-hunting procedure (recon flags, disambiguation bar, TODO template).
- [`../scry-decoder-rs/CLAUDE.md`](../scry-decoder-rs/CLAUDE.md) — decoder
  workspace specifics.
