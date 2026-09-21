# Feature + implementation plan: PLAN.md M3 PR 3d — implement ADR-0020

Status: **draft**
Owner: Joe
Risk: **3** (`core/interfaces/`, `core/kernel/`, plugin-facing ABI — `docs/process/workflow.md`'s
highest tier)
Intent/approval: `docs/decisions/0020-aero-propulsion-true-interfaces.md` (accepted by Joe,
2026-09-20); full critique/revision history `docs/plans/m3-framework-realignment.md`;
`docs/architecture.md` §1/§6/§7/§18 (rev 7) already rewritten to describe the target design this
plan implements against, not redecides; `PLAN.md`'s own PR 3d bullet.
Architecture/ADRs: ADR-0020 (this plan's subject); ADR-0016 (per-RK4-substage purity, unchanged);
ADR-0017 (control-input write seam, unchanged); ADR-0018 (superseded in part — see below).
System context: `Vehicle::integrate()`'s per-stage derivative functor (`core/src/kernel/
vehicle.cpp:127-223`) already builds `stage_env` and calls every `wrench_sources_` entry's
`wrench_b(vs_stage, stage_env)` once per RK4 sub-stage, health-checking each contribution and the
total via `HealthMonitor::check_wrench`. This plan changes what `AeroModel`/`PropulsionModel`
require a concrete plugin to implement, and adds one new check to that same per-stage sequence.

## Outcome and boundaries

**Problem.** `AeroModel`/`PropulsionModel` (ADR-0018) seal `wrench_b()`/`requires_environment()` as
`final` and force every concrete plugin through a core-defined `AeroInputs`/`PropulsionInputs`
struct — the exact "core decides what any aero model may see" defect ADR-0019/0020 exist to remove
(`docs/decisions/0019-vehicle-agnostic-purpose.md`, §1's own definition of "interface"). PR 4
(notional aero/turbofan) is stopped until this lands.

**Outcome.** `AeroModel`/`PropulsionModel` become minimal marker interfaces: no `compute()`, no core
`Inputs` struct, no `final` anywhere except the one safe-default `requires_environment()` override
each already has today (now non-final). Air-data math becomes `core/include/astra/util/
air_data.hpp` free functions. A new `HealthMonitor::check_environment` restores, core-side, the
no-silent-NaN guarantee the removed `final` guard used to provide.

**Non-goals.** No change to `WrenchSource`'s own signature, to ADR-0016/0017, to PR 4's own aero/
propulsion numbers (separate, PLAN.md PR 4, depends on this), or to ADR-0021 (independent, PLAN.md
PR 4a).

**Invariants preserved.** Per-frame path: no allocation, no exceptions (§14). `wrench_b()`'s
per-RK4-substage purity contract (ADR-0016/0017, unchanged in shape; this plan's own R-below amends
its *text*, not its meaning). No-silent-NaN into the EOM (§15) — the actual subject of this plan's
new check.

**Compatibility.** `kCoreApiVersion` bump (7): `AeroModel`/`PropulsionModel`'s vtables lose
`compute()`; `requires_environment()` goes from `final` to a plain virtual override. No out-of-tree
plugin exists yet (ADR-0020's own Consequences), so this is a settling bump, not a compatibility
break for anyone.

Assumptions: `docs/architecture.md` §1/§6/§7/§18 (rev 7) and `PLAN.md`'s PR 3d bullet already state
the target design; this plan does not re-derive it, only maps it to code. ADR-0021 (model-owned
state, PR 4a) is **not** assumed available here — `Model`'s five new virtuals from ADR-0021 exist in
`docs/architecture.md` §7's own code block already (rev 7), defaulted no-ops; this plan does not
implement their bodies, only leaves them untouched. `HealthMonitor::check_environment`'s own
`sim.health` action reuses the existing `nan` key (see Q1 below), not a new one, matching the
existing precedent that `check_state`/`check_wrench` already share `nan_action_`/`nan_count_`
(`health_monitor.cpp:77-84`, `:86-118`) rather than each owning a dedicated key — only `check_input`
(ADR-0017) has its own `input` key, and that precedent is for a materially different failure class
(a write-seam value, not a NaN detection).

## Contract and failure behavior

**`AeroModel`/`PropulsionModel`** (`core/include/astra/interfaces/aero_model.hpp`,
`propulsion_model.hpp`): each becomes

```cpp
class AeroModel : public WrenchSource {
 public:
  ~AeroModel() override;  // stays the key function (dynamic_cast across the .so boundary)
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};
```

(`PropulsionModel` identical.) No `wrench_b()` override, no `compute()`. A concrete plugin overrides
`WrenchSource::wrench_b(const VehicleState&, const EnvironmentSample&) const noexcept` directly —
the existing `tests/plugins/nesc_aero`/`nan_wrench` pattern. `requires_environment()` stays callable
and overridable by a concrete plugin (non-`final`); an author who never touches it gets the safe
default above.

**`core/include/astra/util/air_data.hpp`** (new): free functions, relocated verbatim from
`aero_model.cpp`'s current arithmetic (`core/src/interfaces/aero_model.cpp:42-58`) — same formulas,
same V→0-safe `atan2` forms, no behavior change:

```cpp
[[nodiscard]] Eigen::Vector3d relative_velocity_b(const VehicleState& vs,
                                                  const EnvironmentSample& env) noexcept;
[[nodiscard]] double true_airspeed(const Eigen::Vector3d& v_rel_b) noexcept;
[[nodiscard]] double angle_of_attack(const Eigen::Vector3d& v_rel_b) noexcept;   // atan2(w, u)
[[nodiscard]] double sideslip(const Eigen::Vector3d& v_rel_b) noexcept;          // atan2(v, hypot(u,w))
[[nodiscard]] double dynamic_pressure(double rho, double v_true) noexcept;
[[nodiscard]] double mach(double v_true, double speed_of_sound) noexcept;
```

Plus `all_finite(std::initializer_list<double>)` or an overload set covering a scalar/`Vector3d`
mix, relocating `aero_model.cpp`'s existing `all_finite` helper (lines 24-29) as a named, reusable
utility — recommended for a plugin's own use, not enforced.

**`HealthMonitor::check_environment`** (new, `core/include/astra/kernel/health_monitor.hpp` +
`core/src/kernel/health_monitor.cpp`), mirroring `check_wrench`'s exact shape:

```cpp
[[nodiscard]] HaltReason check_environment(const EnvironmentSample& env, std::string_view
                                           wind_source, std::string_view atmosphere_source,
                                           const StepContext& ctx,
                                           std::uint32_t vehicle_index) noexcept;
```

Checks `env.wind_n.allFinite()` and `env.atmosphere`'s four fields for `std::isfinite`, applying the
existing `nan_action_`/`nan_count_` (Q1, resolved above) and a new `HealthCheck::kNonFiniteEnvironment`
enumerator, naming `wind_source` or `atmosphere_source` on a halt (whichever field failed first;
atmosphere checked after wind, matching `check_domain`'s own earth-then-gravity precedent for a
"whichever fails first is the one named" rule). No new `sim.health` config key. Per-frame path: no
allocation, no exceptions — identical discipline to every other `HealthMonitor` method.

**Call site** (`Vehicle::integrate()`'s derivative functor, `vehicle.cpp:152-194`): immediately after
`stage_env` is built (after the `atmosphere_ != nullptr` branch, line ~159) and strictly before the
`wrench_sources_` loop begins, call `health.check_environment(stage_env, wind_plugin_path_,
atmosphere_plugin_path_, ctx, index_)`; on a halt, set `wrench_fault` the same first-fault-wins way
`check_wrench` already does (do not short-circuit the rest of that stage's evaluation, matching the
existing uniform-evaluation rule `vehicle.cpp:171-178`'s own comment states for exactly this
reason).

**New `Vehicle` members**: `std::string wind_plugin_path_;` and `std::string
atmosphere_plugin_path_;`, alongside the existing `earth_plugin_path_`/`gravity_plugin_path_`
(`vehicle.hpp:231-232`), set from `PhysicsSpec` in `Vehicle::init()` the same way those two already
are (`vehicle.cpp:70-72`). Empty string when unconfigured (matching `EnvironmentSample`'s own "absent
== zero/all-NaN" default posture — `check_environment` only checks a field that is actually
populated per `EnvironmentAvailability`, so an empty source name is never actually used to name a
halt).

**`Vehicle::PhysicsSpec`** (`vehicle.hpp:61-68`) gains two new fields: `std::string
atmosphere_plugin_path;` and `std::string wind_plugin_path;`, alongside the existing `earth_plugin_path`/
`gravity_plugin_path`.

**`Simulation::from_config`** (`simulation.cpp:330-396`): both source strings already exist as local
variables at the exact point `PhysicsSpec` is constructed — `atmosphere_plugin_path` (line 378,
already also stored as `sim.atmosphere_plugin_path_`) and `wind_plugin_path` (line 339, currently
**only** a local, never retained past this function). Thread both into the `PhysicsSpec{...}`
aggregate-init at lines 392-394. No new `Simulation` member strictly required for `wind_plugin_path`
(it is consumed immediately by the `PhysicsSpec` construction, the same lifetime `earth_spec.plugin_path`/
`gravity_spec.plugin_path` already have there) — but see R6/Q2 below for `Simulation::for_testing`'s
own parallel construction path, which needs the identical two fields threaded or explicitly defaulted.

**Failure behavior**, restated precisely: a non-finite `wind_n`/`atmosphere` field now halts (default
action) via `check_environment`, naming the wind/atmosphere plugin, **before** any configured
`wrench_b()` runs that stage — closing `environment_sample.hpp`'s own two already-disclosed gaps.
This is strictly earlier and more precisely attributed than today's behavior (where such a value
either produces a wrong-looking-finite output via R-001's own mechanism, or is caught later by
`check_wrench` naming the *consuming* plugin, not the environment source).

## Repository findings

- `core/src/interfaces/aero_model.cpp`/`propulsion_model.cpp` hold the `final wrench_b()`
  implementations to be deleted; their arithmetic (lines 42-58 in `aero_model.cpp`) is the exact
  source for `air_data.hpp`'s relocated functions — a move, not a rewrite.
- `core/include/astra/interfaces/aero_inputs.hpp`/`propulsion_inputs.hpp` are deleted entirely (no
  core-defined `Inputs` struct survives).
- `tests/unit/wrench/aero_model_test.cpp`/`propulsion_model_test.cpp` (read in full) test `wrench_b()`'s
  own `AeroInputs` assembly and isfinite guard end to end — **every test in both files becomes
  obsolete** under this plan, since that assembly/guard logic moves to (a) `air_data.hpp`'s own unit
  tests (the atan2-form/V→0/signed-zero cases, R2-R4/R4-signed-zero in the current file, become
  `air_data_test.cpp` cases against the relocated free functions directly) and (b) a new
  `health_monitor_test.cpp` case for `check_environment` (R5/the infinite-speed-of-sound case,
  now an `EnvironmentSample`-level check, not a per-plugin one). Both existing files are deleted;
  `docs/plans/m3-framework-realignment.md`'s "R1 not `Cmadot`" reasoning does not apply here — this
  is a genuine test-suite restructuring, not a golden change (no file under `tests/golden/` or
  `docs/decisions/` is touched by this PR).
- `tests/unit/kernel/aero_propulsion_seam_test.cpp` (read in full): drives `aero_stub`/`propulsion_stub`
  entirely through `Simulation::from_config` + `registry().find()`, never calls `compute()` directly
  — its first two tests (write-seam drives the output; both specializations sum into the combined
  wrench) need **no structural change** once R5 rebuilds the stubs, only recompiling against the new
  interface. **Its third test does need a real, behavior-driven rewrite, not just a recompile**:
  `NonFiniteAtmosphereHaltsNamingTheConsumingPluginEndToEnd` (lines 100-112) configures
  `tests/plugins/nan_atmosphere` (an `AtmosphereModel` returning an all-NaN `AtmoState` at every
  altitude — confirmed by reading `tests/plugins/nan_atmosphere/nan_atmosphere.cpp`) and asserts
  `halt.check == HealthCheck::kNonFiniteWrench`, `halt.source == "tests/plugins/aero_stub"` — this is
  **exactly** the scenario `check_environment` (R3/R4) now intercepts *earlier*, before any
  `wrench_b()` runs that stage. Under this plan the halt becomes `HealthCheck::kNonFiniteEnvironment`
  naming `"tests/plugins/nan_atmosphere"` instead — this test's own assertions must be updated to the
  new, corrected attribution, not merely recompiled (folded into R4's acceptance below, not a
  separate requirement).
- **A second, more consequential instance of the same test-behavior change**, found by reading
  `tests/unit/kernel/simulation_halt_test.cpp:159-183`:
  `SimulationHalt.NanAtmosphereInRangeHaltsNamingTheWrenchConsumerNotTheAtmospherePlugin` is the
  **named regression test for the exact disclosed gap this plan closes** — its own docstring reads
  "misattributed by design (disclosed, not fixed): the halt names the wrench consumer, not
  `tests/plugins/nan_atmosphere`, which is the plugin that actually produced the NaN." This test
  (using `tests/plugins/nesc_aero`, a pre-ADR-0018 direct-`WrenchSource` plugin, not `aero_stub`) must
  be **renamed and its assertions flipped**: `halt.check` becomes `kNonFiniteEnvironment`, `halt.source`
  becomes `"tests/plugins/nan_atmosphere"`, and the docstring changes from "disclosed, not fixed" to
  recording that ADR-0020 fixed it. This is the single clearest acceptance proof that `check_environment`
  actually closes the gap it exists for — promoted into R4's acceptance criteria below, not left as
  an incidental side effect.
- `tests/physics/no_alloc_test.cpp` (read in full): `ZeroHeapAllocationsOverOneThousandFramesWithAeroAndPropulsionModels`
  (lines 117-137) drives `aero_stub`/`propulsion_stub` through a config file exactly like
  `aero_propulsion_seam_test.cpp` does — no direct `compute()`/`AeroInputs` reference in the test
  body itself. **No structural change needed**; only its two comments (lines 117-119, 135-136, both
  citing "ADR-0018"/"compute()") are updated to cite ADR-0020/`wrench_b()` for accuracy, not because
  the test's own behavior changes.
- `tests/plugins/aero_stub/aero_stub.cpp`/`propulsion_stub`'s own `.cpp` (aero_stub read in full):
  currently overrides `compute(const AeroInputs&)`; rebuilds to override `wrench_b(vs, env)` directly,
  reading `vs`/`env` (or `air_data.hpp` utilities over them) instead of a pre-assembled `AeroInputs`.
  `elevator_`-reading logic (ADR-0017 write seam) is unaffected — `bind_inputs()` itself does not
  change.
- `core/include/astra/kernel/eom.hpp:29-31` (read): a comment only ("Zero by default: no AeroModel or
  PropulsionModel exists yet") — `Eom` never holds an `AeroModel`/`PropulsionModel` pointer itself
  (`Vehicle::wrench_sources_` does); confirmed no code change needed here.
- `Vehicle::PhysicsSpec`/`Simulation::for_testing` (`simulation.hpp`, not re-read in full this
  session beyond the `from_config` path above): `for_testing` builds its own `PhysicsSpec`
  independently of `from_config` — PLAN.md's own M2 PR 2 entry already names this exact class of
  divergence ("wind's divisor guard existed without atmosphere's ... from_config/for_testing
  divergence") as a recurring source of bugs in this codebase. This plan's two new `PhysicsSpec`
  fields must be threaded through `for_testing` too, or explicitly, visibly defaulted to empty with
  a comment explaining why (see Q2).
- `docs/architecture.md` §7 (rev 7, already rewritten this session) is the target code shape this
  plan implements against — confirmed consistent with everything read above; no further architecture
  edit needed as part of this plan.

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance |
|---|---|---|---|
| R1 marker interfaces | `aero_model.hpp`/`propulsion_model.hpp` rewritten to the code block above; `aero_model.cpp`/`propulsion_model.cpp` deleted (no more out-of-line `wrench_b()`); `aero_inputs.hpp`/`propulsion_inputs.hpp` deleted | none | compiles; `nesc_aero`/`nan_wrench` unaffected and still pass unmodified |
| R2 `air_data.hpp` | new header + `.cpp`, relocated arithmetic | R1 (same commit, source moves) | `tests/unit/util/air_data_test.cpp` (new): the still-air/wind-subtraction/V→0/signed-zero cases from the deleted `aero_model_test.cpp`, now against the free functions directly |
| R3 `check_environment` | `health_monitor.hpp`/`.cpp`: new method, `HealthCheck::kNonFiniteEnvironment` enumerator | none | `tests/unit/kernel/health_monitor_test.cpp` (extended): non-finite `wind_n` halts naming `wind_source`; non-finite/infinite atmosphere field (density, speed_of_sound — the R-001 case) halts naming `atmosphere_source`; `warn`/`off` actions behave like the existing `nan` key's other checks |
| R4 call site + plugin-path plumbing | `vehicle.hpp`/`.cpp`: two new members, the `check_environment` call in the derivative functor; `PhysicsSpec` gains two fields; `simulation.cpp` threads both locals through | R3 | a new `Vehicle`-level integration test (or extended `tests/unit/kernel/vehicle_test.cpp`) proving a stage with a non-finite `env.atmosphere.speed_of_sound_m_s` halts naming the configured atmosphere plugin's path, **before** any `wrench_sources_` entry's `wrench_b()` runs that stage (a counting stub proves zero calls, mirroring the deleted `aero_model_test.cpp`'s own `call_count` pattern); **`simulation_halt_test.cpp`'s `NanAtmosphereInRangeHaltsNamingTheWrenchConsumerNotTheAtmospherePlugin` renamed and its assertions flipped to `kNonFiniteEnvironment`/`"tests/plugins/nan_atmosphere"`** — the named proof this requirement actually closes the disclosed gap it exists for, not an incidental side effect; `aero_propulsion_seam_test.cpp`'s own third test updated the same way |
| R5 rebuilt stubs | `aero_stub.cpp`/`propulsion_stub.cpp` override `wrench_b()` directly | R1, R2 | existing PR 3 integration-test pattern (`aero_propulsion_seam_test.cpp`, rebuilt if R6 below finds it needs it) passes |
| R6 `for_testing` parity | `simulation.hpp`/`.cpp`: `for_testing`'s own `PhysicsSpec` construction gets the same two fields (or an explicit, commented empty default) | R4 | a test proving `for_testing` with a non-finite environment behaves consistently with `from_config` (closing, not repeating, the from_config/for_testing divergence class PLAN.md's M2 PR 2 entry already names) |
| R7 `kCoreApiVersion` | `core_api.hpp.in`: bump 6→7, comment naming this PR/ADR-0020, same precedent as version 6's own entry | R1 | `just build` under every preset; a stale-descriptor rejection test (existing pattern) still passes |
| R8 docs | none — `docs/architecture.md` §1/§6/§7/§18 already rewritten this session (rev 7); confirm no drift once code lands | R1-R7 | `git diff` after implementation touches no architecture.md prose beyond what rev 7 already states (a mismatch found here is this plan's own bug, not a doc-update task) |

One Claude builder by default; R1/R2 are one commit (the arithmetic moves, not rewritten), R3/R4 are
the health-check addition, R5/R6 are the stub/test rebuild, R7 is mechanical. No parallel split
proposed — the pieces are small and share files (`vehicle.hpp`/`.cpp` touched by both R4 and, lightly,
R6).

## Validation and risks

Fast gate (`scripts/check-fast.sh`) after R1-R3; full gate (`scripts/check-full.sh`) once R1-R7 land.
No golden or tolerance file touched — no `touch-goldens` arm needed for this PR (distinct from PR
4a/ADR-0021's own R1 golden). Regression risk: `nesc_aero`/`nan_wrench` (pre-ADR-0018 direct-
`WrenchSource` plugins) must continue passing completely unmodified — they are the existing proof
this design already works and are not touched by this plan at all. Numerical risk: none — `air_data.hpp`'s
functions are a verbatim relocation, not a re-derivation; a diff-based review (not a re-derivation
review) is the right check for R2. Compatibility: `kCoreApiVersion` bump is the only ABI-visible
change; no external plugin exists to break. Rollback: fully reversible pre-PR-4 (PR 4 itself has not
started).

All files initially flagged as unread have since been read in full and folded into the Repository
findings and R4 acceptance criteria above (`aero_propulsion_seam_test.cpp`, `no_alloc_test.cpp`,
`eom.hpp`, `simulation_halt_test.cpp`'s disclosed-gap regression test). **Not yet read this
session**: `Simulation::for_testing`'s own implementation (`simulation.cpp`, the function body
itself, beyond confirming from `simulation.hpp`'s declaration that it builds its own `PhysicsSpec`
independently of `from_config`) — needed before R6 starts, to settle Q2 concretely rather than only
in principle.

## Plan critique and decisions

Tier 3, and this plan makes real decisions ADR-0020 itself left open (the `sim.health` key reuse;
the extent of plugin-path plumbing; the full deletion of two existing test files) — one fresh,
read-only Codex plan critique is warranted per `docs/process/workflow.md`'s plan-review trigger and
this repo's own established practice for every M3 ADR so far. **Not yet dispatched** — this document
is the "implementation plan ... shown to Joe before coding" Joe asked for; the critique runs next,
findings recorded here before any code is written.

### Questions and decision

- **Q1** — `check_environment`'s `sim.health` action: reuse the existing `nan` key (this plan's
  default, matching `check_state`/`check_wrench`'s own precedent) or add a new `environment` key.
  Consequence of reusing `nan`: an operator cannot independently silence environment-NaN detection
  while keeping state/wrench-NaN detection on, or vice versa. Owner: Joe. **Proposed default above;
  flagged for explicit confirmation, not assumed silently.**
- **Q2** — `Simulation::for_testing`'s own `PhysicsSpec` construction: thread real plugin-path
  strings (needs `for_testing`'s own signature to gain two more string parameters, a real, if small,
  API change) or default both to empty with a comment (cheaper, but means a `for_testing`-built
  `Vehicle` can never name a real plugin on an environment-NaN halt — a test-only limitation, not a
  production one, the same shape `for_testing`'s own existing gaps already take per PLAN.md's M2 PR 2
  entry). Owner: Joe.
- **Q3** — whether `wind_plugin_path`/`atmosphere_plugin_path` belong on `PhysicsSpec` (this plan's
  proposal, matching `earth_plugin_path`/`gravity_plugin_path`'s existing placement there) or should
  instead be threaded some other way (e.g. a separate small struct, since wind/atmosphere are
  logically "environment," not "physics" in the same sense earth/gravity/mass are). Owner: Joe, low
  stakes — flagged for completeness, this plan's author has no strong preference.

Joe's plan approval: **pending** (this document, plus the Codex critique's findings once dispatched).
