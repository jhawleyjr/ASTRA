# M6 PR 7: Monte Carlo sizing options, their data modes, convergence and `astra mc size` (R9 follows as PR 7b)

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29, as amended by Joe on 2026-10-02 (the
"Requirements change" and "Joe's direction on the critique" below): "Sizing and convergence", R11, R12, R15's
`mc size` case, Q10, Q14, Q15, and "Obligations carried to PR plans" (PR 7 now closes C-09 and N-04, from PR #78's
per-finding notes); architecture §12's `astra.mc_sizing` and `astra.cli` bullets. R9, Q9 and **C-08's closure move
to PR 7b** (D11).
Risk: **Tier 2** (a Python feature and tests; no new dependency, since scipy 1.18.1 is pinned). Base: `origin/main`
`7e65a85`. Worktree/branch: `worktree-m6-pr7-mc-sizing`, draft PR #93. Touches no `core/`, `plugins/`, `aircraft/`,
`icd/`, `tests/golden/**`, tolerance file or ADR. Binding ADRs: 0025/0027 (batches read through
`astra.mc_analysis.load_batch`), 0002 (SI), 0010.
Status: **revision 3a, for Joe's approval.** The narrow Codex verification pass on revision 3 ran (rework: three
medium findings), and 3a fixes all three ("Verification pass", at the end).
- Revision 1 was approved by Joe with changes 1 to 7.
- At the R9 checkpoint he approved R9's numbers and changed the requirements, which became revision 2.
- The Codex critique of revision 2 found 13 issues. Joe accepted all 13 and gave the direction this revision follows.

Nothing here is approved until he says so.

## Context

The M6 plan's sizing answered four questions with closed formulas before a batch runs. Joe's change makes each one
an *option* named for the claim it answers: **probability**, **percentile**, **precision** and **ranking**. Each
option has two modes:
- without a batch, its formula gives the minimum N;
- with a batch, it reads the batch's own data.

The data modes come in two tiers:
- **Calibrated verdicts.** Probability, and percentile restated as an exceedance probability, rest on the exact
  binomial.
- **Nominal projections.** Precision and ranking give an approximate projection of N, never a verdict.

A pilot batch, with its own master seed, sizes N. The verdict comes only from a fresh final batch. R9, the planted
cause, follows as PR 7b.

## Requirements change (Joe, 2026-10-02, in substance)

1. Without a batch, the formula gives the minimum N (zero-failure, Wilks, Clopper-Pearson proportion, Fisher z). R11's
   oracles stay.
2. With `--batch DIR`, the option gives its verdict, or (as now decided) its projection, from the batch's own data,
   and the N that would decide it. The options are probability, percentile, precision and ranking. Several options
   can be checked on one batch, and the result is one table PR 8's report can show.
3. A pilot sizes N; only the final batch gives the verdict.
4. Convergence: each option's statistic, with its interval, against n.
5. Oracles: exact binomial and order-statistic values, empirical coverage with a binomial allowance, and producer-risk
   cases.
6. R9 uses the ranking option. Say whether R9 stays in this PR or follows: it follows (D11).

Also approved at the R9 checkpoint, for PR 7b:
- R9's numbers: [−0.6, +0.6], 10 s, `max.v0.derived.alpha > 0.23`, minimum effect unchanged;
- no inert-parameter BH assertion in the main batch;
- the corrected physics rationale and lesson.

## Joe's direction on the critique (2026-10-02, in substance)

Codex findings 1 to 13 are accepted. Joe directed:
- **The pilot only sizes N (findings 3, 4, 9, 12).**
  - An independent pilot, with its own master seed, only sizes N. The final batch is fresh, so its verdicts are
    exact, and D3's fresh-batch producer risk is the right one.
  - Final N = max(projection, option minimum).
  - Pilot logs are deleted after sizing, and the disk limit is recomputed.
- **Two tiers (findings 1, 2, 5 to 9).**
  - Calibrated verdicts:
    - probability, by the exact Clopper-Pearson interval;
    - percentile, restated as exceedance, P(metric > limit) ≤ 1 − γ;
    - each side's bound checked independently;
    - one non-strict convention everywhere, with exact rational boundary oracles.
  - Approximate projections, labeled nominal and not calibrated, and never a verdict:
    - precision, with the corrected `Var(s²) = σ⁴ [κ − (n−3)/(n−1)]/n` and stated estimator conventions;
    - a rare-tail case that shows the method's limit;
    - percentile N as a projection that depends on regularity;
    - ranking, with a jackknife Spearman variance instead of 1.06, and no interval at |ρ| = 1 or below a stated
      minimum n.
