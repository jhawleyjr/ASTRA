# M6 PR 11: configs read through PyYAML

Spec: PLAN.md's (M6) PyYAML item; lessons 2026-09-28 (unsigned exponents) and 2026-09-30 (YAML 1.1 booleans);
M6 PR 7b's `test_planted_r9_config_pyyaml_route_matches_yaml_cpp`; `astra.cli._with_log` (the route
`astra run --log` already avoids).
Risk: **Tier 2**. The change is one new private Python module, four call sites and tests. Nothing changes in
`core/`, `python/bindings/`, `plugins/`, `icd/`, `tests/golden/**`, tolerance files or ADRs, and there is no
new dependency. The recommended design needs **no binding** (D1).
Base: `origin/main` `72877da` (#96 merged). Worktree/branch: `m6-pr11-yaml-route`, which this session enters
after approval. This file then moves to `docs/plans/m6-pr11-yaml-route.md` in that worktree. Draft PR at the
first commit.
Binding: architecture §13 (one Config tree, frozen for the run) and §15 (unknown keys are errors); ADR-0025 (a
log stores its config as `emit_without("log")` of the run's document). No Codex plan critique: the design is
small, and every claim it rests on is a probe recorded below.
Status: **approved by Joe, 2026-10-04, D1 to D5 as written, with six amendments, folded in below**: (1) D2 states
the text-key residual; (2) the check's cost per trimmed run (astra.trim now reads its config once); (3) no
`RouteError` clause in `cli._trim`, and each module's `error=` is confirmed; (4) the probe table adds an alias,
block scalars and `0.10` on a text key; (5) D5 prints each run's package, `_core` and plugin dir, at one worker
count on both sides; (6) the PR body records `--collect-only -q` for `-k pyyaml_route`, and gate time excludes
R9's test.

## Context

Four tools read a config with `yaml.safe_load` (YAML 1.1), edit it as a dict and give it to the core through
`Config.from_dict`, which emits YAML text for yaml-cpp. When PyYAML's reading of a scalar is not yaml-cpp's
reading, the run uses a config the file does not say. Lesson 2026-09-30's example: a config with
`sim.health.nan: off` loads with `astra run` but is refused by every tool, because PyYAML turns `off` into
`False` and the core then reads `false`. Today `010` reaches the core as 8 (the core reads 10.0), and `1_000`,
`1:30`, `0x10` and `0b101` reach it as numbers the core would refuse. `~`, a date, an integer above int64 and
a non-text key make `from_dict` raise `TypeError` or `RuntimeError`, which escapes the tool's error class. The
outcome: every tool reads a config the way the core reads it, or refuses it and names the key. Configs that
have no such form keep byte-identical outputs.

## The route today (read at `72877da`)

| Module | PyYAML read → `from_dict` | What it edits as a dict |
|---|---|---|
| `astra.montecarlo` | `_yaml` (`montecarlo.py:353`) reads the spec and the base config's bytes. `_base_config` refuses `transport`/`log` blocks. `_validated` builds `Simulation.from_config(Config.from_dict(config))` once to learn `dt` and signals. `loads` re-reads the recorded bytes for `rerun` (`batch_spec`). | `_dispersions` reads each base value from the dict (`_runkit.numeric_leaf`). Each run sets the draws (`set_numeric_leaf`), then `_runkit.apply_trim` and `_runkit.run_logged` adds `log: {path}`, followed by `from_dict`. |
| `astra.scenario` | `_base_config` (`scenario.py:287`) reads the config and refuses `transport`/`log`. `load` builds through `from_dict` to learn the signals and inputs. | `overrides` go through `set_numeric_leaf`, and the values come from the scenario file. Then `trim` (`apply_trim`) and the log block (`run_logged`). |
| `_runkit.apply_trim` (both of the above) | It writes `yaml.safe_dump(data)` to a temporary copy, which `astra.trim` reads again with PyYAML. | It replaces `initial.state` (`p_n`, `v_b`, `euler_nb`, `omega_b`), pops `q_nb` and sets the controls `elevator` and `throttle`. |
| `astra.trim` | `_read_yaml_dict` (`trim.py:55`) is called twice: by `_earth_plugin_path` and by `_load_still_air_config`, which builds through `from_dict`. `astra.linearize` and `astra trim`'s `_initial_setpoint` also go through `_load_still_air_config`. | It drops `log` and `physical.wind`. The state and controls are written into the `Simulation`, not into the dict. |
| `astra.lockstep` | `trim_for` (`lockstep.py:181`) and `load_config` (`:282`) read the config. `run` (`:647`) builds through `from_dict`. | `trim_for` strips `transport` and writes a `safe_dump` copy for `astra.trim`. `load_config` sets `transport.segment`, optionally the three timeouts (each must be an `int`), `sim.frames` and `physical.wind.seed`. |

`astra run` (`_with_log`), `astra.log.replay` (`from_string` of the stored text) and `astra report` (it
escapes the recorded texts and never parses them) do not use the route. `mc_sizing` parses only the spec,
for its seed.

## Probe table (on this machine's build: PyYAML 6.0.3 with libyaml, yaml-cpp 0.8.0, `_core` built 2026-10-01; C++ unchanged since)

"Core" is `Config.from_string("k: <form>")` read by `number / integer / boolean / text` (X means refused).
"Route" is what the core reads from `Config.from_dict({"k": <PyYAML value>})`.

| Form | PyYAML | Core reads | Route: the core reads | Today | After (D1, recommended) |
|---|---|---|---|---|---|
| `on` `off` `yes` `no` (every case: `Off`, `NO`, `True`) | `bool` | X / X / bool / `'off'` | text `'false'` (the boolean is the same) | An enum key (`sim.health.nan: off`) is refused at build. A boolean key works. | Repaired to `'off'`, the core's reading, so the enum builds. |
| `y` `n` | `'y'` | X / X / True / `'y'` | same | agree | unchanged |
| `010` | 8 | **10.0 / 8** / X / `'010'` | 8.0 / 8 | A number key gets 8.0, but the core reads 10.0. | Repaired. The core's own reading applies, and it depends on the getter. |
| `0o10` | `'0o10'` | X / X / X | same | agree | unchanged |
| `0x10` | 16 | X / **16** / X | 16.0 / 16 | A number key gets 16.0, but the core refuses it. | Repaired. A number key is refused at build, as `astra run` refuses it. |
| `0b101` | 5 | X / X / X | 5.0 / 5 | The route invents 5. | Repaired, then refused at build. |
| `1:30`, `190:20:30` | 90, 685230 | X / X / X | 90 | The route invents 90. | Repaired, then refused at build. |
| `1_000` | 1000 | X / X / X | 1000 | The route invents 1000. | Repaired, then refused at build. |
| `1.0e3` `1e3` `1e+3` | `str` | 1000.0 / X / X | same text | The core reads 1000.0 either way. A Python number check (a dispersion base, an override target) refuses the string, as it does now. | unchanged |
| `1.0e+3` `1.5E+3` `1000.0` `1.` `.5` `+12` | `float`/`int` | 1000.0 / X | text `'1000'`, number 1000.0, **integer 1000** | Same number. An integer key accepts `1000.0` through the route but not through the core (residual, below). | unchanged: the same number written two ways |
| `0.1` `0.10` | 0.1 | 0.1 | text `'0.10000000000000001'`, 0.1 | same number | unchanged |
| `-0.0` | -0.0 | -0.0 / X | `'-0'`, -0.0, integer 0 | same number | unchanged |
| `.inf` `-.inf` `.nan` `.NaN` | `float` | ±inf / nan | `.inf` / `.nan` | agree | unchanged |
| `~` `null` `Null` (empty) | `None` | a null node (not a scalar), text `'null'` | `from_dict` raises `TypeError` | `TypeError` escapes, not the tool's error | Refused at read, naming the key. |
| `2026-10-03`, `2026-10-03 12:00:00` | `date`/`datetime` | X / X / X / the text | `TypeError` | escapes | Repaired to the text. A number key is then refused at build. |
| `9223372036854775808` | `int` > int64 | 9.22e18 / X | `RuntimeError: std::bad_cast` | escapes | Repaired. The core's 9.22e18 applies. |
| `!!binary aGk=` | `bytes` | text `'aGk='` | `TypeError` | escapes | Repaired to `'aGk='`. |
| `!!set {a, b}` | `set` | a map of nulls | `TypeError` | escapes | Refused at read (a null or structural difference). |
| `'off'` `'010'` `"1_000"` `!!str 010` `!!float 1` `1e+400` `1e-400` | as quoted/tagged | the same text | same | agree | unchanged |
| Duplicate key `a: 1` / `a: 2` | `{a: 2}` (last) | first (1), and the leaf is listed twice | 2 | The route reads the last. | Refused at read, naming the key. |
| Merge key `<<: *b` | merged | `<<` stays a key (yaml-cpp 0.8 does not merge), so it is an unknown key at build | merged | The route builds a config the core refuses. | Refused at read, naming `m.<<`. |
| Key `on:`, `010:`, `~:` | `True`, 8, `None` key | `'on'`, `'010'`, `'null'` | `RuntimeError: std::bad_cast` | escapes | Refused at read, naming the key. |
| Two documents | PyYAML refuses | the core reads the first | – | The tool refuses (its YAML error). | unchanged |
| Alias without merge (`a: &x {v: off, w: 010}`, `b: *x`) | one shared dict: `{v: False, w: 8}` twice | each path read as its own text (`'off'`, `'010'`) | `'false'`, 8.0 at both paths | as each form alone | Repaired at both paths; PyYAML shares the dict, so one repair serves both, with the same text. |
| Block scalars `\|`, `\|-`, `>` (`s: \|-` then `off`; `>` then `010` / `more`) | `str` (`'off'`, `'010 more\n'`) | the same text | same | agree (a block scalar is always a string to PyYAML) | unchanged |
| `0.10` on a key the core reads with `text()` | 0.1 | text `'0.10'`, number 0.1 | text `'0.10000000000000001'`, number 0.1 | the text changes | unchanged: the text-key residual (D2) |

How I checked the claims the design rests on:
- **Repair is exact.** For every repaired form above, `from_dict({"k": core.text("k")})` gives the core's four
  readings exactly: yaml-cpp ignores quoting when it converts a scalar. `from_dict` quotes `"null"` and
  `"~"`, so they stay scalars.
- **No tracked config is touched.** All 36 tracked configs (`aircraft/**/configs`, `tests/configs`, NESC,
  fixtures) agree leaf by leaf with the rule in D2, with zero repairs. The check costs 0.3 to 4.6 ms per
  config, 50 ms in total.

## D1, the fix (recommendation: (b), repair the dict to the core's reading; no binding)

| Option | What it does | Cost |
|---|---|---|
| (a) Refuse | Read the config both ways. Refuse a leaf whose readings differ (D2), naming the key and both readings. | One module (~100 lines) and five call sites. The run path does not change. `nan: off` is still refused, now with a message that says to quote it. |
| **(b) Repair** | Build the dict as now. For every scalar leaf whose readings differ (D2), put the core's own text (`Config.text`) in the dict. Then build the routed Config again and verify that no leaf differs. Refuse what text cannot repair: nulls, structure (duplicate or merge keys, non-text keys, a set) and any leaf that still differs after the repair. The edits then apply to a dict that the core reads exactly as it reads the file. | (a) plus about 25 lines. **No binding**: `Config` already exposes `from_string`, `unknown_keys` (every leaf path), `at`, `is_scalar`/`is_map`/`is_sequence` and the four getters. `nan: off` builds, and a number the core refuses is refused at build, as in `astra run`. |
| (b′) Core-side edits | Parse with the core and edit the core's document (bind a setter and `emit_without`). | Binding plus core change, so Tier 3. Architecture §13's frozen Config gets a mutation API. It also changes every log's stored config text (source spellings instead of the route's), so the byte-identity below fails by design. |
| (c) A YAML 1.2 reader (ruamel.yaml) | Replace PyYAML for configs. | New dependency (your call). It does not close the gap: yaml-cpp's typing depends on the getter (`010` is 10.0 by `number`, 8 by `integer`; `0x10` is an integer only), so no single schema matches. The route's text changes remain. |

Why (b): it fixes the reported failure, not just its message. It refuses everything (a) refuses that text
cannot express. Its acceptance check is (a)'s comparison, run after the repair. Configs with no differing
leaf are untouched, so their outputs stay byte-identical by construction. If you prefer (a), drop the repair
step, and the tests that expect "same as the quoted spelling" expect a refusal instead.

The module is `python/astra/_yaml_route.py`: `load(text, origin, *, error) -> Any`. It calls
`yaml.safe_load` and lets `yaml.YAMLError` propagate, so each caller keeps its own message. A non-map result
is returned untouched, because callers already refuse it. Otherwise it reads `Config.from_string(text,
origin)`; a document PyYAML reads but yaml-cpp refuses is refused through `error`. Then it walks, repairs,
verifies and returns the dict. `error` is the caller's error class (the `_runkit` convention), and it is
required: `montecarlo` passes `SpecError`, `scenario` `ScenarioError`, `lockstep` `LockstepError`, and `trim`
the core's own `_core.ConfigError`. Python can raise it (probed: a subclass of `_core.Error` and
`RuntimeError`), and `cli._trim` already maps it to 65. Amendment 3's premise did not hold: `cli._trim`
re-raises any `ValueError` without the flat-Earth prefix as a defect (exit 70), so a `ValueError` subclass
would have needed a clause. No new exception class and no new clause. It imports only `yaml` and `_core`, so `astra.trim` can use it without a cycle
(`_runkit` imports `astra.trim`).

