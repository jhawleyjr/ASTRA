# M6 PR 13: the batch envelope's failing-run overlays

Spec: PLAN.md's (M6) overlay item (from [PR 8b's plan](m6-pr8b-envelope.md), D3, and its
[sizes](../reviews/evidence/2579fd7-before-after.log)); the [M6 plan](m6-analysis-tooling.md)'s R5 ("every
failing run is overlaid under its index"); [PR 6's plan](m6-pr6-mc-analysis.md) D8 (the overlay contract) and its
overlay check (`tests/python/test_mc_analysis.py:229`, line data equal to the log column); PR 8b's rule that the
lines, the count and the overlays keep every frame (the module docstring, `python/astra/mc_analysis.py:83`).
Risk: **Tier 2**. One drawing function in `python/astra/mc_analysis.py` and its tests. No `core/`, `plugins/`,
`icd/`, `tests/golden/**`, tolerance file or ADR change, and no new dependency.
Base: `origin/main` `fa3a3c0` (#100 merged). Worktree/branch: `m6-pr13-envelope-overlays`
(`worktree-m6-pr13-envelope-overlays`), which this session entered after approval. Draft PR at the first commit.
Binding ADRs: 0010 (x = `frame * dt`), 0025/0027 (logs read through `astra.log`), 0011.
No Codex plan critique. The design is PR 8b's measured per-column option, carried onto the band's own columns.
Every claim below is read from the code or from a probe recorded here.
Status: **approved by Joe, 2026-10-04**: D1 column alone, D2, D3, D4, D5 Tier 2, as recommended, with six
amendments folded in below: (1) the 4-byte difference from PR 8b's D3 table is explained, not called exact, and
PLAN.md's closing line carries the build's re-measurement with the 2,701 bound; (2) T2's run shorter than one
column; (3) T5's y limits and legend texts; (4) the load check's pytest pattern excludes the check's own shell and
this worktree; (5) the gate-time number of record; (6) the plan write itself.

## Context

After PR 8b's band reduction, the failing-run overlays are most of a rough signal's envelope. In R9's alpha
envelope (58 failing runs of 10 s) they are 6.7 MB of 7.5 MB, and at 16 runs of 300 s they are 2.9 MB of 4.2 MB.
Each overlay is drawn from every frame, and matplotlib's default 1/9 px simplification barely shortens a rough
line. The outcome sought: each overlay costs a bounded number of points whatever the duration, it still shows
every failing run under its index, and everything else the framework writes stays byte-identical to `fa3a3c0`.

## Repository findings (read at `fa3a3c0`)

- `draw_envelope` (`mc_analysis.py:1082`) draws each failing run as `ax.plot(t, y, linewidth=0.8, label=f"run
  {run}")` from `env.failing[run] = (frames * dt, values)`. Each is a full copy of its log column
  (`envelope`, `:969`). The legend shows the lowest ten runs, then `and k more`.
- `_band` (`:1063`) reduces each band above `ENVELOPE_COLUMNS + 1 = 901` frames, over the columns
  `e_j = j (n - 1) // 900`, which share their boundary frames. It reads `ENVELOPE_COLUMNS` at call time, so the
  tests patch it (to 37).
- `envelope` refuses a run whose frames are not a prefix of the longest run's (`:946`). So a halted run's frame k
  is the envelope's frame k, and the envelope's columns apply to it directly.
- PR 6's overlay check is inside the R5 test
  (`test_mc_analysis_envelope_equals_numpy_over_the_runs_that_reached_each_frame`, `:206`). It compares the
  overlaid labels with the halted runs, and each line's `get_xdata()`/`get_ydata()` with `_trace`, an independent
  `astra.log.load` read of the log. The descent batch has halted runs at several frames, but its envelopes are 500
  and 50 frames long, below the threshold.
- `report.batch_figures` (`report.py:248`) calls `draw_envelope`, so one change covers `mc analyze` and the batch
  report. `docs/architecture.md:431` says only that "the failing runs are overlaid by index", which stays true.
- `-k envelope_overlay` collects nothing today (0 of 1,834). `-k mc_analysis` collects 86.
- matplotlib 3.11.2 builds a line's path at `add_line` (for the data limits) and, for a sorted line of more than
  1,000 points, rebuilds it at draw time (`Line2D._subslice`). So an `rc_context` around `ax.plot` alone does not
  reach the drawn path of a long line. The probe's first simplify attempt silently drew the leave figure, byte for
  byte.

## Probes (2026-10-04, scratchpad only; Joe's permission in plan mode, with his conditions)

`S` = `/tmp/claude-1000/-home-joe-repos-ASTRA/d8c112ed-b50b-4a87-a08a-0c97b5b974b5/scratchpad/probe`.
**To look at:** `S/index.html` (every option's SVG with its size and pixel comparison), and the SVGs in `S/svg/`.
- **Tree:** the main checkout at `fa3a3c0`, clean, with no build, configure or install. `astra.__file__ =
  /home/joe/repos/ASTRA/python/astra/__init__.py`, matplotlib 3.11.2, numpy 2.5.3 (printed in
  `S/overlay_options.out`). The 1-minute load was 1.4 to 2.6 on 32 CPUs (`S/load.txt`).
- **Inputs:** PR 8b's batches, read in place from session `3ffe2739`'s scratchpad
  (`/tmp/claude-1000/-home-joe-repos-ASTRA/3ffe2739-bd00-47fc-8ada-ce8a3677ad4c/scratchpad/batches/{130x10,16x60,16x300}`).
  PR 8b recorded no digest for them, so the probe checked them as follows before drawing anything:
  - Each summary's stored spec is `tests/python/mc_planted_fixtures/planted.yaml` byte for byte (130 × 10 s), or
    that file with only `duration:` and `runs:` changed (16 × 60 s, 16 × 300 s). Its seed is 707.
  - Its stored config equals `aircraft/notional_transport/configs/cruise.yaml` at `fa3a3c0`.
  - The row and log counts are 130, 16 and 16, all `ok`. The envelopes' frame counts are 10,000, 60,000 and
    300,000.
  - The leave figure reproduces 2579fd7's six envelope totals and overlay rows byte for byte, e.g. alpha
    7,509,957 / 6,716,272, 2,769,857 / 1,898,886 and 4,218,635 / 2,894,241.

  Digests, recorded now. They only tie the plan's SVGs to their inputs; the build regenerates the batches.
  Summary sha256, then the batch digest (each file's name and sha256, in order):
  `130x10` `0da9a664…5932`, `c24d4c47…fb42`; `16x60` `a6aa302f…b0ce`, `c525c29e…f4d1`; `16x300` `51f22a70…1fd08`,
  `222a5ba1…1dae`. Full values are in `S/sizes.json`.
- **Scripts** (the probe's own candidate code; none of it enters the repo): `S/overlay_options.py` draws and
  measures every option under `plot.pinned_style` through `plot.svg_bytes`, as `mc analyze` writes. `S/semantics.py`
  checks the column candidate's NaN and halt cases against a plain frame-by-frame reference. `S/pixels.py` compares
  each option with leave as PNG at the pinned 100 dpi.
- **Row rules (PR 8b D4):** total = `len(svg_bytes)`; polygons = the two fill groups' byte spans; overlays = the
  figure minus the same figure drawn without the failing-run lines; rest = total − polygons − overlays.

## D1, the reduction (recommendation: "column", alone)

**Sizes** (bytes; the envelopes Joe asked for are drawn in `S/svg/`):

| | R9 alpha, 130 × 10 s (58 failing) | 16 × 60 s alpha (12) | 16 × 300 s alpha (12) | R9 altitude | 16 × 300 s altitude |
|---|---:|---:|---:|---:|---:|
| **leave** (fa3a3c0), total / overlays | 7,509,957 / 6,716,272 | 2,769,857 / 1,898,886 | 4,218,635 / 2,894,241 | 302,980 / 75,318 | 317,480 / 66,309 |
| **column**, total / overlays | **3,290,932 / 2,497,247** | **1,303,774 / 432,803** | **1,844,394 / 520,000** | 302,938 / 75,276 | 316,894 / 65,723 |
| cap 10, total / overlays | 1,969,076 / 1,175,391 | 2,497,051 / 1,626,080 | 3,827,625 / 2,503,231 | 246,650 / 18,988 | 308,081 / 56,910 |
| simplify 1 px (overlays only), total / overlays | 2,059,386 / 1,265,701 | 1,075,804 / 204,833 | 1,633,402 / 309,008 | 266,470 / 38,808 | 278,797 / 27,626 |
| column + simplify 1 px, total / overlays | 1,682,467 / 888,782 | 952,964 / 81,993 | 1,469,976 / 145,582 | 266,497 / 38,835 | 278,985 / 27,814 |
| most points in one overlay, column (of the full length) | 2,134 (10,000) | 2,450 (60,000) | 2,589 (300,000) | 905 | 923 |

The column overlays equal PR 8b's per-column numbers as its evidence log re-measured them at 2579fd7 (2,497,247;
432,803; 520,000; 75,276; 38,397; 65,723). That holds because no run in these batches halts, so each run's own
columns are the envelope's. **They are not PR 8b's D3 table** (amendment 1): R9's alpha there reads 2,497,251 for
per-column and 6,716,276 for leave, 4 bytes more. The probe (`S/four_bytes.out`) found the cause:
- The row rule is the same in both: one figure difference, with the `and k more` entry going with the overlays.
- The band is what differs. PR 8b's prototype drew D3's planning figures with its pre-R-001 `step="post"` band;
  the evidence log's figures used the shipped band.
- Outside the two fill groups, both pairs of figures are byte-identical: 7,324,323 bytes with overlays, 607,993
  without.
- Inside them, drawing the overlays narrows the axes (the legend gains the run labels), which rewrites the band
  polygons' coordinates. The prototype band shrinks by 54 bytes, the shipped one by 58. The overlays row is a
  whole-figure difference, so it absorbs that 4-byte gap.

The number PLAN.md's closing line carries is the build's re-measurement (D4), not either of these.
"Simplify" here is scoped to the overlays (below), so its numbers differ slightly from PR 8b's figure-wide
measurement.

**PNG at 100 dpi against leave** (`S/pixels.out`; 576,000 pixels each; differing / differing by more than a
quarter of full scale):

| | R9 alpha | 16 × 300 s alpha | R9 altitude | 16 × 300 s altitude |
|---|---:|---:|---:|---:|
| column | 7.49 % / 1,736 | 8.11 % / 2,496 | 3.42 % / 0 | 2.41 % / 0 |
| cap 10 | 22.65 % / 89,150 | 28.34 % / 119,059 | 15.32 % / 50,804 | 14.82 % / 39,796 |
| simplify 1 px | 9.10 % / 6,616 | 8.95 % / 6,626 | 5.80 % / 661 | 3.41 % / 2,158 |

So no option is pixel-identical. Most of column's differences are anti-aliasing shades. Strong differences are
0.3 to 0.4 % of pixels on alpha (a rough line whose path inside a sub-pixel column differs) and none on altitude.
Side by side at full size, the 16 × 300 s alpha leave and column PNGs look the same to me (`S/png/`).

### The options

**Column (recommended).** Changes a new private `_overlay(n, y)` beside `_band`, plus `draw_envelope`'s overlay
loop, which plots `t[index], y[index]`. `Envelope` keeps every frame, so its API, `envelope()` and the R-001
copy test are unchanged.
- *What it draws.* The columns are the band's own: `e_j = j (n - 1) // ENVELOPE_COLUMNS` over the envelope's `n`
  frames. They share their boundary frames and are clipped to the run's own frames, since a halted run is a prefix.
  Each maximal stretch of finite frames is cut at those edges into pieces. Each piece keeps its first frame, its
  first-occurring minimum, its first-occurring maximum and its last frame. The run's first and last frames are
  always kept, and so is the first NaN after each finite stretch. Every kept point is one of the run's own
  samples, exact, in time order; nothing is interpolated.
- *What that preserves.* In every column, the drawn line spans exactly the run's minimum to maximum there.
  Neighboring columns join at the shared boundary frame, so the line is continuous through real samples. The
  run's global minimum and maximum are kept, so autoscaling, ticks and the legend are unchanged; the statistic
  lines' SVG groups are byte-identical in every probe figure. What changes is where, inside a column of at most one
  pixel at the save dpi (PR 8b's T6), the line passes between those points.
- *R5.* Its wording holds: every failing run is still a `Line2D` labeled `run <i>`. **PR 6's overlay check
  changes.** The line's data is no longer the log column but its column reduction. Below the threshold the
  reduction is the column itself, so the descent batch's check is unchanged in substance (D3).
- *Threshold.* The same as the band's, applied to the envelope's frame count (`n - 1 > ENVELOPE_COLUMNS`), because
  the columns are the band's. At or below 901 frames each column is one frame step and every frame would be kept
  anyway. Short-batch envelopes (the descent batch's) stay byte-identical. A halted run inside a long envelope is
  reduced on the envelope's grid, whatever its own length.
- *A run that halts early.* Its last frame is always kept, so the overlay ends exactly there; its last, partial
  column runs from `e_j` to that frame. Checked on a run halted at frame 123,456 of 300,001: 1,070 points, the last
  at 123,456.
- *An all-NaN column.* The line breaks across it, and the finite frames either side are kept. The line ends at the
  last finite frame before the gap and resumes at the first one after it, to the frame, so the gap is as wide as
  the full line's and the neighbors keep their width (PR 8b's R-001, avoided by construction). One NaN frame is
  kept to make the break. Checked on column 450 of 300,001 frames: kept …149,999, 150,000 (NaN), 150,334….
- *A column with one finite frame.* That frame is kept, with a NaN on each side: an isolated vertex, which
  matplotlib draws as nothing, exactly as it draws that frame in the full line. The data still holds it (checked:
  frame 150,200 = 7.0, between NaNs at 150,000 and 150,201).
- *A NaN stretch strictly inside a column.* The same rule applies: both of its edges are exact, unlike the band,
  which draws over it.
- *The bound.* A trace with no NaN keeps at most `3 × ENVELOPE_COLUMNS + 1` = **2,701 points**. PLAN.md's 3,600
  counted four per column without the shared boundary frame. With `r ≥ 1` finite stretches the bound is at most
  `3 C + 5 r − 1`, and an all-NaN trace keeps 2. NaN in a logged signal is pathological, and the full line would
  draw each such gap anyway. Measured: at most 2,589 points on the 16 × 300 s alpha, of 300,000.

**Cap at 10 runs (not recommended).** Changes `draw_envelope`'s overlay loop.
- R5 changes: runs are hidden from the figure.
- The check would become "the lowest ten failing runs", and `and k more` would no longer mean that the runs are
  drawn.
- It bounds the count, not each line: 16 × 300 s alpha stays at 3.8 MB.
- It moves the legend's width and so the whole axes: every element's bytes change, the statistic lines' included.
- Halted runs and NaN are unchanged, since the lines that remain are full. There is no frame threshold.

**Simplify 1 px, scoped to the overlays (not recommended).** It would change `draw_envelope`'s overlay loop, which
would have to reach matplotlib's path at both build points: `add_line` for a line of at most 1,000 points, and draw
time for a longer one (Repository findings). That relies on private matplotlib behavior, and the probe's first
attempt failed silently.
- R5 holds, and PR 6's check passes unchanged in letter, because `get_ydata()` stays full. But the check would no
  longer read what is drawn: the renderer simplifies the line.
- It is not bounded: the result depends on the signal and on the layout's pixel scale.
- It allows a 1 px deviation, and has no frame threshold.
- Halted runs and NaN behave as matplotlib's simplifier treats them, which I did not probe further.

**Column + simplify 1 px (not recommended).** The smallest option (146 KB of overlays at 16 × 300 s alpha), but it
inherits simplify's scoping fragility and its unbounded 1 px deviation.

## D2, what must not change: before/after against `fa3a3c0` (PR 12's D3 method)

- **How.** `git archive fa3a3c0 python/astra aircraft` into `scratchpad/base/`. There is no C++, so both sides use
  the worktree's one build (its `_core` and plugin dir).
  - The base side runs with an empty `_editable_skbc_astra` shim first on `PYTHONPATH`, then `scratchpad/base`.
  - Each side prints `astra.__file__`, `astra._core.__file__`, `_core.default_plugin_dir()` and the resolved path
    of every package file it draws, and asserts a spawned worker's `astra.__file__` (lessons 2026-10-04). Each
    side draws its own tree's packages.
  - Inputs are produced once, before the first product edit. Both sides read them, write to the same output path
    (renamed afterwards) from the same cwd, and run at one worker count (4).
- **Control.** The base side runs twice and its two sha256 manifests must be identical. Then the candidate runs.
  The manifests are diffed, then `cmp` runs on each mismatch.
- **Fixtures:**
  - logs: the `elevator_step` scenario log and a 60 s notional `cruise.yaml` log;
  - the descent batch (`tests/python/mc_analysis_fixtures/descent_batch.yaml`);
  - R9's batch (`planted.yaml`).
- **Outputs:**
  - `astra plot` of every shipped package as SVG on both logs (`flight`, `attitude`, `health`, the template's
    `controls`, `wrench` and `ground_track`, notional `cruise`);
  - each package's `--html` plotly page on both logs;
  - `astra report` on both logs;
  - `astra mc analyze` on the descent batch (`--signal v0.derived.altitude --signal v0.slow_rate.value`) and on R9
    (its criterion, `--signal v0.derived.alpha --signal v0.derived.altitude`), with figures and stdout (stdout
    normalized for each side's output directory);
  - `astra report` on both batches;
  - `astra mc rerun` of one descent run.
- **Expected differences, stated before the run, and only these:** R9's `mc-envelope-v0.derived.alpha.svg` and
  `mc-envelope-v0.derived.altitude.svg`, and R9's batch report. Inside each, only the failing-run line groups may
  differ:
  - In each SVG, after the five statistic lines' groups, the next `F` `<g id="line2d_<k>">` groups (in the report,
    `fig<k>-line2d_<k>`) are the overlays, `F` being the failing-run count (58).
  - Each such group is replaced by a placeholder on both sides, and the remainders must be byte-identical. That
    covers the bands, the statistic lines, the count, the axes, the legend and the text.
  - Each candidate group is no larger than its base group.
  - The report also compares its non-figure bytes as one string, and its figure count, as PR 12 did.
- Everything else is byte-identical, the descent batch's envelopes and report included (500 and 50 frames, below
  the threshold), and so are every plot figure, every plotly page, the log reports and `mc rerun`'s figures.
- The PR body records the file counts, the control's manifest digest, the expected-difference list and each side's
  printed paths. If `python/` changes after this step (a review fix included), the candidate side reruns on the
  final head.

## D3, tests (`tests/python/test_mc_analysis.py`; acceptance `.venv/bin/pytest tests/python -k envelope_overlay`)

Every new test is named `test_mc_analysis_envelope_overlay_*`. The reference reduction is
`_reference_overlay(edges, y)`, a plain frame-by-frame loop in the test file, written from the docstring's rule and
never calling `mc_analysis` (`S/semantics.py`'s reference, which agreed with the candidate on all six probe cases).
Column edges are read from the drawn band's path with the existing `_edges` helper (PR 8b's change 4), never
recomputed with the implementation's formula.
- **T1** `..._overlay_is_the_reduction_of_each_failing_runs_log[signal × shipped|columns_37]`: PR 6's overlay
  block moves here from the R5 test.
  - The overlaid labels are exactly the failing runs.
  - Each line's x and y equal `_reference_overlay` of the log column read by `_trace`.
  - As shipped (below the threshold), that is the log column itself, which is PR 6's check exactly.
  - At 37 columns, the descent batch's halted runs are reduced on the envelope's grid: real halted runs, with each
    line's last x at its run's last frame.
  - The R5 test keeps its statistics, count, labels and suptitle checks, unchanged.
- **T2** `..._overlay_of_a_halted_run_ends_at_its_last_frame`: a synthetic 300,001-frame envelope with one failing
  run halted at frame 123,456. Its line equals the reference, its last point is frame 123,456, and every band edge
  up to that frame is one of its points (columns are the envelope's, not the run's own). Second case (amendment
  2), the shape a long batch's early halt takes: a failing run of 200 frames in the same envelope, shorter than
  one column (333 frames). Its line is exactly its first, minimum, maximum and last frame, in time order, and ends
  at its last frame.
- **T3** `..._overlay_keeps_the_frames_either_side_of_an_all_nan_column`: column 450 all NaN. The line holds
  149,999 (finite), then a NaN, then 150,334 (finite), with no finite point between, and equals the reference.
- **T4** `..._overlay_keeps_a_columns_one_finite_frame`: column 450 NaN except frame 150,200. That frame is in the
  line with a NaN on each side, and the line equals the reference.
- **T5** `..._overlay_spans_each_columns_extent` (independent of the reference):
  - The drawn points are a subsequence of the run's samples: x are frame times, strictly increasing, and y equals
    the log at those frames.
  - The first and last frames are present.
  - In every column, the drawn y range equals `nanmin`/`nanmax` of the run there.
  - Has teeth: a stride decimation (every k-th frame) fails this check at a one-frame spike, and the shipped line
    passes.
  - Autoscaling and legend unchanged (amendment 3): after `draw_without_rendering()`, the reduced figure's y limits
    and legend texts equal those of the same envelope drawn from the full lines. The test builds the full-line
    figure itself, by replacing each overlay's data with its whole trace (`set_data`), not through any
    `mc_analysis` path.
- **T6** `..._overlay_vertex_count_is_bounded_independent_of_run_length`: rough traces of 1, 2, 901, 902, 10,001
  and 300,001 frames.
  - The count is `n` up to 901 frames, with the line equal to the trace; that is the threshold.
  - From 902 frames it is at most `3 × ENVELOPE_COLUMNS + 1`.
  - A trace with `r` NaN-separated stretches stays within `3 C + 5 r − 1`.

**What else `-k envelope_overlay` selects:** nothing today (0 of 1,834 collected). After the change, only T1 to
T6, which `--collect-only -q` confirms before the gate; the PR body lists them. The regression run is
`-k 'mc_analysis or report'` (PR 8b's expression; it adds `test_report.py` and seven substring matches, listed in
the PR body).

**Gate time.** PR 13 adds no C++, so its gate cost is the pytest delta. The PR body labels the two parts as
follows (amendment 5):
- **The number of record:** T1 to T6's summed `--durations=0`, and the `test_mc_analysis.py` pair (its summed
  durations at base and at candidate).
- **Context only:** the whole `.venv/bin/pytest tests/python` wall time, base then candidate, back to back in the
  worktree, one run each, noise stated.

Each run records `/proc/loadavg` before and after, plus the load check below.

## D4, sizes after the change (PR body, beside PR 8b's table)

The build regenerates the three batches in the worktree (R9's `planted.yaml` as-is, and with 16 runs of 60 s and
of 300 s, seed 707), producing them once with the load recorded. It re-measures with the real code under the row
rules:
- for each batch, alpha and altitude: total, polygons, overlays, rest, before (`fa3a3c0`) and after;
- the batch report with alpha alone, and with alpha and altitude;
- the most points in one overlay.

Expected from the probe: alpha 7.51 → 3.29 MB, 2.77 → 1.30 MB and 4.22 → 1.84 MB; altitude almost unchanged
(about −0.04 to −0.6 KB). The PR body sets this table beside PR 8b's.

## D5, risk tier: Tier 2

## Files

- `python/astra/mc_analysis.py`: `_overlay`, its use in `draw_envelope`, and the module docstring's Figures
  paragraph: the overlays' reduction contract, replacing "the overlays keep every frame".
- `tests/python/test_mc_analysis.py`: T1 to T6, `_reference_overlay`, and the overlay block moved out of the R5
  test.
- Docs, in `/wrap`'s docs step before the final gate where it can:
  - `PLAN.md`: a PR 13 line under M6, and the overlay item closed with this PR's evidence. The closing line
    carries the build's re-measured sizes (D4) and the bound: at most 2,701 points per overlay for a trace with no
    NaN, not the item's 3,600. Neighboring columns share their boundary frame, so each column adds at most three
    new points (amendment 1);
  - `STATE.md`: next step PR 14, then PR 10;
  - this plan;
  - the review record;
  - the gate and before/after evidence.
- `docs/architecture.md:431` needs no change.

## Verification and process

1. After approval: enter the worktree (`EnterWorktree m6-pr13-envelope-overlays`, done) and commit this plan as
   `docs/plans/m6-pr13-envelope-overlays.md`. Push, then open the draft PR.
2. `just python` in the worktree. Produce D2's inputs and D4's batches, and run D2's base side twice, before any
   product edit.
3. Implement `_overlay`, then T1 to T6, in coherent commits, pushing after each. Focused checks:
   - `.venv/bin/pytest tests/python -k envelope_overlay`, then `-k 'mc_analysis or report'` and `-k plot`;
   - `just ruff`, `.venv/bin/pyright`, `just docs-check` and `git diff --check`.
4. D2's candidate side, D4's re-measurement and the gate-time runs. The before/after summary is committed as
   `docs/reviews/evidence/<head>-before-after.log`.
5. **Load check (lessons 2026-10-04):**
   - `pgrep -af '^just (check|test|build)'` and `pgrep -af 'python[0-9.]* -m [p]ytest'` (plus `bin/[p]ytest`). The
     bracket form keeps the pattern from matching the check's own shell (amendment 4);
   - before any hit is read as another project's run, exclude a hit whose pid is the check's shell (`$$`) or one
     of its ancestors, or whose cwd is this worktree;
   - for each remaining hit, `readlink /proc/<pid>/cwd` names whose checkout it is;
   - the other project's `just autopilot` dispatcher is ignored;
   - `ps -C clangd -o pid,pcpu,etime` must show clangd idle;
   - the 1-minute load and other projects' builds are recorded before and after each measured run.

   Then `rm -rf build && ./scripts/check-full.sh` in the background, with no edits during the run. It passes on
   exit 0 plus `check: OK`. Commit the trimmed `docs/reviews/evidence/<head>-gate-summary.log`.
6. `/review-cycle` (Codex, fresh context, `--base origin/main`), with at most two correction rounds. The #94
   gate-rerun rule applies from the start: after a passing gate, a Markdown- or evidence-only delta needs only
   `just docs-check` and `git diff --check <gated-head>..HEAD`, named in the PR body. Anything else makes the gate
   stale.
7. `/human-review`: the PR body per `docs/templates/pr-description.md`, with D1 to D5, both size tables, the
   before/after, the gate-time numbers with load, and the `-k` lists.
8. `/wrap` before merge, with its docs step before the final gate where it can.

## Decisions (Joe, 2026-10-04: all as recommended, with the six amendments in the status line)

- **D1:** "column", alone. Over more than 901 frames, each overlay keeps per piece (a finite stretch inside one of
  the band's 900 columns) its first, minimum, maximum and last frame, plus the run's first and last frames and the
  first NaN after each stretch. That is at most 2,701 points for a trace with no NaN. R5's wording stands. PR 6's
  check becomes "the line equals the column reduction of the log column".
- **D2:** the before/after method, fixtures and expected-difference list above.
- **D3:** T1 to T6, with `envelope_overlay` as the one keyword, and the overlay block moved out of the R5 test.
- **D4:** the size table re-measured in the build, beside PR 8b's.
- **D5:** Tier 2.
