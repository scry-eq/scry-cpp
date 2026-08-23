# Phase 8 loot ownership

`--loot-decoder=legacy|shadow|rust` fixes one loot owner for a session. The
default is `legacy`. Restart the session to change it.

The C++ bridge maps semantic tags 74 and 75 as `CorpseLootSnapshot` and
`LootAcquired`. It preserves capture timestamps, raw and normalized corpse
names, zone and instance context, looter, item lists, optional item and corpse
ids, optional request sequences, disposition, coin value, source kind, and the
correlation completeness flag.

Rust mode applies the semantic events to the existing host contracts. Corpse
snapshots emit `seq.v1.LootDrops` and write `window` rows. Complete acquisitions
emit `seq.v1.LootTransaction` and write `message` or `coin` rows. An incomplete
server confirmation still emits the transaction that legacy emitted when the
packet arrived. An incomplete narration writes history but does not invent a
transaction. Reset, replay-end, shutdown, and temporary-session finalization
consume the Rust flush batch before discarding the session.

There is one database writer per session. Legacy and shadow modes use
`EqlLootTracker` compatibility rows. Rust mode disables that writer and ignores
the additive `loot_rows` drain, then persists only semantic events. The SQLite
schema and natural dedup keys are unchanged, so `/loot` and existing databases
keep working.

Rollback stays the default. Live and Test have no verified semantic loot wire
mapping. EQL currently maps `OP_LootMessage` and `OP_LootDrops`, but the
2026-08-18 catalog intentionally leaves `OP_LootTransaction` at `ffff` until a
new capture identifies it. The adapter, ordering, protobuf, persistence, and
flush paths have automated coverage. A current EQL capture and Live/Test wire
research are still required before enabling Rust ownership by default.