## D2, "differs" (per leaf of the core's reading)

Each leaf has two readings, the core's reading of the file and its reading after the route. Each reading is
the four getter outcomes (a value, or refused) plus the node kind (scalar, map, sequence, null). The leaf
**differs** when any of these holds:
1. the node kinds differ;
2. `number` outcomes differ (bitwise, so -0.0 ≠ 0.0, and NaN equals NaN);
3. `boolean` outcomes differ;
4. the core reads an `integer`, and the route reads a different one or refuses;
5. the core reads **no number**, and the texts differ.

So a number written two ways (`1.0e+3` / `1000`, `0.1` / `0.10000000000000001`, `.NaN` / `.nan`) does not
count. `on` / `true` counts, and so does `True` / `true`: they are different texts with no number, and a text
key would see the change. The tool cannot know whether the key is read as a boolean or as text, so it
assumes text. Beyond the four getters, the multiset of leaf paths must match: a duplicate key lists a path
twice, and a merge key lists `m.<<.x` where PyYAML has `m.x`. A key that is not text, and a null, are
refused before `from_dict`, because it cannot carry them.

The residual, which stays as today: an integer-typed key written `1000.0` or `1.0e+3` is accepted by the
route (it emits `1000`) and refused by `astra run`. The value is equal, and this is your number exemption.
Beside it, also not fixed here: a key the core reads with `text()` keeps the route's spelling of a
numeric-looking value (`0.10` reaches it as `'0.10000000000000001'`), because rule 5 exempts any leaf the
core reads as a number.

