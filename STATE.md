# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 6 (Tier 2): PR #68, branch `worktree-m5-pr6-scenario` off `origin/main` `0cd3f78`, plan
`docs/plans/m5-tooling.md` PR 6 row (Q14 to Q16, R11 to R13), binding ADR-0025. Stage: human review.
The PR body is the description Joe reviews. Code candidate `63777fc` (correction round 2, J-1); later
commits add only its gate summary and this handoff. Scope: `python/astra/scenario.py`,
`tests/scenarios/conftest.py` plus five seed scenarios (bands committed in `5542900`, before any run),
`cruise_still_air.yaml`, `tests/python/test_scenario.py` and `scenario_fixtures/`, `testpaths`, pyright
include, a validation.md row, and PLAN.md's PR 6 line. No `core/` change, no new dependency.

## Last verified result
This session, at `63777fc`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/63777fc-gate-summary.log`; acceptance `.venv/bin/pytest tests/scenarios
tests/python -k scenario`, exit 0, 64 passed). Findings (`docs/reviews/m5-pr6-scenario-review.md`):
- R-001 (Codex, minor): fixed and verified.
- J-1 (Joe, major; trim-owned overrides): fixed. Codex verified it with "New findings: none".
Thread ids are in the PR body. Correction rounds **2 of 2, used up**.

## Next step
1. Joe: decide on PR #68. Its "Needs Joe's decision" section covers:
   - approval of `63777fc`; a new defect is an escalation to Joe, not a third round;
   - the Q15 bands (table with measured values);
   - the Q14 schema readings (`expect_halt.frame`, half-open windows, trim keys, list-index overrides);
   - the change from the plan's "expected counter is exactly 1" rule.
   After any later push, rerun CI's `check` with `gh pr ready --undo`, then `gh pr ready`.
2. M5 PR 7 (Tier 2): `astra.montecarlo`, the sampler, the summary parquet, pyarrow pinned (Q2), and the
   64-run case under `just check` (Q17 to Q20). PR 7 needs the same consumer assertion this PR has, and
   can reuse `astra.scenario.verify`'s frames_run and halt logic.
3. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #68's body holds them. The plan's expected-halt counter rule
("exactly 1") was wrong: ADR-0020's per-stage check counts each out-of-range RK4 stage (lessons
2026-09-28). J-1: trim silently overwrote overrides it owns; none of my tests combined the two (lessons
2026-09-28). PyYAML reads `1.0e3` as a string. `gh pr edit` fails on `projectCards`; use `gh api -X
PATCH .../pulls/68 -F body=@file`. The worktree guard refuses compound commands; run scripts by path.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
