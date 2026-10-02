# M6 PR 6: Monte Carlo analysis (figures, `astra.mc_analysis`, `astra mc analyze`, `astra mc rerun`)

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29: the PR 6 row of "PR decomposition", "Monte Carlo
visualization" and "Outcomes and correlation" under "Contract and failure behavior", R5 to R8, R7b, R10, R15's
`mc analyze` and `mc rerun` cases, Q7, Q8, Q14, Q22, and "Obligations carried to PR plans" (PR 6: C-06, C-07, C-10,
N-06, from PR #78's per-finding notes); architecture §12's Monte Carlo figures, `astra.mc_analysis` and `astra.cli`
bullets.
Risk: **Tier 2** (Python feature; no new dependency, since scipy, matplotlib and pandas are already pinned). Base:
`origin/main` `8b4852b`. Worktree/branch: `worktree-m6-pr6-mc-analysis`. Touches no `core/`, `plugins/`, `icd/`,
`tests/golden/**`, tolerance file or ADR. Binding ADRs: 0025/0027 (logs read strictly through `astra.log.load`; a
rerun publishes through `close_log()`), 0002 (SI in summaries and logs), 0010 (`t = frame * dt`), 0011 (nothing
assumes one vehicle).
Parallel: M6 PR 9 runs in `m6-pr9-benchmark`. This PR stays out of the benchmark executable and its CMake and
justfile wiring. The shared files are PLAN.md (one PR line each), STATE.md and the docs `/wrap` touches. Whichever
PR merges second merges `origin/main`, rewrites STATE.md for the combined main and reruns the full gate.
Status: **approved by Joe, 2026-10-01, in plan mode, with his additions 1 to 8 folded in below.** They cover:
- the many-failing-runs legend policy (D8);
- the ranking order and the no-metric line (D6);
- the criterion's `status` atom and its test literals (D4, R10);
- rerun's render failure (D9);
- the attribution check's hash (Verification 1);
- the docs step's future-work and STATE lines (Files);
- one fixture batch per session (Tests).

Joe decided D1 and D2 in plan mode the same day.

## Context

PR 5 lets a user plot one run. A Monte Carlo batch needs more than that. A user wants to see the draws and outcomes,
see trajectory envelopes across the runs, find which dispersed parameters drive failures, and rerun a flagged run
with full logging. This PR adds those as `astra.mc_analysis` and two CLI subcommands. It also closes C-06, C-07,
C-10 and N-06, which the Codex critique of the M6 plan left partly open, and it takes PLAN.md's legend open item
(D1).

## Repository findings (read at `8b4852b`)

- `summary.parquet` (`python/astra/montecarlo.py:774`, `_write_summary`) has an explicit schema: `run`, `seed`,
  `status`, **then one column per dispersion in spec order** (float64, or int64 for a `seed` dispersion), then
  `error_type` ... `halt_vehicle`, then `health.*`, `final.*`, `min.*`/`max.*`. A dispersion key can never equal a
  fixed column (`_dispersions` refuses it). So a batch's dispersion columns are exactly those between `status` and
  `error_type`, and the seed dispersions are the int64 ones there.
- The batch directory holds only `summary.parquet` and `run_<i>.h5` for `ok` and `halt` rows; `error` rows leave no
  log. Nothing in it records the spec or the base config. That is why D2 is needed.
- The log's `config` attribute is the config text without the `log` block, and the origin is not stored
  (`run_log.cpp:393`; `log.py:214`). A run's log therefore does not depend on its directory. The existing 1-vs-4
  worker test (`test_montecarlo.py:139`) already relies on this.
- `run_one(spec, index, out_dir)` draws, trims, runs and returns the row; `draw(spec, index)` is pure
  (`montecarlo.py:276`). `load()` reads both files with `read_text` then `yaml.safe_load`, validates, and builds a
  `Simulation` to learn `dt` and the signal names.
- A log's rate group `/rate_<d>` holds frames `0, d, 2d, ...` below `frames_run` (cross-checked by `log.load`), so
  a halted run's rows are a prefix of a full run's.
- `astra.plot`: `pinned_style(package, figure)` (pinned rc, DejaVu Sans as paths, hash salt) and
  `ax.legend(loc="upper right")` (`plot.py:650`, the legend open item). The exclusive-create helper is `_create`,
  and the no-overwrite pre-check lives in `render`.
- `astra.cli`: argparse with `_Parser` (usage error = 64), plus subcommands `run`, `trim` and `plot`. Its codes are
  64, 65, 74 and 70, and stdout is written by `_write`.
- Fixtures: `tests/plugins/slow_rate` registers `slow_rate.value` at a configured divisor;
  `tests/configs/log_slow_rate.yaml` gives `/rate_1` and `/rate_10`. ISA 1976's valid range is -5000 m to 86000 m.
  The `domain` health action defaults to `halt`. A negative `physical.mass.mass` is a `ConfigError` (an `error`
  row, `test_montecarlo.py:170`).
- scipy 1.18.1 (`ks_2samp`, `spearmanr`, `rankdata`, `t`, `binom`, `false_discovery_control`); numpy is unpinned, so
  every synthetic draw uses `montecarlo.Rng` (a stable stream), never numpy's generators.

## Decisions

Joe decided D1 and D2 in plan mode. Approving the plan approves D3 to D9.

- **D1 (Joe): legends outside the axes.** One helper, `plot.legend_outside(ax, ncols=1)`, does
  `ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1.0), borderaxespad=0.0)` under the constrained layout. Every
  matplotlib legend uses it: plot packages and Monte Carlo figures. PLAN.md's open item closes. plotly is unchanged.
- **D2 (Joe): the batch records its spec.** This takes his refinements:
  - (a) `montecarlo.run` writes the spec file's text and the base config's text, **byte for byte as read**, into
    `summary.parquet`'s schema metadata. rerun parses them through the same `yaml.safe_load` route.
  - (b) The config's **file name only** is recorded, no path. The keys are `astra.montecarlo.spec`,
    `astra.montecarlo.config` and `astra.montecarlo.config_name`. The 1-vs-4 worker byte comparison of the summary
    stays as the regression.
  - (c) rerun recomputes the seed and draws from the recorded spec and checks them against the summary row before
    running. It compares the full row afterwards (N-06), and compares bytes with the original log when that log
    exists.
  - (d) The batch is **not** immune to a rebuild: plugins come from the current build. `mc rerun`'s help and the
    `montecarlo` docstring say a rebuild can change the result, and that the new log's provenance and the row
    comparison show it.
  - (e) A batch from before this PR, with no metadata, is refused with that reason. There is no `--spec` fallback.
  - (f) The docs step updates `montecarlo.py`'s `summary.parquet` docstring with the keys.
  - To serve this, `montecarlo.load` is split into file reading plus `loads(spec_text, config_text, *, name,
    config_name, origin)`, which holds every rule. `load` keeps its signature and behavior.
- **D3: what is analyzed.**
  - Parameters are the batch's dispersion columns (schema position, above).
  - Seed dispersions are listed and never ranked or plotted, because they are not ordered quantities. A parameter
    that is constant over the analyzed rows is listed as `constant` and leaves the family.
  - The analyzed rows are the non-`error` rows. `error` rows are counted separately and never fail. In the
    histogram and scatter they are drawn in their own color.
- **D4: outcome criterion grammar (closed).**
  - `expr := cmp (("and" | "or") cmp)*`, where `and` binds tighter than `or` and there are no parentheses.
  - `cmp := COLUMN OP NUMBER | "status" ("==" | "!=") ("ok" | "halt")`.
    - `OP` is one of `< <= > >= == !=`.
    - `NUMBER` is a finite decimal or exponent literal with an optional sign (`-0.5`, `1e-3`, `+2.0E+4`).
    - The `status` atom (Joe's addition 4) lets "halted or max.x > y" be written without knowing `N` for
      `frames_run < N`.
  - A column token is `[A-Za-z_][A-Za-z0-9_.]*`, so a dotted name with a numeric segment
    (`vehicle.models.0.force_x`, `max.v0.derived.alpha`) is one token.
  - `COLUMN` must be a numeric summary column. A dispersion is allowed, and so are `run`, `seed`, `frames_run`,
    `halt_frame`, `health.*`, `final.*`, `min.*` and `max.*`.
  - The default, with no criterion, is `status == halt`, the same atom.
  - Errors name the offending token: an unknown column, an unknown operator, a non-numeric value, a string
    column, or trailing or missing tokens.
  - A null or NaN in a referenced column on a non-`error` row is an error naming the column and the first such
    run. For example, `halt_frame` is null on `ok` rows, so `frames_run < N` is the way to say "halted".
  - The `--metric` column follows the same rules. It must not be a dispersion, `run` or `seed`.
- **D5: thresholds.** KS and the tree need at least `MIN_GROUP = 5` failing and 5 passing analyzed rows. Spearman
  needs at least 3 analyzed rows. PRCC needs `n >= m + 3`, so its t-test has at least 2 degrees of freedom. Below a
  threshold, the method's result is a stated `too few ...` line with no ranking and no rejection, never an error.
- **D6: significance.**
  - BH (step-up, in-house) runs at `q` (default 0.05) within each family: KS, Spearman and PRCC separately. Each
    row reports the raw p, the effect size, the BH decision and the family size `m`.
  - Effect sizes are the KS `D`, Spearman's rho and the PRCC coefficient.
  - KS uses `ks_2samp(..., method="auto")`, two-sided. Spearman's p comes from `spearmanr`. PRCC's p is a t-test
    with `df = n - 2 - (m - 1)`.
  - The dependence assumption, stated in the docstring: BH's guarantee needs independence or PRDS. The KS and
    Spearman statistics are independent conditional on the outcome or metric, because `montecarlo` draws each
    dispersion independently. PRCC residuals share every parameter, so its BH is approximate.
  - **Ranking order** (Joe's addition 2): p ascending, then |effect| descending, then spec order. p can tie at 0
    (an exact KS at full separation, or underflow), and PR 7's R9 ("ranks first with a strict margin") depends on
    this order.
  - **Without `--metric`** (Joe's addition 3), the Spearman and PRCC families print the stated line `no metric
    given: Spearman and PRCC not run`. They never rank against the 0/1 outcome.
  - The tree is descriptive: it has no p-value and reports leaf counts.
- **D7: the tree.**
  - Gini impurity, depth at most 3, with an exhaustive search over `GRID = 20`. Each parameter's thresholds are
    `lo + k(hi - lo)/20` for k = 1..19, with `[lo, hi]` taken over all analyzed rows. Splits are `x <= t` against
    `x > t`.
  - A split must leave at least `MIN_LEAF = 5` rows per child and strictly lower the impurity.
  - Ties go to the larger impurity decrease, then the earlier parameter in spec order, then the smaller threshold.
  - A leaf predicts `fail` only when its fails strictly outnumber its passes.
  - Rules are the `fail` leaves' paths, each simplified to the tightest bound per parameter and direction and
    printed as `p1 > 0.6 and p2 <= 0.3 (fail 58 of 58)`.
  - With no failures, the result is the stated `no failures: no tree`.
- **D8: figures.**
  - **Histograms.** One panel per ranked parameter, with bin edges `numpy.histogram_bin_edges(all rows' values,
    bins=20)`, stacked pass/fail/error.
  - **Scatter.** A lower-triangle scatter matrix of every ranked parameter pair, in fixed colors:
    `PASS = "#1f77b4"`, `FAIL = "#d62728"`, `ERROR = "#7f7f7f"` (marker `x`).
  - **Envelope.** One figure per `--signal`:
    - The panels are the band 5-95 %, the median, the min-max band, the failing runs overlaid as lines labeled
      `run <i>`, and a panel with the per-frame run count.
    - Legend policy for many failing runs (Joe's addition 1):
      - Every overlaid line carries the label `run <i>`, so R5's label check is unchanged.
      - The legend shows at most `MAX_LEGEND_RUNS = 10` failing runs, the lowest indices.
      - Beyond that, one more entry reads `and k more`. It is a proxy artist that draws no data.
    - Alignment is by frame number in the signal's rate group, with x = `frame * dt`. A run contributes at a frame
      iff its log has that row, with no interpolation and no padding.
    - Percentiles are `numpy.percentile` (linear), taken over contiguous segments of equal population. Halted runs
      are prefixes, so each segment's array holds exactly the runs that reached it.
  - **Refusals.** Each is an error naming the run and the reason: a missing log for an `ok` or `halt` row, a log
    whose `dt` or whose signal divisor differs from the first log's, or an unknown signal.
  - **Files.** The names are `mc-hist`, `mc-scatter` and `mc-envelope-<signal>`, SVG by default and PNG on request,
    under `plot.pinned_style("mc", <figure>)`. Writes follow `plot.render`'s rules: everything is resolved and every
    target checked before anything is written, each file is created exclusively, and a failed write deletes only
    the files this call created.
  - **Memory.** Logs are loaded one at a time, and only the signal column is kept.
- **D9: CLI.**
  - **`astra mc analyze <batch> [--criterion EXPR] [--metric COLUMN] [--signal NAME]... [--q Q] [--out DIR]
    [--format svg|png]`.** It prints the analysis (outcome counts, each family's table, the tree's rules, the paths
    written) to stdout and writes the figures to DIR (default `.`; never into the batch). Codes:
    - 64: a usage error, or a `q` outside (0, 1).
    - 65: the batch, criterion, metric or signal is refused, a log is missing or refused, or an output exists.
    - 74: a write or stdout fails.
    - 70: an internal error.
    - 0: success.
  - **`astra mc rerun <batch> <run> --out DIR [--package P]...`.**
    - DIR is created if missing and must be empty. The default packages are the framework `flight` and `health`.
    - It reads the metadata, refusing a batch without it (65). The run must be in the summary and not an `error`
      row (65).
    - It recomputes `draw()` and checks the seed and every draw against the row (65 on a mismatch, naming the
      column), then calls `run_one(spec, run, DIR)`.
    - It then compares every column of the new row with the recorded one, NaN equal to NaN. When `<batch>/run_<i>.h5`
      exists, it also compares bytes. It renders the packages from the new log and prints the comparison.
    - Codes: 0 reproduced; 1 not reproduced, with the columns that differ and the log kept; 64; 65; 74 (a failed
      write); 70.
    - Render failure (Joe's addition 5):
      - The reproduction result is printed to stdout **before** rendering.
      - If rendering then fails, the log stays and stderr names the package and the reason. The code is 65 for a
        refused package (`PlotPackageError`, or an existing output) and 74 for a failed write.
      - A non-reproduction (1) takes precedence over a render failure, because the comparison is what the command
        is for.
    - Package check: the dry run confirms that the framework `flight` and `health` packages render against the
      fixture config. That config has no aero model, so `flight`'s derived signals may be missing. If they are,
      the default becomes the packages that apply, and the PR body says which.

## Interface (`python/astra/mc_analysis.py`, new; additions to `montecarlo.py`, `plot.py`, `cli.py`)

- `Batch` (frozen): `path`, `summary`, `parameters` (ranked), `seed_parameters`, `dt`. `load_batch(dir)` reads
  `summary.parquet` with pyarrow and takes the parameters from the schema. `from_frame(summary, parameters,
  seed_parameters=())` builds a synthetic batch for tests.
- `Criterion`: `parse_criterion(text) -> Criterion` and `DEFAULT`. `outcomes(batch, criterion) -> Outcomes`, the
  pass, fail and error run indices. `metric(batch, column)`.
- `benjamini_hochberg(p, q) -> list[bool]`. `ks_ranking`, `spearman_ranking` and `prcc_ranking` return a `Ranking`
  (rows of name, effect, p, rejected; `m`; `excluded` with each reason; `status`).
- `fit_tree(batch, outcomes) -> Tree`, with `predict(values)`, `depth` and `rules()`.
- `analyze(batch, criterion=None, metric=None, q=0.05) -> Analysis`, plus `Analysis.text()` (deterministic).
- `histogram_data`, `scatter_data` and `envelope(batch, signal, outcomes) -> Envelope` (`frames`, `t`, `count`,
  `percentiles`, `minimum`, `maximum`, `failing: {run: (t, y)}`). `render_figures(batch, outcomes, signals,
  out_dir, fmt) -> list[Path]` returns the drawn matplotlib figures to tests through `draw_*` functions.
- `montecarlo.loads(...)`, `montecarlo.batch_spec(dir) -> Spec`, which reads the metadata (D2), and
  `montecarlo.rerun(batch_dir, run, out_dir) -> RerunResult` (`row`, `recorded`, `differences`,
  `log_identical: bool | None`).
- `plot.legend_outside`. `plot._create` is promoted to `plot.create_exclusive` for reuse.

## Tests (`tests/python/test_mc_analysis.py`; acceptance `.venv/bin/pytest tests/python -k mc_analysis`)

Every test is named `test_mc_analysis_*`, so `-k mc_analysis` selects all of them. Synthetic draws use
`montecarlo.Rng` with stated seeds. The fixture batch is built once per session, as a session-scoped fixture, and
each destructive rerun test copies it into its own temporary directory first (Joe's addition 8).

- **Fixture batch (C-10).** `tests/python/mc_analysis_fixtures/` holds a config derived from `log_slow_rate.yaml`
  and a spec over it.
  - The config's initial altitude is near ISA's -5000 m floor, which makes the domain check halt.
  - The spec disperses `initial.state.v_b.2` (descent rate; the faster draws cross the floor at different frames),
    `physical.mass.mass` (a range dipping below zero, which gives `error` rows), `vehicle.models.0.force_x`
    (irrelevant) and `physical.wind.seed: seed`. It runs about 24 runs of 0.5 s with signals `v0.derived.altitude`
    (divisor 1) and `slow_rate.value` (divisor 10).
  - The builder confirms the bands with a dry run before fixing the numbers. The test asserts the fixture's
    properties instead of assuming them: at least one `ok`, one `error` and two halts at distinct frames.
- **R5 and C-10.** These are checked through the drawn matplotlib artists against independent computations from the
  summary and from `log.load` per run:
  - stacked bar heights equal `numpy.histogram` per outcome on the stated edges;
  - each scatter collection's offsets are exactly the rows of its outcome, with the documented facecolor;
  - for both signals (divisors 1 and 10), at every frame the band and median lines equal `numpy.percentile` over
    exactly the runs whose log holds that frame, and the count line equals that number;
  - each failing run is a line labeled `run <i>` whose data equals its log column, and no passing or error run is
    overlaid;
  - a custom criterion changes the failing set and overlays to match.

  Refusals: a deleted `ok` log, a mismatched `dt` or divisor (a hand-built batch) and an unknown signal each give an
  error naming the run or the signal. Cross-process byte-identity of `mc-hist.svg` is also checked (C-12's rule).
- **R6 and C-06.**
  - Synthetic data: n = 300, six uniform parameters, and failure iff `p1 > 0.8`. p1 ranks first with `D == 1.0`
    exactly, since the classes are separated. It survives BH at q = 0.05, and no other parameter survives.
  - BH oracle vectors, hand-computed:
    - `[0.01, 0.02, 0.03, 0.04, 0.05]` at q = 0.05: BH rejects 5, Bonferroni 1, reject-none 0.
    - `[0.01, 0.03, 0.035, 0.04]`: step-up rejects 4, a step-down 1.
    - `[0.001, 0.04, 0.045, 0.3]`: BH 1, uncorrected 3.
  - Also: agreement with `scipy.stats.false_discovery_control` on seeded random vectors, an input of unsorted
    order, and ties.
  - The ranking order (D6): a constructed p tie at 0 ranks by |effect|, and an exact tie in both ranks by spec
    order.
- **R8 and C-06 (global-null calibration).**
  - K = 200 synthetic batches, with seeds `run_seed(2026, k)`. Each has n = 200, five independent uniform
    parameters, and failure Bernoulli(0.2) drawn independently from the same `Rng`. The metric for the Spearman and
    PRCC families is an independent uniform.
  - For each family (KS, Spearman, PRCC), the count of batches with any BH rejection at q = 0.05 must be at most
    `scipy.stats.binom.ppf(0.99, 200, 0.05)`.
  - R6's power requirement is the pair: a correction that never rejects fails R6.
- **R7 and C-07 (tree).**
  - Failure iff `p1 > 0.6 and p2 < 0.3`, on n = 400 with 4 more irrelevant parameters. The single fail rule names
    p1 (`>`) and p2 (`<=`), with thresholds within one grid step of 0.6 and 0.3.
  - Predictions are correct on a 21 x 21 held-out mesh in all four quadrants, excluding points within one grid step
    of a boundary. A disjunction fails this.
  - Depth is at most 3 on a target that needs 4.
  - Tie rules: a duplicated column splits on the earlier parameter, and equal-gain thresholds take the smaller one.
    A leaf with fails equal to passes predicts pass.
  - `MIN_LEAF` holds, and a batch with no failures gives the stated result.
- **R7b and C-07 (PRCC).**
  - Construction: p1 uniform, `p2 = p1 + 0.1 u2`, `y = p1 + 0.1 u3`, with n = 500.
  - The bounds come from the population values: rho(p2, y) is about 0.99, and the partial rho(p2, y | p1) is 0.
    Its sampling SE is about 1/sqrt(n - 3), roughly 0.045.
  - Assertions: Spearman(p2) > 0.9, |PRCC(p2)| < 0.2 (over 4 SE) and PRCC(p1) > 0.5. Plain Spearman returned as
    PRCC fails.
  - Coefficient oracles:
    - Spearman on a hand-ranked tied vector, with Pearson over literal average ranks, independent of `rankdata`;
    - a monotone decreasing input gives exactly -1;
    - PRCC equals `-P_jy / sqrt(P_jj P_yy)` with P the inverse of the rank-correlation matrix (m = 3), and the
      closed-form first-order partial for m = 2, to 1e-12.
  - Stated results: a constant parameter is listed as excluded, `p2 = 2 p1` gives `collinear` for both in PRCC
    while Spearman still reports, and too few rows gives the stated line.
- **R10.** An unknown column, an unknown operator (`=`, `=>`), a non-numeric value, a string column, a dangling
  `and`, a `status` value other than `ok` or `halt`, and a null or NaN in a referenced column each give an error
  naming the token. Accepted: a negative number, an exponent, dotted columns with a numeric segment
  (`vehicle.models.0.force_x`, `max.v0.derived.alpha`), and `status == halt or max.x > y`. `error` rows are counted
  separately and never in fail or pass. `and` binds tighter than `or`, checked on rows that tell the two readings
  apart.
- **R15 (`mc analyze`).** A minimal case on the fixture batch exits 0, prints the counts and tables, and writes the
  figures. A bad option, `--q 1.5` and a missing batch argument exit 64. A bad criterion exits 65, and an existing
  output exits 65.
- **R15 (`mc rerun`) and N-06.**
  1. On the fixture batch, take a halted run. Hash every file in the batch, copy its log to the test's own temp
     area, and **delete every `run_*.h5` from the batch**.
  2. `astra mc rerun` into a fresh DIR exits 0. The new `run_<i>.h5` loads strictly (`complete`, published) and is
     **byte-identical to the saved original**. Its stored config's dispersed leaves equal the summary's draws,
     checked independently through `_runkit.numeric_leaf`. The recomputed row equals the recorded one, and the
     package figures exist.
  3. The batch's remaining files hash as before.
  4. Refusals:
     - rerunning with the original log present also reports `log_identical`;
     - a non-empty DIR gives 65;
     - an `error` row gives 65;
     - a run index out of range gives 65;
     - a summary with the metadata stripped (a pre-PR batch) gives 65, naming the reason;
     - a summary whose recorded draw was hand-edited gives 65, naming the column;
     - a monkeypatched `run_one` returning a changed value gives 1, naming the column;
     - a bad argument gives 64.
- **D2 regression.** In `test_montecarlo.py`, the existing 1-vs-4 byte comparison stays. One added test reads the
  three metadata keys and checks them against the spec and config files' bytes and the bare file name.
- **D1.** In `test_plot.py`, the existing legend-text assertion stays. One added check is that each legend's
  bounding box lies outside its axes.

## Files

- New: `python/astra/mc_analysis.py`, `tests/python/test_mc_analysis.py` and `tests/python/mc_analysis_fixtures/`
  (a config and a spec).
- Changed:
  - `python/astra/montecarlo.py` (`loads`, metadata, `batch_spec`, `rerun`, docstring per D2 (d) and (f));
  - `python/astra/plot.py` (`legend_outside`, `create_exclusive`);
  - `python/astra/cli.py` (the `mc` group with `analyze` and `rerun`, and the docstring);
  - `tests/python/test_montecarlo.py` and `tests/python/test_plot.py` (one test each).
- Docs (the `/wrap` docs step, branch scope):
  - architecture §12, where the Monte Carlo figures and `astra.mc_analysis` move from *planned* to built and the
    CLI bullet gains `mc analyze` and `mc rerun`;
  - `README.md` (CLI bullet, plus a short "Analyze a batch" how-to);
  - PLAN.md (the PR 6 line, and the legend open item removed);
  - `docs/future-work.md` (its "Monte Carlo figures (PR 6) ... remain planned" line);
  - STATE.md (drop "legends over data" from the plot open-items thread);
  - this plan, renamed to `docs/plans/m6-pr6-mc-analysis.md`;
  - the review record and gate evidence.
- Not touched: anything of PR 9's (the benchmark executable, its CMake and justfile wiring), `core/`, `plugins/`,
  goldens, tolerances and ADRs.

## Verification and process

1. On approval, enter the worktree `m6-pr6-mc-analysis` (from `origin/main` `8b4852b`). `mv` this draft into the
   worktree's `docs/plans/` as `m6-pr6-mc-analysis.md`, with its status line updated, and leave nothing untracked in
   the main checkout. Commit it as the first commit and push.
   - Check `git log -1 --format='%(trailers:key=Co-authored-by)'` and record the result in the PR body next to the
     commit hash, confirming the hash is not `8b4852b`. If the hook refused the commit, HEAD would still be main,
     whose trailer is already empty. Open the draft PR at that commit.
   - Run `just venv`/`just python` if the worktree lacks `.venv`, and build `astra._core` and the test plugins.
2. Build in coherent commits, pushing after each, in this order: `plot.legend_outside`/`create_exclusive`;
   montecarlo `loads` and metadata; `mc_analysis` statistics and their tests; figures and their tests; CLI
   `mc analyze`/`mc rerun` with rerun tests; docs.
3. Run `.venv/bin/pytest tests/python -k mc_analysis` (acceptance), then `-k 'montecarlo or plot or cli'` (the
   regressions), `.venv/bin/pyright`, `ruff check .`, `ruff format --check .`, `python3 scripts/docs_check.py` and
   `git diff --check`.
4. Full gate:
   - First, `ps` must show no other ASTRA build or gate (PR 9's worktree included). Then run
     `rm -rf build && ./scripts/check-full.sh` with Bash `run_in_background` only, and read its exit status and
     `check: OK`. A timing test failing while another session builds is rerun alone before it counts.
   - Commit the trimmed `docs/reviews/evidence/<head>-gate-summary.log`.
5. `/review-cycle`: a Codex review of the committed branch (`--base origin/main`) on the final diff, at most two
   correction rounds, recorded in `docs/reviews/m6-pr6-mc-analysis-review.md`. The review checks C-06, C-07, C-10
   and N-06 against this plan.
6. `/human-review`: the PR body per `docs/templates/pr-description.md`, with Docs impact and the attribution-check
   result. "Needs Joe's decision" lists D3 to D9 as approved with the plan, plus the architecture edits.
7. `/wrap` before merge, with its docs step in branch scope. Close STATE.md's attribution thread if the trailer
   check was empty. If PR 9 merged first, merge `origin/main`, rewrite STATE.md for the combined main and rerun the
   full gate. Guard refusals are rerun split or written with Write/Edit, and noted in the PR body, never asked
   about.

Rollback: everything is additive except the legend placement (revert `legend_outside`) and the summary metadata.
Readers ignore unknown parquet metadata, so reverting it affects only rerun. No plan critique: the milestone plan
was critiqued and Q23 assigns PR-level detail to this PR's Codex review.
