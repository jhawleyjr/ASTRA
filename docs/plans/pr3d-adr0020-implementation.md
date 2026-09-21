# Feature + implementation plan: PLAN.md M3 PR 3d — implement ADR-0020

Status: **approved by Joe, 2026-09-20** (Q1-Q4 decided; see "Questions and decision" below)
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

**`core/src/interfaces/aero_model.cpp`/`propulsion_model.cpp` are trimmed, not deleted — Codex plan
critique finding.** The original draft said R1 deletes both `.cpp` files outright; that is wrong and
would break the link: each file's only current content besides the (moving) `wrench_b()` arithmetic
is `AeroModel::~AeroModel() = default;` / `PropulsionModel::~PropulsionModel() = default;` — the
documented key-function definition needed for `dynamic_cast<AeroModel*>` to work correctly across
the `.so` boundary (`aero_model.hpp`'s own comment, unchanged by this plan). Deleting the file
deletes the only definition of that destructor, an undefined-symbol link error. **Corrected**: both
files are kept, trimmed to just the out-of-line destructor definition (`requires_environment()`
moves in-line into the header per the code block above, so it needs no out-of-line body). No
`core/CMakeLists.txt` change needed — both files stay in the same build target under the same names.

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
`core/src/kernel/health_monitor.cpp`), mirroring `check_wrench`'s shape, **revised after Codex plan
critique below** to take explicit availability rather than inferring it:

```cpp
[[nodiscard]] HaltReason check_environment(const EnvironmentSample& env, bool wind_configured,
                                           bool atmosphere_configured, std::string_view wind_source,
                                           std::string_view atmosphere_source,
                                           const StepContext& ctx,
                                           std::uint32_t vehicle_index) noexcept;
```

