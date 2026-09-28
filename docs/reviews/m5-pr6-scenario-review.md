# Review: M5 PR 6, astra.scenario (the scenario harness)

Spec/plan: [m5-tooling.md](../plans/m5-tooling.md) (PR 6 row, Q14 to Q16, R11 to R13), ADR-0025
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6, `review --base origin/main`, fresh context; general review, Tier 2)
Scope: `0cd3f78..04e6cd1` (PR #68), committed branch diff, clean worktree
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` at `6a827b8`
([summary](evidence/6a827b8-gate-summary.log)); acceptance `.venv/bin/pytest tests/scenarios tests/python -k scenario`
exit 0, 52 passed
Round: fix 1

## Finding R-001

Severity: minor (Codex P2)
Status: fixed-unverified
Location: `python/astra/scenario.py:665-668` at `04e6cd1` (`_check_failure`)
Requirement/ADR: plan contract "Failure output names the scenario, check, signal, window, worst value and its
frame"; ADR-0025 rate groups (`/rate_<divisor>`)
Failure: for a signal in a divisor-10 rate group, the valid window `[1, 2)` frames selects no row, and the
inferred expected count is also 0. The completeness check passed, and `np.argmax` on the empty array raised
`ValueError` instead of a named failure. Codex reproduced it with a rate-10 table indexed `[0, 10]`. Not
reachable by any current scenario: every registered signal has divisor 1.
Requested outcome: reject empty sampled windows at validation, or report a descriptive failure.
Builder response: fixed, both ways.
- `Check` now carries the signal's registry divisor, which `load` reads from `registry.signals`. The harness
  no longer infers it from row spacing, which was also wrong for a one-row table.
- `_window` refuses at collection a window in which that rate group logs no frame.
- `_check_failure` computes the expected row count from `Check.divisor`, and reports a zero-row selection as
  a failure before any reduction.
- "final" for a slower rate group is documented as that group's last row.
- Regression tests in `tests/python/test_scenario.py`:
  - `test_a_window_with_no_row_of_its_signal_rate_is_a_collection_error`;
  - `test_verify_reports_a_rate_group_window_without_its_rows_instead_of_raising`, which covers the empty,
    short and final cases on a synthetic divisor-10 table.
Verification: pending (Codex fix verification of this batch).
History: fix 1, 2026-09-28, opened by Codex general review; fixed-unverified.
Human exception if needed: none.

## Completion

One finding, minor, fixed in round 1 and pending Codex verification. No critical or major findings. Reviewer
recommendation is not Joe's approval.
