#!/usr/bin/env bash
# The one entry point for ADR-0021's R1/R1-Sim golden capture step
# (docs/plans/pr4a-dynamics-model.md, "R1 golden capture"). Run once, from the repo root, in a
# session Joe has armed with ASTRA_TOUCH_GOLDENS=1/.claude/touch-goldens. Never run by `just check`.
#
# What it does, in order:
#   1. Refuses to run unless the tree is clean except for this capture step's own new/modified files
#      (this script, the harness, its CMakeLists.txt, tests/CMakeLists.txt's one-line addition).
#   2. Refuses to run unless core/, plugins/, cmake/, the top-level build files, and
#      aircraft/notional_transport/ are byte-identical to the fixed baseline revision 26ad594 -- the
#      pre-change revision the R1/R1-Sim goldens characterize.
#   3. Commits the harness + a frozen copy of aircraft/notional_transport/configs/cruise.yaml (the
#      one commit this step is allowed to make before capture).
#   4. Builds and runs the harness under all four presets (debug, release, asan, static-plugins),
#      capturing each preset's raw output.
#   5. Cross-checks all four presets' output byte-for-byte, for both R1 and R1-Sim. Any disagreement
#      stops the script here, prints the diff, and writes nothing under tests/golden/**.
#   6. Writes the two golden headers and their shared manifest, and commits them.
#   7. Prints the four-preset agreement result and exits. Does not begin any implementation work.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

readonly BASELINE_REVISION="26ad594"
readonly GUARD_DIFF_PATHS=(
  core/ plugins/ cmake/ CMakeLists.txt CMakePresets.json justfile aircraft/notional_transport/
)
readonly CAPTURE_STEP_OWN_PATHS=(
  "tests/golden/dynamics/capture_r1_golden.cpp"
  "tests/golden/dynamics/capture_r1_golden.sh"
  "tests/golden/dynamics/CMakeLists.txt"
  "tests/CMakeLists.txt"
)
readonly DYNAMICS_DIR="tests/golden/dynamics"
readonly FIXTURE_SRC="aircraft/notional_transport/configs/cruise.yaml"
readonly FIXTURE_DST="${DYNAMICS_DIR}/cruise_end_to_end_r1_fixture.yaml"
readonly MANIFEST="${DYNAMICS_DIR}/rigid_body_6dof_r1_manifest.md"
readonly R1_GOLDEN="${DYNAMICS_DIR}/rigid_body_6dof_r1_golden.hpp"
readonly R1SIM_GOLDEN="${DYNAMICS_DIR}/cruise_end_to_end_r1_golden.hpp"
readonly PRESETS=(debug release asan static-plugins)
readonly JOBS="$(nproc)"

log() { echo "capture_r1_golden.sh: $*"; }
die() {
  echo "capture_r1_golden.sh: $*" >&2
  exit 1
}

# --- Step 1: clean tree, except this capture step's own expected new/modified files. ---
check_clean_tree() {
  local exclude_args=()
  local path
  for path in "${CAPTURE_STEP_OWN_PATHS[@]}"; do
    exclude_args+=(":(exclude)${path}")
  done
  local unexpected
  unexpected="$(git status --porcelain -- . "${exclude_args[@]}")"
  if [[ -n "${unexpected}" ]]; then
    echo "${unexpected}" >&2
    die "tree has changes outside this capture step's own files (see above); commit or discard them first"
  fi
  log "clean tree, apart from this capture step's own harness/CMake files."
}

# --- Step 2: no diff against the baseline in the paths that affect the captured arithmetic. ---
check_no_drift_from_baseline() {
  if ! git diff --quiet "${BASELINE_REVISION}" -- "${GUARD_DIFF_PATHS[@]}"; then
    git diff --stat "${BASELINE_REVISION}" -- "${GUARD_DIFF_PATHS[@]}" >&2
    die "core/, plugins/, cmake/, top-level build files, or aircraft/notional_transport/ differ" \
        "from baseline ${BASELINE_REVISION} (see above) -- refusing to capture"
  fi
  log "no drift from baseline ${BASELINE_REVISION} in the guarded paths."
}

