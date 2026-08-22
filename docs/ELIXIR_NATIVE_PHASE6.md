# Phase 6 player cutover

`--player-decoder=legacy|shadow|rust` selects one immutable owner for the
player event family for the lifetime of each Rust session. The default is
`legacy`; restart the session with that setting to roll back the entire family.

The family comprises decoder tags 55 through 62: player identity, player
movement, player vitals, spawn health, player death, spawn death, spawn
identity, and player appearance. The C++ adapter preserves their typed payloads
and order. Rust mode applies accepted events through neutral `Player` and
`SpawnShell` state methods. Matching legacy Live/Test and EQL writes are gated
only while Rust owns the family; decode, registry, session, flush, and apply
failures remain fail-closed rather than falling through to legacy mutation.

Shadow mode records the ordered legacy manager observations and compares them
with the Rust event observations. It also compares ordered serialized `seq.v1`
projections, not just message types. The unit suite covers every Phase-6 tag in
one ordered batch and verifies the projector's serialized output.

EQL keeps profile fields belonging to later phases, including skills, AA,
base stats, stance, invocation, and money. Its legacy self-stat tracker and
player-family profile/spawn writes stop only when Rust owns this family;
`legacy` and `shadow` retain the existing behavior. Lifecycle and entity
selectors remain independent.

The implementation has passed Live, Test, and EQL builds and their 13-test
suites, binding validation, protocol-catalog parity, and clean-diff checks.
No repository capture fixtures were available, so capture-derived replay and
soak evidence remains the cutover gap and `legacy` remains the default.