Cost per run. `astra.trim` now reads its config once per trim (it read it twice: `_earth_plugin_path` and
`_load_still_air_config`). So a trimmed run checks two configs: the base config once at load, and then
`apply_trim`'s `safe_dump` copy once per run (per draw in a batch). At the probe's 0.3 to 4.6 ms per config,
that is at most about 5 ms per trimmed run (cruise.yaml: 3.6 ms), against a run of 60,000 frames in Q19's
batch. The PR body states the measured figure.

Messages name the file, the dotted key and both readings. For example: `cruise.yaml: sim.health.nan: PyYAML
reads off as False, which reaches the core as 'false'; the core reads 'off'`. The repair message appears only
on a refusal.

## D3, scope (recommendation: configs only)

The Monte Carlo spec, scenario files, plot packages and sizing claims are Python's own formats. Only PyYAML
ever reads them, at load and at rerun alike, so no second reader can disagree. Their numbers already go
through strict checks (`_runkit.number` refuses a `bool`, and an unsigned exponent gets a hint;
`_integer` refuses a `bool`). They still have YAML 1.1 surprises (`seed: 010` is 8, `duration: 1:30` is
90 s, `runs: 1_000` is 1000, and an override `010` reaches the config as 8). This PR states in the
`montecarlo` and `scenario` docstrings that these files are YAML 1.1 as PyYAML reads them, and lists those
forms. Refusing them would mean a composer-level check on another format, so it is not in this PR.