- **Coverage tests (findings 10, 11).** State the deficit to detect (for example, 0.92 against 0.95 at 95 % power),
  and derive K from it (about 1000). Endpoint oracles for every statistic, and N / N−1 minimality for every search.
- **R9 (finding 13).** C-08's closure and R9's acceptance move explicitly to PR 7b, and R9 stays in the gate.
- **Next:** one narrow Codex verification pass on the revised sections only.

## Repository findings (read at `7e65a85`)

- `python/astra/mc_sizing.py` is drafted in the worktree (not yet committed). It holds the formula mode, and every R11
  oracle is met:
  - 299;
  - Wilks: 59 and 93, orders 2 and 3 at 93 and 124, and two-sided order 2 at 153;
  - Fisher z: 85 and 120;
  - Clopper-Pearson against `binomtest`, within 6e-17;
  - the p0 = 0 closed form, 122;
  - Joe's change-4 example: 632, with the half-width at `round(n·p0)` exceeding 0.02 for n = 632 to 635.

  It is renamed and extended per D1 to D9.
- `astra.montecarlo.run_seed(master, index)` hashes only the master seed and the index (`montecarlo.py:260`). A pilot
  with another master seed is therefore statistically independent of the final batch, given the same spec otherwise.
- `astra.montecarlo.run` refuses a non-empty `out_dir` and runs every run, so it never appends to a pilot
  (`montecarlo.py:626`). The sizing data modes read only `summary.parquet`, never the run logs, so a pilot's logs can
  be deleted once it is sized.
- `astra.mc_analysis` (PR 6) gives `load_batch`, `outcomes`, `metric`, `parameter_values`, `analyzed_rows` and
  `CriterionError`. Ranked parameters exclude seed dispersions, and a constant parameter leaves the family.
- `astra.cli` has `_Parser` (exit 64), `_write`, `_rate`, `_finite`, and the `mc` group with `analyze` and `rerun`.
- Randomness: numpy is unpinned, so every resample and every synthetic draw uses `montecarlo.Rng`.
- Hanson & Beard is not under `docs/refs/`. D3 defines the producer risk in full, so its oracle needs no outside
  source.
- **R9 physics (carried to PR 7b).**
  - There is no divergence. The transfer `r_aero_b × F` (AERORP 0.36 m ahead of and 1.63 m above the CG) gives the
    fixed-elevator `Cm(α)` a stable high-α root and an unstable root below the trim α. Solved independently from
    `aero_math.hpp`'s assembly:

    | `cm_alpha_per_rad` | −0.6 | −0.3 | −0.1 | −0.03 | 0.0 | +0.03 | +0.2 | +0.6 |
    |---|---|---|---|---|---|---|---|---|
    | high root (rad) | 0.0236 | 0.0866 | 0.188 | 0.229 | 0.246 | 0.264 | 0.368 | 0.619 |
    | low root (rad) | −0.184 | −0.049 | −0.022 | −0.018 | −0.017 | −0.016 | −0.011 | −0.007 |

  - The low root is below the trim α of 0.0236 across [−0.6, +0.6], so every excursion is nose-up.
  - Maximum α approaches the high root. Its 0.23 rad crossing sits at −0.028 in the limit, and at about +0.03 within
    10 s.

## Probe evidence (scratch, nothing committed; no run used master seed 707)

- **(a) Sweep.** 37 values of `cm_alpha_per_rad` from −1.2 to +0.6, × wind seeds {42, 1, 2, 3, 4}, 20 s each, from
  the fixed state.
  - Maximum α is monotone in `cm_alpha_per_rad` and plateaus near the static high root: 0.056 at −0.6, 0.22 at 0.0
    and 0.62 at +0.6, at 10 s.
  - Minimum α is never below −0.006, so every excursion is nose-up.
  - The seed spread is about ±0.01. There were no halts and no errors.
- **(b)** α recomputed from the logged state and wind agrees with the logged α to 2.4e-15 rad. The summary's `max`
  equals the log maximum exactly, in all 185 runs.
