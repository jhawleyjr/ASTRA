# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing. M1 PR 1 (PR #15) merged: `core/util/frames.hpp` (quaternion ↔ DCM ↔ 3-2-1 Euler, rotation,
kinematics, ADR-0001), `core/kernel/integrator.hpp` (Euler/RK4 over `StateVector`, ADR-0003/0004),
`scripts/tokens.py`. Also: plan review for unpinned designs (EOM, kinematics, integrator interface,
health monitor) is now automatic — Claude runs one `/codex:rescue` round inside plan mode before the
plan is shown, not triggered by Joe — and `/wrap`/`/codex:review`/`/codex:adversarial-review` are
model-invocable, so the session ends its own session and runs its own reviews.

## Last verified result
- `just check` exited 0 (`check: OK`) on the merged state; `ctest --preset asan -R 'frames|integrator'`
  passed 18/18: every ADR-0001 golden case within `kTolerance` (1e-12); RK4 error ratio 16±0.5 and
  Euler 2±0.1 on a linear oscillator when dt halves; `|q|` within 1e-12 after post-step; a non-finite
  `dt` or unknown `IntegratorKind` is rejected at construction; a zero/non-finite quaternion leaves
  `post_step_renormalize` returning `false` without corrupting state.
- `/codex:review`/`/codex:adversarial-review` findings (non-finite `dt`; unconditional `normalize()`)
  are fixed in the merged code, not just noted. `AGENTS.md` + `CLAUDE.md` = 100 lines exactly.
- `tests/golden/frames/adr0001_cases.hpp`'s `C_nb` member needed one
  `NOLINTNEXTLINE(readability-identifier-naming)` (Joe armed `touch-goldens`; no values changed) —
  nothing had included this human-owned header in a compiled TU before this PR, so clang-tidy had
  never actually analyzed it until now.

## Next step
M1 PR 2 (PLAN.md): `Config` (yaml-cpp; typed getters, missing/unknown key errors name the path),
`SignalRegistry` (register at init, pointers on the hot path, `v0.` namespace), `VirtualClock`,
`Scheduler`, `Vehicle`, `Simulation` as a list of one that loads plugins named by path in the config.
Accept: a config naming the dummy plugin runs 1000 frames with `t == frame * dt` exact and the
plugin's signal visible as `v0.dummy.*`. Adds ctest labels `unit`/`physics`; Stop hook runs
`ctest -L unit`. No design here is on the ADR-unpinned list, so the automatic `/codex:rescue`
plan-review round is skipped.
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m1-config --permission-mode plan`

## Tried / failed
- Nothing unresolved this session. (Golden-header clang-tidy stop-and-ask and two Codex adversarial-
  review findings were all fixed; see `docs/lessons.md`.)

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
