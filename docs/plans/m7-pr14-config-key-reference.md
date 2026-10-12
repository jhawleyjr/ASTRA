# M7 PR 14: the generated config-key reference

Status: **approved by Joe, 2026-10-11, in plan mode**, with his coverage decision (amendments 1 to 8), D1 to D3
and his approval amendments A1 to A8 folded in below.
Owner: Joe. Builder: Claude (one builder). Tier: **2** (comments, scripts, docs, Python tests).
Base: `origin/main` = `acdc6b5` (#115). I read it with `git ls-remote` in plan mode, and `git fetch` confirmed it.
Branch and worktree: `worktree-m7-pr14`, from `origin/main`. This file started as the session's plan-mode draft
and was moved into the worktree (`docs/agents/claude-code.md`). Nothing stays untracked in the main checkout.
Sources: the [M7 plan](m7-f16-nesc-oracle.md)'s Q21 (decided as recommended, 2026-10-04), the contract's
"Config-key reference (Q21)" paragraph, R17, R19, the PR table's row 14 and the obligations table's row 14 (from
[PR 4's plan](m7-pr4-mass-convention.md) §4); STATE.md's PR 14 thread. Binding: ADR-0009. There is no Codex plan
critique, because the design is pinned. PR 7 runs in parallel in `m7-pr7`. This PR touches neither
`tests/physics/nesc/` nor `core/`.

## Context

Nothing lists a plugin's config keys outside its source. Q21 pins the fix. Each plugin's header comment gets a
structured key block. `scripts/config_keys.py` renders the blocks into `docs/reference/config-keys.md`.
`just docs-check` fails when that file is stale. A pytest checks the blocks against the keys the core reports
as consumed. R17's reference half is `mass/constant`'s block, which states the S&L product-of-inertia convention.
No plugin's behavior changes, and `kCoreApiVersion` stays at 11.

## Joe's decision and amendments (2026-10-11)

The second check ("a documented key no plugin reads") cannot be met from the 37 tracked configs. About 50 of
roughly 110 documented keys are consumed by no build. Joe chose **fixtures**
(`tests/python/config_keys_fixtures/`), with these amendments:
1. The fixtures are ordinary tracked configs, found by the same rule as every other config. The test keeps no
   list of keys and no list of configs. Per plugin, the documented set equals the union of the keys consumed
   under that plugin's blocks, over every tracked config. That union is read from the `Config` object the test
   holds after `Simulation.from_config` returns or raises (§4).
2. A refusal is declared, never silent: a table of expected refusals, not of keys (§4.2).
   *Correction:* four tracked configs refuse today, not two. `cruise_light_turbulence.yaml` and
   `cruise_moderate_turbulence.yaml` refuse at `transport.segment` because `astra.lockstep` supplies it at run
   time. They are measured in plan mode on the installed build, with every leaf consumed.
3. A sequence element has one notation, and the generator and the test share one normalization function (§1).
4. The loopback fixture sets every schedule field and every fault kind. The fields are listed in §5 from
   `loopback.cpp`, and the plugin accepts them all in one config.
5. A sixth fixture holds the F-16's three plugins in bare blocks (§5). §4.4's "every documented plugin is
   reached" turns it into a check.
6. The mass fixtures set nonzero products on a positive-definite tensor (§5). *Finding:* one harness does glob
   the directory (D2).
7. The scope is stated in the test's docstring and the reference's header. A block whose plugin is under
   `tests/plugins/` is skipped, and the test's output names it (§4.5).
8. Two negative runs join the docs-check one, and the PR body quotes all three (§9).

## Joe's approval (2026-10-11): D1 to D3 and amendments A1 to A8

- **D1: yes.** §18 step 1 gets the clause (§7).
- **D2:** the directory name stays.
- **D3:** both pointers go in (§7).
- **A1** (§4.1): a tracked YAML that `Config.from_file` cannot load fails the test, naming the file. It is never
  skipped as "not a config". If one turns up during the build, it goes to Joe, not into a wider rule.
- **A2** (§4.3): the test's docstring says why the withheld build exists. It cites the two read-before-load sites
  by function name, `resolve_model_spec` and `LockstepExchange`'s constructor, never by line number.
- **A3** (§5): `shm.yaml`'s comment cites the `shm.cpp` rule that refuses the name. The test asserts that the
  `ConfigError` names `transport.segment`, and that no file of that name exists under `/dev/shm` after the run.
- **A4** (§8): no claim that object code is unchanged; the evidence is stated as §8 states it.
- **A5** (§4.2): the measurement of the two turbulence configs goes in the PR body, with its command.
- **A6** (§10): if the "approved by Joe" Status write is refused, lesson 2026-09-29 applies (§10, step 1).
- **A7** (§9): each negative run is its own command with its own restore, and `git status --porcelain` is
  shown clean between the runs and before the gate.
- **A8** (§6): each of the F-16's three blocks is `none` and nothing more.

## 1. The key block and the generator's parsing rule (`scripts/config_keys.py`)

The generator is stdlib only and runs as `python3 -I -B scripts/config_keys.py [--check] [--root DIR]`.
- **Plugin set.** The plugins are every directory under `plugins/` or `aircraft/` whose `CMakeLists.txt` calls
  `astra_add_plugin(`. The plugin path is that directory, relative to the root, as ADR-0009 derives it. There
  are 22 today, all single-file except `shm`.
- **Where the block lives.** It sits in the *leading comment* of the first file named in `SOURCES`: the lines
  from the top of the file that start with `//`. Each plugin has exactly one block. A missing block, or a
  second one, is an error.
- **Extent.** The block opens at the exact line `// Config keys:`. It ends at the first empty `//` line or at
  the end of the leading comment. Inside it, each line's text is what follows `//`, stripped:
  - `none` says the plugin reads no key. It must be the block's only line.
  - A line containing `|` is a **row**: `key | type | unit | default | meaning`. It is split on `|` at most
    four times, each cell is stripped, and all five cells must be non-empty.
  - A line with no `|` **continues** the previous row's meaning. It is an error before the first row.
- **key.** A path relative to the plugin's block. Its segments are `[a-z][a-z0-9_]*`, joined by `.`. A segment
  that is a sequence takes `[]`, once per level. Keys are unique within a block.
- **type.** One of `number`, `integer`, `boolean` or `text`, named for the `Config` getter that reads the key.
- **unit.** The unit string as ADR-0002 writes it in the SignalRegistry (`kg`, `kg*m^2`, `m/s`; `1` for a
  dimensionless number or a count). A config value in a boundary unit keeps it (`w20_knots`: `kt`). A boolean
  or text key takes `-`.
- **default.** Either `required`, or the value used when the key is omitted, written as YAML (`0`, `false`,
  `9.80665`), or `-`, when an omitted key means something the meaning states. A condition on `required` (only
  for `kind: non_finite`, say) goes in the meaning.
- **Normalization (one function, imported by the test).** The core names a leaf with numeric index segments
  (`transport.peer.schedule.0.throttle.1`). Relative to its block, every all-digit segment is dropped, and
  `[]` is appended to the segment before it: `peer.schedule[].throttle[]`. A key segment starts with a letter,
  so an index is never mistaken for a key.
- **Errors.** Every problem is printed as `file:line: reason`, and the exit code is 1. `--check` writes
  nothing. It exits 1 when the reference is missing or differs, naming the first differing line and the
  regeneration command. Without `--check`, the generator writes only `docs/reference/config-keys.md`.

`mass/constant`'s block (R17's reference half; Joe reads it). Each row fits the 100-column limit, which
`format-check` enforces:
```
// Config keys:
//   mass | number | kg     | required | The vehicle's mass, finite and positive.
//   ixx  | number | kg*m^2 | required | Moment of inertia about body x (FRD) through the CG.
//          The tensor built from all six keys must be positive-definite.
//   iyy  | number | kg*m^2 | required | Moment of inertia about body y (FRD) through the CG.
//   izz  | number | kg*m^2 | required | Moment of inertia about body z (FRD) through the CG.
//   ixy  | number | kg*m^2 | 0        | S&L product of inertia, Ixy = integral(x*y*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's xy entry.
//   ixz  | number | kg*m^2 | 0        | S&L product of inertia, Ixz = integral(x*z*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's xz entry.
//   iyz  | number | kg*m^2 | 0        | S&L product of inertia, Iyz = integral(y*z*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's yz entry.
```

## 2. The generated reference (`docs/reference/config-keys.md`)

- **The header** says three things:
  - the file is generated, and how to regenerate it;
  - the scope, per amendment 7: the core's own keys are not here (`sim`, `log`, `initial`, `vehicle` outside the
    model blocks, each block's `plugin` and `divisor`, and `transport` outside its plugin's keys), and neither
    are `tests/plugins/`;
  - the `[]` notation.
- **One section per plugin,** sorted by path. Each has the plugin path as its heading, the source file, and a
  table: Key, Type, Unit, Default, Required (yes when the default is `required`), Meaning. A `none` block
  renders "Reads no config key: a config names it by its path alone."
- **The output is deterministic.** Rows stay in block order. The newlines are LF, and there is no timestamp
  and no absolute path. In a meaning, `|` is escaped, and so are `\ * _ <` outside backtick spans, so that a
  formula such as `integral(x*z*dm)` renders as written.

## 3. docs-check (`scripts/docs_check.py`)

There is one new `check_config_keys(root, problems)`. It runs `[sys.executable, "-I", "-B",
<this script's directory>/config_keys.py, "--check", "--root", root]` and adds each line the generator prints
to `problems`. It runs as a subprocess, not an import, so nothing writes `__pycache__` into `scripts/`
(lessons 2026-10-01 and 2026-10-07). The module docstring gains one clause. `just docs-check` needs no new
line, because it already runs `docs_check.py`. Only the recipe's description comment gains a clause.
`test_docs_check.py` calls the individual checks, never `main()`, so it is unaffected.

## 4. The consumption test (`tests/python/test_config_keys.py`)

The test loads the generator through `compile` and `exec` into a fresh module, so it writes nothing. It shares
the parser and the normalization function with the generator. It builds through `_core` and the installed
plugin directory, with `cwd = tmp_path`. It asserts `PluginLoader.is_static()` is false, because the
withheld-library build needs a plugin directory.

### 4.1 Tracked configs
These are every file from `git ls-files -- '*.yaml' '*.yml'` whose document has top-level `sim` and `vehicle`,
read through the core (`Config.from_file(...).has(...)` on a throwaway `Config`). Today there are 37, and
43 once the six fixtures land. A tracked YAML that `Config.from_file` cannot load fails the test, naming the
file. It is never skipped as "not a config" (A1). In plan mode, every tracked YAML loaded. If one fails
during the build, it goes to Joe and the rule stays as it is.

### 4.2 Builds and declared refusals
Each config is built once from a fresh `Config`. Every leaf is listed first (`unknown_keys()` before any read),
and `consumed = leaves − unknown_keys()` is read after `from_config` returns or raises.
`EXPECTED_REFUSALS` is pre-registered as config to key:
- `aircraft/_template/configs/cruise.yaml` → `vehicle.models.0.wing_area_m2` (pinned by
  `test_trim_round_trip`);
- `aircraft/notional_transport/configs/cruise_light_turbulence.yaml` → `transport.segment`;
- `aircraft/notional_transport/configs/cruise_moderate_turbulence.yaml` → `transport.segment`;
- `tests/python/config_keys_fixtures/shm.yaml` → `transport.segment`.

A declared refusal must be a `ConfigError` whose message names `at '<key>'`. Any other refusal fails the test
with the config path and the error, and so does a declared entry that builds or is missing.

The attribution for the shm and loopback blocks in the two turbulence configs rests on one measurement: each
consumes every leaf before refusing at `transport.segment`. The PR body carries that measurement with its
command (A5). On the plan-mode install it read 87 leaves and 0 unread for each config.

### 4.3 Attribution, with no key list
A *block* is a map with a `plugin` leaf, and a leaf belongs to its deepest enclosing block.
- **The core reads keys inside blocks too.** These are `plugin` and `divisor` in every block, and the transport's
  sim-owned keys in `transport` (ADR-0022). It reads all of them before loading that block's plugin:
  - `resolve_model_spec` (`simulation.cpp`) resolves every model block first;
  - `LockstepExchange`'s constructor reads the transport's keys before it loads the transport plugin.

  The test's docstring says this is why the withheld build exists. By construction, it subtracts the core's
  in-block reads with no list of core keys, and it cites the two sites by function name, never by line number
  (A2).
- **The withheld build.** The plugins a config names are its leaves whose text passes
  `PluginLoader.is_valid_plugin_path` and has a library in the plugin directory. For each such plugin P under
  `plugins/` or `aircraft/`, the test builds the config again from a fresh `Config`. The plugin directory is a
  `tmp_path` holding a symlink to every library but P's.
- **What that build must raise.** It raises either `PluginNotFoundError` naming P (*P is reached*) or the
  config's own declared refusal. The second case means P's blocks were never reached, as for the template's
  turbofans behind its refused aero.
- **P's keys.** In each of P's blocks, P's keys are the leaves the first build consumed and the withheld build
  did not, normalized as in §1. The core's keys are consumed by both builds and drop out.
- **The failure direction.** A core read made after a plugin loads would be charged to that plugin and would
  fail as undocumented. It cannot pass silently.

### 4.4 The checks
- Every plugin the generator documents is reached by at least one tracked config.
- Per plugin, the documented keys equal the union of its consumed keys. The failure lists both differences:
  consumed keys that are undocumented, and documented keys that nothing reads.
- The cost is about 43 full builds plus about 220 withheld builds, init only, with nothing run.

### 4.5 Out of scope, and named
The docstring states amendment 7's scope. Blocks whose plugin is under `tests/plugins/` are skipped and printed,
one line each (config, block path, plugin). `pytest -rP -k config_keys` shows them, and the PR body quotes them.
They are not warnings: `filterwarnings = ["error"]` would turn a warning into a failure.

### 4.6 The generator's own tests
- `--check` on the real root exits 0. `git status --porcelain --ignored -- plugins aircraft scripts` is
  unchanged by it.
- A write run on a `tmp_path` copy (every `CMakeLists.txt` and `SOURCES` file) creates only
  `docs/reference/config-keys.md`, and a second run gives the same bytes.
- The parsing rule refuses each malformed block, naming `file:line` (parametrized):
  - no block;
  - two blocks;
  - a block outside the leading comment;
  - four cells;
  - an unknown type;
  - a bad key (`Mass`, `a..b`, `0`);
  - a duplicate key;
  - a continuation before any row;
  - `none` with a row.
- Normalization cases: `points.0.altitude`, `peer.schedule.0.throttle.1` and `a.0.1`.

## 5. Fixtures (`tests/python/config_keys_fixtures/`, ordinary tracked configs)

| File | Sets | Builds? |
|---|---|---|
| `template.yaml` | The template's aero, two turbofans and mass, with the notional transport's numbers (`test_trim_round_trip`'s precedent), plus `ixy: 1000.0` and `iyz: 2000.0` beside the notional `ixz`; flat Earth, constant gravity, ISA | yes |
| `mass_constant.yaml` | `plugins/physical/mass/constant`: diagonal 10, 11, 12; `ixy` 1.25, `ixz` 2.5, `iyz` 0.75 (PR 4's values; strictly diagonally dominant, so positive-definite) | yes |
| `notional_transport_mass.yaml` | The notional mass: `cruise.yaml`'s numbers, with `ixy: 1000.0` and `iyz: 2000.0` beside `ixz: -10846.5` (diagonally dominant) | yes |
| `loopback.yaml` | `no_alloc_transport.yaml`'s base. `peer.commands`: `elevator`, `aileron`, `rudder` and `throttle` `[t0, t1]`. `peer.mode_flags`, `peer.spurious_wakeups`, `peer.handshake_fail: false`. One `peer.schedule` entry with `frame`, `elevator`, `aileron`, `rudder` and `throttle`. `peer.faults`: `frame` and `kind` for each of `out_of_order_ack`, `stale_reply_frame`, `bad_magic`, `bad_version`, `timeout` and `peer_closed`, plus `non_finite` with `field` (one of `elevator`, `aileron`, `rudder`, `throttle_0`, `throttle_1`) and `value` (`nan`, `inf` or `-inf`), from `loopback.cpp:48-200`. Faults have no ordering rule and fire at run time, so one config holds them all | yes |
| `shm.yaml` | `transport.plugin: plugins/sim/transport/shm` with the malformed `segment: config_keys_fixture`. `ShmTransport::init` reads it, and then its `valid_segment` rule refuses it (`astra_<1-10 digits>_<1-32 letters or digits>`). The fixture's comment cites that rule, so the refusal is pinned to a documented check and not to an `shm_open` failure. No segment is created and no peer is awaited. The test asserts that the `ConfigError` names `transport.segment`, and that `/dev/shm/config_keys_fixture` does not exist after the run (A3) | refuses (declared) |
| `f16.yaml` | The F-16's aero, engine and mass in bare blocks; F16NoAlloc's config (`aircraft/f16/tests/no_alloc_test.cpp:126-142`) | yes |

No golden, regression, scenario or tracked config is edited. Every value is a plain decimal or a plain
string, so PyYAML and the core read each fixture the same (D2).

## 6. The 22 key blocks (comment-only)

- Each block goes at the end of its plugin's leading comment, and no other line of code changes. The keys come
  from each plugin's `init`:
  - `earth/round`: `radius` and `rotating`;
  - `gravity/constant`: `g`;
  - `gravity/inverse_square`: `g0`, `radius` and `rotating`;
  - `mass/constant`: 7 keys;
  - `wind/dryden`: `seed`, `w20_knots` and `sigma_high`;
  - `wind/table`: `points[].altitude`, `.north`, `.east` and `.down`;
  - `wind/uniform`: `north`, `east` and `down`;
  - `loopback`: the 16 `peer.*` keys;
  - `shm`: `segment`, required and supplied by `astra.lockstep`;
  - each aircraft's mass, aero and turbofan: 7, 23 and 6 keys;
  - `none` for `isa1976`, `earth/flat`, `wgs84_rotating`, `wgs84_j2` and the F-16's three. An F-16 block is
    the opener and `none`, and nothing more. The sentence that its numbers come from P1 stays in the header
    prose that is already there (A8).
- The template's blocks are byte-identical to the notional transport's, which keeps their headers' "identical
  except this comment" sentences true. `just new-aircraft` copies the template's blocks as they are.
- In `earth/flat`'s header, whose comment this PR edits, "metres" becomes "meters" (American English).

## 7. Docs

- `docs/architecture.md` §13 gets one sentence: every plugin's keys are in
  [the reference](../reference/config-keys.md), generated from each header's key block; docs-check fails when
  it is stale, and a test checks it against the keys every tracked config consumes.
- **D1 (Joe: yes).** After `just new-aircraft`, the copy's plugins carry blocks the reference lacks, so
  `just docs-check` fails until `python3 -I -B scripts/config_keys.py` runs. §18 step 1 gains a clause that
  says so, and PR 15's tutorial carries the step. `new-aircraft.sh` is unchanged.
- **D2 (Joe: keep the name; amendment 6's finding).** `test_pyyaml_route_tracked_configs_need_no_repair` globs
  `tests/python/*_fixtures/*.yaml`, so it also checks the six fixtures. That check is D5's premise for every
  tracked config, and the fixtures are written to need no repair. No other harness globs the directory:
  `test_scenario` globs `tests/scenarios`, and the Monte Carlo, CLI and plot tests name their own directories.
- **D3 (Joe: both).** `docs/process/validation.md`'s docs-check paragraph gains the reference's currency, and
  `docs/README.md`'s table gains a `reference/` row.

## 8. Blast radius (pre-registered)

- The diff holds comments, scripts, docs, Python tests and fixtures only. No plugin's behavior, keys, defaults
  or refusals change, and every tracked config loads as before.
- No `__LINE__`, `__FILE__` or `assert(` appears in `plugins/` or `aircraft/`, and no plugin hash is committed.
- A comment edit moves line tables in the debug and asan presets. The `.so` bytes may differ, then, while
  behavior does not (A4). The evidence is not a hash. It is two things:
  - every `ctest -N` count per preset equals the base's. I record the base's counts in the worktree before
    commit 3, and the gate log gives the candidate's;
  - M2's cases, M3's regression, the R1 golden, M4's logs and M6's fixtures pass unchanged under the gate.
- Only pytest's count grows, and the PR body states by how much.
- Nothing changes under `tests/golden/`, `tests/physics/`, `core/`, `docs/decisions/` or any tolerance file.

What this does not validate:
- An optional key that a plugin reads but that no config sets never reaches the core. The block's author and
  the review are the guard for it.
- A documented default value is not compared with the code's.
- CI's docs filter (`.github/workflows/check.yml:76-79`) skips docs-check for a PR that touches only
  `plugins/`, `aircraft/` or this script. The local gate always runs it. That is out of scope here, and Joe
  may want a PLAN.md item at `/wrap`.

## 9. Checks

- `.venv/bin/pytest tests/python -k config_keys`, then `-rP` for the skipped `tests/plugins` lines.
- `python3 scripts/docs_check.py`.
- Three negative runs, against the installed build. Each is its own command with its own `git restore`, and
  `git status --porcelain` is shown clean after each restore and before the gate, so a leftover edit cannot
  ride into the gated candidate (A7). The PR body quotes all three:
  1. one block's meaning edited without regenerating: `python3 scripts/docs_check.py` must exit 1, naming the
     reference;
  2. `ixz` deleted from `mass/constant`'s block: `pytest -k config_keys` must fail on an undocumented consumed
     key;
  3. a phantom `iyx` row added to the same block: it must fail on a documented key that nothing reads.
- `.venv/bin/pytest tests/python -k new_aircraft`.
- `just ruff` before the gate (lesson 2026-10-01).
- `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`. The trimmed summary goes in
  `docs/reviews/evidence/<head>-gate-summary.log`.
- `git diff --check origin/main..HEAD`.

## 10. Order

1. Joe approves, and the Status line is written as approved by Joe on his word. If that write is refused,
   lesson 2026-09-29 applies (A6):
   - no retry by any other route;
   - the plan is committed with its current Status line, and the PR body names the refusal;
   - the work carries on, and Joe instructs the write in session.

   Then `git fetch` and the worktree. This draft moves into the worktree under the plan's own name. Commit 1
   is this plan. Push, and open the draft PR.
2. **Commit 2:** the generator, the test and the six fixtures. Before it, only `just ruff` and an `ast.parse`
   syntax check run. Nothing runs the generator or the test before this commit. Push.
3. **Commit 3:** the 22 key blocks, the generated reference (the generator's first run), the docs-check
   wiring, and §7's docs (D1 and D3). Then §9's checks and the negative runs. Push.
4. The full gate, then commit 4: the gate summary. Push.
5. One Codex pass on the final diff (`/review-cycle`; Tier 2, at most two correction rounds). A substantive
   fix reruns the gate.
6. `/human-review`. Joe reads `mass/constant`'s block, the parsing rule (§1) and the consumption test (§4).

## 11. `/wrap` (before Joe's merge)

- PLAN.md gets the PR 14 line under M7.
- STATE.md is written as main reads it after the merge: the PR 14 thread is closed, and PR 15 still waits on
  PR 12.
- Before the merge, `git fetch`. If PR 7 has merged, merge `origin/main` in and rewrite STATE.md and PLAN.md
  for both PRs merged. Then rerun what the merge touched:
  - Markdown only: docs-check and `git diff --check`;
  - otherwise, the gate.
  
  A plugin added by PR 7 needs a block, and the generator refuses a plugin without one.

## Files

- `docs/plans/m7-pr14-config-key-reference.md` (this plan)
- `scripts/config_keys.py` (new) and `scripts/docs_check.py`
- `tests/python/test_config_keys.py` (new) and `tests/python/config_keys_fixtures/*.yaml` (six, new)
- the leading comment of each of the 22 plugins' first `SOURCES` file, for example
  `plugins/physical/mass/constant/constant.cpp`, `plugins/sim/transport/loopback/loopback.cpp` and
  `aircraft/f16/physical/aero/aero.cpp`
- `docs/reference/config-keys.md` (generated)
- `docs/architecture.md` §13 and §18 step 1 (D1); `docs/process/validation.md` and `docs/README.md` (D3)
- `justfile` (the docs-check comment only)
- the gate summary
- `PLAN.md` and `STATE.md` at `/wrap`