- **(c)** The PyYAML route and the yaml-cpp route give equal rate-group data on `cruise.yaml` over 1000 frames.
- **(d) Stand-in batches** (seed 7070, [−0.6, +0.6], 10 s, 0.23 rad): about 1.5 s wall each, at 5.6 MB per run.
  - 40 runs: 18 fail and 22 pass. KS D = 1.0 and Spearman ρ = 0.995 for the cause; the best inert parameter reaches
    D = 0.33 and ρ = 0.16.
  - 120 runs: 59 fail and 61 pass. D = 1.0 and ρ = 0.997; the best inert reaches 0.22 and 0.17.
  - The controls flag nothing: KS gives "too few runs", and Spearman runs on a non-constant metric.
- **(e) By-chance BH flags.** 7.4 % per family in the main batch, and 5.0 % in the control.

## Decisions

- **D1: options and formula modes** (R11; C-09 and N-04 as in revision 1):

  | option | data-mode tier | formula mode |
  |---|---|---|
  | `probability` | calibrated verdict | `probability_runs(p_max, C)`: smallest n with `(1−p_max)^n ≤ 1−C` (299 at 0.01/0.95) |
  | `percentile` | calibrated verdict (as exceedance) | `percentile_runs(γ, C, two_sided, order)`: Wilks (59, 93; orders 2 and 3) |
  | `precision` | nominal projection | `precision_runs(p0, h, C)`: Clopper-Pearson at the expected count n·p0 (122; the change-4 caveat) |
  | `ranking` | nominal projection | `ranking_runs(r, m, α, power)`: Fisher z with Bonferroni (85, 120; signed r) |

  - The formula modes are unchanged from revision 1. Their objectives are already non-strict (`≤`), and D2 adopts
    that convention everywhere.
  - Fisher z's docstring keeps C-09's labels: asymptotic for Pearson, an approximation for Spearman, and no sizing of
    PRCC or KS.
- **D2: one non-strict convention, decided exactly (findings 5 and 6).**
  - A claim's bound is compared with its limit as `bound ≤ limit` for "met". The opposite one-sided bound must be
    strictly beyond the limit for "violated".
  - Every verdict is decided by the binomial-tail duality of the Clopper-Pearson bounds, not by comparing a computed
    beta quantile:
    - `U(k, n) ≤ p` if and only if `P(Bin(n, p) ≤ k) ≤ 1 − C`;
    - `L(k, n) > p` if and only if `P(Bin(n, p) ≥ k) < 1 − C`.
  - The tail is computed in floating point. When it lies within 1e-9 (relative) of `1 − C`, it is recomputed exactly
    in rational arithmetic, with the inputs taken as the decimals they are written as (`Fraction(repr(x))`, so 0.95
    is 19/20). A boundary case is then decided exactly.
  - Complements are formed in rational arithmetic from the original inputs, never in floating point (verification
    finding 1). Percentile's `p_max = 1 − γ` is `1 − Fraction(repr(γ))`, and `1 − C` is `1 − Fraction(repr(C))`. A
    float `1 − 0.9` is 0.09999999999999998, which would flip an exact boundary.
  - `probability_runs` applies the same exact check to `(1−p)^n ≤ 1−C` at its result and at its result minus 1.
  - Reported intervals still come from beta quantiles. Only decisions use the duality.
- **D3: probability (calibrated verdict).**
  - The claim is that the failure probability under a criterion (`astra.mc_analysis`'s grammar, default
    `status == halt`) is at most `p_max`, at confidence C. k failures in n analyzed rows; `error` rows are excluded
    and counted.
  - Verdict:
    - `met` if the one-sided exact upper bound satisfies `U ≤ p_max`;
    - `violated` if the one-sided exact lower bound satisfies `L > p_max`;
    - `undetermined` otherwise.

    Both bounds are always computed and checked independently (finding 6).
  - **Producer risk** (after Hanson & Beard; defined here in full).
    - β is the probability that a fresh batch of N runs fails to give `met` when the true rate equals the pilot's
      p̂ = k/n.
    - `needed(p̂, p_max, C, β)` is the smallest N with `P(Bin(N, p̂) ≤ k*(N)) ≥ 1 − β`. `k*(N)` is the largest k with
      `U(k, N) ≤ p_max`, decided per D2.
    - p̂ = 0 gives `probability_runs(p_max, C)`. p̂ ≥ p_max gives `None` ("the observed rate is not below p_max").
    - The default is β = 0.10, which is Joe's to change.
    - The objective is a sawtooth in N. `needed` is the smallest N that meets it, and the docstring says a larger N
      can fall short again.
  - The option minimum is `probability_runs(p_max, C)`: below it, `met` is unreachable.
