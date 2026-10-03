# Review: M6 PR 7, Monte Carlo sizing (`astra.mc_sizing`, `astra mc size`)

Spec/plan: [plan](../plans/m6-pr7-mc-sizing.md), revision 3a as approved by Joe on 2026-10-02 with his decisions
and the Bonett build checkpoint; [M6 plan](../plans/m6-analysis-tooling.md) and its amendment note.
Builder: Claude (Opus 5.5).
Reviewer: Codex (companion 1.0.6; model `gpt-6.1-sol` at effort `high` from `~/.codex/config.toml`; fresh
context). Two passes on the same candidate:
- general `review --base origin/main`, thread `01a10017-fec3-7f10-8ed6-8ed3c77187ae`;
- `adversarial-review --base origin/main` with a focus on the calibrated tier, the nominal tier, convergence,
  the oracles' independence, C-09, N-04 and the CLI split: job `review-murwwl1i-53bn9p`, thread
  `01a10018-0023-7e31-81a5-f292763d4844`.
Scope: the committed branch diff, base `7e65a85` (origin/main), head `e40714c`. The tree was clean (no staged,
unstaged or untracked files).
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` on `e3951fe`, the same code as `e40714c`, which
adds only the gate summary.
Round: initial review, then fix 1 (this commit).

The general pass gave five findings (G-001 to G-005), all reproduced by Codex. The adversarial pass's verdict
was "needs-attention", with four findings. Three of them duplicate G-001, G-002 and G-005 and are recorded
under those IDs; the fourth is A-004. Claude reproduced G-001, G-002, G-004 and G-005 in this session. G-003 is
Codex's reproduction, and its regression now covers it.

## Finding G-001

Severity: major (adversarial: high)
Status: fixed-unverified
Location: `python/astra/mc_sizing.py:832-836` at `e40714c`
Requirement/ADR: plan D2 and D4 (one convention, inputs as written; a value equal to the limit is not an exceedance)
Failure: the limit was taken as written (`Fraction(repr)`), but each metric value went through `Fraction(value)`,
the binary expansion. So 59 values of 0.1 against limit 0.1 counted 59 exceedances and gave `violated`
instead of `met`.
Requested outcome: compare both operands under the same convention, and add API and CLI regressions on both
sides.
Builder response: fixed. Each value is converted by `exact("value", v)` (shortest repr), like the limit.
Regression `test_mc_sizing_g001_decimal_ties_are_not_exceedances` covers 0.1 upper, 0.3 lower,
0.30000000000000004 as a real exceedance, and the CLI on a written summary.
Verification: pending.
History: round 1, open → fixed-unverified.

## Finding G-002

Severity: major (adversarial: medium)
Status: fixed-unverified
Location: `python/astra/mc_sizing.py:920-921` at `e40714c`
Requirement/ADR: plan D5 (std's nominal output, never a crash)
Failure: a constant metric of 100 runs divided by a zero sum of squares in `bonett_kurtosis`. The result was a
`ZeroDivisionError`, an exit of 70 from the CLI, and an aborted convergence on a constant prefix.
Requested outcome: detect zero variance, return no interval and no projection with a reason, and cover constant
batches and constant prefixes.
Builder response: fixed. `bonett_kurtosis` returns NaN for zero variance, so `std_interval` gives none.
`_precision_std` returns "no interval: the metric is constant" before computing it. Regression
`test_mc_sizing_g002_a_constant_metric_has_no_std_interval` covers the API, convergence (100 constant then 20
varying) and the CLI.
Verification: pending.
History: round 1, open → fixed-unverified.

## Finding G-003

Severity: major
Status: fixed-unverified
Location: `python/astra/mc_sizing.py:1554-1556` at `e40714c`
Requirement/ADR: plan D10 (convergence gives bounds and verdicts at every prefix)
Failure: each prefix ran the full check, producer-risk search included. With one halt then 99 passes and
`p_max = .3334`, the 3-run prefix's search ran to `MAX_RUNS` (about 42 s) and raised.
Requested outcome: compute prefix statistics without sizing searches.
Builder response: fixed. Convergence checks each prefix under a `ContextVar` that turns off every projection
(calibrated `needed`, nominal projections and ranking searches), so a prefix's `needed` is `None`; the docstring
says so. Regression `test_mc_sizing_g003_convergence_runs_no_sizing_search` checks that the full check still
gives `needed = 8`.
Verification: pending.
History: round 1, open → fixed-unverified.

## Finding G-004

Severity: minor
Status: fixed-unverified
Location: `python/astra/mc_sizing.py:549-551` at `e40714c`
Requirement/ADR: plan D9 (closed, validated claims)
Failure: `relative: "false"` in a claims file was accepted and was truthy, which turned an absolute tolerance
into a relative one.
Requested outcome: validate the field's type.
Builder response: fixed. `PrecisionClaim` refuses a non-bool `relative`. Every claim also checks that `metric`,
`criterion` and `parameter` are non-empty strings or absent. Regression `test_mc_sizing_g004_claim_fields_are_typed`
covers the API and a claims file.
Verification: pending.
History: round 1, open → fixed-unverified.

## Finding G-005

Severity: minor (adversarial: medium)
Status: fixed-unverified
Location: `python/astra/cli.py:989-991` at `e40714c`
Requirement/ADR: plan D9 (the formula/batch argument split; out-of-domain values exit 64)
Failure: `astra mc size ranking --r 0.3 --parameters 4 --confidence 0.99` returned 120 and ignored the
confidence. `--confidence 2` was also accepted.
Requested outcome: reject it in the formula mode, and point to `--alpha`.
Builder response: fixed. Ranking's formula mode refuses `--confidence` (64: "needs --batch; the formula mode
takes --alpha"). The usage-error parametrization adds 0.99 and 1.5.
Verification: pending.
History: round 1, open → fixed-unverified.

## Finding A-004

Severity: medium
Status: fixed-unverified
Location: `tests/python/test_mc_sizing.py:201-205` at `e40714c`
Requirement/ADR: plan Tests (oracles independent of the module); C-09
Failure: the Clopper-Pearson minimality check used the module's own `half_width_at`, and the
probability-projection endpoint took its expected N from the module's `precision_runs`. A shared endpoint error
would pass both.
Requested outcome: compute the real-count beta endpoints in the test and check N and N−1 against them, and
derive the projection's expected N independently.
Builder response: fixed. The test's own `_expected_half_width` computes the beta quantiles at `k = n p0`
directly. It checks N and N−1, the monotonicity, and agreement with `half_width_at` at four sizes. The
probability projection's expected N comes from a linear search over it.
Verification: pending.
History: round 1, open → fixed-unverified.

## Completion

Six findings in round 1, all fixed-unverified with regressions. None was rejected or deferred.

Validation after the fixes:
- `.venv/bin/pytest tests/python -k mc_sizing`: 73 passed.
- `-k 'mc_analysis or montecarlo or cli'`: 236 passed.
- ruff, ruff format and pyright are clean.
- The full gate is rerun on the fix commit.

Next is one focused verification pass on G-001 to G-005 and A-004. This is correction round 1 of 2. The
reviewer's recommendation is not Joe's approval.
