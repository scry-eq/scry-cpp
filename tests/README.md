# scry-cpp regression tests

The harness has three planned tiers. Tiers 1 and 2 are runnable today;
tier 3 is gated on the Rust decoder track standing up. Tier 2 needs a
real `.vpk` fixture captured from a live session before it can actually
catch regressions.

## Running

```sh
cd scry-cpp
cmake -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

`tests/CMakeLists.txt` is added unconditionally from the top-level
`CMakeLists.txt`, so a normal build always builds + runs the unit tests.

---

## Tier 1 — encoder unit tests *(present)*

Pure-function tests against `seq::encode::*` in `protoencoder.{h,cpp}`.
Construct synthetic `Spawn` / `Item` objects, call the encoder, assert
on the resulting protobuf fields. No event loop, no QWebSocket, no
fixture files.

**Catches:**
- Schema field renames / removals (compile breaks at `out.set_xxx(...)`)
- Wrong field-mapping (e.g. `out.set_class_(sp->level())`)
- Heading-conversion regressions in `fillPos` — the trickiest math in
  the encoder
- Name-transform regressions (article move, underscore strip)

**Misses:**
- Anything upstream of the encoder (decoder, opcode wiring, struct
  layouts)
- Snapshot/tail race in `SessionAdapter`
- Per-client state machine bugs

Files: `tests/protoencoder_test.cpp`. Add new encoder tests by extending
its `private slots:`. Heavier setups (full `Player`, `MapData`,
`CategoryMgr`) need helper builders — see the inline TODO when you hit
one.

---

## Tier 2 — replay against `.vpk` goldens *(infrastructure ready, developer-local)*

The catch-net for **patch-day struct drift**. End-to-end pipeline:
record real packets once, generate a deterministic envelope golden
from them, then re-run the daemon against the same `.vpk` after every
change and `cmp` against the golden.

**Fixtures stay local — they contain plaintext EQ session data
(character, zone, chat) and are not committed.** Each developer keeps
their own `.vpk` + `.pbstream` pair under `tests/replay/`; the
directory's `.gitignore` excludes both extensions. CI runs tier-1
only; tier-2 is a pre-push check you run on the daemon's machine.
Step-by-step capture instructions live in `tests/replay/README.md`.

### What's wired

- `SessionAdapter` writes through `IEnvelopeSink` (`src/envelopesink.h`),
  so the production `WebSocketSink` and the recording `FileSink` (in
  `src/filesink.{h,cpp}`) both plug in cleanly.
- `--record-vpk FILE` on the daemon — records raw EQ packets to a
  `.vpk` file via the existing `VPacket::Record` mechanism.
- `--record-golden FILE` on the daemon — spins up an internal
  `SessionAdapter` writing to a `FileSink`. Wall-clock fields
  (`Envelope.server_ts_ms`, `BuffsUpdate.captured_ms`) are zeroed
  before serialization so the same input produces byte-identical
  output across runs. Two idle-mode runs already pass `cmp`.
- `EQPacket::playbackFinished` signal — fires when `--replay` reaches
  EOF. In `--replay --record-golden` mode, `DaemonApp` connects this
  to `QCoreApplication::quit` so the daemon exits cleanly after replay
  instead of hanging in the event loop.

### Per-developer workflow

The full capture-and-verify procedure is in
[`tests/replay/README.md`](replay/README.md). Short version:

```sh
# Once: capture from live EQ + generate the golden.
sudo ./build/scryd --device eth0 --config-dir conf \
    --record-vpk tests/replay/<scenario>.vpk
./build/scryd --replay tests/replay/<scenario>.vpk \
    --config-dir conf \
    --record-golden tests/replay/<scenario>.pbstream

# Per change: regenerate + cmp.
./build/scryd --replay tests/replay/<scenario>.vpk \
    --config-dir conf \
    --record-golden /tmp/check.pbstream
cmp tests/replay/<scenario>.pbstream /tmp/check.pbstream && echo OK
```

---

## Tier 3 — cross-decoder diff *(future, gated on Phase 4)*

When the Rust decoder track in `scry-decoder-rs` starts producing
`Envelope` output, the same `.vpk` fixtures from tier 2 are the input
to a cross-decoder diff: feed the same packets through both decoders,
compare their `seq.v1` output byte-for-byte.

No work needed in this repo until the Rust track stands up.