- **D4: percentile, restated as exceedance (calibrated verdict).**
  - The claim for `side=upper` is `P(metric > limit) ≤ 1 − γ`. For `side=lower` it is `P(metric < limit) ≤ 1 − γ`.
  - This is D3 on the exceedance count, a strict inequality against the limit, so a value equal to the limit is not an
    exceedance.
  - With the quantile `q_γ = inf{x : F(x) ≥ γ}`, the upper claim is exactly `q_γ ≤ limit`, atoms included (`F(L) ≥ γ`
    if and only if `P(X > L) ≤ 1 − γ`). The docstring states this convention, and nothing else is claimed. Atoms at
    the limit and the lower side get deterministic oracles.
  - The verdict, `needed` and the option minimum are D3's at `p_max = 1 − γ`. No order statistic decides anything.
    The order statistic that bounds `q_γ`, when one exists, is printed for information.
- **D5: precision (nominal projection, never a verdict).**
  - Output:
    - the estimate;
    - a nominal two-sided interval at C, and its half-width hw (the larger distance from the estimate);
    - the tolerance (absolute or relative; relative refused at an estimate of 0);
    - the projected N, labeled `nominal, not calibrated`.

    There is no sufficient/insufficient word.
  - **Estimator conventions** (finding 7):
    - the sample mean;
    - `s²` with ddof = 1;
    - central moments `m_k = (1/n) Σ (x − x̄)^k`;
    - kurtosis `κ̂ = m4/m2²` (not excess);
    - skewness `g1 = m3/m2^{3/2}`.
  - **mean:** a t interval. Projected `N = ceil(n·(hw/tol)²)`.
  - **std:**
    - `Var(log s²) ≈ [κ̂ − (n−3)/(n−1)]/n`, from `Var(s²) = σ⁴[κ − (n−3)/(n−1)]/n`. The interval is
      `s·exp(±z·sqrt(Var)/2)`.
    - A percentile bootstrap (B = 2000, `montecarlo.Rng(0)`) is reported beside it, flagged `bootstrap disagrees` past
      a 25 % difference in half-width. The docstring says agreement does not imply calibration.
    - Option minimum: n ≥ 30, and n ≥ 100 when |g1| > 1. Below it there is no interval and no N.
    - **Projection** (verification finding 3). The smallest N with `max(s·e^{w(N)} − s, s − s·e^{−w(N)}) ≤ tol`,
      where `w(N) = z·sqrt(V(N))/2` and `V(N) = [κ̂ − (N−3)/(N−1)]/N`. s and κ̂ are held at the batch's values, and
      the result is then raised to the option minimum. It is a search, with an oracle and N / N−1 minimality.
    - A rare-tail test shows the limit (D7).
  - **percentile:**
    - The distribution-free two-sided order-statistic interval for `q_γ`, with the estimate `X_(⌈nγ⌉)`.
    - The projection `N = ceil(n·(hw/tol)²)` is labeled as depending on regularity: it assumes a positive density at
      the quantile.
    - Degenerate cases: a zero-width interval (an atom) gives `no projection: zero-width interval`, and an n too small
      for any interval at C gives none. Option minimum: the smallest n with a two-sided interval at C.
  - **probability:** the two-sided Clopper-Pearson interval. The projection is `precision_runs(p̂, tol, C)`.
  - **correlation:** Spearman ρ with D6's jackknife interval. The projection is D6's scaling.
  - Final `N = max(projection, option minimum)` (finding 9).
