# M6 PR 7: Monte Carlo sizing options, their data modes, convergence and `astra mc size` (R9 follows as PR 7b)

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29, as amended by Joe on 2026-10-02 (the
"Requirements change" below): "Sizing and convergence", R11, R12, R15's `mc size` case, Q10, Q14, Q15, and
"Obligations carried to PR plans" (PR 7: C-08, C-09, N-04, from PR #78's per-finding notes); architecture §12's
`astra.mc_sizing` and `astra.cli` bullets. R9 and Q9 move to PR 7b (D11).
Risk: **Tier 2** (a Python feature and tests; no new dependency, since scipy 1.18.1 is pinned). Base: `origin/main`
`7e65a85`. Worktree/branch: `worktree-m6-pr7-mc-sizing`, draft PR #93. Touches no `core/`, `plugins/`, `aircraft/`,
`icd/`, `tests/golden/**`, tolerance file or ADR. Binding ADRs: 0025/0027 (batches read through
`astra.mc_analysis.load_batch`), 0002 (SI), 0010.
Status: **revision 2, for one Codex plan critique and then Joe's approval.** Revision 1 was approved by Joe with
changes 1 to 7 (his message of 2026-10-02). At the R9 checkpoint he then approved R9's numbers and changed the
requirements. Nothing here is approved until he says so.

## Context

The M6 plan's sizing answered four questions with closed formulas before a batch runs. Joe's change makes each one
an *option* named for the claim it answers: **probability**, **percentile**, **precision** and **ranking**. Each
gets two modes:
- without a batch, today's formula gives the minimum N;
- with a batch, the option judges its claim from the batch's own data and gives the N that would decide it.

The protocol is that a pilot batch is a prefix of the final batch. The pilot sizes N; only the final batch gives the
verdict. Convergence shows each option's statistic and its interval against the run count. R9, the planted cause,
becomes the ranking option's end-to-end case, so it follows as PR 7b (D11).

## Requirements change (Joe, 2026-10-02, verbatim in substance)

1. **Without a batch:** the formula gives the minimum N (zero-failure, Wilks, Clopper-Pearson proportion, Fisher z).
   R11's oracles stay.
2. **With `--batch DIR`:** the option's verdict from the batch's own data (met / violated / undetermined, or
   sufficient / not sufficient), and the N that would decide it.
   - **probability:** exact one-sided Clopper-Pearson on a criterion; N from the observed rate with a stated producer
     risk (Hanson & Beard).
   - **percentile:** a distribution-free order-statistic bound on a metric against a limit; N likewise.
   - **precision:** of a mean, a std (fourth-moment interval on log s², a bootstrap cross-check, disagreement
     flagged), a percentile, a probability or a correlation, with a relative or absolute tolerance. N = n·(hw/tol)²
     from the batch's own variance and kurtosis, with a stated minimum n for std on skewed outputs.
   - **ranking:** the top driver, resolved by simultaneous intervals. The null claim is that every |ρ|'s upper bound
     is below r_min, with Spearman variance 1.06/(n−3). N comes from the observed correlations.

   Several options can be checked on one batch, and the result is one table PR 8's report can show.
3. **Protocol:** the pilot is a prefix of the final batch (`run_seed(master, i)` does not depend on `runs`; test
   it). The pilot sizes N, and only the final batch gives the verdict.
4. **Convergence:** each option's statistic, with its interval, against n. This extends D7 beyond the failure
   proportion.
5. **Oracles for the data modes:** exact binomial and order-statistic values; empirical coverage over seeded normal
   and skewed synthetic batches, with a binomial allowance (like R8); and known producer-risk cases.
6. **R9 uses the ranking option.** The main batch claims "`cm_alpha_per_rad` is the top driver". The control claims
   "no parameter with |ρ| ≥ 0.3". The pilot is a prefix of seed 707, and N comes from the tool. Say whether R9 stays
   in this PR or follows.

Also from the checkpoint, approved by Joe:
- R9's numbers: uniform [−0.6, +0.6], 10 s, `max.v0.derived.alpha > 0.23`, minimum effect unchanged.
- The main batch asserts no inert-parameter BH decision (only the control's "flags nothing").
- The rewritten physics rationale and lesson (findings and probe evidence below).

## Repository findings (read at `7e65a85`)

- `python/astra/mc_sizing.py` is drafted in the worktree (not yet committed). It holds the formula mode:
  - `zero_failure_runs`, `tolerance_runs` and `correlation_runs`;
  - `clopper_pearson` for a real k by beta quantiles;
  - `half_width_at`, `proportion_runs`, and a failure-proportion `convergence`.

  Every R11 oracle is met: 299; 59 and 93; Wilks orders 2 and 3 at 93 and 124; two-sided order 2 at 153; 85 and
  120; Clopper-Pearson against `binomtest` within 6e-17; the p0 = 0 closed form, 122. Joe's change-4 example also
  holds: 632 runs, and the half-width at `round(n·p0)` exceeds 0.02 for n = 632 to 635. It is renamed and extended
  per D1.
- `astra.montecarlo.run_seed(master, index)` hashes only the master seed and the index, and `draw(spec, index)` reads
  nothing from `spec.runs` (`montecarlo.py:260`, `:305`). So a pilot of P runs is exactly the first P rows of a batch
  of N ≥ P runs with the same spec otherwise. The prefix test (D8) pins this.
- `astra.mc_analysis` (PR 6) gives `load_batch`, `outcomes`, `metric`, `parameter_values`, `analyzed_rows` and
  `CriterionError`. A batch's ranked parameters exclude seed dispersions, and a constant parameter leaves the family.
- `astra.cli` has `_Parser` (64), `_write`, `_rate`, `_finite`, and the `mc` group with `analyze` and `rerun`.
- Randomness: numpy is unpinned, so every resample and every synthetic draw uses `montecarlo.Rng`.
- Hanson & Beard is not under `docs/refs/`. Its role here is the producer-risk definition, which D3 states in full so
  that the oracle needs no outside source. If Joe wants the citation checked, he places the report under
  `docs/refs/`.
- **R9 physics (carried to PR 7b; corrected per Joe).**
  - There is no divergence. AERORP sits 0.36 m ahead of and 1.63 m above the CG, and the transfer `r_aero_b × F` is
    nonlinear in α.
  - So the fixed-elevator `Cm(α)` about the CG has two roots: a stable one at high α and an unstable one below trim
    α. Solved independently from `aero_math.hpp`'s assembly (q = 0, β = 0, elevator at the shipped trim):

    | `cm_alpha_per_rad` | -0.6 | -0.3 | -0.1 | -0.03 | 0.0 | +0.03 | +0.2 | +0.6 |
    |---|---|---|---|---|---|---|---|---|
    | high root (rad) | 0.0236 | 0.0866 | 0.188 | 0.229 | 0.246 | 0.264 | 0.368 | 0.619 |
    | low root (rad) | -0.184 | -0.049 | -0.022 | -0.018 | -0.017 | -0.016 | -0.011 | -0.007 |

  - The low root stays below the trim α of 0.0236 across [−0.6, +0.6], so every excursion is nose-up.
  - Maximum α approaches the high root. Its 0.23 rad crossing sits at `cm_alpha_per_rad` = −0.028 in the limit, and at
    about +0.03 within 10 s (the sweep).

## Probe evidence (scratch, nothing committed; no run used master seed 707)

- **(a) Sweep.** 37 values of `cm_alpha_per_rad` from −1.2 to +0.6, × wind seeds {42, 1, 2, 3, 4}, 20 s each, from
  `cruise.yaml`'s fixed state.
  - Maximum α is monotone in `cm_alpha_per_rad` and plateaus near the static high root: at 10 s, 0.056 at −0.6, 0.22
    at 0.0 and 0.62 at +0.6; at +0.6 the 20 s value equals the 10 s one.
  - Minimum α is never below −0.006 rad, so every excursion is nose-up.
  - The seed spread is about ±0.01. There were no halts and no errors.
  - At 10 s, 0.23 rad is crossed for `cm_alpha_per_rad` ≳ +0.03.
- **(b)** α recomputed from `v0.derived.{u,w,phi,theta,psi}` and `wind.*` agrees with the logged α to 2.4e-15 rad.
  The summary's `max` equals the log column's maximum exactly, in all 185 runs. α stays below π, but the comparison
  keeps the (−π, π] wrap (Joe's change 5).
- **(c)** `cruise.yaml` read through `yaml.safe_load` + `Config.from_dict` and through yaml-cpp gives equal
  rate-group data over 1000 frames.
- **(d) Stand-in batches** (master seed 7070, range [−0.6, +0.6], 10 s, threshold 0.23):

  | batch | wall | disk | fail/pass | KS cause D, p | best inert D | Spearman cause ρ | best inert ρ |
  |---|---|---|---|---|---|---|---|
  | planted 40 | 1.5 s | 225 MB | 18/22 | 1.0, 1.8e-11 | 0.33 | 0.995 | 0.16 |
  | planted 120 | 1.5 s | 673 MB | 59/61 | 1.0, 2.1e-35 | 0.22 | 0.997 | 0.17 |

  - Both controls: KS gives "too few runs" (no failures). Spearman runs on a non-constant metric (40 and 120 distinct
    values) and rejects nothing.
  - A log takes 5.6 MB per 10 s run, and the time is dominated by spawning workers.
- **(e) By-chance flags, simulated** (400,000 draws, cross-checked with the module's BH on 20,000):
  - in the main batch, the chance that an inert parameter is rejected beside the cause is 7.4 % per family;
  - in the control, the chance that BH rejects any of the three nulls is 5.0 %.

  Joe dropped the main-batch assertion, so R9's by-chance failure is about 5 %.

## Decisions

- **D1: options and names.** These are module functions and `astra mc size <option>`. The formula mode of each:

  | option | claim | formula mode (R11) |
  |---|---|---|
  | `probability` | P(fail) < p_max | zero-failure: `probability_runs(p_max, C)` (299 at 0.01/0.95) |
  | `percentile` | the γ-quantile of a metric is below (or above) a limit | Wilks: `percentile_runs(γ, C, two_sided, order)` |
  | `precision` | a statistic is known within a tolerance | Clopper-Pearson proportion: `precision_runs(p0, h, C)` (D4 of revision 1, kept: real k = n·p0, and the change-4 caveat) |
  | `ranking` | a top driver is resolved, or no parameter reaches r_min | Fisher z with Bonferroni: `ranking_runs(r, m, α, power)` (85, 120; signed r, N-04) |

  The revision 1 domain checks, assumption lines and refusals stay (C-09: the Wilks convention, the Clopper-Pearson
  objective, Fisher z's limits for Spearman, and that it does not size PRCC or KS).
- **D2: one verdict shape.**
  - Every data-mode check returns a `Check`: option, claim text, n analyzed, n of `error` rows, the statistic, its
    interval, the verdict, `needed` (the N that would decide it, or `None` with a reason), and notes (flags).
  - Verdicts are `met`, `violated` or `undetermined` for probability, percentile and ranking-null, and `resolved` or
    `unresolved` for ranking-top. Precision gives `sufficient` or `not sufficient`.
  - `error` rows are excluded and counted, as in `astra.mc_analysis`.
  - `check(batch, claims) -> Table` checks several claims on one batch. `Table.text()` is deterministic, and
    `Table.rows` is what PR 8's report renders.
  - A claims file (YAML, a closed map, read through `yaml.safe_load` like every spec) lists the claims for the CLI.
- **D3: probability (data mode).**
  - The claim is that the failure probability under a criterion (`astra.mc_analysis`'s grammar, default
    `status == halt`) is below `p_max`, at confidence C.
  - With k failures in n analyzed rows: one-sided exact Clopper-Pearson bounds `U = beta.ppf(C, k+1, n−k)` (1 at
    k = n) and `L = beta.ppf(1−C, k, n−k+1)` (0 at k = 0).
  - Verdict: `met` if U < p_max; `violated` if L ≥ p_max; otherwise `undetermined`.
  - **Producer risk (stated here, after Hanson & Beard).**
    - The producer risk β is the chance that a system whose true rate is the observed p̂ = k/n fails to be shown
      `met` by a batch of N runs.
    - `needed` is the smallest N with `P(Binomial(N, p̂) ≤ k*(N)) ≥ 1 − β`, where `k*(N)` is the largest count with
      `U(k*, N) < p_max`.
    - p̂ = 0 gives the zero-failure N. p̂ ≥ p_max gives `None` ("the observed rate is not below p_max").
    - Default β = 0.10, which is a choice for Joe.
    - The objective is a sawtooth in N, so the result is the smallest N, and the docstring says a larger N can fall
      below 1 − β again.
- **D4: percentile (data mode).**
  - The claim is that the γ-quantile of a metric column is below a limit L (`side=upper`), or above it
    (`side=lower`), at confidence C.
  - Distribution-free: the upper confidence bound is the order statistic `X_(r)`, with r the smallest index such that
    `P(Binomial(n, γ) ≤ r−1) ≥ C`. The lower bound is symmetric.
  - Verdict: `met` if the bound lies on the claimed side of L; `violated` if the opposite one-sided bound lies beyond
    L; `undetermined` otherwise, including when n is below the Wilks minimum, so that no such r exists.
  - That verdict is exactly the probability option on the exceedance event `metric ≥ L` with `p_max = 1 − γ` (side
    upper; mirrored for lower). A tie with L counts as an exceedance (conservative).
  - So `needed` reuses D3's producer-risk N on the exceedance count. A test checks the equivalence on random data
    (D8).
- **D5: precision (data mode).** Each statistic has a two-sided interval at C, and the half-width hw is the larger
  distance from the estimate. The verdict is `sufficient` iff hw ≤ tol, where tol is absolute or relative
  (`tol·|estimate|`; refused at an estimate of 0).
  - **mean:** the t interval. `needed = ceil(n·(hw/tol)²)`.
  - **std:**
    - Interval: `s·exp(±z·se/2)` with `se² = (κ̂ − (n−3)/n)/(n−1)`, the fourth-moment variance of log s², with κ̂
      the sample kurtosis (non-excess).
    - Cross-check: a percentile bootstrap (B = 2000, indices from `montecarlo.Rng(0)`). If the two half-widths differ
      by more than 25 %, the check is flagged `bootstrap disagrees` and keeps the analytic verdict.
    - Stated minimum: n ≥ 30, and n ≥ 100 when the sample's |skewness| > 1. Below it the result is `undetermined`
      (`too few runs for a std on a skewed output`).
    - `needed = ceil(n·(hw/tol)²)`, and the minimum still applies.
  - **percentile:** the distribution-free two-sided order-statistic interval for the γ-quantile, with the estimate
    `X_(ceil(nγ))`. `needed = ceil(n·(hw/tol)²)`. When n is too small for any interval at C, the result is
    `undetermined`.
  - **probability:** the two-sided Clopper-Pearson interval on a criterion. `needed = precision_runs(p̂, tol, C)`, the
    exact search rather than the scaling law, because the binomial is skewed at a small p̂.
  - **correlation:** Spearman ρ of a parameter against a metric. The interval is Fisher z with variance
    `1.06/(n−3)` (Fieller, Hartley and Pearson). `needed` is the smallest n whose interval at the observed ρ has
    hw ≤ tol (a search).
  - The scaling law assumes the batch's variance and kurtosis hold at N. That is stated, and it is why only the final
    batch gives the verdict.
- **D6: ranking (data mode).** The metric is a numeric summary column, and the parameters are the batch's ranked
  ones (seeds and constants excluded), m of them.
  - Simultaneous intervals: `tanh(atanh(ρ̂_j) ± c·sqrt(1.06/(n−3)))` with `c = z_{1−α/(2m)}`, α = 1 − C.
  - |ρ| bounds: the lower is 0 when the interval spans 0, else the smaller absolute end; the upper is the larger
    absolute end. A |ρ̂| of 1 gives the degenerate interval [±1, ±1].
  - **top:** `resolved` iff the largest |ρ̂|'s lower |ρ| bound exceeds every other parameter's upper bound. `needed`
    is the smallest n for which that holds at the observed ρ̂ (a search), or `None` when the two largest |ρ̂| tie.
  - **null(r_min):**
    - `met` iff every upper |ρ| bound < r_min; `violated` iff some lower bound ≥ r_min; `undetermined` otherwise.
    - `needed` is the smallest n with every upper bound < r_min at the observed ρ̂, or `None` when some |ρ̂| ≥ r_min.
    - This N is very sensitive to the pilot's largest |ρ̂|. At r_min = 0.3, m = 3 and C = 0.95, it is 142 at 0.10,
      536 at 0.20 and 2079 at 0.25, so D11 sizes R9's pilot with this in mind.
- **D7: convergence.**
  - `convergence(batch, claim) -> Convergence` evaluates the claim's statistic and interval at every prefix of the
    batch's rows in run order: per parameter for ranking, the bound for percentile and probability, the interval for
    precision.
  - Prefixes with no analyzed row, or below the claim's minimum, give NaN.
  - The bootstrap is not run per prefix (stated).
  - `Convergence.text(every)` prints every K-th prefix plus the last.
- **D8: protocol.** The docstring and the CLI help describe it:
  1. Run a pilot of P runs.
  2. Size it with `--batch` (`needed`).
  3. Rerun the same spec with `runs: N` and the same master seed. The pilot's runs are its first P runs, so nothing
     is wasted.
  4. Take the verdict from the final batch only.

  Reusing the pilot's runs inside the final batch makes this a two-stage (internal-pilot) design, whose small effect
  on the stated confidence is not corrected; this is said, not hidden.
- **D9: CLI.** `astra mc size <option> [formula args]` keeps the formula mode, and adds
  `astra mc size <option> --batch DIR [claim args] [--convergence [--every K]]` and
  `astra mc size check DIR CLAIMS.yaml`.
  - The claim args per option:
    - probability: `--p-max P --criterion EXPR --producer-risk B`;
    - percentile: `--metric COL --quantile G --limit L --side upper|lower`;
    - precision: `--statistic ... --metric COL | --criterion EXPR | --parameter NAME`, `--tolerance T [--relative]`,
      `--quantile G`;
    - ranking: `--metric COL --top | --null R_MIN`;
    - every option: `--confidence C`.
  - Exit codes: 64 usage (a value outside its domain included); 65 a refused batch, criterion, metric or claims file;
    74 a failed stdout write; 70 an internal error; 0 printed. A `violated` or `unresolved` verdict is a result, not
    an error, so it exits 0.
- **D10: gate time and disk.** Everything in this PR is synthetic or uses PR 6's 24-run descent batch, so it adds
  seconds. The coverage tests' cost is measured and recorded. R9's disk belongs to PR 7b.
- **D11: R9 follows as PR 7b.** This PR's surface (four data modes, the coverage oracles, the CLI) is large enough for
  one review. R9 is the ranking option's end-to-end case and should be built against its reviewed interface. The probe
  evidence above carries over. PR 7b's plan:
  - **Pilot.** P = `ranking_runs(0.3, m)` = 120 (the formula mode sizing the pilot). At 40 runs the chance that some
    null reaches |ρ̂| ≥ 0.3, so that no N exists, is 17 %. At 120 it is 0.3 %, and the typical largest |ρ̂| is 0.13,
    which gives N ≈ 200.
  - **Sizing.** N = max(the main batch's top-driver `needed`, the control's null `needed`, P). Both batches use N so
    that they stay paired.
  - **Disk.** 5.6 MB per run, so N = 200 is 2.2 GB at peak. If `needed` would exceed a 4 GB peak (N > 350), PR 7b
    stops and asks Joe before running.
  - **Assertions.** It also keeps M6 R9's KS, Spearman and BH assertions on the final batch, as approved.

## Interface (`python/astra/mc_sizing.py`)

- **Formula mode:** `probability_runs`, `percentile_runs`, `precision_runs` and `ranking_runs` (D1), plus
  `clopper_pearson`, `half_width_at` and `percentile_confidence`.
- **Claims:** `ProbabilityClaim`, `PercentileClaim`, `PrecisionClaim` and `RankingClaim` (frozen, validated at
  construction, `SizingError` naming the field), and `load_claims(path)`.
- **Results:** `Check`, `Table`, `check(batch, claims)`, `check_one(batch, claim)`, and `convergence(batch, claim)`
  returning a `Convergence`.

## Tests (`tests/python/test_mc_sizing.py`; acceptance `.venv/bin/pytest tests/python -k mc_sizing`)

Every test is named `test_mc_sizing_*`. Expected values come from scipy's `binomtest`/`binom`, stdlib
`statistics.NormalDist`, closed forms, or brute force in the test, never from the module.
- **R11 (formula mode):** revision 1's oracles, unchanged (299; 59/93/124/153; 85/120 with ±r and the refusals;
  Clopper-Pearson against `binomtest`; 122; the change-4 test, 632 and 632..635).
- **R11b, exact data-mode values.**
  - Probability on hand-built batches: (n, k) = (299, 0) and p_max 0.01 is `met` (U < 0.01); (100, 5) and p_max 0.01
    is `violated`; (100, 1) and p_max 0.03 is `undetermined`. Each check compares against `binomtest(k, n,
    alternative=...)`'s one-sided exact bounds.
  - Percentile: on seeded data, `X_(r)` and r equal a brute-force search over `binom.cdf`. The D4 equivalence with
    the exceedance count holds on 200 seeded cases, ties included.
- **R11c, producer risk.**
  - The limit p̂ = 0 gives the zero-failure N (299).
  - For four (p_max, C, p̂, β) cases, `needed` equals a brute-force scan of N that finds `k*` with
    `binomtest(alternative="less")` and the probability with `binom.cdf`.
  - p̂ ≥ p_max gives `None` with its reason.
- **R11d, coverage (Joe's item 5).**
  - K = 400 seeded synthetic batches per case (`montecarlo.Rng`), in a normal case and in a skewed case (exponential,
    `-ln u`; skewness 2).
  - The intervals: probability's one-sided Clopper-Pearson, percentile's order-statistic bound, and precision's mean
    t, std (log s², at the stated minimum n), percentile, probability and correlation (bivariate normal with known
    ρ, population Spearman `(6/π)·asin(ρ/2)`).
  - Ranking's simultaneous coverage across m = 4 counts as a family.
  - Each count of covering batches must be at least `binom.ppf(0.01, K, C)`.
  - Any shortfall is a build finding that goes to Joe, never a loosened allowance.
- **R11e, verdict logic.** Constructed batches give each verdict for each option: top resolved and unresolved, the
  tie with `None`, and null met, violated and undetermined; std below its minimum and the bootstrap-disagreement
  flag; relative tolerance at an estimate of 0 refused.
- **R12, convergence.** For one claim of each option, on a synthetic batch with interleaved `error` rows and on the
  descent batch, every prefix's statistic and interval equal a direct computation on that prefix.
- **D8, prefix.**
  - `draw(spec with runs=24, i) == draw(spec with runs=8, i)` for i < 8.
  - The descent spec run with `runs: 8` gives summary rows equal to the first 8 of the 24-run batch, with logs
    byte-identical.
- **R15 (`mc size`).**
  - Each option's formula mode prints the expected N and the assumption line, and exits 0.
  - Each data mode on the descent batch exits 0 and prints its verdict and N.
  - `check` with a claims file prints one table, and `--convergence` prints prefixes.
  - A missing or unknown option, an out-of-domain value, `--top` with `--null`, or a data argument without
    `--batch` exits 64.
  - A missing batch, a bad criterion or metric, or a claims file with an unknown key exits 65.
- The descent batch is built once per module, and a finalizer removes it, failures included (Joe's change 2).

## Files

- **New:** `python/astra/mc_sizing.py`, `tests/python/test_mc_sizing.py`, and
  `tests/python/mc_sizing_fixtures/claims.yaml`.
- **Changed:** `python/astra/cli.py` (`mc size` and the docstring), and `tests/python/test_montecarlo.py` (the prefix
  test, if it fits better there; named `test_mc_sizing_*` either way).
- **Docs** (the `/wrap` docs step, branch scope):
  - architecture §12 (`astra.mc_sizing` built, with the four options and data modes; the CLI bullet);
  - the M6 plan, with an amendment note recording Joe's requirements change against "Sizing and convergence", R11,
    R12, and R9's move to PR 7b. A note only: his decision, recorded, without rewriting the approved text;
  - PLAN.md (the PR 7 line and a PR 7b line);
  - README ("Size a batch");
  - `docs/future-work.md`;
  - `docs/lessons.md`, in Joe's wording: a linear stability estimate about trim mispredicted a departure that a
    nonlinear moment transfer turns into a high-α equilibrium, so solve Cm(α) = 0 before predicting divergence;
  - STATE.md, this plan, and the review record and gate evidence.
- **Not touched:** `core/`, `plugins/`, `aircraft/`, `astra.mc_analysis`, `astra.montecarlo`'s behavior, goldens,
  tolerances and ADRs.

## Verification and process

1. **Plan critique (now).**
   - One fresh, read-only Codex plan critique of this revision through `/codex:rescue --fresh --wait`
     (`docs/agents/review-dispatch.md#plan-critique`), asking for failed assumptions only. It is recorded in this plan
     and in PR #93's body.
   - Then Joe's approval. Nothing is built before it, and the formula-mode draft stays uncommitted until then.
2. **Commits,** pushing after each, in this order:
   - formula mode and R11;
   - the probability and percentile data modes, with R11b and R11c;
   - precision;
   - ranking;
   - the coverage tests (R11d);
   - convergence (R12);
   - the prefix test;
   - the CLI (R15);
   - docs.
3. **Checks.**
   - Acceptance: `.venv/bin/pytest tests/python -k mc_sizing`. Regressions: `-k 'mc_analysis or montecarlo or cli'`.
   - Then `.venv/bin/pyright`, `ruff check .`, `ruff format --check .`, `python3 scripts/docs_check.py` and
     `git diff --check`.
4. **Full gate.** `ps` must first show no other ASTRA build or gate. Then run `rm -rf build && ./scripts/check-full.sh`
   in the background, and confirm exit 0 and `check: OK`. Commit the trimmed gate summary, and record the time added
   against `ffee1d7`.
5. **`/review-cycle`.** A Codex review of the final diff (at most two correction rounds), checking C-08 (as it applies
   here: the separation of correlation power from KS and ranking power), C-09 and N-04.
6. **`/human-review` and `/wrap`.**

Rollback: everything is additive, and reverting removes the module, the subcommand and the tests.
