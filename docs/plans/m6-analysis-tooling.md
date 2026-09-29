# M6 Analysis tooling: spec, plan and PR decomposition

Status: **draft, for Joe's review** (written in the Cowork planning session, 2026-09-29)
Owner: Joe
Risk: 3 overall. PR 2 changes run-log finalization in `core/` (determinism and a health-adjacent output path);
the rest are Tier 2 or lower. Each PR's tier is in the decomposition table.
Intent/approval: Joe's M6 scope in [PLAN.md](../../PLAN.md) (mapped 2026-09-29 from
[future work](../future-work.md)): plotting with reusable plot packages, Monte Carlo visualization,
failure-to-draw correlation, Monte Carlo sizing, a run report, a frame-time benchmark, `astra run --strict`,
and the superseding ADR to ADR-0025's Finalization. Spec and plan approval: pending. Approval of the plan does
not approve any later PR's diff.
Base: `origin/main` = `55a0bc3`.
Architecture/ADRs: [architecture](../architecture.md) §1 (visualization is claimed), §9 (logger, replay), §12
(Python layer), §15 (post-run summary, `astra run --strict`), §16 (a new M6 row), §18 (the on-ramp names
`astra trim` and `astra run`);
[ADR-0002](../decisions/0002-si-units-and-signal-metadata.md) (SI inside, conversion at boundaries),
[ADR-0005](../decisions/0005-hdf5-c-api-logging.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md),
[ADR-0011](../decisions/0011-one-vehicle-as-list-of-one.md),
[ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md),
[ADR-0026](../decisions/0026-derived-flight-condition-signals.md). Proposed: ADR-0027, superseding ADR-0025's
Finalization section (Q1, Q13).
System context: every M6 tool is a consumer of what M5 produces. A run log (ADR-0025) is read through
`astra.log.load`; a Monte Carlo batch is `summary.parquet` plus one log per completed run
(`astra.montecarlo`); a scenario is a YAML file plus its run's log (`astra.scenario`). M6 adds readers
(plots, analysis, report), a front door (`astra` CLI), a measurement (the benchmark), and one change on the
producer side: how a log is finalized (ADR-0027). Nothing in M6 changes `step()`.

## Outcome and boundaries

**Outcome.** A user can, from one command or one Python call:
- plot any run log with a named plot package, written once and reused across runs and aircraft;
- see a Monte Carlo batch: the draws, the outcomes, and trajectory envelopes;
- ask which dispersed parameters drive failures (or a continuous margin), including a two-parameter
  interaction, with the false-positive rate controlled;
- size a Monte Carlo before running it, from the question asked and the number of dispersed parameters,
  and check convergence after it;
- produce a self-contained report of a run, a scenario or a batch, with provenance;
- run a config with `astra run [--strict]` and get §15's exit behavior;
- measure the per-frame cost and the margin against `dt`.
And a killed or failed run never leaves a file at the final log path, while its partial log stays readable.

**Non-goals.** Closed-loop (lockstep) scenarios, Monte Carlo and analysis (M10); `astra run` of a config with a
`transport` block (refused with a reason; M10); an interactive plotting UI or plotting a live run; PDF output;
variance-based sensitivity (Sobol) or adaptive sampling, which need designed sample sets rather than an
existing batch; `RealTime` mode and tracking the benchmark over time (M11); §15 envelope monitors (M7); any
change to `step()`, `icd/`, `tests/golden/**`, tolerance files, or an accepted ADR other than superseding
ADR-0025's Finalization through ADR-0027.