- **D6: ranking (nominal projection, never a verdict).**
  - Spearman `ρ̂_j` of each ranked parameter against a metric. m excludes seed and constant parameters.
  - **Jackknife variance of `z_j = atanh(ρ̂_j)`:** `v_j = ((n−1)/n) Σ_i (z_(−i) − z̄_(−i))²`, over the n leave-one-out
    Spearman values. This replaces 1.06/(n−3) (finding 2).
  - **Nominal simultaneous interval:** `tanh(z_j ± c·sqrt(v_j))`, `c = z_{1−α/(2m)}`.
    - Signed bounds are reported. The |ρ| bounds are: lower 0 when the interval spans 0, else the smaller absolute
      end; upper the larger.
  - **No interval** (finding 1) when:
    - |ρ̂_j| = 1;
    - any leave-one-out |ρ| = 1;
    - n is below the option minimum `RANK_MIN_N = 30`.

    The row says why.
  - **Projections.** Scaling `v_j(N) = v_j·n/N`:
    - top: the smallest N at which the largest |ρ̂|'s lower |ρ| bound exceeds every other parameter's upper bound,
      or `None` at a tie;
    - null(r_min): the smallest N at which every upper |ρ| bound is below r_min, or `None` when some
      |ρ̂| ≥ r_min.

    Final `N = max(projection, RANK_MIN_N)`.
- **D7: coverage tests (findings 2, 8, 10).**
  - **Deficit to detect:** a true coverage of 0.92 against a nominal 0.95. A correct interval must fail at most 1 %
    of the time, and an interval at 0.92 must fail at least 95 % of the time.
  - **K, by exact binomial arithmetic:** K = 1047 batches, passing when at least 978 cover. At 0.95 the false-fail
    rate is 0.96 %; at 0.92 the pass rate is 4.9 %. All coverage tests run at C = 0.95 with n = 100 per batch.
  - **Calibrated tier**, which must pass:
    - probability's one-sided bounds at p ∈ {0.01, 0.1, 0.5};
    - percentile's exceedance bounds on normal, exponential and a discrete distribution with an atom at the limit.
  - **Nominal tier.** Each regime is pinned below and recorded with its measured coverage:
    - mean: normal and exponential;
    - std: normal, and exponential at n = 100;
    - percentile: γ = 0.9, normal and exponential;
    - probability: as above;
    - correlation: bivariate normal at ρ ∈ {0, 0.5}, the sign-flip counterexample (X uniform, Y = S·X), and
      tied outcomes (Y rounded to 5 levels).

    Rules:
    - The regimes the docstring claims must pass. A regime that fails becomes a stated limit in the docstring, with
      its measured coverage, after Joe sees it. It is never loosened silently.
    - The **rare-tail case** (±1 with probability 0.99, ±100 with probability 0.01, n = 100) must show the std
      interval undercovering: at most 0.80 measured, against 0.95 nominal. It is the stated limit (finding 8).
- **D8: protocol (findings 3, 4, 9, 12).** The docstring and the CLI help state it:
  1. Run a pilot of P runs with its own master seed.
  2. Run `astra mc size <option> --batch PILOT`. This prints `needed`, already `max(projection, option minimum)`.
  3. Delete the pilot's directory. Only its `summary.parquet` was read.
  4. Run the final batch with a different master seed and `runs: N`.
  5. Take the verdict from the final batch alone.

  - The final batch is independent of the pilot, so a calibrated verdict is exact at C and the producer risk is the
    one D3 computes. Pilot and final share nothing but the spec.
  - The tool cannot enforce the separation. It prints a reminder when it is given a batch, and the check table records
    each batch's master seed (from the batch's record, PR 6's D2), so a reader can see two different seeds.
- **D9: output and CLI.**
  - `check(batch, claims) -> Table`. Each row holds:
    - the option and its tier;
    - the claim text;
    - n and the `error` rows;
    - the statistic, its bounds or interval, and the verdict (calibrated tier only);
    - `needed`;
    - notes (`nominal, not calibrated`, flags, reasons).

    `Table.text()` is deterministic. The claims file is a closed YAML map read with `yaml.safe_load`.
  - The CLI forms:
    - `astra mc size <option> [formula args]`;
    - `astra mc size <option> --batch DIR [claim args] [--convergence [--every K]]`;
    - `astra mc size check DIR CLAIMS.yaml`.

    The option arguments are as in revision 2, with `--p-max`, `--quantile`, `--limit`, `--side`, `--statistic`,
    `--tolerance [--relative]`, `--top | --null R_MIN` and `--producer-risk`.
  - Exit codes:
    - 0 printed (`violated` is a result, not an error);
    - 64 usage, including a value outside its domain;
    - 65 a refused batch, criterion, metric or claims file;
    - 74 stdout I/O;
    - 70 internal.
