# Human review packet: M3 PR 10, fresh-checkout `tidy` fix

PR / candidate: base `origin/main` `7a83a75` (still `main`'s tip at packet time); code candidate
`f3c1460`, head `1fcd846` plus this packet's commit (evidence and docs only). Branch
`worktree-m3-pr10-fresh-tidy`.
Spec/plan: [`docs/plans/pr10-fresh-tidy.md`](../plans/pr10-fresh-tidy.md), Revision 2 (approved with
changes, 2026-09-22).
Risk: Tier 2 (build/gate behavior).
Decision requested: approve the candidate for PR and merge.

## What changes and why

Before: on a fresh checkout, `just check` failed at `tidy` with `no such file or directory:
'@tests/capture/CMakeFiles/capture_cruise_regression.dir/capture_cruise_regression.cpp.o.modmap'`.
CMake 3.28 scans every C++23 TU for modules under Ninja, so every compile-database entry referenced
a `.modmap` that only a build writes. `(test "asan")` built all of them except the
`EXCLUDE_FROM_ALL` harness's. After: no entry references a `.modmap`, so tidy is independent of
build state, and the gate passes from `rm -rf build` with no manual target build.

- `CMakeLists.txt`: `set(CMAKE_CXX_SCAN_FOR_MODULES OFF)`. There are no C++ modules in the tree.
- `tests/capture/CMakeLists.txt`: `CXX_SCAN_FOR_MODULES OFF` on `capture_cruise_regression`, with a
  comment pointing at the top-level setting.
- `tests/python/test_tidy_fresh_configure.py`: the regression.
- Docs: `PLAN.md` (follow-up resolved, new R1 follow-up, PR 9 and PR 10 lines), `docs/lessons.md`
  (one append), `STATE.md`, the plan, review records and evidence.
- `tests/golden/**` is untouched (Q1).

## Evidence

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| (1) Regression on the candidate | passed, 8.0 s including configure; also inside the gate's pytest (115 passed) | worktree at `f3c1460`; [gate log](evidence/f3c1460-full-gate.log) |
| (1) Revert case: `7a83a75` plus only the test file | new scratch directory (confirmed absent first) → `cmake --preset asan -B` → no build (harness dir holds only `CXXDependInfo.json`) → `run-clang-tidy-18` on the harness TU: `.modmap` error, **exit 1**. The pytest itself: 1 failed, **exit 1** | [evidence/pr10-acceptance1-revert-7a83a75.log](evidence/pr10-acceptance1-revert-7a83a75.log) |
| (2) + (4) `rm -rf build`, `./scripts/check-full.sh` | `check: OK`, exit 0, 12 m 44 s. 389/389 on each of four presets, tidy clean (`build/asan` and `build/python`), pyright 0/0/0, pytest 115 passed. No manual target build: afterward the harness's object directory is empty, and the database has 0 `.modmap` references | [evidence/f3c1460-full-gate.log](evidence/f3c1460-full-gate.log). Dirty tree during the run: only the acceptance-3 log's append |
| (3) Cruise harness | built into a scratch directory; `trajectory` (305 lines) and `meta` exit 0, output to scratch; script unarmed → refusal, "Nothing was built", exit 1 | [evidence/pr10-acceptance3-harness.log](evidence/pr10-acceptance3-harness.log) |
| (3) R1 script | from a clean tree at `f3c1460`: refuses at its baseline-drift step (`26ad594`), exit 1, HEAD unchanged | same log |
| (3) `tests/golden/**` | `git status --porcelain --untracked-files=all tests/golden` empty before and after | same log |

Toolchain: CMake 3.28.3, Ninja 1.11.1, clang/clang-tidy 18.

## Independent review

Codex general pass on `7a83a75..1fcd846`: no actionable defects
([review](pr10-fresh-tidy-review.md)). No fix rounds were needed (0/2); there was no adversarial pass,
by your instruction.

## Findings for Joe

1. **The actual difference between the two harnesses.** `capture_r1_golden` isn't in the compile
   database: `tests/CMakeLists.txt` hasn't added `golden/dynamics` since PR 4a merged (`96c9459`).
   It passed PRs 5–8 because tidy never saw it. Its exposure to the `.modmap` failure is the same as
   the cruise harness's. R1 was never broken on fresh checkouts of `main`, so PR 9's lesson needed
   no correction.
2. **The PR 4a history (your wording corrected).** You wrote "PR 4b"; there is no PR 4b. PR 4a wired
   R1 in `a0ecf30` and de-wired it in `65bed84`. From a fresh configure at `a0ecf30`, tidy fails on
   R1's TU with exactly this `.modmap` error (reproduced this session). So the failure was reachable
   in PR 4a, and branch gates passed only on build directories the capture script had already built.
   But the recorded reason for the de-wiring is stale source (`Eom` retired), and nothing in the
   repository mentions `.modmap` before PR 9. The lesson therefore says the failure was first
   *reachable* in PR 4a and went undiagnosed; it doesn't say it was *encountered*. If you know it was
   actually seen then, that's a one-line lesson edit before merge.
3. **The R1 capture script has no arming check of its own**, unlike PR 8's. Today it refuses at its
   clean-tree and baseline-drift steps, before its first commit. This is recorded in the new
   PLAN.md follow-up with the stale-source item, not fixed here.

## Joe's attention

- The top-level `CMAKE_CXX_SCAN_FOR_MODULES OFF` affects every target in every preset, and the
  scikit-build Python build too. If C++ modules are ever adopted, that line must go; its comment
  says why it exists.
- The regression reads the `tidy` recipe's first `run-clang-tidy-18` line out of `justfile`. If that
  recipe changes shape (for example, `-p` no longer `build/asan`), the test fails loudly rather than
  passing vacuously.
- Rollback: revert the two CMake lines. The regression then fails, which is the point of it.

## Decision record

State: approved
Human decision/date/reference: Joe, 2026-09-22, in session: "approve the candidate at head 936c2a8
(code candidate f3c1460)". The top-level `CMAKE_CXX_SCAN_FOR_MODULES OFF` setting was Joe's explicit
approval (plan Revision 2; it was proposed as the better approach, not in his original instruction).
No lesson edit for finding 2: the "reachable, not encountered" wording stands.
Approved candidate/scope: head `936c2a8`, code candidate `f3c1460`, base `7a83a75`; later commits
record this decision only
Merge observed: not merged
Release authorization: not requested

Accepted risks: none.
Nonblocking follow-ups: the R1 stale-harness and arming-check item (PLAN.md).
Architectural decisions: none.