**Invariants.** A config without a `log` block behaves bit-identically to `55a0bc3` (M3's frozen regression).
Two runs of one config still give byte-identical logs, and `astra.log.replay` still reproduces a log byte for
byte (M5 R4, R9), under ADR-0027's finalization. Logs and summaries stay SI; unit conversion happens only for
display (ADR-0002). `t = frame * dt` is derived (ADR-0010). No wall-clock read in `core/`, `plugins/`,
`aircraft/` or `embedded/`; Python tools and test executables may measure wall time and print it, never store
it in a log or summary (ADR-0025). Nothing assumes one vehicle (ADR-0011). Tools read their inputs and never
modify them. Analysis results are a deterministic function of their inputs.

**Assumptions.** M5's log, replay, scenario and Monte Carlo behavior is correct as shipped and is not
re-validated here beyond the invariants above. Same-build, same-machine determinism only. The statistics
answer questions about the batch that was run; they do not validate the models.

## Repository findings (observed at `55a0bc3`)

- `astra.log.load` returns a `RunLog`: `rates` (divisor to a DataFrame indexed by `frame`, first column `t`),
  `units`, `frames_run`, `halt`, `writes`, `health`, `minimum`, `maximum`, and the final state. It refuses a
  file without `complete = 1` unless `allow_incomplete=True`. Its docstring records the gap ADR-0027 closes: a
  `Simulation` dropped mid-run is finalized by its destructor with `complete = 1`, and `load` cannot tell.
- `astra.montecarlo` writes `summary.parquet`, one row per run: `run`, `seed`, `status` (`ok`, `halt`,
  `error`), one column per dispersion path, `error_type`, `error_message`, `frames_run`, `halt_*`,
  `health.<counter>`, `final.<state>`, and `min.<signal>`/`max.<signal>` for the spec's `signals`. Logs of
  `ok` and `halt` runs are kept (`run_NNNN.h5`); a killed worker's partial file is deleted because h5py cannot
  read it. Open loop only; a `transport` config is refused. `run` prints wall time and the real-time factor.
  The 64-run case (`tests/python/fixtures/cruise_64.yaml`) runs under `just check`.
- `astra.scenario`: YAML with `checks` (`within`, `between`, `angle_within` over a window) evaluated on the
  run's own log, collected by `tests/scenarios/conftest.py`.
- No CLI exists: `pyproject.toml` has no `[project.scripts]`. §15 specifies `astra run`'s strict exit rule and
  §18 names `astra trim` and `astra run`.
- Dependencies: runtime `numpy`; the dev group pins `scipy`, `pyyaml`, `h5py`, `pandas`, `pyarrow`. No plotting
  library. M4's trace figures were SVG from a scratch script (`docs/reviews/m4-pr6-lockstep-acceptance-human.md`).
- The wall-clock scan covers the objects of `core/`, `plugins/`, `aircraft/` and `embedded/`, not `tests/`.
- `aircraft/_template/` is copied whole by `just new-aircraft`, so a `plots/` directory placed there reaches every
  new aircraft with no framework edit (§18; `tests/python/test_new_aircraft_build.py`).
- The notional transport's aero config carries `cm_alpha_per_rad: -0.6`, a physically meaningful parameter to
  plant as a cause (dispersed across zero, static stability is lost).
- Gate cost today: `./scripts/check-full.sh` took 1001 s from a fresh configure on `cb2853c`; pytest 473 tests in
  59.3 s.

## Contract and failure behavior

**Plot packages (`astra.plot`).** A package is a YAML file: figures, each with panels, each with series. A
series names a logged signal, optionally with `{v}` for the vehicle index (expanded for every vehicle in the
log, so a package never hardcodes `v0`), and an optional display unit. Display conversion uses a fixed table
keyed on the log's own unit string (for example `rad` to `deg`, `m` to `ft`, `m/s` to `kt`); an incompatible
conversion is an error. Every map is closed: an unknown key, an unknown signal, a bad conversion or an empty
figure is an error naming the package file and the key, before anything is drawn. A package may instead name a
Python function (module and name) that takes a `RunLog` and returns figures, for plots YAML cannot express.
Framework packages live with `astra.plot`; an aircraft's own live in `aircraft/<name>/plots/` (the template
carries a starter set). Two renderers take the same package (Q2): matplotlib writes one file per figure, SVG by
default and PNG on request, and the same log and package give byte-identical SVG; plotly writes one
interactive HTML file per package for exploring a run (zoom, hover), with plotly.js embedded so it opens offline.
The report uses the matplotlib SVG only.

**Monte Carlo visualization.** From a batch directory: a histogram of each dispersed parameter's draws; the
draws against each other colored by outcome; and, for a named signal, the trajectory envelope (median and
chosen percentile bands at each frame) across the batch's run logs, with any failing runs overlaid.

