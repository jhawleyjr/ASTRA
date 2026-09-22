# Implementation plan: M3 PR 5 — nanobind bindings (Simulation, Config, SignalRegistry, write seam, derivative)

Spec/brief: this file (Tier 3; a small change may keep spec and plan together per
`docs/templates/implementation-plan.md`, but PR 5 is not small — the spec fields below are kept
distinct from the plan fields for that reason).
Risk: **Tier 3** — touches `core/kernel/` (`Simulation`, `Vehicle`) and health behavior
(`docs/process/workflow.md`'s risk-tier table names both explicitly); PLAN.md already assigns PR 5
Tier 3.
Status: **approved**
Plan approval: **Approved by Joe, 2026-09-21**, after Codex plan critique (9 findings, all folded
in) and Joe's own second review round (4 required changes, 3 minor folds, 2 decisions — see "Plan
critique and decisions" below), with one further addition on approval: R2a's lossless round-trip
acceptance row, backed by an empirical probe against the real yaml-cpp 0.8.0 (see "What `Config`
from a Python dict needs" above).
Intent/approval: Joe's session instruction, 2026-09-21 (start PR 5 in a new worktree; write this
plan via `/plan-feature`; Codex plan critique before showing Joe; map every PLAN.md PR 5 acceptance
clause to a named test/command; inventory `python/`'s fixed-13 assumptions first).
Architecture/ADRs: `docs/architecture.md` §6/§7/§8/§12/§13; ADR-0021 (state generalization, **R4**
binds this PR directly); ADR-0017 (write seam, PR 2); ADR-0020 (marker interfaces, PR 3d); ADR-0009
(plugin loader, editable-only Python).
System context: `astra._core` is the nanobind module over `libastra_core`
(`python/bindings/_core.cpp`, `python/CMakeLists.txt`). It currently binds only the plugin
loader/descriptor types, `StepContext`, `Model`, and the fixed-13 state-vector helpers
(`docs/architecture.md` §12's own "Status" line). PR 5 is the first PR to bind `Simulation` itself,
making the nanobind module able to build, drive, and query a real simulation from Python for the
first time — every downstream M3 PR (6 trim, 7 linearize, 8 cruise regression) is built on exactly
what this PR exposes.

## Repository findings

### The existing `python/` surface, in full (nothing omitted)

```
python/astra/__init__.py       -- re-exports astra._core.VERSION as astra.__version__
python/astra/py.typed
python/bindings/_core.cpp      -- the entire nanobind module, 149 lines
python/CMakeLists.txt          -- editable-only build (ADR-0009), stub generation
```

`python/bindings/_core.cpp` binds, today:

- `core_api_version()`, `built_with()`, `default_plugin_dir()` — version/build-identity functions.
- **Fixed-13 state helpers**: `STATE_SIZE` (=13), `POSITION`/`VELOCITY`/`QUATERNION`/`ANGULAR_RATE`
  (offsets) and their `_SIZE` counterparts, `zero_state()`, `identity_state()`. These construct
  `astra::StateVector` (`Eigen::Vector<double, 13>`, `core/include/astra/kernel/state.hpp`) — the
  **rigid-body block only**. `core/include/astra/kernel/state.hpp`'s `kStateSize = 13` was
  deliberately **not** changed by ADR-0021/PR 4a: it still names exactly the rigid-body block's own
  fixed size, distinct from a vehicle's runtime-sized *combined* state buffer
  (`13 + sum(model.state_count())`, `core/include/astra/kernel/state_buffer.hpp`). `tests/python/
  test_state.py` pins this: `test_layout_partitions_thirteen_states_as_adr_0003_says` asserts
  `STATE_SIZE == 13` — **still true and still correct today**, because it is testing the rigid-body
  block specifically, not "the whole state." Nothing in the existing binding surface currently
  claims or assumes that 13 is a *vehicle's* whole state size — there is no `Simulation`/`Vehicle`
  binding yet for that assumption to live in. The risk PR 5 must design against is introducing one
  by accident: e.g. hard-coding a length-13 numpy array for `derivative()`'s input/output shape, or
  reusing `zero_state()`/`STATE_SIZE` as if they described a vehicle's combined state. Concretely,
  PR 5 must **not** reuse `STATE_SIZE`/`zero_state()`/`identity_state()` for anything
  `Simulation`/`Vehicle`-shaped; those stay exactly what they are (rigid-body-block helpers,
  unaffected by this PR), and every new binding that returns or accepts "the state" must size itself
  from the live `Vehicle`'s own combined-buffer length, never from the literal 13.
- Init-time error types (`Error`, `PluginError` and its four subclasses) — reusable as-is; PR 5 adds
  no new exception hierarchy, only new instances raising the existing `Error`/`ConfigError` (not yet
  bound — see below) types where the C++ side already throws them.
- `StepContext`, `Model` (name/reset/step only — no trampoline, no `init`/`state_count`/etc. bound;
  matches PLAN.md's own settled "D2: minimal scope, no trampolines in M3" for this PR), the plugin
  loader stack (`PluginDescriptor`, `LoadedPlugin`, `PluginLoader`).

**Not bound today, all in PR 5's scope per PLAN.md's own line:** `Simulation`, `Config` (only
`from_file`/`from_string`, both YAML-text; no dict path), `SignalRegistry`, `InputRegistry`'s write
path, `HealthMonitor`, `ConfigError`/`Error`'s field accessors beyond the base message.

### C++ surfaces read this session (paths/symbols, not restated in full — see the ADR-0021/vehicle.hpp
### excerpts already in this session's transcript)

- `core/include/astra/kernel/state.hpp` — `kStateSize = 13`, rigid-body block only (unchanged by
  ADR-0021).
- `core/include/astra/kernel/state_buffer.hpp` — `StateBuffer`: runtime-sized, non-copyable,
  move-constructible only, no `resize()`/`push_back()` in its public interface (R2's enforcement
  mechanism).
- `core/include/astra/kernel/vehicle_state.hpp` — `VehicleState`: `Eigen::Map<const StateVector>`
  over a raw pointer (ADR-0021's retype); still exactly 13 named accessors (`p_n()`/`v_b()`/
  `q_nb()`/`omega_b()`), a *view* over the rigid-body block's own slice of a larger buffer.
- `core/include/astra/interfaces/dynamics_model.hpp` — `DynamicsModel`: `state_count()`,
  `initial_state(double*)`, `derivative(t, const double*, double*)`, `post_step(double*)`.
- `core/include/astra/kernel/vehicle.hpp` / `core/src/kernel/vehicle.cpp` — `Vehicle`: two-phase
  init (`load_vehicle_models`/`load_physics_and_mass`/`seed_initial_state`/
  `publish_rigid_body_signals`/`bind_and_publish_state_owners`), `combined_`/`stage_`/
  `pre_step_scratch_` (`std::optional<StateBuffer>`, sized once at init to `13 + sum(state_count())`),
  `state_owners_` (`StateOwnerEntry{model, plugin_path, config, name, offset, count}`, in load
  order), `integrate()`'s own private `derivative` lambda (the exact logic PR 5 must reuse, not
  reimplement — see "Design decision 1" below), `state()`/`sync_stage_buffer()`, `inputs()`
  (`InputRegistry&`), already-public test-only accessors `combined_state_for_testing()`/
  `stage_state_for_testing()` (`std::span<const double>` — direct precedent for the "small C++ test
  seam" PLAN.md's PR 5 line calls for; see "Mapping and sequence" R9 below).
- `core/include/astra/kernel/simulation.hpp` — `Simulation`: `from_config(Config, plugin_dir)`,
  `for_testing(...)` (explicitly **not** bound per PLAN.md's own PR 5 line — a decision to record,
  not an omission), `step()`/`run(n)`/`run()`, `clock()`/`registry()`/`health()`/`vehicles()`
  (const and non-const overloads)/`frames()`. No `derivative()` method exists yet — this PR adds it.
- `core/include/astra/config/config.hpp` — `Config`: pimpl over a yaml-cpp `Document`, deliberately
  exposing no yaml-cpp type in its own header ("so no plugin ever needs the yaml-cpp headers, only
  this one" — the header's own comment). Only `from_file`/`from_string` construct one today; no
  dict/programmatic-tree constructor exists.
- `core/include/astra/signals/signal_registry.hpp` — `SignalRegistry`: `add()` (init-time, not
  bound — models call it, Python never does), `find()`, `freeze()`/`frozen()`, `signals()`
  (`std::span<const Signal>`, `Signal{name, value (const double*), unit, divisor}`) — this last one
  is what "SignalRegistry read views" binds.
- `core/include/astra/signals/input_registry.hpp` — `InputRegistry`: `write(name, value)` (stages a
  *pending* value only — has no effect on the canonical value read by a bound `const double*` until
  the next `latch()`), `find()` (returns an `InputHandle`, C++-only concept, not needed in Python),
  `latch()` (per-vehicle-per-frame commit, called by `Simulation::step()`), `reset()`. Owned one
  per-`Vehicle` (`vehicle.inputs()`), not per-`Simulation` — see "Design decision 3" below.
- `core/include/astra/kernel/integrator.hpp` — `Integrator::step()` is a template over a
  `StateDerivative`-concept functor (`f(t, y, dy) noexcept`); `k1_`/`k2_`/`k3_`/`k4_`/`trial_` are
  private `mutable std::vector<double>` scratch, sized once at construction, with **no existing
  accessor** to read `k1_` back out — the acceptance test's "small C++ test seam" (PLAN.md's own
  phrase) needs one; see R9 below.
- `core/include/astra/kernel/health_monitor.hpp` — `HealthMonitor::init(Config, SignalRegistry&)`
  registers its own signals/counters into the registry it is handed; `check_wrench`/`check_state`/
  `report_out_of_domain`/`report_non_finite_environment` are called from inside `Vehicle::integrate`'s
  derivative lambda and mutate `HealthMonitor`'s own counters/signals as a side effect of every call,
  independent of the returned `HaltReason`. This side effect is the crux of Design decision 1 below.
- `core/include/astra/kernel/environment_sample.hpp` — `EnvironmentSample{wind_n, atmosphere}`;
  `wind_n` is **frame-latched** (copied verbatim from `Vehicle::environment_`, itself only updated by
  `Simulation::step()`'s environment stage calling `WindModel::begin_frame`/`wind_n`); `atmosphere`
  is recomputed **fresh every RK4 sub-stage** from the stage's own trial altitude
  (`Vehicle::prepare_stage_environment`), never latched. This asymmetry is exactly what PLAN.md's PR
  5 line means by "fresh atmosphere at the given state, latched wind."

### Two documented mismatches found checking the spec's assumptions against the repository

1. **`docs/architecture.md` §8 vs. ADR-0021 R4 is not actually stale prose — ADR-0017 already
   settles it as PR 5's own choice.** §8 reads *"The derivative function is exposed to Python as
   `Simulation.derivative(state, inputs)`"*; ADR-0021 R4 and PLAN.md's own PR 5 line both write
   `Simulation::derivative(state)` (one argument). Initially read as a stale doc sentence to correct
   — **wrong**, caught rereading ADR-0017 itself (`docs/decisions/0017-control-input-write-seam.md`,
   the paragraph beginning "PR 5's `Simulation::derivative()` evaluates whatever is currently latched
   in each `WrenchSource`'s own canonical storage"): ADR-0017 explicitly states *"`docs/architecture.md`
   §8's already-documented Python-facing `derivative(state, inputs)` signature can still be honored
   at the Python boundary while its internal implementation does `write()` for each declared input,
   then one `latch()`, before evaluating at `state` — PR 5's own choice whether to keep that explicit
   parameter or require its own caller to `write()`+`latch()` directly."* Both signatures are
   ADR-sanctioned; §8 is not wrong, it documents one of two options ADR-0017 leaves open. **Resolved
   in this plan**: keep `derivative(state)` single-argument (matches ADR-0021 R4's own wording, and
   keeps `write_input`/`derivative` as two clearly separate, independently testable primitives rather
   than one call doing both) — §8 gets a one-line clarifying edit ("or, equivalently, write each
   input then call `derivative(state)`"), not a contradiction fix. ADR-0017's own hard requirement
   applies regardless of which shape is chosen: *"Whichever path PR 5 takes, it must check `latch()`'s
   returned name and raise rather than continue"* — folded into Design decision 3 below.
2. **Test path convention.** PLAN.md's own PR 6/PR 7 lines write `pytest python/tests/test_trim.py`
   / `python/tests/test_linearize.py`. The actual repository convention — `pyproject.toml`'s
   `[tool.pytest.ini_options] testpaths = ["tests/python"]`, and every existing Python test living
   under `tests/python/` (`test_docs_check.py`, `test_loader.py`, `test_new_aircraft.py`,
   `test_package.py`, `test_state.py`) — is `tests/python/`, not `python/tests/`. This is PR 6/7's
   line, not PR 5's own, but PR 5 is the first PR to add *any* new Python test file for this milestone
   arc, so it is the place to settle it: **this plan puts every new PR 5 Python test under
   `tests/python/`**, matching the existing convention and `pyproject.toml`'s `testpaths`, and treats
   PLAN.md's PR 6/7 wording as a typo to fix when those PRs are planned, not a second, competing
   convention to introduce now. Flagged, not silently different from what PLAN.md's PR 5 line itself
   says (PR 5's own line names no test path).

### What `Config` from a Python dict needs, concretely — **settled by Joe: option (a), no core change**

`docs/architecture.md` §13 already states the intent: *"One `Config` tree, loaded from YAML
(yaml-cpp, apt) or built from a Python dict."* `Config`'s existing header deliberately hides yaml-cpp
behind its own pimpl (`Document`, defined only in `config.cpp`) "so no plugin ever needs the yaml-cpp
headers." Two implementation shapes were considered in the drafting round:

- **(a)** Let `python/bindings/_core.cpp` include yaml-cpp directly, build a `YAML::Node` from a
  Python dict/list/scalar, then reuse the **already-existing, unmodified** `Config::from_string()`
  by emitting that `Node` to a YAML text string (`YAML::Emitter`/`YAML::Dump`) — zero new `core/`
  API, zero `config.hpp` changes.
- **(b)** A new small `core/`-owned recursive value type plus a new `Config::from_value()` factory.

**Joe's decision: (a).** Reasoning recorded here for the record: yaml-cpp is already an
already-apt-installed, already-project-wide dependency (`AGENTS.md`'s own toolchain line;
`core/CMakeLists.txt:59`'s `target_link_libraries(astra_core PRIVATE yaml-cpp::yaml-cpp)`), so
linking it directly into `_core.cpp` is not a new third-party dependency requiring separate
authorization, only a new direct link edge. **Verified, not assumed**: that link is `PRIVATE` on
`astra_core`'s own CMake target, so it does **not** propagate to `python/CMakeLists.txt`'s `_core`
nanobind module (which only links `astra::core`, confirmed by direct read of both CMake files) —
`python/CMakeLists.txt` needs its own explicit `find_package(yaml-cpp REQUIRED)` +
`target_link_libraries(_core PRIVATE yaml-cpp::yaml-cpp)`, a small, real, one-time addition, not
something already free. A core-owned `Value` type is deferred until a second, genuinely non-YAML
producer actually needs one — not invented speculatively for this single caller. This also means
`Config`'s own "no plugin ever needs the yaml-cpp headers, only this one [`config.cpp`]" property in
its header comment becomes, precisely, "only `config.cpp` and `python/bindings/_core.cpp`" —
`_core.cpp` is core-adjacent trusted infrastructure, not a plugin crossing the versioned ABI
boundary that comment is actually about, so this does not weaken what that comment protects against.

Concretely: `Config.from_dict(d)`'s nanobind implementation recursively builds a `YAML::Node` from
`d` (dict → map node, list → sequence node, `str`/`bool`/`int`/`float` → scalar node, raising
`TypeError` nanobind-side for anything else), emits it to a string, and calls the existing
`Config::from_string(text, origin)` — the identical code path `Config.from_string` itself already
uses when bound directly. No `Config`/`config.cpp` diff at all for R2.

**Round-trip fidelity — empirically verified against the system's actual yaml-cpp, not assumed**
(Joe's plan-review instruction: stop and report before falling back to option (b) if this can't be
made exact). A standalone probe (`YAML::Node` assigned a typed C++ value → `YAML::Emitter` with
**default** settings → `YAML::Load` the emitted text back → `.as<T>()`, matching `Config::text`/
`number`/`integer`/`boolean`'s own real implementation exactly — confirmed by reading `config.cpp`'s
`read_scalar`/`scalar_as`, both `node.as<T>()`, not a hand-rolled parser) was compiled with
`clang++-18` and run against `libyaml-cpp.so.0.8.0` (`0.8.0+dfsg-6build1`, `dpkg -l` confirmed — the
exact version `core/CMakeLists.txt` links), covering every case named:

| Case | Input | Result |
|---|---|---|
| Full-precision double | `9.820228349842926` (Table 73's `g0`) | Bit-exact round trip (`std::bit_cast<uint64_t>` compared), **with yaml-cpp's default emitter precision — no `YAML::Precision(17)` override needed** |
| `-0.0`, `1e-300`, `1e300` | | All bit-exact, default precision |
| `int64_t` beyond 2^53 | `(1<<60)+12345`, `INT64_MAX` | Exact |
| `bool` | `true`, `false` | Exact — emitted as literal `true`/`false`, not `1`/`0` |
| Strings that look like other types | `"1.5"`, `"true"`, `"no"`, `"~"`, `""` | All exact via `Config::text()`'s own `node.as<std::string>()` path — **`~` and `""` are auto-quoted by the default emitter; `"1.5"`/`"true"`/`"no"` are emitted bare, unquoted, and still round-trip exactly**, because `.as<std::string>()` returns a scalar node's raw text verbatim regardless of what it would resolve to under YAML's own implicit-typing rules — it is not doing YAML 1.1 type resolution at all. Forcing `YAML::DoubleQuoted` on every string also round-trips correctly, but is not required. |

**Conclusion**: option (a) achieves exact round-trip fidelity for every case Joe named, using
yaml-cpp's own default `Emitter` behavior — **no explicit precision or quoting override is needed
in the implementation**. Two real implementation requirements this probe surfaces, not previously
stated, both added to the Mapping table below: (1) the dict-walking converter must construct a
properly **typed** `YAML::Node` leaf directly from the Python value's own type (`node = double_value`,
`node = int64_value`, `node = bool_value`, `node = string_value`) — never by first stringifying a
number through Python/C++ text formatting and handing yaml-cpp a string node, which would reintroduce
exactly the precision loss this probe shows the typed path avoids. (2) the Python-value dispatch
**must check `bool` before `int`** — `bool` is a subclass of `int` in Python (`isinstance(True, int)
== True`), so a naive `isinstance(v, int)` check ahead of a `bool` check would silently convert
`True`/`False` into an integer `YAML::Node` (`1`/`0`), which the probe above shows round-trips as an
*integer*, not a *bool* — `Config::boolean()` on that key would still happen to work (`node.as<bool>()`
accepts `1`/`0`), but `type(value) is bool` identity, and any code path relying on `Config::integer()`
*rejecting* a bool-typed node, would silently do the wrong thing. Not a yaml-cpp defect — a
`_core.cpp` implementation-order requirement, checked by the new acceptance row below.

## Outcome and boundaries

PR 5 makes it possible to build, drive, and inspect an `astra::Simulation` entirely from Python:
construct a `Config` from a Python `dict` (no YAML file needed for a Python-driven run), build a
`Simulation` from it, read any published signal by name, write a control input, and evaluate the
rigid-body-plus-model-state derivative at an arbitrary state without advancing the simulation clock
— the exact primitive PR 6 (`astra.trim`, `scipy.optimize.root`) and PR 7 (`astra.linearize`,
central differences) are built on.

**Non-goals** (explicitly out of scope for PR 5, per PLAN.md's own line and D2):
- No trampolines on any `Model`-derived base class — Python cannot yet implement a model.
- `Simulation::for_testing` stays C++-only; not bound.
- No `HealthMonitor` mutation/configuration surface beyond what's needed to read a halt reason back
  (read-only: whether the real `sim.step()`/`sim.run()` halted, and why).
- No `astra.trim`/`astra.linearize` themselves — those are PR 6/7.
- No transport/HAL/logging bindings (M4/M5 scope).

**Invariants preserved:**
- The per-frame path (`Vehicle::integrate`, `Integrator::step`) gains no new allocation, exception,
  or behavior change — PR 5 only adds new *callers* of existing logic (an init-time/analysis-time
  Python entry point, never invoked from inside `step()`/`run()`).
- `derivative(state)`'s returned value is bit-identical to what the real k1 RK4 sub-stage would
  compute for the same state — "not a second implementation" (PLAN.md's own words) is the acceptance
  bar, not merely "produces a plausible-looking answer."
- SI units, no unit suffixes (ADR-0002); Hamilton scalar-first quaternion (ADR-0001) — Python sees
  the same layout C++ does, nothing translated at the boundary.

**Assumptions this PR is allowed to make** (from PLAN.md, ADR-0021, prior PRs, not re-litigated
here): PR 2 (write seam), PR 3/3d (marker-interface `WrenchSource`), and PR 4a (ADR-0021 state
generalization) are merged and unchanged; `from_config` hardcodes `IntegratorKind::kRk4` (PLAN.md's
own R9 note — no Euler-comparison path exists without new config surface, so it is not attempted
here); v1 builds exactly one `Vehicle` (ADR-0011 notwithstanding, `from_config`'s own current
behavior) — `write_input`/`derivative` bind against that one vehicle, not a vehicle index, matching
PLAN.md's own literal `sim.write_input(name, value)` signature.

## Contract and failure behavior

### `astra.Config`

- `Config.from_dict(d: dict, origin: str = "<python dict>") -> Config` — new. Recursively converts
  `d` (values: `dict`, `list`, `str`, `bool`, `int`, `float`) into a `YAML::Node` (nanobind-side,
  `_core.cpp` linking yaml-cpp directly — Joe's settled option (a), "What `Config` from a Python
  dict needs" above), emits it to YAML text, and calls the existing, unmodified
  `Config::from_string(text, origin)`. A non-scalar leaf that isn't one of the five supported Python
  types raises `TypeError` before any `YAML::Node` is built (nanobind-level, no half-built `Config`).
  `Config.from_file`/`Config.from_string` also newly bound (currently C++-only), since `from_dict`
  alone would leave the two pre-existing YAML paths still unreachable from Python for no reason —
  `docs/architecture.md` §13 documents both as first-class.
- Existing getters (`has`, `at`, `number`, `integer`, `boolean`, `text`, `unknown_keys`, `path`,
  `origin`) bound read-only — needed so a Python caller building a config from a dict can round-trip
  it for debugging, and so `ConfigError`'s own message-construction path (`Vehicle::init`'s
  `spec.config.origin()`/`.path()`) is exercisable/inspectable from a raised Python exception.

### `astra.Simulation`

- `Simulation.from_config(config: Config, plugin_dir: str | os.PathLike[str]) -> Simulation` —
  static, mirrors the C++ signature exactly. Raises the already-bound `PluginError` subclasses or a
  newly-bound `ConfigError` (not bound today — added this PR, since `from_config` is the first
  Python-reachable call that can throw one).
- `sim.step() -> HaltReason`, `sim.run(frames: int) -> HaltReason`, `sim.run() -> HaltReason` —
  direct passthroughs. `HaltReason` bound as a self-contained value type per **Design decision 7**
  (its `source` field copied into an owned Python `str` at the boundary, never a raw `string_view`):
  a truthy/falsy `__bool__`, `check`, `source: str`, `frame`, `vehicle_index`.
- `sim.clock`, `sim.registry`, `sim.health`, `sim.frames` — read-only properties over the existing
  const accessors, every one using `nb::rv_policy::reference_internal` per **Design decision 6**.
  `sim.vehicles` — read-only tuple of a new, minimal `Vehicle` binding (name, index, `state` as a
  read-only numpy view over the new `Vehicle::combined_state()` — **Design decision 5** — and
  `state_owner_slices` — see below), each element also `reference_internal`-tied back to `sim`. The
  **non-const** `vehicles()` overload (test-only, per its own C++ doc comment) is **not** bound —
  binding it would let Python corrupt a running simulation's state outside any checked path, the
  "ease of misuse" scrutiny `docs/process/workflow.md`'s human-review section calls out for public
  interfaces.
  **`vehicle.state` is a *live* view** (Joe's plan review, minor fold iii — stated explicitly, not
  left implicit): the returned `nb::ndarray` aliases `Vehicle::combined_state()`'s own backing
  storage directly, so its contents change under `sim.step()`/`sim.run()` exactly like reading
  `Vehicle::state()` from C++ would — it is a window onto live storage, not a snapshot. This is the
  deliberate opposite of `derivative()`'s own return value (below), which is always a fresh,
  independent copy, precisely because a probe's trial state must never be confused with the
  vehicle's real, accepted one (Design decision 1e's own restore guarantee depends on this
  distinction staying real, not just documented).
- `sim.write_input(name: str, value: float) -> None` — new; binds a new C++ `Simulation::
  write_input()` (not merely a Python-side convenience — see **Design decision 3**), which calls
  `write()` then `latch()` immediately on `vehicles_[0].inputs()` (v1's single vehicle; see
  "Assumptions" above) and raises `Error` if `latch()` reports a fault.
- `sim.derivative(state: numpy.ndarray) -> numpy.ndarray` — new; the PR's namesake requirement. See
  **Design decisions 1/1b/1c/1d/1e and 2** for its exact semantics, all resolved after the Codex
  critique. `state` must be a 1-D, C-contiguous, `float64` array of length equal to
  `sim.vehicles[0].combined_state`'s own length (`Vehicle::combined_state().size()`, the new
  accessor from **Design decision 5** — never the fixed `STATE_SIZE`/13); a mismatched length raises
  `ValueError` before any C++ call. Internally: `rigid_body_->begin_frame()` runs first (1d), then
  the shared derivative method runs against the probe `HealthMonitor` (1/1b), restoring `stage_` on
  every exit path including a raised fault (1e/1c), with wind read verbatim from `environment_` and
  no `WindModel` call at all (2). Returns a freshly-allocated array of the same length (never a view
  into internal RK4 scratch, which is reused by every subsequent call and must not be exposed as if
  stable).
- **State layout discovery** (`derivative()`'s "named slices," ADR-0021 R4): no registry-name
  parsing (rejected by the critique — Design decision 5). The rigid-body block's fixed layout is
  already bound (`POSITION`/`VELOCITY`/`QUATERNION`/`ANGULAR_RATE` + `_SIZE` constants, valid for
  any vehicle's first 13 doubles); `vehicle.state_owner_slices` (new) exposes
  `Vehicle::state_owner_slices()` as a read-only sequence of `(name: str, offset: int, count: int)`
  for every state-owning model beyond the rigid body, in combined-buffer order. Together these fully
  and authoritatively describe `derivative()`'s input/output layout with no string convention to
  reverse-engineer.

### `astra.SignalRegistry`

- `registry.signals` (read-only sequence of `(name: str, value: float, unit: str, divisor: int)` —
  copied out, not a live view; `Signal.value` is a `const double*` into model-owned storage whose
  lifetime this binding does not want to reason about beyond "read once, copy out") and
  `registry.find(name: str) -> float | None`. Both bound — **Design decision 4**.

### `astra.InputRegistry` / write seam

Not bound as its own type — `sim.write_input(name, value)` is the only surface PLAN.md's PR 5 line
names, and no other `InputRegistry` method (`find`, `names`, `latch`, `reset`) has a Python caller
yet (PR 6/7 don't need them either: `astra.trim` only ever calls `write_input`/`derivative` in a
loop). Exposing less than the full C++ type here is deliberate scope discipline, not an oversight.

### Failure behavior

Every C++ exception type reachable from a bound call must already have (or gain, this PR) a
registered `nb::exception` translator, matching the existing pattern for `PluginError`'s subclasses.
`ConfigError` is the one new one this PR needs (thrown by `Config`'s getters, `Simulation::
from_config`, and — new this PR — `Config::from_dict`'s conversion path on an unsupported Python
value type, which nanobind should reject before it ever reaches C++, so the raised type there is
`TypeError`, not `ConfigError`, for a cleaner Python-side error surface). `derivative()`'s length
mismatch is `ValueError` (Python convention for a bad array shape, not a re-raised C++ `Error`, since
this check happens nanobind-side before any C++ call). Two new raise paths, both required by the
Codex critique's findings: `write_input()` raises `Error` when `latch()` reports the just-written
value itself was non-finite (Design decision 3, ADR-0017's own explicit requirement); `derivative()`
raises `Error` when the probe evaluation's own `HaltReason` is truthy (out-of-domain, non-finite
wrench/environment — Design decision 1c) rather than returning a numeric result computed at a state
that was never validly evaluated.

## Design decisions

**Revised after Codex's plan critique** (recorded in full under "Plan critique and decisions"
below) — this section states the corrected designs directly; the critique record shows what each
one replaced and why.

**Design decision 1 — isolating a probe call's side effects, without a shadow `SignalRegistry`.**
`Vehicle::integrate()`'s per-stage `derivative` lambda (`core/src/kernel/vehicle.cpp`) is not a pure
function: on stage 0 it writes `*published_wrench_` (the live `"<vehicle>.wrench_total.*"` signal),
and every stage it calls `HealthMonitor::check_wrench`/`check_state`/`report_out_of_domain`/
`report_non_finite_environment`, mutating `HealthMonitor`'s own counters/signals as a side effect.
Unmodified reuse from `Simulation::derivative()`'s probe path would corrupt `wrench_total` for any
real consumer and pollute/potentially halt the real `HealthMonitor` from probe-only events.

The critique's finding 6 corrected the originally-proposed fix (a lazily-constructed shadow
`HealthMonitor` + `SignalRegistry` pair): `HealthMonitor` holds no `SignalRegistry` reference at all
(`core/include/astra/kernel/health_monitor.hpp`'s private section is exactly four `HealthAction`
enums and five `double` counters — confirmed by direct read; `SignalRegistry` only ever holds
pointers *into* a `HealthMonitor`'s own counter fields, never the reverse), so no shadow registry is
needed at all — a plain **copy-construction** of a second `HealthMonitor` value carries its action
policy (`nan_action_`/`quaternion_action_`/`domain_action_`/`input_action_`) forward with no
registry involvement whatsoever. Resolved design: `Vehicle` lazily copy-constructs a
`probe_health_ = *the real HealthMonitor` (via a small internal accessor `Vehicle`/`Simulation`
already has access to) on first `derivative()` call, reused thereafter — same policy as the real one
(**required** — see finding 6b below, not merely convenient), isolated counters (the copy's counters
diverge from the real one's from the moment of copy onward; the real one's stay frozen). The shared
derivative method also needs its own scratch `Wrench` destination for the probe path, since
`published_wrench_` is written unconditionally today and must not be, for a probe call.

**Design decision 1b (new, from finding 6b) — the probe's `HealthMonitor` must match the real one's
policy, not just avoid touching its counters.** The original claim "the shared method's arithmetic
never depends on which `HealthMonitor` it reports through" was **wrong**, caught by the critique:
`Vehicle::prepare_stage_environment` calls `health.domain_enabled()` to decide whether to sample
atmosphere at all or skip the wrench stage entirely (`core/src/kernel/vehicle.cpp`) — a
default-policy probe `HealthMonitor` could take a different code branch than the real k1 stage would
for the identical state, breaking the bit-identity requirement (R9) outright, not just leaking side
signals. The copy-construction fix above (copying the *real* `HealthMonitor`'s policy fields, not
default-constructing a fresh one) resolves this directly, since the copy always matches the real
policy until the real one's policy itself changes (which nothing does after init — `sim.health` is
read once, at `from_config` time).

**Design decision 1c (new, from finding 6c) — a probe fault must raise, never return a
plausible-looking wrong derivative.** An invalid-altitude (or otherwise out-of-domain/non-finite)
probe state can make the shared method's environment/domain checks fire, producing a zero-wrench
(or otherwise degraded) derivative with a truthy `HaltReason` the original plan never routed
anywhere. Resolved: `Simulation::derivative()` inspects the probe `HealthMonitor`'s/shared method's
returned `HaltReason` and, if truthy, raises (a C++ `Error`, translated to a Python exception) rather
than returning a numeric result — matching the same "loud failure over silent wrong answer"
principle ADR-0017 already states for `write_input` (finding 8/Design decision 3 below): a numerical
method (trim's root-find, linearize's central differences) whose soundness depends on evaluating
exactly the requested point must never receive a value that looks like a real derivative at a state
that was never actually, validly evaluated.

**Design decision 1d (new, from finding 5) — mass/inertia caching (`begin_frame()`) must run before
any derivative evaluation, not only inside `Vehicle::integrate()`.** `Vehicle::integrate()` calls
`rigid_body_->begin_frame()` unconditionally, once per call, **immediately before**
`integrator_->step(...)` — outside the per-stage lambda entirely (`core/src/kernel/vehicle.cpp`,
confirmed by direct read of `integrate()`'s body). `begin_frame()` is what caches `MassProps`
(mass/inverse-inertia) from the vehicle's `MassPropertiesModel`; before it has ever run,
`RigidBody6DofDynamics` holds zero mass and zero inertia by construction
(`core/include/astra/interfaces/mass_properties_model.hpp`'s `MassProps{mass=0.0, inertia_b=Zero()}`
default, confirmed by direct read) — a real division-by-zero risk on the very first `derivative()`
call of a `Simulation` that has never stepped, exactly finding 5's point. Resolved: `Simulation::
derivative()` calls `rigid_body_->begin_frame()` immediately before invoking the shared derivative
method, on **every** call, mirroring exactly where and how often the real per-frame path already
calls it — mass is constant in v1 (ADR-0003), so this is idempotent and cheap, not merely "good
enough": no special-cased "first call only" caching logic is needed or proposed, since matching the
real call site's own placement is both simpler and provably correct.

**Design decision 1e (new, from finding 4) — a probe must restore the stage buffer before
returning, or it leaves published per-model state signals contaminated.** The shared method must
copy the trial state `y` into `Vehicle::stage_` before any `wrench_b()`/`integrate_state()` call, per
ADR-0021's own design (`bind_state()`/`resolve_peers()`-resolved pointers read live from `stage_`).
`Vehicle::integrate()` restores the invariant "`stage_` equals `combined_` outside any derivative
call" unconditionally on every exit path (halt or success — `core/src/kernel/vehicle.cpp`'s own
`stage_->copy_from(*combined_)` calls at both return points). A probe call that skips this restore
would leave `stage_` — and therefore every `"<vehicle>.<model>.state.<i>"` signal a state-owning
model publishes — showing the last-probed **trial** state, not the real, accepted `combined_` state,
until the next real frame happens to overwrite it. Resolved: `Simulation::derivative()`'s call
through the shared method ends with the identical `stage_->copy_from(*combined_)` restore
`Vehicle::integrate()` already performs, on every exit path including the new raise-on-fault path
(Design decision 1c) — the restore must run even when the probe evaluation itself fails.

**Design decision 1f (added at Joe's plan-review request) — the exact time argument, and precisely
what of `Vehicle::integrate()`'s pre-lambda sequence the probe reproduces versus deliberately
skips.** This was previously left implicit; R9's bit-identity claim depends on this reasoning being
written down, not assumed.

**`t`**: `Simulation::derivative()` builds its `StepContext` exactly as `Simulation::step()` does —
`const StepContext ctx = clock_.context();` (`core/src/kernel/simulation.cpp:563`, confirmed by
direct read) — i.e. `VirtualClock::context()`'s own `StepContext{.t = t(), .dt = dt_, .frame =
frame_}` (`core/include/astra/kernel/virtual_clock.hpp`, confirmed by direct read), off the
`Simulation`'s **current, unadvanced** clock state. A probe never calls `clock_.advance()`, so
repeated `derivative()` calls at a never-stepped `Simulation` all use `t = 0, frame = 0`; after `N`
real `step()` calls, they use whatever `t`/`frame` the clock currently holds. This matters because
`RigidBody6DofDynamics::derivative()` and the Earth/gravity models it calls are **not** functions of
`t` in v1 (no explicit time-dependence anywhere in the EOM — `t` only ever reaches `derivative()`
as an unused-by-physics parameter, present for the `StateDerivative` concept's own signature and for
whatever a future time-varying model might read), so this choice is inert for every model shipped
today, but is stated as a real, checked choice rather than an unstated one, since a future
time-varying `Model::integrate_state()` would make it load-bearing.

**Whether physics must be configured at all**: `Vehicle::integrate()`'s own first line is an early
return when `!rigid_body_.has_value()` (no `"physical"` section configured). `Simulation::
derivative()` raises `Error` under the identical condition, before attempting anything — a state
derivative is meaningless for a vehicle with no dynamics model at all, and the combined buffer for
such a vehicle is 13 zeroed, never-integrated doubles (`Vehicle::init()`'s own comment: the
rigid-body block is always allocated, physics configured or not) that `derivative()` must not
pretend to differentiate. Not previously stated; added here.

**Reproduced** (the shared method's own body, called once from the probe path exactly as it would
be called once for a real frame's k1 stage):
- `stage_env = environment_` (wind: **Design decision 2, revised below** — reproduced, but now with
  an explicit raise instead of a silent read, when wind is configured and never latched).
- `prepare_stage_environment(vs_stage, stage_env, ctx, health, wrench_fault)` — fresh atmosphere
  from the probed state's own altitude (R6), unmodified.
- `std::copy_n(y, stage_->size(), stage_->data())` — the sub-stage-exact `bind_state()`/
  `SignalRegistry` visibility ADR-0021 requires; restored afterward per Design decision 1e.
- `accumulate_wrench(vs_stage, stage_env, ctx, health, wrench_fault)`, `rigid_body_->
  set_applied_wrench(total)`, `rigid_body_->derivative(t, y, dy)`, every state owner's
  `integrate_state(t, vs_stage, stage_env, dy + owner.offset)` — the actual arithmetic; unmodified.
- `rigid_body_->begin_frame()` — Design decision 1d, reproduced on every call.

**Deliberately omitted**, each with why its absence cannot change k1's own bits for the probed `y`:
- **Frame-0 pre-step domain check** (`if (ctx.frame == 0 && health.domain_enabled()) { check_domain
  (combined_->data(), ctx, health); }`, `core/src/kernel/vehicle.cpp:413`, confirmed by direct
  read). This checks the **accepted** state (`combined_->data()`), a precondition on whether a real
  frame may begin at all — it is never read by the derivative lambda's own body (which only ever
  reads the `y` it is called with), so its presence or absence cannot change one bit of what k1
  computes for a given `y`. It also has no well-defined meaning for a probe in the first place: a
  probe is not "frame 0 of a run," so the condition itself doesn't transfer.
- **`pre_step_scratch_->copy_from(*combined_)`**. Pure bookkeeping for rolling `combined_` back if
  the full RK4 step later faults; the derivative lambda's body never reads `pre_step_scratch_` at
  all, at any stage, for any reason.
- **`Simulation::step()`'s frame-start atmosphere pre-sample** (`sample.atmosphere =
  atmosphere_->at(altitude_m)`, `core/src/kernel/simulation.cpp`'s `step()` body, confirmed by
  direct read, feeding `vehicle.set_environment(sample)` before `vehicle.integrate()` runs). This
  value is dead by the time any wrench is evaluated: `prepare_stage_environment` unconditionally
  overwrites `stage_env.atmosphere` with a fresh sample from **that stage's own** altitude whenever
  `atmosphere_ != nullptr` and the domain check passes (`Vehicle::prepare_stage_environment`'s own
  body) — true for k1 in a real frame exactly as it is true for a probe. Whether or not a frame-start
  pre-sample ever ran, k1's own atmosphere term is always this fresh, per-stage value, never the
  frame-start one.

**Design decision 2 — wind is never (re)sampled by the shared derivative method at all; it only
ever reads `Vehicle::environment_` verbatim, exactly like the real k1 stage.** The original design
had this backwards: `Vehicle::integrate()`'s per-stage lambda never calls into any `WindModel`
itself — it only copies the **already frame-latched** `Vehicle::environment_` (`stage_env =
environment_;`, `core/src/kernel/vehicle.cpp`), which `Simulation::step()`'s own, separate
environment stage populates once per major frame, strictly *before* `Vehicle::integrate()` runs at
all. "Latched wind" in the real design means exactly this: wind is a frame-scoped input the
environment stage prepares once; no RK4 sub-stage (and, by the "not a second implementation"
requirement, no standalone probe evaluation either) ever resamples it. The originally-proposed fix —
have the shared method call `wind_->wind_n()`/`begin_frame()` directly against each probe's own
state, gated by a new `shareable_across_vehicles()`-based rejection of "unsafe" wind models — is
**wrong on two counts**, per finding 7: (a) it does something the real k1 stage itself never does,
which is itself a violation of "not a second implementation," not merely a side-effect risk; (b)
`shareable_across_vehicles()` does not mean what the proposal needed it to mean — `plugins/physical/
wind/table` (confirmed by direct read, `table.cpp:129`) returns `shareable_across_vehicles() ==
true` while its own `begin_frame()` still mutates and republishes `wind_n_`/`altitude_` every call
(`table.cpp`'s own `registry.add("wind.north", &wind_n_(0), ...)` at init, then overwritten every
`begin_frame()`), so the proposed guard would have *admitted* `wind/table` while still corrupting its
published signals via repeated probe-driven `begin_frame()` calls at arbitrary, non-monotonic
altitudes — the exact class of bug Design decision 1 exists to prevent, reintroduced through wind
specifically.

**Resolved**: the shared method reads `Vehicle::environment_` unmodified, with no new `WindModel`
call, no new predicate, and no `kCoreApiVersion` bump.

**Revised at Joe's plan review — the never-latched-wind gap raises, it is not merely disclosed.**
The first draft of this decision left "uniform wind, analysis-only, never stepped" as a documented
gap (`derivative()` silently reading still air instead of the configured wind). Joe's own call,
overriding that draft: this is exactly the "plausible-looking wrong answer" class Design decisions
1c and 3 already refuse elsewhere in this same plan — PLAN.md's own PR 5 line explicitly names
"uniform wind" as a supported trim/linearize condition, not merely tolerated, so silently
substituting still air for it is a correctness bug, not a disclosed limitation. **Resolved**:
`Vehicle` gains a new private `bool environment_latched_ = false;` set unconditionally (every call,
harmlessly idempotent) inside the existing `set_environment()` — the one method only `Simulation::
step()`'s environment stage ever calls — and, per Joe's own instruction, **cleared by nothing**
(not `Vehicle::reset()`, not anything else): once a real frame has latched a real environment
sample, `derivative()` may always trust `environment_` again, permanently, for that `Simulation`'s
lifetime. `Simulation::derivative()`'s probe path checks, before doing anything else (before even
`begin_frame()`): if `wind_configured_ && !environment_latched_`, raise `Error` naming the
configured wind plugin, rather than silently evaluating still air. This makes the contract exactly
what PLAN.md's own line promises: **no wind configured** → always correct (still air is the actual
configured behavior, not a substitute). **Wind configured, never stepped** → raises, loudly, instead
of guessing. **Wind configured, stepped at least once** → reads the real latched sample, correctly,
for every subsequent probe regardless of how many more real frames run or don't. No public accessor
needed — `environment_latched_` is read only from inside `Vehicle`'s own new derivative-entry-point
method, which already has private access.

**No `sample_environment()`-style primitive in PR 5**, per Joe's explicit instruction — this raise
is the entire fix; PR 6 decides separately whether latching wind without a full `step()` (to support
trim/linearize starting cold on a wind-configured config) is worth its own small primitive, once it
actually needs one. `astra.trim`'s own fallback today, with no such primitive, is straightforward:
call `sim.step()` (or `sim.run(1)`) once before any `derivative()` call on a wind-configured
`Simulation` — one real frame's own state advance, at the caller's own initial condition, is a cost
PR 6 can accept or design around, not something PR 5 needs to solve.

**Design decision 3 — `write_input` commits immediately (`write()` then `latch()`), and must raise
on a fault, per ADR-0017's own explicit requirement.** `InputRegistry::write()` only stages a
pending value; nothing observes it until the next `latch()`, which only `Simulation::step()` calls
today. A `Simulation` driven only through `derivative()` would never see a written value take effect
without an explicit commit. Resolved (confirmed correct by rereading ADR-0017 directly, not merely
proposed): `Simulation::write_input()` (new C++ method, exercised by the same C++ test seam
`derivative()`'s own tests use) calls `write()` then `latch()` immediately. ADR-0017's own text
(`docs/decisions/0017-control-input-write-seam.md`, confirmed by direct read) states this exact
requirement in its own PR 5 paragraph: *"it must check `latch()`'s returned name and raise rather
than continue: a non-finite write leaves the previous, last-good value latched, so `derivative()`
would evaluate at the wrong point and return a plausible-looking but wrong residual."* Resolved:
`write_input` checks `latch()`'s returned name; if non-empty, raises `Error` naming the offending
input. The critique's own worry about *other*, unrelated never-written inputs is moot for every
Python-reachable `Simulation` — confirmed by direct read of `InputRegistry::latch()`
(`core/src/signals/input_registry.cpp`): a non-finite pending value is never committed (canonical
stays at its last good value; only the offending name is reported), and `Simulation::from_config`
(the only construction path bound to Python — `for_testing` is not) already requires every declared
input to have a finite value via `vehicle.initial_controls` before `InputRegistry::freeze()` ever
runs (ADR-0017's own "Sequencing at init" paragraph) — so a Python-built `Simulation` never has a
never-written input in the first place; the only way `latch()`'s returned name is ever non-empty
after a `write_input` call is if *that specific* just-written value was itself non-finite.

**Design decision 4 — `SignalRegistry` read surface: copy-out list plus `find()`-by-name.** Bind
both: `registry.signals` (copied-out `(name, value, unit, divisor)` tuples — needed to discover the
state-owner naming layout, though see Design decision 5 below for why `derivative()`'s own named
slices should not depend on parsing these names) and `registry.find(name) -> float | None`. Neither
exposes a live pointer to Python. Unchanged from the original proposal; the critique raised no
objection to this one specifically.

**Design decision 5 (new, from finding 3) — `derivative()`'s named slices need an authoritative
accessor, not registry-name parsing.** The original plan proposed deriving the rigid-body-plus-
state-owner slice layout by grouping `SignalRegistry` names by their existing dotted-name
convention. The critique correctly rejects this: (a) `Vehicle::state()` returns
`Eigen::Map<const StateVector>` — hardcoded to exactly 13 doubles (`core/include/astra/kernel/
vehicle.hpp:196`, confirmed by direct read) — so it cannot serve as the N-length accessor
`derivative()`'s input/output actually needs regardless of naming; a genuinely new, non-test-only
accessor is required either way. (b) `Signal` (`core/include/astra/signals/signal_registry.hpp`)
carries no offset/count/state-membership marker, and arbitrary non-state plugin signals share the
same flat namespace and registration order as state signals — grouping by string convention alone
cannot reliably reconstruct which registry entries are state slices, at what buffer offset, or in
what order, especially once a vehicle has no physics configured (rigid-body signals are never
published at all in that case, `core/src/kernel/vehicle.cpp`'s own guard on `rigid_body_.has_value()`
before `publish_rigid_body_signals`) or once a plugin's own signals happen to contain the substring
`".state."` in an unrelated name.

**Resolved**: add one new, authoritative, non-test-only `Vehicle` accessor this PR must ship, plus
promote an existing one, per Joe's own minor-fold instruction (no duplicate accessor returning the
same span): (1) **rename/promote** `combined_state_for_testing()` to `Vehicle::combined_state() ->
std::span<const double>` — one accessor, not two; its doc comment drops "test-only introspection"
and states its real contract (the full N-length combined buffer, read-only; live — see the
"vehicle.state is a live view" note under `astra.Simulation`'s contract below). The one existing call
site (`tests/unit/kernel/vehicle_state_coupling_test.cpp`, per PR 4a's own plan) is updated to the
new name as part of this PR's diff — a mechanical rename, not a behavior change.
`stage_state_for_testing()` is unaffected (stays test-only; production code never needs to observe
`stage_` directly, only `combined_`). (2) new `Vehicle::state_owner_slices() -> std::span<const
NamedStateSlice>` — a public accessor for the already-existing, already-correctly-shaped private
member `state_owner_health_slices_` (`name`, `offset`, `count` per state-owning model, in combined-
buffer order — built once at init from `state_owners_`, `core/src/kernel/vehicle.cpp`'s
`bind_and_publish_state_owners`). Combined with the rigid-body block's own **fixed, compile-time-
known** layout — offset 0, count `kStateSize` (=13), already bound today as `POSITION`/`VELOCITY`/
`QUATERNION`/`ANGULAR_RATE` and their `_SIZE` constants, always valid for the first 13 doubles of
*any* vehicle's combined buffer regardless of what state-owning models it has (`Vehicle::init()`'s
own comment: "the rigid-body block always occupies the combined buffer's first kStateSize doubles,
whether or not physics is configured") — Python can now construct the full, authoritative named-
slice layout with no string parsing at all: the rigid-body block from the existing bound constants,
plus every state-owning model's own slice from the new `state_owner_slices()` accessor. This directly
satisfies ADR-0021 R4's "named slices" wording with real metadata, not an inferred one.

**Design decision 6 (new, from finding 1) — every Python object that borrows into `Simulation`-owned
storage must keep the owning `Simulation` alive, including one level removed.** The original plan
named `nb::rv_policy::reference_internal` in passing for a couple of accessors but did not specify
the complete chain the critique correctly demands: `sim.clock`, `sim.registry`, `sim.health`, each
element of `sim.vehicles`, and any numpy view over a `Vehicle`'s state must all keep the owning
`Simulation` Python object alive for as long as the borrowed wrapper (or, for a vehicle extracted
from `sim.vehicles` and kept alone, or a numpy array extracted from that vehicle and kept alone,
*their* owner) is reachable. Resolved: every such binding uses `nb::rv_policy::reference_internal`
(nanobind's mechanism for "this return value's lifetime is tied to `self`") consistently through the
whole chain — `Simulation` → `Vehicle` (element of `vehicles`) → numpy state view — not only at the
first hop; nanobind's own `reference_internal` composes correctly when applied at every level, but
a test proving the actual GC-survival property end to end is required, not merely a code-review
claim that the policy was applied (see R1c below). Read-only properties (`sim.registry`, a vehicle's
state view) must also produce genuinely non-writable numpy arrays (`writeable = false` on the
returned `nb::ndarray`), since the underlying storage is model-owned and a writable view would let
Python corrupt a running simulation's state through a "read-only" accessor — the same "ease of
misuse" scrutiny already applied to *not* binding the non-const `vehicles()` overload.

**Design decision 7 (new, from finding 2) — `HaltReason`'s `source` field must never cross the
Python boundary as a dangling `string_view`.** `HaltReason` (`core/include/astra/kernel/
health_monitor.hpp:67`, confirmed by direct read) is `{HealthCheck check; std::string_view source;
std::uint64_t frame; std::uint32_t vehicle_index;}` — `source` is a view into a plugin-path or
signal-name string owned by `Vehicle`/`Simulation` storage (e.g. `Vehicle::earth_plugin_path_`,
`wrench_total_name_`), not into the `HaltReason` value itself. A Python binding that returns
`HaltReason` by value (as `sim.step()`/`sim.run()` must) and lets the caller hold onto it after the
owning `Simulation` is destroyed would leave `.source` reading freed memory the moment a getter
converts it to a Python `str`, or earlier. Resolved: the nanobind wrapper for `HaltReason` copies
`source` into an owned Python `str` **at the point `step()`/`run()`/`derivative()`'s raise-path
returns it**, not lazily on first access — the bound type holds a real `std::string`/Python `str`,
never a `string_view`, so the returned Python object is fully self-contained and safe to hold past
the `Simulation`'s own lifetime. This is a small, mechanical binding-layer fix (construct the
Python-facing struct with an owned copy at the C++/Python boundary crossing), not a C++ API change —
`astra::HaltReason` itself is unchanged.

## Mapping and sequence

One Claude builder (default; no genuinely independent, stably-contracted sub-pieces to split — the
Config-from-dict, Simulation/SignalRegistry bindings, write-seam binding, and derivative() all share
one nanobind translation unit and one design-decision set above).

| Requirement (PLAN.md PR 5 line) | Change / owned paths | Dependencies / contract | Acceptance test + command |
|---|---|---|---|
| R1: bind `Simulation` (`from_config`, `step`/`run`) | `python/bindings/_core.cpp`: new `nb::class_<astra::Simulation>`; new `nb::class_<astra::HaltReason>` with an owned-`str` `source` (Design decision 7) | `core/include/astra/kernel/simulation.hpp` (unchanged) | `test_simulation.py::test_from_config_builds_and_steps_a_real_cruise_config`; `pytest tests/python/test_simulation.py -k from_config_builds_and_steps` |
| R1a: `sim.clock`/`registry`/`health`/`vehicles`/`frames`, every borrow keeps `sim` alive (Design decision 6) | `python/bindings/_core.cpp`: `reference_internal` on every property/element, at every hop (`Simulation`→`Vehicle`→numpy view) | Design decision 6 | `test_simulation.py::test_a_vehicle_kept_alone_after_its_simulation_is_deleted_still_reads` (build a `Vehicle`/numpy-view reference, `del sim`, `gc.collect()`, then read); `pytest tests/python/test_simulation.py -k kept_alone_after_its_simulation_is_deleted` |
| R1b: read-only numpy views are genuinely non-writable | `python/bindings/_core.cpp`: `writeable=false` on every `nb::ndarray` return | Design decision 6 | `test_simulation.py::test_vehicle_state_view_is_not_writable` (`with pytest.raises(...): view[0] = 1.0`); `pytest tests/python/test_simulation.py -k state_view_is_not_writable` |
| R1c: `Simulation::for_testing` **not** bound (decision recorded, not a gap) | none (absence is the deliverable) | — | `test_simulation.py::test_for_testing_is_not_exposed_to_python` (`assert not hasattr(_core.Simulation, "for_testing")`); `pytest tests/python/test_simulation.py -k for_testing_is_not_exposed` |
| R1d: `HaltReason.source` survives past the real `Simulation`'s own lifetime | Design decision 7's owned-copy binding | — | `test_simulation.py::test_halt_reason_source_is_readable_after_the_simulation_is_deleted` |
| R2: `Config` from a Python dict | `python/bindings/_core.cpp`: `Config.from_dict` (dict → `YAML::Node` → emitted text → the existing `Config::from_string`), plus newly-bound `from_file`/`from_string`/getters; `python/CMakeLists.txt`: new direct `yaml-cpp::yaml-cpp` link for the `_core` target. **No `core/`/`config.cpp` diff** (Joe's settled option (a)) | Design point "Config from dict" above; `docs/architecture.md` §13's own already-stated intent | `test_config.py::test_from_dict_builds_a_simulation_equivalent_to_the_same_yaml`; `test_config.py::test_from_dict_rejects_an_unsupported_python_value_type`; `pytest tests/python/test_config.py` |
| R2a: `Config.from_dict` round-trips scalars losslessly through the emitted YAML text (Joe's plan-review addition, empirically verified against the real yaml-cpp above) | The dict→`YAML::Node` converter constructs typed nodes directly (never via manual stringification) and dispatches `bool` before `int` | Empirical probe above (yaml-cpp 0.8.0 default emitter, no precision/quoting override needed) | `test_config.py::test_from_dict_round_trips_scalars_exactly` — a dict containing: `9.820228349842926` read back via `number()`, compared `math.isclose(..., rel_tol=0)` (bit-exact, not approximate); `-0.0`, `1e-300`, `1e300` via `number()`; an int beyond `2**53` via `integer()`; `True`/`False` via `boolean()` **and** asserting the round-tripped Python value's own `type(...) is bool` (not silently coerced to `int` by a wrong dispatch order); `"1.5"`, `"true"`, `"no"`, `"~"`, `""` via `text()`, each compared for exact string equality; `pytest tests/python/test_config.py -k round_trips_scalars_exactly` |
| R3: `SignalRegistry` read views | `python/bindings/_core.cpp`: `nb::class_<astra::SignalRegistry>` (`signals`, `find`) | Design decision 4 | `test_signal_registry.py::test_signals_lists_every_published_signal`; `test_signal_registry.py::test_find_returns_none_for_an_unknown_name`; `pytest tests/python/test_signal_registry.py` |
| R4: `sim.write_input(name, value)` commits immediately and raises on a non-finite write (PR 2's write seam, D7; ADR-0017's own explicit requirement) | new `Simulation::write_input(std::string_view, double)` (`simulation.hpp`/`.cpp`: `write()` then `latch()`, raises `Error` on fault); `python/bindings/_core.cpp` binding | Design decision 3 | `test_write_input.py::test_write_input_is_visible_to_the_very_next_derivative_call`; `test_write_input.py::test_write_input_raises_on_a_non_finite_value`; `pytest tests/python/test_write_input.py` |
| R5: `Simulation::derivative(state)` reuses, not reimplements, `Vehicle::integrate`'s k1 arithmetic | new `Vehicle::evaluate_derivative(...)` (shared private method, Design decisions 1/1b/1d/1e); `Vehicle::integrate()`'s own lambda refactored to call it; new `Simulation::derivative(std::span<const double>) -> std::vector<double>`; `python/bindings/_core.cpp` binding (numpy in/out, length-checked against `Vehicle::combined_state().size()`) | Design decisions 1, 1b, 1d, 1e; **proved by R9 below, not by a separate Python-side arithmetic test** — this row's own test only proves the binding surface (shape, exceptions), not bit-identity | `test_derivative.py::test_derivative_returns_an_array_shaped_to_the_combined_state`; `pytest tests/python/test_derivative.py -k returns_an_array_shaped` |
| R5a: a probe never leaks into the real `HealthMonitor`'s counters/signals or `wrench_total` | Design decision 1's copy-constructed `probe_health_`; probe-only scratch `Wrench` | Design decision 1 | `test_derivative.py::test_repeated_derivative_calls_do_not_change_the_real_health_counters_or_wrench_total_signal` (compare `sim.registry.find("v0.wrench_total.force_b_x")` and `sim.health`'s counters before/after a batch of `derivative()` calls at perturbed states) |
| R5b: a probe never leaves `stage_` (and therefore state-owner signals) contaminated | Design decision 1e's restore-on-every-exit | Design decision 1e | `test_derivative.py::test_a_derivative_probe_leaves_state_owner_signals_showing_the_accepted_state_not_the_trial_state` (needs an N>13 fixture — see R8 below) |
| R5c: a fault during a probe raises, not a plausible-looking wrong value | Design decision 1c | Design decision 1c | `test_derivative.py::test_derivative_raises_when_the_probe_state_is_out_of_domain` |
| R5d: mass/inertia are cached before the very first probe (no first-call division by zero) | Design decision 1d's `begin_frame()` call ordering | Design decision 1d | `test_derivative.py::test_the_first_derivative_call_on_a_never_stepped_simulation_uses_the_configured_mass_and_inertia` (a physics-configured fixture, assert the returned `dv_b` term is consistent with the real mass, not `inf`/`nan` from a zero-mass divide) |
| R6: fresh atmosphere at the given state | shared method reuses `prepare_stage_environment`'s existing per-stage-altitude sampling unmodified | `core/src/kernel/vehicle.cpp`'s existing `prepare_stage_environment`, unchanged | `test_derivative.py::test_derivative_atmosphere_tracks_the_altitude_in_the_passed_state` (two probe states differing only in altitude produce different atmosphere-dependent wrench terms) |
| R7: "latched wind" — the shared method never calls any `WindModel`; reads `Vehicle::environment_` verbatim once actually latched | Design decision 2 (revised, Joe's plan review): no new predicate, no new `WindModel` call, no `kCoreApiVersion` bump | Design decision 2 | `test_derivative.py::test_derivative_reads_the_wind_configuration_once_stepped` (a `wind/uniform` fixture: `sim.step()` once, then `derivative()` reflects the real configured `wind_n`, not zero); `pytest tests/python/test_derivative.py -k reads_the_wind_configuration_once_stepped` |
| R7a: `derivative()` raises, never silently substitutes still air, when wind is configured and never latched | new `Vehicle::environment_latched_` (set by `set_environment()`, cleared by nothing) | Design decision 2 | `test_derivative.py::test_derivative_raises_on_a_never_stepped_simulation_with_wind_configured` (`wind/uniform` fixture, never stepped, `derivative()` raises `Error`); `test_derivative.py::test_derivative_does_not_raise_once_stepped_even_if_wind_is_never_touched_again` (one `sim.step()`, then many `derivative()` calls all succeed); `pytest tests/python/test_derivative.py -k raises_on_a_never_stepped_simulation_with_wind` |
| R7b: `derivative()` never mutates a configured wind plugin's own published signals (the critique's `wind/table` counterexample) | shared method never calls any `WindModel` (Design decision 2) | Design decision 2 | `test_derivative.py::test_repeated_derivative_calls_do_not_mutate_the_configured_wind_plugins_own_published_signals` (a `wind/table` fixture, stepped once, then probed at several different altitudes; asserts `sim.registry.find("v0.wind.altitude")` is unchanged by any `derivative()` call) |
| R7c: `derivative()` raises when the vehicle has no physics configured at all (Design decision 1f, added at plan review — not previously stated) | `Simulation::derivative()`'s own precondition check, before `begin_frame()`/anything else | Design decision 1f | `test_derivative.py::test_derivative_raises_when_no_physics_is_configured` (a dummy-plugin-only fixture, no `"physical"` section) |
| R8: `derivative(state)` exposes ADR-0021 R4's named slices via authoritative metadata, not registry-name parsing | Design decision 5: renamed `Vehicle::combined_state()` (promoted from `combined_state_for_testing()`), new `Vehicle::state_owner_slices()`; `python/bindings/_core.cpp` bindings | Needs a real N>13 fixture — reuse `tests/plugins/state_owner` (already shipped by PR 4a, `docs/plans/pr4a-dynamics-model.md`'s own "The new test-only plugin" section) in a new test config | `test_derivative.py::test_state_owner_slices_reports_the_correct_offset_and_count_for_a_state_owning_model` (against a `state_owner`-configured fixture); `pytest tests/python/test_derivative.py -k state_owner_slices` |
| R9: `derivative(state)` bitwise equal to the real integrator's k1 for the same state, over the FULL combined buffer (rigid body + a state owner's own slice), and proven not to leave anything behind that changes the very next real frame — the required, same-object form (Joe's plan review) | new `Integrator::k1_for_testing() -> std::span<const double>` (test-only public accessor, precedented by `stage_state_for_testing()`); new forwarding `Vehicle::k1_for_testing()` (`integrator_` is private — `Integrator` alone is not reachable from a test without one, per the critique's own finding); new `tests/unit/kernel/simulation_derivative_test.cpp` using a `state_owner`-configured fixture (N>13), registered in `tests/unit/CMakeLists.txt`'s existing `astra_unit_tests` source list (default `gtest_discover_tests`, no `TEST_PREFIX`, matching every other file in that list) | `core/include/astra/kernel/integrator.hpp`; `core/include/astra/kernel/vehicle.hpp`; `tests/plugins/state_owner` | **Required (same-object)**: `TEST(SimulationDerivative, DerivativeThenARealFrameOnTheSameSimulationAgreeOnK1ForAStateOwningModel)` — build one `Simulation` (physics + `state_owner`, N>13) via `for_testing`; read `vehicle.combined_state()`; call `sim.derivative()` at that exact state, capture the returned array; then run one real frame via `vehicle.integrate(ctx, health)`; read `vehicle.k1_for_testing()`; assert bitwise (`std::bit_cast<std::uint64_t>`) equality, element-by-element, over all N doubles, between the captured `derivative()` array and `k1_for_testing()` — this single comparison proves both bit-identity (R9's own name) and that the probe left nothing behind that changed the subsequent real frame's own k1 (Design decision 1e, end to end). **Secondary (optional, two-object cross-check, kept for extra coverage)**: `TEST(SimulationDerivative, MatchesTheRealIntegratorsK1AcrossTwoIdenticallyConfiguredSimulations)`. `ctest --preset asan -R SimulationDerivative` (all four presets via `just check`) |
| Dependency check: PR 2/3/4a already merged | none (verification only) | — | `just check` (existing full gate stays green; no regression) |
| D2 (no trampolines in M3) — pinned to the one shape actually true today (Joe's plan review: no hedged "if reachable ... otherwise..." clause) | none (absence is the deliverable; `Model` is bound today with no `nb::init<>()` and no trampoline — confirmed by direct read of `python/bindings/_core.cpp`'s existing `nb::class_<astra::Model>` block, which binds only `name`/`reset`/`step`, no constructor) | Design point under "Non-goals" | `test_simulation.py::test_a_python_subclass_of_model_cannot_be_instantiated` (`class MyModel(_core.Model): pass`, then `with pytest.raises(TypeError): MyModel()` — no bound constructor exists for Python to reach, trampoline or not); `pytest tests/python/test_simulation.py -k subclass_of_model_cannot_be_instantiated` |
| Docstring clarification (Joe's plan review, minor fold ii): `zero_state()`/`identity_state()` must not read as describing a vehicle's combined state | one-line docstring edit each, `python/bindings/_core.cpp` (`"...shape (13,)..."` → `"...shape (13,): the rigid-body block only, not a vehicle's combined state (see Simulation.derivative)..."`) | None — pure docstring, no behavior change; `tests/python/test_state.py`'s existing assertions (`STATE_SIZE == 13`, etc.) are unaffected | `pytest tests/python/test_state.py` (existing suite stays green, proving no behavior regression from the docstring-only edit) |
| §8 clarification (not a contradiction fix — ADR-0017 already permits either signature; see "mismatch 1") | one-line edit, `docs/architecture.md` §8 | Editorial, bundled with this PR's diff | `git diff --stat docs/architecture.md` (reviewed by Joe alongside the rest, not a separate gate) |

## Validation and risks

- **Fast loop**: `./scripts/check-fast.sh` (ASan build + `just test-unit`) during implementation.
- **Full gate**: `./scripts/check-full.sh` (`just check`) once on the candidate, rerun after fixes —
  all four presets (debug/release/static-plugins/asan), `pytest`, `pyright`, `ruff`, clang-tidy.
- **New C++ test**: `tests/unit/kernel/simulation_derivative_test.cpp` (R9's bit-identity proof,
  `ctest -L unit`).
- **New Python tests**: `tests/python/test_config.py`, `test_signal_registry.py`,
  `test_write_input.py`, `test_derivative.py`, extensions to `test_simulation.py` if that file does
  not yet exist (it does not — new file). All under `tests/python/`, matching `pyproject.toml`'s
  `testpaths` (see "mismatch 2" above).
- **No allocation/no-throw regression on the per-frame path**: `Vehicle::integrate()`'s lambda is
  refactored, not rewritten — `tests/physics/no_alloc_test.cpp` (existing, from PR 4a) must stay
  green unmodified; no new allocation is introduced on that path by moving its body into a shared
  method (the shared method itself may allocate when called from `Simulation::derivative()`'s
  lazily-constructed, copy-built `probe_health_` path — explicitly outside the per-frame budget, an
  analysis-time call, not `step()`/`run()`; the copy itself is cheap — `HealthMonitor` is four enums
  and five doubles, no heap member, confirmed by direct read).
- **Implementation checkpoint, not assumed**: `HealthMonitor`'s implicit copy constructor must
  actually be usable (not `= delete`d) for Design decision 1's copy-built `probe_health_` — the
  class declares no constructors/copy/move members explicitly in the header this session read, which
  usually means the compiler-generated ones are available, but this needs confirming against the
  actual class declaration (not only the members section) at implementation time, before relying on
  it.
- **No golden/tolerance file touched** — no `ASTRA_TOUCH_GOLDENS` needed for this PR.
- **New dependency**: none at the package level — yaml-cpp is already an apt-installed,
  project-wide dependency; `_core`'s new direct `target_link_libraries(_core PRIVATE
  yaml-cpp::yaml-cpp)` (Joe's settled option (a)) is a new link edge, not a new dependency to
  authorize.
- **`kCoreApiVersion` impact**: none expected — `Vehicle`'s new private method and public accessors
  (`combined_state`, `state_owner_slices`, `k1_for_testing`), and `Simulation`'s two new public
  methods (`write_input`, `derivative`), do not change any `Model`-derived virtual interface's
  vtable; to be confirmed once the actual diff exists (every prior M2/M3 PR that touched a
  `Model`-family header needed a bump — worth an explicit check at implementation time, not assumed
  clean).
- **Compatibility**: purely additive to the Python surface; no existing binding's signature changes.
  `docs/architecture.md` §8's one-line correction is the only doc-only change outside `python/`/
  `core/kernel/`/`core/config/`/`tests/`.
- **Rollback**: fully reversible pre-any-external-consumer (no released wheel; editable-only per
  ADR-0009) — a revert of this PR's diff is a clean revert, nothing else in the tree depends on the
  new bindings yet (PR 6/7 do not exist).

## Plan critique and decisions

One Codex plan critique dispatched and completed this session (Tier 3, PLAN.md's own line:
"ownership/lifetime across the Python/C++ boundary"), per `docs/agents/review-dispatch.md`'s
plan-critique route: `codex:codex-rescue` subagent, `--fresh --wait`, explicit read-only request
(no `--write`), against `docs/plans/pr5-nanobind-bindings.md` at that point in its drafting, base
`origin/main` at `96c9459`. No files were written or edited by the critique itself.

Nine findings returned, all "Major." Every one was independently verified against the actual
repository (not accepted on the reviewer's word alone) before being folded in — the specific files/
lines checked are cited inline above, in each finding's resolution. All nine are **accepted and
folded into the design decisions above**; none rejected.

| # | Finding (short) | Verified how | Resolution |
|---|---|---|---|
| 1 | Python ownership chain for `clock`/`registry`/`health`/`vehicles`/numpy views unspecified | Read `simulation.hpp`'s own accessors; nanobind's `reference_internal` semantics | Design decision 6 — `reference_internal` at every hop, non-writable views, explicit GC-survival test (R1a/R1b) |
| 2 | `HaltReason.source` is a `std::string_view`; returning it by value after `Simulation` is destroyed dangles | Read `health_monitor.hpp:67-74` directly, confirmed the field type | Design decision 7 — copy `source` into an owned Python `str` at the boundary crossing (R1d) |
| 3 | `Vehicle::state()` is hardcoded to 13 doubles; `Signal` carries no offset/count/state-membership; registry-name grouping cannot reconstruct the combined-buffer layout | Read `vehicle.hpp:196`'s `Eigen::Map<StateVector> state()` return type; read `signal_registry.hpp`'s `Signal` fields | Design decision 5 — new `Vehicle::combined_state()` + `Vehicle::state_owner_slices()` accessors exposing real offset/count metadata, no string parsing (R8) |
| 4 | The shared derivative method writes into `stage_` (bound-pointer-visible); a probe that doesn't restore it afterward leaves state-owner signals showing the perturbed trial state | Read `vehicle.cpp`'s `stage_->copy_from(*combined_)` calls at both of `integrate()`'s own exit points | Design decision 1e — restore `stage_` on every exit path, including the new raise-on-fault path (R5b) |
| 5 | `rigid_body_->begin_frame()` (mass/inertia caching) runs outside the derivative lambda, in `integrate()` itself; a derivative-only `Simulation` that never steps never calls it, risking a zero-mass division on the first probe | Read `vehicle.cpp`'s `integrate()` body: `rigid_body_->begin_frame();` immediately before `integrator_->step(...)`, outside the lambda; read `mass_properties_model.hpp`'s `MassProps{mass=0.0, ...}` default | Design decision 1d — `Simulation::derivative()` calls `begin_frame()` every call, mirroring the real call site exactly (R5d) |
| 6 | (a) A shadow `SignalRegistry` was unnecessary complexity; (b) the original "arithmetic is independent of which `HealthMonitor`" claim was wrong (`domain_enabled()` gates whether atmosphere is even sampled); (c) an invalid-altitude probe's fault never reaches Python | Read `health_monitor.hpp`'s private section directly (four enums, five doubles, no registry pointer); read `vehicle.cpp`'s `prepare_stage_environment` calling `health.domain_enabled()` | Design decisions 1 (copy-construct, not shadow-construct), 1b (copy carries the real policy, fixing the wrong claim), 1c (raise on a truthy `HaltReason`) — R5a, R5c |
| 7 | `shareable_across_vehicles()` does not mean "safe to probe repeatedly out of frame order" — `wind/table` returns `true` while its `begin_frame()` still mutates/republishes `wind_n_`/`altitude_` every call | Read `table.cpp:129`'s override returning `true`; read `table.cpp`'s `begin_frame()` mutating `wind_n_`/`altitude_`, both registered signals | Design decision 2, rewritten entirely — the shared method never calls any `WindModel`; reads `Vehicle::environment_` verbatim, exactly like the real k1 stage (R7) |
| 8 | (a) `latch()` does not commit a non-finite pending value (misread on my part, corrected); (b) `for_testing` not being bound means the "other unwritten inputs" worry is moot for any Python-reachable `Simulation`; (c) ADR-0017 already mandates checking `latch()`'s return and raising | Read `input_registry.cpp:81-91`'s actual `latch()` body directly; read ADR-0017's own PR 5 paragraph (`docs/decisions/0017-control-input-write-seam.md`) | Design decision 3 — `write_input` = `write()` + `latch()`, raises on a fault; the "other unwritten inputs" open question removed as moot, not merely answered (R4) |
| 9 | Acceptance mapping gaps: R5 conflated with R9 (layout test doesn't prove reuse); R7 didn't prove "latched" or address `wind/table`; R8 couldn't prove real offsets; R9's test seam was unreachable (`Vehicle::integrator_` is private) and its named command didn't match this repo's GoogleTest/ctest convention; D2 (no trampolines) had no acceptance row | Read `tests/unit/CMakeLists.txt`'s `gtest_discover_tests` convention directly; read `vehicle.hpp`'s private `integrator_` member | Mapping and sequence table rewritten in full: R5 now explicitly points to R9 for the reuse proof and only claims the binding surface for itself; R5a-R5d/R7 split out with concrete tests; R8 tied to the new accessors and an N>13 `state_owner` fixture; R9 given a real `TEST(SimulationDerivative, ...)` name, a new `Vehicle::k1_for_testing()` forwarding accessor, and registration in `tests/unit/CMakeLists.txt`'s existing source list; a new D2 acceptance row added |

No finding was rejected.

Codex job identity, for later verification: thread `01a0c678-fb85-7312-9ee4-ed0c18af6b9d`, turn
`01a0c678-fd92-7160-925e-49a992379cc5` (`codex-companion.mjs task --wait`, `codex:codex-rescue`
subagent, read-only, `--fresh`, dispatched this session against this plan file at that point in its
drafting, base `origin/main` at `96c9459`).

### Joe's own plan review (second round, after the Codex critique above)

Joe reviewed the critique-revised plan directly (not through Codex) and required four further
changes plus three minor folds before approving, and made both decisions the critique round had
left open:

1. **Time argument and reproduced/omitted sequence, unstated** — added as Design decision 1f above
   (`ctx = clock_.context()`; the full enumerated list of what the probe reproduces from `Vehicle::
   integrate()`'s pre-lambda sequence and what it skips, with a one-line reason per omission, citing
   `vehicle.cpp:413` and `simulation.cpp:563` directly).
2. **Wind gap too weak** — Design decision 2 revised from "disclosed gap, deferred to PR 6" to
   **raise**: `derivative()` raises when a `WindModel` is configured and `Vehicle::environment_` has
   never been latched (a new `environment_latched_` flag, set by `set_environment()`, cleared by
   nothing). No `sample_environment()` primitive in PR 5 — Joe's own explicit instruction. New
   acceptance row: R7a below.
3. **R9 too weak for ADR-0021 R4** — (a) an N>13 fixture (`tests/plugins/state_owner`, already
   shipped) added so bit-identity is proven over a state-owner's own slice, not only the rigid-body
   13; (b) the *required* form of R9 is now the **same-`Simulation`-object** test — `derivative()`
   first, then a real `vehicle.integrate()` from the identical starting state, then compare
   `k1_for_testing()` against `derivative()`'s own earlier return — which simultaneously proves
   bit-identity **and** that the probe left nothing behind that changes the very next real frame
   (Design decision 1e, closing the loop end to end on one object, not merely asserted by
   construction). The original two-object variant is retained as a secondary, not the required row.
4. **D2's acceptance row was hedged** (`"if reachable ... otherwise ..."`), not a real acceptance
   clause. Resolved by determining what's actually true today rather than hedging: `Model` is bound
   with no constructor at all (no `nb::init<>()`, no trampoline) — a Python subclass cannot be
   instantiated through any bound path, full stop, today. The row is pinned to exactly that one
   test.

**Minor folds, applied without further discussion**: (i) `Vehicle::combined_state_for_testing()` is
renamed/promoted to `Vehicle::combined_state()` — one accessor, not a new one added beside it
(Design decision 5, revised). (ii) `zero_state()`/`identity_state()`'s bound docstrings gain an
explicit "rigid-body block, not a vehicle's combined state" clarification — new row under
"Mapping and sequence." (iii) `sim.vehicles[i].state`'s own contract now states explicitly that it
is a **live** view (backed directly by `combined_->data()`, changes under `step()`/`run()`),
contrasted with `derivative()`'s always-freshly-allocated, independent return array — both stated
explicitly under `astra.Simulation`'s contract above.

**Joe's two decisions**: Config-from-dict = **option (a)** (no `core/` change at all — dict → 
`YAML::Node` → text → the existing `Config::from_string`, not even the smaller of the two originally
proposed core-side shapes); wind gap = **raise**, per item 2 above, not deferred.

### Approved, with one addition (2026-09-21)

Joe approved this plan on condition of one further addition: R2 needed a lossless round-trip
acceptance row, since option (a) goes through emitted YAML text rather than a direct in-memory
structure — with explicit instruction to verify this empirically against the real yaml-cpp before
writing the row, and to stop and report rather than silently fall back to option (b) if any case
couldn't be made exact. Verified (see "What `Config` from a Python dict needs" above): every case
named round-trips exactly under yaml-cpp 0.8.0's default emitter, no precision/quoting override
needed. New row R2a added to "Mapping and sequence," and two real implementation requirements
(typed-node construction, not stringification; bool-before-int dispatch order) recorded alongside
it. **Plan status: approved.** Implementation begins next in this session.

## Done when

- Every row in "Mapping and sequence" above has its named test passing.
- `just check` green on all four presets, evidence retained.
- Codex plan critique dispatched and recorded above — done, all nine findings folded in, none
  rejected; Codex job identity cited (thread/turn IDs, above).
- Joe's own plan review (second round) — done, all four required changes and three minor folds
  applied, both decisions (Config-from-dict = option (a); wind gap = raise) recorded.
- Joe's explicit approval — **done, 2026-09-21**, on condition of R2a (lossless round-trip
  acceptance row), itself verified empirically before being added, not merely written on request.