- **D10: convergence.**
  - `convergence(batch, claim)` gives the claim's statistic with its bounds or interval at every prefix, in run order.
  - Calibrated tier (verification finding 2): the bounds and the verdict are given at every prefix with at least one
    analyzed row. Below `probability_runs` they can still show `violated`. The option minimum only marks whether
    `met` is reachable at that prefix, a column of its own, and suppresses nothing.
  - Nominal tier: prefixes below the statistic's minimum give NaN, with the reason.
  - Nominal intervals stay labeled nominal. The bootstrap is not run per prefix; the jackknife is.
- **D11: R9 follows as PR 7b, and C-08 moves with it (finding 13).**
  - PR 7b closes C-08 and R9's acceptance: KS and Spearman, the strict margin, the minimum effect, BH, and the
    negative control, on Joe's approved numbers. R9 stays in `just check`, and PR 7b depends on PR 7.
  - **PR 7b's protocol (D8):**
    1. A pilot of P = 120 runs (`ranking_runs(0.3, m)`) with master seed 7071, plus its control. Each is sized with
       the ranking option: top driver for the main batch, null at 0.3 for the control.
    2. Delete both pilot directories.
    3. Final batches, both of N = max(the two projections, `RANK_MIN_N`), with master seed 707.
  - **Peak disk** is the larger of the pilots (2·120·5.6 MB = 1.34 GB) and the finals (2·N·5.6 MB). PR 7b stops and
    asks Joe if `2·N·5.6 MB > 4 GB`, that is, N > 357. The projected N is nominal, and the ranking option gives no
    verdict, so R9's acceptance stays M6 R9's named assertions on the final batch.
- **D12: gate time.** This PR adds synthetic tests, plus PR 6's 24-run descent batch, built once per module and
  removed by a finalizer that also runs on failure. K = 1047 coverage batches per regime are cheap without logs: no
  sim runs, only `montecarlo.Rng` draws. Except the jackknife's O(n²) per batch, which is measured and recorded.

## Interface (`python/astra/mc_sizing.py`)

- **Formula mode:** `probability_runs`, `percentile_runs`, `precision_runs`, `ranking_runs`, `clopper_pearson`,
  `half_width_at`, `percentile_confidence`.
- **Claims:** `ProbabilityClaim`, `PercentileClaim`, `PrecisionClaim` and `RankingClaim` (frozen, validated, with
  `SizingError` naming the field); `load_claims(path)`.
- **Results:** `Check`, `Table`, `check`, `check_one`, `producer_runs`, and `convergence`, returning a `Convergence`.
  Constants: `RANK_MIN_N`, `STD_MIN_N`, `STD_SKEWED_MIN_N`.

## Tests (`tests/python/test_mc_sizing.py`; acceptance `.venv/bin/pytest tests/python -k mc_sizing`)

Every test is named `test_mc_sizing_*`. Expected values come from scipy's `binomtest`, `binom` and `t`, from stdlib
`statistics` and `fractions`, from closed forms, or from loops in the test. Never from the module.
- **R11 formula modes:** revision 1's oracles, unchanged.
- **D2 boundaries, exact rational:**
  - `probability_runs(0.5, 0.75)` = 2, and at n = 2, k = 0 the verdict for p_max = 0.5 is `met`, since
    `(1/2)² = 1/4 = 1 − 3/4`.
  - At n = 2 and k = 2, `L = 1/2`, so p_max = 0.5 is not `violated`.
  - Further cases where the tail equals `1 − C` exactly in `Fraction`, each giving the non-strict side.
  - A float-near-boundary case decided by the exact recheck.
  - Percentile at γ = 0.9, C = 0.19, n = 2, k = 0: the tail equals 0.81 exactly, so the verdict is `met`, through the
    rational complement (verification finding 1).
  - Convergence on the ten-exceedance case at γ = C = 0.95: every prefix keeps its bounds, and the full prefix shows
    `violated` (L = 0.741…) below the option minimum (verification finding 2).
- **D3 values:**
  - Verdicts for (n, k, p_max) on hand-built batches, checked against `binomtest(..., alternative="less"/"greater")`
    bounds.
  - `error` rows are excluded and counted.
- **Producer risk:**
  - p̂ = 0 gives 299.
  - Four cases match a brute-force scan over N, which finds `k*` by `binomtest` and the probability by `binom.cdf`.
  - p̂ ≥ p_max gives `None`.
  - Minimality: N meets it, and N − 1 does not.
