// One vehicle's own state, models, and schedule (docs/architecture.md §7, ADR-0011): a Simulation
// holds a list of these of length one in v1, but nothing here assumes that -- each Vehicle owns
// its own StateVector and its own signal namespace ("v0.aero.CL").
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/eom.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/scheduler.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

class Vehicle {
 public:
  // One entry of vehicle.models (docs/architecture.md §13): the plugin to load, its own config
  // subtree (the map that carries its "plugin:" key, so a model's init can read its own
  // parameters next to it), and the scheduler rate divisor for this model.
  struct ModelSpec {
    std::string plugin_path;
    Config config;
    std::uint32_t divisor;
  };

  // Physics for this vehicle (PLAN.md M1 PR 3): the vehicle's own mass-properties model spec
  // (ADR-0011: mass properties are per-vehicle, loaded under this vehicle's PrefixScope like any
  // other vehicle.models entry) plus the shared earth and gravity models Simulation owns
  // (ADR-0011: environment models are shared across vehicles). init() constructs this vehicle's Eom
  // from all three; null when a config has no "physical" section (PR 2's dummy-plugin fixture keeps
  // running with no physics, exactly as before). earth_plugin_path/gravity_plugin_path (ADR-0012)
  // are what a domain halt names -- Eom retains the pointers (via its own earth()/gravity()
  // accessors) but not a name, the same reason mass's own plugin_path travels separately from its
  // ModelSpec everywhere else in this file.
  //
  // atmosphere (PLAN.md M2 PR 3c, docs/plans/pr3c-substage-wrench.md): nullable, mirroring
  // Simulation::atmosphere_ -- non-null only when physical.atmosphere is configured. Lets Vehicle
  // recompute a fresh atmosphere sample at each RK4 sub-stage's own altitude (via eom_->earth(),
  // already held) for wrench evaluation, without a second EarthModel* member. This struct and
  // Vehicle::init are technically public C++ API, but are never part of the versioned plugin ABI:
  // constructed only by Simulation::from_config/for_testing, both compiled inside libastra_core
  // itself, never across the dlopen loader boundary a Model-derived plugin crosses -- see
  // core/include/astra/core_api.hpp.in's own bump policy and docs/plans/pr3c-substage-wrench.md's
  // "kCoreApiVersion" section for the full reasoning (Codex adversarial review, PR 3c plan
  // critique).
  struct PhysicsSpec {
    ModelSpec mass;
    EarthModel* earth;
    GravityModel* gravity;
    std::string earth_plugin_path;
    std::string gravity_plugin_path;
    AtmosphereModel* atmosphere = nullptr;
    // ADR-0020: what a per-stage domain-pre-check or report_non_finite_environment halt names on a
    // broken or out-of-range atmosphere sample. Empty when atmosphere is null -- never actually
    // named, since both checks are gated on atmosphere_ != nullptr first. Not threaded alongside
    // wind's own path (see init()'s own wind_plugin_path parameter below): wind is a separate,
    // independently- optional environment model Simulation::from_config already threads through
    // EnvironmentAvailability alone, never through this physics-specific, required-or-absent-
    // together bundle.
    std::string atmosphere_plugin_path;
  };

  // Which optional environment slots Simulation::step will actually populate into EnvironmentSample
  // this frame (environment_sample.hpp). init() rejects a WrenchSource whose requires_environment()
  // asks for wind or atmosphere when the matching field here is false, naming physical.wind or
  // physical.atmosphere respectively (M2 PR 3: atmosphere joins wind here once
  // EnvironmentSample::atmosphere is actually populated -- see init()'s own comment for the
  // rejection this replaced, unconditional before this field existed).
  struct EnvironmentAvailability {
    bool wind = false;
    bool atmosphere = false;
  };

  // name is the signal-namespace prefix ("v0"); state starts zeroed until init()'s caller sets it
  // (Simulation::from_config reads "initial.state", or leaves it zeroed when physics is off).
  Vehicle(std::uint32_t index, std::string name);

