# M6 PR 3: the `astra` CLI (`astra run`, `astra trim`)

Spec: [M6 plan](m6-analysis-tooling.md): the CLI contract and exit-code table, Q12, Q14, R14, R15 (the
trim part), the PR 3 row, and C-14 under "Obligations carried to PR plans". Architecture §12 (`astra.cli`), §15
(post-run summary, exit codes, whose row 3 reads "written, closed or published"), §18 step 4. ADR-0027 items 1, 3, 5
and 8.
Risk: Tier 2 (new Python module plus a console script; no `core/`, no binding, no ADR, no golden).
Base: `origin/main` `da52239` (#80). Branch `worktree-m6-pr3-cli` (new worktree). Does not touch `docs/decisions/`,
`tests/golden/**`, tolerance files, `core/` or `python/bindings/`.
Status: **approved by Joe, 2026-09-30, in plan mode, with his changes J1 to J6 folded in below** (J1 the fate
line, J2 D8, J3 the summary written whole, J4 the fragment equality evidence, J5 D3 and D4 in PLAN.md's open item,
J6 T15 after `gc.collect()`).

## Context

M6 needs a front door: `astra run <config>` flies a config open loop with defined exit codes, and `astra trim`
gives the cruise condition (§18 step 4). The table's precedence and the log's fate on every failure path are the
risky part. This PR adds `astra.cli` with those two subcommands. The other subcommands come with their own PRs.

## Repository findings (read at `da52239`)

- There is no `[project.scripts]` in `pyproject.toml`. Runtime dependencies are `numpy` only. The tools already
  import `scipy`, `yaml`, `h5py` and `pandas` from the dev group, because the install is editable-only (ADR-0009).
  The CLI follows that and imports heavy modules lazily. **No new dependency.**
- Bindings (`python/bindings/_core.cpp`):
  - `Simulation.from_config` raises `ConfigError` or a `PluginError`.
  - `run()` is noexcept and returns a `HaltReason` (`check`, `source`, `frame`, `vehicle_index`).
  - `close_log()` raises `LogError`. `discard_log()` returns a `LogDiscardResult`.
  - `clock.dt`/`frame`, `frames`, `has_log` and `registry.signals` are bound.
  - The core's fault hooks are not bound (PR 5 D2).
- `RunLog::close` (`run_log.cpp:529`): after an earlier drain failure (`failed()`), it throws the recorded
  `LogError` and writes nothing. **So under `run()`, a LOG halt always comes with a `close_log()` that raises.**
- A failed provenance write at construction is a `ConfigError` naming `log.path`, and the core deletes the
  `.partial` it created (`run_log.cpp:208`). The same goes for a missing or unwritable directory, an existing
  `log.path`, and a stale `.partial`.
- Health counters (`health_monitor.cpp:95`): `health.nan`, `quaternion`, `domain` and `input` count every
  detection whose action is not `off`. `health.max_q_drift` is a maximum and is nonzero in almost every run.
  `astra.scenario` already pins `WARN_COUNTERS` and `HEALTH_SIGNALS` (`scenario.py:115`). With no halt, a nonzero
  counter can only come from a `warn` action, because a `halt` action would have halted the run.
- Fixtures: `tests/configs/health_nan_wrench.yaml` halts NON_FINITE_WRENCH at frame 500.
  `tests/python/scenario_fixtures/nan_wrench_warn_config.yaml` runs to the end with `health.nan` = 4500. Nothing
  yet produces a halt that follows a warning. `tests/configs/no_alloc_transport.yaml` has a `transport` block.
- `astra.trim.trim_straight_and_level(config, tas, alt)` refuses `earth/flat` (ValueError), keeps a `transport`
  block (a build would start a handshake), and raises RuntimeError when it does not converge.
  `_runkit.TRIM_RESIDUAL_BOUND` = 1e-6 (D8).
- Config text: `Config.from_file` is yaml-cpp. The tools' `yaml.safe_load` → `Config.from_dict` route reads
  YAML 1.1, where `off` becomes `False`. That would turn `sim.health.nan: off` into `false`, which the core then
  refuses. The CLI therefore never converts a config through a dict (see `--log` below).
- §18 step 4 is not true for a template copy today: the template uses `earth/flat`, which `astra.trim` refuses.
  That is pre-existing and not fixed here; it is reported to Joe as a follow-up.

## Interface

`astra run <config> [--strict] [--log PATH]` and `astra trim <config> [--airspeed V] [--altitude H]` (SI: m/s, m).
`astra --help`, `astra run --help` and `astra trim --help` print usage to stdout and exit 0. The console script is
`astra = "astra.cli:console"`, and `python -m astra.cli` is equivalent. `main(argv) -> int` is the pure,
in-process entry point the tests call. `console()` calls it and `sys.exit`s, and first restores the default
dispositions of SIGINT and SIGPIPE (D5).

### `astra run`: order of work, and which code each step can raise

1. **Parse arguments.** Any argparse error exits 64 with usage on stderr (the `error()` override; argparse's own
   code is 2). Nothing is read and nothing is created.
2. **Load the config.** `Config.from_file(config)`. A missing file or malformed YAML is a `ConfigError` → 65.
3. **Check `--log` against a `log` block.** `--log` given and the config has a `log` block → 64. Stderr names
   both paths (D2). Nothing is created.
4. **Refuse `transport`.** `config.has("transport")` → 65: "astra run: config error: <config>: transport: a
   config with a transport block is refused until M10 (closed-loop runs)". This runs before `from_config`, so no
   `.partial` is created and no handshake starts.
5. **Add the log block for `--log`.** The file's own text plus an appended `log:\n  path: <json string>` goes
   through `Config.from_string(text, origin=<config>)`, and the result must read back `log.path == PATH`. If it
   does not (a flow-style top level, a second document, or a path YAML cannot quote), it exits 64 and names the
   reason. The config text itself is never rewritten.
6. **Build.** `Simulation.from_config(config, default_plugin_dir())`:
   - `ConfigError` or `PluginError` → 65. That includes an existing `log.path` or stale `.partial`, both
     untouched, and a log that fails at construction (for example its provenance write), which the core deletes.
   - `LogError` → 74.
7. **Run and close.** `halt = _drive(sim)` (a module function that returns `sim.run()`, the tests' seam), then
   `sim.close_log()`. A `LogError` from `close_log()` → 74, and the file is kept (next section). Wall time is
   measured with `time.perf_counter` around these two calls only.
8. **Print** the summary to stdout (next section), in two steps (J3):
   - **Build the whole text first.** With a published log, the min/max come from `astra.log.load(log.path)`,
     read strictly while `sim` is alive. If that load of the just-published log raises, the code is **70**
     (a defect, D3), and nothing is printed to stdout.
   - **Then write and flush once.** One `sys.stdout.write(text)` and one `sys.stdout.flush()`, inside a guarded
     block in `main()`. Nothing reaches stdout before the text is complete. If the write or flush raises an
     `OSError` (ENOSPC, `/dev/full`), `main()` itself exits 74 (D4), not the interpreter's flush at exit. After
     such a failure, `console()` points fd 1 at `/dev/null` before `sys.exit`, so that shutdown's second flush
     cannot print "Exception ignored" or change the code. A kernel can still accept part of a large write before
     failing: "all or none" means the text is never printed piecemeal, not that the device took nothing.
   The log's fate is settled before this step.
9. **Pick the exit code, first matching row:**
   - 74 if step 6 or 7 raised `LogError`, or step 8 failed;
   - 1 if `halt`;
   - 2 if `--strict` and any `WARN_COUNTERS` entry is nonzero;
   - 0 otherwise.

   Any exception not classified above → 70 `EX_SOFTWARE`, with the traceback on stderr (D3). It is never 1,
   which Python's default would give and which reads as a halt.

**74 against 1 (the LOG row).** Under `run()`, a LOG halt comes only from a failed drain write. Ring overflow
cannot happen there, because `run()` drains at the high-water mark. `RunLog::close` then rethrows that failure, so
the CLI exits **74**: row 3 precedes row 1, and "the log could not be written" is true. For `astra run`, row 1's
"LOG included" is unreachable. The code still follows the table literally: a LOG halt whose close somehow
succeeded would exit 1. Test T9 shows it: `halt: LOG` on stdout, exit 74.

**65 against 74 (D8).** An I/O failure at construction (a full disk during the provenance write) matches both
row 2 and row 3. The CLI exits **65**: row 2 comes first, and the core raises it as a `ConfigError` naming
`log.path`. The argument, from ADR-0027 item 1:
- the record itself places every construction failure, a missing or unwritable directory included, in
  `ConfigError`;
- item 8's init-time delete means the run never started and left no file, so there is no run log that "could
  not be written";
- stderr carries the core's cause (`log.path: cannot write the log's provenance: ...`), so a user with a full
  disk is told so, under the "config error" prefix.

74 is reserved for `LogError`s during the run and at close and publication. Test T13 shows it. This is D8 under
Decisions.

### Streams

- **stdout** holds only the summary, printed once the run has executed (step 7 was reached). Nothing is printed
  there for 64, for 65, or for a 74 raised at construction.
- **stderr** holds exactly one reason block for every nonzero exit, each starting `astra run: `:
  - usage plus the error (64);
  - `config error:` / `plugin error:` plus the core's message (65);
  - `log error:` plus the core's `LogError` text, then the fate line below (74);
  - `halted: CHECK (source S, frame F, vehicle V)` (1);
  - `--strict: warn counters are nonzero: health.nan = 4500.0` (2);
  - the traceback (70).

  Exit 0 writes nothing to stderr.
- **Summary format**, one `key: value` per line, in this order:
  - `config: <path>`;
  - `frames: <run> of <requested>`;
  - `halt: none`, or `halt: CHECK source=S frame=F vehicle=V`;
  - each `health.*` signal, sorted, as `repr`;
  - `log: <abs path>`, `log: none`, or `log: not published (see stderr)`;
  - with a published log only: `signals:`, then `  <name> <min> <max> <unit>` for every logged signal (`repr`,
    exact);
  - `wall time: <s, 3 decimals> s`;
  - `real-time factor: <frames×dt / wall, 1 decimal>`.

  The format is documented in the module docstring. Wall time is printed, never stored (ADR-0025).

### What `astra run` leaves on disk (ADR-0027), and the discard decision (D1)

| Outcome | Exit | On disk |
|---|---|---|
| Usage error; `--log` against a `log` block | 64 | nothing created |
| Parse or `transport` refusal | 65 | nothing created |
| Existing `log.path` or stale `.partial` | 65 | both untouched (the core refuses before creating) |
| Failure at construction after the create | 65 | neither name (the core deletes its own `.partial`, item 8) |
| Clean run, `--strict` warnings, or a non-LOG halt | 0 / 2 / 1 | `log.path` published, no `.partial` |
| LOG halt (a drain write failed) or a write failure at close | 74 | `.partial` kept, incomplete (its certified rows read with `allow_incomplete=True`) |
| Publication refused: EEXIST or unsupported rename | 74 | `.partial` kept with `complete = 1`, unpublished; the foreign file untouched |
| Publication refused: identity check failed | 74 | nothing touched: the name holds a foreign file, and the run's own file is wherever it was moved |
| Internal error before or after `close_log()` | 70 | the destructor's `.partial` (`finalized_by = destructor`), or `log.path` if already published |
| Ctrl-C or kill | signal | ADR-0027 item 5's table (a `.partial` before the publication point) |

**D1 (recommended): `astra run` never deletes a log file. It keeps the `.partial` for the user and names it on
stderr.** The fate lines (J1) must be true in every 74 case, including the identity case, where
`<log.path>.partial` holds a foreign file:
- **Always:** `astra run: the log was not published to '<log.path>'. astra run deleted nothing (ADR-0027 item
  8): every file is as close_log() left it, and the error above says what it kept or touched. A later run with
  this path is refused while '<log.path>.partial' exists.`
- **Added only for a publication refusal that is not the identity case,** that is EEXIST, an unsupported rename,
  or any other errno after the marker: `'<log.path>.partial' holds this run's complete log (complete = 1). Read it
  with astra.log.load(path, allow_incomplete=True), or publish it by hand with mv -n once '<log.path>' is
  clear.`

  The case is identified from the core's message, `run log '<path>': the publication failed: <reason>`: the
  added lines are printed only when the failed step is "the publication" and the reason is not "the .partial is
  no longer this run's file". Every other case prints only the first line. If the core's wording changes, the
  hint disappears (it fails safe), and T5 and T8 pin both texts.

The argument against item 8:
- Item 8 names its tools (`montecarlo`, `scenario` and `replay`, through `_runkit`). Their obligation to "leave no
  `.partial` of its own" fits runs that can be recomputed from a seed or a source log. `astra run` is the user's
  own run, and the file is its only record.
- Keeping deletes nothing, so it cannot break any of item 8's prohibitions: none by name, none foreign, none after
  a mismatch.
- Item 3 expects the user to clear a collision by hand (`mv -n`), which only works if the `.partial` still exists.
- Item 1 makes a leftover `.partial` the user's. The next run refuses it (65) and never touches it, so keeping it
  cannot silently lose or overwrite anything.
- Discarding would delete a complete, correct log over a name collision or an unsupported filesystem. After a
  failed write, it would delete the certified rows that show where the disk filled.

Alternative: discard through `discard_log()` on every 74, as `_runkit` does. The cost is losing a finished run
over a naming conflict.

### `astra trim` (R15, trim part)

- **Setpoint.** `--airspeed` and `--altitude` (finite, airspeed > 0, else 64) default to the config's own
  initial state: |v_b| and geodetic p_n[2], read from a still-air `Simulation`. That makes §18's
  `astra trim cruise.yaml` literal (D6).
- **Refusals.** A `transport` block → 65 before any build. `earth/flat` (astra.trim's ValueError) → 65, naming
  `physical.earth`. A default airspeed of zero → 65 ("pass --airspeed or set initial.state.v_b").
  `ConfigError`/`PluginError` → 65.
- **Not trimmed.** Non-convergence, or a residual above `_runkit.TRIM_RESIDUAL_BOUND` → 1, with the reason and
  max|residual| on stderr and nothing on stdout.
- **Output.** stdout is YAML written by `yaml.safe_dump`, whose floats are `repr`, the shortest text that reads
  back exactly (PyYAML writes `1.0e-13` so YAML 1.1 still reads a float). It holds:
  - `true_airspeed` and `altitude` (the setpoint used);
  - `elevator`, `throttle` and `alpha`;
  - `residual` (6);
  - `initial.state` (`p_n`, `v_b`, `euler_nb`, `omega_b`) and `vehicle.initial_controls.controls` (`elevator`,
    `throttle`), the fragment `_runkit.apply_trim` writes. It is factored into one `_runkit` helper that both
    use, in the same key order, so scenario and montecarlo configs are unchanged.
- **Fragment evidence (J4).** Scenario and montecarlo tests compare run against run, so they would not see a
  change in the fragment. A scratch script imports `_runkit` as it was at `da52239` (from `git show`) beside the
  new one. For `aircraft/notional_transport/configs/cruise.yaml` and every trimmed fixture (each scenario and
  Monte Carlo fixture with a `trim` block), it applies the old `apply_trim` and the new one to the same config
  and asserts the same dict with the same key order, compared through `json.dumps` without sorting. The command,
  the fixture list and the result go in the PR body.

## Files

- `python/astra/cli.py` (new): parser, `main`, `console`, `_drive`, `_run`, `_trim`, and a docstring holding the
  contract, the exit codes, the streams and the file fate.
- `python/astra/_runkit.py`: `WARN_COUNTERS` and `HEALTH_SIGNALS` move here (`scenario` re-exports them, as it
  does `TRIM_OWNED`), plus the trim-fragment helper that `apply_trim` uses.
- `python/astra/scenario.py`: re-exports only.
- `pyproject.toml`: `[project.scripts] astra = "astra.cli:console"`.
- `tests/python/test_cli.py` (new), and `tests/python/cli_fixtures/halt_after_warn.yaml` (new):
  - round Earth, `domain: warn`, starting 1e-5 rad below the latitude limit, northbound at 100 m/s;
  - plus `nan_wrench` at `nan_frame: 1500`, so `health.domain` counts before a NON_FINITE_WRENCH halt;
  - built from `simulation_halt_test.cpp`'s polar fixture. The first build step verifies it; if it misbehaves,
    the fallback is a flat-Earth fixture that leaves the atmosphere's range under `domain: warn`.
- `PLAN.md`:
  - a PR 3 line under M6;
  - the "(M6) A CLI, `astra run` with `--strict`" open item removed, because this PR delivers it;
  - the (M6) planned-markers item extended to name §15, whose table still reads "(M6 PR 3, *planned*; no `astra`
    command exists yet)", and to carry D3 (70, internal error) and D4 (74 for a stdout failure), so §15's table
    takes them up when the markers are cleared (J5).
- `docs/plans/m6-pr3-cli.md` (this plan) and `docs/reviews/evidence/<head>-gate-summary.log`.

## Tests (`tests/python/test_cli.py`; R14, R15 trim part, C-14)

Subprocess tests run the installed console script (`<venv>/bin/astra`), which proves it exists. In-process tests
call `main(argv)` with `capsys` and monkeypatch `astra.cli._drive`. Every test asserts the exit code, what is on
stdout and stderr (including stderr being empty on 0), and the files left in `tmp_path`.

| # | Row(s) | How it fires | Also asserts |
|---|---|---|---|
| T1 | 0 | `dummy_1000.yaml`, with and without `--log` | summary lines; frames 1000 of 1000; `log.path` published, loads strictly, no `.partial`; printed health and min/max equal the loaded log's exactly |
| T2 | 1 | `health_nan_wrench.yaml`, with `--log` | stdout halt: NON_FINITE_WRENCH, source tests/plugins/nan_wrench, frame 500; halt on stderr; log published |
| T3 | 1 over 2 | `halt_after_warn.yaml`, with and without `--strict` | 1 both times; `health.domain` > 0 printed |
| T4 | 2, 0 | `nan_wrench_warn_config.yaml`: `--strict` → 2 (stderr names health.nan = 4500.0); without → 0 | `health.max_q_drift` nonzero, which alone never gives 2 |
| T5 | 74 (publication) | in-process: `_drive` runs, then writes a file at `log.path` → EEXIST | foreign file byte-identical; `.partial` kept with `complete = 1` (`load(..., allow_incomplete=True)`); stderr names `log.path` and the `.partial`; never 0 |
| T6 | 74 over 1 | T5 with `health_nan_wrench.yaml` | a non-LOG halt with a refused publication → 74 |
| T7 | 74 over 2 | T5 with the warn config and `--strict` | 74 |
| T8 | 74 (identity) | in-process: `_drive` moves the `.partial` aside and puts a new file at its name | both files untouched, no `log.path`, 74 |
| T9 | 74 over 1, LOG | subprocess under `RLIMIT_FSIZE` (SIGXFSZ ignored) set just above the size the test measures right after construction; 5000 frames, so the first high-water drain's block write fails | stdout `halt: LOG`; exit 74; `.partial` kept; no `log.path` |
| T10 | 74 (close) | same limit, 1000 frames (no drain before close), so the tail write in `close_log()` fails | no halt; 74; `.partial` kept |
| T11 | 65 | `no_alloc_transport.yaml`, with and without `--log` | stderr names `transport`; no file created; stdout empty |
| T12 | 65 | missing file, malformed YAML, unknown key, unknown plugin, existing `log.path`, stale `.partial` | the last two files byte-identical |
| T13 | 65 over 74 | `RLIMIT_FSIZE` just below the measured construction size (the provenance flush fails) | 65 naming `log.path`; neither name left |
| T14 | 64 | no args; `run` with no config; unknown option; extra positional; `--log` with no value; unknown subcommand; `--log` against a config with a `log` block; a bad option plus a missing config (64 over 65) | nothing created; usage on stderr |
| T15 | 70 | in-process: `_drive` raises RuntimeError; and a strict `load` of the published log that raises (monkeypatched) | 70; traceback on stderr; stdout empty. After `main()` returns and `gc.collect()` (a traceback can keep the `Simulation` alive), the destructor's `.partial` (state `destructor`) in the first case and `log.path` in the second; nothing deleted |
| T16 | output | subprocess with stdout redirected to `/dev/full` → 74 | log already published |
| T17 | signals | SIGPIPE: stdout is a pipe whose read end is closed → killed by SIGPIPE, log published. SIGINT: sent once the `.partial` appears on a long run → killed by SIGINT, no `log.path` | `console()` restores the defaults |
| T18 | trim 0 | subprocess `astra trim notional_transport/configs/cruise.yaml --airspeed 230 --altitude 10000`, and with the defaults | every value, parsed by `yaml.safe_load` and by `_core.Config.from_string`, is bitwise equal to an in-process `trim_straight_and_level(...)`; the defaults equal the explicit run |
| T19 | trim 64/65/1 | a bad `--airspeed`; unknown option; `earth/flat` config; `transport` config; `xtol` so loose it cannot meet the bound (monkeypatched, in-process) → 1 | stdout empty on every nonzero exit |
| T20 | guard | `HEALTH_SIGNALS` equals the registry's `health.*` names | a new counter forces a deliberate update |

C-14 closure, against the verification note in PR #78:
- Precedence: T3, T6, T7, T9, T13, T14.
- Codes for runtime and output failures: D3 and D4, with T15 and T16.
- A publication failure tested: T5 and T8.
- A halt with warnings without `--strict`: T3.
- How 74 covers a failed publication: `close_log()` raises `LogError`.
- A close or publication failure can never exit 0: T5 to T10.

## Decisions for Joe

- **D1.** Keep the `.partial` on every 74 and never delete (argued above). Alternative: `discard_log()`.
- **D2.** `--log` with a config that already has a `log` block → 64, naming both paths. Alternative: `--log`
  replaces `log.path` and keeps `buffer_frames`. That needs a rewrite of the config, which the text-append route
  cannot do safely.
- **D3.** An internal error → 70 (`EX_SOFTWARE`). The table has no row for it, and "0 otherwise" would be wrong.
  It is documented in the CLI help and docstring only. §15 stays unedited unless you ask (PR 10 could add it).
- **D4.** A failure writing stdout → 74 (EX_IOERR). The log is settled before anything is printed.
- **D5.** `console()` restores SIGINT and SIGPIPE to their defaults. `sim.run()` holds the interpreter for the
  whole run, so Python's handler would fire only after the run ends, and then drop the unclosed run. With the
  default, Ctrl-C is a kill, whose outcome ADR-0027 item 5 already defines. `main()` changes no handlers, so
  in-process tests are unaffected.
- **D6.** `astra trim`'s setpoint defaults to the config's initial state. `--airspeed` and `--altitude` override
  it. Alternative: both required.
- **D7.** Remove the "(M6) A CLI" open item in this PR.
- **D8.** An I/O failure at construction exits 65, not 74. Both rows match; row 2 wins, argued from ADR-0027
  item 1 above. A user with a full disk sees "config error", followed by the core's cause. Alternative: exit 74
  when a construction `ConfigError` names `log.path`. That means classifying by message text, and it would split
  one exception type across two codes.
- Follow-ups reported, not fixed:
  - §18 step 4 on a template copy (`earth/flat` is refused by `astra.trim`);
  - the YAML 1.1 `off` → `False` conversion in the tools' `safe_load` → `from_dict` route, verified during the
    build before it is reported.

## Verification and process

1. Build: `just python` in the worktree. Verify the T3 fixture first.
2. `.venv/bin/pytest tests/python -k cli` (the M6 plan's acceptance command), then ruff and pyright strict via the
   gate.
3. `./scripts/check-full.sh` from a fresh configure (`build/` removed first), run with the Bash tool's
   `run_in_background`. Pass means exit 0 plus `check: OK`. Commit the trimmed
   `docs/reviews/evidence/<head>-gate-summary.log`.
4. Commit coherent steps and push the task branch. Open the draft PR at the first commit, with a body per
   `docs/templates/pr-description.md`.
5. Codex review of the final diff per `docs/agents/review-dispatch.md` (Tier 2), focused on exit-code precedence,
   stdout versus stderr, and the log file's fate on every failure path. Findings are verified, with at most two
   correction rounds. Results go in the PR body.
6. `/human-review`, then `/wrap` before merge. Joe reads the diff and tests.
