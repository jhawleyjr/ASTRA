# ASTRA — Architecture v1

Status: **accepted**, 2026-09-14 (rev 6: M0 defaults — style, CI, time representation, single-vehicle-as-list, Python models; **accepted by Joe 2026-09-14**; rev 7, 2026-09-20: §1/§6/§7/§18 rewritten for ADR-0019 (vehicle-agnostic purpose, supersedes ADR-0000's Purpose clause), ADR-0020 (`AeroModel`/`PropulsionModel` as true marker interfaces, supersedes ADR-0018's `Inputs`/`final` design), and ADR-0021 (core `DynamicsModel` interface with model-owned continuous state, supersedes ADR-0003's fixed-layout decision) — **accepted by Joe 2026-09-20**, both implemented (PLAN.md M3 PR 3d, PR 4a); rev 8, 2026-09-24: §2/§5/§10/§11/§15/§16 for ADR-0022 (lockstep exchange stage), ADR-0023 (transport-timeout clock exception to ADR-0010), and ADR-0024 (shared shm layout header and protocol, supersedes ADR-0007 in part), all **accepted by Joe 2026-09-24**; ADR-0024 supersedes ADR-0007 in part and ADR-0023 supersedes ADR-0010 in part; rev 9, 2026-09-26: §5/§9/§10/§12/§13/§15/§16 for ADR-0025 (HDF5 run log and replay) and ADR-0026 (derived flight-condition signals), both **accepted by Joe 2026-09-27**; neither supersedes an accepted record; rev 10, 2026-09-29: §1/§9/§12/§15/§16 for ADR-0027 (run-log finalization, superseding ADR-0025's Finalization section only) and the M6 plan (`docs/plans/m6-analysis-tooling.md`), **ADR-0027 accepted by Joe 2026-09-29**; every M6 feature not yet built is marked *planned* with its M6 PR). This is the sole input to milestone M0 in Claude Code. It is bound by `decisions/0000-founding-decisions.md` and built under `process/workflow.md`. Where this document makes a choice that a later ADR will own, it says so; M0's first job is to turn those into `docs/decisions/` records with the rejected alternatives written down.

---

## 1. What this is

A vehicle-agnostic, high-fidelity six-degree-of-freedom simulation **framework and architecture**
(ADR-0019). The framework and its interfaces are the product: it must let a user build a new
vehicle, at whatever fidelity they need, without editing `core/`. It ships no vehicle of its own;
`aircraft/notional_transport` is the simplest possible worked example proving the public interfaces
end to end (template → models → trim → linearize → fly against external software), not a claim of
commercial-transport focus or fidelity — its numbers are notional and do not matter. It ships one
trivial autopilot as a worked example of "embedded software outside the sim tree." It does not ship
a fleet of aircraft, avionics models, or a cockpit.

The user story the whole design serves: *clone the repo, plug in whatever models your vehicle needs
— at minimum mass properties, aero, and propulsion, each derived from an abstract interface in
`core/include/astra/interfaces/` and optionally owning its own continuous state (ADR-0021) — point
the sim at your flight software binary, run.* A model's interface states what it must be able to
*do*, never what it is *made of*: no core-defined input struct, no `final` method that fixes
behavior (ADR-0020). Everything else — Earth models, atmosphere, integrators, logging, trim, Monte
Carlo — is already there. Offline plot packages are built (SVG or PNG files, and interactive HTML
that works offline; M6 PR 5), and so are Monte Carlo figures (PR 6); a self-contained HTML run report
(PR 8) is *planned*. A live plotting UI is not claimed.

**v1 scope** (from the founding decisions): airborne start at a trimmed cruise condition; no takeoff or landing, so no ground reaction; the example embedded software is a straight-and-level autopilot in its own process; all-digital only, but with the real-time and HWIL seams designed in.

## 2. Principles that decide arguments

1. **The sim owns time.** No model, transport, or embedded process reads the wall clock for time; the one exception is the lockstep transport's wait deadline (ADR-0023), which never supplies a time value. Time is a `VirtualClock` the scheduler advances. This is what makes "faster than real time" and "bit-identical replay" both work, and it is what makes real-time mode a one-line change (the clock is paced by a timer instead of running free).
2. **Determinism is a feature under test.** Same configuration and inputs produce byte-identical logs. Random processes (turbulence, dispersions) are seeded and stepped by the scheduler, never by a system RNG.
3. **Real-time capable from day one, even though v1 never runs real-time.** Concretely: nothing in the per-frame path allocates, throws, locks a non-RT-safe primitive, or does I/O. Logging goes to preallocated ring buffers. This costs almost nothing to honor now and is close to impossible to retrofit.
4. **The oracle is human-owned.** Physics tests, goldens, tolerances, and decision records are the fixed points the agent works against; they change only when Joe says so (workflow §Verification).
5. **The ICD is toolchain-neutral.** The frame layout shared between the sim (clang-18, C++23), the flight software (FPGA-side toolchain), and any companion computer is plain C11 with fixed-width types and static size assertions. The sim's C++23 stays on the sim's side of that line.
6. **Framework, not aircraft.** If a piece of code knows a wing area, a thrust curve, or a sensor's noise density, it is a plugin under `plugins/` or `aircraft/`, never part of `core/`.
7. **Boring where boring works.** SI doubles, Eigen, RK4, POSIX shared memory, HDF5's C API. Nothing exotic without an ADR that says why.

## 3. System context

```
                       all-digital (v1)                                HWIL (later)
   ┌────────────────────────────────────────────────┐     ┌───────────────────────────────────┐
   │  host PC, Ubuntu 24.04                          │     │ host PC (PREEMPT_RT)      FPGA     │
   │                                                 │     │                            │       │
   │  ┌─────────────┐   shm lockstep   ┌──────────┐  │     │  ┌─────────────┐  link  ┌──┴────┐  │
   │  │  sim proc   │◄───────────────►│ autopilot │  │     │  │  sim proc   │◄──────►│ flight│  │
   │  │  (astra)   │   icd/ frames   │  proc     │  │     │  │  (astra)   │  icd/  │  SW   │  │
   │  └──────┬──────┘                 └──────────┘  │     │  └──────┬──────┘        └──┬────┘  │
   │         │ HDF5 log     ▲ python driver          │     │         │ HDF5 log         │Jetson  │
   │         ▼              │                        │     │         ▼                  ▼(ML)    │
   │   analysis / FlightGear UDP                     │     │   analysis / FlightGear             │
   └────────────────────────────────────────────────┘     └───────────────────────────────────┘
```

Three processes matter in v1: the **sim** (C++ core, driven from Python or a CLI), the **embedded software** (a separate executable built from `embedded/`, talking only through `icd/` frames over a transport), and the **Python driver** (configures, runs, analyzes). In HWIL the embedded process is replaced by the FPGA over a physical link; the sim does not change, only the `Transport` implementation does. The Jetson is a companion to the FPGA and is invisible to the sim.

## 4. Repository layout and the plugin model

**One build, many shared objects.** The framework is a single CMake project built by one command (`just build`). It produces one core library, `libastra_core.so`, and one shared object per **plugin** — every concrete model, environment component, and transport is a plugin. Plugins are discovered by the build (every directory under `plugins/**` and `aircraft/**` with a `CMakeLists.txt` is added), compiled with the same toolchain and flags as the core, and loaded at run time by path from the configuration. A user adds an aircraft by copying `aircraft/_template/` to `aircraft/<name>/`, editing the models inside it, rebuilding, and running its config; nothing in the framework is edited. Nobody ships or imports prebuilt `.so` files — the tree is the unit of distribution, the build produces the binaries. *ADR-0009.*

**Two plugin roots.** `plugins/` is the shared library of models every aircraft can use, filed by kind. `aircraft/<name>/` is one aircraft's own models and configs, filed by the *same* kinds inside. A config references plugins from either root by path, so an aircraft can use its own aero with a shared engine, or two aircraft can share one IMU model.

```
astra/
├── AGENTS.md  CLAUDE.md  README.md  PLAN.md  STATE.md  justfile  CMakeLists.txt  CMakePresets.json  pyproject.toml
├── .claude/                hooks, skills (plan-feature, review-cycle, human-review, wrap), settings.json
├── docs/
│   ├── architecture.md     this document, kept current
│   ├── decisions/          ADRs NNNN-title.md, immutable once accepted
│   └── lessons.md          append-only gotchas
├── icd/                    C11 headers only. No dependencies. The contract with the flight software.
│   └── astra_icd.h
├── core/                   → libastra_core.so  (the framework kernel; knows no aircraft, no hardware)
│   ├── include/astra/
│   │   ├── interfaces/     abstract base classes: Model, AeroModel, SensorModel, Transport, ...
│   │   ├── kernel/         Simulation, Scheduler, VirtualClock, RigidBody6DofDynamics, Integrator, VehicleState
│   │   ├── plugin/         plugin descriptor, loader, registry
│   │   ├── signals/        SignalRegistry, Logger
│   │   ├── config/         Config tree, YAML loading, validation
│   │   └── util/           frames, quaternions, geodesy, units-at-boundaries, RT helpers
│   └── src/
├── plugins/                SHARED models, filed by kind. Each leaf → one libastra_<path_with_underscores>.so
│   ├── physical/           physics of the world (and generic vehicle physics if any)
│   │   ├── atmosphere/isa1976/
│   │   ├── wind/{uniform, table, dryden}/
│   │   ├── gravity/{constant, inverse_square, wgs84_j2}/
│   │   ├── earth/{flat, round, wgs84_rotating}/
│   │   └── mass/constant/
│   ├── hardware/           equipment any aircraft might carry: things with a part number
│   │   ├── sensors/imu/<vendor_or_grade>/      e.g. two IMUs as two plugins, swapped by config
│   │   ├── sensors/{airdata, gnss, radalt}/     (v1: directory contract only)
│   │   └── actuators/<type>/                    (v1: directory contract only)
│   └── sim/                simulation infrastructure that is still swappable
│       └── transport/
│           ├── loopback/   in-process functor, for tests
│           ├── shm/        shared-memory lockstep (v1)
│           └── fpga/       later: the physical link to the FPGA
├── aircraft/               ONE FOLDER PER AIRCRAFT, same kinds inside. Copy _template to start.
│   ├── _template/          the basic set every new aircraft begins with
│   │   ├── physical/{aero, mass}/
│   │   ├── hardware/propulsion/
│   │   └── configs/cruise.yaml
│   └── notional_transport/ the worked example twin-jet
│       ├── physical/{aero, mass}/
│       ├── hardware/propulsion/turbofan/
│       └── configs/{cruise, cruise_still_air, cruise_light_turbulence, cruise_moderate_turbulence}.yaml
├── embedded/               OUTSIDE the sim. Separate CMake project, included by the top level so one
│   │                       build produces everything, but it links icd/ + hal/ only — never core.
│   ├── hal/                reference HAL: frame in/out/wait; backends: shm (v1), fpga (later)
│   └── autopilot/          the straight-and-level example
├── python/astra/          nanobind module `_core` + pure-Python tools
│   └── trim.py  linearize.py  log.py  scenario.py  montecarlo.py  fgbridge.py  lockstep.py  cli.py
├── tests/
│   ├── unit/               GoogleTest, fast, run by the Stop hook
│   ├── physics/            oracle tests: conservation, analytic, NASA check cases
│   ├── golden/             trajectories + tolerances (guarded by hook)
│   ├── lockstep/           shm transport, HAL and closed-loop acceptance tests
│   ├── python/             pytest suites for the astra package
│   └── scenarios/          YAML scenarios executed by pytest
└── tools/                  later: misc tools (the CLI and the FlightGear bridge live in python/astra/)
```

