# M6 PR 12: the TAS/CAS panel and the ground-track aspect

Spec: PLAN.md's two (M6) items (`python/astra/plots/flight.yaml` puts TAS and CAS on one panel, 270 to 475 kt,
both flattened; `aircraft/_template/plots/ground_track.py` draws without an equal aspect ratio, so near-straight
flight exaggerates a tiny lateral drift; both from M6 PR 5, Joe's review, 2026-10-01). M6 PR 5's plan
([m6-pr5-plots.md](m6-pr5-plots.md)): D2 (x is always `t` in YAML; any other x uses the escape hatch) and its
byte-deterministic SVG rule (R3: same log, package and pinned versions give a byte-identical SVG in any process).
Risk: **Tier 2**. Python tooling only: one YAML package, one template Python file, one optional `Panel` field in
`astra.plot` and tests. Nothing in `core/`, `python/bindings/`, `plugins/`, `icd/`, `tests/golden/**`, tolerance
files or ADRs; no new dependency.
Base: `origin/main` `6458e04` (#97 merged). Worktree/branch: `m6-pr12-plots-fixes` (`worktree-m6-pr12-plots-fixes`),
which this session enters after approval; this file then moves to `docs/plans/m6-pr12-plots-fixes.md` there.
Draft PR at the first commit.
No Codex plan critique: the design is small and settled by PR 5's contracts; every claim below is read from the
code or a probe recorded here.
Status: **approved by Joe, 2026-10-04**: D1 (a); D2 equal aspect with `adjustable="datalim"` through
`Panel.aspect`, no YAML key; D2b deferred; D3; D4; D5 Tier 2. His eight amendments are folded in below: (1) an
equal panel only in a one-panel figure; (2) each side of D3 draws its own tree's packages; (3) `mc rerun`'s
`flight-air_data` listed and its health figures identical; (4) the report split's three checks; (5) the still
case's limits; (6) R1/R4's assertion only on equal panels; (7) the probe inputs' digests; (8) gate time as
numbers with the load.

## Context

Joe's look at a 60 s cruise run (2026-10-01) found two display defects in shipped plot packages:
- `flight`'s `air_data` figure draws TAS (about 447 kt) and CAS (about 282 kt) on one panel, so the y axis spans
  270 to 475 kt and both lines' turbulence response is flattened to a few pixels.
- The template's `ground_track` autoscales each axis independently. On near-straight flight (north travel of
  kilometers, east drift of meters), the east axis zooms around zero, and 45 m of drift over 14 km looks like a
  swerve.
The outcome: each line is readable at its own scale, the ground track draws meters equal on both axes, and every
other figure the framework writes stays byte-identical to `6458e04`.

## Repository findings (read at `6458e04`)

- `astra.plot.draw_matplotlib` (`python/astra/plot.py:652`) draws every panel the same way: one axes per panel,
  `figsize=(8.0, 1.0 + 2.2 * count)`, constrained layout, `legend_outside`, shared x when every panel's x label
  matches. It has no aspect handling. `draw_plotly` (`:675`) likewise.
- `Panel` (`:113`) is `title, ylabel, unit, series, xlabel="t", xunit="s"`. A function package (the escape hatch)
  returns `Figure`s and never sees the axes, so **the ground track cannot set an aspect today without a new
  `Panel` field**. `_check_panels` (`:561`) checks every field of a returned panel.
- `flight.yaml` feeds `astra plot <log> flight`, the default log report (`report.DEFAULT_PACKAGES`,
  `report.py:75`, inlined through `plot.draw_matplotlib` and `svg_bytes`, `report.py:229`) and `astra mc rerun`'s
  default packages (`cli.RERUN_PACKAGES`, `cli.py:158`).
- The batch figures (`mc-hist`, `mc-scatter`, `mc-envelope-*`, `mc_analysis.py` and `report.py:251`) draw their
  own matplotlib axes; they use `plot.pinned_style`, `plot.legend_outside` and `plot.svg_bytes` only, none of which
  changes.
