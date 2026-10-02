# M6 PR 5: plots (`astra.plot`, both renderers, packages, `astra plot`)

Spec: [M6 plan](m6-analysis-tooling.md), approved by Joe 2026-09-29: the PR 5 row of "PR decomposition", "Plot
packages" under "Contract and failure behavior", R1 to R4 and R3b, Q2 (both libraries, pinned at current release),
Q3, Q5, Q20, "matplotlib determinism" under "Validation and risks", "Obligations carried to PR plans" (PR 5: C-11,
C-12, C-15's display-unit dispatch, per PR #78's notes and PR 4's plan D6); architecture §12's `astra.plot` and
`astra.cli` bullets.
Risk: **Tier 2** (Python feature plus two dev dependencies Joe already decided). Base: `origin/main` `468c3d7`.
Worktree/branch: `worktree-m6-pr5-plots` (this session's). Touches no `core/`, `plugins/`, `icd/`,
`tests/golden/**`, tolerance file or ADR. Binding ADRs: 0002 (SI in logs; display conversion through
`core/util/units`), 0010 (`t = frame * dt`, read from `astra.log`), 0011 (`{v}` over every vehicle), 0025/0027
(logs read through `astra.log.load`, strict).
Status: **approved by Joe, 2026-10-01, in plan mode, with his revisions 1 to 4 folded in below**: an existing
output file exits 65 (D5); a YAML syntax error is a `PlotPackageError` naming file, line and column; R4 asserts one
`libastra_core` mapping; the escape hatch also takes a `.py` file beside the package (D2), and the template ships a
file-based function package that R4 renders from the copy.

## Context

M6's first user-facing reader: plot any run log with a named, reusable package, as deterministic files (the
report in PR 8 embeds the SVG) or as one offline interactive page. PR 4 bound the unit conversions this needs; PR 3
gave the CLI it plugs into. This PR also closes the PR 5 parts of C-11 (resource-loading checks), C-12
(cross-process determinism) and C-15 (display-unit dispatch).

## Repository findings (read at `468c3d7`)

- `astra.log.load(path)` returns `RunLog`: `rates: dict[int, DataFrame]` (index `frame`, first column `t`, then
  signals), `units: dict[str, str]`, `path`. Strict by default (`python/astra/log.py:138`).
- Logged units (registry): derived `rad`, `rad/s`, `m/s`, `m`, `m/s^2`, `1`; state `""` (p_n, q_nb), `m/s`,
  `rad/s`; wrench `N`, `N.m`; controls `rad`, `1`; health `1`; wind `m/s`, `m`. Vehicles are named
  `"v" + index` (`simulation.cpp:582`); environment plugins log unprefixed (`wind.*`, `mass.mass`).
- `_core` binds `feet_to_meters`, `meters_to_feet`, `knots_to_meters_per_second`, `meters_per_second_to_knots`,
  `degrees_to_radians`, `radians_to_degrees`, float and 1-D float64 array, bitwise equal to C++
  (`python/bindings/_core.cpp:322`). Each from-SI function is `x / k` with `k` = 0.3048, 1852.0/3600.0, pi/180.
- `_runkit.closed_map`, `text`, `number` give closed-map errors as `"<path>: <where>: ..."`, with the exponent hint
  (`python/astra/_runkit.py:87-129`). `astra.cli` has `_Parser` (usage error exits 64), `main`/`_say`/`_write`.
- Editable install in redirect mode: `astra.__file__` is the source tree, so `Path(__file__).parent / "plots"`
  finds framework packages.
- PyPI today: matplotlib **3.11.2**, plotly **7.1.0**; neither in the venv. py.typed for each is checked after
  install (lesson 2026-09-21), not assumed.
- `_template` and `notional_transport` plugins differ only in namespace; the template config is zeroed and
  refused at init. `new-aircraft.sh` copies `_template/.` whole and rewrites only `configs/*.yaml` and C++
  namespaces, so `plots/*.yaml` travels unchanged if it names no path.
- The plugin loader compares `built_with` = compiler, standard, build type, sanitizers, static-plugins
  (`core/CMakeLists.txt:17`). The venv's `_core` is Release/clang-18/no sanitizers/shared, so plugins built by the
  `release` preset load into it; `debug` ones would be refused.
- Sibling test modules import each other (`import autopilot_design`), so R4 reuses `test_new_aircraft_build`'s
  `_copy_sources`/`_env`/`_snapshot`.

## Package format (YAML)

```yaml
title: Flight basics                 # optional string
figures:                             # non-empty list; or `function:` instead (exactly one of the two)
  - name: air_data                   # required, [a-z0-9_]+, unique in the package; the file name
    title: Air data                  # optional
    panels:                          # non-empty
      - title: Altitude              # optional
        ylabel: altitude             # optional; the axis shows "<ylabel> [<unit>]"
        series:                      # non-empty
          - signal: v{v}.derived.altitude
            unit: ft                 # optional display unit; default = the log's unit, data unchanged
            label: altitude          # optional legend text; default = the expanded signal
```
- Every map is closed (unknown key is an error); duplicate keys are refused by a `SafeLoader` subclass
  (PyYAML otherwise keeps the last silently). Strings must be YAML strings: a boolean (`on/off/yes/no`, lesson
  2026-09-30) or number where a string is required is an error with a "quote it" hint. The format has no numeric
  keys, so the exponent-sign lesson (2026-09-28) cannot bite; `_runkit.number` is reused if one is ever added.
- `{v}` is replaced by each vehicle index found in the log (`v<index>.` prefixes, ascending numerically), so
  `v{v}.derived.alpha` gives one series per vehicle, labeled `v<i> <label>`. Any other `{...}` is an error.
- A panel's series must share one display unit (so the axis label is well defined).
- A YAML syntax error (PyYAML's bare `ParserError`/`ScannerError`, e.g. `label: {v} altitude`, a flow map where a
  string was meant) is a `PlotPackageError` naming the file, line and column (from the error's `problem_mark`).
- Escape hatch: `function: {module: <dotted importable module>, name: <callable>}` or `function: {file:
  <name>.py, name: <callable>}`, exactly one of `module`/`file`. `file` is a bare file name in the package's own
  directory (no `/`, no `..`), loaded by path under a private module name (`astra.plot._user.<hash of path>`),
  so an aircraft's own Python plots travel with a `just new-aircraft` copy (D2). It takes a `RunLog` and
  returns `list[astra.plot.Figure]` (dataclasses `Figure(name, title, panels)`, `Panel(title, ylabel, unit,
  series, xlabel="t", xunit="s")`, `Series(label, x, y)`), validated by the same checker as YAML-built figures.
- Package name = file stem, `[A-Za-z0-9_][A-Za-z0-9_-]*`.

**Display-unit dispatch (C-15).** One table, keyed by (log unit, display unit), each entry a late-bound call of a
bound `_core` function: `m`→`ft` `meters_to_feet`; `m/s`→`kt` `meters_per_second_to_knots`; `m/s`→`ft/s`
`meters_to_feet`; `rad`→`deg` and `rad/s`→`deg/s` `radians_to_degrees`. Display unit equal to the log unit:
identity, no call. Anything else, including any conversion from `""`, `1`, `N`, `N.m`, is an error naming the
file, key, log unit, display unit and the units defined for that log unit. No Python-side multiply exists.

**Errors.** `PlotPackageError("<package file>: <key>: <problem>")`, key like `figures[0].panels[1].series[2].unit`
or `function.module`. Order: `load_package` (schema, no log) → `resolve` (signals, vehicles, conversions, or the
function's call and checked result) → compute every output path and refuse any that exists → create the out dir
and write. So every package error is raised before any file or directory is created. If a write fails midway,
the call deletes only files it created (ADR-0027's who-deletes-what spirit) and re-raises.

## Interface (`python/astra/plot.py`)

- `load_package(path) -> Package`; `framework_package(name) -> Path` (`python/astra/plots/<name>.yaml`).
- `resolve(package, run_log) -> list[Figure]`.
- `draw_matplotlib(figure, package_name) -> matplotlib.figure.Figure` and `draw_plotly(figure) -> go.Figure`
  (public so tests read the drawn data).
- `render(package, run_log, out_dir, *, fmt="svg"|"png", html=False) -> list[Path]`.
- **matplotlib renderer:** `matplotlib.figure.Figure` directly, never `pyplot` (headless, no backend state). Per
  figure inside `rc_context()`: `rcdefaults()` (ignores the user's matplotlibrc), then the pinned style
  (`font.family: DejaVu Sans`, the font matplotlib bundles; `svg.fonttype: path`; `svg.hashsalt:
  astra/<package>/<figure>` so hashed ids are fixed and differ across figures; fixed figsize, dpi 100,
  constrained layout). `savefig(..., metadata={"Date": None})`. Files `<package>-<figure>.svg|png`.
- **plotly renderer:** one `make_subplots` figure per package figure (shared x), `go.Scatter`; one HTML
  `<package>.html`: our own skeleton, plotly.js inline once (first `to_html(include_plotlyjs=True,
  full_html=False)`, later ones `False`), fixed `div_id`s, config `displaylogo: False`, no MathJax.
- Lazy imports: matplotlib and plotly load only inside the renderers.

**`astra plot LOG PACKAGE [--out DIR] [--format svg|png] [--html]`** in `astra.cli`. PACKAGE is a path if it
contains `/` or ends in `.yaml`/`.yml`, else a framework package name. `--out` defaults to `.`. Stdout: one written
path per line. Codes: 64 usage (argparse via `_Parser`); 65 package refused (`PlotPackageError`) or log refused
(`LogFormatError`, `IncompleteLogError`, including a missing file), or an output file already exists (as `astra
run` exits 65 for an existing `log.path`); 74 a write that fails; 70 internal error; 0 written.

## Packages shipped

- Framework (`python/astra/plots/`): `flight` (altitude ft, TAS/CAS kt, Mach, vertical speed ft/s, alpha/beta
  deg), `attitude` (phi/theta/psi deg, p/q/r deg/s), `health` (the five `health.*` signals).
- Template starter set (`aircraft/_template/plots/`): `controls` (`v{v}.controls.*`, deg and throttle), `wrench`
  (`v{v}.wrench_total.*`), and `ground_track` (`function: {file: ground_track.py, name: figures}`, north vs east
  displacement integrated from `v<i>.derived.v_n_0/1`, one figure, x labeled `east [m]`), the starting point for
  an aircraft's own Python plot. Implementation note: the displacement integrates the derived NED velocity rather
  than reading `p_n`, which is latitude and longitude on a round Earth.
  Header comments say how to add one. No path inside, so the copy needs no rewrite.
- `aircraft/notional_transport/plots/cruise.yaml`: altitude, TAS, alpha, theta, q, elevator, throttle, wind NED.

## Tests (`.venv/bin/pytest tests/python -k plot`)

`tests/python/test_plot.py` (unit, R1 to R3b, CLI) and `tests/python/test_plot_new_aircraft.py` (R4).
Comparison is bitwise (`dtype == float64`, then `.view(np.uint64)` equal). Expected values come from the log's
column and an independent expression of each documented definition (`x / 0.3048`, `x / (1852.0 / 3600.0)`,
`x / (math.pi / 180.0)`), one IEEE division each, so bitwise equal to the C++ function.

- **R1:** one log from `astra.scenario.run` of `tests/scenarios/elevator_step.yaml` into `tmp_path` (module-scoped
  fixture). Every shipped package (3 framework, 3 template, notional transport) renders against it: every figure
  file is written (exact name set); each `Line2D`'s x equals `t` and y equals the expected array; series order is
  package order then vehicle order (legend labels); every y-axis label ends in `[<display unit>]`.
- **R2 (each raises `PlotPackageError` whose message starts `<package path>: <key>:`, and `out_dir` does not
  exist afterwards):** unknown key at each level; unknown signal; undefined conversion (`v0.derived.mach` `unit:
  deg`); empty `figures`, `panels`, `series`; `{v}` series against a synthetic log whose vehicle 1 lacks the
  signal, and against a log with no vehicle. Also: a YAML syntax error (`label: {v} altitude`) naming file, line
  and column; duplicate key, `label: no` (boolean hint), mixed units in a panel, duplicate/invalid figure name,
  `{x}`, both or neither of `figures`/`function`, both or neither of `module`/`file`, escape-hatch failures
  (module, missing file, a `file` with `/` or `..`, name, return type, x/y length mismatch, non-float64), and an
  existing output file left untouched with nothing else written. A file-based function renders. Synthetic logs are `RunLog` instances built in the test (multi-vehicle is not producible
  by the sim: ADR-0011's list of one), also used for `{v}` ordering (vehicles 0, 2, 10).
- **C-15 dispatch:** for each table row, all six `_core` functions are monkeypatched to distinct sentinels; the
  series carries that row's sentinel only. Identity rows call none. Known values: `0.3048 m` → `1.0 ft`,
  `1852/3600 m/s` → `1.0 kt`, plus the independent-expression equality above. Each undefined pair is an error.
- **R3 (C-12):** two subprocesses of the installed `astra plot` script (also the CLI's minimal case) render the
  same log and package (`flight`, plus the notional package) into two directories. Process 1:
  `PYTHONHASHSEED=1`, a fresh empty test-owned `MPLCONFIGDIR` (cold font cache). Process 2: `PYTHONHASHSEED=2`,
  the same dir (warm cache) now holding a `matplotlibrc` that changes font family, size and line width, and
  `SOURCE_DATE_EPOCH` set. Same file set, byte-identical SVGs, no `<dc:date>`, glyph ids `DejaVuSans-*`. Hashed
  ids (`p<hex>`) are disjoint across a package's figures. Not claimed here: matplotlib's counter ids
  (`figure_1`, `axes_1`) and glyph ids repeat across files, so a report inlining several SVGs must namespace
  them: carried to PR 8 as C-12's report part.
- **R3b (C-11):** `draw_plotly` traces' x and y equal the expected arrays through `fig.to_dict()`. The HTML holds
  plotly.js inline, and a structural check (`html.parser`, a test helper `tests/python/resource_refs.py` that PR 8
  can reuse) finds no `src`, `href`, `xlink:href`, `srcset`, `poster`, `data`, `action`, `formaction` or
  `background` attribute except `data:` or `#...`; no `<link>`, `<base>`, `<iframe>`, `<embed>`, `<object>`, meta
  refresh, or `<script src>`; no CSS `url(...)` (except `data:`/`#`) or `@import` in `<style>` or `style=`.
  `xmlns` URIs are not loads. The same check runs on every SVG. The helper is proven to bite: each forbidden
  construct in a small fixture is caught. Limit stated in the PR: runtime loads by inline JS are not statically
  checkable; the config avoids plotly's known ones (logo link, MathJax, geo/map traces).
- **PNG:** `--format png` writes PNG-signature files with the same names (not byte-stable, per the plan).
- **CLI:** minimal case (R3's subprocess); bad argument (`--format pdf`, missing PACKAGE) exits 64; refused
  package 65 and no file; missing log 65; existing output 65 with the file untouched; a failing write (an
  unwritable out dir, as a non-root user) 74.
- **R4:** throwaway copy as in `test_new_aircraft_build.py` (`GIT_*` stripped, `GIT_CEILING_DIRECTORIES`);
  `just new-aircraft gliderx`; the test fills the copy's `configs/cruise.yaml` by line substitution of each `key:
  0.0` with the notional transport's values (count of each replacement asserted; flat Earth, `p_n: [0.0, 0.0,
  -10000.0]`, notional trim state and controls), editing nothing else; `cmake --preset release -B <tmp build>`
  and build only the six plugin targets the config names (three copy plugins, earth/flat, gravity/constant,
  atmosphere/isa1976). It runs the config for its 1000 frames with a log block appended to the text (parsed by
  the core, never PyYAML), closes the log, asserts no halt, and asserts exactly one `libastra_core` file is mapped
  in the test process (`/proc/self/maps`) after the copy's plugins load. It then renders each of the copy's
  `plots/*.yaml`, the file-based `ground_track` included, and checks every YAML series as in R1 and the ground
  track against a recomputation from the log's `v_n` columns. The copy's `plots/` equals the template's byte for byte, and the snapshot shows
  only `aircraft/gliderx/configs/cruise.yaml` changed after the copy. Cost measured in the PR (Release build of
  the core plus six plugins; `test_new_aircraft_build` already does a debug build of the tree).

## Files

- `python/astra/plot.py`, `python/astra/plots/{flight,attitude,health}.yaml` (new); `python/astra/cli.py`
  (`plot` subcommand and docstring).
- `aircraft/_template/plots/{controls,wrench,ground_track}.yaml` and `ground_track.py`, `aircraft/notional_transport/plots/cruise.yaml` (new).
- `pyproject.toml`: `matplotlib==3.11.2`, `plotly==7.1.0` in the dev group with the pin-date comment.
- `tests/python/test_plot.py`, `tests/python/test_plot_new_aircraft.py`, `tests/python/resource_refs.py` (new).
- `docs/architecture.md` (Joe's): §12 `astra.plot` and `astra.cli` markers to built, with the `{v}`, file-name and
  CLI details; §12 status line; §1's visualization sentence (plot packages built, PR 6 and PR 8 planned); §15 row
  65 "a config or plugin error, including a `transport` block ...".
- `README.md`: the CLI bullet and a short "Plot a log" how-to. `PLAN.md`: the PR 5 line (no merge facts).
- `docs/plans/m6-pr5-plots.md` (this plan); review record and gate evidence.

## Decisions for Joe (approving the plan approves these)

- D1: `{v}` is the integer index (`v{v}.derived.alpha`); vehicles are discovered from `v<index>.` prefixes.
- D2 (revised by Joe): x is always `t` in YAML; any other x (a ground track) uses the escape hatch, which takes a
  dotted importable module or a bare `.py` file beside the package; the template ships one file-based package.
- D3: display-unit table as above (five conversions plus identity); no ft/min (it would need a new
  `core/util/units` function, Tier 3).
- D4: a panel's series share one display unit; duplicate YAML keys are refused.
- D5: file names `<package>-<figure>.svg|png` and `<package>.html`; an existing output is refused, never
  overwritten; `astra plot` codes 64/65/74/70/0 as above (revised by Joe: an existing output is 65, as for
  `astra run`'s `log.path`; 74 only for a failing write).
- D6: matplotlib and plotly's transitive dependencies stay unpinned, as for pandas and h5py today.
- D7: C-12's id uniqueness across a report (counter and glyph ids) is PR 8's; PR 5 fixes hashed ids per figure.

## Verification and process

1. Commit this plan (renamed from its plan-mode draft); push; open the draft PR at that commit.
2. `just venv` and `just python` in the worktree (no `.venv` here yet); confirm py.typed for matplotlib and plotly;
   handle any untyped surface with `cast` and named `pyright: ignore[...]` only, never a strictness exemption.
3. Build in coherent commits (pins; `astra.plot` and tests; packages; CLI; R4; docs), pushing after each.
4. `.venv/bin/pytest tests/python -k plot` (R1 to R4, R3b); `.venv/bin/pyright`; `.venv/bin/ruff check .` and
   `ruff format --check .`; `python3 scripts/docs_check.py`; `git diff --check`.
5. `rm -rf build && ./scripts/check-full.sh` in the background (Bash `run_in_background` only), bounded foreground
   waits on that pid, then its exit status and `check: OK`; commit the trimmed
   `docs/reviews/evidence/<head>-gate-summary.log`, stating the fresh configure in its header.
6. `/review-cycle`: Codex review of the committed branch (`--base origin/main`) on the final diff; at most two
   correction rounds; review record in `docs/reviews/m6-pr5-plots-review.md`.
7. `/human-review` for the PR body per `docs/templates/pr-description.md`, including Docs impact; "Needs Joe's
   decision" lists D1 to D7 and each architecture.md edit (§1, §12 two markers and status line, §15 row 65).
8. `/wrap` before merge, its docs step in branch scope. Guard refusals: rerun split or use Write/Edit, listed in
   the PR body, never asked.
