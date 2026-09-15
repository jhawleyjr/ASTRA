# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
Branch `adr0001-oracle`, pushed, PR open: `tests/golden/frames/adr0001_cases.hpp`, the human-owned ADR-0001
sign-pinning values for M1 PR 1 (eight attitudes: q_nb, C_nb, Euler round-trip, axis images, qdot). Checked
independently in Cowork against the closed forms in its header (worst disagreement 2.2e-16); clang-format and
clang-tidy clean. Awaiting Joe: squash-merge before the M1 PR 1 worktree is cut, since a worktree branches from
remote `main`. M0 is complete (PRs #1–#11); the M0 retro and the M1 plan landed in #11.

## Last verified result
- Nothing ran this session (Cowork, planning only). The last gate result is the M0 one: `just check` exited 0
  (`check: OK`) on the tree that became PR #9 — 4 presets × 16 ctest cases, tidy, format, hooks 32/32, ruff,
  pyright strict, pytest 26 passed. CI `check` is required on `main`.
- Not exercised: the Stop hook's 840 s budget branch (exit 124) and the eight-block cap.

## Next step
1. Joe: merge `adr0001-oracle`.
2. Joe: `/codex:rescue` with the five-line plan for the integrator interface once the M1 PR 1 session has
   produced it in plan mode (workflow.md §Review, plan review); one round.
3. Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m1-frames --permission-mode plan`
   M1 PR 1 (PLAN.md): `core/util/` frames — quaternion Hamilton scalar-first (w,x,y,z) ↔ DCM ↔ 3-2-1 Euler,
   vector rotation, `q_nb` kinematics (n→b, ADR-0001); `core/kernel/` `Integrator` over `StateVector`
   (ADR-0003/0004): Euler and RK4, `f(t, y) → ẏ` functor, post-step renormalize, no allocation in `step`.
   Accept: `just check` exits 0 and `ctest --preset asan -R 'frames|integrator'` passes with (a) the ADR-0001
   sign-pinning test whose expected values I hand you before code, (b) RK4 error ratio 16 ± 0.5 and Euler
   2 ± 0.1 when dt halves on a linear ODE with a closed form, (c) |q| within 1e-12 after post-step.
   Build and test output to a file, tail only. Review: `/codex:review --base origin/main --wait`, then
   `/codex:adversarial-review --base origin/main` with ADR-0001/0003/0004 as focus. Then `/wrap`.
4. Follow-ups carried into M1: PR 2 labels ctest `unit`/`physics` and points the Stop hook at `ctest -L unit`;
   the Python `Simulation.derivative` binding (ADR-0003) lands with M3 trim, not M1.

## Tried / failed
- Nothing this session. (M0 entries retired to docs/lessons.md.)

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