# --- Step 3: freeze the cruise.yaml fixture and commit the harness. ---
commit_harness() {
  cp "${FIXTURE_SRC}" "${FIXTURE_DST}"
  git add "${CAPTURE_STEP_OWN_PATHS[@]}" "${FIXTURE_DST}"
  git commit -m "M3 PR 4a: R1/R1-Sim golden capture harness and frozen cruise.yaml fixture

Adds tests/golden/dynamics/capture_r1_golden.{cpp,sh} (the R1/R1-Sim capture harness and its
wrapper script), tests/golden/dynamics/CMakeLists.txt (an EXCLUDE_FROM_ALL target, never built
by just check), and a frozen copy of aircraft/notional_transport/configs/cruise.yaml as this
capture step's own end-to-end fixture. No production code changed; baseline ${BASELINE_REVISION}
verified byte-identical in core/, plugins/, cmake/, top-level build files, and
aircraft/notional_transport/ before this commit."
  log "committed harness + frozen fixture as $(git rev-parse HEAD)."
}

# --- Step 4: build and run the harness under all four presets. ---
capture_under_all_presets() {
  local workdir="$1"
  local preset
  for preset in "${PRESETS[@]}"; do
    log "=== preset ${preset}: configuring ==="
    cmake --preset "${preset}"
    log "=== preset ${preset}: full build (every plugin) ==="
    cmake --build "build/${preset}" --parallel "${JOBS}"
    log "=== preset ${preset}: building capture_r1_golden ==="
    cmake --build "build/${preset}" --target capture_r1_golden --parallel "${JOBS}"

    local harness="build/${preset}/tests/golden/dynamics/capture_r1_golden"
    local plugin_dir="build/${preset}/plugins"
    "${harness}" r1 "${plugin_dir}" >"${workdir}/${preset}.r1.out"
    "${harness}" r1sim "${plugin_dir}" "${FIXTURE_DST}" >"${workdir}/${preset}.r1sim.out"
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

# --- Step 5: cross-check all four presets, per mode. Sets AGREEMENT[mode]=true/false. ---
declare -A AGREEMENT
cross_check() {
  local workdir="$1"
  local mode
  for mode in r1 r1sim; do
    local reference="${workdir}/${PRESETS[0]}.${mode}.out"
    local ok=true
    local preset
    for preset in "${PRESETS[@]:1}"; do
      if ! diff -q "${reference}" "${workdir}/${preset}.${mode}.out" >/dev/null; then
        ok=false
        echo "capture_r1_golden.sh: DISAGREEMENT in ${mode} between ${PRESETS[0]} and ${preset}:" >&2
        diff "${reference}" "${workdir}/${preset}.${mode}.out" >&2 || true
      fi
    done
    AGREEMENT[${mode}]="${ok}"
    log "${mode}: cross_preset_agreement=${ok}"
  done
}

# --- Step 6: write the golden headers and the shared manifest. ---
write_goldens() {
  local workdir="$1"
  local reference="${PRESETS[0]}"

  {
    echo "// R1 (ADR-0021 bit-identity): frozen, pre-change RigidBody arithmetic, captured from"
    echo "// the ${BASELINE_REVISION} build. Human-owned (tests/golden/**); see"
    echo "// rigid_body_6dof_r1_manifest.md for how, when, and under what this was captured."
    echo "// Never hand-edit; re-run capture_r1_golden.sh under a fresh Joe authorization instead."
    echo "#pragma once"
    echo ""
    echo "#include <array>"
    echo "#include <cstdint>"
    echo ""
    echo "namespace astra::test::r1 {"
    echo ""
    cat "${workdir}/${reference}.r1.out"
    echo ""
    echo "}  // namespace astra::test::r1"
  } >"${R1_GOLDEN}"

  {
    echo "// R1-Sim (ADR-0021, end-to-end companion to R1): a full Simulation::from_config run"
    echo "// over the frozen cruise_end_to_end_r1_fixture.yaml in this directory, captured from"
    echo "// the ${BASELINE_REVISION} build. Human-owned (tests/golden/**); see"
    echo "// rigid_body_6dof_r1_manifest.md for how, when, and under what this was captured."
    echo "// Never hand-edit; re-run capture_r1_golden.sh under a fresh Joe authorization instead."
    echo "#pragma once"
    echo ""
    echo "#include <array>"
    echo "#include <cstdint>"
    echo ""
    echo "namespace astra::test::r1sim {"
    echo ""
    cat "${workdir}/${reference}.r1sim.out"
    echo ""
    echo "}  // namespace astra::test::r1sim"
  } >"${R1SIM_GOLDEN}"

  clang-format-18 -i "${R1_GOLDEN}" "${R1SIM_GOLDEN}"

  local harness_blob fixture_blob
  harness_blob="$(git rev-parse "HEAD:${DYNAMICS_DIR}/capture_r1_golden.cpp")"
  fixture_blob="$(git rev-parse "HEAD:${FIXTURE_DST}")"

  {
    echo "# R1 / R1-Sim capture manifest (ADR-0021)"
    echo ""
    echo "Captured $(date -u +%Y-%m-%dT%H:%M:%SZ) by tests/golden/dynamics/capture_r1_golden.sh,"
    echo "per docs/plans/pr4a-dynamics-model.md's \"R1 golden capture\" section."
    echo ""
    echo "- \`baseline_revision\`: \`${BASELINE_REVISION}\` -- the pre-change revision this"
    echo "  golden's arithmetic actually is. Verified byte-identical to this capture in"
    echo "  \`core/\`, \`plugins/\`, \`cmake/\`, the top-level build files, and"
    echo "  \`aircraft/notional_transport/\` immediately before the harness commit below."
    echo "- \`capture_revision\`: \`${CAPTURE_REVISION}\` -- the actual commit the harness ran"
    echo "  from (the harness file itself, and this worktree's own docs bookkeeping ahead of"
    echo "  it, postdate \`baseline_revision\`; the captured arithmetic does not)."
    echo "- \`cross_preset_agreement\`:"
    echo "  - r1: \`${AGREEMENT[r1]}\`"
    echo "  - r1sim: \`${AGREEMENT[r1sim]}\`"
    echo "- Capture harness: \`${DYNAMICS_DIR}/capture_r1_golden.cpp\`, blob \`${harness_blob}\`."
    echo "- Frozen fixture: \`${FIXTURE_DST}\`, blob \`${fixture_blob}\`" \
      "(copied verbatim from \`${FIXTURE_SRC}\` at capture time)."
    echo "- Sampled initial-state bit patterns (R1): see \`kRigidBody6DofR1Initial\` in"
    echo "  \`rigid_body_6dof_r1_golden.hpp\` -- the actual values, not a description."
    echo "- Applied wrench (R1, an input, not a captured result): see"
    echo "  \`kRigidBody6DofR1Wrench\` in the same file."
    echo "- R1's five sampled initial states are hardcoded \`std::uint64_t\` bit patterns in"
    echo "  \`capture_r1_golden.cpp\`'s own \`sample_states()\`, not computed at capture time. An"
    echo "  earlier version of that function built samples 1/2/4's quaternions at runtime via"
    echo "  \`std::sin\`/\`std::cos\`/\`Eigen::Vector4d::normalize()\`; a first capture run found"
    echo "  debug and release disagreeing by 1 ULP on sample 2's \`w\`/\`x\` components (diagnosed:"
    echo "  the six underlying \`std::sin\`/\`std::cos\` results were bit-identical across presets;"
    echo "  only the multiply-add combination (a \`ca*cb*cc + sa*sb*sc\`-shaped expression)"
    echo "  differed -- \`-ffp-contract\` contracting that expression shape differently by"
    echo "  optimization level, not a hardware FMA effect, since this build passes no"
    echo "  \`-march\`/\`-mfma\`)."
    echo "  The current hardcoded bit patterns were taken from a **release**-preset run of that"
    echo "  prior, runtime-computed code -- computed once, frozen, never recomputed by this"
    echo "  program again. See \`docs/lessons.md\` for the general rule this became."
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

  log "wrote ${R1_GOLDEN}, ${R1SIM_GOLDEN}, ${MANIFEST}."
}

commit_goldens() {
  git add "${R1_GOLDEN}" "${R1SIM_GOLDEN}" "${MANIFEST}"
  git commit -m "M3 PR 4a: R1/R1-Sim frozen goldens, captured from ${BASELINE_REVISION}

cross_preset_agreement: r1=${AGREEMENT[r1]} r1sim=${AGREEMENT[r1sim]} (all four presets:
debug, release, asan, static-plugins). See rigid_body_6dof_r1_manifest.md for the full record."
  log "committed goldens as $(git rev-parse HEAD)."
}

main() {
  check_clean_tree
  check_no_drift_from_baseline
  commit_harness
  readonly CAPTURE_REVISION="$(git rev-parse HEAD)"

  local workdir
  workdir="$(mktemp -d)"
  trap 'rm -rf "${workdir}"' EXIT

  capture_under_all_presets "${workdir}"
  cross_check "${workdir}"

  if [[ "${AGREEMENT[r1]}" != true || "${AGREEMENT[r1sim]}" != true ]]; then
    die "cross-preset disagreement (see above) -- no golden written. Report this to Joe."
  fi

  write_goldens "${workdir}"
  commit_goldens

  log "DONE. cross_preset_agreement: r1=${AGREEMENT[r1]} r1sim=${AGREEMENT[r1sim]}."
}

main "$@"
