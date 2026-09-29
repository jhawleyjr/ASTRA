# astra_add_plugin_test(): a test declares its own plugin build order

Spec: Joe's instruction (2026-09-28, in session); PLAN.md open item "`cmake/AstraPlugin.cmake`'s
`astra_finalize_plugins()` hardcodes which test executables…"; architecture §18 ("copy `_template`, edit,
rebuild; nothing in the framework is edited"). Risk: Tier 2 (a build-system change touching every preset).
Status: **approved by Joe, 2026-09-28, in session, with the four adjustments in "Approved adjustments".**
Base `origin/main` `ffb395a`, branch `worktree-plugin-test-helper`. Plan critique: none, since architecture §18
pins the design (Codex reviews the diff, per Tier 2).

## Context

In the shared build a test that `dlopen`s plugins has no link edge to them, so it needs
`add_dependencies(<test> <every plugin>)` or a targeted or clean build races the `.so` files. Today
`astra_finalize_plugins()` (`cmake/AstraPlugin.cmake`) carries that as a hardcoded list of 15 target names. A
new aircraft with its own test executable must edit that framework file, which breaks §18. And
`aircraft/_template` has no test target of its own: its three tests live in
`tests/unit/{mass,aero,turbofan}/template_*_test.cpp` and hardcode `"aircraft/_template/..."` load paths, so a
copy gets no tests at all.

## Repository findings (observed on `ffb395a`)

- **The list**: `astra_unit_tests`, `astra_physics_tests`, `astra_notional_transport_mass_test`,
  `astra_template_{mass,aero,turbofan}_test`, `astra_notional_transport_{aero,turbofan}_wrench_test`,
  `astra_notional_transport_cruise_regression_test`, `capture_cruise_regression` (EXCLUDE_FROM_ALL),
  `astra_lockstep_tests`, `astra_lockstep_timing_tests`, `astra_echo_peer_stress_test` (Release only),
  `astra_closed_loop_tests`, `astra_closed_loop_full_test` (asan only). Guarded by `if(TARGET …)`.
- The last five come from two local functions, `astra_lockstep_test()` and `astra_closed_loop_test()` in
  `tests/lockstep/CMakeLists.txt`; one call inside each covers all five, conditionals included.
- **Static build** already orders by linkage (every plugin object links into `astra_core`); the list is
  shared-build only.
- **Registration order**: `aircraft/**` is discovered before `tests/`, and aircraft discovery is alphabetical,
  so a test inside `aircraft/a/` is declared before `aircraft/b/`'s plugins exist. The helper records the
  target, and `astra_finalize_plugins()` (which already runs last) adds the dependencies.
- **CTest and GTest scope**: `enable_testing()`, `find_package(GTest)` and `include(GoogleTest)` run only in
  or after `tests/` today. A directory added before `enable_testing()` gets no `CTestTestfile`, and
  `GTest::gtest_main` (a directory-scoped imported target) is invisible to `aircraft/`. Both move before
  discovery in the top-level `CMakeLists.txt`.
- **`scripts/new-aircraft.sh`** copies `aircraft/_template/.` (dotfiles included), rewrites only
  `configs/*.yaml` plugin paths and the two math namespaces. Nothing rewrites a `.cpp` load path, so a copied
  test gets its aircraft path from CMake, not from a literal.
- **Tidy**: `tests/.clang-tidy` drops `readability-magic-numbers` and cognitive-complexity for GoogleTest;
  a test under `aircraft/` would not inherit that.
- **Wall-clock scan** (`scripts/wallclock_scan.py`) scans every TU under `aircraft/`, so the moved test TUs
  come into its scope (stricter than today, where `tests/` is out of scope).
- `pyproject.toml` builds `build/python` with `ASTRA_BUILD_TESTS=ON`, so the aircraft test builds there too.

## Design

1. **`astra_add_plugin_test(<target>)`** in `cmake/AstraPlugin.cmake`, in `astra_add_plugin`'s style:
   `FATAL_ERROR` on a missing target or extra arguments, naming `CMAKE_CURRENT_SOURCE_DIR`; appends to a new
   global property `ASTRA_PLUGIN_TEST_TARGETS`. In `astra_finalize_plugins()` the shared branch replaces the
   hardcoded `foreach` with a loop over that property; the static branch does nothing with it (linkage already
   orders). The header comment documents the helper and why the dependency is added at finalize.
2. **Migrate every listed target** with one call beside its `add_executable`: `tests/unit/CMakeLists.txt`
   (`astra_unit_tests`, notional mass, cruise regression), `tests/physics/CMakeLists.txt` (physics, aero and
   turbofan wrench), `tests/capture/CMakeLists.txt` (`capture_cruise_regression`), and inside
   `astra_lockstep_test()` / `astra_closed_loop_test()`. The list goes away. The capture script's comment that
   cites "its explicit add_dependencies list" is updated.
