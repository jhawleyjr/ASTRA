# M6 Analysis tooling: spec, plan and PR decomposition

Status: **draft, second revision** (Cowork planning session, 2026-09-29), after the Codex plan critique and its
verification pass. Joe has decided Q2, Q6, Q8, Q16 and Q17 to Q22; approval of the spec and plan as a whole is
pending.
Owner: Joe
Risk: 3 overall. PR 1 records Tier 3 decisions (ADR-0027), PR 2 changes run-log finalization in `core/`, and
PR 4 changes `core/util/units`; the rest are Tier 2 or lower. Each PR's tier is in the decomposition table.
Intent/approval: Joe's M6 scope in [PLAN.md](../../PLAN.md) (mapped 2026-09-29 from
[future work](../future-work.md)): plotting with reusable plot packages, Monte Carlo visualization,
failure-to-draw correlation, Monte Carlo sizing, a run report, a frame-time benchmark, `astra run --strict`,
and the superseding ADR to ADR-0025's Finalization. Approval of the plan does not approve any later PR's diff.
Base: `origin/main` = `55a0bc3`.
Architecture/ADRs: [architecture](../architecture.md) §1 (visualization is claimed), §9 (logger, replay), §12
(Python layer), §15 (post-run summary, `astra run --strict`), §16 (a new M6 row), §18 (the on-ramp names
`astra trim` and `astra run`);
[ADR-0002](../decisions/0002-si-units-and-signal-metadata.md) (SI inside; every boundary conversion a named
function in `core/util/units`),
[ADR-0005](../decisions/0005-hdf5-c-api-logging.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md),
[ADR-0011](../decisions/0011-one-vehicle-as-list-of-one.md),
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md),
[ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md),
[ADR-0026](../decisions/0026-derived-flight-condition-signals.md). Proposed: ADR-0027, superseding ADR-0025's
Finalization section (Q1, Q13, Q17 to Q19).
System context: every M6 tool is a consumer of what M5 produces. A run log (ADR-0025) is read through
`astra.log.load`; a Monte Carlo batch is `summary.parquet` plus one log per completed run
(`astra.montecarlo`); a scenario is a YAML file plus its run's log (`astra.scenario`). M6 adds readers
(plots, analysis, report), a front door (`astra` CLI), a measurement (the benchmark), Python access to the
core's unit conversions, and one change on the producer side: how a log is finalized (ADR-0027). Nothing in M6
changes `step()`.

## Outcome and boundaries

**Outcome.** A user can, from one command or one Python call:
- plot any run log with a named plot package, written once and reused across runs and aircraft, as files or
  as an offline interactive page;
- see a Monte Carlo batch: the draws, the outcomes, and trajectory envelopes;
- ask which dispersed parameters drive failures (or a continuous margin), including a two-parameter
  interaction, with the false-positive rate controlled, and rerun a flagged run from its seed with full
  logging;
- size a Monte Carlo before running it, from the question asked and the number of dispersed parameters, and
  check convergence after it;
- produce a self-contained report of a run, a scenario or a batch, with provenance;
- run a config with `astra run [--strict]` and get defined exit codes;
- measure the per-frame cost and the margin against `dt`.
And a file at a log's final path is always one that `close_log()` completed. A killed or dropped run leaves at
most a `.partial` file; when that file opens, every row in it is correct, and when it does not, `load` says so
instead of returning anything.

**Non-goals.** Closed-loop (lockstep) scenarios, Monte Carlo and analysis (M10); `astra run` of a config with a
`transport` block (refused with a reason; M10); a plotting UI or plotting a live run (offline plotly HTML is in
scope); PDF output; variance-based sensitivity (Sobol) or adaptive sampling, which need designed sample sets
rather than an existing batch; `RealTime` mode and tracking the benchmark over time (M11); §15 envelope
monitors (M7); any change to `step()`, `icd/`, `tests/golden/**`, tolerance files, or an accepted ADR other than
superseding ADR-0025's Finalization through ADR-0027.

