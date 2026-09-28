# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 6 (Tier 2): PR #68, branch `worktree-m5-pr6-scenario` off `origin/main` `0cd3f78`, plan
`docs/plans/m5-tooling.md` PR 6 row (Q14 to Q16, R11 to R13), binding ADR-0025. Stage: human review.
The PR body is the description Joe reviews. Code candidate `553077d`; later commits add only its gate
summary and this handoff (STATE, two lessons). Scope: `python/astra/scenario.py`,
`tests/scenarios/conftest.py` plus five seed scenarios (bands committed in `5542900`, before any run),
`cruise_still_air.yaml`, `tests/python/test_scenario.py` and `scenario_fixtures/`, `testpaths`, pyright
include, a validation.md row, and PLAN.md's PR 6 line. No `core/` change, no new dependency.

## Last verified result
This session, at `553077d`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/553077d-gate-summary.log`, which also records the acceptance command
`.venv/bin/pytest tests/scenarios tests/python -k scenario`, exit 0, 54 passed). Codex general review
of `0cd3f78..04e6cd1` found R-001 (minor), fixed in `553077d`. The Codex fix verification approved
with "New findings: none". Thread ids and the round count (1 of 2) are in the PR body. All five seed
scenarios pass, with measured values in the PR body; the halt came at the predicted frame 512.

## Next step
1. Joe: decide on PR #68. Its "Needs Joe's decision" section covers:
   - approval of `553077d`;
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
2026-09-28). PyYAML reads `1.0e3` as a string (lessons 2026-09-28). `gh pr edit` still fails on
`projectCards`; `gh api -X PATCH .../pulls/68 -F body=@file` works (lessons 2026-09-19). The worktree
guard refuses compound shell and heredoc-to-bash commands; Write-tool scripts run by path work.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
