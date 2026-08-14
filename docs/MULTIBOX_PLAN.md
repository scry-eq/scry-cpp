# Multibox Sessions

Status: feat/multibox-sessions, in progress.

## Goal

A single daemon instance captures and decodes every EQ client session
visible on the wire. Web clients select which session ("box") is the
active decode target; switching is live.

v1 scope: **capture + multi-decode, single-active**. The daemon owns N
per-session decode pipelines but only emits the active one to web. v2
opens the door to parallel-active (one tab per box).

## Today's coupling

- `EQPacket` single-shot latches `m_client_addr` on the first
  world-port packet (`packet.cpp:627`). After that, the BPF filter and
  stream dispatch (`packet.cpp:681-700`) ignore other clients on the
  wire.
- One quad of `EQPacketStream`s (world C↔S, zone C↔S) per daemon.
- One instance of each state manager: `SpawnShell`, `Player`,
  `ZoneMgr`, `MessageShell`, `SpellShell`, `GroupMgr`, `CombatRouter`,
  `SpawnMonitor`. Wired by `daemonapp.cpp:181-256`.
- Workaround in CLAUDE.md:12: run N daemons, one `--ip` each.
- Prior fragment-layer multibox fix (`8687904`, `400310d`) broke
  single-client decoding; reverted. Lesson: fix at session-pinning
  layer, not in `PacketFragment::addFragment`.

## Becomes per-session ("Box")

- `EQPacketStream`s (×4)
- `SpawnShell`, `Player`, `ZoneMgr`
- `MessageShell`, `SpellShell`, `GroupMgr`, `CombatRouter`
- `SpawnMonitor` (per-zone state is per-box)

## Stays daemon-global

- `ItemCache` (cross-box, file-backed)
- `GuildMgr` (cross-server but server-uniform)
- `FilterMgr` (user-defined filters apply to whichever box is active)
- `Spells`, `EQStrings`, `Messages`/`MessageFilters`,
  `CategoryMgr`, `PrefsBroker`, `DateTimeMgr`, `DataLocationMgr`

## Box identity — two-layer

L4 headers and decoded state arrive at different times, so identity is
layered.

**Wire-routing key (immediate, L4 only):**
`(client_ip, client_world_port, server_world_port)`. This is all
`EQPacket::dispatchPacket` has on the first world packet — used to
route bytes to the right Box before any decode has happened.

**Steady-state wire key:** once a Box exists, EQ rolls its UDP socket
on each zone change. Track `(client_ip, latest_port_pair)` and update
when a new pair binds to a known box's client_ip and no other box
claims it.

**Stable identity (post-handshake):** **character name**, set from
`charProfileStruct` on `OP_PlayerProfile`. NOT `Player::id()` — the
local spawn id is server-reassigned on every zone-in
(`player.cpp:1050` re-fires "Your player's id is X" per zone), so it
rotates and is useless as a stable key. The character name persists
across zones, relogs, IP changes, and port rolls. **Promotion**: when
OP_PlayerProfile arrives on a Box with an empty name, the daemon sets
it; if another Box with that name already exists (e.g. relog → new
5-tuple) it's merged in and the old wire-routing entries adopt the
existing Box.

**External `box_id`** (the value used in proto `SetActiveBox`): a
stable **hash** of the character name when known, else a placeholder
hash of the creation-time 5-tuple. A separate `display_name` proto
field carries the human-readable label. This keeps screenshots,
pbstream goldens, and log files scrub-safe — display_name can be
zeroed for redaction without breaking box_id continuity. Once
promoted from placeholder to name-hash, the alias map keeps in-flight
UI references valid.

**v1 hole:** two same-host boxes zoning simultaneously can lose the
port-pair → box mapping until the next packet identifies them.
Acceptable; recovers within seconds.

## Naming

Proto already uses `session_id` as a *reconnect resume token*
(`Snapshot.session_id`, `Subscribe.session_id`). New concept is a
**Box**: `box_id` on the wire. Internal C++ class: `Box`.

## Stages

Each stage ships behind a stable daemon binary; goldens must still
pass between stages.

### Stage 0 — fixture

Record a real two-client `.vpk` on the LAN. Verifies both clients are
present via `--opcode-stats`. Lands in `tests/replay/multibox-2c.*`.

### Stage 1 — session detection (observation only)

- Add `BoxRegistry`: keyed by creation-time 5-tuple
  (`client_ip`, `client_world_port`, `server_world_port`), with a
  parallel `client_ip → box_id` index for steady-state zone-stream
  routing. Updated by `EQPacket::dispatchPacket` on every world-port
  packet and on each new client-IP/zone-port pairing.