**Invariants.** A config without a `log` block behaves bit-identically to `55a0bc3` (M3's frozen regression).
Two runs of one config still give byte-identical logs, and `astra.log.replay` still reproduces a log byte for
byte (M5 R4, R9), under ADR-0027 and for every drain pattern in use (`run()`, a per-frame `flush_log()` as
`astra.lockstep.Session` does, and replay). Logs and summaries stay SI; conversion for display goes through
`core/util/units` (ADR-0002). `t = frame * dt` is derived (ADR-0010). No wall-clock read in `core/`,
`plugins/`, `aircraft/` or `embedded/` except ADR-0023's two sites; Python tools and test executables may
measure wall time and print it, never store it in a log or summary (ADR-0025). Nothing assumes one vehicle
(ADR-0011). Tools read their inputs and never modify them. Analysis results are a deterministic function of
their inputs.

**Assumptions.** M5's log, replay, scenario and Monte Carlo behavior is correct as shipped and is not
re-validated here beyond the invariants above. Same-build, same-machine determinism only, with the pinned
Python dependencies. The statistics answer questions about the batch that was run; they do not validate the
models.

## Repository findings (observed at `55a0bc3`)

- **The log's write pattern** (`core/src/kernel/run_log.cpp`). `drain()` moves ring rows into one staging block
  per rate group and writes a block to HDF5 only when it holds `kBlockRows` = 1024 rows
  (`run_log.hpp:17`, `:69`); partial blocks, `/summary`, `/final_state` and `complete = 1` are written only in
  `close()`. So the bytes written before close depend on the row count alone, not on how often `drain()` runs.
- **Drain patterns.** `run()` drains past a high-water mark (half of `buffer_frames`); `flush_log()` drains on
  demand; `astra.lockstep.Session` calls `flush_log()` after every step (`lockstep.py:551`); `astra.log.replay`
  re-executes through `run()` in segments between recorded writes (`log.py:178`).
- **The destructor closes like `close_log()`.** `Simulation::~Simulation` calls `close_best_effort`, which
  calls the same `close()` and sets `complete = 1` if every write succeeds (`simulation.cpp:617`,
  `run_log.cpp:398`). That is why a dropped `Simulation` is indistinguishable today (the `astra.log.load`
  docstring and the ADR-0025 Finalization open item).
- **Never overwrite.** `log.path` is refused if anything exists there, a dangling symlink included
  (`run_log.cpp`, ADR-0025 Q5), and the file is then created.
- **Cleanup is spread over four places, each by the final name:** `_runkit` (`:264`), `montecarlo`
  (`:489`, `:562`, `:710`), `scenario` (via `_runkit`), and `log.replay` (`:173`). The Monte Carlo kill test
  waits for `run_*.h5` to appear before killing (`tests/python/test_montecarlo.py:291`).
- `astra.montecarlo` writes `summary.parquet`, one row per run: `run`, `seed`, `status` (`ok`, `halt`,
  `error`), one column per dispersion path, `error_type`, `error_message`, `frames_run`, `halt_*`,
  `health.<counter>`, `final.<state>`, and `min.<signal>`/`max.<signal>` for the spec's `signals`. Logs of
  `ok` and `halt` runs are kept. Open loop only. `run` prints wall time and the real-time factor. The 64-run
  case (`tests/python/fixtures/cruise_64.yaml`) runs under `just check`. The summary itself is written through
  a `.partial` file and a rename (`montecarlo.py:764`), a precedent for ADR-0027's lifecycle.
- `astra.scenario`: YAML with `checks` (`within`, `between`, `angle_within` over a window) evaluated on the
  run's own log, collected by `tests/scenarios/conftest.py`.
- **Units.** `core/include/astra/util/units.hpp` holds `feet_to_meters`, `meters_to_feet` and the knot pair,
  with no angle conversion, and none of it is bound to Python. `astra.fgbridge` defines its own `FOOT` and
  `KNOT` constants (`fgbridge.py:124`), a departure from ADR-0002's "every boundary conversion is a named
  function in `core/util/units`".
- No CLI exists: `pyproject.toml` has no `[project.scripts]`. §15 specifies `astra run`'s strict exit rule and
  §18 names `astra trim` and `astra run`.
- Dependencies: runtime `numpy`; the dev group pins `scipy`, `pyyaml`, `h5py`, `pandas`, `pyarrow`. No plotting
  library. M4's trace figures were SVG from a scratch script (`docs/reviews/m4-pr6-lockstep-acceptance-human.md`).
- The wall-clock scan covers the objects of `core/`, `plugins/`, `aircraft/` and `embedded/`, not `tests/`;
  ADR-0023 allows two monotonic-clock sites for the lockstep timeout.
- `aircraft/_template/` is copied whole by `just new-aircraft`, so a `plots/` directory placed there reaches every
  new aircraft with no framework edit (§18; `tests/python/test_new_aircraft_build.py`).
- The notional transport's aero config carries `cm_alpha_per_rad: -0.6`, a physically meaningful parameter to
  plant as a cause (dispersed across zero, static stability is lost).
- Gate cost today: `./scripts/check-full.sh` took 1001 s from a fresh configure on `cb2853c`; pytest 473 tests in
  59.3 s.

## Contract and failure behavior

**Plot packages (`astra.plot`).** A package is a YAML file: figures, each with panels, each with series. A
series names a logged signal, optionally with `{v}` for the vehicle index (expanded for every vehicle in the
log, so a package never hardcodes `v0`), and an optional display unit. Display conversion calls the
`core/util/units` functions through `astra._core` (Q20); a conversion the table does not define for the log's
unit string is an error. Every map is closed: an unknown key, an unknown signal, a bad conversion or an empty
figure is an error naming the package file and the key, before anything is drawn. A package may instead name a
Python function (module and name) that takes a `RunLog` and returns figures, for plots YAML cannot express.
Framework packages live with `astra.plot`; an aircraft's own live in `aircraft/<name>/plots/` (the template
carries a starter set). Two renderers take the same package (Q2): matplotlib writes one file per figure, SVG by
default and PNG on request, with its SVG made byte-deterministic (pinned style, bundled font, fixed hash salt,
no date metadata); plotly writes one interactive HTML file per package, with plotly.js inline so it opens
offline. The report uses the matplotlib SVG only.

**Monte Carlo visualization.** From a batch directory: a histogram of each dispersed parameter's draws; the
draws against each other colored by outcome; and, for a named signal, the trajectory envelope across the
batch's run logs. Runs have different lengths when they halt; at each frame the envelope uses the runs that
reached it and shows that count, and the failing runs are overlaid and labeled by run index.

**Outcomes and correlation (`astra.mc_analysis`).** The outcome is either a pass/fail criterion or a continuous
metric. The default criterion is `status == halt`. A custom criterion is a closed expression over summary
columns (a column, a comparison, a number, joined by `and`/`or`), never evaluated Python. A continuous metric is
any numeric summary column. `error` rows are reported separately and never counted as failures. Integer `seed`
dispersions are not ordered quantities; they are listed but excluded from rank methods. Methods:
- per parameter, a two-sample Kolmogorov-Smirnov test of passing against failing draws, with the
  Benjamini-Hochberg procedure across parameters at a stated false-discovery rate;
- per parameter, Spearman rank correlation and partial rank correlation (PRCC: rank-transform, regress out the
  other parameters, correlate the residuals) against a continuous metric;
