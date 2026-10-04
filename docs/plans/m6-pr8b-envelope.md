# M6 PR 8b: the batch envelope's size

Spec: PLAN.md's (M6) batch-envelope size item (its numbers and proposal from [PR 8's plan](m6-pr8-report.md),
Size and D1b); the [M6 plan](m6-analysis-tooling.md)'s R5 and C-10; [PR 6's plan](m6-pr6-mc-analysis.md) D8
(the envelope figure and its overlay contract).
Risk: **Tier 2** (one drawing function in `python/astra/mc_analysis.py` and its tests; no `core/`, `plugins/`,
`icd/`, `tests/golden/**`, tolerance file or ADR; no new dependency).
Base: `origin/main` `8055ce7` (#95 merged). Worktree/branch: `worktree-m6-pr8b-envelope` (this session's own).
Binding ADRs: 0010 (x = `frame * dt`), 0025/0027 (logs read through `astra.log`), 0011 (`v0` is a name, not an
assumption). No Codex plan critique: the design is the one D1b named, and its sizes are measured below.
Status: **approved by Joe, 2026-10-03, with six changes, folded in below**: (1) D1 reduces only above
`ENVELOPE_COLUMNS + 1` frames; (2) T1 also runs with a small patched column count, plus an unreduced-band
check; (3) T6 asserts figure width × pinned dpi; (4) T2 and T4 read the column edges from the drawn path;
(5) D5's after half reruns on the final candidate if `python/` changes after step 4; (6) D4 states how each
row is measured. D3: leave, with the narrower overlay item, milestone M6.

## Context

`astra mc analyze`'s envelope figure (and so the batch report, which inlines it) grows linearly with the run
length because matplotlib never simplifies a `fill_between` polygon: about 51 bytes per frame per band, 31 MB
of a 35 MB figure for 16 runs of 300 s. This PR replaces each of the two bands (min–max and 5–95 %) with a
bounded step polygon that still contains every sample. Everything else in the figure keeps its full data, so
R5's checks stand as written. The failing-run overlays, the item's second cost, stay as PR 6's contract has
them unless Joe decides otherwise (D3).

## Repository findings (read at `8055ce7`)

- `mc_analysis.draw_envelope(env)` (`python/astra/mc_analysis.py:1052`) draws, on a 9.0 × 6.4 in constrained
  figure: two `ax.fill_between(env.t, lo, hi, linewidth=0.0)` bands (min–max `#d9d9d9`, then 5–95 % `#9ecae1`);
  five statistic `Line2D`s (max, 95 %, median, 5 %, min) over every frame; one `Line2D` per failing run
  labeled `run <i>`; the count as a `steps-post` line in a second axes. `report.batch_figures`
  (`python/astra/report.py:235`) and `render_figures` both call it, so one change covers both outputs.
- `Envelope` (`mc_analysis.py:878`) holds `t`, `minimum`, `maximum` and `percentiles` per frame of the longest
  run. Every frame has at least one run, so the arrays have no gaps by construction; a NaN can only come from
  a NaN in a log.
- Pinned style (`python/astra/plot.py:607`): `figure.dpi` and `savefig.dpi` are both 100. Measured axes widths
  of the envelope at 100 dpi: 663 to 686 px. They vary with the legend's text (signal name, run labels).
- matplotlib 3.11.2: `fill_between` returns a `FillBetweenPolyCollection` with one closed path, written to SVG
  as `<g id="FillBetweenPolyCollection_<n>">` holding one `<path>`. With `step="post"` over `C + 1` column
  edges, the path has exactly `4C + 5` vertices (probed: 3,605 at C = 900, for 100,001 frames).
- The reduced band has the same data limits as the full one (same first and last `t`, same global min and
  max), so autoscaling, ticks and every other byte of the figure are unchanged (probed: identical `dataLim`,
  `xlim`, `ylim`; `sticky_edges` empty for both).
- R5's tests (`tests/python/test_mc_analysis.py:170`) check the statistic lines, the count line and each
  overlay's `Line2D` data. None reads the fill collections, so none changes.
- `-k mc_analysis` selects 75 tests at `8055ce7`, all in `test_mc_analysis.py`; nothing else matches.

## D1, the polygon reduction (recommendation)

- **Columns: a fixed count, `ENVELOPE_COLUMNS = 900`, not the axes' pixel width.** 900 is the figure's width
  (9 in) at the pinned save dpi (100). The axes is always narrower (663–686 px measured), so a column is at
  most one pixel of the axes at the save dpi, for SVG and for `--format png` alike. A width read from the
  axes would need a layout pass before the band exists (constrained layout runs at draw time). The drawn data
  would then depend on the legend's text, which changes the axes width. A fixed count is deterministic,
  independent of the renderer and the layout, and gives a vertex bound that is a constant. A guard test fails
  if the figure is ever widened or the dpi raised past it (Tests, T6).
- **Reduced only when `n - 1 > ENVELOPE_COLUMNS`** (Joe's change 1). At or below that (`n <= 901` frames),
  the exact band is drawn as today: `2n + 3 <= 1,805` vertices, inside the bound. Reducing short runs would
  make columns wider than a pixel (the descent batch's slow signal would get 49 columns, steps of about
  14 px), and would make the band up to twice today's size below about 1,800 frames. Short-batch envelopes,
  the descent batch's included, therefore stay byte-identical (D5). No `n == 1` special case is needed:
  one frame is below the threshold.
- **Columns are frame ranges that share their boundary frames.** For `n` frames, `m = ENVELOPE_COLUMNS`
  columns, with edges `e_j = (j * (n - 1)) // m`, j = 0..m (integer arithmetic, strictly increasing since
  `n - 1 > m`). Column j holds frames `e_j..e_{j+1}` inclusive. Its band is `[min lo, max hi]` over those
  frames.
- **Shape: a step per column.** `fill_between(t[e], lo_col, hi_col, step="post")`, with the last column's
  value repeated at the final edge. That gives `4m + 5` = 3,605 vertices per band for any run length.
  Because columns share boundary frames, the step band contains every sample. It also contains the full
  band's linear interpolation between neighboring samples: every segment between frames k and k+1 lies
  inside one column. A shape that interpolates between columns (a vertex per column center) would cut the
  corner at a one-frame spike, and T3 shows the check catches exactly that.
- **NaN.** A column's extent ignores NaN frames (`np.fmin`/`np.fmax` reductions), so every finite sample is
  contained. A column whose frames are all NaN is NaN, and `fill_between` leaves it as a gap, as it does
  today at a NaN frame. A NaN frame inside an otherwise finite column is drawn over; the statistic lines
  still show its gap, since they keep every frame.
- **Unchanged:** the five statistic lines, the count step, the overlays, the colors, `linewidth=0.0`, the
  z-order, the legend, `Envelope` and every caller. The module docstring's Figures paragraph gains the
  reduction's contract.

## D2, containment

- Read back from the drawn figure: `ax.collections[0]` (min–max, `#d9d9d9`) and `[1]` (5–95 %, `#9ecae1`),
  each asserted by color and by having one path. The test helper `_band_at(path, t) -> (low, high)` gives the
  band's vertical cross-section at each frame time. It first asserts that the polygon is the shape
  `fill_between` draws: one closed path whose `x` is non-decreasing up to its maximum and non-increasing
  after. Then the cross-section at any `x` is one interval. For each chain, a vertex at exactly `t` (a column
  edge) contributes every y there; otherwise the chain is interpolated within its segment. Vectorized with
  `searchsorted`, so 300,001 frames cost milliseconds.
- **Pass condition:** at every frame k with finite values, `low_k <= min_k`, `max_k <= high_k` for the
  min–max band, and the same for the 5–95 % band against `p5_k`, `p95_k`.
- **The expected values are computed independently**, not read from the drawn lines: on the descent batch,
  from each log through `astra.log.load` over the runs that reached each frame (the R5 test's computation,
  moved into a helper both tests share); on a synthetic envelope, from the arrays the test built.
- **Cases:** the descent batch, both signals (divisor 1, 500 frames; divisor 10, 50 frames), drawn as
  shipped (unreduced, below the threshold) and again with `ENVELOPE_COLUMNS` patched to 37, a non-divisor
  of 499 and 49, so real data meets multi-frame columns of unequal size (Joe's change 2). A synthetic
  envelope of 300,001 frames over a smooth base, with a one-frame spike up in the maximum and in the 95 %
  line, and down in the minimum and in the 5 % line. One spike sits inside a column and one on a column edge.
  The spike frame is the band's top in its column. A partly NaN column keeps its finite frames contained.
- **Column edges are read from the drawn path** (Joe's change 4), never recomputed with the implementation's
  formula: the distinct `x` values of the step chain are the edges. T2 and T4 assert that they are frame
  times, strictly increasing, start at the first frame and end at the last, number `ENVELOPE_COLUMNS + 1`, and
  that the column widths in frames differ by at most one.
- **Tightness (no inflated band):** in every column (between two read edges, inclusive), the band's top
  equals the maximum and its bottom the minimum over that column's frames, read from the path. That excludes
  a band that passes by covering everything.
- **The threshold:** an envelope of 901 frames draws the unreduced band (its path's vertices equal those of
  `fill_between(env.t, lo, hi)` drawn directly), and one of 902 frames draws the reduced one.
- **The check has teeth:** the helper applied to the full `fill_between` band returns exactly
  `[min_k, max_k]` at every frame (it reads a polygon correctly). Applied to a band that interpolates between
  column centers, it reports the spike frame as not contained.

## D3, the overlays (Joe's decision: leave, with a narrower open item in M6)

After the reduction, the overlays are most of what is left for a rough signal (bytes of the overlays alone,
from the same figures, polygons reduced):

| Overlays of `v0.derived.alpha` | 130 × 10 s (58 failing) | 16 × 60 s (12) | 16 × 300 s (12) |
|---|---:|---:|---:|
| the two reduced polygons, for comparison | 185,746 | 185,791 | 185,721 |
| **leave** (PR 6's contract) | **6,716,276** | **1,898,886** | **2,894,241** |
| cap at 10 (the lowest indices, as the legend) | 1,175,391 | 1,626,080 | 2,503,231 |
| reduce: per-column first/min/max/last points, 900 columns | 2,497,251 | 432,803 | 520,000 |
| reduce: simplify threshold 1 px (from the pinned 1/9 px) | 1,269,662 | 215,279 | 344,568 |

`v0.derived.altitude`'s overlays (smooth) are 75,318, 38,622 and 66,309 bytes left as they are.

- **Leave.** Nothing changes: R5 ("every failing run is overlaid under its index") and its `Line2D` check
  stand. The cost is the failing-run count times what each line does on screen: R9's alpha envelope stays at
  7.5 MB, 89 % overlays.
- **Cap.** Bounds the count, not each line: little help at 12 failing runs. It changes R5's requirement in the
  M6 plan and PR 6's contract, and hides runs from the figure. Not recommended.
- **Reduce, per column** (min–max line decimation over the same 900 columns). Each overlay is at most
  `4 × 900` points, whatever the duration or the signal, and it keeps each sample's column extent, so it
  draws the same pixels at 100 dpi. It changes PR 6's overlay check: the line's data would be the column
  reduction of the log column, not the column itself. R5's wording ("overlaid under its index") holds.
- **Reduce, simplify threshold.** Smaller, but not bounded (it depends on the signal), and it allows a 1 px
  deviation. matplotlib reads the threshold from `rcParams` when it builds a line's path, so scoping it to the
  overlays alone is fragile. Raising it for the whole figure would change the statistic lines' drawn paths too
  and break this PR's byte-identity check.
- Rasterizing the overlays is excluded: it needs an `<image>` with a `data:` URI, which the report's
  allow-list (PR 8, D9) refuses.

**Decided (Joe, 2026-10-03): leave in this PR.** The batch-envelope item closes, and a narrower open item for
the overlays, milestone **M6**, carries this table (re-measured in the build) and names the per-column
reduction as the bounded option.

## D4, size (measured 2026-10-03 at `8055ce7`, scratchpad only; "after" from a scratchpad prototype)

The batches are R9's `planted.yaml` (seed 707, criterion `max.v0.derived.alpha > 0.23`) as-is (130 × 10 s, 58
failing), and with 16 runs of 60 s and of 300 s (12 failing each). The "before" sizes reproduce PR 8's table
byte for byte, and the prototype's unreduced copy of `draw_envelope` gives the shipped bytes exactly, so the
"after" column differs only by the reduction. At 10,000 frames and more, every envelope here is above the
threshold, so change 1 does not move these numbers.

**How each row is measured** (Joe's change 6). The planning rows below were figure differences: "polygons"
was the figure minus the same figure drawn without the two bands; "overlays" the figure minus the figure
without overlays; "lines, count, axes and text" the figure with neither. Those three do not sum to the total
exactly (54 bytes off at 10 s). PR 8's polygon figures (514,393 + 514,427 = 1,028,820 at 10 s, against
1,028,622 here) were the two groups' byte spans, which include the `<g>` wrapper that the difference leaves
out. The build's re-measurement uses one rule per row so that the rows reconcile:
- **polygons**: the sum of the two `<g id="FillBetweenPolyCollection_<n>">` groups' byte spans (start tag
  through `</g>`) in the figure as drawn, PR 8's method;
- **overlays**: the figure minus the same figure drawn without the failing-run lines (one figure
  difference);
- **the rest** (statistic lines, count, axes, legend and text): the total minus the two rows above, by
  definition, so polygons + overlays + rest = total exactly;
- **totals**: `len(svg_bytes(...))` of the figure as `astra mc analyze` writes it, and the length of the
  report's UTF-8 bytes.

The planning numbers (figure differences throughout), in bytes:

| | 130 × 10 s | 16 × 60 s | 16 × 300 s |
|---|---:|---:|---:|
| frames | 10,000 | 60,000 | 300,000 |
| `mc-envelope-v0.derived.alpha` before | 8,352,937 | 8,754,763 | 34,883,756 |
| **after** | **7,510,061** | **2,769,958** | **4,218,739** |
| its two polygons, before → after | 1,028,622 → 185,746 | 6,170,596 → 185,791 | 30,850,738 → 185,721 |
| its overlays (unchanged) | 6,716,276 | 1,898,886 | 2,894,241 |
| its lines, count, axes and text (unchanged) | 607,985 | 685,321 | 1,138,705 |
| `mc-envelope-v0.derived.altitude` before | 1,146,797 | 6,264,923 | 31,010,691 |
| **after** | **303,084** | **275,767** | **317,584** |
| batch report, alpha envelope, before → after | 8,712,996 → 7,870,120 | 9,027,536 → 3,042,731 | 35,156,553 → 4,491,536 |
| batch report, both envelopes, before → after | 9,862,415 → 8,175,826 | 15,294,806 → 3,320,845 | 66,169,646 → 4,811,522 |

- The reduced polygons are a constant 186 KB per figure (two bands of 3,605 vertices), whatever the run
  length. For comparison, 1,800 columns gave 371 KB.
- What still grows with duration is the five statistic lines (simplified, but alpha is rough: 0.6 to 1.1 MB)
  and the overlays (D3). The statistic lines are out of scope: R5 checks their full data.
- The build re-measures all of this with the real code (step 4 below), under the row rules above, and puts
  both tables in the PR body.
- **Bound test (T5):** synthetic envelopes of 1, 2, 901, 902, 10,001 and 300,001 frames. Each band is one path
  with at most `4 * ENVELOPE_COLUMNS + 5` vertices: `2n + 3` up to 901 frames (1,805 at 901), and exactly
  3,605 for every length from 902 on.

## D5, what must not change: before/after against `8055ce7`

A scratchpad script runs twice on the same inputs with the worktree's own `.venv` (Python-only change, so
`_core` is the same build for both halves). The **before half runs at the base, before the first product
edit**: it records `git rev-parse HEAD`, `git diff --stat 8055ce7 -- python/ tests/` (empty), and `git status
--porcelain`. The **after half** runs at the candidate. That avoids the main checkout (this session's worktree
guard refuses `git -C` there). Inputs are produced once: the `elevator_step` and `atmosphere_ceiling_halt`
scenario logs, the descent batch, and R9's batch.

- **Byte-identical (`cmp`):** `astra plot` of every shipped package (framework `flight`, `attitude` and
  `health`; the template's three; notional `cruise`) on both logs; `astra mc analyze`'s `mc-hist.svg` and
  `mc-scatter.svg` and its stdout, on the descent batch (`--signal v0.derived.altitude --signal
  v0.slow_rate.value`) and on R9's batch (its criterion, `--signal v0.derived.alpha --signal
  v0.derived.altitude`); `astra report` of the `elevator_step` log.
- **Envelope figures differ only inside their two fill collections.** For each `mc-envelope-*.svg`, before and
  after: assert exactly two `<g id="FillBetweenPolyCollection_1">` / `_2` groups, each holding exactly one
  `<path .../>` and no nested `<g`. Replace each group's byte span (its start tag through its first `</g>`)
  with a fixed placeholder, and `cmp` the remainders: they must be identical. Also: the after group is
  smaller than the before group for R9's batch. The same rule covers `astra report` of R9's batch with both
  signals, where the ids are `fig<k>-FillBetweenPolyCollection_<n>`. The descent batch's envelopes (500 and
  50 frames, below the threshold) must be byte-identical outright.
- **Containment on a real batch:** the after half also runs T1's `_band_at` check on R9's alpha and altitude
  envelopes (10,000 frames, about 11 frames per column).
- **The after half runs on the final candidate** (Joe's change 5): if any `python/` file changes after step 4
  (a review fix included), the after half is rerun at the new head. The evidence log names the head each half
  ran on.
- Committed as `docs/reviews/evidence/<head>-before-after.log`: the commands, both halves' heads and git
  state, file counts, every `cmp` result, the span sizes and the containment result. Named in the PR body.

## Tests (`tests/python/test_mc_analysis.py`; acceptance `.venv/bin/pytest tests/python -k 'mc_analysis or report'`)

Every new test is named `test_mc_analysis_envelope_*`, so `-k mc_analysis` selects it by its own name and by
its module:

- T1 `test_mc_analysis_envelope_band_contains_every_frame_of_the_descent_batch[...]`, over both signals
  and two column counts: as shipped (asserting the unreduced band) and with `ENVELOPE_COLUMNS` monkeypatched
  to 37
- T1b `test_mc_analysis_envelope_of_at_most_901_frames_draws_the_unreduced_band`: 901 frames unreduced (its
  vertices equal a direct `fill_between`), 902 reduced
- T2 `test_mc_analysis_envelope_band_contains_a_one_frame_spike` (the synthetic envelope, with the partly NaN
  column; edges read from the path)
- T3 `test_mc_analysis_envelope_band_check_reads_the_full_band_and_catches_a_cut_corner` (D2, the check has
  teeth)
- T4 `test_mc_analysis_envelope_band_is_each_columns_extent` (tightness, over the columns between the edges
  read from the path)
- T5 `test_mc_analysis_envelope_band_vertex_count_is_bounded_independent_of_run_length`
- T6 `test_mc_analysis_envelope_columns_are_at_most_one_pixel_at_the_save_dpi` (Joe's change 3): the
  envelope figure's width in inches times the pinned `savefig.dpi` is `<= ENVELOPE_COLUMNS`, the invariant
  itself. As an extra, the descent envelope's axes width at that dpi, after layout, is below it too.

Existing R5 tests are unchanged. One refactor: the per-frame expected statistics in
`test_mc_analysis_envelope_equals_numpy_over_the_runs_that_reached_each_frame` move into a helper that T1
also uses, with the computation unchanged.

**What else the acceptance expression selects** (collected at `8055ce7`): `-k mc_analysis` selects only
`test_mc_analysis.py` (75 tests today). `or report` adds `test_report.py` (220) and seven substring matches:
`test_autopilot_design.py::test_one_extra_exchange_of_delay_is_reported[longitudinal|lateral]`,
`test_derivative.py::test_state_owner_slices_reports_the_correct_offset_and_count_for_a_state_owning_model`,
`test_lockstep.py::test_r2c_hold_is_reported[E_light_hold|G_moderate_hold]`,
`test_lockstep.py::test_seed_sweep_light_is_reported`,
`test_scenario.py::test_verify_reports_a_rate_group_window_without_its_rows_instead_of_raising`. 302 in all
today; 302 + the new tests after.

## Files

- `python/astra/mc_analysis.py`: `ENVELOPE_COLUMNS`, a private `_band(t, lo, hi)` reduction, its use in
  `draw_envelope`, and the module docstring's Figures paragraph.
- `tests/python/test_mc_analysis.py`: T1 to T6, `_band_at`, and the shared expected-statistics helper.
- Docs (`/wrap`'s docs step, before the final gate where it can): `PLAN.md` (the PR 8b line under M6; the
  batch-envelope item closed with this PR's evidence; D3's new overlay item, milestone M6); `STATE.md` (next step: PR 10); `docs/architecture.md:431` needs no change (it describes
  what the figure shows, not how the band is drawn); this plan; the review record; gate and before/after
  evidence.

## Decisions (Joe, 2026-10-03)

- **D1** The fixed `ENVELOPE_COLUMNS = 900` (figure width × pinned dpi), a step per column over columns that
  share boundary frames, applied only above 901 frames, with NaN frames ignored per column.
- **D2** The containment, tightness, threshold and has-teeth checks on the descent batch (as shipped and at
  37 columns) and the synthetic spike, with edges read from the path, plus the containment check on R9's
  batch in the before/after evidence.
- **D3** Overlays left as PR 6's contract has them. The batch-envelope item closes, and a narrower overlay
  item opens in M6 with the table above.
- **D4** The sizes above, re-measured in the build under the stated row rules, and the vertex-bound test.
- **D5** The before/after method: byte-identical except the two fill groups of above-threshold envelopes,
  compared by span replacement; the after half on the final candidate.

## Verification and process

1. Rename the plan-mode draft to `docs/plans/m6-pr8b-envelope.md`; commit, push, open the draft PR.
2. `just python` (the worktree's `.venv`); run D5's before half at the base, before any product edit.
3. Implement D1, then T1 to T6, in coherent commits, pushing after each. Focused:
   `.venv/bin/pytest tests/python -k 'mc_analysis or report'`, `-k plot`; `just ruff` (both steps, every
   file); `.venv/bin/pyright`; `just docs-check`; `git diff --check`.
4. D5's after half and D4's re-measurement (the three batches, both figures and both reports) with the real
   code; commit the before/after log. Rerun the after half if `python/` changes later (D5).
5. Gate time: the whole `.venv/bin/pytest tests/python` on the branch and on `8055ce7`, run back to back in this
   worktree with the 1-minute load before each, plus the new tests' own `--durations`. PR 8b adds no C++, so
   its gate cost is the pytest delta.
6. `ps` first (`pgrep -x 'just|ninja|ctest|clang-tidy-18|clang\+\+-18|clangd'` plus `'[b]in/pytest'`, each
   hit's cwd), recording other projects' load and the 1-minute load average in the evidence header. At
   planning time another project's `just check` (`Algorithmic-Trader`) was running at a load of 15 on 32
   CPUs. Then `rm -rf build && ./scripts/check-full.sh` in the background with no edits during the run; read
   its exit status and `check: OK`; commit `docs/reviews/evidence/<head>-gate-summary.log`.
7. `/review-cycle` (Codex, `--base origin/main`; at most two correction rounds; recorded in
   `docs/reviews/m6-pr8b-envelope-review.md`). The gate-rerun rule from #94 applies from the start: after a
   passing gate, a Markdown/evidence-only delta needs only `just docs-check` and `git diff --check
   <gated-head>..HEAD`, named in the PR body. Anything else makes the gate stale.
8. `/human-review` (PR body per `docs/templates/pr-description.md`: D1 to D5, both size tables, the
   before/after evidence, the gate-time figures and the `-k` extras).
9. `/wrap` before merge, its docs step before the final gate where it can: PLAN.md's item closes with this
   PR's evidence, and STATE.md's next step becomes PR 10.

## Amendments found in the build and review

- **Review round 1, R-001 (Joe; confirmed by both Codex passes;
  [review record](../reviews/m6-pr8b-envelope-review.md)).** D1's `step="post"` shape lost finite frames
  next to an all-NaN column: `fill_between` masks the NaN node before expanding the step and ends each
  region at its last finite x, so the column before the gap lost its width. Each column is now its own two
  points, `(e_j, v_j)` and `(e_(j+1), v_j)`, with no `step`. The shape, the columns and the containment
  contract are D1's; the vertex count becomes `4C + 3` = 3,603 per band (not `4C + 5` = 3,605, as Repository
  findings, D1 and D4 above state), and a band split at a NaN column stays within that bound in total. The
  sizes move by about 100 bytes per figure ([final before/after](../reviews/evidence/2579fd7-before-after.log)).
- **D5's comparison rule, refined in the build.** matplotlib 3.11 writes each fill group as `<defs>` (the
  path) plus a nested clip-path `<g>` holding a `<use>`, so a group's span runs to its depth-matched `</g>`,
  not its first, and each group must hold exactly one `<path>`, one `<use>` and one nested `<g>`. Stdout is
  compared with each half's output directory normalized.