## D4, batches already on disk

`astra report`, `mc analyze` and `mc_sizing` never parse the recorded config, so an old batch behaves as
before. `mc rerun` re-parses it through `loads` → `_base_config`, which now repairs. A batch written before
this PR whose config has a differing leaf ran with PyYAML's reading:
- If the form made the build fail (`nan: off`), no batch exists.
- If it is a dispersion's base (for example `010`), `loads` refuses it: `numeric_leaf` finds the text `'010'`,
  "not a number". The message gets a hint that the core reads this text differently from PyYAML.
- Otherwise, rerun re-executes with the core's reading. The row comparison (and the log comparison when the
  log is kept) reports the difference. That is rerun's existing contract ("the record is not a frozen
  build"), and the docstring gains a sentence for this case.

No batch built from a tracked config is affected, since zero leaves differ. Recommendation: no legacy route
and no record marker. A marker would change every new summary's bytes.

## D5, what must not change: before/after against `72877da`

Configs with no differing leaf leave the dict untouched, so the run path is unchanged. This is checked, not
assumed:
- **How.** `git archive 72877da python/astra` goes into `scratchpad/base/`, together with the candidate's
  `_core*.so` (the PR has no C++, so both trees use one build and the same compiled-in plugin directory). A
  scratch driver, `compare.py`, runs every fixture under one tree, then the other. For the base tree it
  removes the editable finder (`_editable_skbc_astra`) from `sys.meta_path` and prepends `scratchpad/base`.
  Each run prints `astra.__file__`, `astra._core.__file__` and `_core.default_plugin_dir()` as evidence of
  which tree ran on which build. Every batch runs at the same worker count on both sides (4). Both runs use
  the worktree's fixture files, start from the same cwd and write to the same output path, which is renamed
  afterwards. Origins and messages therefore match. A sha256 manifest of every output file is diffed, then
  `cmp` is run on any mismatch.
