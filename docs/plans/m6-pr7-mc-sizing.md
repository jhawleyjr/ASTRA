# M6 PR 7: Monte Carlo sizing, convergence, `astra mc size`, and R9's planted cause

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29: the PR 7 row of "PR decomposition", "Sizing and
convergence" under "Contract and failure behavior", R9, R11, R12, R15's `mc size` case, Q9, Q10, Q14, Q15, and
"Obligations carried to PR plans" (PR 7: C-08, C-09, N-04, from PR #78's per-finding notes); architecture §12's
`astra.mc_sizing` and `astra.cli` bullets.
Risk: **Tier 2** (Python feature and tests; no new dependency, since scipy 1.18.1 is pinned). Base: `origin/main`
`7e65a85`. Worktree/branch: `m6-pr7-mc-sizing` (`claude --worktree`). Touches no `core/`, `plugins/`, `aircraft/`,
`icd/`, `tests/golden/**`, tolerance file or ADR. Binding ADRs: 0025/0027 (batches and logs read through
`astra.mc_analysis.load_batch` and `astra.log.load`), 0002 (SI), 0010 (`t = frame * dt`).
Status: **approved by Joe with changes 1 to 7, 2026-10-02** (his message "Approve with these changes", folded in
below and marked "Joe's change N"). R9's numbers (D1) and the run-count option stay open until the probe checkpoint. No Codex plan critique: the milestone plan was critiqued, and Q23 gives PR-level detail to this PR's review.

## Context

PR 6 ranks which dispersed parameters drive a batch's outcome. Two things are still missing. First, a user can't size
a batch before running it, or see whether its estimate has settled. Second, nothing shows the analysis finding a real
cause in the real sim. This PR adds `astra.mc_sizing` (four sizing questions, plus convergence), the `astra mc size`
subcommand, and R9, the end-to-end planted cause in the notional transport with its negative control. It also closes
C-08, C-09 and N-04.

## Repository findings (read at `7e65a85`)

- **The pitching moment has a second α term.** `aero_math.hpp:140-150` sums `cm_alpha·α + cm_de·δe` and `cm_q·q`
  about AERORP, then transfers to the CG with `r_aero_b × F`. `cruise.yaml` places AERORP at `(0.3556, 0, -1.6256)` m
  in body axes, which is 0.36 m ahead of the CG and 1.63 m above it.
  - Linear estimate at cruise (qbar ≈ 10.9 kPa, CL ≈ 0.31, c̄ = 3.752 m): the x offset adds about
    `+cl_alpha·x/c̄ ≈ +0.41` per rad. The z offset adds about `-0.13` per rad.
  - So the total static derivative about the CG is about `cm_alpha_per_rad + 0.28`. The neutral point is near
    `cm_alpha_per_rad ≈ -0.28`, not at zero.
  - With `cm_q` and `Z_α` (short-period constant term `Z_α M_q / V - M_α ≈ 0.29 - 2.24 Cm_α,total`, all in s⁻²), the
    pitch divergence starts near `cm_alpha_per_rad ≈ -0.15`.
  - The M6 plan's finding ("dispersed across zero, static stability is lost") is therefore loose. A range across zero
    still crosses the boundary, as R9 requires. The probes measure the real boundary; this is a hand estimate only.
- The aero model is linear with no stall and no α domain check (`aero.cpp`). A divergent run keeps flying with a large
  α, and nothing halts it short of NaN or the atmosphere's altitude range.
- **Inert parameters exist.** `croll_aileron_per_rad`, `croll_rudder_per_rad` and `cn_rudder_per_rad` multiply
  `controls.aileron` and `controls.rudder`. Both are `0.0` in `cruise.yaml`'s `initial_controls`, and an open-loop run
  without `trim` never writes them. So these three coefficients can't change any value, by construction.
- `cruise.yaml` has a still-air trimmed state for `cm_alpha_per_rad = -0.6` and Dryden light turbulence with
  `seed: 42`. The first gust is about 6.6 m/s vertical.
- Logged per frame: `v0.derived.alpha` = `atan2(w_rel, u_rel)` with `v_rel_b = v_b - R_nb wind_n`
  (`air_data.hpp:22`), `v0.derived.{u,v,w,phi,theta,psi}`, and `wind.{north,east,down}`. Together these allow an
  independent recomputation of α. The summary's `max.v0.derived.alpha` comes from the logger's `/summary`.
- `astra.montecarlo` draws each dispersion in spec order from one `Rng` stream. A spec whose last dispersion is removed
  draws exactly the same values for the others. D3 relies on this.
- `astra.mc_analysis` (PR 6) provides `load_batch`, `outcomes`, `ks_ranking`, `spearman_ranking` and `analyze`.
  - Ranking order: p ascending, then |effect| descending, then spec order.
  - `MIN_GROUP = 5`.
  - A constant metric gives the stated result "the metric is constant".
  - Seed dispersions are listed but never ranked.
- `astra.cli` has a `_Parser` (a usage error exits 64), `_write`, and an `mc` group with `analyze` and `rerun`. Its
  exit codes are 64, 65, 70 and 74.
- **PyYAML.** `montecarlo` reads a spec and its config with `yaml.safe_load`, then `Config.from_dict`. PLAN.md's (M6)
  open item lists the YAML 1.1 differences from yaml-cpp: `on`/`off`/`yes`/`no`, leading-zero integers, `1:30` and
  `1_000`.
- Gate cost: the last full gate summary (`ffee1d7`) shows pytest at 1326 passed in 80.6 s. The machine has 32 cores.
  M5 measured 64 × 60 s cruise runs with logs of tens of MB each.

## Decisions

Approving the plan approves D2 to D9. Joe decides D1 at the probe checkpoint.

- **D1 (Joe, at the checkpoint): R9's numbers.** These are provisional, from the linear estimate above. The probes
  (Verification 2) confirm them or replace them, and they come back to Joe with evidence before the R9 batch is ever
  run.
  - `cm_alpha_per_rad`: `uniform [-0.8, +0.4]`. This crosses zero and the estimated boundary of about -0.15, with
    about 45 % of draws on the divergent side.
  - **Duration: 10 s.** At `cm_alpha_per_rad = +0.2` the divergent root is about 0.5 s⁻¹, an e-folding time of about
    2 s. The phugoid (period about 100 s) is not reached, so speed and altitude stay near cruise.
  - **Threshold: max α > 0.23 rad.** That is the end of the lift curve this config transcribes (`cl_alpha_per_rad` is
    the secant from (0, 0.20) to (0.23 rad, 1.20)). Above it the model is extrapolating. The criterion is
    `max.v0.derived.alpha > 0.23`.
  - **Minimum effect, stated before the run:**
    - KS `D ≥ 0.5`.
    - Spearman `rho ≥ +0.5`. The expected direction is positive: a less stable airframe reaches a higher maximum α.
    - Strict margin: rank 1's p is strictly below rank 2's, and its |effect| exceeds rank 2's by at least 0.25.
    - `cm_alpha_per_rad` is rejected by BH at q = 0.05, and no inert parameter is rejected.
  - **Run count (Joe's change 1): two options go to the checkpoint, each with its gate cost.** m is the number of
    ranked parameters derived from the spec (its non-`seed` dispersions), never a literal 4.
    - Size at the stated minimum, r = 0.5: `correlation_runs(0.5, m) = 40` per batch at m = 4. The "at least 20
      failing and 20 passing" allowance is lowered to fit (proposed: at least 8 of each, above `MIN_GROUP = 5`).
    - Or keep r = 0.3 (120 per batch), with a stated reason why the sizing effect sits below the asserted minimum (for
      example, margin for the ranking and KS power, which Fisher z does not size).
    - KS power and ranking power are argued separately (C-08), and the probes check both. With about 54 failing and
      66 passing runs, KS's critical D at α/m = 0.0125 is about 0.30, against an expected D near 0.9. The 40-run case
      gets the same calculation at the checkpoint.
  - **False-positive chance, stated before the run (Joe's change 7).** BH at q = 0.05 controls the family's error
    under independence, so a true null can still be flagged by chance:
    - in the control (three true nulls, global null), the chance that any is rejected is about q, so 5 %;
    - in the main batch (one true cause rejected first, three nulls), a null is rejected roughly when its p falls
      below about 2q/4 = 0.025, so the chance is about 7 to 8 %. The build computes the exact figure by simulation
      before the run, and the PR body states it.

    A failure of "no inert parameter rejected" at seed 707 is read against that chance: it is reported as such, not
    as a broken method, and never reseeded.
  - **Master seed: 707**, fixed now. The R9 batch is never run before Joe approves the numbers, and a failure is
    reported, never reseeded.
  - **Population and allowances:**
    - every one of the 120 rows is analyzed (zero `error` rows allowed: no draw can raise, since these coefficients
      are unvalidated and there is no trim);
    - at least 20 failing and 20 passing runs at 120 runs, or the lowered allowance at 40 (change 1);
    - halts are expected to be zero (a halted run's maximum covers only the frames it ran). The probes confirm this.
- **D2: one fixed trimmed state, no per-run trim.** Every run starts from `cruise.yaml`'s state, the still-air trim for
  `cm_alpha_per_rad = -0.6`.
  - Any other draw starts with a pitching-moment imbalance of `(cm_alpha + 0.6)·α_trim`. For draws above -0.6 that
    imbalance is nose-up, so every divergent run diverges nose-up. Maximum α is then the right one-sided metric, and
    the direction (`rho > 0`) is predictable.
  - Per-run trim would leave the direction of divergence to each run's turbulence. Roughly half the divergent runs
    would go nose-down, and maximum α would miss them.
  - The cause is still `cm_alpha_per_rad` alone. It contributes through both the imbalance and the loss of stability,
    and R9 tests the analysis, not a flight-dynamics claim.
- **D3: the dispersions, in spec order.**
  1. `croll_aileron_per_rad`, `croll_rudder_per_rad` and `cn_rudder_per_rad`, each uniform over ±50 % of its config
     value. All three are inert by construction (findings).
  2. `physical.wind.seed: seed`.
  3. `cm_alpha_per_rad`, **last**.

  - The negative control is the same spec with the last dispersion removed, so `cm_alpha_per_rad` stays at -0.6.
    Because the others are drawn first, the control's runs have **the same inert draws and wind seeds** as the main
    batch: the same batch, with only the cause removed.
  - The wind seed makes the control's maximum α vary from run to run, so its Spearman test is a real null test of
    three parameters against a non-constant metric. Without it, the test would pass vacuously on a constant metric.
  - The control's KS is expected to give the stated "too few runs" result (no stable run reaches 0.23 rad). The plan
    says so rather than counting it as evidence.
  - Putting `cm_alpha_per_rad` last also puts it last in the tie order, the hardest place to rank first from.
- **D4: Clopper-Pearson and its sizing (C-09).**
  - `clopper_pearson(k, n, confidence)` takes its bounds from beta quantiles:
    `L = beta.ppf((1-C)/2, k, n-k+1)` (0 at k = 0) and `U = beta.ppf((1+C)/2, k+1, n-k)` (1 at k = n).
  - The sizing objective is `half_width(n) = max(U - p̂, p̂ - L)` with `k = n·p0`, a real value. The half-width is the
    larger distance from the estimate, so the estimate is within h on both sides.
  - The beta quantiles accept a real k. That makes the objective smooth in n, and it equals the exact interval whenever
    `n·p0` is an integer. Rounding k would make it a sawtooth, where a larger n can fail again.
  - **What it does not promise (Joe's change 4).** n is sized at the expected count `n·p0`. The interval at an
    observed integer k can still exceed h. For example, p0 = 0.05 and h = 0.02 give n = 632, and at k =
    `round(n·p0)` the half-width exceeds h for n = 632 to 635. The docstring and the CLI's assumption line say so, and
    a test shows it.
  - `proportion_runs(p0, h, C)` returns the smallest n with `half_width(n) ≤ h`. The search evaluates n in doubling
    vectorized chunks up to a cap of 10⁷, and refuses beyond the cap. Domain: `p0 ∈ [0, 1]`, `h ∈ (0, 0.5)`,
    `C ∈ (0, 1)`.
- **D5: Wilks convention (C-09).**
  - `tolerance_runs(coverage γ, confidence C, two_sided=False, order k=1)` returns the smallest n with
    `P(Binomial(n, 1-γ) ≥ k·(2 if two_sided else 1)) ≥ C`.
  - Equivalently, the k-th largest sample (one-sided), or the k-th smallest and k-th largest (two-sided), bounds a γ
    fraction of the population with confidence C (distribution-free, continuous outcome).
  - First order (k = 1) is the sample extremes.
  - `zero_failure_runs(p, C)` = `ceil(ln(1-C)/ln(1-p))`, adjusted by one step until `(1-p)^n ≤ 1-C < (1-p)^(n-1)`
    holds through `log1p`, so a ceiling never lands one off on rounding. It is the same question as one-sided
    first-order Wilks at γ = 1 - p, and a test checks that they agree.
- **D6: Fisher z, signed r (N-04, C-09).**
  - `correlation_runs(r, parameters m, alpha=0.05, power=0.8)` =
    `ceil(((z_{1-α/(2m)} + z_{power}) / atanh(|r|))² + 3)`.
  - r is signed and only |r| enters, so ±r size equally. `r = 0`, `|r| ≥ 1`, a non-finite value, a bool, and `m < 1`
    or a non-integer m are refused.
  - The docstring and the CLI line state the limits:
    - Fisher z is exact asymptotically for Pearson's r under bivariate normality.
    - Applied to Spearman's rho it is an approximation. Spearman's z variance is nearer `1.06/(n-3)` (Fieller,
      Hartley and Pearson), so it can undersize slightly.
    - Bonferroni over m is conservative in m.
    - It does not size PRCC, KS or the ranking.
- **D7: convergence (R12).**
  - `convergence(batch, criterion=None, confidence=0.95)` uses PR 6's `outcomes` and the default criterion
    `status == halt`.
  - For each prefix length j = 1..N of the summary's rows in run order, it reports `n_j` (the analyzed rows),
    `k_j` (the failures), `errors_j`, the estimate `k_j/n_j`, and the Clopper-Pearson `[L, U]`.
  - A prefix with no analyzed row gives NaN for all three.
  - Only the failure proportion is reported. Metric convergence and a convergence figure are held out (PR 8 may draw
    one).
- **D8: CLI, `astra mc size <question>`.** Each question prints the run count, the inputs, and one line naming the
  method and its assumption.
  - The questions:
    - `zero-failure --p P [--confidence C]`
    - `tolerance --coverage G [--confidence C] [--two-sided] [--order K]`
    - `proportion --p P0 --half-width H [--confidence C]`
    - `correlation --r R --parameters M [--alpha A] [--power P]`
    - `convergence BATCH [--criterion EXPR] [--confidence C] [--every K]`, which prints the rows at multiples of K
      (default 10) plus the last row.
  - Defaults: C = 0.95, α = 0.05, power = 0.8.
  - Exit codes:
    - 64: a usage error, including a value outside its domain (PR 6's `--q 1.5` precedent);
    - 65: a refused batch or criterion;
    - 74: a failed stdout write;
    - 70: an internal error;
    - 0: success.
- **D9: gate time (Q15).**
  - The probes time a stand-in batch the same size as R9's (same n and duration, master seed 7070 instead of 707),
    plus its control, and record the wall time and disk use.
  - After the build, the PR records the added pytest time from `--durations` and the full gate's total against
    `ffee1d7`'s.
  - If the two batches add more than 60 s to the full gate, Joe chooses at the checkpoint: keep them in `just check`,
    or make them opt-in like the benchmark. Shortening below R11 (d)'s run count is not an option.
  - **Disk (Joe's change 2).** The probes and the R9 fixture record the peak disk use of both batches. The
    session-scoped fixture removes both batch directories with a finalizer that also runs when building or a test
    fails (M5 PR 7 amendment 1's pattern).
  - Making R9 opt-in removes neither R12's gate-run case nor R15's `convergence` CLI case, because both also run on
    PR 6's small `descent_batch.yaml` (change 3).

## Interface

`python/astra/mc_sizing.py` (new):
- `SizingError(ValueError)`, whose message names the argument.
- `zero_failure_runs`, `tolerance_runs`, `clopper_pearson`, `half_width`, `proportion_runs`, `correlation_runs`.
- `Convergence` (frozen): `criterion`, `confidence`, `runs`, `n`, `k`, `errors`, `estimate`, `lower`, `upper`.
- `convergence(batch, criterion=None, confidence=0.95)`, plus `Convergence.text(every)`.

`cli.py` gains the `size` group under `mc`, with one parser per question, and its docstring.

## Tests

Acceptance is `.venv/bin/pytest tests/python -k 'mc_sizing or planted'`. Every test name contains `mc_sizing` or
`planted`.

**`tests/python/test_mc_sizing.py`, every test named `test_mc_sizing_*`.**
- **R11 (a).**
  - p = 0.01, C = 0.95 gives 299.
  - On a grid of (p, C), each n meets its bound and n - 1 does not, computed directly.
  - It equals `tolerance_runs(1-p, C)`.
  - Refusals: p or C at 0 or 1, NaN, or a bool.
- **R11 (b).**
  - 95/95: 59 one-sided and 93 two-sided.
  - Higher orders against published Wilks tables: one-sided 93 at k = 2 and 124 at k = 3; two-sided 153 at k = 2.
  - Each n meets its bound and n - 1 does not, through the closed forms `1 - γⁿ` and
    `1 - γⁿ - n(1-γ)γⁿ⁻¹` for first order.
- **R11 (c).**
  - Intervals for (n, k) = (100, 0), (100, 5), (299, 0) and (1000, 20) equal
    `binomtest(k, n).proportion_ci(confidence_level=C, method="exact")` to 1e-12 absolute (two scipy routes, last
    bits). A seeded grid of (n, k) does the same.
  - The sizing at p0 = 0 has a closed-form oracle: `U = 1 - ((1-C)/2)^(1/n)`, so
    `n = ceil(ln((1-C)/2)/ln(1-h))`. For example, h = 0.03 and C = 0.95 give 122.
  - On a p0 grid, the half-width doesn't increase with n, and the returned n meets h while n - 1 does not.
  - Change 4: p0 = 0.05 and h = 0.02 give 632, and the exact interval at k = `round(n·p0)` exceeds h for each n in
    632..635 (checked through `binomtest`).
  - Refusals, and a cap that is refused naming it.
- **R11 (d).**
  - r = 0.3, α = 0.05, power 0.8 gives 85 at m = 1 (Cohen's table) and 120 at m = 4.
  - For m = 1..10, the n equals a recomputation through `statistics.NormalDist().inv_cdf` and `math.atanh`, which is
    stdlib and independent of scipy.
  - n never decreases as m grows; -0.3 gives the same n as 0.3; 0, ±1, 1.5 and NaN are refused.
- **R12** (Joe's change 3).
  - On a synthetic batch (`from_columns`) with interleaved `error` rows, every prefix's n, k, errors, estimate and
    interval equal a direct count plus `binomtest`'s exact interval for that prefix.
  - Gate-run on a small real batch: PR 6's `descent_batch.yaml` (24 runs of 0.5 s, with `ok`, `halt` and `error`
    rows), under its default `status == halt`, which there gives a nonzero k. The batch is built once per session in
    this module and removed by the same finalizer pattern.
  - On R9's batch, under **R9's criterion** `max.v0.derived.alpha > A`, not the default (which gives k = 0
    throughout), as `test_mc_sizing_r12_convergence_on_the_planted_batch`.
- **R15 (`mc size`).**
  - Each question's minimal case exits 0 and prints the expected n (299, 59, 122, 120) and the assumption line.
  - `convergence` on the descent batch exits 0 (change 3).
  - A missing option, an unknown option, `--p 1.5` and `--r 0` exit 64.
  - A missing or non-batch directory and a bad criterion exit 65.

**`tests/python/test_mc_planted.py`, every test named `test_mc_planted_*`.** Fixtures:
`tests/python/mc_planted_fixtures/planted.yaml` and `planted_control.yaml`, over `cruise.yaml` as shipped. Both
batches are built once per session into `tmp_path_factory`.
- **PyYAML route.**
  - The base config read through `montecarlo.load` (that is, `yaml.safe_load` then `Config.from_dict`) and through
    `Config.from_file` runs 1000 frames to equal rate-group data, compared exactly with `log.load`. The files differ
    only in the stored config text, so file bytes are not compared.
  - The spec's parsed values equal their intended literals.
  - A token scan of both files finds none of the open item's YAML 1.1 forms, nor an unsigned exponent such as `1.0e3`,
    which PyYAML reads as a string (lesson 2026-09-28; Joe's change 6).
  - This doesn't close PLAN.md's open item.
- **Inertness (D3).**
  - Two short runs that differ only in the three inert coefficients give exactly equal rate-group data.
  - This proves by test that the null family is null.
- **Sizing link.** `spec.runs == mc_sizing.correlation_runs(r, m)`, with r as decided at the checkpoint and m counted
  from the spec's non-`seed` dispersions (change 1).
- **Cleanup (change 2).** Both batch directories are removed by a finalizer that runs on failure too.
- **Population (C-08).** The D1 counts and allowances: errors, failing and passing runs, halts, and that the four
  ranked parameters are exactly the expected ones, with the seed listed.
- **The metric, recomputed (C-08).**
  - Per run, the summary's `max.v0.derived.alpha` equals the numpy maximum of the log's alpha column exactly.
  - At every frame, α recomputed from the logged `u, v, w`, the 3-2-1 Euler angles and `wind.*` (rotation and `atan2`
    in the test) matches the logged α to 1e-9 rad. The difference is wrapped to (-π, π] before it is compared,
    because divergent runs pass ±π within 10 s (Joe's change 5). The probes confirm this bound before it is fixed.
- **KS.** The planted parameter ranks first under criterion `max.v0.derived.alpha > A` with D1's minimum, margin and
  BH conditions.
- **Spearman.** The planted parameter ranks first against `max.v0.derived.alpha` with `rho > 0`, the minimum, the
  margin, and BH.
- **Negative control.**
  - The same three inert parameters (m = 3) and the same wind seeds (checked against the main batch's columns).
  - The metric is not constant: at least 100 distinct values.
  - Spearman's status is `ok` and no parameter is rejected.
  - KS gives its stated "too few" result or rejects nothing.

## Files

- New: `python/astra/mc_sizing.py`, `tests/python/test_mc_sizing.py`, `tests/python/test_mc_planted.py` and
  `tests/python/mc_planted_fixtures/` (two specs).
- Changed: `python/astra/cli.py` (`mc size` and its docstring).
- Docs (the `/wrap` docs step, branch scope):
  - architecture §12 (`astra.mc_sizing` moves from *planned* to built; the CLI bullet gains `mc size`);
  - `README.md` (CLI bullet and a short "Size a batch" how-to);
  - `PLAN.md` (the PR 7 line);
  - `docs/future-work.md`'s sizing line;
  - `docs/lessons.md` (the neutral point is not at `cm_alpha = 0`, so read the moment transfer);
  - STATE.md;
  - this plan, as `docs/plans/m6-pr7-mc-sizing.md`;
  - the review record and gate evidence.
- Not touched: `core/`, `plugins/`, `aircraft/` (`cruise.yaml` is used as shipped), goldens, tolerances, ADRs and
  `astra.mc_analysis`.

## Verification and process

1. **Setup.** On approval, enter the worktree `m6-pr7-mc-sizing` from `origin/main` `7e65a85`.
   - `mv` this draft into the worktree as `docs/plans/m6-pr7-mc-sizing.md` with its status updated. Commit it, push,
     and open the draft PR.
   - Check the trailer (`git log -1 --format='%(trailers:key=Co-authored-by)'`), and record the result in the PR body.
   - Build `.venv` and `astra._core` if the worktree lacks them.
2. **Probes, in scratch, nothing committed, before any R9 test.** None of them uses master seed 707.
   - (a) A sweep of `cm_alpha_per_rad` from -1.2 to +0.6 in steps of 0.05, from the fixed state, 20 s each, with wind
     seeds 42 and 4 others. It records max α at 5, 10 and 20 s, the time α crosses 0.23 rad, any halt or error, and
     the direction of divergence. It also checks D2's nose-up claim and finds the measured boundary.
   - (b) The α recomputation's worst disagreement.
   - (c) The PyYAML-route comparison.
   - (d) Stand-in batches and their controls at both candidate sizes (40 and 120 runs) with master seed 7070. Each
     records wall time, peak disk, failure fraction, KS D and rho. This measures power and cost without touching the
     R9 seed.
   - (e) The exact chance, by simulation, that an inert parameter is rejected under BH in the main batch's
     configuration (change 7).
   - `mc_sizing.correlation_runs` is built first, so that the run count used in the probes comes from the tool.
3. **Checkpoint to Joe.** The probe evidence and the proposed R9 numbers (range, duration, threshold, minimum effect,
   allowances), the two run-count options with their gate cost (change 1), the false-positive chances (change 7),
   D9's gate-time and disk figures, and, if needed, the keep-or-opt-in choice. The R9 fixtures and the asserted
   numbers are written only after his answer. Meanwhile, `mc_sizing`, the CLI and their tests proceed.
4. **Commits,** pushing after each: `mc_sizing` with its R11 tests; convergence with R12; `mc size` with R15; the R9
   fixtures and tests (after the checkpoint); docs.
5. **Checks.**
   - Acceptance: `.venv/bin/pytest tests/python -k 'mc_sizing or planted'`. Regressions:
     `-k 'mc_analysis or montecarlo or cli'`.
   - Then `.venv/bin/pyright`, `ruff check .`, `ruff format --check .`, `python3 scripts/docs_check.py` and
     `git diff --check`.
   - Record `--durations` for the R9 fixture.
6. **Full gate.** `ps` must first show no other ASTRA build or gate. Then run `rm -rf build && ./scripts/check-full.sh`
   with Bash `run_in_background`, and confirm exit 0 and `check: OK`. Commit the trimmed
   `docs/reviews/evidence/<head>-gate-summary.log` and record the gate's total time against `ffee1d7` (D9).
7. **`/review-cycle`.** A Codex review of the committed branch against `origin/main` on the final diff, at most two
   correction rounds, recorded in `docs/reviews/m6-pr7-mc-sizing-review.md`. It checks C-08, C-09 and N-04 against
   this plan.
8. **`/human-review` and `/wrap`.**
   - The PR body follows `docs/templates/pr-description.md`, with the probe evidence, the gate-time figure and the
     attribution check.
   - `/wrap` runs before merge, with the docs step in branch scope.
   - Guard refusals are rerun split or written with Write/Edit and noted in the PR body, never asked about.

Rollback: everything is additive. Reverting removes the module, the subcommand and the tests. R9's fixtures read
`cruise.yaml` as shipped, so a later change to that file (M7) can move R9. The test's failure message then names the
condition that failed.