- **Which figures draw a track:** only `aircraft/_template/plots/ground_track.py`. None of the framework packages
  (`flight`, `attitude`, `health`) draws one, nor does any batch figure or the notional transport's `cruise.yaml`
  package (all its panels are `t`-based: altitude, TAS, angles, q, throttle; beta, phi, psi, p, r). The notional transport
  has no copy of the ground track (`aircraft/notional_transport/plots/` holds `cruise.yaml` only).
- **Coverage of `ground_track.py`:** no golden (`tests/golden/` holds atmosphere, dynamics, flightgear, frames,
  nesc, notional_transport, wind; none references plots). `tests/python/test_new_aircraft_build.py` copies the
  template and builds it, but never reads `plots/`. The coverage is `tests/python/test_plot.py`'s R1 (renders the
  template package against the elevator-step log; `check_package`'s function branch checks the drawn data
  bitwise against an independent recomputation and the axis labels) and `tests/python/test_plot_new_aircraft.py`'s
  R4 (a `just new-aircraft` copy; its `plots/` equals the template byte for byte; `check_package` again, plus
  `check_ground_track_against_position`, an oracle from the flat-Earth `p_n`). Neither looks at limits or scale.
- No test pins the `air_data` figure's panel count or bytes. `test_report.py:404` names the figure
  (`flight-air_data`), which does not change; R1 derives its expectations from the package's own spec.

## Probes (2026-10-04, scratchpad only; Joe's permission in plan mode, with his six conditions)

