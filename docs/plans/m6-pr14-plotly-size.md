# M6 PR 14: the plotly page's size

Spec: PLAN.md's (M6) 25 MB item (origin: M6 PR 5; the SVG side is settled by M6 PR 8's D1a: no decimation).
PR 5's plotly contract: [its plan](m6-pr5-plots.md), the plotly renderer bullet and R3b (C-11): trace x and y
equal the log after conversion, plotly.js inline, nothing loaded from outside.
Risk: **Tier 2**. The changes are `python/astra/plot.py`'s plotly path, `astra plot`'s CLI option, and their tests
and docs. No `core/`, `plugins/`, `icd/`, `tests/golden/**`, tolerance file or ADR change, and no new dependency.
Base: `origin/main` `d851b8f` (#101 merged). Worktree `m6-pr14-plotly-size` (branch
`worktree-m6-pr14-plotly-size`), with a draft PR at the first commit.
Binding ADRs: 0010 (`t = frame * dt`), 0002 (display units through `astra._core`), 0011 (`{v}`).
No Codex plan critique: this is a renderer option over measured data, and no architecture is unsettled.
Also in this PR: Joe's Jev decision (2026-10-04), a Markdown-only deletion in PLAN.md (Process, step 4).
Status: **approved by Joe, 2026-10-04**: D1, D2 (title suffix as proposed), D3, D4, D5 Tier 2, as recommended,
and no Codex plan critique. His six amendments are folded in below:
1. The bounds hold only for a trace with no non-finite point. The docs say so, and T6 has a gapped case.
2. T1 also reads `x0`/`dx` from the written page's text.
3. T2 measures a real decimated rate group, and the PR body names every series that fell back.
4. PR 5's plan is not rewritten; it gets one pointer line.
5. The before/after also compares the 300 s log's outputs.
6. `/wrap` adds an unscheduled PLAN item for the plotly.js floor.

## Context

`astra plot --html` writes one plotly page per package. plotly 7.1.0 stores every trace's x and y as base64
float64, with every frame, so the page grows linearly with the run: 25.1 MB for the notional `cruise` package at
60 s and 105 MB at 300 s. The SVGs are not affected, because matplotlib simplifies each line before writing it. The
outcome sought: a smaller page by default with no loss, a page whose size is bounded independent of the run's
duration when the user asks for one, and every SVG, report and batch figure byte-identical to `d851b8f`.

## Repository findings (read at `d851b8f`)

- `draw_plotly` (`python/astra/plot.py:696`) adds `go.Scatter(x=line.x, y=line.y, mode="lines")` per series.
  `_html` (`:724`) is our own skeleton around `pio.to_html(..., include_plotlyjs=i == 0, div_id=<fixed>)`, so
  the page is deterministic: PR 13's D2 compared it byte for byte.
- `render(..., html=False)` (`:750`); `astra plot ... --html` (`python/astra/cli.py:259`, `:704`).
- `Series.x` is `t = frame * dt` for every YAML series (`column`, `:418`). A function package can return any x:
  the template's `ground_track` draws east against north.
- `astra.log`'s structural check makes each `/rate_<d>/frame` exactly `0, d, 2d, ...` (`log.py:32`).
- **plotly.js computes a trace's `x0`/`dx` x as `x0 + i*dx` in float64.** In the bundled 7.1.0
  `plotly/package_data/plotly.min.js`, `makeCalcdata` has `A[y]=k+y*D` with `k = d2c(x0)` and `D = Number(dx)`.
  numpy's `x0 + np.arange(n) * dx` does the same two IEEE operations, so an exact-equality check in Python
  matches what the browser computes. Note that `dx` set to 0 or missing becomes 1 (`_["d"+M]?...:1`), so `dx`
  must be positive.
- R3b's data test, `test_r3b_plotly_data_equals_the_log_after_conversion` (`tests/python/test_plot.py:1020`),
  reads `trace["x"]` through `_trace_array` (`:1008`). It compares bits (`assert_bits`).