**Outcomes and correlation (`astra.mc_analysis`).** The outcome is either a pass/fail criterion or a continuous
metric. The default criterion is `status == halt`. A custom criterion is a closed expression over summary
columns (a column, a comparison, a number, joined by `and`/`or`), never evaluated Python. A continuous metric is
any numeric summary column. `error` rows are reported separately and are never counted as failures. Integer
`seed` dispersions are not ordered quantities; they are listed but excluded from rank-based methods. Methods:
- per parameter, a two-sample Kolmogorov-Smirnov test of passing against failing draws, with the
  Benjamini-Hochberg procedure across parameters at a stated false-discovery rate;
- per parameter, Spearman rank correlation, and partial rank correlation (PRCC), against a continuous metric;
- a shallow classification tree (depth at most 3) to surface interactions as readable threshold rules.
Every ranking reports effect sizes with the p-values and says how many parameters were tested. Too few failures
for a method is a stated result, not an error.

**Sizing and convergence (`astra.mc_sizing`).** Before a batch: the number of runs for (a) demonstrating a
failure probability below p with confidence C when no failure is seen, n = ceil(ln(1 - C) / ln(1 - p));
(b) a percentile tolerance bound by order statistics (Wilks); (c) a probability estimate within a stated
half-width (Clopper-Pearson); (d) detecting a rank correlation of a stated size with stated power across m
dispersed parameters (Fisher z with a Bonferroni split of alpha over m). After a batch: the estimate and its
interval as a function of run count, so a user sees whether it has settled.

**Run report (`astra.report`).** For a log, a scenario, or a batch: one self-contained HTML file (inline SVG,
no script and no external reference) with the plot package's figures, the summary (frames run, halt, health
counters), scenario check results where they apply, the batch's outcome counts, sizing check and correlation
ranking where they apply, and provenance read from the log (config text, git describe, core version and API,
`dt`, the plugin table with SHA-256). The same inputs give a byte-identical file.

**CLI (`astra`).** One console script with subcommands `run`, `trim`, `plot`, `report` and `mc` (`size`,
`analyze`). `astra run <config>` runs `sim.frames` frames open loop (an optional `--log` path adds the `log`
block), prints §15's post-run summary plus wall time and real-time factor to stdout, and exits by Q12's codes;
`--strict` makes a nonzero `warn` counter a failure. A config with a `transport` block is refused with a named
reason.

**Benchmark.** A test executable, built in Release, that times each `step()` over named configs with a steady
clock and writes JSON: mean, median, 99th percentile and maximum frame cost, frames per second, the margin
against `dt`, and the toolchain and CPU. The gate runs a short smoke of it; it asserts nothing about speed.

