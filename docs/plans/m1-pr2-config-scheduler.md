# M1 PR 2 — Config, SignalRegistry, VirtualClock, Scheduler, Vehicle, Simulation

Recovered from `~/.claude/plans/m1-pr-2-as-shimmering-kahn.md` on 2026-09-30. Approval: the file records none; the work merged as #16. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

M1 PR 1 landed `core/util/frames.hpp` and `core/kernel/integrator.hpp`: the math a rigid body needs, with
nothing to drive it. PR 2 builds the kernel scaffolding that turns those into a *run*: a YAML config that
names plugins by path, a signal registry the config-named plugins publish into, an integer-frame clock, an
ordered scheduler, and a `Simulation` holding a list of one `Vehicle` (ADR-0011). `Model::init(const
Config&, SignalRegistry&)` has been declared since M0 against two forward-declared classes; this PR makes
them real, which is what lets PR 3 (EarthModel, gravity, mass, EOM) and PR 4 (HealthMonitor) be about
physics rather than plumbing.

Acceptance (PLAN.md M1 PR 2): a config naming the dummy plugin runs 1000 frames with `t == frame * dt`
exact and the plugin's signal visible as `v0.dummy.*`; ctest gains `unit`/`physics` labels and the Stop
hook runs `ctest -L unit`; `scripts/tokens.py` prints one row per session **and model** and `--since
<date>` works.

## One schema decision worth Joe's eye

`docs/architecture.md` §13's config names models through *typed* slots (`physical.aero`,
`hardware.propulsion`). None of those interfaces exist yet — after PR 2 the only interface is `Model`. So
PR 2 introduces one generic, ordered list and nothing else:

```yaml
# tests/configs/dummy_1000.yaml
sim:
  dt: 0.001
  frames: 1000
vehicle:
  models:
    - { plugin: tests/plugins/dummy, divisor: 1 }
```

`vehicle:` is singular, matching §13's single-vehicle document (ADR-0011 puts the list in `Simulation`, and
says v2's second vehicle comes with a superseding ADR). `vehicle.models` is the generic slot the typed §13
keys join in PR 3 onward. It is cheap to unwind: one test config plus one builder function. This is the
only thing in the PR that architecture.md does not already pin, and it is not on the workflow.md §Review
unpinned list (EOM, kinematics, integrator interface, health monitor), so no `/codex:rescue` round.

Deliberately **not** invented here: no `environment:` key and no top-level model list. `Simulation::run`
carries the §5 frame order as a comment with only the "vehicle models" stage populated; environment, EOM,
integrator, post-step and logger land in the PRs that create them.

## Core

New headers under the §4 layout, each with a `.cpp` in `core/src/`:

- **`core/include/astra/config/config.hpp`** — `Config`, pimpl over yaml-cpp so **no plugin ever compiles
  against a yaml-cpp header**. A `Config` is `shared_ptr<const Document>` + its own dotted path; every
  accessor re-resolves from the root (init-time only, and it makes the error path trivially correct).
  - `static Config from_file(const std::filesystem::path&)`, `from_string(std::string_view, origin)`.
  - Typed getters, each with a defaulted overload: `number`, `integer`, `boolean`, `text`; `at(key)` /
    `at(index)` for subtrees, `has`, `size`, `is_map`/`is_sequence`/`is_scalar`, `path()`.
  - Missing key, wrong type, and a malformed document all throw a new `ConfigError : Error` whose
    `what()` and `config_path()` name the dotted path (`vehicle.models.0.plugin`) and the origin file.
    This retires the "when Config exists (a later milestone)" note at the top of `core/include/astra/errors.hpp`.
  - **Unknown keys are errors** (architecture.md §15): the `Document` records every scalar leaf path at
    load and every path a getter reads; `unknown_keys()` returns the difference.
  - yaml-cpp gotcha to respect: `operator[]` on a *non-const* `YAML::Node` inserts a zombie node. All
    traversal goes through `const YAML::Node&`.
- **`core/include/astra/signals/signal_registry.hpp`** — `SignalRegistry` (ADR-0002, §9). `add(name,
  const double*, unit, divisor = 1)` at init only; duplicate name, null pointer, empty name, or `add`
  after `freeze()` throw. `find(name) -> const double*` resolves at init; the hot path holds pointers and
  never touches a string. `signals()` returns a `std::span<const Signal>` (valid after `freeze()`).
  A nested RAII `PrefixScope` is how `v0.` is applied: `Vehicle` opens one around each model's `init`, so a
  model registering `"dummy.steps"` lands as `v0.dummy.steps` — exactly ADR-0011's `v0.aero.CL`.