- **D4 exceedance:**
  - Lower and upper sides.
  - A value equal to the limit is not an exceedance.
  - A discrete population with an atom at the limit: the claim holds exactly when `F(L) ≥ γ`.
  - Violation is shown below the Wilks minimum. Joe's case: γ = C = 0.95, ten values all above L gives `violated`.
  - The printed order statistic matches a `binom.cdf` search.
- **Endpoint oracles (finding 11)**, each on fixed data against a computation in the test:
  - the t interval;
  - std with κ̂ and the corrected variance (κ = 3 and n = 30 give 2/29 exactly, for synthetic moments);
  - bootstrap agreement and disagreement, on constructed data that gives each;
  - the percentile order-statistic interval indices, and the atom giving zero width;
  - the two-sided probability interval;
  - the jackknife of Spearman, by an explicit loop with `scipy.stats.spearmanr`;
  - signed and absolute simultaneous bounds, including an interval spanning 0;
  - no interval at |ρ̂| = 1, at a leave-one-out |ρ| = 1, and below `RANK_MIN_N`;
  - a constant parameter, an empty family, and too few runs.
- **Minimality (finding 11).** For every search and projection, N meets its objective and N − 1 does not:
  - `probability_runs`, `percentile_runs`, `precision_runs` and the producer risk;
  - the precision scalings;
  - ranking top and null.
- **Coverage (D7):**
  - K = 1047 per regime, C = 0.95, n = 100.
  - The calibrated regimes must pass, at 978 or more.
  - The nominal regimes the docstring claims must pass.
  - The rare-tail std case must measure at most 0.80.
  - The measured coverages are printed and recorded in the PR.
- **R12 convergence.** For one claim of each option and each precision statistic, on a synthetic batch with `error`
  rows and on the descent batch, every prefix equals a direct computation on that prefix.
- **D8 independence.**
  - A pilot spec and a final spec that differ only in `seed` draw different values. The final batch's rows don't
    depend on whether a pilot ran.
  - `needed` is at least the option minimum.
  - The table records the batch's master seed.
- **R15 (`mc size`).**
  - Each option's formula mode prints the expected N and the assumption line.
  - Each data mode on the descent batch prints its verdict or nominal projection and N.
  - `check` with a claims file prints one table, and `--convergence` prints prefixes.
  - The refusals exit 64 or 65 as in D9.

## Files

- **New:** `python/astra/mc_sizing.py`, `tests/python/test_mc_sizing.py`, and
  `tests/python/mc_sizing_fixtures/claims.yaml`.