**Finalization (ADR-0027, superseding ADR-0025's Finalization).** Open item points a to d:
(a) the log records how it was finalized (`close_log()` or the destructor) and `load` flags a
destructor-finalized log; (b) rename on complete: the file is written under a temporary name and renamed to
`log.path` only after a successful close, so a file at the final path is complete by construction and a killed
run leaves none; (c) crash-readable: HDF5 metadata is flushed at each drain, so a killed run's temporary file
opens with `allow_incomplete=True`; (d) `load`'s cross-checks: `final_state.frame == frames_run`, and each rate
group's signal names against `/summary`'s. Byte identity and replay must survive (b) and (c); Q13.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | A package renders a scenario log | pytest: every figure is written; each plotted series' data equals the log column after the display conversion, exactly; axis labels carry the display unit |
| R2 | Packages fail loudly | Negative fixtures: unknown key, unknown signal, incompatible conversion, empty figure, a `{v}` series against a log without that vehicle: each an error naming the file and the key, raised before any file is written |
| R3 | Deterministic figures | Same log and package, two output directories: byte-identical SVG |
| R3b | Interactive output | The plotly renderer's figure data for every series equals the log column after conversion, exactly (compared through the figure's dict, not the file's bytes); the HTML holds plotly.js inline and no external `src` or `http` reference |
| R4 | Aircraft packages travel | A copy made by `just new-aircraft` renders its `plots/` starter set against its own log with no framework edit (the throwaway-copy pattern of `test_new_aircraft_build.py`) |
| R5 | Monte Carlo figures are right | Histogram counts equal the summary's per-bin counts; envelope percentiles at each frame equal `numpy.percentile` over the runs' columns |
| R6 | Single planted cause | Synthetic summary (no sim, fixed seed): failure iff one parameter exceeds a threshold, with 5 irrelevant parameters. KS ranks it first; no irrelevant parameter survives Benjamini-Hochberg at q = 0.05 |
| R7 | Planted interaction | Synthetic: failure iff p1 > a and p2 < b. The tree's rule names p1 and p2 with thresholds within one split-grid step of a and b |
| R8 | False positives controlled | Synthetic null: failures independent of every parameter, over K seeded batches; the fraction of batches flagging any parameter is at most q plus a stated binomial allowance |
| R9 | Planted cause in the real sim | A notional-transport batch with `cm_alpha_per_rad` dispersed across zero and at least three irrelevant dispersions; against the maximum angle of attack (or the criterion Joe approves), `cm_alpha_per_rad` ranks first. Run count from R11's tool; criterion and range come back to Joe with evidence (Q9) |
| R10 | Outcome criteria are closed | An unknown column, an unknown operator or a non-numeric value is an error naming it; `error` rows are counted separately |
| R11 | Sizing formulas | Closed-form oracles: p = 0.01, C = 0.95 gives 299 runs; Wilks one-sided 95/95 gives 59, two-sided gives 93; Clopper-Pearson intervals equal `scipy.stats.binomtest(...).proportion_ci(method="exact")`; correlation r = 0.3, alpha = 0.05 two-sided, power 0.8, m = 1 gives 85 (Cohen's table), and n grows with m |
| R12 | Convergence | On a batch, the reported estimate and interval at each prefix length equal a direct computation on that prefix |
| R13 | Report | For a log, a scenario and a batch: one HTML file containing no `<script`, no `http`, and no external `src`; provenance fields equal the log's attributes; byte-identical for the same inputs |
| R14 | `astra run` exits per §15 | A clean config exits 0; a halting config (for example `tests/configs/health_nan_wrench.yaml`) exits nonzero and prints the check, source and frame; `--strict` with a nonzero `warn` counter exits nonzero, and without `--strict` exits 0; a `transport` config is refused naming `transport` |
| R15 | Other subcommands | `trim`, `plot`, `report`, `mc size`, `mc analyze` each run a minimal case and reject a bad argument with a usage error |
| R16 | Finalization path and rename | A normal run leaves only `log.path`, `complete = 1`, finalized by `close_log()`; a `Simulation` dropped mid-run is flagged by `load`; a run killed with SIGKILL in a subprocess leaves no file at `log.path` and a temporary file that `load(..., allow_incomplete=True)` opens |
| R17 | Cross-checks | Hand-edited files (a wrong `final_state.frame`, a renamed signal) are refused by `load` naming the check |
| R18 | Identity survives | M5's R4 (two runs byte-identical) and R9 (replay byte-identical, including a halting run) pass unchanged under ADR-0027; M3's frozen regression passes |
| R19 | Benchmark runs | The smoke run emits JSON with every field named above for each named config; the Release baseline is recorded in its PR's evidence |
| R20 | Gate | `./scripts/check-full.sh` exit 0 with `check: OK` per code PR; review per tier |

## Open questions

Each has a recommendation. "ADR" means the answer is recorded in ADR-0027; "plan" means Joe's decision here is
enough. Blocks: the first PR that cannot start without it.

- **Q1. Records (blocks PR 1).** ADR-0027 supersedes ADR-0025's Finalization section only (accepted ADRs are
  immutable). Architecture rev 10 updates §1's visualization claim, §9 (finalization), §12 (the new modules and
  the CLI), §15 (exit codes) and adds §16's M6 row. The plot-package format is recorded in §12, not in an ADR.
  Recommend yes. PR 1 writes under `docs/decisions/`, so Joe arms the guard for that session.
- **Q2. Plotting dependencies (blocks PR 4). Decided by Joe: both.** matplotlib, headless (Agg), for files
  (SVG and PNG) and for the report, since its SVG can be made byte-deterministic (fixed hash salt, no date
  metadata); plotly for interactive HTML. Both pinned in the dev group at the current release at pin time.
- **Q3. Package format (plan).** YAML as described, with the Python-function escape hatch; framework packages
  with `astra.plot`, aircraft packages in `aircraft/<name>/plots/`, a starter set in the template. Recommend
  yes.
