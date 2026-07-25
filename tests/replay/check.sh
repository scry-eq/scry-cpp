#!/usr/bin/env bash
# Tier-2 regression check: replay every local .vpk fixture through the
# daemon, regenerate the envelope golden into a temp file, and cmp
# against the committed-locally .pbstream. Exits non-zero if any
# fixture mismatches — suitable for a git pre-push hook.
#
# Fixtures are personal data (see ../README.md), so this script is
# intentionally a developer-local tool, not wired into ctest/CI.
#
# Usage:
#   tests/replay/check.sh                 # check all fixtures
#   tests/replay/check.sh walk-and-zone   # check one (substring match)

set -euo pipefail

REPLAY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAEMON_DIR="$(cd "${REPLAY_DIR}/../.." && pwd)"
# The daemon is compiled for exactly one backend (-DSEQ_TARGET). Goldens
# only match the backend that produced them, so fixtures live in per-target
# subdirs (live/ eql/ test/) and we validate just the current build's set.
# SEQ_BUILD_DIR lets a dev point at a sibling build dir (build-test/ etc.).
BUILD_DIR="${SEQ_BUILD_DIR:-${DAEMON_DIR}/build}"
DAEMON="${BUILD_DIR}/showeq-daemon"
CONF_DIR="${DAEMON_DIR}/conf"
FILTER="${1:-}"

if [[ ! -x "${DAEMON}" ]]; then
    echo "error: daemon not built — run 'cmake --build ${BUILD_DIR##*/} -j' in ${DAEMON_DIR}" >&2
    exit 2
fi

# Detect the compiled backend from the build's CMakeCache (same parse as
# build.sh; unset == the default 'live'). Override with SEQ_CHECK_TARGET.
detect_target() {
    local cache="${BUILD_DIR}/CMakeCache.txt" t=""
    [[ -f "${cache}" ]] && t="$(sed -n 's/^SEQ_TARGET:[^=]*=//p' "${cache}")"
    echo "${t:-live}"
}
TARGET="${SEQ_CHECK_TARGET:-$(detect_target)}"
GOLDEN_DIR="${REPLAY_DIR}/${TARGET}"
# Mirrors the compiled SEQ_DATA_NAMESPACE: live is flat at ~/.showeq, other
# targets nest under it.
if [[ "${TARGET}" == "live" ]]; then
    GUILD_CACHE="${HOME}/.showeq/tmp/guilds2.dat"
else
    GUILD_CACHE="${HOME}/.showeq/${TARGET}/tmp/guilds2.dat"
fi
echo "tier-2 target=${TARGET} (from ${BUILD_DIR##*/}/CMakeCache.txt); fixtures in ${GOLDEN_DIR#${REPLAY_DIR}/}/"

# Pin the map package so the goldens' embedded map geometry is deterministic.
# The daemon otherwise resolves the active [Maps] Package pref against the
# dev-local (gitignored) conf/maps tree, which can point the same package name
# at different folders (e.g. brewall vs "Good's Maps") depending on ambient
# state — silently changing the map geometry baked into zone_changed/snapshot
# envelopes and flapping the byte-cmp. eql goldens are recorded against the
# classic "brewall" set, so pin it here; live/test keep their ambient default
# (unchanged). When (re)recording an eql golden, pass the SAME
# `--map-package brewall`. Override with SEQ_MAP_PACKAGE.
map_default=""
[[ "${TARGET}" == "eql" ]] && map_default="brewall"
MAP_PACKAGE="${SEQ_MAP_PACKAGE:-${map_default}}"
MAP_ARGS=()
[[ -n "${MAP_PACKAGE}" ]] && MAP_ARGS=(--map-package "${MAP_PACKAGE}")

