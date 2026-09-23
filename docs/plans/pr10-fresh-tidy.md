# M3 PR 10: `just check` passes `tidy` from a fresh configure

Spec: Joe's PR 10 instruction (2026-09-22, in session); PLAN.md follow-up "`just check` fails at its
own `tidy` step on a fresh checkout". Risk: Tier 2 (build/gate behavior). Plan critique: none.
Status: **Revision 2, approved by Joe 2026-09-22 with changes (below).** Base `origin/main` `7a83a75`, branch
`worktree-m3-pr10-fresh-tidy`. Codex: one general pass, plus fix verification if needed.

## Joe's decisions (2026-09-22)

- **Q1.** `tests/golden/dynamics/CMakeLists.txt` stays untouched.
- **Q2.** Acceptance (3) for R1: unarmed refusal plus `tests/golden/**` untouched. PLAN.md gets a
  follow-up: R1's harness source is stale, so a future re-capture needs a source fix under Joe's
  authorization, not just re-adding the subdirectory.
- **Fix.** Set `CMAKE_CXX_SCAN_FOR_MODULES OFF` once at top level, and keep the per-target property
  on `capture_cruise_regression`, with a comment pointing at the top-level setting. The regression
  asserts on the harness TU, not the global effect.
- **Evidence (1).** The revert case shows the exact sequence: fresh scratch configure, no build,
  tidy on the harness TU, exit ≠ 0. Don't reuse a built scratch directory.
- **Lesson.** Don't correct PR 9's lesson. Record that the PR 4a-era de-wiring of
  `tests/golden/dynamics` was the first encounter with the `.modmap` failure, and that working
  around it instead of understanding the mechanism is why PR 8 re-introduced it; this is also a
  packet finding. Joe wrote "PR 4b", but no PR 4b exists: the wiring (`a0ecf30`) and de-wiring
  (`65bed84`) are both PR 4a. The recorded reason for the de-wiring is stale source, and nothing on
  record mentions `.modmap`. A fresh configure at `a0ecf30` does reproduce the `.modmap` failure on
  R1's TU, so the lesson says the failure was first *reachable* there and went undiagnosed. It does
  not say it was *encountered*.

## Root cause (reproduced, `7a83a75`, scratch build directories)

All commands ran from the worktree with `cmake --preset asan -B <scratch>` (CMake 3.28.3, Ninja
1.11.1, clang-tidy 18). Logs: session scratchpad; the reproduction is repeated in the packet.

1. **Fresh configure, no build.** `compile_commands.json` has 113 entries. **All 113** carry a
   response-file argument `@<object>.modmap`, because CMake 3.28 turns on C++ module dependency
   scanning by default for C++20 and later (`CMAKE_CXX_STANDARD 23`, top-level `CMakeLists.txt:12`)
   under Ninja. The `.modmap` is written by the target's scan/collate build step, not by configure:
   after configure, `tests/capture/CMakeFiles/capture_cruise_regression.dir/` holds only
   `CXXDependInfo.json`. The harness entry:
   ```
   /usr/bin/clang++-18 -I…/tests/support -I…/core/include -I<build>/generated/include -I<src> -isystem /usr/include/eigen3
     -g -std=c++23 … -Werror -fsanitize=address,undefined …
     @tests/capture/CMakeFiles/capture_cruise_regression.dir/capture_cruise_regression.cpp.o.modmap
     -o tests/capture/CMakeFiles/capture_cruise_regression.dir/capture_cruise_regression.cpp.o
     -c …/tests/capture/capture_cruise_regression.cpp
   ```
   `run-clang-tidy-18 -clang-tidy-binary clang-tidy-18 -p <build> -quiet -j 4
   'tests/capture/capture_cruise_regression.cpp'` exits 1:
   `error: no such file or directory: '@tests/capture/CMakeFiles/capture_cruise_regression.dir/capture_cruise_regression.cpp.o.modmap' [clang-diagnostic-error]`.
   The same failure hits any TU on a bare configure (checked: `core/src/kernel/simulation.cpp`,
   `…/astra_core.dir/src/kernel/simulation.cpp.o.modmap`), so standalone `just tidy` on a bare
   configure fails on every entry.
2. **After the default build** (`cmake --build <build>`, what `just check`'s `(test "asan")` does
   before `tidy`): 112 of 113 `.modmap` files exist. The one missing is
   **`tests/capture/CMakeFiles/capture_cruise_regression.dir/capture_cruise_regression.cpp.o.modmap`**,
   because the target is `EXCLUDE_FROM_ALL`. Tidy on the harness TU still exits 1 with the error above.
