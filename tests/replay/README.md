# Local replay fixtures

This directory holds `.vpk` packet captures and their paired
`.pbstream` envelope goldens used by the tier-2 regression workflow
(see `../README.md`).

**Captures are NOT committed to git.** A `.vpk` is a recording of the
plaintext EQ session traffic the daemon saw — that includes your
character name, zone, group/guild membership, and any tells / chat
that flew across the wire during recording. The `.pbstream` golden
derived from the `.vpk` carries the same information in a different
encoding. Both files are listed in `.gitignore` so they stay on the
machine that recorded them. Keep your own pair locally and re-run
the replay+cmp check before each daemon-side change you want
confidence on.

If a fixture ever needs to be shared (e.g. to reproduce a bug across
machines), encrypt it out-of-band and **never push it to a public
remote**.

---

## Layout — one subdir per backend target

The daemon is compiled for exactly one backend (`-DSEQ_TARGET=live|test|eql`),
and a golden only matches the backend that produced it (a Live golden replayed
through an `eql` binary decodes to a different opcode table and mismatches). So
fixtures live in **per-target subdirs**:

```
tests/replay/
  live/   # Live-EQ fixtures + goldens (the historical default)
  eql/    # EverQuest Legends fixtures
  test/   # EQ Test-server fixtures
  check.sh
```

`check.sh` and `scripts/capture.py` both **auto-detect the current target** from
`build/CMakeCache.txt` (`SEQ_TARGET`, unset = `live`) and only touch that
subdir — so on an `eql` build the harness validates `eql/` (and cleanly SKIPs
when a fixture has no golden yet) instead of failing all the Live goldens. This
is what lets you push `eql`/`test` work without the "rebuild live → verify →
push → rebuild back" dance or `--no-verify`.

Overrides: `SEQ_CHECK_TARGET=<t>` forces the target; `SEQ_BUILD_DIR=<dir>` points
at a sibling build dir (e.g. `build-test/`) so you can validate one target's
goldens against another build without reconfiguring.

The files are gitignored (globs recurse into the subdirs), so moving a fixture
between targets is a plain local `mv`, not a tracked change.

## Retiring a fixture after an opcode rotation

On a target whose server rotates its opcode table (EQL does this most patches),
a capture recorded **before** the rotation can no longer be validated against
the current table — its bytes carry ids the table no longer maps. Such a fixture
does not fail because of a regression; it fails because it is unreadable, and
re-recording its golden is actively harmful: the new golden encodes "decodes
almost nothing", so the fixture goes **green while proving the table is broken
for those bytes**.

Measured on the 2026-08-04 EQL rotation: `eql-patch29july` went 3822 envelopes →
22, and `eql-zone-transition` 34472 → 88.

So retire pre-rotation fixtures rather than re-recording their goldens. Delete
them once the current patch has its own capture — a fixture that can no longer
be decoded has no regression value, and keeping dead `.vpk`s around invites
exactly the mistaken re-record above. (If you want to keep one for reference,
`<target>/retired/` works: `check.sh` globs `<target>/*.vpk` non-recursively, so
a subdir drops it from the sweep.)

A rotation therefore normally leaves exactly one active fixture — the current
patch's capture — until more are recorded.

---

## Capturing a fresh fixture

### 1. Record the raw packet stream

Easiest is `scripts/capture.py <scenario>` — it runs `setcap`, records with
`--opcode-stats`/`--list-events`, and **auto-routes the output into
`tests/replay/<target>/`** for the current build's backend. Or record manually
into the matching target subdir:

```sh
cd showeq-daemon
sudo ./build/showeq-daemon \
    --device eth0 \
    --config-dir conf \
    --record-vpk tests/replay/<target>/<scenario>.vpk   # <target> = live|eql|test
```

`<scenario>` should be a short hyphenated description of what the
recording covers, e.g. `walk-and-zone`, `con-and-kill`,
`group-form-and-disband`. Replace `eth0` with whatever interface
carries your EQ traffic. Put it under the subdir matching the backend
you captured against (`live/`, `eql/`, `test/`).

In-game, do the actions you want represented in the regression
sample — keep it short (30–60 s is plenty for most scenarios). When
done, `Ctrl-C` the daemon. The `.vpk` should be on the order of
hundreds of KB to a few MB.

### 2. Generate the deterministic envelope golden

```sh
./build/showeq-daemon \
    --replay tests/replay/<target>/<scenario>.vpk \
    --config-dir conf \
    --record-golden tests/replay/<target>/<scenario>.pbstream
```

This runs the daemon in headless playback mode against the recording.
It exits cleanly at EOF and leaves a `<scenario>.pbstream` next to
the `.vpk` in the same target subdir. The two files are now your local
golden pair.

### 3. Verify a code change hasn't broken decode

After a daemon-side change (or before merging an EQ patch-day update
to `everquest.h` / opcode XMLs), run the check script:

```sh
tests/replay/check.sh                    # all fixtures
tests/replay/check.sh walk-and-zone      # one (substring match)
```

The script auto-detects the current build's backend and iterates every
`*.vpk` in the matching `tests/replay/<target>/` subdir, regenerates a
fresh `.pbstream` to a temp dir, and `cmp`s against the matching
committed-locally golden. Output is one line per fixture (`PASS`,
`FAIL`, or `SKIP` if a `.vpk` has no paired `.pbstream`); exit code
is 0 only if every fixture passes, so it's safe to drop into a git
pre-push hook. A target with no goldens yet (e.g. `eql/`) SKIPs all its
fixtures and exits 0 rather than failing the other backend's goldens.

On `FAIL`, the script copies the divergent `.pbstream` to
`<scenario>.check.pbstream` next to the golden so you can inspect
the diff after the run. A reasonable starting point:

```sh
diff <(xxd tests/replay/<scenario>.pbstream) \
     <(xxd tests/replay/<scenario>.check.pbstream) | head -40
```

If the change was intended, regenerate the golden (step 2) and
delete the `.check.pbstream` artifact.

---

## Why fixtures stay local

The tier-2 harness was built with public/CI use in mind, but the
fixtures themselves are personal data. CI runs only the tier-1 unit
tests; tier-2 is a developer-local check you run before pushing
changes that touch the decoder, opcode wiring, encoder, or
`SessionAdapter` logic. If you regularly forget, consider a local
git pre-push hook that runs the cmp.