- **`core/include/astra/kernel/virtual_clock.hpp`** — header-only. Holds `std::uint64_t frame_` and `dt_`;
  `t()` is `static_cast<double>(frame_) * dt_`, computed, never accumulated (ADR-0010). `context()` returns
  the `StepContext`. Constructor rejects a non-finite or non-positive `dt`, matching `Integrator`'s
  constructor, and defaults to `kDefaultDt` from `integrator.hpp` rather than re-deciding 1 ms.
- **`core/include/astra/kernel/scheduler.hpp`** — the `(Model*, divisor)` list of §5, built at init
  (`add(Model&, divisor)`, divisor ≥ 1 or throw), `step(const StepContext&) const noexcept` calling each
  entry with `frame % divisor == 0`. `noexcept` is deliberate: a model that throws on the per-frame path
  terminates loudly instead of unwinding through the loop (§14). `entries()` exposes the order (it is data,
  and PR 4/M5 log it).
- **`core/include/astra/kernel/vehicle.hpp`** — index, name (`v0`), its own `StateVector`, its
  `std::vector<LoadedPlugin>` (the `unique_ptr<Model>` pointee address is stable across vector growth, so
  the scheduler's `Model*` stay valid), and its `Scheduler`. `init` opens the `PrefixScope` and calls each
  model's `init` with **that model's own config subtree** (the map containing its `plugin:` key), which is
  how §13's `{ plugin: …, seed: 42 }` form works. No EOM, no state signals yet — PR 3.
- **`core/include/astra/kernel/simulation.hpp`** — `static Simulation from_config(const Config&,
  std::filesystem::path plugin_dir)` reads `sim.dt` / `sim.frames`, builds the clock, loads each
  `vehicle.models` entry through the existing `PluginLoader` (`core/include/astra/plugin/loader.hpp`,
  unchanged — it already handles both the dlopen and static-plugins builds), then calls
  `config.unknown_keys()` and throws `ConfigError` naming the first stray path. A small helper accepts
  both §13 spellings: a plain scalar (`earth: plugins/physical/earth/flat`) and a map with `plugin:`.
  `run(frames)` / `run()` loop with no allocation and no exceptions; `clock()`, `registry()`, `vehicles()`
  (a span, ADR-0011 — nothing assumes length one).

Build: `find_package(yaml-cpp 0.8 REQUIRED)` at the top level beside Eigen3 (already installed by
`scripts/setup-ubuntu.sh`); `target_link_libraries(astra_core PRIVATE yaml-cpp::yaml-cpp)` — private,
because the pimpl keeps it out of every public header.

## Tests and the dummy plugin

- `tests/plugins/dummy/dummy.cpp` gains two registered signals and a step body: `dummy.steps` (count,
  unit `"1"`) and `dummy.t` (last step time, unit `"s"`). Its descriptor and `name()` are untouched, so the
  M0 loader tests (`tests/unit/plugin/{loader,load_all}_test.cpp`, neither of which calls `init`) still pass.
- `tests/configs/dummy_1000.yaml` as above, reached through a new `ASTRA_TEST_CONFIG_DIR` compile
  definition alongside the existing `ASTRA_TEST_PLUGIN_DIR`.
- New suites in `astra_unit_tests` (suite names give `ctest -R`: `ConfigTest`, `SignalRegistryTest`,
  `VirtualClockTest`, `SchedulerTest`, `SimulationTest`):
  - `tests/unit/config/config_test.cpp` — typed getters and defaults, nested maps and sequences, a missing
    key and a wrong type each naming the full dotted path, a malformed document, `unknown_keys()`.
  - `tests/unit/signals/signal_registry_test.cpp` — add/find/units/divisor, duplicate and post-freeze
    rejection, nested `PrefixScope` producing `v0.dummy.steps`, pointer identity after `freeze()`.
  - `tests/unit/kernel/clock_test.cpp` — `t() == frame * dt` bit-exact across 1000 frames, `advance`,
    `reset`, bad `dt` rejected.
  - `tests/unit/kernel/scheduler_test.cpp` — divisors 1/2/10 fire the right counts in the registered
    order; divisor 0 rejected at init.
  - `tests/unit/kernel/simulation_test.cpp` — **the acceptance**: load `tests/configs/dummy_1000.yaml`,
    run 1000 frames, assert `clock.t()` equals `static_cast<double>(frame) * dt` exactly at every frame,
    assert `registry.find("v0.dummy.steps")` is non-null and reads 1000, and `v0.dummy.t` equals
    `999 * dt` exactly. Plus the negative cases: an unknown key and an unknown plugin path both throw with
    the offending name in `what()`.

## ctest labels and the Stop hook

- `unit` on every fast test: `gtest_discover_tests(… PROPERTIES LABELS unit)` for the four test
  executables, `set_tests_properties(… PROPERTIES LABELS unit)` for the `add_test` cases in
  `tests/CMakeLists.txt` (the ICD C11 compiles and the dlopen symbol check). The `physics` label is
  established as the convention here and first applied in PR 3, when `tests/physics/` exists.
- `justfile`: every `ctest` invocation gains `--no-tests=error` (a label selector that matches nothing must
  fail, not pass silently), and a new recipe `test-unit preset="asan": (build preset)` running
  `ctest --preset {{preset}} -L unit --parallel {{jobs}} --no-tests=error`. `just check` is unchanged and
  still runs every test under every preset.
- `.claude/hooks/stop-check.sh` runs `just test-unit` instead of `just test`; its three stub justfiles in
  `.claude/hooks/tests/run.sh` and the comments in `stop-check.cases` rename their recipe to match, so
  `just hooks-test` still proves the hook fires.
- `AGENTS.md` mentions `test-unit` **inside the existing `just test` bullet** — `AGENTS.md` + `CLAUDE.md`
  are exactly 100 lines and workflow.md caps them there, so this cannot be a new line.

## scripts/tokens.py

Two changes, both required by the acceptance:

1. The shebang is mangled in `main` — `#!/ usr / bin / env python3`. Fix it.
2. Rows are keyed per session today, with the models seen joined into one cell, which cannot show the
   Opus/Sonnet split workflow.md §Model and effort asks for under `opusplan`. Re-key the aggregation to
   `(session_id, model)` so each session contributes one row per model; the `TOTAL` row is unchanged.
   `tests/python/test_tokens_script.py` updates with it (it is not an oracle file — the guard covers
   `tests/golden/**`, tolerance files, and `docs/decisions/**` only).

## Verification

Build and test output goes to a file in the scratchpad; only the tail is read (CLAUDE.md).

1. `ctest --preset asan -R 'Config|Signal|Clock|Scheduler|Simulation' --no-tests=error` — the new suites,
   including the 1000-frame acceptance.
2. `ctest --preset asan -L unit --no-tests=error` and `ctest --preset static-plugins -L unit
   --no-tests=error` — the label selects a non-empty set in both the dlopen and the no-dlopen build.
3. `just hooks-test` — the Stop hook's cases still pass against the renamed recipe.
4. `.venv/bin/python scripts/tokens.py` and `.venv/bin/python scripts/tokens.py --since 2026-09-14` —
   one row per session and model, and `--since` returning rows rather than an empty table, against this
   session's own transcripts.
5. **`just check` exits 0 (`check: OK` in the log).** That is the gate and the definition of done.
6. Codex review from Bash per CLAUDE.md: `node "$(find ~/.claude/plugins -name codex-companion.mjs | head
   -1)" review --base origin/main --wait`. The diff touches `core/kernel/`, so also `adversarial-review
   --base origin/main --wait` with focus naming ADR-0002 (units as registry metadata), ADR-0010 (`t` derived,
   never accumulated), ADR-0011 (`v0.` namespace, no assumption of one vehicle) and §14 (no allocation,
   no exceptions, no `std::string` on the per-frame path). Findings that hold are fixed in this session.
7. `/wrap`.

**Known risk, with a decided response.** The `asan` preset links a non-instrumented apt `libyaml-cpp`
while yaml-cpp's header-inline code compiles into our instrumented TU; mixed libstdc++ container
annotations are the classic source of a false `container-overflow`. It may well not fire (we only hold
`YAML::Node`, whose internals are `shared_ptr`/`std::map`, not vectors we index). If it does: set
`detect_container_overflow=0` via an `ENVIRONMENT` ctest property on the config-touching tests only,
append a `docs/lessons.md` entry, and say so in the PR — not a wider sanitizer weakening, and not a
from-source yaml-cpp (apt-only toolchain).