3. **`capture_r1_golden` is not in the compile database at all** — the actual difference. `tests/
   CMakeLists.txt` has deliberately not called `add_subdirectory(golden/dynamics)` since PR 4a's own
   merge (`96c9459`; its comment says why). Its source also no longer compiles against `core/`: it
   includes `astra/kernel/eom.hpp` and uses `astra::Eom`/`post_step_renormalize`, all retired by
   ADR-0021. It passed PRs 5–8's gates because tidy never saw it, not because its scan artifact
   existed. Its `.modmap` exposure is identical to the cruise harness's; it's just never configured.

So R1 has **not** been broken on fresh checkouts since PR 4a: it was never a live target on `main`.
PR 9's lesson (`docs/lessons.md` line 75) names the right artifact and mechanism and needs no
correction. It does not say that every entry carries a `.modmap` and only `(test "asan")`'s build
supplies them; the proposed lesson below records that.

**Fix (1) applies.** No source in the tree uses C++ modules (no `module`/`import` declarations).
Configuring with `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` drops the `@….modmap` argument from all 113
entries, and tidy on the harness TU from a bare configure (no build) exits 0 in 8.7 s.

Setting `CMAKE_CXX_SCAN_FOR_MODULES OFF` once at top level makes every entry, and standalone
`just tidy`, independent of build state, and covers any future `EXCLUDE_FROM_ALL` target (adopted,
Revision 2).

## Change

- `CMakeLists.txt`: `set(CMAKE_CXX_SCAN_FOR_MODULES OFF)` after the C++ standard settings, with its
  reason.
- `tests/capture/CMakeLists.txt`: `set_target_properties(capture_cruise_regression PROPERTIES
  CXX_SCAN_FOR_MODULES OFF)`, with a comment pointing at the top-level setting.
- `tests/golden/dynamics/CMakeLists.txt`: untouched (Q1).
- `tests/python/test_tidy_fresh_configure.py` (new; shape of `test_capture_cruise_regression_guard.py`):
  `cmake --preset asan -B <tmp_path>/asan` from the repo root, with the environment scrubbed the same
  way as that test's `_env()`; reads the `tidy` recipe's first `run-clang-tidy-18` line out of `justfile`
  (so the test follows the recipe), swaps `-p build/asan` → `-p <tmp_path>/asan` and `{{jobs}}` for a
  number, and appends a file regex anchored to the harness TU's absolute path. It asserts first that
  the harness TU is in the database, because `run-clang-tidy-18` exits 0 silently when nothing
  matches (checked). Then it asserts exit 0, including the tidy output in the failure message. It
  tests the harness TU only, not the top-level setting's effect on other entries (Joe's decision).
  Measured: 8.0 s total including configure. No pytest slow marker exists (`pyproject.toml` defines
  none), and 8 s doesn't need one, so it runs in the gate unmarked.
- `docs/lessons.md` (append): the mechanism, and the PR 4a history as the decision above words it.
- `PLAN.md`: check off the follow-up, add the R1 stale-harness follow-up (Q2), and add a PR 10 line;
  `STATE.md`; review/evidence records.

## Acceptance and validation

1. The regression passes on the candidate. It also fails with the fix reverted: a scratch detached
   worktree at `7a83a75` plus only the new test file gives the `.modmap` error, exit ≠ 0.
2. `rm -rf build` in this worktree, then `./scripts/check-full.sh` without a manual target build →
   `check: OK`, retained at `docs/reviews/evidence/<sha>-full-gate.log`.
3. Cruise harness: build `capture_cruise_regression` into a scratch build directory, run
   `trajectory <src>/aircraft <build>/plugins > scratch` and `meta` (exit 0). Run
   `capture_cruise_regression.sh` unarmed (`ASTRA_TOUCH_GOLDENS` unset, no marker) and confirm it
   refuses; the existing guard tests stay green. `git status --porcelain tests/golden` stays empty
   throughout. R1: `capture_r1_golden.sh` refuses when run unarmed from a clean committed tree. It has no
   arming check of its own; it refuses at its baseline-drift step, before its first commit.
4. Full gate green (item 2's run); Codex general pass on `7a83a75..HEAD`.

Rollback: revert the one property line (the test then fails, which is the point of it).

## Done when

Acceptance 1–4 hold with evidence; Codex findings resolved or recorded; Joe approves.
