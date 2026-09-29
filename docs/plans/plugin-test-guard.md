# Plugin-test guard: an unregistered plugin test fails configure

Spec: Joe's instruction (2026-09-29, in session), and the PLAN.md open item "A shared-build test executable that
omits `astra_add_plugin_test()` is only caught by a targeted or clean build" (origin M5 PR 12,
`docs/plans/plugin-test-helper.md`). Risk: Tier 2 (build-system behavior, every preset's configure).
Status: approved by Joe, 2026-09-29, in plan mode, no adjustments.
Base `origin/main` `c0c0439`, branch `worktree-plugin-test-guard`
(already on `c0c0439`, clean). Plan critique: none. The open item and Joe's instruction settle the design; Codex
reviews the diff under Tier 2. After approval this file is committed as `docs/plans/plugin-test-guard.md`.

## Context

In the shared build a test that `dlopen`s plugins needs `astra_add_plugin_test(<target>)`. That call gives it a
build-order dependency on every plugin. The default build (`just build`/`just check`) builds every plugin anyway,
so leaving the call out passes the gate. The mistake shows only as a race in a targeted or clean build, and
`test_new_aircraft_build.py` watches only the template's target. Goal: leaving the call out fails configure, in
every preset, and the error names the target and its directory.

## Repository findings (observed on `c0c0439`)

- `astra_add_plugin_test()` (`cmake/AstraPlugin.cmake:81`) appends to the global `ASTRA_PLUGIN_TEST_TARGETS` in
  **both** builds. `astra_finalize_plugins()` reads that property only in the shared branch
  (`add_dependencies`); the static branch ignores it. Finalize runs last (`CMakeLists.txt:62`), after every
  directory has been processed.
- CMake keeps no global target list. Every target is reachable by walking the `BUILDSYSTEM_TARGETS` property of
  each directory through `SUBDIRECTORIES`, starting at `ASTRA_SOURCE_DIR`. This covers directories added by
  discovery with their own binary dirs, and `embedded/` (its own `project()`). Imported and alias targets are not
  listed (GTest, Eigen): correct, since we do not build them.
- **Executables that compile with `ASTRA_TEST_PLUGIN_DIR`**, all via `target_compile_definitions(PRIVATE)`:
  `astra_unit_tests`, `astra_notional_transport_mass_test`, `astra_notional_transport_cruise_regression_test`
  (`tests/unit`); `astra_physics_tests`, `astra_notional_transport_{aero,turbofan}_wrench_test` (`tests/physics`);
  `astra_lockstep_tests`, `astra_lockstep_timing_tests`, `astra_echo_peer_stress_test` (Release only),
  `astra_closed_loop_tests` and `astra_closed_loop_full_test` (asan only), from `tests/lockstep`'s two functions;
  `astra_aircraft__template_tests` (`aircraft/_template/tests`); `astra_autopilot_process_tests`
  (`tests/embedded`). Only the last one is unregistered.
- **`astra_autopilot_process_tests`** carries the macro only because it compiles
  `tests/lockstep/shm_test_support.cpp`. That file's `LoadedShm` (hpp:123) and `build_sim` (cpp:445) load
  plugins by path. `autopilot_process_test.cpp` calls neither (grep: no `build_sim`, `LoadedShm`,
  `PluginLoader`, `from_config`).
- **Registered but invisible to the check**: `capture_cruise_regression` (`tests/capture`, EXCLUDE_FROM_ALL)
  takes `<plugin_dir>` on its command line and has no `ASTRA_TEST_PLUGIN_DIR`. It is registered by hand.
- The gate configures debug, release, static-plugins, asan and `build/python` (tests on). So the check runs in
  five configures per `just check`, plus the configures in `test_new_aircraft_build.py` and
  `test_tidy_fresh_configure.py`.

## Design

1. **The check, in `astra_finalize_plugins()`** (`cmake/AstraPlugin.cmake`). It runs before the static/shared
   branch, so it applies in **both** builds:
   - A private recursive helper `_astra_collect_targets(<dir> <out>)` returns the `BUILDSYSTEM_TARGETS` of the
     directory and of every directory in its `SUBDIRECTORIES`.
   - For each `EXECUTABLE` not in `ASTRA_PLUGIN_TEST_TARGETS`, read its own `COMPILE_DEFINITIONS`. If any element
     contains the token `ASTRA_TEST_PLUGIN_DIR` (regex `(^|[^A-Za-z0-9_])ASTRA_TEST_PLUGIN_DIR([^A-Za-z0-9_]|$)`,
     which also catches a generator-expression-wrapped definition), record `<target> (<SOURCE_DIR relative to the
     repo root>)`.
   - Collect every offender, then raise one `FATAL_ERROR`. It says that each target compiles with
     `ASTRA_TEST_PLUGIN_DIR` and must call `astra_add_plugin_test(<target>)` after its `add_executable`, and then
     lists each offender on its own indented line, e.g. `  astra_physics_tests (tests/physics)`. CMake does not
     re-wrap indented lines, so each name/dir pair stays on one line for grep and pytest.
   - The shared branch reuses the same `test_targets` read. The header comment above `astra_add_plugin_test`
     gains two sentences: an unregistered executable compiled with the macro fails configure, and a test that
     finds its plugins some other way must still register by hand.
   - **Why both builds:** registration is recorded in both, so the omission fails under every preset,
     including a static-only developer and the Python build. It never becomes a CMakeLists that works in one
     preset and silently loses ordering in the other. In the static build the check is stricter than linkage
     needs, but the same line is needed by the shared build.
2. **`astra_autopilot_process_tests`: register it.** Add `astra_add_plugin_test(astra_autopilot_process_tests)`
   in `tests/embedded/CMakeLists.txt`, with a comment: the support sources compile in plugin loads that these
   tests do not call, and registration keeps the check exception-free. **Why not an opt-out:** the executable
   really does link code that `dlopen`s. An opt-out (a `NO_PLUGINS` flag or target property) would be new API
   and an escape hatch that silences the guard. Its claim "loads no plugin" would go stale, unnoticed, the day a
   test there calls `build_sim`, which is the exact failure this guard exists to catch. Cost of registering:
   none in the default build or the static build. A targeted shared build of this one target also builds every
   plugin, and its compile waits on them in a clean build.
3. **Pytest regression** in `tests/python/test_new_aircraft_build.py`, reusing `_copy_sources`, `_env` and
   `REPO_ROOT`. New test `test_unregistered_plugin_test_fails_configure`, parametrized shared/static with the
   same ids (the param list is hoisted to one module constant both tests share):
   - Copy the tree. In the copy's `aircraft/_template/tests/CMakeLists.txt`, drop the line matching
     `^astra_add_plugin_test\(`. Assert exactly one line was dropped (the anchor; the header comment's mention
     does not match).
   - Configure only: `cmake --preset debug -B <tmp>/build [-DASTRA_STATIC_PLUGINS=ON]`. No build.
   - Assert a non-zero exit, and that the output contains
     `astra_aircraft__template_tests (aircraft/_template/tests)`. The exact line proves the failure is this
     check and not some other configure error.
   - The module docstring gains one paragraph for this test.
4. **PLAN.md**: remove the open item (lines 306–310). Add under M5, after PR 12: "PR 13 (#<n>): configure
   refuses an unregistered plugin test …" in the house format, with the plan link and "Packet: in the PR body".
5. **No other doc changes.** Architecture §18 stays accurate as written, and the error message explains itself.

## What the check cannot see (goes in the code comment and the PR body)

- A test that finds its plugin directory some other way: another macro name, argv (`capture_cruise_regression`),
  an environment variable, or a literal path. These must register by hand.
- A definition that does not sit on the executable's own `COMPILE_DEFINITIONS`: a directory-scope
  `add_compile_definitions`, a source file's `COMPILE_DEFINITIONS`, a linked library's
  `INTERFACE_COMPILE_DEFINITIONS`, or `-D` in `COMPILE_OPTIONS`.
- A non-executable that compiles the loading code, such as a static or object support library. The executables
  that link it carry no macro themselves.
- It checks registration, not the dependency. A broken finalize `add_dependencies` loop (PR 12's M2) is still
  caught only by `test_new_aircraft_build.py`'s shared case.

## Sequence (one builder, this worktree)

1. Commit this plan as `docs/plans/plugin-test-guard.md` (approved), push, open the draft PR (body per
   `docs/templates/pr-description.md`, gates pending).
2. Add the check (step 1) **without** the embedded registration. Configure debug into a scratchpad build dir and
   record that it fails, naming `astra_autopilot_process_tests (tests/embedded)`: the known case, seen to fire.
   Then add the registration, reconfigure (passes), and commit both together (never a commit that fails
   configure).
3. Add the pytest, run it (both cases), commit. Then PLAN.md, commit. Push after each.
4. Scratch mutations (below), then the full gate, the evidence commit, `/review-cycle` (Codex, Tier 2),
   `/human-review` for the PR body, and `/wrap`.

## Verification

- Iteration: `cmake --preset debug -B <scratchpad>/cfg-debug` and `--preset static-plugins -B …` (configure
  only), then `./scripts/check-fast.sh`.
- `.venv/bin/pytest tests/python/test_new_aircraft_build.py`: exit 0, four cases (two existing, two new). Time the
  new cases for the PR body.
- **Scratch mutations**, never committed. Each is edited with the Edit tool. Its anchor is checked with
  `grep -nF` on the unmutated file first, and `git diff --stat` on the path is the confirmation of record
  (lessons 2026-09-26, 2026-09-27, 2026-09-29). Configures go into scratchpad build dirs as separate plain calls.
  Each mutation is restored and `git diff --stat` shows clean. Output goes in the PR body:
  - **M1** (Joe's acceptance): delete `astra_add_plugin_test(astra_physics_tests)` in
    `tests/physics/CMakeLists.txt`. Expected: `--preset debug` and `--preset static-plugins` both exit non-zero,
    naming `astra_physics_tests (tests/physics)`.
  - **M2**: delete the call inside `astra_lockstep_test()` in `tests/lockstep/CMakeLists.txt`. Expected: one
    error listing both `astra_lockstep_tests` and `astra_lockstep_timing_tests (tests/lockstep)`, showing that
    every offender is reported and that function-made targets are seen.
  - **M3**: neuter the check (its `if(unregistered)` guard made false). Expected: both new pytest cases fail, and
    the check's removal is caught by the regression.
- Full gate from a fresh configure (the worktree has no `build/`; the gate creates `.venv`):
  `./scripts/check-full.sh` via the Bash tool's `run_in_background`. Exit 0 and `check: OK`. Trimmed
  `docs/reviews/evidence/<head>-gate-summary.log` committed. Command, exit code, counts and the added pytest
  time go in the PR body. No CI rerun or wait (no Actions minutes until Oct 1).
- Codex review per Tier 2 via `/review-cycle`. Verdict and thread id go in the PR body. At most two correction
  rounds.

## Done when

The check fires in both builds (K, M1, M2), the pytest guards it (M3), the full gate passes on the candidate,
Codex has reviewed it, and the PR body is written for Joe. Merge is Joe's.