- plotly JSON-escapes `/` as `\u002f`, 6 bytes for one base64 character. This is why y arrays of one length
  differ in size (640,000 to 840,000 bytes at 60 s).
- `astra.report` and `astra.mc_analysis` do not use plotly (architecture §12; `report.py` has inline SVG only).
- Docs that describe the page: `plot.py`'s module docstring (Renderers, `:56`), `cli.py`'s docstring (`:72`),
  and `docs/architecture.md:430` and `:435`.

## Measurements (2026-10-04, scratchpad only, before any option was written down)

`S` = `/tmp/claude-1000/-home-joe-repos-ASTRA/ff30b20c-7b03-420e-abc4-4ef114488192/scratchpad`.
- **Tree and inputs.** The main checkout at `d851b8f`, clean, with no build, configure or install, and
  `PYTHONDONTWRITEBYTECODE=1`. The `_core` build records `git_describe` `8e97040`, and no C++ has changed
  since. The software versions were plotly 7.1.0, numpy 2.5.3 and matplotlib 3.11.2. The 1-minute load was
  0.6 to 1.6, and no other build or pytest was running.
- **Logs.** The notional `aircraft/notional_transport/configs/cruise.yaml`, with only `sim.frames` changed, to
  60,000 and 300,000 (`S/logs/`). Its light Dryden turbulence is on. Both logs are complete, with 60,000 and
  300,000 rows; they are 32.9 MB and 163 MB.
- **Scripts.** `S/measure.py` and `S/candidate.py` give the base page per package, figure and trace (outputs
  `S/base_*.txt`, `S/candidate_*.txt`). `S/options.py` gives the options (outputs `S/options_*.txt`). The option
  pages are prototypes outside `astra`, without `_html`'s skeleton, about 400 bytes smaller than a real page.
  The build re-measures them with the real code.

### Base: bytes per page, figure and trace

plotly.js inline is **4,815,867 bytes**, a floor on every page.

| Page (all of `astra plot --html`) | 60 s | 300 s |
|---|---:|---:|
| `flight` | 14,757,020 | 54,280,225 |
| `attitude` | 13,736,205 | 48,681,240 |
| `health` | 11,418,036 | 37,655,486 |
| **`cruise` (notional)** | **25,120,081** | **105,389,966** |
| `controls` (template) | 10,498,874 | 33,088,834 |
| `wrench` (template) | 12,986,621 | 45,489,011 |
| `ground_track` (template) | 6,236,282 | 11,686,072 |

| Figure div | 60 s | 300 s |
|---|---:|---:|
| `flight` `air_data` (5 traces) / `aero_angles` (2) | 6,941,347 / 2,999,722 | 34,528,147 / 14,936,127 |
| `attitude` `euler` (3) / `rates` (3) | 4,477,967 / 4,442,300 | 21,868,822 / 21,996,480 |
| `health` (5) | 6,602,060 | 32,839,510 |
| `cruise` `longitudinal` (7) / `lateral` (7) | 10,241,517 / 10,062,593 | 50,889,942 / 49,684,053 |

Per trace, at 60,000 points, x is 678,710 bytes (11.3 B/point) and y is 640,000 to 840,000 bytes. At 300,000
points, x is 3,366,200 bytes and y is 3.2 to 4.1 MB. The spread is the `\u002f` escaping. So a trace is 1.32 to
1.52 MB at 60 s and about 6.6 to 7.5 MB at 300 s, and about half of every time-series trace is its x. The full
per-trace listing is in `S/candidate_*.txt`, and it goes into the evidence (Process, step 3).

### Options, `cruise` page (the item's 25 MB page)

