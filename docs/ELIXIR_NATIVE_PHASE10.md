# Phase 10 communication ownership

`--communication-decoder=legacy|shadow|rust` fixes one communication owner for
each decoder session. The default remains `legacy`; changing the selector
requires a session restart.

The family contains semantic chat, UCS chat, group rosters, guild rosters,
guild member deltas, guild MOTD and rank names, and dynamic-zone state. The C++
bridge retains the lower-level wire events for diagnostics, but ownership and
host mutation are gated only on the semantic variants. When lifecycle and
communication are both Rust-owned, their events are applied in the shared Rust
order so roster and dynamic-zone resets precede the following session or zone
transition.

Rust chat uses the existing EQ string resolver and item-link cleanup before it
reaches `Messages` and `seq.v1.Chat`. UTF-8 text, channel number, raw chat
colour, named UCS channel, target, and the UCS spam marker are preserved. UCS
payloads enter the same stateful decoder session as world and zone traffic,
without passing through SEQA framing. Group state retains five stable peer
slots and levels for members that are not currently spawned. Guild partial
rosters and status packets are merged by Rust before the complete current state
is applied to `GuildShell`. Dynamic-zone info and switch packets likewise reach
`ZoneMgr` only after Rust correlation.

The existing `seq.v1` schema has projections for chat, group, guild roster,
guild MOTD, and guild rank names. It has no dynamic-zone envelope, so dynamic
zone is compared by ordered semantic observation and host state only. Existing
chat and loot persistence remain unchanged; a loot narration is still owned
independently by the loot selector.

Rollback remains the default because current capture goldens do not cover all
Live, Test, and EQL communication mappings. UCS attribution currently uses the
active or most recently seen box for the client IP. That preserves state across
a UCS socket rotation while the game box remains alive, but there is no UCS
handshake or port-to-character identity map; a newly created game box may not
inherit the prior UCS mask and channel-name correlation. Reconnect captures,
socket-rotation captures, encoding cases, and partial/full roster goldens are
still required before Rust becomes the default.