- Replace single-shot `m_detectingClient` lock with continuous
  observation. The daemon still routes only the "primary" box's
  packets into the existing decode pipeline; primary is the first IP
  observed, identical to today.
- New flag `--list-boxes` (paired with `--no-listen`) dumps the
  registry every N seconds.
- Stderr log line on first sighting of each new box.
- No proto/web changes yet.

### Stage 2 — per-box packet streams

- Move the 4 `EQPacketStream`s into a `Box` struct.
- `EQPacket` owns `unordered_map<in_addr_t, Box>`; lazy-create on
  first world-port packet.
- Decode pipeline (state managers) still receives signals from one
  active box, selected at startup via `--ip` or "first seen".
- BPF filter: drop the per-IP host clause when in multibox mode and
  filter to "any EQ port"; client IP filtering moves into per-packet
  routing.
- Smoke test: replay a single-client fixture, golden unchanged.
- Smoke test: replay multibox fixture, verify both boxes register but
  only one decodes (active = first-seen).

### Stage 3 — per-box state managers

- `Box` owns `SpawnShell`, `Player`, `ZoneMgr`, `MessageShell`,
  `SpellShell`, `GroupMgr`, `CombatRouter`, `SpawnMonitor`.
- Daemon owns `unordered_map<in_addr_t, unique_ptr<Box>>`.
- `SessionAdapter` binds to the *active* box's signals; switching the
  active box reconnects the signal graph.
- `daemonapp.cpp` ctor moves the construction loop into
  `Box::Box(deps)`.
- New internal API `Box& Daemon::activeBox()`.

### Stage 4 — proto + web surface

- `proto/seq/v1/events.proto`: add `BoxMeta { string box_id; string
  client_ip; string player_name; string zone; }` and
  `BoxListUpdated { repeated BoxMeta boxes; string active_box_id; }`.
  Bump submodule SHA after.
- `proto/seq/v1/client.proto`: add `SetActiveBox { string box_id; }`.
- `SessionAdapter` emits `BoxListUpdated` on box add/remove/active
  change; handles incoming `SetActiveBox` by rebinding.
- `scry-web`: dropdown in the header; emits `SetActiveBox`. Snapshot
  flushed and refetched on switch (server re-emits as if
  `Subscribe.replay_since` were unset).

### Stage 5 — capture/replay multi-box

- `VPacket` fixture format already preserves IP/port. Verify replay
  reconstructs the box map without source changes; add a
  `tests/replay/multibox-2c.pbstream` tier-2 golden capturing both
  boxes' active-decode emissions in sequence.
- Add `--box <ip>` replay flag to pick active box for golden recording.

### Stage 6 — idle session reaping (shipped)

Every zone change opens a fresh world socket (`namepromoter.cpp:66`),
so a long multibox session accumulates one `Box` per character per zone,
each with its own `ManagerSet` + four streams. `BoxRegistry::evictStale(
now_ms, ttl_ms)` reclaims the dead ones; `DaemonApp` runs it on a periodic
`QTimer` (interval = clamp(ttl, 5s, 60s); skipped for `--replay`).

- **Activity signal:** `observe()` only stamps `last_seen_ms` on *world*
  traffic, so a box settled into a zone would look idle. `dispatchPacket`'s
  zone-matched branch now stamps `last_seen_ms` too — without it the sweep
  would reap an actively-playing box.
- **Group-aware policy** (a character = a non-merged parent + its merged
  re-handshake aliases): superseded stale aliases are reaped individually;
  a whole character group is reaped only once its *live* box
  (`currentBoxFor`) is itself stale (logged off). Never reaped: the primary
  box, the active character's live box, every still-active character's live
  box. A non-merged parent only goes when its whole group goes, so a
  surviving alias never orphans its identity anchor.
- **Teardown:** `boxAboutToBeRemoved(Box*)` fires before the `Box` is
  freed. `EQPacket` parents each non-primary box's streams + observers
  under a per-box `QObject` root, `DaemonApp` does the same for its
  `ManagerSet`; the slots `deleteLater()` the root for order-safe subtree
  teardown. Removal propagates to the web picker for free —
  `SessionAdapter::sendBoxList()` rebuilds from the registry on `changed()`.
- **Flag:** `--box-idle-ttl SECONDS` (default 600; 0 disables).
- Tier-1: `boxregistry_test` eviction cases (superseded-alias reap,
  logged-off whole-group reap, primary/active/fresh protection, no-orphan,
  disabled/no-op).

## Out of scope (v1)

- Multi-active (parallel decode → parallel emits). v2.
- Cross-box correlation (group of N boxes that share a leader).
- Per-box prefs / filters.
- Per-box itemcache namespacing.