- **Fixtures:**
  - R9's batch (`planted.yaml`, 130 runs) and its control (`planted_control.yaml`).
  - The descent batch (`descent_batch.yaml`: ok, halt and error rows).
  - The trimmed Q19 batch (`cruise_64.yaml`).
  - Every scenario in `tests/scenarios/` (`cruise_hold` is trimmed) and `tests/python/scenario_fixtures/`:
    each log's bytes, plus each result's `failures` list.
  - `astra trim` stdout on `cruise.yaml`.
  - `astra.linearize`'s A and B matrices (`np.save` bytes).
  - `lockstep.load_config` and `trim_for` on the light-turbulence config: the dict's `repr` and the trim
    values.
  - Each batch's `summary.parquet` and every `run_*.h5`.
- It passes when the manifests are identical. The driver and both manifests stay in the scratchpad, and the
  result (file count, the identical manifests' digest, which tree each run imported) goes into the PR body.
  If `python/` changes after this step, it is rerun on the final candidate.

## Tests (`tests/python/test_pyyaml_route.py`; acceptance `.venv/bin/pytest tests/python -k pyyaml_route`)

Each config variant is written as **text** (a fixture's text with one value substituted), never through
`safe_dump`. "Same as quoted" means the tool's output on the form is byte-identical to its output on the
same file with the core's text quoted (`'off'`, `'010'`). PyYAML and the core agree on the quoted spelling,
and the repair produces exactly that string.
- `test_pyyaml_route_probe_table[<form>]`: `_yaml_route.load` on every row of the probe table. It asserts
  unchanged, repaired to the core's text, or refused (matching the key and both readings). For every form
  that is not refused, it asserts that the routed Config's readings equal the core's for every leaf.
- `test_pyyaml_route_montecarlo[<form>]`: a 1-run, 0.05 s batch of `descent.yaml`. Forms:
  - `off` in `sim.health.nan`, and `010` in `force_x` (not dispersed): the summary and log are the same as
    the quoted spelling.
  - `1_000`, `1:30`, `0x10` and `0b101` in `physical.mass.mass`: `SpecError` at build, carrying the core's
    "expected a number" for that key, the same message `Config.from_string` gives.
  - `~`, a duplicate key, a merge key, and an `on:` key: `SpecError` at read, naming the key.
  - `010` as a dispersion's base: refused, with the hint.
  - `1.0e+3` / `1000.0` in a dispersed leaf: accepted, and byte-identical to `1000.0`.
- `test_pyyaml_route_scenario[<form>]`: the same forms through `scenario.load` and `run`, on `descent.yaml`
  with one check. A dispersion's base becomes an override target.
- `test_pyyaml_route_trim[<form>]`: `trim_straight_and_level` on `cruise.yaml` variants. `off` in `sim.health.nan`
  and `010` in a mass-model leaf give a result exactly equal to the quoted spelling. The configs have no
  boolean key, so the probe-table test covers `yes` and `True`. The refusal forms give the core's
  `ConfigError` (at read, or at build). Also `astra trim` on a refused form exits 65 through its existing
  `ConfigError` clause.
- `test_pyyaml_route_lockstep[<form>]`: `load_config` and `trim_for` on the light config. For a repaired form,
  the dict equals the quoted spelling's dict, and `trim_for` is equal. Refusals are `LockstepError`. No peer
  is launched.
- `test_pyyaml_route_rerun_of_a_batch_read_by_pyyaml`: run a 1-run batch on `force_x: 8`, then rewrite the
  summary's recorded config to `force_x: 010` (what such a record held). `rerun` reports the differing row
  (D4). `astra report` on the same batch still renders.
- `test_pyyaml_route_tracked_configs_need_no_repair`: every tracked config loads with zero repairs. This
  guards D5's premise.

**What else `-k pyyaml_route` selects:** `test_mc_planted.py::test_planted_r9_config_pyyaml_route_matches_yaml_cpp`
(R9's 10 s route check, which is relevant). I found no other collision in a grep of `tests/`.
`--collect-only -q` confirms the list before the gate, and the PR body records the full list. Existing tests that
assert messages from `_base_config` and `trim` still pass unchanged, because YAML errors keep their
callers' messages.

**Gate time** (R9's test excluded throughout; it was in the gate before this PR): measured after the build
on a quiet machine:
1. `-k pyyaml_route --durations=0`, summed without R9's test: the new tests.
2. The check's per-load cost (probe: 0.3 to 4.6 ms) times the number of config loads in the suite, counted
   by a scratch spy.
3. Full `pytest -q` wall time at the base and at the candidate, one run each, with the noise stated.

## Files

- New: `python/astra/_yaml_route.py` and `tests/python/test_pyyaml_route.py`.
- Call sites:
  - `montecarlo._yaml`, for the config only, through `_base_config`.
  - `scenario._base_config`.
  - `trim._read_yaml_dict`.
  - `lockstep.trim_for` and `lockstep.load_config`.
  - `_runkit._require_number`: the hint for a text leaf.
  - `cli`: docstring line 67 only (no new clause).
  - `trim.trim_straight_and_level`: reads the config once.
- Docstrings: `montecarlo` (record/rerun and spec format), `scenario`, `trim`, `lockstep` and `_runkit`'s
  list.
- Docs:
  - PLAN.md: a PR 11 line under M6, and the (M6) PyYAML item closed with this PR's evidence.
  - `/wrap` adds the review-record-gap open item (M6: a fix's final Codex verification cannot be written
    into `docs/reviews/*-review.md` under review-dispatch's no-record-only-commit rule, so the last finding
    stays "fixed-unverified" there; #95 V-001, #96 R-001).
  - A lessons entry: yaml-cpp's reading depends on the getter (`010` is 10.0 or 8; `0x10` is an integer
    only), yaml-cpp 0.8 does not merge `<<`, and `from_dict` crashes on `None`/dates/large ints/non-text
    keys.
  - STATE.md's next step becomes PR 12, then 13, 14 and 10.

## Verification and process

1. Enter the worktree, commit this plan as `docs/plans/m6-pr11-yaml-route.md`, push and open the draft PR.
   Fast loop: `./scripts/check-fast.sh`, then `.venv/bin/pytest tests/python -k pyyaml_route`, then the
   affected modules (`-k 'montecarlo or scenario or trim or lockstep or cli or mc_planted'`).
2. D5's before/after on the candidate, and the gate-time measurements.
3. `just ruff` (both steps), then `ps`/`pgrep -x` per lesson 2026-10-02 for other builds, recording other
   projects' load and the 1-minute load average in the evidence. Then `rm -rf build && ./scripts/check-full.sh`
   with no edits during the run (background, waited on its pid). The pass needs exit 0 and `check: OK`.
   Commit `docs/reviews/evidence/<head>-gate-summary.log`.
4. `/review-cycle` (Codex, fresh context), at most two correction rounds. The #94 gate-rerun rule applies:
   after a passing gate, only a Markdown/evidence-only delta skips a new gate.
5. `/human-review`: the PR body per `docs/templates/pr-description.md`.
6. `/wrap` before merge. Its docs step runs before the final gate where it can.

## Decisions for Joe

- **D1:** (b), repair. The alternative is (a), refuse.
- **D2:** "differs" as defined above, including boolean words written two ways (`True` / `yes` count).
- **D3:** configs only. The spec and scenario formats stay YAML 1.1 and are documented.
- **D4:** old batches get no legacy route. Rerun reports any difference, and a dispersion base in a 1.1 form
  is refused.
- **D5:** the before/after method and fixture list above.
