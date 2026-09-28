# Review: M5 PR 6, astra.scenario (the scenario harness)

Spec/plan: [m5-tooling.md](../plans/m5-tooling.md) (PR 6 row, Q14 to Q16, R11 to R13), ADR-0025
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6, `review --base origin/main`, fresh context; general review, Tier 2),
then Codex fix verification (`adversarial-review`) per round. J-1 is Joe's (human review of `553077d`).
Scope: `0cd3f78..04e6cd1` (PR #68), committed branch diff, clean worktree; fix 1 `04e6cd1..553077d`; fix 2
`4cc8e44..` this commit
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` at `6a827b8` and at `553077d`
([summary](evidence/553077d-gate-summary.log)); the gate after fix 2 is in `evidence/` and the PR body;
acceptance `.venv/bin/pytest tests/scenarios tests/python -k scenario` exit 0
Round: fix 2 (the last autonomous correction round, AGENTS.md)

## Finding R-001

Severity: minor (Codex P2)
Status: verified
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
Verification: Codex fix verification (`adversarial-review --base origin/main`, focus R-001 and the fix delta
`04e6cd1..553077d`), thread `01a0e5f2-bb97-7d31-ae0e-c3855dbdd253`. Verdict approve: "R-001: fixed in head
553077d ... Registry divisors replace inferred spacing correctly; final-row behavior remains consistent with
ADR-0025. New findings: none." The check was static; Codex was asked not to build or test. Evidence for the
fix: `./scripts/check-full.sh` exit 0 at `553077d` (`evidence/553077d-gate-summary.log`).
History: fix 1, 2026-09-28, opened by Codex general review; fixed-unverified; verified by Codex the same day.
Human exception if needed: none.

## Finding J-1

Severity: major (Joe, human review of `553077d`)
Status: fixed-unverified
Location: `python/astra/scenario.py` `load` and `_apply_trim` at `553077d`
Requirement/ADR: plan contract, `overrides` (existing numeric leaves) and `trim`; "a sim that runs from a
broken setup is worse than one that refuses" (AGENTS.md)
Failure: with `trim` set, an override of a leaf that `_apply_trim` replaces was accepted at collection and
then silently discarded. Joe's reproduction: `euler_nb.0 = pi/2` and `throttle = 0.9` with trim, and the run
started at psi = 0 and throttle 0.4986. An aileron or rudder override was kept, but `astra.trim` solves with
both at 0, so that run started untrimmed.
Requested outcome: with `trim` set, `load` refuses every override of a leaf that trim sets or assumes, naming
the key. The set is derived from `trim.py` and `_apply_trim` and stated in the module docstring. Leaves trim
reads but does not replace stay allowed.
Builder response: fixed.
- `TRIM_OWNED` lists the override-key prefixes. A key matches a prefix when it equals it or continues it
  with `.`, meaning an element of a list.
- Trim sets:
  - `initial.state.p_n.2`, from `trim.altitude`;
  - `initial.state.v_b.*`;
  - `initial.state.euler_nb.*`, as `[0, alpha, 0]`;
  - `initial.state.q_nb.*`, which it replaces;
  - `initial.state.omega_b.*`, as zero;
  - `vehicle.initial_controls.controls.elevator` and `throttle`, which it solves for.
- Trim assumes `controls.aileron` and `controls.rudder` are 0.
- `load` checks each override against `TRIM_OWNED` when `trim` is set, and raises a `ScenarioError` naming
  `overrides.<key>` and the reason.
- Still allowed: `p_n.0` and `p_n.1` (read and preserved), mass, and aero and propulsion coefficients.
  `physical.wind` also stays allowed, by Joe's decision (2026-09-28): trim is still-air by design, and wind is
  a disturbance from trim. The docstring discloses that the Dryden frame-0 gust starts such a run off-trim.
- Tests in `tests/python/test_scenario.py`:
  - `test_with_trim_an_override_of_a_leaf_trim_sets_or_assumes_is_a_collection_error`, cases (a)
    `euler_nb.0`, (b) `throttle`, (c) `v_b.0` and (d) `aileron`, plus `rudder`, `elevator`, `p_n.2`,
    `euler_nb.1` and `omega_b.1`. Each also shows that the same override without trim still collects.
  - `test_with_trim_an_override_of_a_leaf_trim_only_reads_collects_and_reaches_the_trim`, case (e): a mass
    override collects, runs clean, and trims at more throttle.
- Against the `553077d` harness, all nine refusal cases failed with "DID NOT RAISE ScenarioError". Case (e)
  passed there too, by design: it guards against an overbroad fix.
Verification: pending (Codex fix verification of this batch).
History: fix 2, 2026-09-28, opened by Joe; fixed-unverified.
Human exception if needed: none.

## Completion

Two findings. R-001 (minor, Codex) was fixed in round 1 and verified. J-1 (major, Joe) was fixed in round 2,
the last autonomous round, and is pending Codex verification. A further defect goes to Joe as an escalation,
not a third round. Reviewer recommendation is not Joe's approval.