**Taxonomy rule.** Inside either root, plugins are filed by what they *are*: aero and mass properties are physics and live under `physical/`; an engine, a sensor, an actuator is equipment and lives under `hardware/`; transports and other swappable infrastructure live under `sim/`. There is no `vehicle/` kind — everything on a vehicle is one of those. The aircraft folder is the ownership boundary (this is *my* aircraft's aero), not a different taxonomy. A run is a config that names one plugin per slot from wherever it lives, which is what makes swapping one engine for another between runs a config edit and not a copy.

**Granularity rule.** A plugin is *one thing a configuration can select*: an atmosphere, an engine, an IMU, a transport. Internals of a high-fidelity model (a compressor map, a combustor, a nozzle) stay inside their plugin as ordinary classes. This is what keeps a large tree navigable without turning it into hundreds of micro-libraries.

**The plugin contract.** Each plugin exports exactly one C symbol and hides everything else (`-fvisibility=hidden`):

```cpp
extern "C" const astra::PluginDescriptor* astra_plugin();
// PluginDescriptor { const char* category; const char* name; uint32_t core_api_version;
//                    const char* built_with; std::unique_ptr<Model> (*create)(); }
```

The loader (`core/plugin/`) resolves a plugin path from the config (e.g. `aircraft/notional_transport/physical/aero` or `plugins/physical/atmosphere/isa1976`) to `libastra_aircraft_notional_transport_physical_aero.so` (etc.) in the build's plugin directory, `dlopen`s it **at init only**, checks `core_api_version` and the `built_with` toolchain string against its own, and calls `create()`. Mismatch is a typed init error naming the plugin, never a crash later. Because every plugin is built in the same CMake invocation as the core, mismatches can only come from a stale build directory — the check exists to make that failure loud.

The same descriptor is usable without `dlopen`: a `ASTRA_STATIC_PLUGINS` build option links every plugin into one binary and registers descriptors by direct call. This is the intended shape for a hard-real-time or embedded-host build and costs no second code path.

**CMake helper.** A plugin's `CMakeLists.txt` is one line:

```cmake
astra_add_plugin(SOURCES isa1976.cpp)     # path is derived from the directory, so a copied aircraft folder needs no edits
```

The helper derives the plugin path from the directory it is called from (so copying `aircraft/_template/` to `aircraft/my_aircraft/` renames every plugin inside it with no edits), sets the output name, visibility, link to `astra_core`, install location, and registers the plugin for the static-plugins build and for the test that loads every plugin once and checks its descriptor.

`embedded/` is inside the git repo and inside the one build for convenience, but it is a separate CMake project that never links `astra_core`. A user's real flight software would live in its own repo and vendor or submodule `icd/` and `embedded/hal/`. The framework's promise to that user is only: *the frame layout and the HAL calls are stable within an ICD version.*

## 5. Time, scheduling, and modes

**Major frame.** Fixed step, default `dt = 1 ms` (1 kHz). Every rate in the system is an integer divisor of the major frame. No variable-step integration in the main loop; adaptive integrators are analysis tools, not the simulator.

**Scheduler.** An explicit, ordered list of `(Model*, divisor)` entries built at init. Each major frame: for each entry whose `frame % divisor == 0`, call `step()`. Order is data, written in the vehicle assembly, and logged, because model ordering is a classic source of one-frame delays that look like physics.

Default order per major frame: log-row reservation when a `log` block is configured (ADR-0025: every due ring reserves the frame's row first, before any side effect, and a full ring halts `kLog` with nothing advanced) → environment (atmosphere, wind, gravity for the frame-start position; wind's shaping-filter state, per ADR-0004/ADR-0016, is a scheduler-driven, seeded stochastic process and is latched once per frame here, never re-sampled mid-integration) → derived flight-condition signals for every physics vehicle (ADR-0026: a separate `icd_pack::pack_sim_to_sw` call at the frame-start state, published as `v<i>.derived.*`; the exchange's own packing is untouched) → transport exchange if due (ADR-0022: at `frame % transport.divisor == 0`, for the bound vehicle (v1: index 0), after the frame's environment sample and before that vehicle's `step()`: pack truth into the `sim_to_sw` frame, publish, block for the reply up to the timeout, validate it, and stage the config-declared command map through `InputRegistry` handles) → each vehicle's own model `step()`s → **receive commands, in the sense ADR-0017 gives that phrase: `InputRegistry::latch()`, once per vehicle, committing whatever a config literal, Python (PR 5), or the exchange stage staged** (a command computed from frame-k state is therefore applied from frame k, with zero compute delay; between exchanges the latched commands hold, §10) → integrator step, which for each of RK4's four sub-stages has the vehicle models (mass, aero, propulsion) accumulate forces and moments fresh at that sub-stage's own state before the EOM derivative (ADR-0016: wrench and the atmosphere sample it reads are pure functions of the stage state and are evaluated at every stage, not once per frame — mass/inertia are re-latched only once per frame, since they are constant in v1, not because per-stage evaluation would be wrong) → post-step (quaternion renormalize) → logger sample, which fills the reserved row with frame-start (t_k) content: the frame-start state, the frame-k environment and derived signals, the latched controls and the k1 wrench (ADR-0025; the derived `ax ay az` are the ICD's previous-frame values, the one disclosed exception, ADR-0026). The rings drain to HDF5 only outside `step()`.

**Clock and modes.** `VirtualClock` holds the integer `frame` counter as the single source of truth; `t = frame × dt` is derived on demand, never accumulated, so time never drifts and log timestamps are exact (*ADR-0010*). Only the scheduler advances it.

- `AsFastAsPossible` — v1 default. Loop with no pacing.
- `RealTime` — pace each major frame on `timerfd` with `CLOCK_MONOTONIC`, thread under `SCHED_FIFO`, memory locked with `mlockall`, overrun counter exposed as a signal. Requires the PREEMPT_RT kernel (`pro enable realtime-kernel`, free for personal use under Ubuntu Pro). Not exercised in v1 beyond compiling and a smoke test on the generic kernel.
- The lockstep transport is orthogonal to the mode: it blocks the scheduler until the external side has answered, in either mode. The wait's timeout is one absolute `CLOCK_MONOTONIC` deadline, the only clock read the transport is allowed (ADR-0023); it never supplies a time value, and its expiry is a halt.

## 6. State, frames, units

**Units.** SI everywhere inside `core/`, `plugins/`, and `aircraft/`: meters, seconds, kilograms, radians, newtons, pascals, kelvin. Conversions happen only at boundaries (config loading, log column metadata, FlightGear packet, ICD fields if the flight software wants feet). Signal names carry no unit suffix; the signal registry carries the unit string as metadata. *ADR-0002.* Rejected: mp-units — compile-time dimensional safety is attractive, but it isn't in the 24.04 repos, complicates nanobind and Eigen interop, and clang-19 is unsupported upstream; revisit if unit bugs actually bite.

**Frames.**

| Frame | Symbol | Definition |
|---|---|---|
| Body | b | Origin at CG, x forward, y right, z down (FRD) |
| Local navigation | n | North-East-Down, tangent to the Earth model's surface at the vehicle |
| Earth-fixed | e | ECEF for round/WGS-84 Earth; coincides with n at the origin for flat Earth |
| Inertial | i | ECI; only used by the rotating-Earth models |

**Attitude.** Unit quaternion `q_nb` rotating vectors from n to b, Hamilton convention, scalar-first `(w, x, y, z)`, renormalized after every integrator step. Euler angles (ψ, θ, φ, yaw-pitch-roll, 3-2-1) are derived outputs only. *ADR-0001* — this is the decision most likely to be silently re-decided by an agent, so it is written first, with a test that pins the sign of every term.

**State vector** (runtime-sized, *ADR-0021*, superseding ADR-0003's fixed-13-layout decision): a
core-owned `DynamicsModel` interface — `state_count()`, `initial_state(double* q)`,
`derivative(t, const double* q, double* dq)`, `post_step(double* q)` (Nutaro's `ode_system`,
ASTRA-flavored) — lets a generic RK4/Euler integrator advance N doubles it knows nothing about,
sized once at init. The rigid-body block is the first `13 + 0` of that buffer, still exactly this
layout, shipped in `core/` as `RigidBody6DofDynamics`, the one `DynamicsModel` every vehicle gets by
default:

```
p_n   (3)  position of CG in the Earth model's native representation
             flat:  NED position from the origin, meters
             round/WGS-84: geodetic latitude, longitude (rad), altitude above ellipsoid (m)
v_b   (3)  velocity of CG relative to the Earth-fixed frame, expressed in body axes (u, v, w)
q_nb  (4)  attitude quaternion
ω_b   (3)  body angular rate relative to inertial frame, expressed in body axes (p, q, r)
```

Any `Model` may additionally declare `n ≥ 0` of its own continuous states (engine spool dynamics,
actuator lag, flex modes, fuel burn) and supply their derivative, at full per-RK4-substage fidelity
(ADR-0016 extends naturally: a model's own state arrives sub-stage-fresh the same way a sub-stage
`VehicleState` already does). `Vehicle` concatenates `[rigid-body block | model A's states |
model B's states | ...]` into one buffer, sized once at init, never resized. `VehicleState` still
views the rigid-body block through the same named accessors (`p_n()`, `v_b()`, `q_nb()`, `omega_b()`)
regardless of what else the combined buffer holds. A state-owning model, and any other model that
needs to read it, gets a sub-stage-exact, read-only pointer into that state (own slice via a
`bind_state()` hook resolved at init; a peer's via `SignalRegistry`, resolved the same "once at
init" way every other signal is) — not a frame-latched copy. Mass and inertia are **not** state in
v1 (constant during a run; fuel burn is a future model-owned state under this same mechanism, not a
core change). The EOM are the standard body-axis rigid-body equations (Stevens & Lewis form) with the
Earth model supplying three things: the gravity vector in n, the Earth rotation rate expressed in n
(zero for flat and round-non-rotating), and the transport rate of the n frame due to motion over a
curved surface (zero for flat). Every Earth model implements that same interface, which is how
"selectable at runtime" costs nothing in the EOM.

## 7. Model interfaces

All framework interfaces are abstract C++ classes with virtual `step`; users subclass. Virtual dispatch cost is irrelevant at these rates and the alternative (templates/concepts) makes the Python side worse, per ADR-0020. Core may offer shared math as **utilities a model may call**, never as a mandatory input struct or a `final` method that fixes behavior — the same rule ADR-0020 states for `core/include/astra/interfaces/` as a whole.

```cpp
namespace astra {

struct StepContext { double t; double dt; std::uint64_t frame; };

struct Wrench { Eigen::Vector3d force_b; Eigen::Vector3d moment_b; };  // about CG, body axes

// temperature_k/pressure_pa/density_kg_m3/speed_of_sound_m_s; NaN by default and outside
// AtmosphereModel::valid_range() (M2 PR 1b) -- a plausible-looking zero-density atmosphere would be
// a worse failure than a NaN. Checked core-side by HealthMonitor::check_environment (ADR-0020)
// before any WrenchSource reads it, not left to each consumer's own arithmetic to surface.
struct AtmoState { double temperature_k, pressure_pa, density_kg_m3, speed_of_sound_m_s; };

// Both bounds inclusive; the interval AtmosphereModel::at() is valid over (M2 PR 1b). Checked once
// at init against the initial altitude (§15) -- not re-validated per frame, the same shape as
// EarthModel/GravityModel's own validate_p_n gap below.
struct AltitudeRange { double min_m, max_m; };

class Model {
public:
  virtual ~Model() = default;
  virtual void init(const Config&, SignalRegistry&) = 0;  // may allocate, may throw
  virtual void reset() = 0;
  virtual void step(const StepContext&) = 0;               // no alloc, no throw, no I/O
  virtual std::string_view name() const = 0;

  // ADR-0021: a model may own n >= 0 continuous states, integrated by the core RK4 integrator at
  // full per-RK4-substage fidelity alongside the rigid-body block -- all defaulted, so every
  // existing subclass keeps compiling and behaving identically without any edit.
  [[nodiscard]] virtual int state_count() const noexcept { return 0; }
  virtual void initial_state(double* q) const noexcept {}
  virtual void integrate_state(double t, const VehicleState& vs, const EnvironmentSample& env,
                                double* dq) const noexcept {}
  virtual void bind_state(const double* own_stage_slice) {}       // init-time; may throw
  virtual void resolve_peers(const SignalRegistry& registry) {}   // init-time; may throw
};

// Environment
class AtmosphereModel : public Model {
public:
  virtual AtmoState at(double altitude_m) const noexcept = 0;
  virtual AltitudeRange valid_range() const noexcept = 0;
};
// wind_n is const noexcept (M2 PR 2, strengthened over this file's own earlier sketch, the same
// way AtmosphereModel::at() was in PR 1b): it is a pure read of whatever begin_frame last latched,
// never itself advancing anything -- a WindModel's own per-frame, per-vehicle evolution (a shaping
// filter's coefficients, which MIL-F-8785C ties to true airspeed and altitude that step() cannot
// see) happens in begin_frame, called once per vehicle by Simulation::step immediately before
// wind_n for that same vehicle. altitude_m is Earth-derived (EarthModel::altitude(), the same value
// AtmosphereModel::at() takes) rather than read from VehicleState::p_n() directly: p_n's meaning
// (NED down vs geodetic altitude, opposite sign) depends on the configured Earth model, the same
// representation trap ADR-0003/PositionRepresentation exists to prevent for gravity models (Codex
// adversarial review). shareable_across_vehicles() declares whether the per-vehicle evolution above
// is even correct under more than one vehicle (ADR-0011); defaults to false.
class WindModel : public Model {
public:
  virtual void begin_frame(const StepContext&, const VehicleState&, double altitude_m) noexcept;
  virtual Eigen::Vector3d wind_n(const StepContext&, const VehicleState&) const noexcept = 0;
  virtual bool shareable_across_vehicles() const noexcept;
};
class GravityModel    : public Model { public: virtual Eigen::Vector3d g_n(const VehicleState&) const = 0; };
class EarthModel      : public Model { /* frames, rotation rate, transport rate, geodetic conversions */ };

// Vehicle
class MassPropertiesModel : public Model { public: virtual MassProps get() const = 0; };

// ADR-0020: minimal marker interfaces -- type identity plus one safe-default override each, no
// compute() step, no core-defined Inputs struct, no final anywhere. A concrete plugin overrides
// WrenchSource::wrench_b(const VehicleState&, const EnvironmentSample&) const noexcept directly.
class AeroModel : public WrenchSource {
public:
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};  // non-final: a plugin MAY override this
  }
};
class PropulsionModel : public WrenchSource {
public:
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};
// Deferred, interfaces reserved: GearModel, ActuatorModel, SensorModel, StructuralModel

}  // namespace astra
```

**Status:** real and built as the marker-interface shape above (ADR-0020, PLAN.md M3 PR 3d,
`docs/decisions/0020-aero-propulsion-true-interfaces.md` — superseding ADR-0018's earlier
`Inputs`-struct/`final`-`compute()` design, PLAN.md M3 PR 3). `AeroModel`/`PropulsionModel` derive
from `WrenchSource`, not `Model` directly. No override of `wrench_b()`, no `compute()`, no
`AeroInputs`/`PropulsionInputs` struct in core — a concrete plugin overrides `wrench_b()` directly,
the pattern `tests/plugins/nesc_aero`/`nan_wrench` already use. `requires_environment()` keeps a
**non-final** safe default (`{wind: false, atmosphere: true}`): an author who never touches it gets
today's safe behavior; only an author who actively overrides it takes on that responsibility
explicitly. **Air-data math is a core utility**, not a mandatory struct: `core/include/astra/util/
air_data.hpp` offers `relative_velocity_b`, `true_airspeed`, `angle_of_attack`, `sideslip`,
`dynamic_pressure`, `mach` as free functions a concrete plugin composes; a plugin may still define
its own plugin-side `Inputs`-shaped struct plus a `compute()`-style split purely as its own
convention for hand-built-fixture testability — `aircraft/notional_transport/physical/aero/
aero_math.hpp` and `aircraft/_template`'s own copy (PLAN.md M3 PR 4) are the concrete demonstration
of this pattern; core no longer prescribes it. **The no-silent-NaN guarantee ADR-0018's `final` used
to provide is restored core-side, not plugin-side**: `HealthMonitor::check_environment` (ADR-0020's
own addition) checks `Vehicle::integrate()`'s per-stage `EnvironmentSample` for NaN/Inf before any
`wrench_b()` call that stage runs, naming the wind/atmosphere plugin on a halt — closing
`EnvironmentSample`'s own previously-disclosed gap directly. The control-input write seam itself —
`InputRegistry`, `WrenchSource::bind_inputs()` — is settled and built (ADR-0017, PLAN.md M3 PR 2);
`AeroModel`/`PropulsionModel` are its first real subscribers, but neither overrides `bind_inputs()`
itself (see below).

**Vehicle** is a composition: one mass-properties model, a list of `AeroModel`s and `PropulsionModel`s whose wrenches are summed. Multiple aero models is deliberate — a user can split wing/tail/fuselage or add a store as a separate contributor without touching the framework. A `MassPropertiesModel` or a `WrenchSource` may additionally own continuous state (ADR-0021, `Model::state_count()` et al.) — a future engine-spool-lag `PropulsionModel`, for example, integrated at full sub-stage fidelity alongside the rigid-body block, with zero further core interface change.

**One vehicle per simulation in v1**, but the door stays open cheaply: `Simulation` holds a list of `Vehicle`s of length one, each vehicle owns its own state vector and signal namespace (`v0.aero.CL`), and the environment models are shared. Multi-vehicle (formation, targets, wake encounters) is a v2 idea Joe wants to keep possible; nothing in v1 may assume there is exactly one vehicle except the CLI's convenience defaults.

**Python-defined models are allowed in v1** through nanobind trampolines on the model base classes (not yet built; not scheduled in PLAN.md M3 — see §12) — for prototyping an aero or propulsion model before porting it to a plugin. They run in `AsFastAsPossible` mode only; the health monitor refuses to start `RealTime` with a Python model loaded.

**Control inputs in v1.** A `WrenchSource` (`AeroModel`/`PropulsionModel`) declares each control it reads as a named, writable input through a new `bind_inputs(InputRegistry&, SignalRegistry&)` hook (ADR-0017), called once per `WrenchSource` at init, inside the vehicle's own signal namespace — `elevator_ = inputs.add(registry, "controls.elevator", "rad")` returns a `const double*` into `InputRegistry`'s own registry-owned storage, also published read-only as `v0.controls.elevator`. A config literal (`vehicle.initial_controls`, this milestone), Python's `sim.write_input()` (PR 5), and M4's transport/`ActuatorModel` all write through the same `write()`/`InputRegistry::latch()` seam — `latch()` runs once per vehicle per major frame (§5), after that vehicle's own `step()` and before the integrator, giving zero-order hold across a frame's four RK4 sub-stages with no one-frame lag for a `step()`-time writer. Two instances of one plugin (e.g. a turbofan's left/right engines) share one value under one name by default. Surface positions and throttle come straight from the ICD command frame with an ideal actuator (commanded = achieved) until M8's actuator models (PLAN.md M8); `ActuatorModel` slots in between then without changing `wrench_b()`'s own `(VehicleState, EnvironmentSample)` signature — ADR-0017 discloses, not solves, whether a non-`WrenchSource` `ActuatorModel` can reach this seam directly.

**Reference plugins shipped under `plugins/`:** `physical/`: ISA-1976 atmosphere; constant-uniform wind and Dryden turbulence (MIL-F-8785C, seeded); gravity: constant, inverse-square, WGS-84 with J2; Earth: flat, round-nonrotating, WGS-84 rotating. `sim/`: loopback transport (built in PLAN.md M4 PR 3, *ADR-0022*) and shm transport (built in PLAN.md M4 PR 4, *ADR-0024*), both described in §10. **PLAN.md M3, built under `aircraft/notional_transport/`:** constant mass properties (PR 1), linear-coefficient aero with stability and control derivatives (PR 4; parameters transcribed/derived from JSBSim's "737" example aircraft, Dave Culp/Aeromatic — still a notional/example airframe, not a validated real 737), and a simple density-ratio thrust-lapse turbofan (PR 4) implementing `AeroModel`/`PropulsionModel` above. `aircraft/_template/` holds stripped-down versions of the same three with the numbers zeroed and marked `TODO`, so a new aircraft starts from something that compiles and trims once its own numbers are filled in. Integrators (Euler for tests, RK4 default) live in the core, not as plugins — they are part of the kernel's contract with the state vector. The framework builds its own reference models through exactly the same `astra_add_plugin` path a user's models take, so the on-ramp is exercised by every build.

## 8. Integration

`Integrator` takes a derivative functor `f(t, y, dy)`, writing into a caller-provided output buffer, over the combined state (ADR-0021: the rigid-body 13-vector plus every state-owning model's own slice, sized once at init, `13` when no model owns state) and advances one major frame. RK4 is default; forward Euler exists so a test can show RK4 is doing its job. Post-step hook renormalizes `q_nb` (`RigidBody6DofDynamics`'s own override) and, per ADR-0021, syncs `Vehicle`'s stage buffer to the newly accepted state.

**Written in-house, not Boost.Odeint.** Fixed-step RK4 over a small state vector is about thirty lines, and owning it buys three things that matter here: a guarantee of zero heap allocation in the step (Odeint's steppers manage temporaries through resizers that are easy to get subtly wrong with Eigen types, and ADR-0021's own runtime-sized combined buffer is sized once at init specifically to keep this true), a convergence-order test that verifies *our* code rather than trusting a dependency, and no template-heavy dependency between the kernel and Python bindings. Boost is in the 24.04 repos (1.83, header-only Odeint), so if the analysis tools later want adaptive steppers (Dormand–Prince for a fast "what does this trajectory look like" preview, symplectic for long-duration orbital checks) they wrap Odeint on the Python-facing side without touching the simulator loop. *ADR-0004* — rejected for the main loop: Odeint; RK45 adaptive (variable step breaks the fixed-rate contract with the flight software); symplectic (irrelevant for a damped aero system).

The derivative function is exposed to Python as `Simulation.derivative(state)` (PLAN.md M3 PR 5, built): single-argument, reading whatever is currently latched in each `WrenchSource`'s own write-seam canonical storage (`Simulation.write_input`, ADR-0017) rather than taking a second `inputs` parameter -- ADR-0017's own PR 5 paragraph leaves the two-argument shape as an equally valid alternative implementation strategy, but PR 5 keeps `write_input`/`derivative` as two separate, independently testable primitives. So trim and linearization can be done by finite differences without a second implementation of the EOM.

## 9. Signals and logging

**SignalRegistry.** At `init`, every model registers the `double`s it wants observable: `registry.add("aero.CL", &CL_, "1")`, with a name, a pointer, a unit string, and an optional rate divisor. The registry is the single data dictionary: logging, the FlightGear bridge, and Python's `sim.signals()` all read it. The hot path never touches strings; the registry resolves everything to pointers at init.

**InputRegistry** (ADR-0017) is `SignalRegistry`'s mutable counterpart, one per `Vehicle`: a `WrenchSource` registers a writable input from its own `bind_inputs()` hook (§7) and gets back a `const double*` into registry-owned storage — the same pointer is also published read-only in the ordinary `SignalRegistry` above, under the identical name. `write()` stages a pending value by name; a repeatable `InputHandle` (from `find()`) does the same for a caller that writes more than once without re-resolving. `latch()`, called once per vehicle per major frame (§5), commits every entry's pending value into its canonical storage, uniformly, never committing a non-finite one — the value every reader's pointer sees keeps its last good value instead.

**Logger.** Samples registered signals into preallocated ring buffers (one per rate group) each frame; a writer drains the buffers to an HDF5 file — inline in `AsFastAsPossible`, from a separate non-RT thread in `RealTime`. One dataset per rate group, columns named by signal, units in attributes, config and git describe stored as file attributes so any log is reproducible. HDF5 through the C API from `libhdf5-dev` with a small internal RAII wrapper (*ADR-0005*; HighFive rejected because it is not in the 24.04 repos). *ADR-0025* pins the rest:
- **Config.** An optional top-level `log: { path, buffer_frames }` block (default 4096 rows). Without it there is no logger and behavior is unchanged. An existing or unwritable path, or an unknown key, is a `ConfigError` naming `log.<key>`. Under *ADR-0027* (built in M6 PR 2), so are an existing `<path>.partial` (a stale partial is the user's and is never touched) and a path ending in `.partial`, and at construction `log.path`'s parent directory is held as a directory descriptor, so a change of working directory cannot redirect the log. Every registered signal is logged.
- **Rows.** Row k describes t_k (§5). The row is reserved at the start of `step()`, before any side effect. `step()` never calls HDF5.
- **Drain.** Only outside `step()`: `run()` between frames at a high-water mark, `Simulation::flush_log()`, and `Simulation::close_log()`. Rows merge oldest-first across rate groups into one 1024-row block per group. `close_log()` writes `/summary` and `/final_state`, sets `complete = 1`, throws a typed error on a failed write, and is idempotent.
- **Finalization** (*ADR-0027*, accepted, superseding ADR-0025's Finalization; built in M6 PR 2). The log is written to `<log.path>.partial`, created exclusively. After every block write of any group, on the drain and close paths alike, the writer flushes and then sets that group's `rows_committed`, so flush points follow the row counts, never the drain pattern. `close_log()` writes the tails, `/writes`, `/summary` and `/final_state`, flushes, sets `complete = 1`, closes the file, checks that the `.partial` is still the file it created (the (device, inode) of a descriptor kept from the create) and publishes it with `renameat2(RENAME_NOREPLACE)`; the successful rename is the publication point. A replaced `.partial`, a file at `log.path`, an unsupported filesystem or any other rename failure throws a `LogError` naming the path, renames nothing and keeps the `.partial`; a later call raises the same error without retrying. A file at `log.path` is one `close_log()` completed and published, and nothing else. The destructor runs the same sequence without the rename and sets `finalized_by = "destructor"` where `close_log()` sets `complete`, so a dropped `Simulation` leaves only the `.partial`. A run killed before the publication point, outside the windows below, leaves nothing, or a `.partial` whose rows below each group's `rows_committed` equal a clean run's, and no `log.path`. A kill during a block write or its flush, during close before the publication point, or during the destructor is ADR-0027's stated limit (HDF5 without SWMR), guarded by `load`'s checks. A kill after the publication point leaves the complete published log at `log.path` and no `.partial`. The kept descriptor is held until the `Simulation` is destroyed. The core deletes a log file in only two cases, each after the identity check: a failing `from_config` call removing the `.partial` it created, and `Simulation::discard_log()` (bound to Python), which works in every state and is the only way a tool deletes its own run's log file.
- **Failure.** A full ring or a failed drain write halts `kLog`, which is terminal and sticky (§15).
- **Layout.** Each rate group is `/rate_<divisor>/frame` (uint64) plus `/rate_<divisor>/values` (float64, rows × signals) with `names` and `units` attributes. Beside them sit `/plugins` (path, category, name, `core_api_version`, `built_with`, and the `.so` SHA-256 or `static`), `/writes`, `/summary` and `/final_state`.
- **Provenance.** The stored config text omits the `log` block, and the file holds no wall-clock value, so two runs of one config give byte-identical files. ADR-0025 records the dataset-and-attribute-equality fallback, should PR 2 show HDF5 cannot give identical bytes. That fallback does not cover ADR-0027's changes, which keep byte identity for every drain pattern and raise `log_format_version` to 2.

**Replay.** `astra.log.load(path)` returns pandas frames per rate group, indexed by `frame`, with `t = frame × dt` derived. It refuses a file without `complete = 1` unless `allow_incomplete=True`. Under *ADR-0027* (built in M6 PR 2), it reports one of three finalization states (complete, destructor, incomplete) and whether the file is published. By default it refuses a `.partial` name, and it cross-checks `final_state.frame` against `frames_run`, the groups' signal names against `/summary`'s, and each `rows_committed` against the group's row count. `allow_incomplete=True` reads a destructor file whole, or only the certified rows of an incomplete one, and refuses any inconsistency with a typed error. Deterministic replay is re-execution (*ADR-0025*): the log stores the config (seeds included), every external `write_input` (frame, name, value), the completed frame count and any halt. `astra.log.replay(path, out)` rebuilds the run from those, makes one further `step()` for a halted log (it must reproduce the halt), and checks `out` against `path` byte for byte. Under ADR-0027, `out` follows `log.path`'s rules, it is published through `close_log()`, and a log that is not complete is refused, naming its state. Two more kinds of log are refused, each with its reason: a run with a `transport` block, since replaying commands without the peer is a different feature, and a log whose halt is `kLog`, an output-side event that depends on the buffer size and the drain calls (neither stored) and that no replay can reproduce.

## 10. ICD and transport — the seam to real software

This section is the one that makes this framework different from a textbook 6DoF, so it gets the most care.

**`icd/astra_icd.h`** (C11, `#include <stdint.h>` only):

```c
#define ASTRA_ICD_VERSION 1u
#define ASTRA_ICD_MAGIC   0x41535452u  /* "ASTR" */

typedef struct {                     /* sim → flight software, "what the aircraft is doing" */
  uint32_t magic, version;
  uint64_t frame;                    /* sim major-frame counter */
  double   t;                        /* sim time, s */
  double   phi, theta, psi;          /* rad   (v1: truth; later: sensor-model outputs) */
  double   p, q, r;                  /* rad/s */
  double   u, v, w;                  /* m/s body */
  double   alpha, beta;              /* rad */
  double   tas, cas, mach;
  double   altitude, vertical_speed; /* m, m/s */
  double   lat, lon;                 /* rad */
  double   ax, ay, az;               /* m/s^2 body */
  uint32_t status_flags, reserved;
} astra_sim_to_sw_v1;

typedef struct {                     /* flight software → sim, "what the aircraft is told to do" */
  uint32_t magic, version;
  uint64_t frame;                    /* echoes the frame it responds to */
  double   elevator, aileron, rudder;/* rad, positive per ADR-0001 sign conventions */
  double   throttle[2];              /* 0..1 per engine */
  uint32_t mode_flags, fault_flags;
} astra_sw_to_sim_v1;

_Static_assert(sizeof(astra_sim_to_sw_v1) == 200, "ICD layout changed: bump version");  /* 8+8+8 + 21*8 + 8 */
_Static_assert(sizeof(astra_sw_to_sim_v1) == 64,  "ICD layout changed: bump version");  /* 8+8 + 5*8 + 8 */
```

Fixed-width types, 8-byte natural alignment, sizes asserted, version in every frame. A change to the layout is a version bump and a new struct, never an edit — the FPGA side may not be rebuilt at the same time. Growing the ICD (sensor-model outputs, more surfaces, discretes) happens by adding `_v2` structs, and the sim can speak the version the other side reports. *ADR-0006.*

**`Transport`** (`core/include/astra/interfaces/transport.hpp`, built in PLAN.md M4 PR 3 under *ADR-0022*) is the sim-side interface, and it owns only what needs the peer or the clock: `icd_version()`, the init-time handshake `open()`, then per exchange `publish()` of the request and its sequence number, `wait_ack()` (every call for one publication shares one deadline, *ADR-0023*), and `read_reply()` to copy the reply, and `close()` when the `Simulation` is destroyed. Core's `LockstepExchange` (`core/include/astra/kernel/lockstep_exchange.hpp`), the exchange stage `Simulation` owns, runs the receive state machine, validates the reply, stages the config-declared command map through `InputRegistry` handles, and makes every halt decision (*ADR-0022*). Implementations:

- **`SharedMemoryTransport`** (v1). `shm_open` + `mmap` of a region holding a header, the two frames, and two 32-bit sequence words. Protocol per exchange: sim writes `sim_to_sw`, publishes `req_seq = frame`, `FUTEX_WAKE`; the flight software waits on `req_seq`, computes, writes `sw_to_sim`, publishes `ack_seq = frame`, `FUTEX_WAKE`; sim waits on `ack_seq` with a timeout. Raw futex words rather than pthread condvars because glibc condvars are not priority-inheritance aware and that matters the day this runs under `SCHED_FIFO`; the same primitive works for the non-RT v1. Refinements (*ADR-0024*): one layout header in `embedded/hal/`, shared by the plugin and the HAL rather than mirrored; both sequence words start at `UINT32_MAX`, so frame 0 cannot collide with a zeroed word, and `UINT32_MAX - 1` is reserved as the shutdown value of `req_seq`; two single-writer readiness words (`sim_state`, `peer_state`) implement a header-only handshake and a `CLOSED` shutdown; an unchanged `ack_seq` means still waiting, never failure. The timeout, not a pid probe, is the liveness detector, and it is one absolute deadline per wait (*ADR-0023*); the peer's wait has its own `peer_timeout_ms` (>= the sim's `timeout_ms`) and lockstep runs are driven by back-to-back `run()`/`step()` calls. *ADR-0007*; iceoryx2 rejected for v1 (still 0.x with breaking releases; one process pair doesn't need a pub/sub broker).
- **`LoopbackTransport`** (v1, tests). Calls a C++ functor in-process. Lets the physics tests run a scripted controller with no second process.
- **`UdpTransport` / `FpgaTransport`** (later). Same frames over the physical link to the FPGA. The link type is the deferred open question.

**Exchange rate.** The transport has its own divisor (default 10 → 100 Hz, a plausible flight-control rate). Between exchanges the sim holds the last commands (zero-order hold). The flight software therefore sees a 100 Hz world and never knows the sim integrates at 1 kHz — which is also true of a real aircraft. `astra_sw_to_sim_v1.throttle[2]` maps to the single shared `controls.throttle` input (ADR-0017): the command map requires the two values equal, and a mismatch halts.

**Exchange recorder (M4's log).** Built before M5's HDF5 logger existed, a preallocated in-memory recorder captures three streams (each `sim_to_sw` frame, each `sw_to_sim` frame, and the rigid-body state at every major frame) and dumps them raw after the run, behind a fixed header (magic, version, stream counts, record sizes). "Bit-identical across two runs" is a byte comparison of that dump plus the final state. M5's HDF5 logger supersedes it (*ADR-0022*). Per *ADR-0025* the recorder stays, because M4's acceptance tests read it; PLAN.md's M10 open item retires it once a lockstep test compares HDF5 logs instead.

## 11. The embedded side: `embedded/`

**`embedded/hal/`** is the reference HAL the example autopilot is written against — three calls: `hal_wait_frame(&in)` (block until the next `sim_to_sw` arrives), `hal_send(&out)`, and `hal_now()` (frame time from the frame, never the wall clock). The wait reports timeout expiry against a driver-supplied `peer_timeout_ms` (>= the sim's `timeout_ms`; the peer cannot tell a dead sim from an idle one, so a sim pause longer than it ends the autopilot nonzero and the sim's next exchange halts `kTransport`; interactive stepping needs a larger value, *ADR-0024*), and there are init and close calls; a peer `CLOSED` state is a clean exit (status 0), never peer death. Backends: `shm` (built on the same shared layout header as the sim's transport, *ADR-0024*, v1) and, later, whatever the FPGA's processor system offers. A user's real flight software either adopts this HAL or maps its own to the same two frame types; the framework only cares about the frames.

**`embedded/autopilot/`** is the v1 example: a ~200-line C++17 program (C++17 deliberately, to stay buildable by an older embedded toolchain) with four PID loops — roll hold → aileron, pitch/altitude hold → elevator, heading hold → roll command, airspeed hold → throttle — and a mode word. It is built by its own `CMakeLists.txt` (pulled into the one top-level build), launched by the Python driver as a subprocess, and knows nothing about `astra_core`. Its acceptance test is the M4 milestone: 10,000 lockstep exchanges (100,000 major frames at the default divisor 10) from a trimmed cruise with zero missed exchanges and altitude/heading held within stated bands under Dryden turbulence.

## 12. Python layer

**Status:** M6's additions below (`docs/plans/m6-analysis-tooling.md`) are each marked with their M6 PR: run-log finalization (PR 2), `astra run` and `astra trim` (PR 3), the bound units (PR 4), `astra.plot` with `astra plot` (PR 5), and the Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze` and `astra mc rerun` (PR 6) are built, and PRs 7 and 8 are *planned*. `astra._core` binds the plugin loader/descriptor types, `StepContext`, `Model`, the state-vector helpers, and, as of PLAN.md M3 PR 5, `Simulation` (`from_config`/`step`/`run`, `write_input`, `derivative`), `Config` (including `from_dict`), `SignalRegistry`/`HealthMonitor`/`VirtualClock`/`Vehicle` read views, and `HaltReason` -- `Simulation.for_testing` and model-base trampolines are deliberately not bound (D2, docs/plans/pr5-nanobind-bindings.md). `astra.trim` is PLAN.md M3 PR 6; `astra.linearize` is PLAN.md M3 PR 7; `astra.log` is PLAN.md M5 PR 5 (on PR 4's run log), `astra.scenario` is M5 PR 6, `astra.montecarlo` is M5 PR 7, and `astra.fgbridge` is M5 PR 8 (`docs/plans/m5-tooling.md`); `astra.lockstep` is M4 PR 6.

- **`astra._core`** — nanobind 3.x module exposing `Simulation`, `Config`, `SignalRegistry`/`HealthMonitor`/`VirtualClock`/`Vehicle` read views, and the derivative function; no model-base trampolines (D2 -- a user cannot yet prototype a model in Python, only select and drive one built as a plugin). Built with scikit-build-core; `.pyi` stubs generated by `nanobind_add_stub` at build so pyright and the LSP see real types.
- **`astra.trim`** — solve for surface/throttle/α/θ that zero the derivative at a flight condition (`scipy.optimize.root` on `Simulation.derivative`).
- **`astra.linearize`** — central-difference A/B matrices about a trim point; returns state-space and the classic longitudinal/lateral modes (short period, phugoid, Dutch roll, roll, spiral) with frequencies and damping. M3's acceptance compares these to the notional transport's design values.
- **`astra.log`** — `load(path)` returns pandas frames per rate group; `replay(path, out)` re-executes a run and checks the new file byte for byte (§9, *ADR-0025*). Finalization states, the `.partial` refusal and the cross-checks follow *ADR-0027* (built in M6 PR 2).
- **Units at the boundary** (built in M6 PR 4) — `core/util/units` has the angle pair beside the foot and knot pairs, and `astra._core` binds the whole set, so every Python display conversion is the core's named function (ADR-0002); `astra.fgbridge` uses it and holds no conversion constant.
- **`astra.montecarlo`** — parameter dispersions from a YAML block, `multiprocessing` fan-out, one HDF5 per run plus a summary parquet. Per-run seeds come from the master seed and the run index only. The summary carries no wall time (it must be byte-identical across worker counts); wall time and real-time factor go to stdout.
- **`astra.scenario`** — YAML: a config, an optional trim, a duration, events (`{at, write, value}`: a write-seam input written through `write_input` at time `at`), checks on logged signals (`{signal, window, <kind>}`, the kind `within`, `between` or `angle_within`) and an optional `expect_halt`; the schema is `python/astra/scenario.py`'s docstring. Executed by a pytest plugin so every scenario in `tests/scenarios/` is a regression test. Events are `write_input` only in M5: wind has no write seam, so a wind change is a config choice, not an event (plan Q14).
- **`astra.fgbridge`** — packs `FGNetFDM` version 24 (big-endian, 408 bytes) from the derived signals (*ADR-0026*) of a running sim or a log, and sends it to UDP 5500 every `divisor` frames. The default divisor is 16, which is 62.5 Hz: 60 Hz is not an integer divisor of 1 kHz (§5). Units are converted at this boundary. A flat-Earth config (`LATLON_NOT_MODELED`) is refused. FlightGear side: `--fdm=null --native-fdm=socket,in,62.5,,5500,udp`, or FlightGear's nearest accepted rate. Wall-clock pacing lives in Python only, is on by default (an unpaced bridge shows the flight in fast-forward, because FlightGear applies the newest packet it has read), is off in tests, and never supplies simulation time.
- **`astra.lockstep`** — drives a lockstep run against a flight-software peer process (ADR-0022, ADR-0024): it owns the shm segment name, launches the peer and reaps it on every exit path, and unlinks the segment; `Session` and `run_closed_loop` are the entry points, and M4's acceptance runs use it.
- **`astra.plot`** (built in M6 PR 5, `docs/plans/m6-pr5-plots.md`) — plot packages. A package is a YAML file of figures, each with panels, each with series; the format is `python/astra/plot.py`'s docstring. A series names a logged signal, optionally with `{v}` for the vehicle index (`v{v}.derived.alpha`, expanded for every vehicle in the log, so a package never hardcodes `v0`), and an optional display unit converted through the bound `core/util/units` (ft, kt, ft/s, deg, deg/s). A conversion the table does not define for the log's unit string is an error. Every map is closed: an unknown key, a duplicate key, an unknown signal or an empty figure is an error naming the file and the key, raised before any file is written. A package may instead name a Python function (an importable module, or a `.py` file beside the package, and a name) that takes a `RunLog` and returns figures. Framework packages live in `python/astra/plots/`; an aircraft's own live in `aircraft/<name>/plots/`, and `aircraft/_template/plots/` carries a starter set, one function-based package included, that `just new-aircraft` copies. Two renderers take the same package. Headless matplotlib writes `<package>-<figure>.svg` per figure, byte-deterministic under a pinned style, the bundled DejaVu Sans and a hash salt fixed per figure, or PNG on request. Plotly writes one interactive `<package>.html` with plotly.js inline, so it opens offline. No output file is ever overwritten.
- **Monte Carlo figures** (M6 PR 6) — from a batch directory: each dispersed parameter's histogram, the draws against each other colored by outcome, and a signal's trajectory envelope across the run logs. At each frame the envelope uses the runs that reached it and shows that count, and the failing runs are overlaid by index. Every matplotlib legend, here and in `astra.plot`, sits outside its axes.
- **`astra.mc_analysis`** (M6 PR 6) — which dispersed parameters drive the outcome. The outcome is either a pass/fail criterion or a continuous summary column. The default criterion is `status == halt`; a custom one is a closed expression over summary columns, never evaluated Python. `error` rows are reported separately, and `seed` dispersions are excluded from rank methods. The methods: a two-sample KS test per parameter with Benjamini-Hochberg across parameters, Spearman and partial rank correlation, and an in-house classification tree of depth at most 3 for interactions. Effect sizes are reported with the p-values. `astra mc rerun` re-executes one run from its recorded seed and draws, with full logging. Its input is the batch's record of its spec and base config, which `astra.montecarlo` keeps in `summary.parquet`'s schema metadata. Plugins come from the current build, so the rerun compares its row, and its log when the batch still holds one, with the batch's.
- **`astra.mc_sizing`** (M6 PR 7, *planned*) — sizes a batch before it runs: the zero-failure bound, Wilks tolerance bounds, a Clopper-Pearson half-width search, and Fisher z for a rank correlation across m parameters with a Bonferroni split, stated as an approximation. After a batch, it shows the estimate and its interval as a function of run count.
- **`astra.report`** (M6 PR 8, *planned*) — one self-contained HTML file per log, scenario or batch: inline SVG, no script, and nothing loaded from outside. It holds the package's figures, the summary, the scenario checks, and the batch's outcomes, sizing check and correlation ranking, with provenance read from the log. The same inputs give a byte-identical file.
- **`astra.cli`** (M6 PR 3 onward; `run`, `trim`, `plot`, `mc analyze` and `mc rerun` built, the rest *planned*) — the `astra` console script. Its subcommands: `run` and `trim` (PR 3), `plot` (PR 5), `mc analyze` and `mc rerun` (PR 6), `mc size` (PR 7), and `report` (PR 8). `astra run` runs a config open loop, closes the log with `close_log()`, prints §15's post-run summary plus wall time and real-time factor, and exits per §15's table. It refuses a config with a `transport` block until M10. `astra plot <log> <package> [--out DIR] [--format svg|png] [--html]` renders a package (a file, or a framework package by name) and exits 64 for a usage error, 65 for a refused package or log or an existing output file, 74 for a failed write. `astra mc analyze` exits the same way, and `astra mc rerun` exits 0 when the run is reproduced and 1 when it is not (`python/astra/cli.py`).

## 13. Configuration

One `Config` tree, loaded from YAML (`yaml-cpp`, apt) or built from a Python dict, validated at init, frozen for the run, and copied into the log. Every selectable component is named by its plugin path, so a run's composition — which Earth, which engine, which IMU — is entirely a configuration choice over the plugins the build produced.

The example below is condensed from `aircraft/notional_transport/configs/cruise.yaml` (PLAN.md M3 PR 1/PR 4) — its explanatory comments trimmed for length, but every key and value identical to the shipped file: every key shown is one `Simulation::from_config` actually reads today — an unconsumed key is a `ConfigError` (§15). Vehicle wrench sources (aero, propulsion — any `WrenchSource`) all register into the one flat `vehicle.models` list; there is no separate typed `physical.aero`/`hardware.propulsion` slot.

```yaml
# aircraft/notional_transport: an example airframe, not a validated real Boeing 737. mass/aero/
# propulsion parameters are transcribed from JSBSim's "737" example aircraft
# (docs/refs/aircrafts/737.xml, gitignored -- not in this repository; $Revision: 1.43 $, authors
# Dave Culp / Aeromatic, GPL). That file's own <note> says it was "created using publicly available
# data, publicly available technical reports, textbooks, and guesses ... meant for educational and
# entertainment purposes only" (Aeromatic/Roskam-style estimates) -- this config carries the same
# caveat forward, not a claim of real-737 fidelity.
sim:
  dt: 0.001
  frames: 1000
vehicle:
  models:
    # Linear-coefficient aero (PLAN.md M3 PR 4, ADR-0020) -- every coefficient transcribed from
    # 737.xml's own <aerodynamics> block; cl_alpha_per_rad/croll_aileron_per_rad/cm_elevator_per_rad
    # are secant-slope/cruise-Mach-evaluated constants, not the source's raw tables (see the plan).
    - plugin: aircraft/notional_transport/physical/aero
      wing_area_m2: 108.78945984       # m^2; 1171 FT2 x 0.09290304
      wingspan_m: 28.864560          # m; 94.70 FT x 0.3048
      mean_chord_m: 3.752088         # m; 12.31 FT x 0.3048
      r_aero_b_x_m: 0.3556           # AERORP relative to CG, body axes (see the plan's own
      r_aero_b_y_m: 0.0              # "Frame-conversion math")
      r_aero_b_z_m: -1.6256
      cl_0: 0.20
      cl_alpha_per_rad: 4.3478260869565215
      cl_elevator_per_rad: 0.2
      cd_0: 0.021
      cd_k: 0.043
      cy_beta_per_rad: -1.0          # JSBSim's own +right SIDE convention, used directly (NOT
                                      # S&L's -C; see the plan's "Force/moment assembly")
      croll_beta_per_rad: -0.09
      croll_p_per_rad: -0.4
      croll_r_per_rad: 0.09
      croll_aileron_per_rad: 0.0743
      croll_rudder_per_rad: 0.01
      cm_alpha_per_rad: -0.6
      cm_q_per_rad: -27.0
      cm_elevator_per_rad: -0.8544
      cn_beta_per_rad: 0.26
      cn_r_per_rad: -0.35
      cn_rudder_per_rad: -0.20
    # Linear thrust-lapse turbofan (PLAN.md M3 PR 4): thrust_ref_n derived from this same config's
    # own cruise drag/thrust equilibrium (no real CFM56 spec exists in docs/refs/), sized so
    # throttle=0.5 balances cruise drag exactly (see the plan's "Worked numbers").
    - plugin: aircraft/notional_transport/hardware/propulsion/turbofan
      station: left
      x_offset_from_cg_m: 2.5146     # engine relative to CG, body axes
      y_offset_from_cg_m: -4.9022
      z_offset_from_cg_m: 0.0
      thrust_ref_n: 29878.121867743088
      density_ref_kgm3: 0.4128
    - plugin: aircraft/notional_transport/hardware/propulsion/turbofan
      station: right
      x_offset_from_cg_m: 2.5146
      y_offset_from_cg_m: 4.9022
      z_offset_from_cg_m: 0.0
      thrust_ref_n: 29878.121867743088
      density_ref_kgm3: 0.4128
  initial_controls:
    controls:
      elevator: 0.026674474137989946   # rad; astra.trim's straight-and-level result
      aileron: 0.0
      rudder: 0.0
      throttle: 0.4985979490507746
physical:
  earth: plugins/physical/earth/wgs84_rotating
  gravity: plugins/physical/gravity/wgs84_j2
  atmosphere: plugins/physical/atmosphere/isa1976
  wind: { plugin: plugins/physical/wind/dryden, seed: 42, w20_knots: 15.0, sigma_high: 3.0 }  # light
  mass:
    plugin: aircraft/notional_transport/physical/mass
    mass: 37648.0    # kg; 83000 LBS x 0.45359237
    ixx: 761970.0    # kg*m^2; 562000 SLUG*FT2 x 1.35581795
    iyy: 1997120.0   # kg*m^2; 1.473e6 SLUG*FT2 x 1.35581795
    izz: 2567919.0   # kg*m^2; 1.894e6 SLUG*FT2 x 1.35581795
    ixz: -10846.5    # kg*m^2; S&L product-of-inertia convention -- see the plugin's own header
                     # comment and the config's own inline comment for the sign derivation.
initial:
  state:
    p_n: [0.0, 0.0, 10000.0]   # wgs84_rotating: lat (rad), lon (rad), altitude (m)
    v_b: [229.9360238725, 0.0, 5.42447469393253]  # m/s, body axes; TAS 230 at trim alpha
    euler_nb: [0.0, 0.023586859573503434, 0.0]    # [psi, theta, phi] rad; heading 0, theta = alpha
    omega_b: [0.0, 0.0, 0.0]
```

`vehicle.initial_controls` (ADR-0017) declares each write-seam input's config-literal value as ordinary nested YAML under the input's own relative dotted name — no `v0.` prefix, matching `physical.mass`'s own existing unprefixed-key convention. Validated finite at init (a `ConfigError` naming the input's own key, never a silent default) and applied before frame 0 ever runs.

An aircraft folder can carry several configs (cruise, cruise with turbulence, a variant with a different engine), and any config can point at a plugin in another aircraft's folder or in `plugins/` — paths are paths.

Swapping one propulsion model for another between runs is a one-line change to this file. The loaded plugin set is recorded in the log so a result is always traceable to the exact components that produced it. A plugin descriptor has no version field, so the identity recorded per plugin is its path, category, name, `core_api_version`, build string, and the SHA-256 of the loaded `.so` (*ADR-0025*).

## 14. Rules that hold everywhere in `core/`, `plugins/`, and `aircraft/`

- **Per-frame path** (`step`, `wrench_b`, integrator, transport exchange, logger sample): no heap allocation, no exceptions, no blocking I/O, no `std::string` construction, no `dlopen`/`dlsym`, no virtual calls into user code beyond the documented interfaces. Enforced by review and by a `-fsanitize=address` run with allocation counting in the physics tests; a test asserts zero allocations across 1000 frames.
- **Init path**: may allocate and throw; every error is a typed exception with the config path that caused it.
- **Threads**: the scheduler is single-threaded. The only second thread in the framework is the HDF5 writer in real-time mode. Models are never called concurrently.
- **Floating point**: `double` everywhere; no `-ffast-math` (determinism); Eigen consumed as a `SYSTEM` include so its C++20/23 deprecation warnings don't fight `-Werror`.
- **Randomness**: `std::mt19937_64` (its output sequence is specified by the standard) with in-house Box–Muller / uniform transforms, because `std::normal_distribution` and friends are *not* specified and differ between libstdc++ and libc++. Every stochastic plugin takes a seed from config; the seed is logged.
- **Style**: Google C++ style via `.clang-format`; `snake_case` functions and variables, `CamelCase` types, trailing-underscore members, `.hpp`/`.cpp`. `clang-tidy` enables `bugprone-*`, `performance-*`, `modernize-*`, `readability-*` minus `readability-identifier-length` (which fights physics notation like `q`, `p`, `dt`). Python: `ruff` for lint and format, `pyright` strict on `python/astra/`.
- **Errors in models at step time** are reported through status flags in the signal registry, not exceptions; the scheduler decides whether to halt based on config.

## 15. Health monitoring

A sim that produces a plausible-looking trajectory from a broken setup is worse than one that refuses to run. `core/kernel/HealthMonitor` runs three kinds of checks, each a registered signal so it lands in the log, with a configured action per check: `warn` (log and continue), `halt` (stop the run with a typed reason), or `off`. Defaults are conservative; a config can loosen them per run and the loosening is recorded in the log.

**Pre-run (init), all `halt` by default:**
- Config validated against every loaded plugin's declared parameters; unknown keys are errors, not warnings.
- Every plugin's descriptor checked: core API version, build string, ICD version for transports.
- Initial state sanity: `|q_nb| = 1` within 1e-9; mass > 0; inertia tensor symmetric positive-definite; altitude inside the atmosphere model's valid range; airspeed inside the aero model's declared envelope; no NaN/Inf anywhere in state or parameters.
- Trim residual (when `initial.trim` is used; *planned*, no config key yet: trim is `astra.trim`, in Python) below the configured threshold, and the trim solution inside surface and throttle limits.
- Transport handshake: header-only (ADR-0024), with no probe frame: the peer checks the segment size, waits to observe `SIM_READY`, then validates ICD magic, version, and layout size, and both readiness states are published, all within one `handshake_timeout_ms` before frame 0. A failure is an init error naming `transport`.
- Timing budget (RealTime mode only): a warm-up of 1000 frames with the log disabled measures worst-case frame cost; halt if it exceeds a configured fraction of `dt`.
- Determinism guard: `-ffast-math` absent, seeds present for every stochastic plugin, no wall-clock, sleep, timer or timed-wait API referenced by any object of `core/`, `plugins/`, `aircraft/` or `embedded/`. `just wallclock-scan <preset>` (`scripts/wallclock_scan.py`, in `just check` for every preset after that preset's build) runs `nm -u -C` on the object of each of those translation units and fails, naming the source file and the symbol, on an undefined reference in one of these anchored families. C and POSIX, matched against the unmangled symbol: `clock_*`, `time`, `clock`, `timespec_get`, `*timeofday`, `ftime`, `*sleep`, `alarm`, `ualarm`, `*itimer`, `timer_*`, `timerfd_*`, `*timedwait`, `*timedlock`, the glibc `*clockwait` and `*clocklock` forms and the timed join, semaphore and message-queue calls, and `select`, `pselect`, `poll`, `ppoll`, `epoll_wait`, `epoll_pwait`. libstdc++, matched against the demangled name: anything in `std::chrono::` ending in `::now()`, anything in `std::this_thread::`, and `wait_for`, `wait_until`, `try_lock_for` and `try_lock_until` on any `std::` type (on this toolchain `sleep_for` and `wait_for` end in `nanosleep` and `pthread_cond_clockwait`, which the C families catch). Lookalikes (`strtod`, `H5Pset_obj_track_times`, time-named functions in our own namespaces) do not match. It allowlists, by source file and by symbol, only what ADR-0023's two deadline sites reference (one function in the shm transport, one in the HAL's shm backend), and fails on a stale allowlist entry, a missing object, or a scan that finds no in-scope translation unit. It is a tripwire for the ordinary calls, not a proof. Known blind spots: a raw `syscall()` (not flagged: too broad, and both sites use it for the futex), inline assembly and intrinsics (`rdtsc`), vDSO or function-pointer indirection (`dlsym`), and calls from third-party libraries outside the scanned objects; each ADR-0023 site's use of the clock stays a review matter. `tests/python/test_wallclock_scan.py` compiles one call per family into a throwaway tree and expects the scan to fail on it, and checks the lookalikes.

**During run (every major frame, cheap; `halt` unless noted):**
- NaN/Inf in the state vector or in any wrench contribution, with the offending plugin named.
- NaN/Inf in a written control input (ADR-0017's `InputRegistry::latch()`), naming the input itself, never the consuming `WrenchSource`: every OTHER declared input still commits that same frame, and the bad one keeps its last good value under `warn`.
- Quaternion norm drift beyond 1e-6 before renormalization (indicates integrator or rate problems).
- Rate and acceleration limits: |ω| and |a| beyond configured physical bounds (`warn` by default).
- Envelope monitors from the aero model's declared limits: α, β, Mach, dynamic pressure, load factor (`warn` by default; the notional transport declares its own).
- Transport health (`HealthCheck::kTransport`): unconditionally `halt`, with no `warn` or `off` action, because a run that continues past a missed lockstep exchange is no longer meaningful and wall-time expiry must never change a trajectory. Failures: timeout, out-of-order or unexpected `ack_seq`, echoed-frame mismatch, bad magic or version, frame-counter discontinuity, peer closed or gone, sequence range exhausted (`frame >= UINT32_MAX - 1`, checked at every publication; `UINT32_MAX` is the sentinel and `UINT32_MAX - 1` the reserved shutdown value), a non-finite command, a command outside its configured limit (no clamping), and a `throttle[2]` mismatch; the last three name the ICD field. The exchange stage checks finiteness itself, before limits and before writing any input, so the halt holds under every `sim.health.input` setting (that check is ADR-0017's and honors `warn`/`off`; it stays a backstop). The clock does not advance on a transport halt (ADR-0022).
- Real-time overruns (RealTime mode): frames late by more than a configured margin, counted and, above a threshold, `halt`.
- Log ring-buffer overflow (`halt`; a silently truncated log is not a log). Per *ADR-0025* this is `HealthCheck::kLog`, appended after `kTransport`. It is unconditionally terminal, with no `warn`/`off` and no `sim.health.log` key, and sticky, with or without a transport. It fires when a ring has no free row, checked at the start of `step()` before any side effect so the clock and state do not advance, and when a drain's HDF5 write fails.
- Allocation detector in debug and sanitizer presets: any heap allocation in the per-frame path fails the test suite (`tests/physics/no_alloc_test.cpp`).

**Post-run:** a summary block written to the HDF5 file by `close_log()` and printed by the CLI: frames run, the halt (check, source, frame) if any, every health counter, and min/max of every logged signal. The plugin set, with the identity §13 describes, sits beside it in `/plugins`. Envelope min/max waits for the envelope monitors above. Per *ADR-0025*, wall time and real-time factor are **not** in the file: core cannot measure them under ADR-0010, and they would break byte identity. The Python tools print them to stdout. The scenario harness runs strict.

**`astra run` exit codes** (built in M6 PR 3, `python/astra/cli.py`). The first matching row wins:

| Code | Condition |
|---|---|
| 64 | usage error (`EX_USAGE`) |
| 65 | a config or plugin error, including a `transport` block, which is refused until M10 (`EX_DATAERR`) |
| 74 | the log could not be written, closed or published, or the summary could not be written to stdout (`EX_IOERR`) |
| 70 | an internal error, a defect, with its traceback on stderr (`EX_SOFTWARE`) |
| 1 | a halt of any check, `LOG` included; a halt takes precedence over warnings |
| 2 | `--strict`, no halt, and a nonzero `warn` counter |
| 0 | otherwise |

The monitors are themselves testable: `tests/unit/health/` feeds a NaN and a broken quaternion (and a non-finite wrench, input or environment, and an out-of-domain state) into the monitor and asserts the right check fires with the right name; a dead peer is `tests/python/test_lockstep.py`'s R4; an overrun waits for `RealTime` mode (M11). A health check nobody has seen fire is not known to work.

## 16. Verification map

| Milestone | What must pass (all under `just check`) |
|---|---|
| M0 | Core and one dummy plugin build under all presets; the loader loads the dummy by its plugin path, rejects a bad `core_api_version`, and the static-plugins build runs the same test without `dlopen`; unit test scaffold; hooks fire; LSPs resolve symbols; GitHub Actions runs `just check` on a pull request against an `ubuntu-24.04` runner and the branch protection requires it; ADR-0001…0011 accepted; `icd/` static asserts hold under both clang-18 and `gcc -std=c11` |
| M1 | Torque-free rigid body: energy and angular momentum drift < 1e-9 relative over 1000 s; quaternion norm within 1e-12; ballistic arc matches closed form; RK4 vs Euler convergence order demonstrated; zero allocations over 1000 frames; health monitor halts on an injected NaN and on a broken quaternion, naming the source |
| M2 | NASA check cases 1–10 (dropped/tumbling sphere and brick, flat / round / rotating / WGS-84 Earth, steady and varying wind) within the tolerances the NESC data provides; ISA table points exact; Dryden PSD matches spec within statistical bounds |
| M3 | Notional transport (an example aircraft's mass/aero/propulsion values, not a validated real 737) trims at cruise (10,000 m, 230 m/s TAS) with residual < 1e-6; linearized modes are sanity-checked against S&L §4.2's classical approximations with loose, labeled per-mode tolerances, not an independent oracle; a 300 s open-loop run matches its own frozen regression trajectory, independently cross-checked against the linearized model's phugoid prediction |
| M4 | Autopilot process: 10,000 lockstep exchanges (100,000 major frames at divisor 10), zero missed; altitude within ±30 m and heading within ±2° at every major frame, at each of two gating Dryden levels at 10,000 m (MIL-F-8785C Fig. 7, read by Joe): light (σ 0.15 m/s, W20 15 kt) and moderate (σ 1.5 m/s, W20 30 kt); byte-identical exchange-recorder output across two runs; killing the autopilot mid-run produces a transport-health halt within one timeout (plus a wake/measurement allowance, plan Q3), not a hang or a garbage trajectory |
| M5 | Scenario harness runs every YAML in `tests/scenarios/`; Monte Carlo of 64 dispersed runs completes; FlightGear receives valid packets (checked by a packet decoder test, not by launching FlightGear) |
| M6 | Plot packages render every series equal to the log column after conversion, fail loudly before drawing, give byte-identical SVG across processes, and travel with a `just new-aircraft` copy; Monte Carlo figures match `numpy` over the runs that reached each frame; a planted cause ranks first and survives Benjamini-Hochberg (synthetic, and in the notional transport with a negative control), a planted interaction is found as a conjunction, PRCC is partial, and false positives stay within the binomial allowance; the sizing formulas meet their oracles; the report holds every section and is byte-identical across processes; `astra run` fires every exit code and the other subcommands run; finalization per ADR-0027: the lifecycle, kill tests giving a correct row prefix or a typed refusal, nothing overwritten, the cross-checks, byte identity across every drain pattern and replay, and one cleanup rule; the angle conversions are bound and `astra.fgbridge` holds no constant; the benchmark asserts its frames ran before timing, and its smoke checks fields and frame count, never a speed |

M5 note (rev 9): the M5 row is checked by `docs/plans/m5-tooling.md` R1 to R21, on top of the HDF5 run log and replay (*ADR-0025*) and the derived signals (*ADR-0026*). The 64-run Monte Carlo runs inside `just check`; if it cannot fit, that is an architecture exception for Joe to decide, not a move out of the gate. The packet decoder is built from a transcribed `FGNetFDM` v24 table, not from the packer, and packets go out at the bridge divisor (§12, 62.5 Hz by default).

M6 note (rev 10): the M6 row is partly built: PRs 1 to 6 are built, and the rest is *planned*. `docs/plans/m6-analysis-tooling.md` R1 to R21 (with R3b, R7b, R16b, R18b and R19b) check it, PR by PR. Its finalization rows rest on *ADR-0027*, accepted by Joe 2026-09-29. Tracking the benchmark over time is M11.

## 17. ADRs to write in M0

| # | Decision | Alternatives to record as rejected |
|---|---|---|
| 0001 | Frames and attitude: FRD body, NED nav, `q_nb` Hamilton scalar-first, 3-2-1 Euler derived | ENU nav; JPL quaternion; scalar-last; Euler as state |
| 0002 | SI doubles, units as signal metadata, conversions at boundaries | mp-units; imperial internally; per-signal unit types |
| 0003 | 13-state vector layout with position representation owned by the Earth model | ECEF position for all Earth models; mass as state |
| 0004 | Fixed-step RK4 at 1 kHz written in-house, Euler retained for tests; Odeint allowed only in analysis tools | Boost.Odeint in the main loop; RK45 adaptive; symplectic; 500 Hz default |
| 0005 | HDF5 via C API with internal RAII wrapper | HighFive (not in apt); Parquet (no C++ writer in apt); CSV |
| 0006 | ICD as versioned C11 structs, additive `_vN` evolution | Generated headers from a schema (revisit at v2 when the FPGA side exists); protobuf/flatbuffers |
| 0007 | Shared-memory lockstep with raw futex sequence words | pthread condvars (no PI); named semaphores; iceoryx2; UDP loopback |
| 0008 | Monorepo with `embedded/` as a separate CMake project inside it | Separate repo from day one |
| 0009 | Plugin architecture: one core library, one `.so` per selectable component, all built in one CMake invocation, loaded by path at init, static-link option for RT builds. Two roots: `plugins/` (shared, by kind) and `aircraft/<name>/` (one folder per aircraft, same kinds inside, copied from `_template`); no `vehicle/` kind | Static factory compiled into the core (users must edit the framework build); header-only/template models (no runtime swap, worse Python story); importing prebuilt `.so` files (ABI drift, not wanted); a single by-kind tree with aircraft scattered across it (nothing a user can call "mine"); a `vehicle/` kind alongside physical/hardware (wrong axis) |
| 0010 | Integer frame counter is time's source of truth; `t` derived, never accumulated | Accumulated double seconds; integer nanoseconds |
| 0011 | One vehicle per simulation in v1, structured as a list of one so multi-vehicle is a v2 extension not a rewrite | Single hard-wired vehicle; full multi-vehicle now |

## 18. How a user adds an aircraft (the on-ramp this design must keep true)

1. `just new-aircraft my_aircraft` — copies `aircraft/_template` to `aircraft/my_aircraft`, rewriting both the copied configs' plugin paths and the copied `_math.hpp`/`.cpp` files' aircraft-specific C++ namespace (`scripts/new-aircraft.sh`'s own header comment has why: two aircraft sharing one namespace is a silent link-time One Definition Rule violation under `ASTRA_STATIC_PLUGINS` the moment either one's math is edited to differ). A plain `cp -r` skips both rewrites and is not a supported route.
2. Implement `MassPropertiesModel::get`, and override `wrench_b(const VehicleState&, const EnvironmentSample&)` directly on `AeroModel`/`PropulsionModel` (ADR-0020 — no `compute()` step, no core-defined `Inputs` struct), in `physical/aero/`, `physical/mass/`, `hardware/propulsion/` inside that folder. Compose whatever air-data math is needed from `core/include/astra/util/air_data.hpp`'s free functions; a plugin-side `Inputs`-shaped struct plus its own `compute()`-style split remains an available convention (the template demonstrates it) for unit-testing with hand-built values and no simulator, now a plugin-side choice rather than a core-imposed shape. A model needing its own continuous state (engine spool dynamics, actuator lag) declares it via `Model::state_count()`/`initial_state()`/`integrate_state()` (ADR-0021) — no framework file is edited for this either.
3. `just build` — the build discovers the new folder and emits its `.so` files. No framework file is edited. The copy's own `tests/` directory (the template's mass, aero and turbofan tests) builds and runs under `ctest` too: its `CMakeLists.txt` ends with `astra_add_plugin_test(<target>)`, which is how a test executable that loads plugins by path gets them built first, so an aircraft's own tests need no entry in `cmake/AstraPlugin.cmake` either.
4. `astra trim aircraft/my_aircraft/configs/cruise.yaml` gives the cruise condition; `astra run` flies it.
5. Build their flight software against `icd/` and `embedded/hal/`; add a `transport` block (`plugin: plugins/sim/transport/shm`, its command map) to the config and run the software as the peer, as `astra.lockstep` does for `embedded/autopilot`.

If any step needs more than that, it is a framework bug.

## 19. Open questions carried forward

- The sim ⇄ FPGA physical link and its framing (UDP, PCIe, other) — decides `FpgaTransport`.
- How the flight software is expressed on the FPGA (C++ on the processor system vs HLS/RTL) — decides whether one source tree can build both the digital-sim process and the FPGA target, or whether the digital sim runs a C++ twin.
- Sensor models and where their outputs replace truth in `sim_to_sw` (ICD v2).
- Actuator models between the command frame and a `WrenchSource`'s own `wrench_b()` read of a control.
- Fuel burn and time-varying mass properties (mass becomes state) — ADR-0021 makes this
  *representable* (a `MassPropertiesModel` could declare one state slot for burned fuel mass) without
  deciding whether v1 uses it; `MassPropertiesModel::get()`'s signature and `Eom::begin_frame()`'s
  per-frame caching still need their own explicit decision if/when a record actually adds it.
- Landing gear / ground reaction when takeoff and landing enter scope.
- Whether to generate `icd/` from a schema once a second consumer (the FPGA) exists.

## Platform facts this document relies on (verified 2026-09-14)

Ubuntu 24.04 apt: clang/clangd/clang-tidy/clang-format 18.1.3, CMake 3.28.3, Python 3.12.3, Eigen 3.4.0, HDF5 1.10.10, GoogleTest 1.14.0, yaml-cpp 0.8.0, `just` 1.21.0; HighFive and mp-units are not packaged. PREEMPT_RT: `pro attach && pro enable realtime-kernel` (kernel 6.8 RT for 24.04 GA; free for personal use on up to 5 machines). nanobind 3.0.1 with `nanobind_add_stub` and scikit-build-core support. NASA/TM-2015-218675 atmospheric check cases 1–10 need no aircraft model; data at nescacademy.nasa.gov/flightsim/2015 and vendored in `nasa/simupy-flight`. FlightGear `FGNetFDM` version 24, big-endian, UDP. Eigen 3.4 under clang-18/C++23 emits enum-conversion and `float_denorm_style` deprecation warnings — use `SYSTEM` includes.