| Option | 60 s | 300 s | Loss |
|---|---:|---:|---|
| base | 25,119,703 | 105,389,588 | none |
| **A** `x0`/`dx` for an exactly uniform x | 15,617,623 | 58,262,648 | none (bit-exact) |
| A + **B** y as float32 | 10,401,308 | 32,580,273 | y values rounded to 24 bits |
| **R** drop interior points of constant runs, else A | 12,657,885 | 43,462,890 | none as a drawing; hover points missing |
| **F** gzip the figure JSON, inflate in the page | 14,984,859 | 53,693,391 | none |
| F on A | 11,153,427 | 36,394,811 | none |
| A + **C** column reduction, 1,000 columns | 5,309,399 | 5,341,758 | zoomed detail |
| A + C, 2,000 columns | 5,759,073 | 5,812,101 | zoomed detail |
| A + C, 4,000 columns | 6,625,267 | 6,731,245 | zoomed detail |

**A + C at 2,000 columns, every page.** The most points in one trace are in parentheses.

| Page | base 60 s | A 60 s | A+C 60 s | base 300 s | A 300 s | A+C 300 s |
|---|---:|---:|---:|---:|---:|---:|
| `flight` | 14,757,020 | 10,005,701 | 5,550,633 (5,205) | 54,280,225 | 30,716,476 | 5,605,530 (5,668) |
| `attitude` | 13,736,205 | 9,663,682 | 5,146,603 (2,228) | 48,681,240 | 28,483,777 | 5,170,493 (2,556) |
| `health` | 11,418,036 | 8,024,289 | 5,045,593 (2,002) | 37,655,486 | 20,824,289 | 5,043,948 (2,002) |
| `cruise` | 25,120,081 | 15,617,623 | 5,759,073 (5,205) | 105,389,966 | 58,262,648 | 5,812,101 (5,668) |
| `controls` | 10,498,874 | 7,783,827 | 5,014,179 (2,001) | 33,088,834 | 19,623,827 | 5,012,879 (2,001) |
| `wrench` | 12,986,621 | 8,914,155 | 5,525,815 (5,190) | 45,489,011 | 25,291,605 | 5,585,130 (5,670) |
| `ground_track` | 6,236,282 | 6,236,123 | 4,870,781 (2,012) | 11,686,072 | 11,685,913 | 4,869,426 (2,012) |

- Every YAML series's x passed the exact test (`x0 + arange(n)*dx == t`, bitwise) on both logs.
  `ground_track`'s x (east) does not, so it keeps an explicit x and its A page is unchanged.
- With C, a page is about 5 to 6 MB at any duration, and plotly.js is 4.8 MB of that. The remaining floor is
  plotly.js itself (see the rejected options).

## Options and decisions for Joe

**D1, lossless default: option A. Recommendation: yes, always on.**
- `draw_plotly` writes `x0`/`dx` instead of `x` when the trace has at least 2 points, `dx = x[1] - x[0] > 0`, and
  `x[0] + np.arange(n) * dx` equals `x` bitwise (the formula plotly.js uses). Otherwise it writes an explicit
  `x`, as today.
- Saves 38 % at 60 s and 45 % at 300 s on `cruise`. It loses nothing: what the browser draws and hovers is the
  same doubles.
- **PR 5's contract:** R3b's sentence "trace x and y equal the log after conversion" stands, read as "the x the
  trace defines". The plotly renderer bullet gains one line: a uniform x is written as `x0`/`dx`.
- **Tests:** R3b's `_trace_array` becomes `_trace_x(trace)`, which reads `x` or rebuilds `x0 + arange*dx`. Its
  assertions stay bitwise. New tests are named `test_plotly_size_*` (D4).
- **The cost:** this alone leaves the 60 s `cruise` page at 15.6 MB and the 300 s one at 58 MB, so the size still
  grows linearly with the run.

**D2, bounded size: option C, column reduction. Recommendation: opt-in, `astra plot --html --html-columns N`
(`render(..., html_columns=N)`), as PLAN.md's item proposed.**
- **The rule** (PR 13's `_overlay` rule, on the trace's own index columns):
  - A trace of `n` points with `n - 1 <= N` is unchanged.
  - Otherwise the columns' edges are `e_j = j (n - 1) // N`. In each piece (a stretch of finite x and y inside one
    column), the page keeps the first and last points and the points with the minimum and maximum y and x.
  - The trace's first and last points are kept, and so is the first non-finite point after each stretch, so a NaN
    gap stays a gap.