  // Loads and initializes each model in models, in order, adding it to this vehicle's Scheduler,
  // then -- when physics is non-null -- loads this vehicle's own mass-properties model the same way
  // and constructs this vehicle's Eom against it and the shared earth/gravity models physics names.
  // Every model's init() runs under a SignalRegistry::PrefixScope for this vehicle's name, so
  // "dummy.steps" is published as "v0.dummy.steps" (ADR-0011). As each WrenchSource is discovered,
  // checks its requires_environment() (wrench_source.hpp) against availability: a wind requirement
  // with availability.wind false throws ConfigError naming physical.wind; an atmosphere requirement
  // with availability.atmosphere false throws ConfigError naming physical.atmosphere -- the same
  // shape as wind, since M2 PR 3 populates EnvironmentSample::atmosphere the same way it already
  // populates wind_n (before this PR, the atmosphere check was unconditional: no PR could satisfy
  // it, since nothing connected the two yet). Init-time only: may allocate, may throw
  // (PluginError, ConfigError).
  //
  // wind_plugin_path (ADR-0020): what report_non_finite_environment names on a non-finite wind_n
  // halt, kept as its own parameter rather than a PhysicsSpec field (wind is independently
  // optional, unlike the physics bundle PhysicsSpec represents -- see
  // PhysicsSpec::atmosphere_plugin_path's own comment). Ignored (and should be empty) when
  // availability.wind is false.
  void init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
            SignalRegistry& registry, const PhysicsSpec* physics,
            EnvironmentAvailability availability, std::string wind_plugin_path = {});

  void step(const StepContext& ctx) noexcept { scheduler_.step(ctx); }

  // This frame's environment sample (docs/architecture.md §5), latched by Simulation::step before
  // step()/integrate() run (environment_sample.hpp). Defaults to still air / all-NaN atmosphere,
  // matching a Simulation with no physical.wind/physical.atmosphere configured.
  void set_environment(const EnvironmentSample& sample) noexcept { environment_ = sample; }
  [[nodiscard]] const EnvironmentSample& environment() const noexcept { return environment_; }

  // Runs this frame's wrench accumulation, EOM derivative, integrator step, and post-step
  // renormalize (docs/architecture.md §5): a no-op returning a falsey HaltReason when this vehicle
  // has no Eom (physics off) -- a no-physics vehicle has no Earth model and no position
  // representation to be out of domain in, so ADR-0012's check does not apply to it either; that
  // is this early return's job already, not something to special-case below. Order: (ADR-0012)
  // domain-check the PRE-step state, but only on frame 0 -- every later frame's pre-step state is
  // the previous frame's already-checked post-step state (see check_domain's own comment), so this
  // is the one call that makes the post-step-plus-lookahead invariant hold from the very first RK4
  // stage the integrator ever evaluates.
  //
  // PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md): wrench_b() and the atmosphere sample it
  // reads are evaluated fresh at each of Integrator's four RK4 sub-stages, from a derivative
  // functor local to this method that wraps Eom (itself unchanged) -- not once at frame-start as
  // before. Each stage's contribution and total are still health-checked (health.check_wrench),
  // uniformly, every stage, via a first-fault-wins HaltReason side channel (the functor cannot
  // itself return one; StateDerivative fixes the return type to StateVector) read back after
  // integrator.step() returns. A wrench fault still leaves state() exactly as if the frame never
  // ran -- previously true by construction (the wrench was checked before integrator.step was ever
  // called); now true by an explicit revert to the saved pre-step state, since integrator.step()
  // always runs all four stages to completion first (RK4 has no early-abort hook). The EOM does see
  // a bad wrench transiently in that case, inside a step whose result is discarded, never returned
  // from state(). health-check the resulting state before renormalization (PLAN.md M1 PR 4: "|q|
  // drift > 1e-6 before renormalization"); domain-check the POST-step state with the one-frame
  // lookahead, skipped when the state check already halted; renormalize unconditionally, halt or
  // not (ADR-0001: renormalized after every integrator step, an invariant a halt does not suspend);
  // return whichever check halted (the state check wins over the domain check when both would
  // fire the same frame). Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason integrate(const Integrator& integrator, const StepContext& ctx,
                                     HealthMonitor& health) noexcept;

  // ADR-0017: restores every model's own reset() (dryden.cpp's own "zero every mutable member"
  // precedent) alongside inputs_.reset() (restoring every write-seam entry to its config-literal
  // init value, or to its NaN default if freeze() never ran). Relative order between the two does
  // not matter: a conforming WrenchSource's own reset() is prohibited from writing through a cached
  // InputHandle (wrench_source.hpp), so nothing can interfere with inputs_.reset()'s own restore
  // regardless of which runs first. Nothing calls this yet, matching Model::reset()'s own current,
  // uncalled status -- a forward-looking hook, wired up end to end by a later milestone (M5's Monte
  // Carlo driver).
  void reset() noexcept;

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] StateVector& state() noexcept { return state_; }
  [[nodiscard]] const StateVector& state() const noexcept { return state_; }
  [[nodiscard]] const Scheduler& scheduler() const noexcept { return scheduler_; }
  // ADR-0017: this vehicle's own write seam. Simulation::from_config applies vehicle.
  // initial_controls's config literals and calls freeze() through this accessor; Simulation::step
  // calls latch() through it once per vehicle per frame, after step(), before integrate() (§5).
  [[nodiscard]] InputRegistry& inputs() noexcept { return inputs_; }
  [[nodiscard]] const InputRegistry& inputs() const noexcept { return inputs_; }

 private:
  // A vehicle.models entry that resolved to a WrenchSource, kept alongside the plugin path its
  // config named it by (§15: "the offending plugin named") -- Scheduler::Entry only stores Model*.
  struct WrenchEntry {
    WrenchSource* model;
    std::string plugin_path;
  };

  // ADR-0012: domain-checks state at p_n and the one-frame lookahead p_n + dt * p_n_dot(state),
  // against both Eom's earth() and gravity() (each may be the first to fail; earth is checked
  // first, matching Simulation::from_config's own earth-before-gravity validate_p_n order), and
  // reports through health.report_out_of_domain naming whichever plugin failed. Called from
  // integrate() both pre-step (frame 0 only) and post-step (every frame); factored out to stay
  // under clang-tidy's readability-function-cognitive-complexity threshold on integrate() itself.
  // Per-frame path: no allocation, no exceptions. Only ever called when eom_.has_value() and
  // health.domain_enabled().
  //
  // Known gap, disclosed not fixed (Codex adversarial review, M2 PR 2c; PLAN.md's own line has
  // the full account): the lookahead above is first-order in velocity, but the Integrator's RK4
  // stages (integrator.hpp) nest ACCELERATION into a position estimate at 0.5 * |a| * dt^2 (~4.9
  // um at standard gravity and ADR-0010's dt = 1 ms, pinned against the real Eom+Integrator by
  // tests/unit/kernel/simulation_halt_test.cpp's
  // OneRk4StepUnderConstantGravityMatchesTheClassicalFallingBodyFormula so this scales visibly, not
  // silently, with dt). A state within that band of a boundary, with near-zero velocity toward it,
  // can have an RK4 stage evaluate the singular math before this check ever runs -- ADR-0012's own
  // Rejected Alternatives names this class ("a domain a single frame can jump clean across"), shown
  // here to apply, within this band, to earth/round + gravity/constant today. The pre-existing
  // NaN/Inf state check (health_monitor.hpp) remains the backstop: the run still halts, just
  // possibly one frame later and naming kNonFiniteState rather than kOutOfDomain. A margin-based or
  // acceleration-aware lookahead would supersede ADR-0012's pinned Euler formula and needs its own
  // plan-reviewed PR, not a change here.
  [[nodiscard]] HaltReason check_domain(const StateVector& y, const StepContext& ctx,
                                        HealthMonitor& health) noexcept;

  // ADR-0020: factored out of integrate()'s own derivative functor for the identical
  // cognitive-complexity reason check_domain above is its own function -- see its own definition
  // comment (vehicle.cpp) for the full contract. Returns whether the caller's wrench_sources_ loop
  // should be skipped this stage; updates stage_env.atmosphere in place when sampled and
  // wrench_fault (first-fault-wins) when a halt fires. Per-frame path: no allocation, no
  // exceptions.
  [[nodiscard]] bool prepare_stage_environment(const VehicleState& vs_stage,
                                               EnvironmentSample& stage_env, const StepContext& ctx,
                                               HealthMonitor& health,
                                               HaltReason& wrench_fault) noexcept;

  std::uint32_t index_;
  std::string name_;
  StateVector state_ = StateVector::Zero();
  EnvironmentSample environment_;
  std::vector<LoadedPlugin> plugins_;
  Scheduler scheduler_;
  std::optional<Eom> eom_;
  std::vector<WrenchEntry> wrench_sources_;
  // ADR-0017: this vehicle's own write seam, one per Vehicle (ADR-0011). A plain by-value member is
  // safe here (unlike published_wrench_ below): InputRegistry's own entries live in a std::deque,
  // so a const double*/InputHandle a plugin already holds is a pointer into that entry's own stable
  // storage, untouched by this Vehicle's own address moving.
  InputRegistry inputs_;
  // PLAN.md M2 PR 3c: nullptr when physical.atmosphere is not configured, mirroring
  // Simulation::atmosphere_ -- lets integrate()'s per-stage derivative recompute a fresh atmosphere
  // sample from eom_->earth().altitude() at each RK4 sub-stage, without a second EarthModel*
  // member. Non-owning: the model lives in Simulation::environment_plugins_, the same lifetime
  // discipline earth_plugin_path_/gravity_plugin_path_ already document below.
  AtmosphereModel* atmosphere_ = nullptr;
  // unique_ptr, not by value: SignalRegistry::add below stores a raw pointer into
  // *published_wrench_, and Simulation::from_config/for_testing move this Vehicle into vehicles_
  // after init() runs (simulation.cpp) -- a by-value member would dangle those pointers on that
  // move, the identical reason Simulation::health_ is a unique_ptr rather than a by-value
  // HealthMonitor (Codex plan critique, PR 3c). Allocated once in init() (the init path may
  // allocate, §14); integrate()'s derivative functor writes *published_wrench_ on the k1 stage
  // only, never allocating per frame.
  std::unique_ptr<Wrench> published_wrench_;
  // "<name_>.state.{p_n,v_b,q_nb,omega_b}", built once at init from this vehicle's own name so a
  // halt never carries a "v0" literal (ADR-0011).
  std::array<std::string, 4> state_names_;
  // "<name_>.wrench_total": names a halt on the accumulated wrench, which is not any one plugin's
  // fault to name (Codex review finding above) -- a sum can be non-finite even when every summed
  // contribution was finite.
  std::string wrench_total_name_;
  // ADR-0012: what a domain halt names, copied from PhysicsSpec at init (run-lifetime storage,
  // same discipline as state_names_/wrench_total_name_ above) -- Eom retains the Earth/gravity
  // pointers themselves, not a name for either.
  std::string earth_plugin_path_;
  std::string gravity_plugin_path_;
  // ADR-0020: what a report_non_finite_environment halt names, copied from PhysicsSpec/init()'s own
  // wind_plugin_path parameter, same discipline as earth_plugin_path_/gravity_plugin_path_ above.
  std::string atmosphere_plugin_path_;
  std::string wind_plugin_path_;
  // ADR-0020: whether physical.wind is actually configured this run -- EnvironmentAvailability
  // itself is a transient init() parameter, not retained past it before this ADR;
  // report_non_finite_environment needs it retained to know whether environment_.wind_n's zero
  // default is a legitimate absence (never checked) or a real, checkable value. atmosphere's own
  // equivalent needs no new member: atmosphere_ != nullptr already says the same thing for free.
  bool wind_configured_ = false;
};

}  // namespace astra