3. **Top-level `CMakeLists.txt`**: inside `if(ASTRA_BUILD_TESTS)`, before `astra_discover_plugins(plugins)`:
   `enable_testing()`, `find_package(GTest 1.14 REQUIRED CONFIG)`, `include(GoogleTest)`, with a comment on why
   they precede discovery; the two redundant lines leave `tests/CMakeLists.txt`.
4. **`aircraft/_template/tests/`**: the template's own test target, one executable built from the three
   `template_*_test.cpp` files (`git mv`'d as `mass_test.cpp`, `aero_test.cpp`, `turbofan_test.cpp`). Its
   `CMakeLists.txt` derives every name from its own directory path (`astra_<path with / as _>`, ctest prefix
   likewise), defines `ASTRA_THIS_AIRCRAFT` as the aircraft's directory relative to the repo root, and ends
   with `astra_add_plugin_test(<target>)`. Sources load `ASTRA_THIS_AIRCRAFT "/physical/mass"` instead of the
   literal path; suites and comments say "this aircraft" since the file is copied. Test bodies and
   expectations are unchanged. A `.clang-tidy` identical to `tests/.clang-tidy` sits beside it.
   `new-aircraft.sh` needs no change.
5. **Acceptance test** `tests/python/test_new_aircraft_build.py` (see "Approved adjustments" for its
   constraints): copy the repo's source files into `tmp_path`, run `scripts/new-aircraft.sh gliderx` there,
   configure a fresh build directory with the `debug` preset, build only the copy's test target, and run its
   tests. Two cases: shared plugins, and `-DASTRA_STATIC_PLUGINS=ON`.
6. **Docs**: architecture §18 step 3 gains one sentence (the copy's `tests/` builds and runs under `ctest`
   through `astra_add_plugin_test()`; no framework file is edited). PLAN.md: remove the open item; add
   "PR 11 (#NN)" under M5 after PR 10, in its format.

## Approved adjustments (Joe, 2026-09-28)

1. `test_new_aircraft_build.py` uses no git inside the throwaway tree. "Only `aircraft/gliderx/` was added"
   is checked by comparing a file list taken before and after the copy step. Every subprocess gets an
   environment with `GIT_*` removed, as `test_tidy_fresh_configure.py` does, so a hook-set `GIT_DIR` or
   `GIT_INDEX_FILE` cannot point git at the real repo.
2. The static case is `debug` plus `ASTRA_STATIC_PLUGINS=ON`, not the `static-plugins` preset (which is asan).
   The PR body says it shows that two aircraft link and run in one `astra_core`. It does not test lesson
   2026-09-20's ODR isolation, because the two copies are identical and the model classes sit in anonymous
   namespaces. `aero_math_namespace_isolation_test` still covers ODR isolation.
3. Mutations: apply each, confirm its anchor changed (grep the marker), then run the test in a separate
   command (lesson 2026-09-26). Record which check fires for M1 (only the shared case of the throwaway test)
   and that the static case and the in-tree gate still pass under M1, as expected.
4. The PR body puts the added pytest time next to the before and after `just check` time.

## Risks

- Tests under `aircraft/` fall into the wall-clock scan. Kept in scope (stricter). If a GoogleTest-inlined
  symbol trips it, stop and bring it to Joe rather than narrow the scan.
- Gate time: two fresh configures and targeted builds added to pytest; measured, not estimated.

## Verification

- `./scripts/check-fast.sh` while iterating; `ctest --preset <p> -R aircraft__template_tests` per preset.
- `.venv/bin/pytest tests/python/test_new_aircraft_build.py`, exit 0, both cases.
- Scratch mutations, never committed, each recorded in the PR body with the failing output:
  (M1) delete `astra_add_plugin_test(...)` from `aircraft/_template/tests/CMakeLists.txt`; expect only the
  shared throwaway case to fail. (M2) delete the finalize loop's `add_dependencies`; expect the same.
  (M3) call `astra_add_plugin_test(nonexistent)`; expect a configure `FATAL_ERROR`.
- Fresh configure: `rm -rf build`, then `./scripts/check-full.sh`, exit 0 and `check: OK`. The trimmed gate
  summary `docs/reviews/evidence/<head>-gate-summary.log` is committed; command, exit and counts go in the PR
  body.
- Codex review per Tier 2 via `/review-cycle`; verdicts and thread ids in the PR body; at most two correction
  rounds. PR body per `docs/templates/pr-description.md`.