- an in-house classification tree, depth at most 3, exhaustive splits on a stated grid, to surface
  interactions as readable conjunctions of thresholds (Q8).
Every ranking reports effect sizes with the p-values and the number of parameters tested. Too few failures for
a method is a stated result, not an error. `astra mc rerun <batch> <run>` re-executes one run from its recorded
seed and draws into a new directory with full logging and a plot package, for debugging a flagged run.

**Sizing and convergence (`astra.mc_sizing`).** Before a batch: the number of runs for (a) demonstrating a
failure probability below p with confidence C when no failure is seen, n = ceil(ln(1 - C) / ln(1 - p));
(b) a percentile tolerance bound by order statistics (Wilks, one- and two-sided); (c) a probability estimate
within a stated half-width, found by searching n on the exact Clopper-Pearson interval at an assumed
proportion; (d) detecting a rank correlation of size r with power 1 - beta across m dispersed parameters,
n = ceil(((z_(1-alpha/(2m)) + z_(1-beta)) / atanh(r))^2 + 3), the Fisher z approximation with a Bonferroni split
of alpha over m, stated as an approximation, valid for 0 < |r| < 1, and conservative in m. After a batch: the
estimate and its interval as a function of run count, so a user sees whether it has settled.

**Run report (`astra.report`).** For a log, a scenario, or a batch: one self-contained HTML file (inline SVG,
no script, and no attribute that loads an external resource) with the plot package's figures, the summary
(frames run, halt, health counters), scenario check results where they apply, the batch's outcome counts,
sizing check and correlation ranking where they apply, and provenance read from the log (config text, git
describe, core version and API, `dt`, the plugin table with SHA-256). The same inputs give a byte-identical
file.

**CLI (`astra`).** One console script with subcommands `run`, `trim`, `plot`, `report` and `mc` (`size`,
`analyze`, `rerun`), each added with the tool it fronts. `astra run <config>` runs `sim.frames` frames open
loop (an optional `--log` path adds the `log` block), closes the log with `close_log()`, prints §15's post-run
summary plus wall time and real-time factor to stdout, and exits with the first matching code:

| Code | Condition |
|---|---|
| 64 | usage error (`EX_USAGE`) |
| 65 | config error, including a `transport` block, refused until M10 (`EX_DATAERR`) |
| 74 | the log could not be written or closed (`EX_IOERR`) |
| 1 | a halt of any check, `LOG` included; a halt takes precedence over warnings |
| 2 | `--strict`, no halt, and a nonzero `warn` counter |
| 0 | otherwise |

**Units.** `core/util/units` gains the angle pair (`radians_to_degrees`, `degrees_to_radians`) beside the
foot and knot pairs, each with a unit test, and the whole set is bound in `astra._core`. `astra.plot` uses it;
`astra.fgbridge` moves its `FOOT` and `KNOT` onto it, with its packet tests unchanged (Q20).

**Benchmark.** A test executable, built in Release, that runs each named config for all its frames, asserts it
ran them without a halt, and only then records, with a steady clock, the mean, median, 99th percentile and
maximum frame cost of `step()`, frames per second, the margin against `dt`, and the toolchain and CPU, as JSON.
It also times a logged config end to end through `run()`, `flush_log()` and `close_log()`, so the drain and
flush cost that lies outside `step()` is measured too. The gate runs a short smoke of it and asserts the fields
and the frame count, never a speed.

