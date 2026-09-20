# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M3 PR 2 (`PLAN.md` line ~557): write-seam primitive. **Implemented, validated, reviewed; PR open,
pending Joe's decision.** `InputRegistry`/`InputHandle`, `WrenchSource::bind_inputs()`
(`kCoreApiVersion` 4→5), `Vehicle::inputs()`/`reset()`, `Simulation::from_config`'s config-literal
apply/validate/freeze sequencing, `Simulation::step`'s `latch()` between `vehicle.step()`/
`vehicle.integrate()`, `HealthMonitor::check_input`, `tests/plugins/input_consumer`, full
acceptance table R1–R13. `docs/architecture.md` §5/§7/§9/§13/§15 updated. Worktree
`m3-pr2-write-seam`, branch `worktree-m3-pr2-write-seam`, based on `main`'s `829df62` (PR 1/#37
merged). Committed `de4815e`+`418167c`, pushed, **PR
<https://github.com/jhawleyjr/ASTRA/pull/38> (draft)**. `main` has not advanced. This handoff's own
`STATE.md`/`PLAN.md`/`docs/lessons.md` edits are uncommitted pending a separate handoff commit.

## Last verified result
`./scripts/check-full.sh` green on `418167c`: 4 presets (327 tests each), clang-tidy 0 warnings,
pyright 0 errors, pytest 38/38, `check: OK`. First run caught a stale pinned Python constant
(`core_api_version() == 4` → `== 5`, `tests/python/test_package.py`), fixed and reverified.
`./scripts/check-fast.sh` green (306/306) both before and after the Codex fix. Targeted: `ctest -R
NoAlloc` (5/5, R6), `-R Plugin` (19/19, R12, `bad_version` still rejects version 5). Codex (general
+ adversarial, `codex-companion` 1.0.6, working-tree scope): one major finding
(`apply_initial_controls` depended on `Config`'s shared read-state via `unknown_keys()`, breaking a
second `Simulation` from the same `Config`), fixed with new `InputRegistry::names()`, verified
same-pass (verdict "approve") — `docs/reviews/pr2-write-seam-review.md`; packet: `docs/reviews/
pr2-write-seam-human.md`.

## Next step
Joe's review/merge decision on PR #38; `PLAN.md`'s PR 2 checkbox stays unchecked until then. Once
merged: PR 3 (`AeroModel`/`PropulsionModel` as `WrenchSource` specializations) is next, and can
finally use the write seam this PR built. Not blocking: ADR-0017's own disclosed M4-scope gap (no
seam route for a non-`WrenchSource` writer); the still-unassigned `PLAN.md` follow-up restoring
"no recon subagents" visibility (from the M3 PR 2 planning session, not yet filed as its own PR).

## Tried / failed
Correction rounds: 1/2 used (Codex's one adversarial-review finding, fixed and Codex-verified in
one round). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
