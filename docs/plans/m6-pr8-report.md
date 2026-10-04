# M6 PR 8: the run report (`astra.report`, `astra report`)

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29: "Run report", R13, R15's report case, Q6, Q14,
C-11 to C-13 (its "Obligations carried to PR plans" row for PR 8); architecture §12's `astra.report` bullet;
[PR 5's plan](m6-pr5-plots.md) D7 (ids unique across a report); PLAN.md's (M6) open items on
`tests/python/resource_refs.py` and on HTML size.
Risk: **Tier 2** (a Python reader and a CLI subcommand; no `core/`, `plugins/`, `icd/`, `tests/golden/**`,
tolerance file or ADR; no new dependency). Base: `origin/main` `51410e2` (#94 merged).
Worktree/branch: `worktree-m6-pr8-report`, created at the start of the build (this session opened on `main`
in plan mode). This draft moves there as `docs/plans/m6-pr8-report.md`, and the copy in the main checkout is
deleted.
Binding ADRs: 0025/0027 (logs read strictly through `astra.log`), 0002 (SI; display units through
`astra.plot`), 0010 (`t = frame * dt`), 0011 (`{v}`, no `v0` assumed).
Status: **approved by Joe, 2026-10-03**, with his six pre-approval points and three additions folded in (D1a,
D1b and D2 to D10 as recommended; the additions are in D1b's PLAN.md item, step 3 and step 5).

## Context

M6's last reader. It turns one run log, one scenario's log, or one Monte Carlo batch into a single HTML file
someone can open offline and read, with provenance. It also closes three things: C-12's report part (PR 5
left matplotlib's counter and glyph ids repeating across SVGs, D7); C-11 for the report; and the
`resource_refs.py` open item, which replaces an enumeration of loading constructs with an allow-list.

## Repository findings (read at `51410e2`)

- `astra.log.RunLog` holds every provenance field R13 names: `config` (the root attribute, yaml-cpp's
  `Config::emit_without("log")` text, `config.cpp:315`, comments dropped and file key order kept), `dt`,
  `git_describe`, `core_version`, `core_api_version`, `format_version`, and `plugins` (`/plugins`: `path`,
  `category`, `name`, `core_api_version`, `built_with`, `sha256`; `run_log.cpp:110`). It also holds
  `frames_run`, `halt`, and `health` (every `health.*` in `/summary`).
- `astra.plot`: `pinned_style(package, figure)`, `draw_matplotlib(figure)`, `resolve`, `load_package`,
  `package_path`, `create_exclusive`. `render` writes files only; no in-memory SVG path exists yet.
  `mc_analysis.render_figures` repeats the same `savefig(..., metadata={"Date": None})` pattern.
- `astra.mc_analysis`: `load_batch`, `analyze(batch, criterion, metric, q) -> Analysis` (KS, Spearman,
  PRCC, tree; `.text()`), `envelope`, `draw_histograms`, `draw_scatter`, `draw_envelope`, `figure_names`.
  `astra.mc_sizing`: `load_claims(path)`, `check(batch, claims) -> Table` (`.rows`, `.seed`, `PROTOCOL`).
- `astra.scenario`: `load`, `run`, `verify`. A check's result exists only as `_check_failure`'s message
  string (`scenario.py:590`); nothing returns per-check rows.
- A batch's record (`summary.parquet` schema metadata) holds the spec bytes, the base config bytes and the
  config name (`montecarlo.py:159`). Each run log's `config` holds that run's drawn values.
- matplotlib 3.11.2's SVG (surveyed over every shipped package at 60 s): elements `svg`, `metadata`,
  `rdf:rdf`, `cc:work`, `dc:type`, `dc:format`, `dc:creator`, `cc:agent`, `dc:title`, `defs`, `style`, `g`,
  `path`, `use`, `clippath`, `rect`. Attributes: `clip-path`, `d`, `height`, `id`, `rdf:resource`,
  `style`, `transform`, `type`, `version`, `viewbox`, `width`, `x`, `xlink:href`, `xmlns`, `xmlns:cc`,
  `xmlns:dc`, `xmlns:rdf`, `xmlns:xlink`, `y`. The only function in a value is `url(` (in `clip-path`), plus
  `translate`/`scale`/`rotate` in `transform`. It has one `<?xml ...?>` declaration and the SVG 1.1
  DOCTYPE. Each SVG carries `<style type="text/css">*{stroke-linejoin: round; stroke-linecap: butt}</style>`,
  which applies to the whole page once inlined in HTML.
- PR 5's plotly page has `html`, `head`, `meta[charset]`, `title`, `body`, `div[id, class, style]` and
  attribute-free `script`.
- `-k report` today selects 8 tests by substring ("reported", "reports"), listed under Tests.

## Size (Joe's question; measured 2026-10-03 on `51410e2`, scratchpad only)

### Log reports

Notional transport `cruise.yaml` with `sim.frames` set to 60,000 and 300,000. The config's own light Dryden
turbulence (seed 42) is on; there was no halt. Then `astra plot` of each default package, as SVG. Bytes per
file:

| Figure | 60 s | 300 s |
|---|---:|---:|
| `flight-air_data` | 657,424 | 760,718 |
| `flight-aero_angles` | 452,428 | 504,253 |
| `attitude-euler` | 76,074 | 130,032 |
| `attitude-rates` | 70,277 | 136,884 |
| `health-health` | 47,073 | 47,445 |
| **framework total (the report's default, D2)** | **1,303,276** | **1,579,332** |
| `cruise-longitudinal` (notional package) | 516,957 | 616,591 |
| `cruise-lateral` | 314,453 | 430,613 |
| all seven | 2,134,686 | 2,626,536 |

- Five times the frames gives about 1.2 times the bytes. matplotlib simplifies each path before writing it
  (`path.simplify`, threshold 1/9 pixel at 100 dpi, part of the defaults `pinned_style` restores), so an SVG
  grows with what the line does on screen, not with the frame count. The SVG draws the data; it does not
  record it (the log does). PR 5's R1 checks the drawn `Line2D` data, not the path bytes.
- Glyph definitions are about 80 KB of each set (3 to 4 %), so deduplicating them across figures is not
  worth the complexity.
- Logs: 32.9 MB (60 s) and 163 MB (300 s). Rendering took at most 0.84 s per package, with a peak RSS of
  590 MB at 300 s, mostly from loading the log.
- The 25 MB in PLAN.md's HTML-size item is plotly's page, which stores every float64 sample and grows
  linearly. The report does not use plotly.

### Batch reports

Three batches of R9's `planted.yaml` (seed 707, `cm_alpha_per_rad` across zero, light turbulence), each
analyzed under R9's criterion `max.v0.derived.alpha > 0.23`:
- R9's own batch: 130 runs of 10 s, 58 failing;
- the same spec with 16 runs of 60 s, 12 failing;
- 16 runs of 300 s, 12 failing.

Each figure was drawn as `mc analyze` draws it. "Without overlays" is the same envelope with no failing run
drawn. "Per failing run" is the difference divided by the number of failing runs.

| Figure | 130 x 10 s | 16 x 60 s | 16 x 300 s |
|---|---:|---:|---:|
| `mc-hist` | 116,217 | 112,304 | 112,304 |
| `mc-scatter` | 207,539 | 129,299 | 129,299 |
| `mc-envelope-v0.derived.alpha` | **8,352,937** | **8,754,763** | **34,883,756** |
| without overlays | 1,636,829 | 6,857,009 | 31,994,855 |
| of which the two fill polygons (min-max, 5-95 %) | 514,393 + 514,427 | 3,085,735 + 3,085,929 | 15,427,865 + 15,428,261 |
| per failing run (alpha) | 115,795 | 158,146 | 240,742 |
| `mc-envelope-v0.derived.altitude` | 1,146,797 | 6,264,923 | 31,010,691 |
| per failing run (altitude) | 1,295 | 3,143 | 5,169 |

- **The fill polygons grow linearly, as you expected:** about 51 bytes per frame per polygon, the same for
  both signals. matplotlib does not simplify a `fill_between` polygon. At 300 s they are 31 MB of a 35 MB
  envelope. Everything else in the envelope (the five statistic lines, the count step) is simplified and
  stays near 1 MB.
- **The overlays depend on the signal.** A failing run's line is simplified, so it costs what the line does
  on screen: 1 to 5 KB for altitude, but 116 to 241 KB for alpha, which oscillates in turbulence. R9's 58
  overlays are 6.7 MB of its 8.4 MB alpha envelope. PR 6's contract and R5 overlay every failing run;
  `MAX_LEGEND_RUNS` limits only the legend.
- `mc-hist` and `mc-scatter` are at most 210 KB and do not depend on duration.
- R9's batch report with one envelope (alpha) would be about 8.7 MB; a 300 s batch with two envelopes
  would be about 66 MB.

### D1, decimation (Joe's call), split by report kind

- **D1a, log reports. Recommendation: no decimation.** The 300 s default report is about 1.6 MB of SVG,
  because matplotlib already simplifies lines. PLAN.md's HTML-size item stays open for the plotly page,
  with these numbers.
- **D1b, batch reports. Recommendation: no decimation in PR 8.** Instead, a new PLAN.md open item, tagged
  `[M6 or a later milestone — Joe fills in]`, carries the numbers above and names both costs: the fill
  polygons, which are linear in frames, and the failing-run overlays, which cost a set amount per failing run
  that depends on the signal. A follow-up PR then decides how to reduce the envelope. The reduction I would
  propose there is conservative: replace each fill polygon with a per-column minimum and maximum (about 2,000
  columns across a 900-pixel axes), so the band still contains every sample and looks the same at 100 dpi.
  That would reduce the 300 s polygons from about 31 MB to a few hundred KB (an estimate; not measured). It
  changes `mc analyze`'s envelope figure, and therefore R5's drawing and step 3's byte-identity check, so it
  belongs in its own reviewed change, not hidden inside the report. The overlays are a separate question:
  capping them would change PR 6's contract (every failing run is overlaid). The alternative is to make the polygon reduction in PR 8, inside
  `draw_envelope`. The before/after check would then exclude the envelope figures by your decision and
  instead check that the drawn band contains every sample.
- The report sizes for the 60 s and 300 s runs and for R9's batch go in the PR body.

## Contract

### Inputs and CLI (D2, D3)

`astra report INPUT --out FILE [options]`. INPUT's kind comes from the filesystem, not its extension:

- **a directory is a batch**, read with `mc_analysis.load_batch`. A directory without a readable
  `summary.parquet` is refused (65).
- **a file is a run log**, read strictly with `astra.log.load`. A file that is not a run log, or a log that
  is not complete and published, is refused (65).
- **a scenario is a log plus `--scenario FILE`**: the scenario's YAML (`astra.scenario.load`, root = the
  current directory, as the module's default) and the log of its run. The report reads and does not run
  anything. The log must match the scenario: equal `dt`, and the log's `/writes` equal to the scenario's
  events (frame, input, value), or the report is refused (65) naming the difference. That is a consistency
  check, not proof that this scenario produced the log; the log's config text is in the report for that.

Options, each refused with 64 when given with the other kind of input (the `mc size` `_BATCH_ONLY` pattern):

- Log only: `--package P` (repeatable; a file or a framework package name, `astra plot`'s rule). The
  default is `flight`, `attitude` and `health`; given packages replace the default (`mc rerun`'s rule).
  Aircraft packages are not discovered from the log's plugin paths. `--scenario FILE`.
- Batch only: `--criterion EXPR` (default `status == halt`), `--metric COLUMN` (Spearman and PRCC run only
  with a metric, as in `mc analyze`), `--q Q` (default 0.05), `--claims FILE` (`mc_sizing.load_claims`),
  `--signal NAME` (repeatable, an envelope each).

`--out FILE` is required. It is created exclusively and never overwritten; an existing file is refused (65).
A path that is inside the batch, or is the batch, is refused (65), as for `mc analyze`. Stdout gets the path
written. Codes: 64 usage; 65 a refused input, scenario, package, criterion, metric, q, claims file, signal or
log, or an existing output; 74 a failed write (the partial file this run created is deleted) or stdout; 70
an internal error; **0 written, including a report that shows a halt, a failed check or a `violated`
verdict** (D3). Everything is computed and every SVG drawn before the file is created.

### Content (C-13: R13's list, section by section, in this order)

Log report:
1. **Summary**: frames run and `frames_run * dt` in s; the halt (check, source, frame, vehicle) or `none`;
   every `health.*` counter from `/summary`, sorted, by `repr`.
2. **Scenario checks** (scenario only): one row per check: index, signal, kind with its bounds, window
   (seconds and frames, or `final`), rows checked, worst value and its frame, `pass`/`FAIL`. Below the rows,
   the scenario's other assertions (`verify`'s frames, halt and warn-counter failures), or "none". Rows come
   from a new public `astra.scenario.evaluate(scenario, check, run_log) -> CheckResult`; `_check_failure`
   formats its message from that result, so `verify`'s text does not change (its tests guard that) and the
   report and pytest cannot disagree.
3. **Figures**: one inline SVG per package figure, in package then figure order, each under a heading
   `<package>-<figure>`.
4. **Provenance**: input (the argument as given), log format version, `git_describe`, `core_version`,
   `core_api_version`, `dt` (`repr`), the plugin table (all six columns), and the config text (D6).

Batch report:
1. **Outcomes**: runs; `ok`/`halt`/`error` counts; under the criterion, pass/fail/error.
2. **Sizing** (D4): with `--claims`, `mc_sizing.check`'s table (each claim's option, tier, claim text, n,
   errors, estimate, bounds or nominal interval, verdict or "nominal, not calibrated", needed, minimum,
   notes, ranking parameters), the master seed and `PROTOCOL`. Without it: the stated line "no claims given
   (--claims FILE, the format `astra mc size check` reads)", the master seed and `PROTOCOL`.
3. **Correlation** (D5): `mc_analysis.analyze(batch, criterion, metric, q)`. For KS, and for Spearman and
   PRCC when there is a metric: rank, parameter, effect, p, the BH decision, then m, n and q, and each
   excluded parameter with its reason (seed dispersions included). Then the tree's rules, or its status. A
   method that has too few rows shows its stated line, as `mc analyze` does.
4. **Figures**: `mc-hist`, `mc-scatter` (with two or more parameters), and `mc-envelope-<signal>` per
   `--signal`, drawn as `mc analyze` draws them.
5. **Provenance** (D7): the batch's record (spec text, base config name and text, master seed), then the
   build provenance of its run logs.

### Rendering, ids and determinism (C-12)

- **SVG in memory.** `astra.plot` gains `svg_bytes(drawn) -> bytes`, the one `savefig` call (SVG,
  `metadata={"Date": None}`). `render` and `mc_analysis.render_figures` use it, so a report's figure is the
  same bytes `astra plot` / `mc analyze` would write. Each is drawn under `pinned_style` exactly as those
  commands draw it.
- **Inlining (D8).** Each SVG is inlined after removing its XML declaration and DOCTYPE. It is namespaced
  with an ordinal prefix: figure k (from 1, in report order) gets `id="fig<k>"` on its root `<svg>`, and
  every `id="X"` inside it becomes `id="fig<k>-X"`. Every reference becomes `#fig<k>-X`: `xlink:href` and
  `href` values `#X`, and `url(#X)`. Ordinals are used rather than figure names because `fig<k>-` prefixes
  cannot collide (`a` + `b-c` and `a-b` + `c` both give `a-b-c`). matplotlib's page-wide `<style>` rule is
  scoped to its figure (`#fig<k> *{...}`). Any other `<style>` text is an internal error naming it, so a
  matplotlib upgrade that changes it fails loudly. The report's own ids are `sec-*` and `prov-*`, which
  cannot clash with `fig<k>-*`.
- **Bytes.** The report is built as one string and written as UTF-8 with `\n` newlines, with no date, time,
  output path, host or user. Every number goes through one `_number(value)` before it is written. It
  converts numpy scalars to Python `int` or `float` (`.item()`), because h5py and pandas hand back numpy
  scalars (`/plugins`' `core_api_version` is `np.uint64`, for example), and under numpy 2 their `repr` is
  `np.float64(...)`. Anything that is not an int or float after the conversion, a bool included, is an
  internal error. The number is then written by `repr`, or by the formats the analysis and sizing text
  already use (`.6g`, `.3e`). Every input string is escaped with `html.escape(quote=True)`. Iteration order
  is list order, never set order. The CSS is a fixed `<style>` in `<head>` that uses no function, at-rule or
  escape.
- **Config text (D6).** The text is `RunLog.config` (the log's attribute), escaped and placed in one
  `<pre id="prov-config">`. It is never parsed or re-serialized; `astra.report` does not import `yaml`.

### Batch provenance (D7)

The batch's record comes from `summary.parquet`'s metadata. The run logs' build provenance (`git_describe`,
`core_version`, `core_api_version`, `dt`, format version, the plugin table) is read from every kept run log
(`ok`/`halt` rows) through a new light reader, `astra.log.read_provenance(path)`. It applies `load`'s
strict state and `.partial` refusals and reads only the root attributes and `/plugins`, never a rate group.
The report shows it once, as read from the lowest-indexed log. A log whose build provenance differs is
refused (65), naming both runs and the field. Per-run config texts differ by their draws and are not shown;
the record's base config and spec are. A batch with no kept log shows the record and the stated line "no run
log".

**A kept row whose log is missing (D10).** An `ok` or `halt` row whose `run_<i>.h5` is absent: R15's
`mc rerun` case builds exactly this, and so does someone deleting logs to save disk. Recommendation: refuse
with 65, naming the first such run and its path, because the provenance check and any envelope need every
kept log. A report built from part of a batch would be a report of a different batch. The refusal says that
`astra mc analyze` without `--signal` and `astra mc size` read only the summary. `mc_analysis.envelope`
already refuses the same case with an `EnvelopeError` naming the run. The alternative is to show the run as
missing in the provenance section and refuse only when an envelope needs it.

## The resource check: an allow-list (`tests/python/resource_refs.py`, D9)

`disallowed(text, profile) -> list[str]` parses with `html.parser` and flags, one line each, every element,
attribute, attribute value, declaration, processing instruction or CSS function that is not allowed below.
Names are compared lowercased, as `html.parser` reports them. Three profiles:

**Elements**
- `SVG` (a matplotlib file): `svg`, `metadata`, `rdf:rdf`, `cc:work`, `dc:type`, `dc:format`, `dc:creator`,
  `cc:agent`, `dc:title`, `defs`, `style`, `g`, `path`, `use`, `clippath`, `rect`.
- `REPORT`: `SVG`'s elements, plus `html`, `head`, `meta`, `title`, `body`, `main`, `section`, `h1`, `h2`,
  `h3`, `p`, `ul`, `li`, `a`, `table`, `caption`, `thead`, `tbody`, `tr`, `th`, `td`, `pre`, `code`.
- `PLOTLY_PAGE`: `html`, `head`, `meta`, `title`, `body`, `div`, `script`.

**Attributes** (allowed per profile; a value rule applies wherever one is given)
- `SVG`: `id`, `d`, `x`, `y`, `width`, `height`, `viewbox`, `version`, `type` (on `style` only:
  `text/css`), `style` (CSS rule), `transform` (transform rule), `clip-path` (CSS rule), `xlink:href`
  (fragment rule), `rdf:resource` (exactly `http://purl.org/dc/dcmitype/StillImage`), and the namespace
  attributes, each with its exact URI: `xmlns` = `http://www.w3.org/2000/svg`, `xmlns:xlink` =
  `http://www.w3.org/1999/xlink`, `xmlns:rdf` = `http://www.w3.org/1999/02/22-rdf-syntax-ns#`, `xmlns:cc` =
  `http://creativecommons.org/ns#`, `xmlns:dc` = `http://purl.org/dc/elements/1.1/`.
- `REPORT`: `SVG`'s, plus `lang` (on `html`), `charset` (on `meta`, exactly `utf-8`), `class`, and `href`
  (on `a`, fragment rule).
- `PLOTLY_PAGE`: `charset` (on `meta`, exactly `utf-8`), and `id`, `class` and `style` (CSS rule) on `div`.
  `script` takes no attribute.
- Every other attribute is flagged. That includes every `on*` handler, `src`, `srcset`, `poster`, `data`,
  `action`, `formaction`, `background`, an `href` that is not `a`'s, and a `meta` other than `charset`.

**Value rules**
- *Fragment*: `#` followed by at least one character other than whitespace. Nothing else is allowed, `data:`
  URIs included: R13 permits them, but nothing ASTRA emits uses one.
- *CSS* (`style` attributes, `<style>` text, `clip-path`): the only allowed function is `url`, and its
  argument must be a fragment. Every other `<ident>(` is flagged by name: `image-set`, `src`, `var`, `env`,
  and any other. Any `@` (an at-rule: `@import`, `@font-face`) and any backslash (an escape that could spell
  a function name) are flagged too.
- *Transform*: a list of `matrix`, `translate`, `scale`, `rotate`, `skewx`, `skewy` calls over numbers only.
- `<script>` text (`PLOTLY_PAGE` only) is not scanned. The stated limit stays: what inline JavaScript fetches
  at run time cannot be judged statically.

**Declarations and processing instructions**: `SVG` allows exactly matplotlib's `<?xml version="1.0"
encoding="utf-8" standalone="no"?>` and the SVG 1.1 DOCTYPE; `REPORT` and `PLOTLY_PAGE` allow exactly
`<!DOCTYPE html>`. Anything else is flagged, so `<?xml-stylesheet?>` is caught, and so is any other
`<!...>` declaration. Comments are ignored.

The three known misses the enumeration had are now caught: `<?xml-stylesheet?>` (a processing instruction),
`image-set()` (a function), and SMIL `<set>`/`<animate>` (elements). If the build finds matplotlib emitting an
element or attribute outside these lists (the `mc-*` figures were not part of the survey), the build stops
there and brings it to Joe; the list is not widened quietly. The same holds for the report's own HTML: the
`REPORT` element and attribute lists are fixed as written. If the report's markup needs anything outside
them (another element, `colspan`, a `data-*` attribute), that comes back to Joe the same way, and the
markup is not quietly fitted to a wider list. The open item closes in this PR.

`id_problems(text) -> list[str]` (same module) gives every repeated `id` in the document, and every reference
(`href`, `xlink:href` `#X`, `url(#X)`) that does not resolve. A reference inside a figure must resolve inside
that same `<svg>` subtree; a reference outside any figure must resolve outside every figure.

## Tests (`.venv/bin/pytest tests/python -k report`)

`tests/python/test_report.py`, every test named `test_report_*`, so `-k report` selects each one by its own
name and by its module. Fixtures (module-scoped, under `tmp_path_factory`):
- a scenario log from `astra.scenario.run(elevator_step.yaml)`;
- a halting log from `atmosphere_ceiling_halt.yaml`;
- the descent batch (`tests/python/mc_analysis_fixtures/descent_batch.yaml`, 24 runs, halts, error rows,
  a seed dispersion) with `tests/python/mc_sizing_fixtures/claims.yaml`;
- synthetic `RunLog`s built in the test, for escaping.

- **R13 content (C-13), log:** the report holds exactly one inline `<svg>` per package figure. Each of them,
  with its namespacing removed, equals the bytes `astra plot` writes for that figure, minus the declaration
  and DOCTYPE. The summary's frames run, halt and every health counter equal an h5py read of `/summary`.
  Halting log: the halt row equals the attributes.
- **R13 content, scenario:** one row per check. Each row's worst value, frame, row count and pass/fail equal
  an independent numpy computation from the log column over the window. A copy of the scenario with one
  tightened band shows that row as `FAIL`, with the frame `verify`'s message names. The other assertions
  equal `verify`'s list. A scenario whose events or dt differ from the log's is refused (65).
- **R13 content, batch:** outcome counts equal a pyarrow read of `summary.parquet`. The sizing rows equal
  `mc_sizing.check`'s, field by field. The ranking rows equal `analyze`'s (order, parameter, effect, p, BH),
  for the default criterion and for `--metric`. The tree rules are present. Each `mc-*` figure equals
  `mc analyze`'s file, as above. Without `--claims`, the stated line and the master seed are shown. A report
  with any required section removed fails these tests (C-13's point).
- **Provenance equals the log's attributes:** every `prov-*` field equals the h5py read of the root
  attribute (`dt` by `float(text) == attr`), and the plugin table equals `/plugins`, row by row and column by
  column. Config: the unescaped `<pre>` equals the attribute's text exactly, and the test first asserts that
  the attribute is not a fixed point of `yaml.safe_dump(yaml.safe_load(text))`, so the equality would catch
  a PyYAML round trip. A synthetic log whose config holds `</pre><script>x</script>&amp;` and a plugin path
  holding `"` comes back exact, and the resource check finds no script. Batch: the run logs' build
  provenance comes from the logs; a batch with one log's `git_describe` rewritten (an h5py edit of a copy) is
  refused (65), naming both runs.
- **R13 resources (C-11):** `disallowed(report, REPORT) == []` for all three kinds.
- **C-12 ids:** `id_problems(report) == []` for all three kinds, with at least five figures in the log report.
  This shows the namespacing is needed: two un-namespaced figures would give repeats. The helper is shown
  to catch a repeated id, an unresolved `#X`, and a reference that crosses figures.
- **Determinism (C-12):** for each of the three kinds, two subprocesses of the installed `astra report`
  write two output files, `a.html` and `b.html`. Process 1: `PYTHONHASHSEED=1`, a cold empty
  `MPLCONFIGDIR`, `TZ=UTC`. Process 2: `PYTHONHASHSEED=2`, the warm cache plus a `matplotlibrc` that
  changes the font and line width, `SOURCE_DATE_EPOCH` set, `TZ=Asia/Kolkata`. The two outputs are
  byte-identical. Neither holds a `\d{4}-\d{2}-\d{2}` date, a `\d{2}:\d{2}:\d{2}` time, the output path, or
  `<dc:date>`, and the fixtures' own inputs hold no date.
- **The allow-list's own tests** (moved from `test_plot.py` and extended, named
  `test_report_resource_check_*`): each forbidden construct is flagged in each profile where it could
  appear: PR 5's 17 snippets, the three former misses, `onload=`, `<a href="javascript:...">`, an unknown
  element, `var()`, a CSS escape, `@font-face`, a second DOCTYPE, and a `data:` URI. Each profile's allowed
  constructs pass.
- **R15 report case (CLI), `test_report_cli_*`:** the minimal case is the determinism subprocess. 64 for a
  missing `--out`, `--criterion` with a log, `--package` or `--scenario` with a batch, and `--q 2`. 65 for a
  missing log, a non-log file, a directory without a summary, a refused package, criterion or claims file, a
  mismatched scenario, an existing `--out` (left untouched), and `--out` inside the batch. 74 for an
  unwritable output directory (as a non-root user; nothing left behind). 0 with a halt and with a failed
  check. A copy of the batch with one `ok` row's log deleted gives 65, naming that run (D10).
- **numpy scalars, `test_report_numbers_*`:** `_number` turns `np.float64`, `np.int64` and `np.uint64` into
  Python values whose `repr` equals that of the Python value, and refuses a bool and a string. Every report
  (log, scenario and batch) holds neither `np.` nor `numpy`. Under the pinned numpy 2, an unconverted
  `np.uint64` in the plugin table fails this test.

**R3b onto the allow-list:** `test_plot.py`'s `test_r3b_the_page_is_offline_and_every_svg_is_self_contained`
uses `disallowed(page, PLOTLY_PAGE)` and `disallowed(svg, SVG)`; the two checker tests move to
`test_report.py` as above. `-k plot` still runs R3b on real output.

**What else `-k report` selects** (substring matches, unchanged by this PR):
`test_autopilot_design.py::test_one_extra_exchange_of_delay_is_reported[longitudinal|lateral]`,
`test_derivative.py::test_state_owner_slices_reports_the_correct_offset_and_count_for_a_state_owning_model`,
`test_lockstep.py::test_r2c_hold_is_reported[E_light_hold|G_moderate_hold]`,
`test_lockstep.py::test_seed_sweep_light_is_reported`,
`test_mc_analysis.py::test_mc_analysis_cli_rerun_reports_a_row_that_differs_and_exits_1`,
`test_scenario.py::test_verify_reports_a_rate_group_window_without_its_rows_instead_of_raising`. Their cost
inside the acceptance command is measured and reported.

## Files

- New: `python/astra/report.py` (module docstring holds the contract); `tests/python/test_report.py`.
- `python/astra/plot.py` (`svg_bytes`, used by `render`); `python/astra/mc_analysis.py` (uses it);
  `python/astra/scenario.py` (`CheckResult`, `evaluate`); `python/astra/log.py` (`read_provenance`);
  `python/astra/cli.py` (`report` subcommand, docstring); `tests/python/resource_refs.py` (rewritten);
  `tests/python/test_plot.py` (R3b onto it; two tests moved).
- Docs (`/wrap`'s docs step, before the final gate where it can): `docs/architecture.md` §12 (`astra.report`
  and `astra.cli` markers to built, status line; Joe's file, listed for his decision); `PLAN.md` (the PR 8
  line; the `resource_refs.py` item closed with its evidence; the HTML-size item narrowed to plotly, with
  this plan's numbers; a new item for D1b's envelope costs); `README.md` (the CLI bullet, a short "Report a
  run"); this plan; review record; gate evidence.

## Decisions for Joe (approving the plan approves these)

- **D1a** Log reports: no decimation (Size, above).
- **D1b** Batch reports: no decimation in PR 8. A new PLAN.md item, tagged `[M6 or a later milestone — Joe
  fills in]`, names both costs (the polygons and the overlays) and leaves a conservative reduction to its own
  PR (Size, above).
- **D2** Input kind from the filesystem: a directory is a batch, a file is a log, and a scenario is a log
  plus `--scenario`. The report reads and runs nothing. Default packages are `flight`, `attitude` and
  `health`; `--package` replaces them; no package is discovered from the log.
- **D3** `--out` is required and never overwritten. Codes 64/65/74/70/0, and a report that shows a halt,
  a failed check or `violated` exits 0.
- **D4** Sizing claims come from `--claims` (the `mc size check` file). Without one, the section states that
  and shows the master seed and the protocol. No default claim is invented.
- **D5** Correlation: the `mc analyze` options and defaults (`status == halt`, an optional metric,
  q = 0.05), shown in full with exclusions and the tree.
- **D6** Config text from `RunLog.config`, escaped, never parsed. `astra.report` does not import `yaml`.
- **D7** Batch provenance: the record, plus the logs' build provenance read from every kept log by
  `log.read_provenance`; a log that differs is refused.
- **D8** Ids namespaced by figure ordinal (`fig<k>-`), with matplotlib's `<style>` scoped to its figure.
- **D9** The allow-list as listed, fragment-only URLs (no `data:`), closing the `resource_refs.py` item.
  The lists are fixed for matplotlib's output and for the report's own HTML alike; anything the build needs
  outside them comes back to Joe.
- **D10** A kept row whose log is missing: refused with 65, naming the run.

## Verification and process

1. Create the worktree (`worktree-m6-pr8-report`). `mv` this draft from the main checkout into it as
   `docs/plans/m6-pr8-report.md`, so no second copy exists. Then `git -C <main checkout> status --short` is
   empty. Commit, push, and open the draft PR at that commit. Main-checkout state after planning (checked
   2026-10-03): the size measurements wrote only to the session scratchpad, and nothing under `build/` is
   newer than the session start. The only files this session left there besides the draft are 42
   gitignored `__pycache__/*.pyc` files, written at 16:56 by the `pytest --collect-only` used to list the `-k
   report` matches. They are bytecode caches the next import rewrites anyway, and they are left alone.
2. Build in coherent commits, pushing after each: resource check and its tests; `svg_bytes` and
   `read_provenance`; `scenario.evaluate`; `astra.report` and its tests; the CLI; docs.
3. **Before/after against `51410e2`** (`svg_bytes` and `scenario.evaluate` must not change what they
   replace). The script first asserts that the main checkout is at `51410e2` with a clean tree
   (`git status --porcelain` empty). It records which commit main's `.venv` `_core` was built from. Both go at
   the head of the log. It then runs twice on the same inputs: once with the main checkout's
   `.venv` (editable to the source at `51410e2`), once with the worktree's. It writes:
   - `astra plot` of every shipped package (three framework, the template's three, notional `cruise`)
     against the `elevator_step` and `atmosphere_ceiling_halt` scenario logs;
   - `astra mc analyze` figures of the descent batch (default criterion, plus `--signal
     v0.derived.altitude`);
   - `verify()`'s messages for each seed scenario in `tests/scenarios/` on its own log, and for a copy of
     each with every check tightened until it fails, so every message path prints. That covers a window
     row, `final`, a NaN worst value, and the rate-group "rows" message from a synthetic log.

   The logs and the batch are produced once and read by both runs. Pass condition: `cmp` reports every
   figure file identical, and the message lists are equal, string for string. Committed as
   `docs/reviews/evidence/<head>-before-after.log` (the commands, file counts, the `cmp` results and the
   message counts) and named in the PR body. If D1b's alternative is chosen, the envelope figures are
   excluded from the `cmp` by that decision, and the log says so.
4. Focused: `.venv/bin/pytest tests/python -k report`, `-k plot`, `-k scenario`, `-k mc_analysis`; `just ruff`
   (both steps, every file, lesson 2026-10-01); `.venv/bin/pyright`; `just docs-check`; `git diff --check`.
5. Gate time: PR 8 adds no C++, so its full-gate cost is reported as the pytest delta. That is the whole
   `pytest tests/python` on the branch and on `origin/main`, run back to back in this worktree, with the
   1-minute load before each run. Also reported: `test_report.py`'s own wall time and `--durations=15`. The
   fresh gate's elapsed time sits beside #94's as context only, with both runs' load.
6. Before the gate, check the load with `pgrep -x 'just|ninja|ctest|clang-tidy-18|clang\+\+-18|clangd'` plus
   `'[b]in/pytest'`, checking each hit's cwd (lesson 2026-10-02). Record other projects' load (the cowork
   VM, other sessions) and the 1-minute load average in the evidence header. Then `rm -rf build &&
   ./scripts/check-full.sh` in the background with no edits during the run; read its exit status and
   `check: OK`; commit `docs/reviews/evidence/<head>-gate-summary.log`.
7. `/review-cycle` (Codex review, `--base origin/main`; at most two correction rounds; record in
   `docs/reviews/m6-pr8-report-review.md`). The gate-rerun rule from #94 applies: after a passing gate, a
   Markdown/evidence-only delta needs only `just docs-check` and `git diff --check <gated-head>..HEAD`, named
   in the PR body; anything else makes the gate stale.
8. `/human-review` (PR body per `docs/templates/pr-description.md`: D1a to D10, the size numbers, the
   before/after evidence, the gate-time figures, the `-k report` extras, and the architecture.md edits for
   Joe's decision).
9. `/wrap` before merge, its docs step before the final gate where it can.