**Finalization (ADR-0027, superseding ADR-0025's Finalization).**
- *Where a log is written (Q19).* At construction the log refuses `log.path` if it exists and refuses
  `<log.path>.partial` if that exists (a stale partial is the user's to remove), each a `ConfigError` naming
  the path, then creates `<log.path>.partial` exclusively, writes the provenance and flushes.
- *Explicit close.* `close_log()` writes the remaining rows, `/summary`, `/final_state` and `complete = 1`,
  closes the file, then publishes it with `renameat2(RENAME_NOREPLACE)`, which is atomic and never replaces a
  file. If a file appeared at `log.path` meanwhile, the filesystem does not support the call, or the rename
  fails for any other reason, `close_log()` raises naming the path and the reason and the `.partial` stays. A
  file at `log.path` is therefore one `close_log()` completed, and nothing else.
- *Destructor (Q17).* The destructor closes the file best effort but never renames and never sets
  `complete`: a dropped `Simulation` leaves only `<log.path>.partial`, with `finalized_by = destructor`.
  `load` refuses it unless `allow_incomplete=True` and says why, and `replay` refuses it with that reason (a
  dropped run is not a replayable record). `astra.lockstep.Session` calls `close_log()` on its normal close so
  its logs keep reaching `log.path`. A log at `log.path` carries no finalization attribute, so a run and its
  replay stay byte-identical.
- *Crash-readable (Q18).* Each rate group writes its own 1024-row blocks; the writer flushes HDF5 metadata after
  every block write, of any group, on the drain path and the close path alike, so the complete sequence of
  HDF5 calls depends on the row counts only, never on the drain pattern. What a killed run leaves: nothing, if
  it died before the `.partial` was created; otherwise a `.partial` that either opens with
  `allow_incomplete=True` or makes `load` raise a typed error naming the file. When it opens, each rate group
  holds a prefix of that group's rows, bitwise equal to a clean run's, and `load` ignores any `/summary` or
  `/final_state` in a file without `complete`. A kill during a flush or during close may leave a file that does
  not open; that is the stated limit (HDF5's single-writer/multiple-reader mode, which would remove it, is not
  adopted: it changes the file-format bounds and the byte-identity story). The cost is one flush per block
  written.
- *Cross-checks.* `load` refuses a log whose `final_state.frame` differs from `frames_run`, or whose rate
  groups' signal names differ from `/summary`'s, naming the check.
- *Who deletes what.* The core never deletes a log file. A tool (`montecarlo`, `scenario`, `replay`, through
  `_runkit`) deletes only files its own run created, the `.partial` or the final file, when it handles a failure,
  and never a file it did not create, such as one that collided at `log.path`. Replay's `out` follows the same
  rules as `log.path`.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | A package renders a scenario log | pytest: every figure is written; each plotted series' data equals the log column after the display conversion, exactly; axis labels carry the display unit |
| R2 | Packages fail loudly | Negative fixtures: unknown key, unknown signal, a conversion the table does not define for the unit, empty figure, a `{v}` series against a log without that vehicle: each an error naming the file and the key, raised before any file is written |
| R3 | Deterministic figures | Same log and package rendered in two separate Python processes into two directories: byte-identical SVG, with the pinned style and font (same machine, pinned versions) |
| R3b | Interactive output | The plotly renderer's data for every series equals the log column after conversion, exactly (through the figure's dict, not the file's bytes); plotly.js is inline; no `src` or `href` attribute loads anything other than a `data:` URI or a `#` fragment (XML namespace URIs are not resource loads) |
| R4 | Aircraft packages travel | A copy made by `just new-aircraft` renders its `plots/` starter set against its own log with no framework edit (the throwaway-copy pattern of `test_new_aircraft_build.py`) |
| R5 | Monte Carlo figures are right | On a batch with at least one halted run: histogram counts equal the summary's per-bin counts; at each frame the envelope's percentiles equal `numpy.percentile` over exactly the runs that reached that frame, and the plotted count equals that number; every failing run is overlaid under its index |
| R6 | Single planted cause, with power | Synthetic summary (no sim, fixed seed): failure iff one parameter exceeds a threshold, with 5 irrelevant parameters. The planted parameter ranks first and **survives** Benjamini-Hochberg at q = 0.05; no irrelevant parameter survives |
| R7 | Planted interaction | Synthetic: failure iff p1 > a **and** p2 < b. The tree's rule is a conjunction naming p1 and p2 with thresholds within one grid step of a and b, and it classifies a held-out grid covering all four quadrants correctly (a disjunction fails this) |
| R7b | PRCC is partial | Synthetic: p2 strongly correlated with p1, the metric depends on p1 only. p2's Spearman correlation is large while its PRCC is near zero, and p1's PRCC is large, each against a bound derived from the construction (plain Spearman returned as PRCC fails this) |
| R8 | False positives controlled | Synthetic null over K seeded batches: the fraction flagging any parameter is at most q plus the one-sided 99 % binomial allowance for K batches. R6's power requirement makes a correction that never rejects fail the pair |
| R9 | Planted cause in the real sim | A notional-transport batch with `cm_alpha_per_rad` dispersed across zero and at least three irrelevant dispersions. Named methods: KS on pass/fail at the approved maximum-angle-of-attack threshold, and Spearman against the maximum angle of attack. `cm_alpha_per_rad` ranks first on both with a strict margin over the second (a tie fails), meets a minimum effect stated before the run, and survives Benjamini-Hochberg. Negative control: the same batch with `cm_alpha_per_rad` fixed at -0.6 flags nothing. Run count from R11 (d); range, duration and threshold come back to Joe with evidence (Q9) |
| R10 | Outcome criteria are closed | An unknown column, an unknown operator or a non-numeric value is an error naming it; `error` rows are counted separately |
| R11 | Sizing formulas | Oracles: (a) p = 0.01, C = 0.95 gives 299; (b) Wilks 95/95 gives 59 one-sided and 93 two-sided; (c) Clopper-Pearson intervals for (n, k) = (100, 0), (100, 5), (299, 0), (1000, 20) equal `scipy.stats.binomtest(k, n).proportion_ci(method="exact")`; (d) r = 0.3, alpha = 0.05 two-sided, power 0.8 gives 85 at m = 1 (Cohen's table) and 120 at m = 4, n never decreases as m grows, r = -0.3 gives the same n as 0.3, and r = 0 or \|r\| >= 1 is refused |
| R12 | Convergence | On a batch, the reported estimate and interval at each prefix length equal a direct computation on that prefix |
| R13 | Report | For a log, a scenario and a batch: the HTML holds one inline SVG per package figure, the frames run, the halt and every health counter, the scenario's check rows, and the batch's outcome counts, sizing check and correlation table; provenance fields equal the log's attributes; no `<script>`, and no `src` or `href` loading anything but a `data:` URI or a fragment; byte-identical when built in two separate processes |
| R14 | `astra run` exit codes | Each row of the exit-code table seen to fire: a clean config gives 0; a halting config (`tests/configs/health_nan_wrench.yaml`) gives 1 and prints the check, source and frame; a halt with nonzero warnings under `--strict` gives 1; warnings without a halt give 2 under `--strict` and 0 without; an injected log close failure gives 74; a `transport` config gives 65 naming `transport`; a bad argument gives 64 |
| R15 | Other subcommands | `trim`, `plot`, `report`, `mc size`, `mc analyze` and `mc rerun` each run a minimal case and reject a bad argument with code 64; `mc rerun`, given a batch whose run logs were deleted, re-executes the chosen run and produces a log byte-identical to the original and the same summary row (a copy cannot pass without the original log) |
| R16 | Finalization lifecycle | A normal run leaves only `log.path`, with `complete = 1` and no `.partial`; a `Simulation` dropped mid-run leaves no `log.path` and a `.partial` with `finalized_by = destructor` and no `complete`, which `load` refuses without `allow_incomplete` and `replay` refuses naming the reason; a `Session` closed normally leaves `log.path`. Kill tests, in a subprocess, with a config that has a divisor-10 signal (a test plugin, since no shipped signal has a divisor above 1): killed before the first block, between blocks, and at several seeded points across a run including close. Every outcome is no `log.path`, and a `.partial` that either opens and holds, per rate group, a row prefix bitwise equal to a clean run's, or makes `load` raise a typed error; never wrong data |
| R16b | Nothing is overwritten | An existing `log.path` and a stale `.partial` are each a `ConfigError` naming the path; a file created at `log.path` during the run makes `close_log()` raise naming it, leaves that file untouched and keeps the `.partial`; an unsupported `RENAME_NOREPLACE` (simulated by an injected failure) raises and publishes nothing; replay's `out` obeys the same rules |
| R17 | Cross-checks | Hand-edited files (a wrong `final_state.frame`, a renamed signal) are refused by `load` naming the check |
| R18 | Identity survives every drain pattern | For the notional transport and for the divisor-10 fixture, byte-identical final files from: two `run()` runs (M5 R4); a run stepped with `flush_log()` after every frame, as `Session` does; a run with a small `buffer_frames`; and `replay` of each (M5 R9, including a halting run). M3's frozen regression passes |
| R18b | One cleanup rule | Through `_runkit`, `montecarlo`, `scenario` and `replay` delete, on a handled failure, only the `.partial` or final file their own run created, and a collided foreign file at the final path is left untouched (R16b's case run through each tool); the Monte Carlo kill test waits on the `.partial` name; no tool leaves its own `.partial` after a clean run or a handled error |
| R19 | Benchmark measures real frames | For each named config, the executable asserts all frames ran with no halt before it records; the smoke checks every JSON field, the frame count, and the end-to-end logged timing |
| R19b | Flush cost measured | PR 2 times a logged run end to end (`run()` plus `close_log()`, wall clock in the test, 1 and 100,000 frames, the divisor-10 fixture included) before and after its change, and records the gate time; PR 9's benchmark then tracks the same end-to-end figure |
| R20 | Gate | `./scripts/check-full.sh` exit 0 with `check: OK` per PR (PR 10 excepted as Tier 0); review per tier |
| R21 | Units at the boundary | The angle pair's unit tests, with the same round-trip property `units.hpp` pins for feet and knots; the bound functions equal the C++ ones; `astra.fgbridge` holds no conversion constant and its packet tests pass unchanged |

## Open questions

Each has a recommendation. "ADR" means the answer is recorded in ADR-0027; "plan" means Joe's decision here is
enough. Blocks: the first PR that cannot start without it. Q17 to Q22 come from the Codex critique.

- **Q1. Records (blocks PR 1).** ADR-0027 supersedes ADR-0025's Finalization section only (accepted ADRs are
  immutable). Architecture rev 10 updates §1's visualization claim, §9 (finalization), §12 (the new modules and
  the CLI), §15 (exit codes) and adds §16's M6 row. The plot-package format is recorded in §12, not in an ADR.
  Recommend yes. PR 1 writes under `docs/decisions/`, so Joe arms the guard for that session.
- **Q2. Plotting dependencies (blocks PR 5). Decided by Joe: both.** matplotlib, headless (Agg), for files
  (SVG and PNG) and for the report; plotly for offline interactive HTML. Both pinned in the dev group at the
  current release at pin time.
- **Q3. Package format (plan).** YAML as described, with the Python-function escape hatch; framework packages
  with `astra.plot`, aircraft packages in `aircraft/<name>/plots/`, a starter set in the template. Recommend
  yes.
- **Q4. Display units (plan).** Superseded by Q20: conversions come from `core/util/units`, bound to Python.
- **Q5. Figure output (plan).** One file per figure; SVG by default, PNG on request; deterministic SVG under a
  pinned style and the font matplotlib bundles. Recommend yes.
- **Q6. Report format (plan). Decided by Joe: self-contained HTML** with inline SVG, no script, no external
  resource, byte-deterministic.
- **Q7. Outcomes (plan).** Default failure is `status == halt`; a custom criterion is a closed expression over
  summary columns; `error` rows are separate; `seed` dispersions are excluded from rank methods. Recommend yes.
- **Q8. Correlation methods and the tree (blocks PR 6). Decided by Joe: in-house first.** KS with
  Benjamini-Hochberg, Spearman and PRCC, and an in-house tree (depth at most 3, exhaustive splits,
  deterministic). If it proves insufficient, scikit-learn comes back to Joe as a dependency decision with the
  evidence. KS, Spearman and the normal quantiles come from scipy, already pinned.
- **Q9. The end-to-end planted cause (plan).** As R9 states: `cm_alpha_per_rad` across zero, at least three
  irrelevant dispersions, the named methods, a strict margin, a minimum effect and a negative control. The
  range, duration, threshold and minimum effect come back to Joe with evidence in PR 7, argued before the first
  run, like M5's scenario bands. Recommend yes.
- **Q10. Sizing methods (plan).** The four in the contract, with R11's oracles. Recommend yes.
- **Q11. Benchmark shape (blocks PR 9).** A C++ test executable timing each `step()` with a steady clock
  (outside the wall-clock scan's scope, like the lockstep timing tests), Release preset, JSON out, frames
  asserted before timing; the gate runs a smoke only and asserts no speed. Recommend yes. Tracking over time is
  M11.
- **Q12. CLI scope and exit codes (blocks PR 3).** Subcommands as in the contract, each added with its tool;
  the exit-code table as written, first match wins. Recommend yes.
- **Q13. ADR-0027's content (ADR; blocks PR 1).** The Finalization contract above. Corrected after the critique
  (C-05): replay does not drain differently in a way that matters, because blocks are written by row count; the
  flush is placed on the same row-count boundary (Q18) so identity is expected to hold for every drain pattern.
  If R18 still fails in PR 2, the choice comes back to Joe, and any relaxing of replay's byte comparison is
  decided in ADR-0027 itself, not borrowed from ADR-0025's fallback. Recommend yes.
- **Q14. Module names (plan).** `astra.plot`, `astra.mc_analysis`, `astra.mc_sizing`, `astra.report`,
  `astra.cli`. Recommend yes.
- **Q15. Gate time (plan).** The new tests add pytest time; R9's batch (with its negative control) is the
  largest. Its run count comes from R11's tool and its duration is kept short; the cost is measured in PR 7 and
  stays in `just check` unless Joe moves it. Recommend yes.
- **Q16. Plan critique (plan). Decided by Joe: yes, before approval.** Done: see "Plan critique and
  decisions".
- **Q17. The destructor (ADR; blocks PR 1). Decided by Joe: as recommended.** The destructor closes best effort but
  never renames and never sets `complete`, so a dropped `Simulation` leaves only a `.partial` marked
  `finalized_by = destructor` that `load` and `replay` refuse, and `Session` calls `close_log()` on its normal
  close. Alternative: the destructor renames too and marks the file,
  which keeps a dropped run's log at `log.path` but makes a file there no longer proof of an explicit close, and
  puts a finalization attribute into logs whose replay may close differently.
- **Q18. What a crash leaves (ADR; blocks PR 1). Decided by Joe: as recommended,** a metadata flush after every
  block write of any rate group, on the drain and close paths alike, plus one after the provenance. What a killed
  run leaves is as the contract states: a correct row prefix per group, or a typed refusal, never wrong data. Flush
  points depend on the row counts only; the cost is one flush per block written. Alternatives: flush at every drain
  (per-frame under `Session`, and flush points that depend on the drain pattern), or no flush (a killed run's file
  is unreadable, as today).
- **Q19. Paths and collisions (ADR; blocks PR 1). Decided by Joe: as recommended.** Refuse an existing `log.path`
  or `.partial` at construction; create the `.partial` exclusively; publish with `renameat2(RENAME_NOREPLACE)`
  only (after the verification pass, N-02: link-and-unlink is not atomic); an unsupported filesystem or a failed
  rename raises and keeps the `.partial`; replay's `out` follows the same rules.
- **Q20. Units (plan; blocks PR 4). Decided by Joe: as recommended.** ADR-0002 requires every boundary conversion
  to be a named function in `core/util/units`. Add the angle pair there with unit tests, binding the set in
  `astra._core`, and moving `astra.fgbridge`'s two constants onto it in the same PR (its packet tests guard it).
  This is a small `core/util` change, so its PR is Tier 3.
- **Q21. PR 1's tier and gate (plan). Decided by Joe: run the gate.** PR 1 records Tier 3 decisions, so it is Tier
  3: adversarial Codex review of the ADR and Joe's acceptance. The workflow lets only Tier 0 skip the full gate, so
  PR 1 runs it too (about 17 minutes); the alternative is Joe's explicit waiver, recorded in the PR body.
- **Q22. Rerun command (plan). Decided by Joe: yes.** `astra mc rerun` is in M6 (PR 6), since the future-work
  detail placed it with the correlation work and a flagged run is what a user wants to look at next.
- **Q23. A third critique of this plan (plan).** Recommend none. The verification pass found the design issues
  fixed above (N-01 to N-06, and the C-01, C-03 and C-05 gaps); what remains of C-06 to C-15, C-18 and C-19 is
  fixture and oracle detail that belongs to each PR's own plan ("Obligations carried to PR plans"), where each
  PR's Codex review checks it. ADR-0027's text gets a Tier 3 adversarial review in PR 1, which is the right place
  for any remaining finalization design scrutiny. Another milestone-level pass would likely keep finding
  test-level detail (lesson 2026-09-28 on enumerations under repeated review).

## PR decomposition

One Claude builder, sequential unless Joe assigns a parallel worktree (PR 9 is independent after PR 1). Each PR
opens as a draft, gets its tier's review, and waits for Joe's merge. Every PR but PR 10 runs
`./scripts/check-full.sh` (exit 0 and `check: OK`) and commits a trimmed gate summary.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance commands (beyond the full gate) |
|---|---|---|---|---|---|
| 1 | ADR-0027; architecture rev 10 (§1, §9, §12, §15, §16 M6 row); PLAN.md M6 PR lines | 3 | 0002, 0005, 0010, 0023, 0025 | Q1, Q13, Q17 to Q19, Q21 | `python3 scripts/docs_check.py`; `git diff --check`; adversarial Codex review of the ADR; Joe accepts ADR-0027 |
| 2 | ADR-0027 implementation: `.partial`, exclusive create, no-replace rename, destructor without rename, per-block flush, `load` flags and cross-checks; `Session.close()` calls `close_log()`; one lifecycle in `_runkit` for `montecarlo`, `scenario` and `replay`; the kill test moved to the `.partial` name | 3 | 0005, 0010, 0025, 0027 | PR 1 | `ctest --preset asan -R 'RunLog\|Hdf5\|NoAlloc' --no-tests=error`; `.venv/bin/pytest tests/python -k 'log or replay or montecarlo or scenario or lockstep'` (R16 to R18b, R19b); `ctest --preset release -R NotionalTransportCruiseRegression --no-tests=error` |
| 3 | `astra` CLI: `run [--strict]` with the exit-code table, `trim`; console script in `pyproject.toml` | 2 | 0010, 0025, 0027 | PR 2, Q12 | `.venv/bin/pytest tests/python -k cli` (R14, part of R15) |
| 4 | Units: the angle pair in `core/util/units` with unit tests; the set bound in `astra._core`; `astra.fgbridge` onto it | 3 | 0002 | PR 1, Q20 | `ctest --preset asan -R Units --no-tests=error`; `.venv/bin/pytest tests/python -k 'units or fgbridge'` (R21) |
| 5 | `astra.plot`, the package format, both renderers, framework, template and notional-transport packages, `astra plot`; matplotlib and plotly pinned | 2 | 0002, 0010, 0011, 0025 | PR 3, PR 4, Q2, Q3, Q5 | `.venv/bin/pytest tests/python -k plot` (R1 to R4, R3b) |
| 6 | Monte Carlo visualization, outcome criteria, `astra.mc_analysis`, `astra mc analyze`, `astra mc rerun` | 2 | 0025, 0027 | PR 5, Q7, Q8, Q22 | `.venv/bin/pytest tests/python -k mc_analysis` (R5 to R8, R7b, R10, part of R15) |
| 7 | `astra.mc_sizing`, convergence, `astra mc size`; the end-to-end planted-cause batch and its negative control | 2 | 0025 | PR 6, Q9, Q10, Q15 | `.venv/bin/pytest tests/python -k 'mc_sizing or planted'` (R9, R11, R12) |
| 8 | `astra.report`, `astra report` | 2 | 0025, 0027 | PR 5 to PR 7, Q6 | `.venv/bin/pytest tests/python -k report` (R13) |
| 9 | Frame-time benchmark executable, its smoke in the gate | 2 | 0010, 0023 | PR 1, Q11 | the smoke target (R19); Release baseline in the PR evidence |
| 10 | M6 closeout: PLAN.md check-off with evidence links, architecture §12 status, open items for held-out scope | 0 | none | PR 1 to PR 9 | `python3 scripts/docs_check.py`; `git diff --check`; one Codex pass |

PR 1, PR 2 and PR 4 are Tier 3: adversarial Codex review (for PR 2: byte identity across drain patterns, the
file lifecycle under failure, the kill test, and the no-replace rename), and Joe reads the diff and tests.
ADR-0027 needs Joe's acceptance before PR 2 starts.

## Obligations carried to PR plans

The verification pass left 17 of the 20 findings partially resolved; its per-finding notes in PR #78's body say
what each still lacks, mostly a fixture or an independent oracle. Each PR's own plan starts from those notes for
the findings below and says how its tests close each one; its Codex review checks that it did.

| PR | Findings to close in the PR's plan |
|---|---|
| 1 (ADR-0027 text) | C-01, C-02, C-03, C-05, C-18, and N-01 to N-03 as resolved here |
| 2 (finalization) | C-01, C-02, C-03, C-04, C-05, C-18 (tests: R16, R16b, R17, R18, R18b, R19b) |
| 3 (CLI) | C-14 |
| 4 (units) | C-15 |
| 5 (plots) | C-11, C-12 |
| 6 (analysis) | C-06, C-07, C-10, N-06 |
| 7 (sizing, planted cause) | C-08, C-09, N-04 |
| 8 (report) | C-11, C-12, C-13 |
| 9 (benchmark) | C-19, N-05 |

## Validation and risks

- **Byte identity (Q13, Q18).** Expected to hold because flush points follow the row count; R18 checks every
  drain pattern in use, and a failure comes back to Joe before anything depends on it.
- **Portability of the rename.** `renameat2(RENAME_NOREPLACE)` is Linux-specific and needs filesystem support;
  the target is Ubuntu 24.04 (ext4 and tmpfs support it). On a filesystem that does not, `close_log()` refuses
  to publish (R16b) rather than fall back to a non-atomic path.
- **matplotlib determinism.** SVG must be byte-stable across processes on one machine with pinned versions; PR 5
  proves it (R3) before the report depends on it. PNG is not required to be byte-stable. The plotly HTML is
  not byte-stable (element ids, inline plotly.js); R3b checks its data and its resource references, and a
  plotly file is several megabytes, so it is written only on request.
- **Statistical tests that pass by luck.** R6 to R8 use fixed seeds and state their allowances; R8's allowance
  comes from the binomial, not tuning; R6 and R7b require power, so a method that never rejects, or that
  returns the wrong statistic, fails.
- **R9's physics.** With `cm_alpha_per_rad` above zero the open-loop run diverges; the range must keep trim (if
  used) converging, or the batch starts from a fixed trimmed state without per-run trim. PR 7 decides with
  evidence; the criterion and minimum effect are argued before the first run.
- **Gate time.** Measured per PR (R19b for the flush; PR 7 for R9's batch).
- **Rollback.** Additive except PR 2 (revert it; R18 guards it) and PR 4 (revert it; the fgbridge packet tests
  guard it).
- **Held out.** Closed-loop analysis (M10); benchmark tracking and `RealTime` (M11); envelope monitors (M7); a
  plotting UI or live plotting beyond offline plotly HTML; PDF; Sobol and adaptive sampling. Recorded by PR 10.

## Plan critique and decisions

Codex plan critique: one fresh, read-only pass (Codex companion 1.0.6, CLI 0.154.0, model `gpt-6-astra`, `task
--fresh --effort high`, job `task-mumyi79j-vzylc2`, thread `01a0ee3b-d185-7f71-af58-b9ad3e389690`) on
`0d70568` against `55a0bc3`; exit 0; verdict **rework**; 20 findings (8 high, 11 medium, 1 low), recorded in
full in PR #78's body. Claude Code checked every citation; this session re-read the code behind C-01, C-03,
C-04, C-05, C-15, C-18 and C-20. **All 20 are accepted** and folded in above; none is rejected.

| # | Sev. | Finding (verified) | Resolution in this plan |
|---|---|---|---|
| C-01 | high | Drains write only full 1024-row blocks and close writes the rest, so a flush per drain does not make a killed run's data readable as R16 claimed | Recoverable content defined: provenance plus complete blocks; flush per block (Q18); R16 checks the recovered rows bitwise |
| C-02 | high | Never-overwrite and exclusive create apply to different names once a `.partial` exists | Collision policy for the final path, a stale `.partial`, a failed rename and replay's `out` (Q19, R16b) |
| C-03 | high | The destructor runs the same close; renaming there breaks the invariant, not renaming loses `Session` logs; a finalization attribute breaks run/replay identity | Destructor never renames; `Session` calls `close_log()`; no finalization attribute at `log.path` (Q17, R16) |
| C-04 | high | Four cleanup sites delete by the final name; the kill test waits for `run_*.h5` | One lifecycle in `_runkit`; the kill test moves to the `.partial` name (R18b) |
| C-05 | high | Q13 misstated replay's drains; ADR-0025's fallback does not license relaxing replay | Q13 corrected; any relaxation is decided in ADR-0027; R18 covers every drain pattern |
| C-06 | high | A correction that never rejects passes R6 and R8 | R6 requires the planted cause to survive; R8 is paired with it |
| C-07 | high | Spearman returned as PRCC passes; a tree using OR passes R7 | R7b discriminates PRCC; R7 classifies all four quadrants |
| C-17 | high | PR 1 is Tier 3 in PLAN.md but was Tier 2 with no gate; PR 4 depended on PR 3 unstated | PR 1 Tier 3 with the gate (Q21); dependencies restated in the table |
| C-08 | medium | "Ranks first" passes on a tie; no method, effect size or negative control | R9 names methods, a strict margin, a minimum effect and a negative control |
| C-09 | medium | Clopper-Pearson's n unspecified; Fisher z's limits unstated | Contract states the search and the approximation's domain; R11 lists (n, k) cases and m = 4 gives 120 |
| C-10 | medium | Halted runs, different lengths and the failure overlay untested | Contract and R5 cover them |
| C-11 | medium | A literal "no http" rejects valid SVG and plotly files | R3b and R13 check resource-loading attributes instead |
| C-12 | medium | Determinism tested in one process with style and fonts unpinned | Pinned style and bundled font; R3 and R13 compare two processes |
| C-13 | medium | A report holding only provenance passes R13 | R13 lists the required content |
| C-14 | medium | No precedence for a halt with warnings; no codes for a log failure or the transport refusal | Exit-code table with precedence; R14 fires each row |
| C-15 | medium | ADR-0002 puts conversions in `core/util/units`; the plan had its own table | Angle pair added there and bound; fgbridge moved onto it (Q20, R21, PR 4) |
| C-18 | medium | `Session` drains every frame, so a per-drain flush has an uncounted cost | Per-block flush; cost measured (R19b) |
| C-19 | medium | The benchmark could time halted no-op steps | Frames asserted before timing (R19) |
| C-20 | medium | The rerun command had no owner; the interactive holdout was too broad | `astra mc rerun` in PR 6 (Q22); holdout narrowed to a UI or live plotting |
| C-16 | low | The wall-clock invariant contradicted ADR-0023 | Invariant names ADR-0023's two sites |

Verification pass: one fresh, read-only Codex task (`--effort high`, model `gpt-6-astra`, job
`task-mumzg2dy-oub5fa`, thread `01a0ee53-ed26-7732-aa10-e6697fb799c2`) on `e807328` against `0d70568`; exit 0;
verdict **rework**. Of the 20 findings, 3 resolved (C-16, C-17, C-20) and 17 partially resolved; 6 new (2 high,
4 medium). Claude Code re-read every cited line. All six new findings are accepted and fixed in this revision;
the partial findings' remaining gaps are the design points fixed here (C-01, C-03, C-05) and the per-PR
obligations above.

| # | Sev. | New finding (verified) | Resolution |
|---|---|---|---|
| N-01 | high | R16b kept a collided `.partial` and the foreign file while R18b deleted both | Who deletes what: the core never deletes; a tool deletes only files its run created, never a foreign file (contract, R18b) |
| N-02 | high | Link-and-unlink is not atomic; a kill between the calls leaves both names | `renameat2(RENAME_NOREPLACE)` only; an unsupported filesystem refuses to publish (Q19, R16b) |
| N-03 | medium | Blocks are per rate group, so "first multiple of 1024 frames" and "one flush per 1024 frames" were wrong for slower groups | Recovery is a row prefix per group; one flush per block of any group; a divisor-10 fixture in R16 and R18 |
| N-04 | medium | R11 refused negative r while the contract accepted 0 < \|r\| < 1 | R11: negative r gives the same n; r = 0 or \|r\| >= 1 refused |
| N-05 | medium | The benchmark timed `step()` only, where drains and flushes do not run, and landed after PR 2 | End-to-end logged timing in PR 2 (R19b) and in the benchmark (R19) |
| N-06 | medium | A rerun that copies the summary row passed R15 | The rerun must recreate a deleted run log byte-identically (R15) |

Joe's decisions (2026-09-29, planning session): Q2 both libraries, Q6 HTML, Q8 an in-house tree first, Q16 a Codex
critique before approval, and, after the critique, Q17 to Q19 (ADR-0027 as recommended), Q20 units through
`core/util/units`, Q21 the full gate on PR 1, Q22 `astra mc rerun` in M6. He raised no objection to the other
recommendations. Open for Joe: Q23, and approval of the spec and plan as a whole. PR #78 shows as ready for
review rather than a draft; that is Joe's to change or leave.

## Done when

R1 to R21 (with R3b, R7b, R16b, R18b and R19b) pass on their PRs; each PR has its tier's review with results in
its PR body; PR 10 checks M6 off in PLAN.md with evidence links. Implementation completion is separate from
Joe's approval and merges.
