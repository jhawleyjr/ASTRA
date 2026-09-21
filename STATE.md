# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + M3 PR 3d
(framework realignment) are now on `origin/main`. `AeroModel`/`PropulsionModel` are minimal
marker interfaces; concrete plugins override `wrench_b()` directly. `PLAN.md`'s PR 3d checkbox
closed with the merge SHA.

New worktree for PR 4 confirmed clean at `origin/main` tip (`worktree-m3-pr4-notional-transport`,
this session) — not a reuse of the stale `worktree-m3-pr4-notional-aero-turbofan` branch, whose
origin ref is now deleted (pruned this session).

Rebuilding `docs/plans/pr4-notional-aero-turbofan.md` against the merged ADR-0020 interface via
`/plan-feature`, per Joe's instruction: both plugins override `wrench_b()` directly; `air_data::`
for α/β/q̄/Mach; density read from `env.atmosphere`; V→0 uses the division-free
`0.25*rho*V*S*L^2*C*rate` form; reuse the superseded plan's frame conversions/AERORP `r×F`/
dropped-terms table as-is; thrust reference sized for a half-throttle cruise trim; all numbers
notional and labeled so; `Cmadot` stays dropped until PR 4a; show the plugin-side `Inputs` struct
+ pure-helper pattern (unit-testable with no simulator) in both the plugin and `_template`; cite
R-001 in the finiteness-check comment per ADR-0020. Plan shown to Joe before any code.

## Last verified result
Full gate for PR 3d/#40: `check: OK`, exit 0 (prior session) — see
`docs/reviews/pr3d-adr0020-human.md`. No gate run this session (docs-only STATE/PLAN update, no
code touched).

## Next step
Finish the `/plan-feature` pass for `docs/plans/pr4-notional-aero-turbofan.md`, show Joe the plan,
get explicit approval before writing any PR 4 code. PR 4a (ADR-0021) remains independent, not
started.

## Tried / failed
Correction rounds: 0/2 used this session. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
