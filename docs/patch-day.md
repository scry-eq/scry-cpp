# Patch-day runbook

EverQuest ships periodic patches that change network opcodes and/or struct
layouts. When this happens, both `showeq` (the legacy Qt app — the
regression oracle) and `scry-cpp` (this repo) must be updated in
lockstep until `showeq` is eventually retired.

This document is the checklist.

## Files that change on patch day

Three source categories, **in both `showeq/` and `scry-cpp/`**:

| File | What changes | Notes |
|---|---|---|
| `src/everquest.h` | Struct layouts | The ~92K bible. Most patch-day edits land here. |
| `conf/worldopcodes.xml` | Opcode id ↔ handler map for the world server stream | |
| `conf/zoneopcodes.xml` | Same, for the zone server stream | Where most opcode drift happens. |

Anything else touched — `src/s_everquest.h`, dispatchers, new handlers — is a
bigger change and should go through a normal PR rather than the patch-day
fast path.

## Procedure

1. **Confirm both repos are clean** (`git status` in each) and on `main` /
   `master`.

2. **Capture a fresh `.vpk`.** Log into the patched client; walk around a
   zone; have combat happen; zone; camp out. Save as
   `scry-cpp/tests/replay/YYYY-MM-DD-<zone>.vpk`.

   ```sh
   sudo build/scryd --device eth0 \
     --record-golden tests/replay/$(date +%F)-gfay.vpk
   ```

   (If the daemon can't decode yet on the new struct layout, do this step
   using `showeq` with its record-capture option — the `.vpk` format is
   compatible.)

3. **Update `showeq` first** (upstream of us). Edit the three files above
   to match the new opcodes/structs. This is the patch-day community
   tradition; everyone coordinates there. If someone else already landed it
   upstream, pull.

4. **Mirror the edits into `scry-cpp`.** The three files are
   byte-for-byte identical between the two repos. Simplest procedure:

   ```sh
   cd scry-cpp
   cp ../showeq/src/everquest.h        src/everquest.h
   cp ../showeq/conf/worldopcodes.xml  conf/worldopcodes.xml
   cp ../showeq/conf/zoneopcodes.xml   conf/zoneopcodes.xml
   ```

   (Once we stabilize, switch to `git subtree` or a vendor script. Until
   then, a verbatim copy is the lowest-risk path.)

5. **Rebuild both.**

   ```sh
   cd showeq && make -j
   cd ../scry-cpp && cmake --build build -j
   ```

   Compile errors here are almost always a missed struct change.

6. **Run both side-by-side.** Log in. Walk. Compare:
   - `showeq` spawn list vs. web client spawn list
   - Spawn positions on both maps
   - Zone transitions happen cleanly on both
   - No "unknown opcode" warnings in the daemon log

7. **Regenerate golden `.pbstream`**. If the daemon's protobuf output
   diverged intentionally (new fields added), update the golden:

   ```sh
   cd scry-cpp/build
   ./replay_test --record tests/replay/YYYY-MM-DD-<zone>.vpk \
     > ../tests/replay/YYYY-MM-DD-<zone>.pbstream
   ```

8. **Commit both repos.** Matching short messages make the pair easy to find
   in `git log`, e.g.:

   ```
   showeq:       Add opcodes for YYYY-MM-DD patch. Thanks <name>.
   scry-cpp:  Sync everquest.h + opcode XML with YYYY-MM-DD patch.
   ```

9. **Tag a release** once parity is confirmed: `v6.4.N` in `showeq`,
   `v0.N` in `scry-cpp`. Tag both on the same day.

## Things that break this workflow

- **Struct size changes** flagged by the `modulus` size-check in the opcode
  XML. If the daemon logs "opcode 0xABCD: size mismatch", the XML's
  expected size formula is out of date.
- **New opcodes** appear as "unknown opcode 0xABCD" in the daemon log.
  Whoever lands the upstream fix should also add the name + handler entry
  to `zoneopcodes.xml`.
- **Renumbered opcodes** are silent until something breaks. Nothing to do
  beyond the side-by-side diff in step 6.

## When to stop dual-maintaining

Once `scry-web` reaches feature parity and there's no remaining user of
`showeq` for production play (target: end of Phase 5), freeze `showeq`.
Subsequent patch days edit only `scry-cpp`. Document the transition
here when it happens.
