// One vehicle's own state, models, and schedule (docs/architecture.md §7, ADR-0011): a Simulation
// holds a list of these of length one in v1, but nothing here assumes that -- each Vehicle owns
// its own combined state buffer and its own signal namespace ("v0.aero.CL").
//
// ADR-0021: two-phase init. Phase one -- for every vehicle.models entry, in order, then the
// vehicle's own mass model: load, init(), bind_inputs(), record state_count(). Once every count is
// known, allocate the combined/stage/pre-step-scratch buffers exactly once (13 for the rigid-body
// block, plus every state-owning model's own slice); seed the rigid-body block (zeroed by
// RigidBody6DofDynamics::initial_state(), then overwritten by the caller's own initial state
// through state()) and every state-owning Model's own slice (initial_state()) into the combined
// buffer, then sync the stage buffer to match -- before any model's own bind_state() or
// resolve_peers() hook runs, since either may dereference its bound pointer immediately rather than
// only caching it for a later per-frame call (Codex adversarial review, M3 PR 4a); only then
// publish the rigid-body block's 13 named signals and, for every state-owning Model, bind_state()
// and publish its own slice's signals (named "<name_>.<model.name()>.state.<i>", local per-model
// index
// -- two same-type instances neither configured to disambiguate their own name() collide here, on
// purpose: SignalRegistry::add()'s existing duplicate-name check catches it, surfaced as a
// ConfigError, not a silent overwrite). Phase two -- resolve_peers() for every loaded model,
// state-owning or not, only after every model's phase one (including publishing) has finished, so
// two models with mutual state dependence both resolve regardless of vehicle.models order. The
// stage buffer already equals the combined buffer from phase one's own sync onward -- nothing
// after it writes combined_ (bind_state() hands out a read-only pointer; resolve_peers() only
// resolves and caches pointers) -- so the invariant already holds before frame 0's step().
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/rigid_body_6dof_dynamics.hpp"
#include "astra/kernel/scheduler.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/state_buffer.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