- **Q4. Display units (plan).** Plots convert SI to a display unit from a fixed table keyed on the log's unit
  string, and refuse an incompatible conversion; default is the logged SI unit. Recommend yes.
- **Q5. Figure output (plan).** One file per figure; SVG by default, PNG on request; deterministic SVG.
  Recommend yes.
- **Q6. Report format (plan). Decided by Joe: self-contained HTML** with inline SVG, no script, no external
  references, byte-deterministic.
- **Q7. Outcomes (plan).** Default failure is `status == halt`; a custom criterion is a closed expression over
  summary columns; `error` rows are separate; `seed` dispersions are excluded from rank methods. Recommend yes.
- **Q8. Correlation methods and the tree (blocks PR 5). Decided by Joe: in-house first.** KS with
  Benjamini-Hochberg, Spearman and PRCC, and a depth-limited classification tree built in-house (exhaustive
  splits, depth at most 3, deterministic). If it proves insufficient, scikit-learn comes back to Joe as a
  dependency decision with the evidence. KS, Spearman and the normal quantiles come from scipy, already pinned.
- **Q9. The end-to-end planted cause (plan).** `cm_alpha_per_rad` dispersed across zero on the notional
  transport, with at least three irrelevant dispersions, judged against the maximum angle of attack. The
  dispersion range, the duration and the pass/fail threshold come back to Joe with evidence in PR 6, like M5's
  scenario bands. Recommend yes.
- **Q10. Sizing methods (plan).** The four formulas in the contract, with the closed-form oracles in R11.
  Recommend yes.
