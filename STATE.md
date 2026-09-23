# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 9** (M3 closeout + ICD control conventions). Tier 1. Branch
`worktree-m3-pr9-icd-conventions`, base `origin/main` `f4d69a7`. No plan (Tier 1, single commit);
no `/plan-feature`. Comment-only in `icd/`: no layout, version, or static-assert change.

One commit, scope: `PLAN.md` (check off PR 8 and the M3 milestone line, citing PRs 6/7/8; check off
the positive-deflection-sense item as resolved here; narrow the deflection-range item to "actuator
model owns it, M4+"; add a follow-up for the fresh-checkout `tidy` gap below, fix in M3 PR 10),
`icd/astra_icd.h` line 45 (states elevator/aileron/rudder positive sense directly, cross-
referencing `aircraft/notional_transport/physical/aero`, replacing the circular ADR-0001 pointer),
`STATE.md` (this rewrite), `docs/lessons.md` (the two PR 8 lessons, confirmed absent before
appending, plus a third on the `tidy` gap found this session).

## Last verified result
`./scripts/check-full.sh` pre-commit, working tree at `f4d69a7` plus the four edits: `check: OK`,
exit 0, 389/389 per preset (four), pyright 0/0/0, pytest 114 passed. `git diff --stat` against
`origin/main` touches only `PLAN.md`, `STATE.md`, `icd/astra_icd.h`, `docs/lessons.md`.

**`main` fails its own full gate from a fresh checkout, right now, since `f4d69a7` (#46) — found,
not fixed here (Tier 1; PLAN.md follow-up added, fix scheduled M3 PR 10, Tier 2).** A fresh
`build/asan` fails `just tidy` on `tests/capture/capture_cruise_regression` (`EXCLUDE_FROM_ALL`,
M3 PR 8): its dependency-scan artifact is generated only by an actual build of that target, which
nothing in `just check` (`tidy` runs before `pytest`) or the `tmp_path`-only guard tests ever does.
The evidence above was reached only after manually running `cmake --build build/asan --target
capture_cruise_regression` (no source change) first; PR 8's own retained evidence passed only
because its worktree's `build/asan` was already primed by a real, armed capture run, masking this.

## Next step
Diff approved by Joe, plus the lessons/PLAN.md additions above. Commit (one commit, all four
files), one Codex general pass (no adversarial, no plan critique), push, `gh pr create --draft`,
`gh pr ready`, report head SHA and check run. `git worktree prune` once removed.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2. First full-gate run failed at `tidy` on the
pre-existing gap above (not a regression); the target build then let it pass.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
