# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing. M0 is complete (PRs #1–#11: bootstrap, core skeleton, plugin loader, Python package, Stop hook, M0 retro
with the M1 plan). PR #12 merged the human-owned ADR-0001 oracle, `tests/golden/frames/adr0001_cases.hpp`, so the
M1 PR 1 worktree can be cut from `main` and see it.

## Last verified result
- `tests/golden/frames/adr0001_cases.hpp`: every entry of the eight cases recomputed independently from the closed
  forms in its header (numpy, Cowork), worst disagreement 2.2e-16 against a 1e-12 test tolerance; clang-format and
  clang-tidy clean with the repo settings; compiles under clang-18 `-std=c++23 -Wall -Wextra -Werror`.
- Last gate result: `just check` exited 0 (`check: OK`) on PR #12's CI (`check` is required on `main`).
- Not exercised: the Stop hook's 840 s budget branch in a live session (its hook case passes under a 1 s budget).

## Next step
M1 PR 1 (PLAN.md). Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m1-frames --permission-mode plan`
   M1 PR 1: `core/util/` frames — quaternion Hamilton scalar-first (w,x,y,z) ↔ DCM ↔ 3-2-1 Euler, vector
   rotation, `q_nb` kinematics (n→b, ADR-0001); `core/kernel/` `Integrator` over `StateVector` (ADR-0003/0004):
   Euler and RK4, `f(t, y) → ẏ` functor, post-step renormalize, no allocation in `step`. The frames test reads
   `tests/golden/frames/adr0001_cases.hpp` (human-owned; do not edit it, and do not loosen the tidy header filter
   to get past it — it is already NOLINT-scoped and clean). Accept: `just check` exits 0 and
   `ctest --preset asan -R 'frames|integrator'` passes with (a) every ADR-0001 case within `kTolerance`,
   (b) RK4 error ratio 16 ± 0.5 and Euler 2 ± 0.1 when dt halves on a linear ODE with a closed form,
   (c) |q| within 1e-12 after post-step. Also commits `scripts/tokens.py` (workflow.md §Model and effort).
   Build and test output to a file, tail only. Plan review: the integrator-interface plan gets one `/codex:rescue`
   round inside plan mode, automatically, before the plan is shown. Review: `/codex:review --base origin/main --wait`, then
   `/codex:adversarial-review --base origin/main` with ADR-0001/0003/0004 as focus. Then `/wrap`.
Follow-ups already in PLAN.md: PR 2 labels ctest `unit`/`physics` and points the Stop hook at `ctest -L unit`;
the Python `Simulation.derivative` binding lands with M3 trim.

## Tried / failed
- Nothing this session. (M0 entries retired to docs/lessons.md.)

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
