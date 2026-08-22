# Phase 5 entity cutover

`--entity-decoder=legacy|shadow|rust` selects the entity and spatial owner for
the lifetime of each Rust session. The default is `legacy`. Keep that default
until both hosts have passed the capture replay and soak checks required by the
implementation plan.

The Rust path owns spawn add, move, remove, and rename events, doors, ground
items, corpse locations, and zone points. C++ applies the typed values to host
state. Only the selected path mutates state. The shadow path records the order
of legacy manager actions and compares the resulting `seq.v1` envelopes with
the Rust projection.

The production catalogs currently leave `OP_SpawnRename` and
`OP_CorpseLocResponse` unmapped. Those events are present in the bridge and the
Test catalog exercises them, but Live and EQL cannot enable them until captures
identify current opcode IDs. In Rust mode an unmapped packet does not fall back
to the legacy handler. Restart the session with `--entity-decoder=legacy` to
roll back the whole family.

Movement shadow comparisons may report a projection mismatch when a legacy
packet supplies velocity or animation fields. The current shared `SpawnMoved`
event carries position and heading only. This is one reason the selector stays
on `legacy` pending capture soak and an additive event-contract update.