- **Changed:** `python/astra/cli.py` (`mc size`, and the docstring).
- **Docs** (the `/wrap` docs step):
  - architecture §12;
  - the M6 plan, with an amendment note only: Joe's requirements change; R11 and R12 extended; R9, Q9 and C-08's
    closure moved to PR 7b;
  - PLAN.md (the PR 7 line and a PR 7b line);
  - README;
  - `docs/future-work.md`;
  - `docs/lessons.md` (Joe's wording);
  - STATE.md, this plan, and the review record and gate evidence.
- **Not touched:** `core/`, `plugins/`, `aircraft/`, `astra.mc_analysis`, `astra.montecarlo`'s behavior, goldens,
  tolerances and ADRs.

## Verification and process

1. **Verification pass, then approval.** One narrow Codex verification pass on the revised sections only: "Joe's
   direction", D1 to D12, Tests and the resolution table. Its findings are recorded here and in PR #93. Then Joe's
   approval. Nothing is built before it.
2. **Commits,** pushing after each:
   - formula mode and D2 (R11 and the boundary cases);
   - D3 and D4 with the producer risk;
   - precision;
   - ranking;
   - the coverage tests;
   - convergence;
   - the CLI;
   - docs.
3. **Checks.**
   - Acceptance: `.venv/bin/pytest tests/python -k mc_sizing`. Regressions: `-k 'mc_analysis or montecarlo or cli'`.
   - Then pyright, `ruff check .`, `ruff format --check .`, `python3 scripts/docs_check.py` and `git diff --check`.
4. **Full gate.** `ps` must first show no other ASTRA build or gate. Then run `rm -rf build && ./scripts/check-full.sh`
   in the background, confirm exit 0 and `check: OK`, and commit the trimmed summary.
5. **`/review-cycle`** on the final diff, at most two correction rounds, checking C-09 and N-04. Then
   `/human-review` and `/wrap`.

Rollback: everything is additive, and reverting removes the module, the subcommand and the tests.

## Plan critique (revision 2) and its resolution

One fresh, read-only Codex plan critique through `/codex:rescue --fresh` (companion 1.0.6), on `80d7c38` against
`7e65a85`. Thread `01a0ffb7-3d24-7742-b35e-cbdc7f34ebcb`. **Verdict: rework before approval.** It made 13 findings,
6 high and 7 medium, given in full in PR #93's body.

Claude checked the numbers in findings 5, 7, 8, 10 and 12 in this session, and they hold. Joe accepted all 13.

| # | Sev. | Finding | Resolution (revision 3) |
|---|---|---|---|
| 1 | high | ±1 sample correlation gives a degenerate interval | D6: no interval at \|ρ̂\| = 1, at a leave-one-out \|ρ\| = 1, or below `RANK_MIN_N`; endpoint oracles |
| 2 | high | 1.06/(n−3) fails off normality (sign-flip: 1.8/n) | D6: jackknife variance; ranking is nominal, never a verdict; D7 sign-flip and tie regimes |
| 3 | high | Pilot reuse erodes confidence | D8: an independent pilot only sizes; the final batch is fresh, so the verdict is exact |
| 4 | high | Producer risk models a fresh batch | D8 makes the final batch fresh, so D3's objective is the right one |
| 5 | medium | Strict and non-strict boundaries disagree | D2: one non-strict convention, decided by tail duality with an exact rational recheck; boundary oracles |
| 6 | medium | Missing bound treated as missing verdict; discrete atoms | D3 and D4: each bound checked independently; exceedance with `q_γ = inf{x: F ≥ γ}`; atom oracles |
| 7 | medium | Wrong fourth-moment factor | D5: the corrected `[κ − (n−3)/(n−1)]/n` and stated estimators; the 2/29 oracle |
| 8 | high | Std minimum and bootstrap don't calibrate | D5 nominal tier; D7's rare-tail case shows the limit |
| 9 | medium | Percentile scaling needs regularity; N < P | D5 labels it and defines the degenerate cases; N = max(projection, minimum); D8's final batch is fresh |
| 10 | high | K = 400 is too weak | D7: K = 1047 from 0.92 against 0.95 at 95 % power, 1 % false-fail |
| 11 | medium | Missing oracles | Tests: endpoint oracles for every statistic; N / N−1 for every search |
| 12 | medium | Disk ignores the pilots | D11: pilots deleted after sizing; peak = max(pilots, finals); N > 357 asks Joe |
| 13 | medium | C-08 must move with R9 | Header and D11: C-08 and R9's acceptance move to PR 7b, and R9 stays in the gate |

## Verification pass (revision 3)

One narrow, read-only Codex verification pass through `/codex:rescue --fresh`, on `b48c5a3`, scoped to the revised
sections. Thread `01a0ffc8-79fb-75c3-9b58-7e476c990120`. **Verdict: rework.**

Of the 13 prior findings, 12 are resolved and 5 is partially resolved (folded into new finding 1). Codex recomputed
D7's figures (false-fail 0.961 %, power 95.09 % at K = 1047 and threshold 978) and D11's disk arithmetic (N = 357
gives 3998.4 MB).

It made three new findings, all medium. Claude checked each numerically in this session, and each holds. All three
are fixed in this revision (3a):

| # | Finding (verified) | Fix |
|---|---|---|
| V1 | The exact recheck forwarded a float `1 − γ`. γ = 0.9 gives p_max = 0.09999999999999998, which flips the exact γ = 0.9, C = 0.19, n = 2 boundary (`met`) | D2: complements in rational arithmetic from the original inputs; the boundary oracle added to Tests |
| V2 | Convergence returned NaN below the option minimum, though calibrated bounds exist there (k = n = 10 at γ = C = 0.95 gives L = 0.741, so `violated`) | D10: calibrated bounds and verdicts at every prefix; the minimum only marks whether `met` is reachable; oracle added |
| V3 | The std projection was undefined | D5: a search on `V(N) = [κ̂ − (N−3)/(N−1)]/N` with s and κ̂ held, raised to the minimum; oracle and N / N−1 |

Joe asked for one verification pass, and it has run. These fixes go to him with the plan, without another pass unless
he asks for one.