- **Bound:** for a trace with no non-finite point, at most `3N + 1` points with increasing x (x's extremes are a
  piece's ends), and at most `5N + 1` for an x-y path such as `ground_track`. A trace with gaps keeps more: each
  stretch adds its own piece ends, plus the first non-finite point after it. The docstring, the CLI help and the
  architecture clause say so, as PR 13's did (amendment 1).
- **Why the drawn values stay true:** every drawn point is a sample of the log, so hover shows true values. At
  full view, every column's y range is drawn, so a one-frame spike is never lost. Zooming in shows the reduction,
  not every frame. plotly has no offline resampling: plotly-resampler needs a server.
- **A reduced figure says so:** its title gains `(reduced: N columns per trace)`, so a reader never takes it for
  every frame. The wording is Joe's to amend.
- **CLI:** `--html-columns` needs `--html`, and N must be an integer of at least 1. Either error exits 64.
  `render` raises `ValueError` for the same cases.
- **PR 5's contract:** the default page keeps R3b exactly, because C is off by default. A reduced page's contract
  is "each trace is the column reduction of the log after conversion", tested against a reference written in the
  test file (D4).
- **Alternative (not recommended):** C on by default at 2,000 columns, with `--html-full` to opt out. Every
  default page would then be about 5 to 6 MB. But that reverses R3b's default, and the PLAN item asked for an
  opt-in.

**D3, rejected options (recommendation: none in this PR):**
- **B, float32 y.** The page would show values that are not the log's (24-bit mantissa), silently.
- **R, constant runs.** It helps only piecewise-constant traces (health counters, open-loop controls), and C
  covers them when the user asks.
- **F, gzip with `DecompressionStream`.** It means our own inline JS loader in place of `pio.to_html`'s script,
  which R3b's static check cannot see into. On rough signals it gains only 29 to 38 % beyond A.
- **Rewriting plotly's `\u002f` escapes in its output text.** It would edit plotly's generated text, for about 5
  to 25 % of y.
- **A partial plotly.js bundle**, the 4.8 MB floor. plotly.py ships only the full bundle, so this would vendor a
  new asset (a dependency, Joe's call). It is not proposed here; Joe may open an item for it.

**D4, tests** (`tests/python/test_plot.py`; acceptance `.venv/bin/pytest tests/python -k plotly_size`, plus R3b):
- **T1** `..._uniform_x_is_x0_dx`: every YAML trace of the flight and notional packages has no `x`, and `x0 +
  arange*dx` equals the log's t bitwise. This is R3b's data test through `_trace_x`. It also reads `x0` and `dx`
  from the written page's JSON text, since the page is what the browser gets. They must parse back to the same
  doubles bitwise (amendment 2).
- **T2** `..._nonuniform_x_stays_explicit`:
  - `ground_track`, x = `[0, 1, 3]`;
  - an x one ulp off the formula, found in the test and asserted as a precondition;
  - 0, 1 and 2 points, a NaN in x, and a decreasing x;
  - a real decimated rate group: a log with a rate divisor above 1, drawn through a YAML series. This measures
    the uniform test's outcome on `i*(d*dt)` against `(i*d)*dt` rather than assuming it, and the test asserts
    whichever way it goes, which is drawn the same either way. The PR body names every shipped series that fell
    back to an explicit x on both logs (amendment 3).
- **T3** `..._reduction_equals_the_reference[...]`: against `_reference_columns`, a plain loop in the test file
  written from the docstring rule. It never calls `plot`. Cases are time traces and the `ground_track` path.
- **T4** `..._reduction_spans_each_column`, independent of the reference:
  - the drawn points are a subsequence of the samples, and the first and last are present;
  - in each column, the drawn y range is the samples' `nanmin`/`nanmax`.
  - It has teeth: a stride decimation fails it at a one-frame spike.
- **T5** `..._reduction_keeps_nan_gaps`: an all-NaN column, and a column with a single finite frame.
- **T6** `..._reduction_is_bounded`: n = 1, 2, N+1, N+2, 10N and 300,001. The count is `n` up to N+1 points,
  then at most `3N + 1`, or `5N + 1` for a path. One gapped case is allowed to exceed `3N + 1`, and stays within
  the gapped bound the docstring states (amendment 1).
- **T7** `..._reduced_title_and_default_unreduced`: a reduced figure's title has the suffix, and without
  `html_columns` the page is unreduced (T1 holds).
- **T8** `..._cli_html_columns`:
  - `--html-columns` without `--html`, and N = 0, -1 or `x`, each exit 64 and write no file;
  - with `--html`, the page's traces are within the bound.

**D5, risk tier: Tier 2.**

## What must not change: before/after against `d851b8f` (PR 12's D3 and PR 13's D2 method)

- **How.**
  - `git archive d851b8f python/astra aircraft` into `S/base/`. There is no C++, so both sides use the worktree's
    one build.
  - The base side runs with an empty `_editable_skbc_astra` shim first on `PYTHONPATH`, then `S/base`.
  - Each side prints `astra.__file__`, `astra._core.__file__`, `_core.default_plugin_dir()` and the resolved path
    of every package it draws. It also asserts a spawned worker's `astra.__file__` (lessons 2026-10-04).
  - Inputs are produced once, before the first product edit. Both sides read them, write to the same output path
    (renamed after), from one cwd, and at one worker count (4).
- **Control.** The base side runs twice, and the two sha256 manifests must be identical. Then the candidate runs.
  The manifests are diffed, then `cmp` runs on each mismatch.
- **Fixtures** (PR 13's, plus the 300 s log, amendment 5):
  - the `elevator_step` scenario log, and the 60 s and 300 s notional cruise logs;
  - the descent batch;
  - R9's batch (`planted.yaml`).
- **Outputs:**
  - `astra plot` of every shipped package as SVG on the three logs, and each package's `--html` page;
  - `astra report` on the three logs and both batches;
  - `astra mc analyze` on both batches (PR 13's signals), with figures and normalized stdout;
  - `astra mc rerun` of one descent run.
- **Expected differences, stated before the run, and only these:** the `--html` pages whose traces have a
  uniform x, that is every page except `ground_track.html`. `ground_track.html` must be byte-identical: it is the
  control for D1's fallback.
- **Inside each changed page**, on every log (both durations included), a semantic check, not only "it
  differs":
  - parse each `Plotly.newPlot` call's data, layout and config on both sides;
  - for each trace, the candidate's `x0`/`dx` rebuild equals the base `x` bitwise, and y is bitwise equal;
  - everything else in the call is equal: names, layout and config;
  - the page text outside the data arrays is byte-identical once each trace's `x` and `x0`/`dx` are replaced by
    a placeholder.
- Every SVG, every report and every batch figure must be byte-identical.
- The candidate's `--html-columns` pages are new, so they have no base: D4 tests them. Their sizes go in the PR
  body (below).
- If `python/` changes after this step (a review fix included), the candidate side reruns on the final head.

## Sizes after the change (PR body)

The build re-measures, with the real code, on the 60 s and 300 s logs regenerated in the worktree:
- every page, with `--html` (D1), and with `--html --html-columns 2000` (and 1,000 and 4,000 for `cruise`);
- the most points in one trace;
- this plan's tables beside them.

Expected: `cruise` 25.1 → about 15.6 MB (60 s) and 105 → about 58 MB (300 s) by default, and about 5.8 MB at
both durations with 2,000 columns.

## Files

- `python/astra/plot.py`:
  - `draw_plotly(figure, *, columns=None)` with `_trace_xy` (D1) and `_columns` (D2);
  - `_html`, and `render(..., html_columns=None)`;
  - the module docstring's Renderers paragraph: `x0`/`dx`, the reduction rule, its bound and the title suffix.
- `python/astra/cli.py`: `--html-columns N`, its validation, and the docstring's `astra plot` line.
- `tests/python/test_plot.py`: `_trace_x`, T1 to T8, and `_reference_columns`.
- `docs/architecture.md:430` and `:435`: one clause each, on the uniform x and the opt-in reduction (with the
  gapped bound).
- `docs/plans/m6-pr5-plots.md`: one line under its plotly bullet, pointing to this plan's D1 and D2 as the
  amendment. The bullet and R3b stay as approved, and `plot.py`'s docstring carries the current contract
  (amendment 4).
- Docs, in `/wrap`'s docs step:
  - `PLAN.md`: a PR 14 line under M6; the 25 MB item closed with the measured sizes; the Jev item deleted
    (Process, step 4). Add a new (unscheduled) open item: the plotly page's 4.8 MB floor is plotly.js inline,
    and a partial bundle would be a new vendored asset, Joe's call (amendment 6);
  - `STATE.md`: next step PR 10;
  - this plan, the review record, and the gate and before/after evidence.

## Process

1. After approval:
   - `EnterWorktree m6-pr14-plotly-size`;
   - `mv` this draft from the main checkout into the worktree as `docs/plans/m6-pr14-plotly-size.md`, so no
     second copy exists, then check that `git -C <main checkout> status --short` is empty;
   - commit, push, and open the draft PR.
2. `just python` in the worktree. Produce the before/after inputs and the 60 s and 300 s logs, and run the base
   side twice, before any product edit.
3. Implement D1, then D2, then T1 to T8, in coherent commits, pushing after each. Focused checks:
   - `-k plotly_size`, then `-k plot`;
   - `just ruff`, `.venv/bin/pyright`, `just docs-check` and `git diff --check`.

   Then the candidate side, and the size re-measurement. Commit
   `docs/reviews/evidence/<head>-before-after.log` with the sizes and the base per-trace listing.
4. **Jev item (Joe, 2026-10-04):**
   - delete PLAN.md's "(unscheduled) Evaluate Jev (TypeSafe AI) for workflow routing" entry, the whole three-line
     item (`PLAN.md:468-470` at `d851b8f`), and nothing else, in a commit of its own;
   - run `just docs-check` and `git diff --check`, with no new gate for it;
   - add no lessons or future-work entry;
   - the PR body names the change as Joe's decision. If the commit lands before the full gate, the gate covers
     it too.
5. **Load check (lessons 2026-10-04):**
   - `pgrep -af '^just (check|test|build)'` and `pgrep -af 'python[0-9.]* -m [p]ytest'` (plus `bin/[p]ytest`);
   - exclude the check's own shell and its ancestors, and this worktree;
   - `readlink /proc/<pid>/cwd` for each remaining hit, ignoring the other project's `just autopilot` dispatcher;
   - `ps -C clangd -o pid,pcpu,etime` must show clangd idle;
   - record the 1-minute load before and after.

   Then `rm -rf build && ./scripts/check-full.sh` in the background, with no edits during the run. It passes on
   exit 0 plus `check: OK`. Commit `docs/reviews/evidence/<head>-gate-summary.log`.
6. `/review-cycle` (Codex, fresh context, `--base origin/main`), with at most two correction rounds. The #94
   gate-rerun rule applies from the start: after a passing gate, a Markdown- or evidence-only delta needs only
   `just docs-check` and `git diff --check <gated-head>..HEAD`, named in the PR body. Anything else makes the gate
   stale.
7. `/human-review`: the PR body has D1 to D5, both size tables, the before/after with each side's printed paths
   and the control digest, the `-k` lists, and the Jev deletion.
8. `/wrap` before merge. STATE.md's next step becomes PR 10.

## Decisions

Joe, 2026-10-04, all as recommended, with the six amendments in the status line:
- **D1:** A, always on.
- **D2:** C, opt-in with `--html-columns N`, and the title suffix as proposed.
- **D3:** none of B, R, F, escape rewriting or a partial bundle in this PR.
- **D4:** T1 to T8, as amended.
- **D5:** Tier 2.