- **Q11. Benchmark shape (blocks PR 8).** A C++ test executable timing each `step()` with a steady clock
  (outside the wall-clock scan's scope, like the lockstep timing tests), Release preset, JSON out; the gate runs
  a smoke only and asserts no speed. Recommend yes. Tracking over time is M11.
- **Q12. CLI scope and exit codes (blocks PR 3).** Subcommands `run`, `trim`, `plot`, `report`, `mc size`,
  `mc analyze`, added as each tool lands. Exit codes: 0 success; 1 a halt; 2 `--strict` and a nonzero `warn`
  counter; 64 a usage error (`EX_USAGE`); 65 a config error (`EX_DATAERR`). A `transport` config is refused by
  `run` until M10. Recommend yes.
- **Q13. ADR-0027's content (ADR; blocks PR 1).** Points a to d as in the contract. The temporary name is
  `<log.path>.partial`. The flush at each drain (point c) may make the bytes depend on the drain pattern, and
  replay drains differently from `run()`: PR 2 must show M5's R4 and R9 still hold. If they do not, Joe
  chooses between flushing only at `close_log()` (losing crash-readability) and ADR-0025's
  dataset-and-attribute-equality fallback for replay. Recommend the ADR as described with that decision point.
- **Q14. Module names (plan).** `astra.plot`, `astra.mc_analysis`, `astra.mc_sizing`, `astra.report`,
  `astra.cli`. Recommend yes.
- **Q15. Gate time (plan).** The new tests add pytest time; R9's batch is the largest. Its run count comes from
  R11's tool and its duration is kept short; the cost is measured in PR 6 and stays in `just check` unless Joe
  moves it. Recommend yes.
- **Q16. Plan critique (plan). Decided by Joe: yes, before approval.** ADR-0027 and the package format are
  costly to unwind, so one independent Codex critique of this plan runs before approval (from Claude Code,
  read-only, effort high). Findings are folded in here in the planning session.

## PR decomposition

One Claude builder, sequential unless Joe assigns a parallel worktree (PR 8 is independent after PR 1). Each PR
opens as a draft, gets its tier's review, and waits for Joe's merge. Every code PR runs
`./scripts/check-full.sh` (exit 0 and `check: OK`) and commits a trimmed gate summary.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance commands (beyond the full gate) |
|---|---|---|---|---|---|
| 1 | ADR-0027; architecture rev 10 (§1, §9, §12, §15, §16 M6 row); PLAN.md M6 PR lines | 2 | 0002, 0005, 0010, 0025 | Q1, Q13 | `python3 scripts/docs_check.py`; `git diff --check`; one Codex review; no full gate (docs only) |
| 2 | ADR-0027 implementation: finalization-path attribute, `.partial` then rename, flush at drain, `load` flags and cross-checks; `montecarlo` and `scenario` adjusted to the new file lifecycle | 3 | 0005, 0010, 0025, 0027 | PR 1 | `ctest --preset asan -R 'RunLog\|Hdf5\|NoAlloc' --no-tests=error`; `.venv/bin/pytest tests/python -k 'log or replay or montecarlo or scenario'` (R16 to R18); `ctest --preset release -R NotionalTransportCruiseRegression --no-tests=error` |
| 3 | `astra` CLI: `run [--strict]`, `trim`; console script in `pyproject.toml` | 2 | 0010, 0025 | PR 1, Q12 | `.venv/bin/pytest tests/python -k cli` (R14, part of R15) |
| 4 | `astra.plot`, the package format, both renderers, framework and template packages, notional transport packages, `astra plot`; matplotlib and plotly pinned | 2 | 0002, 0010, 0011, 0025 | PR 1, Q2 to Q5 | `.venv/bin/pytest tests/python -k plot` (R1 to R4, R3b) |
| 5 | Monte Carlo visualization, outcome criteria, `astra.mc_analysis`, `astra mc analyze` | 2 | 0025 | PR 4, Q7, Q8 | `.venv/bin/pytest tests/python -k mc_analysis` (R5 to R8, R10) |
| 6 | `astra.mc_sizing`, convergence, `astra mc size`; the end-to-end planted-cause batch | 2 | 0025 | PR 5, Q9, Q10, Q15 | `.venv/bin/pytest tests/python -k 'mc_sizing or planted'` (R9, R11, R12) |
| 7 | `astra.report`, `astra report` | 2 | 0025 | PR 4 to PR 6, Q6 | `.venv/bin/pytest tests/python -k report` (R13) |
| 8 | Frame-time benchmark executable, its smoke in the gate | 2 | 0010 | PR 1, Q11 | the smoke target (R19); Release baseline in the PR evidence |
| 9 | M6 closeout: PLAN.md check-off with evidence links, architecture §12 status, open items for held-out scope | 0 | none | PR 1 to PR 8 | `python3 scripts/docs_check.py`; `git diff --check`; one Codex pass |

PR 2 is Tier 3: an adversarial Codex review focused on byte identity, replay, the file lifecycle under failure,
and the kill test; Joe reads its diff and tests. PR 1's ADR needs Joe's acceptance before PR 2 starts.

## Validation and risks

- **Flush versus byte identity (Q13).** The main technical risk; PR 2 measures it before anything depends on it.
- **matplotlib determinism.** SVG output must be byte-stable across runs on one machine; PR 4 proves it (R3)
  before the report depends on it. PNG is not required to be byte-stable. The plotly HTML is not byte-stable
  either (it embeds plotly.js and generates element ids); R3b checks its data, not its bytes, and a plotly
  file is several megabytes, so it is written only on request.
- **Statistical tests that pass by luck.** R6 to R8 use fixed seeds and state their allowances; R8's allowance
  is computed from the binomial, not tuned to pass.
- **R9's physics.** With `cm_alpha_per_rad` above zero the open-loop run diverges; the range must keep trim (if
  used) converging, or the batch starts from a fixed trimmed state without per-run trim. PR 6 decides with
  evidence; either way the criterion is argued before the first run, as M5's scenario bands were.
- **Gate time.** Measured per PR; R9's batch is sized by the new tool and kept short.
- **Rollback.** Everything is additive (new modules, a console script, a test executable) except PR 2, whose
  rollback is reverting it; R18 guards it.
- **Held out.** Closed-loop analysis (M10), benchmark tracking and `RealTime` (M11), envelope monitors (M7),
  interactive plots, PDF, Sobol and adaptive sampling: follow-ups, recorded by PR 9.

## Plan critique and decisions

Joe's decisions so far (2026-09-29, planning session): Q2 both libraries, Q6 HTML, Q8 an in-house tree first,
Q16 a Codex critique before approval. He raised no objection to the other recommendations; approval of the spec
and plan as a whole is pending the critique.

Codex plan critique: pending.

## Done when

R1 to R20 (with R3b) pass on their PRs; each PR has its tier's review with results in its PR body; PR 9 checks M6 off in
PLAN.md with evidence links. Implementation completion is separate from Joe's approval and merges.
