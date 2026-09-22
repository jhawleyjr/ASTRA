#!/usr/bin/env bash
# The one entry point for M3 PR 8's cruise regression golden capture (D20,
# docs/plans/pr8-cruise-regression.md). Writes tests/golden/notional_transport/
# cruise_regression_golden.hpp and cruise_regression_manifest.md, and commits them. Never run by
# `just check`. Follows tests/golden/dynamics/capture_r1_golden.sh's shape, with one addition: this
# script checks arming ITSELF instead of relying on the guard-oracle.sh hook, which inspects only a
# command's own text and never sees what a script writes.
#
# In order, stopping at the first failure with nothing written under tests/golden/**:
#   1. Refuses unless armed: ASTRA_TOUCH_GOLDENS=1, or the gitignored marker .claude/touch-goldens in
#      this tree or in the main checkout -- the same test guard-oracle.sh applies.
#   1b. Refuses if the golden header or manifest already exists (Joe's J-2): arming alone never
#      authorizes a re-capture. Re-capturing needs Joe's fresh, explicit authorization, given by
#      removing the existing golden in a commit he authorizes; there is deliberately no override
#      flag, because arming plus a flag would just be a second arming.
#   2. Refuses unless the whole tree is clean (the harness, its CMake and the trimmed cruise.yaml
#      are committed before capture, so the capture SHA names exactly what ran).
#   3. Refuses unless the baseline revision is an ancestor of HEAD.
#   4. Builds and runs the harness under all four presets (debug, release, asan, static-plugins).
#   5. Refuses on any halt (a halt is a finding, D20) or any byte difference between presets.
#   6. Writes the golden header and manifest, clang-formats the header, and commits both.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

readonly BASELINE_REVISION="553700f"
readonly GOLDEN_DIR="tests/golden/notional_transport"
readonly GOLDEN="${GOLDEN_DIR}/cruise_regression_golden.hpp"
readonly MANIFEST="${GOLDEN_DIR}/cruise_regression_manifest.md"
readonly HARNESS_SRC="tests/capture/capture_cruise_regression.cpp"
readonly RUN_SRC="tests/support/cruise_regression_run.hpp"
readonly CONFIG_SRC="aircraft/notional_transport/configs/cruise.yaml"
readonly PRESETS=(debug release asan static-plugins)
# Global, not `local` to main(): the EXIT trap fires after main() returns (capture_r1_golden.sh's
# own "workdir: unbound variable" lesson).
WORKDIR=""

log() { echo "capture_cruise_regression.sh: $*"; }
die() {
  echo "capture_cruise_regression.sh: $*" >&2
  exit 1
}