Checks `env.wind_n.allFinite()` only when `wind_configured`, and `env.atmosphere`'s four fields for
`std::isfinite` only when `atmosphere_configured` — **not unconditionally** (Codex finding: the
original draft's unconditional four-field check would halt every valid atmosphere-free simulation,
since `AtmoState`'s own documented default, read when no `physical.atmosphere` is configured at all,
is all-NaN by design, not a fault). Applying the existing `nan_action_`/`nan_count_` (Q1, resolved
above) and a new `HealthCheck::kNonFiniteEnvironment` enumerator, naming `wind_source` or
`atmosphere_source` on a halt (whichever field failed first; atmosphere checked after wind, matching
`check_domain`'s own earth-then-gravity precedent). No new `sim.health` config key. Per-frame path:
no allocation, no exceptions.

**Availability plumbing** (closes the same Codex finding): `atmosphere_configured` is exactly
`atmosphere_ != nullptr`, a `Vehicle` member that already exists (`vehicle.hpp:212`) — no new state
needed for it. `wind_configured` has no existing equivalent: `Vehicle` never retains
`EnvironmentAvailability` past `init()` (it is a transient parameter, `vehicle.cpp:12-13`, used only
to validate `requires_environment()` against it). **New `Vehicle` member**: `bool wind_configured_ =
false;`, set from `availability.wind` inside `init()`, alongside the two new plugin-path members
below.

**Call site and control flow — revised twice: after Codex's plan critique, then again per Joe's own
Q4 decision.** The original draft called `check_environment` before the `wrench_sources_` loop but
let the loop run anyway on a failure (matching `check_wrench`'s own "uniform evaluation, no
short-circuit" rule) — Codex correctly found this contradicts R4's own "zero `wrench_b()` calls"
acceptance claim: `check_wrench`'s rule exists because evaluating IS how a wrench fault is detected;
`check_environment`'s fault is already known *before* any `wrench_b()` call, so there is no
equivalent reason to still call every configured plugin on input already known to be broken. A
first fix made `check_environment` itself skip the `wrench_sources_` loop on any failure; **Joe's own
Q4 decision goes further and separates atmosphere's two distinct failure classes *before*
`check_environment` ever runs**, resolving the domain-check-precedence conflict Codex separately
found (Q4), not merely disclosing it:

1. When `atmosphere_ != nullptr` (unchanged existing gate, `vehicle.cpp:157`): if `health.
   domain_enabled()`, compute `altitude_m = eom_->earth().altitude(vs_stage)` and check it against
   `atmosphere_->valid_range()` using the identical NaN-safe negated form `simulation.cpp:420`
   already uses (`!(altitude_m >= range.min_m && altitude_m <= range.max_m)`), **before** calling
   `atmosphere_->at()` this stage.
2. **Out of range** (including a NaN `altitude_m`, covered by the same negated form): call the
   existing `health.report_out_of_domain(atmosphere_plugin_path_, ctx, index_)` — no new
   `HealthMonitor` method for this half, it is the same method `Vehicle::check_domain` and
   `Simulation::step`'s own post-step check already call for exactly this purpose. Apply the result
   under `wrench_fault`'s existing first-fault-wins rule and **skip the `wrench_sources_` loop this
   stage** — `stage_env.atmosphere` is left unsampled (never read, since the loop is skipped).
3. **In range** (or `domain_enabled()` is false, matching "domain: off restores current behavior" —
   `altitude_m` is still computed unconditionally when `atmosphere_ != nullptr`, since `at()` already
   needed it every stage before this plan): sample `stage_env.atmosphere = atmosphere_->at
   (altitude_m)` exactly as today, **then** call `health.check_environment(stage_env, wind_configured_,
   atmosphere_ != nullptr, wind_plugin_path_, atmosphere_plugin_path_, ctx, index_)` (still under
   `nan`, Q1) — this call can now only ever observe an atmosphere sample already confirmed in-range,
   so a still-non-finite result is unambiguously a genuinely broken model (`nan_atmosphere`'s own
   enormous `valid_range()` keeps it "in range" by construction, so it still reaches this path,
   exactly as intended). On a `check_environment` halt: identical first-fault-wins handling and
   `wrench_sources_`-loop skip as step 2.

Either skip path treats the total applied wrench as zero for this stage's EOM evaluation (the
frame's own state is discarded on the default halt action regardless, per the existing wrench-fault
rollback, `vehicle.cpp:203-209`; under `warn`, the run continues with a zero-wrench stage — an
explicit, disclosed choice, documented in `check_environment`'s own header comment per Joe's
instruction, not an accidental one). `published_wrench_` (k1 only) is set to zero the same way.
**This is a per-stage skip, not a per-frame abort**: a later stage in the same frame with a
since-recovered (or never out-of-range/never-broken) environment sample evaluates normally —
`EnvironmentSample` is rebuilt fresh every sub-stage (PR 3c), so neither fault is assumed to persist
across stages within one frame. R4's acceptance below tests both fault classes present at every
stage and each appearing only from k2 onward.

**New `Vehicle` members**: `std::string wind_plugin_path_;`, `std::string atmosphere_plugin_path_;`,
and `bool wind_configured_` (above), alongside the existing `earth_plugin_path_`/`gravity_plugin_path_`
(`vehicle.hpp:231-232`), set from `PhysicsSpec`/`EnvironmentAvailability` in `Vehicle::init()` the
same way `earth_plugin_path_`/`gravity_plugin_path_` already are (`vehicle.cpp:70-72`). Empty plugin-
path strings are never actually named on a halt, since the corresponding `*_configured` flag (or the
domain pre-check, for atmosphere) gates whether that field is checked at all.

**`Vehicle::PhysicsSpec`** (`vehicle.hpp:61-68`) gains one new field: `std::string
atmosphere_plugin_path;`, alongside the existing `earth_plugin_path`/`gravity_plugin_path`. Wind's
path does **not** go on `PhysicsSpec` (Codex finding, see R6/Q2 below): `PhysicsSpec` already exists
specifically for `Vehicle::init()`'s **physics** (earth/gravity/mass/atmosphere) arguments, all
required or all-absent together; wind is a separate, independently-optional environment model
`Simulation::from_config` already threads through `EnvironmentAvailability` alone, never through
`PhysicsSpec` — `wind_plugin_path_` is threaded as its own new `Vehicle::init()` parameter instead,
matching that existing separation rather than forcing it onto the physics-specific aggregate.

**`Simulation::from_config`** (`simulation.cpp:330-396`): `atmosphere_plugin_path` (line 378, already
also stored as `sim.atmosphere_plugin_path_`) threads into the `PhysicsSpec{...}` aggregate-init at
lines 392-394. `wind_plugin_path` (line 339) threads into `vehicle.init()`'s new parameter directly
(same call, `vehicle.cpp:395-396`), not through `PhysicsSpec`.

**Failure behavior**, restated precisely: a non-finite, *configured* `wind_n`, or an in-range but
genuinely non-finite `atmosphere` sample, now halts (default action) via `check_environment`, naming
the wind/atmosphere plugin, **and skips every configured `wrench_b()` call this stage** — closing
`environment_sample.hpp`'s own two already-disclosed gaps and restoring the exact
no-silent-NaN-into-a-plugin guarantee the deleted `final` guard gave. Separately, an atmosphere
sample that is out of the model's own declared `valid_range()` (a legitimate, documented condition,
never itself a fault) is now caught **before** `check_environment` ever runs, by a new per-stage
domain pre-check reusing the existing `report_out_of_domain`/`kOutOfDomain`/`sim.health.domain`
path — **this is the resolution of `PLAN.md`'s "Atmosphere-domain-check misattribution window" item
(Q4), not merely a disclosed interaction**: the two failure classes (legitimately out of range, vs.
genuinely broken within range) are now separated at the source, before either check needs to
disambiguate an already-produced ambiguous value.

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
- `Vehicle::PhysicsSpec`/`Simulation::for_testing` — **Codex finding, corrects this plan's own
  original framing**: `for_testing` has **no atmosphere parameter at all** today (confirmed
  independently by `aero_propulsion_seam_test.cpp`'s own header comment: "`Simulation::for_testing()`
  cannot exercise this at all (Codex plan critique F1: it has no atmosphere parameter)"). So this
  plan's atmosphere-path threading has nothing to parallel in `for_testing` — only `wind_plugin_path`
  (now threaded as a separate `Vehicle::init()` parameter, not a `PhysicsSpec` field, per the Contract
  section's own correction above) needs a `for_testing` counterpart, and `for_testing` already
  supplies wind availability independently with its own descriptive placeholder naming pattern for
  earth/gravity (`simulation.cpp:521-540`, not yet read in full this session). R6 above is narrowed
  to wind only, not both fields.
- **Codex finding, not in this plan's original repository-findings pass**:
  `tests/unit/kernel/simulation_halt_test.cpp:333-358`'s
  `LeavingTheAtmosphereValidRangeMidRunHaltsNamingTheAtmospherePlugin` (exact name not yet confirmed
  against the file, cited by Codex) configures a vehicle with **no wrench sources** that crosses ISA's
  lower altitude boundary at constant downward velocity, asserting the existing post-step
  `kOutOfDomain` halt fires, naming the atmosphere plugin. Since `Vehicle::integrate()`'s derivative
  functor samples `stage_env.atmosphere` fresh every RK4 sub-stage (PR 3c) — including at k4, before
  `Simulation::step`'s own post-step range check ever runs (`simulation.cpp:588-610`) — the crossing
  stage's atmosphere sample goes all-NaN (the documented, legitimate out-of-`valid_range()` behavior)
  *before* the domain check gets a chance to observe it. `check_environment` as this plan specifies it
  cannot distinguish "all-NaN because legitimately out of `valid_range()`" from "all-NaN because
  genuinely broken" (`tests/plugins/nan_atmosphere` returns the identical `AtmoState{}` pattern **by
  design**, specifically to be indistinguishable at the value level — confirmed by reading
  `nan_atmosphere.cpp`'s own header comment). This is the **same already-disclosed PLAN.md item**
  ("Atmosphere-domain-check misattribution window ... PR 3c's own disclosed, accepted gap ... Needs
  its own plan-reviewed decision, not a bolt-on") — this plan does not create a new gap, but a naive
  implementation would change which check preempts the other for this scenario, letting
  `sim.health.nan` silently override an operator's own `sim.health.domain` choice. **Resolved by
  Joe's Q4 decision above**: the new per-stage domain pre-check runs *before* atmosphere is ever
  sampled, so this exact scenario is caught as `kOutOfDomain` (governed by `sim.health.domain`,
  unchanged) before `check_environment` could ever see the ambiguous all-NaN value — this test's own
  existing `kOutOfDomain`/atmosphere-plugin-name assertions should hold unchanged in substance, though
  the precise `ctx.frame` the halt fires on may shift earlier (the pre-check can trigger on an
  earlier RK4 sub-stage's own trial altitude within the crossing frame, not only the frame's final
  post-step state) — verify against the real implementation, not assumed, and adjust any frame-count
  assertion if needed.
- `docs/architecture.md` §7 (rev 7, already rewritten this session) is the target code shape this
  plan implements against — confirmed consistent with everything read above; no further architecture
  edit needed as part of this plan.

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance |
|---|---|---|---|
| R1 marker interfaces | `aero_model.hpp`/`propulsion_model.hpp` rewritten to the code block above; `aero_model.cpp`/`propulsion_model.cpp` **trimmed to the destructor only, not deleted** (Codex finding — key-function definition); `aero_inputs.hpp`/`propulsion_inputs.hpp` deleted | none | compiles and links; `nesc_aero`/`nan_wrench` unaffected and still pass unmodified |
| R2 `air_data.hpp` | new header + `.cpp`, relocated arithmetic | R1 (same commit, source moves) | `tests/unit/util/air_data_test.cpp` (new): the still-air/wind-subtraction/V→0/signed-zero cases from the deleted `aero_model_test.cpp`, now against the free functions directly |
| R3 `check_environment` | `health_monitor.hpp`/`.cpp`: new method taking explicit `wind_configured`/`atmosphere_configured` (Codex finding — no unconditional field check), `HealthCheck::kNonFiniteEnvironment` enumerator | none | `tests/unit/kernel/health_monitor_test.cpp` (extended): non-finite `wind_n` halts naming `wind_source` only when `wind_configured`; non-finite/infinite atmosphere field (density, speed_of_sound — the R-001 case) halts naming `atmosphere_source` only when `atmosphere_configured`; an unconfigured field (the other `*_configured` false) is never checked even when its `EnvironmentSample` value is the documented all-NaN/zero default; `warn`/`off` actions behave like the existing `nan` key's other checks |
| R4 call site, domain pre-check, skip-on-fault, and plugin-path/availability plumbing | `vehicle.hpp`/`.cpp`: three new members (`wind_plugin_path_`, `atmosphere_plugin_path_`, `wind_configured_`); the altitude-vs-`valid_range()` pre-check (reusing `report_out_of_domain`, Q4) before atmosphere sampling; the `check_environment` call after sampling; the **skip-`wrench_sources_`-loop-this-stage** control flow shared by both fault paths in the derivative functor; `PhysicsSpec` gains one field (`atmosphere_plugin_path`; wind's path threads as a new `Vehicle::init()` parameter instead); `simulation.cpp` threads both locals through | R3 | a new `Vehicle`-level integration test (or extended `tests/unit/kernel/vehicle_test.cpp`) proving: a stage with a non-finite, *configured*, **in-range** `env.atmosphere.speed_of_sound_m_s` halts `kNonFiniteEnvironment` naming the atmosphere plugin, with zero `wrench_sources_` calls that stage (a counting stub proves it), for a fault present at k1 and, separately, first appearing at k2; a stage whose altitude leaves `valid_range()` halts `kOutOfDomain` naming the atmosphere plugin, atmosphere never sampled, zero `wrench_sources_` calls, **before** any `check_environment` involvement; a test proving an *unconfigured* wind/atmosphere never halts even though its `EnvironmentSample` field is the documented default; `PLAN.md`'s "Atmosphere-domain-check misattribution window" item updated to record resolution; `simulation_halt_test.cpp`'s `NanAtmosphereInRangeHaltsNamingTheWrenchConsumerNotTheAtmospherePlugin` renamed/reasserted to `kNonFiniteEnvironment`/`"tests/plugins/nan_atmosphere"` plus a zero-call assertion, and its `LeavingTheAtmosphereValidRangeMidRunHaltsNamingTheAtmospherePlugin` verified (and its frame-count assertion adjusted if the real implementation halts earlier than before); `aero_propulsion_seam_test.cpp`'s own third test updated the same way as the first regression test; `check_environment`'s own header comment documents the "zero wrench under `warn`" behavior (Joe's instruction) |
| R5 rebuilt stubs | `aero_stub.cpp`/`propulsion_stub.cpp` override `wrench_b()` directly | R1, R2 | existing PR 3 integration-test pattern (`aero_propulsion_seam_test.cpp`, rebuilt if R6 below finds it needs it) passes |
| R6 `for_testing`'s own wind-path parity (narrowed — Codex finding: `for_testing` has no atmosphere parameter at all today, so only wind's plumbing applies here) | `simulation.hpp`/`.cpp`: `for_testing`'s own construction gets a `wind_plugin_path` parameter (or an explicit, commented empty default) alongside its existing wind-availability handling | R4 | a test proving a `for_testing`-built vehicle with a non-finite, configured `wind_n` behaves consistently with `from_config` for wind specifically; **explicitly out of scope**: `for_testing` exercising a non-finite *atmosphere* at all, since it has no atmosphere path today — not a regression this plan introduces |
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
this repo's own established practice for every M3 ADR so far.

**Codex plan critique (adversarial-review, working-tree scope, `2026-09-20`): verdict
"needs-attention," two high findings and two medium findings, all accepted and folded into this
plan above; no rejected findings.**

- **High** (accepted, folded in above): R4's own "zero `wrench_b()` calls" acceptance claim
  contradicted the plan's own call-site instruction to keep evaluating `wrench_sources_` uniformly
  after a `check_environment` failure (matching `check_wrench`'s different, correctly-uniform rule).
  **Fixed**: `check_environment`'s failure now explicitly skips the `wrench_sources_` loop for that
  stage, restoring the actual guarantee; acceptance criteria extended to test a fault at k1 and a
  fault first appearing at k2.
- **High** (accepted, folded in above): the proposed `check_environment` signature had no way to
  distinguish a configured field from `EnvironmentSample`'s own legitimate "absent" defaults (zero
  `wind_n`, all-NaN `atmosphere`), so it would have halted every atmosphere-free simulation. **Fixed**:
  explicit `wind_configured`/`atmosphere_configured` parameters; `atmosphere_configured` reuses the
  existing `atmosphere_ != nullptr` member at zero new cost, `wind_configured_` is a new `Vehicle`
  member. Also corrected `Simulation::for_testing`'s own involvement (Q2) and where the new plugin-
  path fields actually belong (Q3, `PhysicsSpec` for atmosphere only, a separate parameter for wind)
  once reading confirmed `for_testing` has no atmosphere concept at all today.
- **Medium** (accepted, recorded as Q4, not resolved by this plan): `check_environment`'s earlier,
  per-stage timing can now preempt the existing, already-disclosed "atmosphere-domain-check
  misattribution window" `PLAN.md` item for an in-range-model, out-of-`valid_range()` excursion — not
  a new gap this plan creates, but a real change in which `sim.health` action (`nan` vs `domain`)
  governs it, missed entirely by this plan's original repository-findings pass. Left for Joe's
  explicit decision (three options given, no recommendation) rather than resolved unilaterally, since
  a field-value heuristic cannot actually distinguish the two cases (confirmed: `nan_atmosphere`'s own
  all-NaN return is deliberately identical to a legitimate excursion's).
- **Medium** (accepted, folded in above): R1's original "delete `aero_model.cpp`/`propulsion_model.cpp`"
  would have deleted the only definition of `AeroModel`'s/`PropulsionModel`'s own out-of-line
  destructor — the documented key-function needed for `dynamic_cast` across the `.so` boundary — an
  undefined-symbol link error. **Fixed**: both files are trimmed to the destructor definition only,
  not deleted; no `CMakeLists.txt` change needed.

A second, focused re-verification pass is not yet requested: Q1/Q4 are real design decisions for Joe,
not technical claims a re-verify could itself confirm or refute — the next Codex pass, if any, checks
the *implementation* of whichever option Joe picks, not this plan's own text further.

### Questions and decision

- **Q1 — decided by Joe, 2026-09-20: reuse the existing `nan` key.** No new `sim.health` config
  surface for `check_environment`.
- **Q2/Q3 — decided by Joe, 2026-09-20: as resolved above** (a `for_testing` wind-path parameter, no
  atmosphere-path counterpart there; `atmosphere_plugin_path` on `PhysicsSpec`, `wind_plugin_path` as
  a separate `Vehicle::init()` parameter).
- **Q4 — decided by Joe, 2026-09-20: none of (a)-(c). A fourth design, checking domain *before*
  sampling, separates the two failure classes at the source instead of trying to disambiguate an
  already-produced value.** In the per-stage derivative functor, when `atmosphere_ != nullptr` and
  `health.domain_enabled()`: compute the stage's own altitude
  (`eom_->earth().altitude(vs_stage)`) and check it against `atmosphere_->valid_range()` using the
  **identical NaN-safe negated form** `Simulation::from_config`'s own init-time check already uses
  (`simulation.cpp:420`, `!(altitude_m >= range.min_m && altitude_m <= range.max_m)`, never the
  "simplified" `<`/`>` form, which is false — silently accepting — for a NaN altitude) —
  **before** `atmosphere_->at()` is ever called this stage. **Out of range** (or a NaN stage
  altitude, covered for free by the same negated form): `health.report_out_of_domain
  (atmosphere_plugin_path_, ctx, index_)` — the existing method `Vehicle::check_domain` and
  `Simulation::step`'s own post-step check already use for exactly this purpose, no new
  `HealthMonitor` method needed for this half — under `wrench_fault`'s existing first-fault-wins
  rule, and **skip that stage's `wrench_sources_` loop** (the same skip mechanism `check_environment`'s
  own fault already uses, reused here, not a second implementation). Atmosphere is **not** sampled
  this stage in that case (`stage_env.atmosphere` stays at whatever `environment_`'s own frame-start
  copy already held — never read, since the wrench loop is skipped). **In range**: sample
  `atmosphere_->at(altitude_m)` exactly as today, then run `check_environment` (still under `nan`,
  Q1) against the now-sampled fields plus `wind_n`. This means `check_environment`'s own atmosphere
  check can now only ever see a sample the domain check has already confirmed is in-range — a
  genuinely broken model (`nan_atmosphere`, whose own enormous `valid_range()` keeps it "in range" by
  construction, per its own header comment) is what reaches it, never an expected excursion. **This
  is the actual resolution of `PLAN.md`'s "Atmosphere-domain-check misattribution window" item** —
  update that item's own text to record it fixed, not merely disclosed, and update both affected
  regression tests (`simulation_halt_test.cpp`'s `NanAtmosphereInRangeHaltsNamingTheWrenchConsumer-
  NotTheAtmospherePlugin`, unaffected in substance by this new pre-check since `nan_atmosphere` stays
  "in range" — rename/reassert to `kNonFiniteEnvironment`/`"tests/plugins/nan_atmosphere"` as already
  planned; and `LeavingTheAtmosphereValidRangeMidRunHaltsNamingTheAtmospherePlugin`, whose existing
  `kOutOfDomain`/atmosphere-plugin-name assertions should now hold **exactly as already written**,
  but the halt can fire up to three RK4 sub-stages earlier within the boundary-crossing frame than
  before — the actual `ctx.frame`/exact-halt-timing needs verifying against the real implementation,
  not assumed unchanged, since the test may need a frame-count adjustment even though its `check`/
  `source` assertions do not). **Also required, per Joe's instruction**: `check_environment`'s own
  header comment documents the "zero wrench for a faulted stage under `warn`" behavior explicitly —
  under a non-halting `warn` action (either check), the run continues with that stage's total applied
  wrench treated as zero, not the last-known-good value, an explicit, disclosed choice.

Joe's plan approval: **approved, 2026-09-20**, with Q1-Q4 decided as recorded above. Implementation
begins next: `/review-cycle`, full gate, `/human-review` packet, draft PR — Joe stops the workflow
there for his own review, not merge.
