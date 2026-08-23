# Recording application-packet traces

`scryd --record-app-traces DIR` records the ordered input to each logical Rust
`Session`. The packet hook runs after SOE reassembly and before decoded-packet
observers, muted-box gating, and legacy handlers. Unmapped, malformed, and
muted packets therefore appear once in the same order seen by
`Session::decode`.

The daemon writes opaque names such as
`scry-live-20260823-120000000-1234-session-000001-part-0001.trace.json`.
It does not put a character name in the filename. Each file snapshots the
compiled backend and the embedded registry's semantic catalog hash. A session
starts a new part after a semantic session reset or zone transition. Replay
end, eviction, and normal shutdown finalize the current part.

Files remain temporary until `QSaveFile::commit` atomically renames a complete
strict version 1 JSON document into place. A write or commit failure stops a
trace-enabled daemon instead of continuing with a partial corpus.

These files are capture-derived and set `synthetic` to `false`. Payloads can
contain character names, chat, guild data, and other private text. Keep them
outside the repository until they have been reviewed and scrubbed according to
the decoder's `docs/application-packet-traces.md` instructions.

Validate, replay, and check a reviewed trace with the pinned decoder:

```sh
cargo run --manifest-path scry-decoder-rs/Cargo.toml -p seq-trace -- \
  validate trace.json
cargo run --manifest-path scry-decoder-rs/Cargo.toml -p seq-trace -- \
  replay trace.json -o trace.golden.json
cargo run --manifest-path scry-decoder-rs/Cargo.toml -p seq-trace -- \
  check trace.json trace.golden.json
```

Use the matching `--no-default-features --features backend-test` or
`backend-eql` arguments for those targets. The repository's automated trace
fixture is generated data and sets `synthetic` to `true`. It proves format and
replay interoperability only. It is not capture parity evidence.