# --- Step 1: armed, checked before anything else touches the tree or the build. ---
check_armed() {
  if [[ "${ASTRA_TOUCH_GOLDENS:-0}" == "1" ]]; then
    log "armed (ASTRA_TOUCH_GOLDENS=1)."
    return
  fi
  # --git-common-dir is the main checkout's .git: relative from the main checkout, absolute from a
  # worktree -- resolved exactly as guard-oracle.sh resolves it.
  local common main_root
  common="$(git rev-parse --git-common-dir 2>/dev/null || true)"
  case "${common}" in
    "") main_root="${ROOT}" ;;
    /*) main_root="$(dirname "${common}")" ;;
    *) main_root="$(cd "${ROOT}/$(dirname "${common}")" && pwd)" ;;
  esac
  if [[ -e "${ROOT}/.claude/touch-goldens" || -e "${main_root}/.claude/touch-goldens" ]]; then
    log "armed (.claude/touch-goldens)."
    return
  fi
  die "unarmed: tests/golden/** is human-owned. Joe arms a capture by touching" \
      ".claude/touch-goldens (this tree or the main checkout) or launching with" \
      "ASTRA_TOUCH_GOLDENS=1. Nothing was built or written."
}

# --- Step 1b: no golden yet. A re-capture is Joe's explicit decision, not a side effect of arming. ---
check_no_existing_golden() {
  if [[ -e "${GOLDEN}" || -e "${MANIFEST}" ]]; then
    die "a golden already exists (${GOLDEN} / ${MANIFEST}). Arming does not authorize a" \
        "re-capture: that needs Joe's fresh, explicit authorization -- he removes the existing" \
        "golden in a commit he authorizes, then re-runs this script. Nothing was built or written."
  fi
  log "no existing golden."
}

# --- Step 2: the whole tree clean. ---
check_clean_tree() {
  local unexpected
  unexpected="$(git status --porcelain)"
  if [[ -n "${unexpected}" ]]; then
    echo "${unexpected}" >&2
    die "tree has uncommitted changes (see above); commit or discard them first -- the capture" \
        "SHA must name exactly what ran"
  fi
  log "clean tree."
}

# --- Step 3: the baseline is an ancestor of what is being captured. ---
check_baseline() {
  if ! git merge-base --is-ancestor "${BASELINE_REVISION}" HEAD 2>/dev/null; then
    die "baseline ${BASELINE_REVISION} is not an ancestor of HEAD -- refusing to capture"
  fi
  log "baseline ${BASELINE_REVISION} is an ancestor of HEAD $(git rev-parse --short HEAD)."
}

# --- Step 4: build and run the harness under every preset. ---
capture_under_all_presets() {
  local workdir="$1"
  local jobs preset
  jobs="$(nproc)"
  for preset in "${PRESETS[@]}"; do
    log "=== preset ${preset}: configure and full build (every plugin) ==="
    cmake --preset "${preset}"
    cmake --build "build/${preset}" --parallel "${jobs}"
    cmake --build "build/${preset}" --target capture_cruise_regression --parallel "${jobs}"

    local harness="build/${preset}/tests/capture/capture_cruise_regression"
    local status=0
    "${harness}" trajectory "${ROOT}/aircraft" "build/${preset}/plugins" \
      >"${workdir}/${preset}.trajectory.out" || status=$?
    if [[ "${status}" -ne 0 ]]; then
      die "preset ${preset}: harness exited ${status} (2 = the run halted, a finding) --" \
          "no golden written. Report this to Joe."
    fi
    "${harness}" meta >"${workdir}/${preset}.meta.out"
    {
      clang++-18 --version
      echo "target_triple=$(clang++-18 -dumpmachine)"
      grep -E '^CMAKE_BUILD_TYPE:|^ASTRA_SANITIZERS:|^ASTRA_STATIC_PLUGINS:' \
        "build/${preset}/CMakeCache.txt" || true
    } >"${workdir}/${preset}.toolchain.out"
    log "=== preset ${preset}: captured ==="
  done
}

# --- Step 5: byte-for-byte agreement across presets. ---
cross_check() {
  local workdir="$1"
  local reference="${workdir}/${PRESETS[0]}.trajectory.out"
  local ok=true preset
  for preset in "${PRESETS[@]:1}"; do
    if ! cmp -s "${reference}" "${workdir}/${preset}.trajectory.out"; then
      ok=false
      echo "capture_cruise_regression.sh: DISAGREEMENT between ${PRESETS[0]} and ${preset}:" >&2
      diff "${reference}" "${workdir}/${preset}.trajectory.out" | head -40 >&2 || true
    fi
  done
  if [[ "${ok}" != true ]]; then
    die "cross-preset disagreement (see above) -- no golden written. Report this to Joe."
  fi
  log "cross_preset_agreement=true (${PRESETS[*]})."
}

# --- Step 6: write, format, commit. ---
write_goldens() {
  local workdir="$1"
  mkdir -p "${GOLDEN_DIR}"
  {
    echo "// M3 PR 8 cruise regression trajectory (D20, docs/plans/pr8-cruise-regression.md). O5: a"
    echo "// regression, not an oracle. Human-owned (tests/golden/**); see"
    echo "// cruise_regression_manifest.md for how, when and from what this was captured. Never"
    echo "// hand-edit; re-run tests/capture/capture_cruise_regression.sh under a fresh Joe"
    echo "// authorization instead."
    echo "#pragma once"
    echo ""
    echo "#include <array>"
    echo "#include <cstddef>"
    echo "#include <cstdint>"
    echo ""
    echo "namespace astra::test::cruise_regression_golden {"
    echo ""
    cat "${workdir}/${PRESETS[0]}.trajectory.out"
    echo ""
    echo "}  // namespace astra::test::cruise_regression_golden"
  } >"${GOLDEN}"
  clang-format-18 -i "${GOLDEN}"

  {
    echo "# Cruise regression capture manifest (M3 PR 8, D20)"
    echo ""
    echo "Captured $(date -u +%Y-%m-%dT%H:%M:%SZ) by \`tests/capture/capture_cruise_regression.sh\`,"
    echo "per \`docs/plans/pr8-cruise-regression.md\`."
    echo ""
    echo "- \`baseline_revision\`: \`${BASELINE_REVISION}\` -- \`origin/main\` this PR branched from."
    echo "- \`capture_revision\`: \`${CAPTURE_REVISION}\` -- the clean commit the harness ran from."
    echo "- \`cross_preset_agreement\`: \`true\` (${PRESETS[*]}, byte for byte); no halt."
    echo "- Run: \`${CONFIG_SRC}\` (blob \`$(git rev-parse "HEAD:${CONFIG_SRC}")\`) through"
    echo "  \`Simulation::from_config\`; the 13-double rigid-body state sampled at frame 0 and after"
    echo "  each of 300 \`run(1000)\` calls (1 Hz, 300 s, dt = 1 ms), frozen as \`std::bit_cast\`"
    echo "  bit patterns. Run definition: \`${RUN_SRC}\` (blob"
    echo "  \`$(git rev-parse "HEAD:${RUN_SRC}")\`); harness: \`${HARNESS_SRC}\` (blob"
    echo "  \`$(git rev-parse "HEAD:${HARNESS_SRC}")\`)."
    echo "- **What this trajectory is.** The initial state and controls are the still-air trim"
    echo "  (\`astra.trim\`, D19; residual 8.3e-17 without wind). The config keeps its Dryden wind"
    echo "  (seed 42), so the run starts in turbulence: the frame-0 gust is"
    echo "  \`wind_n = (-3.08, 0.91, 6.63)\` m/s (stationary start), and the residual with that gust"
    echo "  latched is 3.77 m/s^2. This is a turbulence response from trim, not a cruise hold."
    echo ""
    echo "## Per-preset build metadata"
    echo ""
    local preset
    for preset in "${PRESETS[@]}"; do
      echo "### ${preset}"
      echo '```'
      cat "${workdir}/${preset}.meta.out"
      cat "${workdir}/${preset}.toolchain.out"
      echo '```'
      echo ""
    done
  } >"${MANIFEST}"
  log "wrote ${GOLDEN} and ${MANIFEST}."
}

commit_goldens() {
  git add "${GOLDEN}" "${MANIFEST}"
  git commit -m "M3 PR 8: cruise regression golden, captured at ${CAPTURE_REVISION:0:7}

cross_preset_agreement: true (${PRESETS[*]}); no halt. Baseline ${BASELINE_REVISION}. See
${MANIFEST} for the full record."
  log "committed goldens as $(git rev-parse HEAD)."
}

main() {
  check_armed
  check_no_existing_golden
  check_clean_tree
  check_baseline
  readonly CAPTURE_REVISION="$(git rev-parse HEAD)"

  WORKDIR="$(mktemp -d)"
  trap 'rm -rf "${WORKDIR}"' EXIT

  capture_under_all_presets "${WORKDIR}"
  cross_check "${WORKDIR}"
  write_goldens "${WORKDIR}"
  commit_goldens
  log "DONE."
}

main "$@"