shopt -s nullglob
vpks=("${GOLDEN_DIR}"/*.vpk)
if [[ ${#vpks[@]} -eq 0 ]]; then
    echo "no .vpk fixtures in ${GOLDEN_DIR} — nothing to check for target '${TARGET}' (see README.md)" >&2
    exit 0
fi

# High-random port avoids collision with a daemon you might have running.
PORT=$(( 50000 + RANDOM % 10000 ))
TMPDIR_RUN="$(mktemp -d)"
trap 'rm -rf "${TMPDIR_RUN}"' EXIT

pass=0
fail=0
skip=0
failures=()

for vpk in "${vpks[@]}"; do
    name="$(basename "${vpk}" .vpk)"
    if [[ -n "${FILTER}" && "${name}" != *"${FILTER}"* ]]; then
        continue
    fi

    # Per-fixture skip list. Drop a fixture's basename here when its
    # daemon-side handling is wallclock-dependent and cmp would flap.
    # Currently: SpellShell::timeout fires every 6s independent of the
    # packet stream (decrements buff durations / removes expired ones),
    # so any fixture that exercises buff lifecycles produces a different
    # byte stream per run.
    # Fixtures where the player casts DoTs/debuffs (on self or mobs) exercise
    # the wallclock SpellShell 6s timer via Buffs/SpawnEffectsUpdate — the
    # effect's captured_ms and decrementing duration flap the byte stream
    # run-to-run. eqlegends-death-respawn (eql) + moblock/spell-mem/
    # buffquery-aaaction (live) join the original `buffs` fixture here.
    case "${name}" in
      buffs|eqlegends-death-respawn|moblock|spell-mem|buffquery-aaaction)
        echo "SKIP ${name} (wallclock-dependent SpellShell timer; cmp non-deterministic)"
        skip=$((skip+1))
        continue
        ;;
    esac

    golden="${GOLDEN_DIR}/${name}.pbstream"
    if [[ ! -f "${golden}" ]]; then
        echo "SKIP ${name} (no .pbstream — capture a golden via --record-golden first)"
        skip=$((skip+1))
        continue
    fi

    check="${TMPDIR_RUN}/${name}.pbstream"
    log="${TMPDIR_RUN}/${name}.log"
    PORT=$((PORT+1))

    # GuildMgr persists learned guild id→name pairs to guilds2.dat and reloads
    # them at startup, so a warm cache resolves spawn guild tags earlier than a
    # cold one and shifts the envelope stream. Clear it per fixture so goldens
    # always encode the cold path and reproduce on a fresh clone / in CI.
    rm -f "${GUILD_CACHE}"

    if ! "${DAEMON}" \
            --replay "${vpk}" \
            --config-dir "${CONF_DIR}" \
            --record-golden "${check}" \
            ${MAP_ARGS[@]+"${MAP_ARGS[@]}"} \
            --listen "127.0.0.1:${PORT}" >"${log}" 2>&1; then
        echo "FAIL ${name} (daemon exited non-zero — see ${log})"
        fail=$((fail+1))
        failures+=("${name}")
        continue
    fi

    # Compare method: exact byte-cmp by default. The long, high-spawn captures
    # below carry the under-load spawn flap (idempotent duplicate spawn_added +
    # async guild-tag back-fill timing; see semantic_diff.py and
    # project_eql_golden_spawn_order_flap) that a byte-cmp can't tolerate, so they
    # compare SEMANTICALLY — real regressions (spawn content/presence, non-spawn
    # events) still fail; only the flap is normalized away. live-guild is a busy
    # Live zone (232-member guild roster + 4 in-zone guilds tagging ~600 spawns).
    case "${name}" in
      eql-contarget|eqlegends-patch20260714|eql-fighting|live-guild)
        if semdiff="$(python3 "${DAEMON_DIR}/scripts/semantic_diff.py" "${golden}" "${check}" 2>&1)"; then
            echo "PASS ${name} (semantic: ${semdiff})"
            pass=$((pass+1))
        else
            keep="${GOLDEN_DIR}/${name}.check.pbstream"
            cp "${check}" "${keep}"
            echo "FAIL ${name} (semantic): $(echo "${semdiff}" | head -1)"
            echo "     golden:  ${golden}"
            echo "     produced: ${keep}"
            echo "${semdiff}" | sed -n '2,3p' | sed 's/^/     /'
            fail=$((fail+1))
            failures+=("${name}")
        fi
        ;;
      *)
        if cmp --silent "${golden}" "${check}"; then
            echo "PASS ${name}"
            pass=$((pass+1))
        else
            # Save divergence artifacts so the user can inspect them after
            # the script's tmpdir would otherwise be wiped.
            keep="${GOLDEN_DIR}/${name}.check.pbstream"
            cp "${check}" "${keep}"
            bytes="$(cmp "${golden}" "${check}" 2>&1 || true)"
            echo "FAIL ${name}: ${bytes}"
            echo "     golden:  ${golden}"
            echo "     produced: ${keep}"
            fail=$((fail+1))
            failures+=("${name}")
        fi
        ;;
    esac
done

echo
echo "summary: ${pass} pass, ${fail} fail, ${skip} skip"
if [[ ${fail} -gt 0 ]]; then
    echo "failed fixtures: ${failures[*]}"
    exit 1
fi