`P` = `/tmp/claude-1000/-home-joe-repos-ASTRA/b10c9bd6-e75f-47ab-aca2-cf7254b9ed07/scratchpad/probe`.
- **Load check** (`P/load-before-wait.txt`, `P/evidence-header.txt`): at 13:42:05 another project
  (Algorithmic-Trader's autopilot) ran five `clang-tidy` processes, 1-minute load 17.96 on 32 CPUs. I waited for
  them to end (13:43:16). At probe start (13:43:24): load 8.80, no `pgrep -x` hit for `clang-tidy`, `cmake`,
  `ninja`, `make`, `ctest`, `clang++`, `clang`, `cc1plus`, `ld.lld` or `pytest`. That project's dispatcher and a
  `claude -p` session were still running, with no build active.
- **Build, read-only:** the main checkout at `6458e04` (clean), `_core` at
  `/home/joe/repos/ASTRA/.venv/lib/python3.12/site-packages/astra/_core.cpython-312-x86_64-linux-gnu.so`, plugins
  from `/home/joe/repos/ASTRA/build/python/plugins`. No configure, build or install.
- **Scripts:** `P/make_logs.sh` writes the configs to `P/cfg/` and the logs to `P/logs/`. `P/draw_options.py`
  draws every option as SVG into `P/svg/`, under `plot.pinned_style` and through `plot.svg_bytes`, which is how
  `astra plot` writes. `P/preview_png.py` makes the same figures as PNG in `P/png/` (no SVG rasterizer is
  installed) so I could look at them. Their stdout is in `P/make_logs.out` and `P/draw_options.out`. To rerun:
  remove `svg/` and `png/`, then from `/home/joe/repos/ASTRA` run `bash P/make_logs.sh` (only into an empty
  `logs/`), `.venv/bin/python P/draw_options.py` and `PYTHONPATH=P .venv/bin/python P/preview_png.py`.
- **Logs:**
  - Straight runs: the notional `cruise.yaml` (light Dryden, seed 42), with only `sim.frames` changed to 60,000
    and 300,000. No halt in either.
  - **Turning run:** no tracked config turns, so I wrote `P/cfg/turn_300s.yaml` in the scratchpad. It is
    `cruise_still_air.yaml` with `sim.frames: 300000` and an initial bank `phi = 0.35` rad. It runs open loop,
    with no other change and no halt. The bank decays, so it is not a steady circle: the heading swings from 0
    to 1.18 rad (68°), and the track goes 50.4 km east and 42.2 km north.
- **Input digests (amendment 7):** the probe logs and `turn_300s.yaml` stay in the scratchpad. Their sha256 is
  in `P/evidence-header.txt`, so a D3 rerun can confirm the same inputs. If the session restarts before D3,
  `P/make_logs.sh` regenerates them and the new digests are recorded.
- **Cross-check:** the current `flight-air_data` measures 657,424 bytes at 60 s and 760,718 at 300 s, which
  equals PR 8's Size table byte for byte. So the drawing path is deterministic between that probe and this one.
- The probe's TAS reads 431 to 476 kt and its CAS 262 to 295 kt, over both runs.

## D1, TAS and CAS (recommendation: (a), separate panels)

| Figure (`P/svg/`) | 60 s, bytes | 300 s, bytes |
|---|---:|---:|
| `tascas-<len>-0-current.svg` (today's `flight.yaml`) | 657,424 | 760,718 |
| `tascas-<len>-a-separate-panels.svg` | 672,100 (+2.2 %) | 767,741 (+0.9 %) |
| `tascas-<len>-b-twin-axes.svg` | 667,212 | 763,564 |
| `tascas-<len>-c-departure-kt.svg` | 651,163 | 750,929 |

`<len>` is `60s` or `300s`, so these are eight files. Option (a)'s package is `P/option_a/flight.yaml`, the
candidate `flight.yaml`.

What the previews show:
- **(a):** each line fills its own panel, from 435 to 476 kt and from 265 to 295 kt at 60 s. The turbulence
  response reads clearly.
- **(b):** the two lines lie almost on top of each other, because CAS tracks TAS at fixed altitude. The overlay
  hides both, and the right axis's legend needs its own placement.
- **(c):** the two departures overlap in a band of about ±20 kt. The absolute speeds appear nowhere in the
  figure.

| Option | What changes | Cost |
|---|---|---|
| **(a) Separate panels** | `air_data` gets five panels: altitude, TAS, CAS, Mach, vertical speed. Each autoscales. | `flight.yaml` only; no code. Every panel keeps PR 5's "one display unit per panel" rule. The figure grows from 9.8 to 12.0 in tall. |
| (b) One panel, twin y axes | TAS on the left axis, CAS on a right axis, each autoscaled. | A new `Panel`/YAML concept (a second axis and which series it holds), in both renderers, `legend_outside` across two axes, R1's checks, the plotly page (`secondary_y`). Two scales on one grid are easy to misread: the lines cross where the values do not. |
| (c) One panel, shared axis, a different unit | Both drawn as their change from the first sample, kt (my reading of "a different unit": the quantity is the departure, which both share). | A new series option in the YAML format (an offset by the first value) and its tests; the absolute airspeeds then appear nowhere in the figure (Mach does not give them). |

Why (a): it fixes the defect with a data change, adds no format concept, and the absolute values stay readable.

## D2, the ground track's aspect (recommendation: equal aspect, `adjustable="datalim"`)

| Figure (`P/svg/`) | current | equal aspect | minimum span |
|---|---:|---:|---:|
| `track-cruise-60s-*.svg` (14.0 km north, 45 m east) | 29,326 | 29,022 | 28,651 |
| `track-cruise-300s-*.svg` (69.6 km north, 2.30 km east) | 28,481 | 29,656 | 30,411 |
| `track-turn-300s-*.svg` (42.2 km north, 50.4 km east) | 25,707 | 26,761 | 25,707 |

The suffixes are `0-current`, `equal-aspect` and `minimum-span`. Equal aspect is
`ax.set_aspect("equal", adjustable="datalim")`. For minimum span, the probe's rule is that each axis spans at
least a quarter of the other axis's span (and at least 100 m), centered on its data, with autoscale otherwise.
The quarter is a free parameter I picked for the drawing.

What the previews show:
- **60 s straight:** today's drawing turns a 45 m drift over 14 km into a swerve across the whole panel (Joe's
  defect). Equal aspect draws a vertical line. Minimum span widens east to about ±1.9 km, and the drift becomes
  a slight lean.
- **300 s straight:** the turbulence wanders the heading to -0.11 rad, so the drift is a real 2.3 km. Equal
  aspect draws it as a gentle lean at its true size. Minimum span still bends it into a curve. Its east scale is about 3 km per inch against
  about 33 km per inch north, so the drift is magnified about 10×.
- **Turn:** minimum span changes nothing (byte-identical to the current drawing, since both spans exceed a
  quarter of the other), so the turn stays squashed. Equal aspect draws the true geometry.
- **Equal aspect's cost:** in today's 8 × 3.2 in canvas (one panel), a north-going track has only the panel's
  height, about 2.2 in, and the panel's width is mostly empty east limits. A taller canvas for an equal-aspect
  panel would use the space better. **D2b: not in this PR** (recommended). It would change `draw_matplotlib`'s
  figure size rule for one kind of panel, and that change can follow later if the drawing reads poorly in use.

| Option | Long straight run (300 s, ~69 km north, meters of drift) | Turning run |
|---|---|---|
| **Equal aspect** (`ax.set_aspect("equal", adjustable="datalim")`) | One meter is the same length on both axes. The axes box keeps its size and the east limits widen to match the north scale, so the track is a straight line and the drift is drawn at its true size (invisible at 69 km, which is the truth). | A circle draws as a circle; true geometry. |
| Minimum axis span (each axis spans at least some fraction of the other, or some meters) | The drift is still magnified by whatever the threshold allows; the threshold is a free parameter to choose and explain. | Scales still differ, so a circle draws as an ellipse whose shape depends on the panel's box. |

`adjustable="box"` (matplotlib's default for `set_aspect`) is rejected: on a straight north run it shrinks the
axes to a sliver meters wide. `datalim` keeps the panel's box and changes the limits.

Implementation: `Panel` gains `aspect: Literal["auto", "equal"] = "auto"`. `draw_matplotlib` calls
`ax.set_aspect("equal", adjustable="datalim")` only when it is `"equal"`; `draw_plotly` sets the row's y axis
`scaleanchor` to its x axis with `scaleratio=1` only then. `_check_panels` refuses any other value
(`PlotPackageError` naming `<key>.aspect`). The YAML format gains no key: YAML's x is always `t` (PR 5's D2), so an
equal aspect has no meaning there. **Amendment 1:** `draw_matplotlib` shares x across panels whose x labels
match, and matplotlib's `datalim` adjustment on an axes whose x is shared changes the y limits instead of the x
limits (and raises if both are shared). So `_check_panels` refuses `aspect="equal"` in a figure with more than one
panel (`PlotPackageError` naming `<figure>.panels[<j>].aspect`). Lifting that is a later change, not an
undocumented behavior. `ground_track.py` passes `aspect="equal"`. The module docstring documents the
field under the escape hatch. Default `"auto"` leaves every other figure's drawing calls exactly as they are.

## D3, what must not change: before/after against `6458e04`

Every other figure is byte-identical by construction (the only drawing change is behind `aspect == "equal"`, and
the only package change is `flight.yaml`'s `air_data`). This is checked, not assumed, with PR 11's D5 method:
- **How.** `git archive 6458e04 python/astra aircraft` into `scratchpad/base/`. The PR has no C++, so both sides
  use the worktree's one build (`_core`, plugin dir). The base side runs with an empty `_editable_skbc_astra` shim
  first on `PYTHONPATH` and `scratchpad/base` prepended (PR 11's amendment: a spawned worker of an editable install
  otherwise imports the candidate). Each run prints `astra.__file__`, `astra._core.__file__`,
  `_core.default_plugin_dir()` and the package files' paths. **Amendment 2:** the base side draws the base
  tree's packages, `python/astra/plots/*.yaml` from `scratchpad/base/python` and the template's
  `ground_track.yaml`/`ground_track.py` from `scratchpad/base/aircraft`, never the worktree's copies. Both sides
  print the resolved package path and the escape-hatch file path, and the evidence records them. Both sides read the same input logs/batches (produced
  once), write to the same output path (renamed afterwards), from the same cwd. Batches run at one worker count (4)
  on both sides. A sha256 manifest of every output is diffed, then `cmp` on each mismatch.
- **Control.** The base side runs twice; its two manifests must be identical (this proves the plotly page and the
  report are deterministic before any difference is read as this PR's).
- **Fixtures and outputs:**
  - Logs: the elevator-step scenario, plus the probe's three logs (`P/logs/`: 60 s and 300 s cruise, and the
    turn). They are read in place from the scratchpad, as is `P/cfg/turn_300s.yaml`. Nothing from the
    scratchpad moves into the worktree without Joe's say.
  - `astra plot` of every shipped package on each log, as SVG: `flight`, `attitude` and `health`; the template's
    `controls`, `wrench` and `ground_track`; and the notional `cruise.yaml`. Each package's `--html` page is
    drawn on the elevator-step and 60 s logs only. A 300 s page is about 125 MB per package (plotly stores every
    sample; PLAN.md's (M6) size item), so 300 s pages add cost and no coverage.
  - `astra report` (default packages) on each log.
  - The batch figures and batch report: `astra mc analyze` figures and `astra report` on the descent batch
    (`tests/python/mc_analysis_fixtures/descent_batch.yaml`) and R9's planted batch.
  - `astra mc rerun` of one run of the descent batch (default packages `flight`, `health`).
- **Expected differences, and only these:** `flight-air_data.svg` and `flight.html` (D1); `astra mc rerun`'s
  `flight-air_data.svg` (D1; amendment 3), whose health figures are byte-identical; `ground_track-ground_track.svg`
  and `ground_track.html` (D2); each log report's inline `flight-air_data` figure. **Report split (amendment
  4):** each report is split at its inline `<svg id="fig<k>">` elements. The figure count per report is
  unchanged; every figure other than `fig1` (air_data, the default report's first) is byte-identical; the
  non-figure bytes, joined, compare as one string. The evidence records the three counts (reports, figures
  compared, non-figure strings compared). Anything else differing fails.
- The driver and manifests stay in the scratchpad; the PR body records the file count, the identical manifests'
  digest, the expected-difference list and which tree each side imported. If `python/` or `aircraft/` changes
  after this step, it reruns on the final candidate.

## D4, tests (acceptance `.venv/bin/pytest tests/python -k panel_scale`)

No pixel comparison. The equal-aspect regression test reads matplotlib's own transform after layout: after
`drawn.draw_without_rendering()`, `ax.transData` maps a 1 m step east and a 1 m step north to the same display
length (relative difference under 1e-9), and `ax.get_aspect() == 1.0`, `ax.get_adjustable() == "datalim"`.

In `tests/python/test_plot.py`:
- `test_panel_scale_ground_track_draws_meters_equal_on_both_axes[straight|turn|still]`: synthetic logs
  (`synthetic_log`): a straight north run with a 1 m east drift over 69 km, a circle, and zero velocity. The
  first also asserts the east limits span at least the north span scaled by the axes' box ratio (the drift is
  not zoomed). The still case (amendment 5) asserts a finite, nonzero limit span on both axes after
  `draw_without_rendering()`, not only that it renders.
- `test_panel_scale_an_equal_panel_must_be_its_figure_only_panel` (amendment 1): a function returning a
  two-panel figure with one equal panel is a `PlotPackageError` naming that panel's `aspect`, and nothing is
  written.
- `test_panel_scale_auto_aspect_is_the_default_and_unequal`: the same straight log through a `Panel` without
  `aspect` gives unequal scales: the negative control that proves the assertion above bites.
- `test_panel_scale_plotly_anchors_the_y_axis_on_an_equal_panel`: `draw_plotly` sets `scaleanchor` to that row's x
  axis and `scaleratio == 1` on an equal panel, and leaves both unset on an auto panel.
- `test_panel_scale_a_bad_aspect_is_refused`: a function returning `Panel(..., aspect="square")` is a
  `PlotPackageError` naming `figures[0].panels[0].aspect`, and nothing is written.
- `test_panel_scale_tas_and_cas_have_their_own_panels`: `flight`'s `air_data` panels are, in order, altitude,
  TAS, CAS, Mach, vertical speed, each with one series per vehicle.
- `check_package`'s function branch (R1 and R4) gains the transform assertion, so the template package and a
  `just new-aircraft` copy are both checked on a real log (R4's 0.6 rad heading travels on both axes). It runs
  only on panels whose aspect is `"equal"` (amendment 6); the `t`-based template figures keep their checks
  unchanged.

**What else `-k panel_scale` selects:** nothing today (`grep -rn panel_scale tests/ python/` is empty);
`--collect-only -q` confirms the list before the gate and the PR body records it. R1/R4's assertion runs under
`-k plot` and in the gate, not under `-k panel_scale`.

**Gate time (amendment 8: numbers in the PR body, with the 1-minute load at each measurement):** measured
after the build on a quiet machine: `-k panel_scale --durations=0` summed (the new
tests); R1's and R4's durations at base and candidate (`--durations=0` on `test_plot.py` and
`test_plot_new_aircraft.py`); full `pytest -q` wall time at base and candidate, one run each, noise stated.

## Files

- `python/astra/plots/flight.yaml` (D1); `aircraft/_template/plots/ground_track.py` (D2, `aspect="equal"` and its
  docstring).
- `python/astra/plot.py`: `Panel.aspect`, `_check_panels`, both renderers, the module docstring.
- `tests/python/test_plot.py` (new tests; `check_package`).
- Docs: PLAN.md's PR 12 line under M6, and the two (M6) items closed with this PR's evidence. STATE.md's next step
  becomes PR 13, then 14, then 10 (at `/wrap`). `docs/architecture.md` (Joe's) names no `Panel` field, so no edit.
- Evidence: `docs/reviews/evidence/<head>-gate-summary.log`.

## Verification and process

1. Enter the worktree, commit this plan as `docs/plans/m6-pr12-plots-fixes.md`, push, open the draft PR.
2. `just venv`/`just python` in the worktree as needed; fast loop `./scripts/check-fast.sh`, then
   `.venv/bin/pytest tests/python -k panel_scale`, then `-k 'plot or report or mc_analysis or cli'`.
3. D3's before/after on the candidate; the gate-time measurements.
4. `just ruff`, then `ps`/`pgrep -x` for any other build, recording other projects' load and the 1-minute load
   average in the evidence. Then `rm -rf build && ./scripts/check-full.sh`, no edits during the run (background,
   waited on its pid). Pass: exit 0 and `check: OK`. Commit the trimmed gate summary.
5. `/review-cycle` (Codex, fresh context), at most two correction rounds. The #94 gate-rerun rule applies from
   the start: after a passing gate, only a Markdown/evidence-only delta skips a new gate.
6. `/human-review`: the PR body per `docs/templates/pr-description.md`.
7. `/wrap` before merge, its docs step before the final gate where it can.

## Decisions for Joe

- **D1:** (a), separate TAS and CAS panels.
- **D2:** equal aspect with `adjustable="datalim"`, through an optional `Panel.aspect` (no YAML key).
  **D2b:** the canvas size stays as it is.
- **D3:** the before/after method, fixtures and expected-difference list above.
- **D4:** the tests, with `panel_scale` as the one keyword.
- **D5:** Tier 2.
