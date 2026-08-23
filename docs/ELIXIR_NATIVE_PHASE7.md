# Phase 7 items and progression

`--progression-decoder=legacy|shadow|rust` selects one immutable owner for
inventory, equipment, money, skills, experience, levels, and alternate
advancement. The default is `legacy`. Restart the session to change it.

The C++ adapter maps Rust event kinds 63 through 73 without reading packet
bytes. Item instance serials, exact locations, previous locations, and every
optional item value remain distinct in the adapter. An absent icon, count,
weight, flag word, or corruption value does not become a value of zero before
projection.

Inventory snapshots replace the current item view. Equipment snapshots clear
all worn slots before installing the new set. Incremental moves apply the old
slot removal before the destination set. Live does not send a complete
inventory snapshot, so the JSON item cache loaded at startup seeds its item
view and Rust updates it incrementally. EQL bulk inventory snapshots replace
that view.

Rust-owned player updates mutate typed `Player` state, then write `Player.dat`
once per decoded batch. Item cache persistence remains under `ItemCache`; no
second progression path writes it. EQL AA title string ids resolve through the
existing `DbStrings::nameById` table before the next `PlayerStats` projection.

Shadow mode compares semantic event order and serialized `seq.v1` envelopes.
The legacy path remains available because post-patch Live and EQL capture
goldens and the soak period are still missing. Do not remove it or change the
default until those gates pass.
