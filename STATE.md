# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
Nothing. M0 is complete: PRs #1–#10 merged, `main` at the M0 close. This Cowork session (2026-09-14) planned M1:
PLAN.md now lists M1 as four PRs; CLAUDE.md status line says M1. Both edits are uncommitted in the main checkout —
Joe commits them from his terminal before opening a worktree (a worktree cannot see them, docs/lessons.md).

## Last verified result
- Nothing ran this session (Cowork, planning only). The last gate result is the M0 one: `just check` exited 0
  (`check: OK`) on the tree that became PR #9 — 4 presets × 16 ctest cases, tidy, format, hooks 32/32, ruff,
  pyright strict, pytest 26 passed. CI `check` is required on `main`.
- Not exercised: the Stop hook's 840 s budget branch (exit 124) and the eight-block cap.

## Next step
1. Joe: retro for M0 (`/retro` in Claude Code, or the Cowork `astra-retro` skill once saved); commit PLAN.md,
   CLAUDE.md, STATE.md on `main`.
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