// ADR-0032: the Earth's rotation rate relative to inertial, in this stage's body axes. It is the
// EOM's own c_nb * omega_ie_n (rigid_body_6dof_dynamics.cpp), on the stage's own state.
//
// Adding +0.0 turns a -0.0 component into +0.0 and leaves every other value, NaN included, as it
// is (IEEE 754 round-to-nearest; nothing in the tree changes the rounding mode, and without
// -ffast-math the compiler cannot drop the addition).
// - On a flat or non-rotating Earth every component is therefore +0.0, where c_nb * 0 alone gives
//   -0.0 for a c_nb row that is all negative.
// - So air_data::omega_eb_b's omega_b - omega_ie_b is omega_b bit for bit there: x - (+0.0) is x
//   for every x, -0.0 included, and x - (-0.0) is not.
// Per-frame path: fixed-size Eigen values only, no allocation, no exceptions.
[[nodiscard]] Eigen::Vector3d earth_rate_b(const EarthModel& earth,
                                           const VehicleState& vs) noexcept;

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
  // (ADR-0011: environment models are shared across vehicles). init() constructs this vehicle's
  // RigidBody6DofDynamics from all three; null when a config has no "physical" section (PR 2's
  // dummy-plugin fixture keeps running with no physics, exactly as before). earth_plugin_path/
  // gravity_plugin_path (ADR-0012) are what a domain halt names -- RigidBody6DofDynamics retains
  // the pointers (via its own earth()/gravity() accessors) but not a name, the same reason mass's
  // own plugin_path travels separately from its ModelSpec everywhere else in this file.
  //
  // atmosphere (PLAN.md M2 PR 3c, docs/plans/pr3c-substage-wrench.md): nullable, mirroring
  // Simulation::atmosphere_ -- non-null only when physical.atmosphere is configured. Lets Vehicle
  // recompute a fresh atmosphere sample at each RK4 sub-stage's own altitude (via
  // rigid_body_->earth(), already held) for wrench evaluation, without a second EarthModel*
  // member. This struct and Vehicle::init are technically public C++ API, but are never part of
  // the versioned plugin ABI: constructed only by Simulation::from_config/for_testing, both
  // compiled inside libastra_core itself, never across the dlopen loader boundary a Model-derived
  // plugin crosses -- see core/include/astra/core_api.hpp.in's own bump policy and
  // docs/plans/pr3c-substage-wrench.md's "kCoreApiVersion" section for the full reasoning (Codex
  // adversarial review, PR 3c plan critique).
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
  // and constructs this vehicle's RigidBody6DofDynamics against it and the shared earth/gravity
  // models physics names. Every model's init() runs under a SignalRegistry::PrefixScope for this
  // vehicle's name, so "dummy.steps" is published as "v0.dummy.steps" (ADR-0011). As each
  // WrenchSource is discovered, checks its requires_environment() (wrench_source.hpp) against
  // availability: a wind requirement with availability.wind false throws ConfigError naming
  // physical.wind; an atmosphere requirement with availability.atmosphere false throws ConfigError
  // naming physical.atmosphere. ADR-0021's two-phase state-owning discovery (see this header's own
  // top comment) runs interleaved with this same pass, finishing after phase two. Init-time only:
  // may allocate, may throw (PluginError, ConfigError).
  //
  // wind_plugin_path (ADR-0020): what report_non_finite_environment names on a non-finite wind_n
  // halt, kept as its own parameter rather than a PhysicsSpec field (wind is independently
  // optional, unlike the physics bundle PhysicsSpec represents -- see
  // PhysicsSpec::atmosphere_plugin_path's own comment). Ignored (and should be empty) when
  // availability.wind is false.
  //
  // dt/integrator_kind (ADR-0021): Integrator now allocates its own RK4 scratch sized to a
  // combined-buffer state count that is only known once this vehicle's own models have all been
  // loaded -- constructing it is therefore this vehicle's own job, not Simulation's, and it is no
  // longer a Simulation-wide shared instance (ADR-0011: two vehicles' combined buffers can differ
  // in size once either owns model state beyond the rigid body, so one shared Integrator sized to
  // one vehicle would be wrong for another's differently-sized buffer).
  void init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
            SignalRegistry& registry, const PhysicsSpec* physics,
            EnvironmentAvailability availability, std::string wind_plugin_path = {},
            double dt = kDefaultDt, IntegratorKind integrator_kind = IntegratorKind::kRk4);

  void step(const StepContext& ctx) noexcept { scheduler_.step(ctx); }

  // This frame's environment sample (docs/architecture.md §5), latched by Simulation::step before
  // step()/integrate() run (environment_sample.hpp). Defaults to still air / all-NaN atmosphere,
  // matching a Simulation with no physical.wind/physical.atmosphere configured. omega_ie_b and
  // altitude_m stay NaN here: each RK4 stage's copy sets them (ADR-0032, ADR-0033,
  // prepare_stage_environment). Also marks environment_latched_ true, unconditionally, every call
  // (M3 PR 5, docs/plans/pr5-nanobind-bindings.md, Design decision 2) -- cleared by nothing,
  // including reset(): once a real frame has latched a real sample, derivative() may trust
  // environment_'s wind_n permanently for this Vehicle's lifetime, regardless of how many further
  // frames run or don't.
  void set_environment(const EnvironmentSample& sample) noexcept {
    environment_ = sample;
    environment_latched_ = true;
  }
  [[nodiscard]] const EnvironmentSample& environment() const noexcept { return environment_; }

  // M4 PR 3 (docs/plans/m4-pr3-core-seam.md): the exchange stage's PreviousFrame inputs
  // (icd_pack.hpp). Read at frame k before integrate(), both still hold frame k-1's values: the
  // k1-stage wrench integrate() last published, and the mass the last begin_frame() latched.
  // Physics must be configured (a transport requires it); without it published_wrench() is a zero
  // wrench and latched_mass() is NaN. The exchange stage decides separately whether frame k-1 was
  // actually integrated (Vehicle::derivative() also calls begin_frame(), with no integration).
  [[nodiscard]] const Wrench& published_wrench() const noexcept;
  [[nodiscard]] double latched_mass() const noexcept;

  // Runs this frame's wrench accumulation, dynamics derivative, integrator step, and post-step
  // (docs/architecture.md §5): a no-op returning a falsey HaltReason when this vehicle has no
  // RigidBody6DofDynamics (physics off) -- a no-physics vehicle has no Earth model and no position
  // representation to be out of domain in, so ADR-0012's check does not apply to it either; that
  // is this early return's job already, not something to special-case below. Order: (ADR-0012)
  // domain-check the PRE-step state, but only on frame 0 -- every later frame's pre-step state is
  // the previous frame's already-checked post-step state (see check_domain's own comment), so this
  // is the one call that makes the post-step-plus-lookahead invariant hold from the very first RK4
  // stage the integrator ever evaluates.
  //
  // wrench_b()/integrate_state() and the atmosphere sample the former reads are evaluated fresh at
  // each of Integrator's four RK4 sub-stages, from a derivative functor local to this method. Each
  // stage's own trial state is copied into the stage buffer before any such call this stage
  // (ADR-0021), so bind_state()/SignalRegistry reads of a state-owning model's own value are
  // sub-stage-exact. Each wrench stage's contribution and total are still health-checked
  // (health.check_wrench), uniformly, every stage, via a first-fault-wins HaltReason side channel
  // read back after integrator.step() returns. A wrench fault still leaves state() exactly as if
  // the frame never ran, via an explicit revert to the saved pre-step state (a StateBuffer::
  // copy_from into a Vehicle-owned scratch buffer sized once at init -- R2). health-check the
  // resulting state before renormalization (PLAN.md M1 PR 4: "|q| drift > 1e-6 before
  // renormalization"); domain-check the POST-step state with the one-frame lookahead, skipped when
  // the state check already halted; run RigidBody6DofDynamics::post_step() (renormalize)
  // unconditionally, halt or not (ADR-0001); sync the stage buffer to the newly-accepted combined
  // state, unconditionally, on every path (ADR-0021's own stage-buffer invariant); return whichever
  // check halted (the state check wins over the domain check when both would fire the same frame).
  // Per-frame path: no allocation, no exceptions. Uses this vehicle's own integrator_ (constructed
  // in init(), see its own comment) -- no longer a parameter, now that the integrator's own scratch
  // is sized per-vehicle.
  [[nodiscard]] HaltReason integrate(const StepContext& ctx, HealthMonitor& health) noexcept;

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
  // Whether physics is configured (a rigid-body block and its EOM); ADR-0026 publishes derived
  // signals only for such a vehicle.
  [[nodiscard]] bool has_physics() const noexcept { return rigid_body_.has_value(); }
  // The rigid-body block: the combined state buffer's own first 13 doubles, by value (a small
  // Eigen::Map, not an owning copy -- ADR-0021's own retype, matching VehicleState's). The mutable
  // overload is how Simulation::from_config/for_testing write the parsed "initial.state" into the
  // combined buffer, after init() has allocated it and RigidBody6DofDynamics::initial_state() has
  // zeroed it -- assigning through the returned Map writes into that same storage, exactly like
  // assigning through any other Eigen block expression. A caller that writes through this mutable
  // overload before frame 0 MUST call sync_stage_buffer() afterward (see its own comment) --
  // init()'s own end-of-phase-one sync ran before this write exists to sync. Only ever called after
  // init() has emplaced combined_ (every real caller's own contract); Eigen::Map has no null/empty
  // state to fall back to, so this is NOLINT, not a real branch, unlike the accessors below.
  [[nodiscard]] Eigen::Map<StateVector> state() noexcept {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return Eigen::Map<StateVector>(combined_->data());
  }
  [[nodiscard]] Eigen::Map<const StateVector> state() const noexcept {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return Eigen::Map<const StateVector>(combined_->data());
  }
  // ADR-0021: re-syncs the stage buffer to the combined buffer -- required after any write through
  // the mutable state() overload above, since init()'s own sync (this header's own top comment)
  // necessarily ran before such a write exists to sync. Without this, a state-owning model
  // resolving the rigid-body block's own published signals ("<name>.state.p_n_0" etc.) would see
  // RigidBody6DofDynamics::initial_state()'s zeroed placeholder, not "initial.state"'s real parsed
  // value, until frame 1 -- a real gap this method closes, not merely a defensive addition; caught
  // by tests/unit/kernel/vehicle_state_coupling_test.cpp's own
  // StageBufferMatchesCombinedBufferAfterInit failing without it. Simulation::from_config/
  // for_testing both call this immediately after their own state()-mutating assignment. The guard
  // below is redundant for correctness (never called before init() by any real caller) but keeps
  // clang-tidy's bugprone-unchecked-optional-access satisfied, the same precedent check_domain's
  // own comment (vehicle.cpp) documents for rigid_body_.
  void sync_stage_buffer() noexcept {
    if (!stage_.has_value() || !combined_.has_value()) {
      return;
    }
    stage_->copy_from(*combined_);
  }
  [[nodiscard]] const Scheduler& scheduler() const noexcept { return scheduler_; }
  // ADR-0017: this vehicle's own write seam. Simulation::from_config applies vehicle.
  // initial_controls's config literals and calls freeze() through this accessor; Simulation::step
  // calls latch() through it once per vehicle per frame, after step(), before integrate() (§5).
  [[nodiscard]] InputRegistry& inputs() noexcept { return inputs_; }
  [[nodiscard]] const InputRegistry& inputs() const noexcept { return inputs_; }

  // The full combined state buffer -- the rigid-body block plus every state-owning model's own
  // slice, in the order state_owner_slices() below names (M3 PR 5, ADR-0021 R4;
  // docs/plans/pr5-nanobind-bindings.md, Design decision 5). Renamed/promoted from this method's
  // own prior test-only name (combined_state_for_testing()) rather than adding a second accessor
  // returning the same span: this is the real production accessor derivative()'s own input-length
  // validation and the Python `vehicle.state` binding both use, not merely a test seam. A live view
  // -- it aliases combined_'s own storage directly, so its contents change under integrate() (and
  // therefore under Simulation::step()/run()) exactly the way Vehicle::state()'s rigid-body-only
  // view already does; a caller that wants a stable snapshot must copy it. Unlike stage_'s own
  // still-test-only accessor below, production code has a real reason to read this one. The
  // has_value() check is redundant for correctness (never called before init() by any real caller)
  // but keeps clang-tidy's bugprone-unchecked-optional-access satisfied, the same precedent
  // sync_stage_buffer's own comment documents.
  [[nodiscard]] std::span<const double> combined_state() const noexcept {
    if (!combined_.has_value()) {
      return {};
    }
    return {combined_->data(), combined_->size()};
  }
  // ADR-0025 (M5 PR 4): the stage buffer's address range, which every "<name>.state.*" rigid-body
  // signal and every model-owned state signal points into. Those signals are end-of-frame once a
  // frame completes; the run log tells them apart by address and logs the frame-start combined
  // state (combined_state(), same offsets) in their place.
  [[nodiscard]] std::span<const double> published_state() const noexcept {
    if (!stage_.has_value()) {
      return {};
    }
    return {stage_->data(), stage_->size()};
  }
  // Test-only introspection (ADR-0021): the stage buffer's own current contents, for
  // tests/unit/kernel/vehicle_state_coupling_test.cpp's stage-buffer-invariant checks (outside any
  // derivative call, the stage buffer equals the combined buffer). Public on the shipped Vehicle
  // class, not gated behind a test-only build -- the same posture Simulation::vehicles()'s own
  // non-const overload already takes for a comparable test-serving accessor: the guard is the name
  // and this comment, not the compiler.
  [[nodiscard]] std::span<const double> stage_state_for_testing() const noexcept {
    if (!stage_.has_value()) {
      return {};
    }
    return {stage_->data(), stage_->size()};
  }
  // M3 PR 5 (ADR-0021 R4): every state-owning model's own slice of the combined buffer beyond the
  // rigid-body block (which is always offset 0, count kStateSize -- already bound in Python as
  // POSITION/VELOCITY/QUATERNION/ANGULAR_RATE), in combined-buffer order. The authoritative layout
  // metadata derivative()'s named-slice contract needs -- built once at init from state_owners_,
  // never derived by parsing SignalRegistry names (docs/plans/pr5-nanobind-bindings.md, Design
  // decision 5).
  [[nodiscard]] std::span<const NamedStateSlice> state_owner_slices() const noexcept {
    return state_owner_health_slices_;
  }
  // Test-only introspection (M3 PR 5, R9): the real Integrator's own k1 scratch, for a test proving
  // derivative()'s probe result is bitwise identical to a real frame's own k1 sub-stage. The
  // has_value() check keeps clang-tidy's bugprone-unchecked-optional-access satisfied.
  [[nodiscard]] std::span<const double> k1_for_testing() const noexcept {
    if (!integrator_.has_value()) {
      return {};
    }
    return integrator_->k1_for_testing();
  }

  // M3 PR 5 (ADR-0021 R4, docs/plans/pr5-nanobind-bindings.md): evaluates this vehicle's own
  // derivative at an arbitrary state, without advancing the frame -- the exact arithmetic
  // integrate()'s own k1 RK4 sub-stage evaluates (evaluate_derivative_stage() below, shared by
  // both call sites, not a second implementation). y must be exactly combined_state().size()
  // doubles; dq is the caller-owned output buffer of the same size, written in full on success.
  // health is the real HealthMonitor (Simulation's own *health_) purely as the policy source for
  // this Vehicle's own lazily copy-constructed, counter-isolated probe_health_ (constructed once,
  // on first call, reused thereafter -- copying carries the real action/policy settings forward
  // without copying or touching the real monitor's own counters/signals, since HealthMonitor holds
  // no SignalRegistry reference to alias). Throws astra::Error (not a HaltReason -- this is not a
  // per-frame path) when: no physics is configured; a WindModel is configured but
  // environment_latched_ is still false (never latched by a real frame); or the probe evaluation's
  // own domain/environment checks fault. Restores the stage buffer to match the accepted combined
  // state before returning on every path, including a raised fault, so a probe never leaves
  // anything behind that would change the very next real frame's own k1. combined_ itself is never
  // read or written by this call -- y is the caller's own buffer, never combined_->data() -- so
  // there is nothing to roll back on that side. Not on the per-frame path: may allocate (the lazy
  // probe_health_ construction; std::string in a thrown Error), may throw.
  void derivative(const StepContext& ctx, std::span<const double> y, std::span<double> dq,
                  const HealthMonitor& health);

 private:
  // A vehicle.models entry that resolved to a WrenchSource, kept alongside the plugin path its
  // config named it by (§15: "the offending plugin named") -- Scheduler::Entry only stores Model*.
  struct WrenchEntry {
    WrenchSource* model;
    std::string plugin_path;
  };

  // ADR-0021: a Model-derived entry (a vehicle.models entry or the vehicle's own mass model) that
  // declared state_count() > 0 -- its own slice of the combined/stage buffers, plus everything
  // needed to name a ConfigError against its own config subtree if publishing its signals fails
  // (a duplicate name, most likely -- see this header's own top comment). config is a cheap owning
  // copy (Config sharing its document, per its own header comment), kept because the loop that
  // publishes these signals runs after the ModelSpec list itself may have gone out of scope.
  struct StateOwnerEntry {
    Model* model;
    std::string plugin_path;
    Config config;
    std::string name;
    std::size_t offset;
    std::size_t count;
  };

  // ADR-0012: domain-checks state at p_n and the one-frame lookahead p_n + dt * p_n_dot(state),
  // against both RigidBody6DofDynamics's earth() and gravity() (each may be the first to fail;
  // earth is checked first, matching Simulation::from_config's own earth-then-gravity
  // validate_p_n order), and reports through health.report_out_of_domain naming whichever plugin
  // failed. Called from integrate() both pre-step (frame 0 only) and post-step (every frame);
  // factored out to stay under clang-tidy's readability-function-cognitive-complexity threshold on
  // integrate() itself. y points at the combined buffer's own start (the rigid-body block is
  // always its first 13 doubles). Per-frame path: no allocation, no exceptions. Only ever called
  // when rigid_body_.has_value() and health.domain_enabled().
  //
  // Known gap, disclosed not fixed (Codex adversarial review, M2 PR 2c; PLAN.md's own line has
  // the full account): the lookahead above is first-order in velocity, but the Integrator's RK4
  // stages (integrator.hpp) nest ACCELERATION into a position estimate at 0.5 * |a| * dt^2 (~4.9
  // um at standard gravity and ADR-0010's dt = 1 ms, pinned against the real dynamics+Integrator by
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
  [[nodiscard]] HaltReason check_domain(const double* y, const StepContext& ctx,
                                        HealthMonitor& health) noexcept;

  // ADR-0020: factored out of integrate()'s own derivative functor for the identical
  // cognitive-complexity reason check_domain above is its own function -- see its own definition
  // comment (vehicle.cpp) for the full contract. Returns whether the caller's wrench_sources_ loop
  // should be skipped this stage. It sets stage_env.omega_ie_b (ADR-0032) and stage_env.altitude_m
  // (ADR-0033) first, on every stage. It updates stage_env.atmosphere in place when sampled, and
  // wrench_fault (first-fault-wins) when a halt fires. Per-frame path: no allocation, no
  // exceptions.
  [[nodiscard]] bool prepare_stage_environment(const VehicleState& vs_stage,
                                               EnvironmentSample& stage_env, const StepContext& ctx,
                                               HealthMonitor& health,
                                               HaltReason& wrench_fault) noexcept;

  // ADR-0020: factored out of integrate()'s own derivative functor for the identical
  // cognitive-complexity reason above -- sums every WrenchSource's own contribution this stage,
  // health-checking each contribution and the total. Per-frame path: no allocation, no exceptions.
  [[nodiscard]] Wrench accumulate_wrench(const VehicleState& vs_stage,
                                         const EnvironmentSample& stage_env, const StepContext& ctx,
                                         HealthMonitor& health, HaltReason& wrench_fault) noexcept;

  // M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, Design decisions 1/1b/1e/2/6): the exact
  // per-stage derivative body -- environment copy, the stage's Earth rate (ADR-0032) and
  // fresh-atmosphere sampling, sub-stage-exact
  // stage_ sync, wrench accumulation, rigid-body and state-owner derivatives -- shared verbatim by
  // integrate()'s own per-frame lambda and the new derivative() probe entry point below, so neither
  // is a second implementation of the other. health is whichever HealthMonitor the caller passes
  // (the real one from integrate(); a probe-only, policy-matching, counter-isolated one from
  // derivative()) -- every check this method runs reports through exactly that instance, so a
  // probe's counters/signals never touch the real ones. wrench_destination receives this
  // evaluation's accumulated wrench when non-null (integrate()'s own stage 0 only, via
  // published_wrench_.get(); derivative()'s own probe scratch, always); nullptr means "publish
  // nowhere" (every other real per-frame stage). Per-frame path (health the real monitor,
  // wrench_destination published_wrench_.get() or nullptr): no allocation, no exceptions --
  // enforced by every caller's own context, not by this method's signature alone.
  void evaluate_derivative_stage(double t, const double* y, double* dy, const StepContext& ctx,
                                 HealthMonitor& health, HaltReason& wrench_fault,
                                 Wrench* wrench_destination) noexcept;

  // ADR-0021: the four init()-time helpers below exist for the identical
  // readability-function-cognitive-complexity reason check_domain/prepare_stage_environment/
  // accumulate_wrench above are their own functions, not init() itself -- each is called exactly
  // once, in this order, from init(). Init-time only: may allocate, may throw.

  // Phase one, part A: loads/inits/binds every vehicle.models entry, in order, recording every
  // WrenchSource (wrench_sources_) and every state-owning entry (state_owners_, appended starting
  // at *running_offset, which this method advances by each entry's own state_count()).
  void load_vehicle_models(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                           SignalRegistry& registry, EnvironmentAvailability availability,
                           std::size_t& running_offset);

  // Phase one, part A's physics tail: loads this vehicle's own mass model, constructs rigid_body_,
  // records its state ownership (if any, appended the same way load_vehicle_models does), and
  // publishes the always-on wrench_total.* signals. A no-op when physics is null.
  void load_physics_and_mass(const PluginLoader& loader, const PhysicsSpec* physics,
                             SignalRegistry& registry, std::size_t& running_offset);

  // Seeds the rigid-body block's and every state-owning model's own initial value into combined_,
  // then syncs stage_ to match -- before any model's own bind_state()/resolve_peers() hook runs
  // (Codex adversarial review, M3 PR 4a: either hook may dereference its bound pointer immediately,
  // not only cache it for a later per-frame call, so stage_ must already hold every model's real
  // initial value, not StateBuffer's default zeroed contents, by the time either first runs). Only
  // ever called from init(), after combined_/stage_ are emplaced; the one and only sync phase one
  // performs (see init()'s own comment for why no second sync is needed at the end of phase two).
  void seed_initial_state();

  // The rigid-body block's own 13 named signals (ADR-0021 design decision, "Resolved: the
  // rigid-body signal question"), published unconditionally whenever physics is configured. Only
  // ever called from init(), after seed_initial_state() has already run.
  void publish_rigid_body_signals(SignalRegistry& registry);

  // Phase one, part B: binds and publishes every state-owning model's own slice (see this header's
  // own top comment for the full contract, including the deliberate local-index-collision design).
  // Only ever called from init(), after seed_initial_state() has already seeded combined_ and
  // synced stage_ to match.
  void bind_and_publish_state_owners(SignalRegistry& registry);

  std::uint32_t index_;
  std::string name_;
  EnvironmentSample environment_;
  std::vector<LoadedPlugin> plugins_;
  Scheduler scheduler_;
  std::optional<RigidBody6DofDynamics> rigid_body_;
  std::vector<WrenchEntry> wrench_sources_;
  // ADR-0021: every Model-derived entry that owns continuous state, in load order; sized once and
  // never modified after init(). combined_/stage_/pre_step_scratch_ are optional<StateBuffer> only
  // because their size (13 + sum(state_count())) is not known until every model's init() has run
  // (StateBuffer itself has no default/resizable state -- R2); all three are emplaced exactly once,
  // together, at the end of phase one, and never re-emplaced afterward.
  std::vector<StateOwnerEntry> state_owners_;
  // Built once at init from state_owners_ (string_views into its own, never-reallocated-after-init
  // std::string members): the argument check_state's own generic per-model finiteness check reads,
  // every frame, with no per-frame allocation.
  std::vector<NamedStateSlice> state_owner_health_slices_;
  std::optional<StateBuffer> combined_;
  std::optional<StateBuffer> stage_;
  std::optional<StateBuffer> pre_step_scratch_;
  // Constructed once at the end of init(), once combined_'s own size is known (ADR-0021: sized to
  // this vehicle's own combined state, not shared across vehicles -- see init()'s own comment).
  std::optional<Integrator> integrator_;
  // ADR-0017: this vehicle's own write seam, one per Vehicle (ADR-0011). A plain by-value member is
  // safe here (unlike published_wrench_ below): InputRegistry's own entries live in a std::deque,
  // so a const double*/InputHandle a plugin already holds is a pointer into that entry's own stable
  // storage, untouched by this Vehicle's own address moving.
  InputRegistry inputs_;
  // PLAN.md M2 PR 3c: nullptr when physical.atmosphere is not configured, mirroring
  // Simulation::atmosphere_ -- lets integrate()'s per-stage derivative recompute a fresh atmosphere
  // sample from rigid_body_->earth().altitude() at each RK4 sub-stage, without a second
  // EarthModel* member. Non-owning: the model lives in Simulation::environment_plugins_, the same
  // lifetime discipline earth_plugin_path_/gravity_plugin_path_ already document below.
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
  // same discipline as state_names_/wrench_total_name_ above) -- RigidBody6DofDynamics retains the
  // Earth/gravity pointers themselves, not a name for either.
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
  // M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, Design decision 2): set true, unconditionally,
  // by every set_environment() call -- i.e. by every real frame's own environment stage -- and
  // cleared by nothing. derivative() raises when wind_configured_ is true but this is still false:
  // no real frame has ever latched a real wind sample for this Vehicle to trust.
  bool environment_latched_ = false;
  // M3 PR 5 (Design decision 1/1b): this Vehicle's own probe-only HealthMonitor, lazily
  // copy-constructed from the real one on derivative()'s first call and reused thereafter -- same
  // action policy as the real monitor (required: prepare_stage_environment's own domain_enabled()
  // check can take a different branch under a different policy, which would break bit-identity,
  // not just leak side signals), isolated counters (this copy's counters diverge from the real
  // one's from the moment of copy onward; the real one's stay untouched by any probe call).
  std::optional<HealthMonitor> probe_health_;
};

}  // namespace astra
