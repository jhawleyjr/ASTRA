# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 7 (Tier 2): PR #69 (draft), branch `worktree-m5-pr7-montecarlo` off `origin/main` `6227a17`, plan
`docs/plans/m5-tooling.md` PR 7 row (Q17 to Q20, R14 to R17, R15b) plus Joe's four approval amendments, binding
ADR-0025. Stage: human review; the PR body is the description Joe reviews. Code candidate `b41bdcb`
(correction round 1); later commits add only its gate summary and this handoff. Scope: private
`python/astra/_runkit.py` (helpers moved out of `scenario.py`, behavior unchanged), `python/astra/montecarlo.py`,
`tests/python/test_montecarlo.py`, `tests/python/montecarlo_fixtures/cruise_64.yaml` (Q19), pyarrow 25.0.1
pinned, a validation.md row, PLAN.md's PR 7 line, `docs/reviews/m5-pr7-montecarlo-review.md`. No `core/` change.

## Last verified result
This session, at `b41bdcb`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/b41bdcb-gate-summary.log`; pytest 333 passed in 43.18 s, from 275 in 37.59 s at PR 6).
Acceptance `.venv/bin/pytest tests/python -k montecarlo` exit 0, 58 passed. Q19 measured: about 2.0 s wall
on 32 cores, 2.106 GB of logs (peak disk the same), removed by the fixture on pass or fail. 14 scratch
mutations, all killed. Codex: general and adversarial reviews found R-001 to R-004 (all major, all fixed);
the verification pass on `b41bdcb` approved all four with "New findings: none". Thread ids are in the PR body.
Correction rounds 1 of 2.

## Next step
1. Joe: decide on PR #69. Its "Needs Joe's decision" section covers approval of `b41bdcb`, the Q19 duration
   (keep 60 s) and dispersions (mass without inertia disclosed), the schema readings (clip ≥ 1σ with redraw),
   the error-row classification, the extra summary columns, and `astra._runkit`. A new defect after round 1
   uses round 2 of 2. Before merge, rerun CI's `check` with `gh pr ready --undo`, then `gh pr ready`.
2. M5 PR 8 (Tier 2): `astra.fgbridge`; needs Joe's FlightGear `net_fdm.hxx` (version 24) in `docs/refs/`
   (Q21) and his guard arming for the transcribed table.
3. M5 PR 9: closeout (check-off, open items for the CLI, recorder retirement, wind events, closed loop).

## Tried / failed
Correction and integration counts: PR #69's body holds them. My R15 error batch ran without trim, so the
trim copy's random path in error messages (R-001) went unseen until review. A killed worker's duplex pipe
can raise `ConnectionResetError` rather than EOF (a 3-in-15 flake before the fix). A test that skips
`close_log()` cannot tell a strict load from a lenient one (the writer holds the file), so mutation M2
survived until a `log.load` spy test (lessons 2026-09-28). The worktree guard refused a multi-line `sed`
and a heredoc; use Edit/Write and scripts run by path. `gh api -X PATCH .../pulls/69 -F body=@file` for the body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
