#include "astra/kernel/vehicle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

namespace {

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
Eigen::Vector3d earth_rate_b(const EarthModel& earth, const VehicleState& vs) noexcept {
  const Eigen::Matrix3d c_nb = frames::dcm_from_quaternion(vs.q_nb());
  Eigen::Vector3d omega_ie_b = c_nb * earth.omega_ie_n(vs);
  omega_ie_b.array() += 0.0;
  return omega_ie_b;
}

}  // namespace

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

// ADR-0021 phase one, part A: see this method's own declaration comment (vehicle.hpp) for why it
// is factored out of init() itself. running_offset is both an input (where the first state-owning
// entry found here starts) and an output (advanced past every entry this method records).
void Vehicle::load_vehicle_models(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                                  SignalRegistry& registry, EnvironmentAvailability availability,
                                  std::size_t& running_offset) {
  for (const ModelSpec& spec : models) {
    LoadedPlugin loaded = loader.load(spec.plugin_path);
    // Captured before the move below: .get() does not affect ownership, and the model stays alive
    // for the vehicle's lifetime once plugins_ owns it.
    Model* model_ptr = loaded.model.get();
    WrenchSource* wrench_source = nullptr;
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      loaded.model->init(spec.config, registry);
      // ADR-0017 (Joe's review round 1, finding 1): bind_inputs() must run while this PrefixScope
      // is still open, so every write-seam name lands correctly namespaced ("v0.controls.elevator",
      // not "controls.elevator") -- moved inside this block for that reason; the requires_
      // environment() check below never touches the registry's prefix, so it stays outside.
      wrench_source = dynamic_cast<WrenchSource*>(model_ptr);
      if (wrench_source != nullptr) {
        wrench_source->bind_inputs(inputs_, registry);
      }
    }
    scheduler_.add(*loaded.model, spec.divisor);
    if (wrench_source != nullptr) {
      const EnvironmentRequirements requirements = wrench_source->requires_environment();
      if (requirements.wind && !availability.wind) {
        throw ConfigError(spec.config.origin(), "physical.wind",
                          spec.plugin_path + " requires wind");
      }
      if (requirements.atmosphere && !availability.atmosphere) {
        throw ConfigError(spec.config.origin(), "physical.atmosphere",
                          spec.plugin_path + " requires atmosphere");
      }
      wrench_sources_.push_back(WrenchEntry{wrench_source, spec.plugin_path});
    }
    const int count = model_ptr->state_count();
    plugins_.push_back(std::move(loaded));
    if (count > 0) {
      state_owners_.push_back(StateOwnerEntry{model_ptr, spec.plugin_path, spec.config,
                                              std::string(model_ptr->name()), running_offset,
                                              static_cast<std::size_t>(count)});
      running_offset += static_cast<std::size_t>(count);
    }
  }
}

// ADR-0021 phase one, part A's physics tail: see this method's own declaration comment
// (vehicle.hpp) for why it is factored out of init() itself.
void Vehicle::load_physics_and_mass(const PluginLoader& loader, const PhysicsSpec* physics,
                                    SignalRegistry& registry, std::size_t& running_offset) {
  if (physics == nullptr) {
    return;
  }
  LoadedPlugin mass_loaded = loader.load(physics->mass.plugin_path);
  auto* mass_model = dynamic_cast<MassPropertiesModel*>(mass_loaded.model.get());
  if (mass_model == nullptr) {
    throw PluginLoadError(physics->mass.plugin_path, "does not implement MassPropertiesModel");
  }
  {
    const SignalRegistry::PrefixScope scope(registry, name_);
    mass_model->init(physics->mass.config, registry);
  }
  scheduler_.add(*mass_model, physics->mass.divisor);
  rigid_body_.emplace(*physics->earth, *physics->gravity, *mass_model);
  const int mass_count = mass_model->state_count();
  plugins_.push_back(std::move(mass_loaded));
  if (mass_count > 0) {
    state_owners_.push_back(StateOwnerEntry{mass_model, physics->mass.plugin_path,
                                            physics->mass.config, std::string(mass_model->name()),
                                            running_offset, static_cast<std::size_t>(mass_count)});
    running_offset += static_cast<std::size_t>(mass_count);
  }
  // ADR-0012: what a domain halt names. RigidBody6DofDynamics (just above) retains the
  // Earth/gravity pointers themselves; this vehicle retains their names, the same lifetime
  // discipline as state_names_/wrench_total_name_.
  earth_plugin_path_ = physics->earth_plugin_path;
  gravity_plugin_path_ = physics->gravity_plugin_path;
  atmosphere_ = physics->atmosphere;
  atmosphere_plugin_path_ = physics->atmosphere_plugin_path;

  // PLAN.md M2 PR 3c: the vehicle's applied wrench, evaluated at the frame-start (k1) RK4
  // sub-stage only, published core-side -- see integrate()'s own comment for why this replaced
  // each WrenchSource's own self-publish. Heap-allocated (see published_wrench_'s own comment in
  // vehicle.hpp for why a by-value member would dangle). Registered even when wrench_sources_ is
  // empty (no WrenchSource configured): the signal then simply reads zero every frame, the same
  // "still air" default posture EnvironmentSample::wind_n already uses for an absent input.
  published_wrench_ = std::make_unique<Wrench>();
  registry.add(name_ + ".wrench_total.force_b_x", &published_wrench_->force_b(0), "N");
  registry.add(name_ + ".wrench_total.force_b_y", &published_wrench_->force_b(1), "N");
  registry.add(name_ + ".wrench_total.force_b_z", &published_wrench_->force_b(2), "N");
  registry.add(name_ + ".wrench_total.moment_b_l", &published_wrench_->moment_b(0), "N.m");
  registry.add(name_ + ".wrench_total.moment_b_m", &published_wrench_->moment_b(1), "N.m");
  registry.add(name_ + ".wrench_total.moment_b_n", &published_wrench_->moment_b(2), "N.m");
}

// The rigid-body block's own 13 named signals: see this method's own declaration comment
// (vehicle.hpp). Only ever called from init(), after combined_/stage_ are emplaced and rigid_body_
// has a value -- the guard below is redundant for correctness but keeps clang-tidy's
// bugprone-unchecked-optional-access satisfied, the same precedent check_domain's own comment
// documents for rigid_body_.
void Vehicle::publish_rigid_body_signals(SignalRegistry& registry) {
  if (!stage_.has_value()) {
    return;
  }
  // NOLINTBEGIN(readability-magic-numbers): ADR-0003's own fixed 13-double rigid-body layout
  // (p_n(3) v_b(3) q_nb(4) omega_b(3)), not arbitrary constants -- the same reasoning
  // integrator.hpp's own RK4 tableau NOLINTs.
  double* rigid_body_stage = stage_->data();
  registry.add(name_ + ".state.p_n_0", &rigid_body_stage[0], "");
  registry.add(name_ + ".state.p_n_1", &rigid_body_stage[1], "");
  registry.add(name_ + ".state.p_n_2", &rigid_body_stage[2], "");
  registry.add(name_ + ".state.v_b_0", &rigid_body_stage[3], "m/s");
  registry.add(name_ + ".state.v_b_1", &rigid_body_stage[4], "m/s");
  registry.add(name_ + ".state.v_b_2", &rigid_body_stage[5], "m/s");
  registry.add(name_ + ".state.q_nb_w", &rigid_body_stage[6], "");
  registry.add(name_ + ".state.q_nb_x", &rigid_body_stage[7], "");
  registry.add(name_ + ".state.q_nb_y", &rigid_body_stage[8], "");
  registry.add(name_ + ".state.q_nb_z", &rigid_body_stage[9], "");
  registry.add(name_ + ".state.omega_b_0", &rigid_body_stage[10], "rad/s");
  registry.add(name_ + ".state.omega_b_1", &rigid_body_stage[11], "rad/s");
  registry.add(name_ + ".state.omega_b_2", &rigid_body_stage[12], "rad/s");
  // NOLINTEND(readability-magic-numbers)
}

// ADR-0021: seeds the rigid-body block's and every state-owning model's own initial value into
// combined_, then syncs stage_ to match -- before any model's own bind_state()/resolve_peers() hook
// runs. Codex adversarial review (M3 PR 4a): the original ordering called initial_state()
// interleaved with bind_state() inside the same per-owner loop bind_and_publish_state_owners() now
// runs, and synced stage_ only once, at the very end of init() -- after both bind_state() and phase
// two's resolve_peers() had already run against a stage buffer still holding StateBuffer's default
// (zeroed) contents. A model whose bind_state() or resolve_peers() dereferences its bound pointer
// immediately (not only caches it for a later per-frame integrate_state()/wrench_b() call) observed
// zero instead of the real seeded value. Only ever called from init(), after combined_/stage_ are
// emplaced -- the guard below is redundant for correctness but keeps clang-tidy's
// bugprone-unchecked-optional-access satisfied.
void Vehicle::seed_initial_state() {
  if (!combined_.has_value() || !stage_.has_value()) {
    return;
  }
  if (rigid_body_.has_value()) {
    // Zeroed here; Simulation::from_config/for_testing overwrite this slice through state()'s own
    // mutable accessor immediately after init() returns (see sync_stage_buffer()'s own comment,
    // vehicle.hpp, for why that external write needs its own re-sync). Vehicle itself never reads
    // "initial.state" -- that stays Simulation's own responsibility.
    rigid_body_->initial_state(combined_->data());
  }
  for (const StateOwnerEntry& owner : state_owners_) {
    owner.model->initial_state(combined_->data() + owner.offset);
  }
  stage_->copy_from(*combined_);
}

// ADR-0021 phase one, part B: see this method's own declaration comment (vehicle.hpp). Only ever
// called from init(), after seed_initial_state() has already seeded combined_ and synced stage_ to
// match -- a model's own bind_state() may dereference its bound pointer immediately, not only cache
// it for a later per-frame call (Codex adversarial review finding), so stage_ must already hold
// every model's real initial value, not a still-zero placeholder, by the time this runs. The guard
// below is redundant for correctness but keeps clang-tidy's bugprone-unchecked-optional-access
// satisfied.
void Vehicle::bind_and_publish_state_owners(SignalRegistry& registry) {
  if (!stage_.has_value()) {
    return;
  }
  state_owner_health_slices_.reserve(state_owners_.size());
  for (const StateOwnerEntry& owner : state_owners_) {
    owner.model->bind_state(stage_->data() + owner.offset);
    for (std::size_t i = 0; i < owner.count; ++i) {
      const std::string signal_name = name_ + "." + owner.name + ".state." + std::to_string(i);
      try {
        registry.add(signal_name, &stage_->data()[owner.offset + i], "");
      } catch (const Error& e) {
        throw ConfigError(owner.config.origin(), owner.config.path(), e.what());
      }
    }
    state_owner_health_slices_.push_back(NamedStateSlice{owner.name, owner.offset, owner.count});
  }
}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry, const PhysicsSpec* physics,
                   EnvironmentAvailability availability, std::string wind_plugin_path, double dt,
                   IntegratorKind integrator_kind) {
  state_names_ = {name_ + ".state.p_n", name_ + ".state.v_b", name_ + ".state.q_nb",
                  name_ + ".state.omega_b"};
  wrench_total_name_ = name_ + ".wrench_total";
  // ADR-0020: what report_non_finite_environment names on a non-finite wind_n halt, and whether
  // wind_n is even a real, checkable value this run (vs. still air's own legitimate zero default).
  wind_configured_ = availability.wind;
  wind_plugin_path_ = std::move(wind_plugin_path);

  plugins_.reserve(models.size() + (physics != nullptr ? 1 : 0));
  wrench_sources_.reserve(models.size());  // upper bound: not every model is a WrenchSource
  state_owners_.reserve(models.size() + (physics != nullptr ? 1 : 0));  // upper bound

  // ADR-0021: the rigid-body block always occupies the combined buffer's first kStateSize doubles,
  // whether or not physics is configured (matching the pre-ADR-0021 state_ member, which always
  // existed too) -- state-owning discovery below starts allocating right after it.
  std::size_t running_offset = kStateSize;
  load_vehicle_models(loader, models, registry, availability, running_offset);
  load_physics_and_mass(loader, physics, registry, running_offset);

  // ADR-0021: allocate the combined/stage/pre-step-scratch buffers exactly once, now that every
  // model's own state_count() is known (R2: never resized again).
  combined_.emplace(running_offset);
  stage_.emplace(running_offset);
  pre_step_scratch_.emplace(running_offset);
  integrator_.emplace(running_offset, integrator_kind, dt);

  seed_initial_state();

  if (rigid_body_.has_value()) {
    publish_rigid_body_signals(registry);
  }
  bind_and_publish_state_owners(registry);

  // ADR-0021 phase two: resolve_peers() for every loaded model, state-owning or not, only after
  // every model's phase one (including publishing above) has finished -- mutual cross-model state
  // dependence resolves regardless of vehicle.models order.
  for (LoadedPlugin& loaded : plugins_) {
    loaded.model->resolve_peers(registry);
  }
  // ADR-0021: the stage-buffer invariant already holds before frame 0's own step() ever runs --
  // seed_initial_state()'s own sync above is the one and only sync phase one performs; nothing
  // between it and here writes combined_ (bind_state() hands out a read-only pointer;
  // resolve_peers() only resolves and caches pointers, per Model::resolve_peers()'s own contract).
}

// ADR-0017: relative order between inputs_.reset() and each model's own reset() does not matter --
// a conforming WrenchSource's reset() is prohibited from writing through a cached InputHandle
// (wrench_source.hpp), so nothing a model's own reset() does can interfere with inputs_.reset()'s
// restore, regardless of which runs first.
void Vehicle::reset() noexcept {
  inputs_.reset();
  for (LoadedPlugin& plugin : plugins_) {
    plugin.model->reset();
  }
}

// M4 PR 3: see the declarations' comment (vehicle.hpp).
const Wrench& Vehicle::published_wrench() const noexcept {
  static const Wrench kZero{};
  return published_wrench_ ? *published_wrench_ : kZero;
}

double Vehicle::latched_mass() const noexcept {
  return rigid_body_.has_value() ? rigid_body_->mass_props().mass
                                 : std::numeric_limits<double>::quiet_NaN();
}

// ADR-0012: checked at p_n and the one-frame lookahead p_n + dt * p_n_dot(y) -- the lookahead is
// what makes a post-step check sufficient (a state close enough to a boundary that the next
// frame's RK4 stages would cross it fails THIS frame, before any stage evaluates there). Earth is
// checked before gravity, matching Simulation::from_config's own earth-then-gravity validate_p_n
// order; whichever fails first is the one named. Only ever called when rigid_body_.has_value()
// (both call sites in integrate() below are already behind that function's own early return); the
// guard here is redundant for correctness but keeps clang-tidy's bugprone-unchecked-optional-access
// satisfied within this function's own body, the same way integrate()'s pre-existing guard
// satisfies it for rigid_body_->set_applied_wrench()/begin_frame() there.
HaltReason Vehicle::check_domain(const double* y, const StepContext& ctx,
                                 HealthMonitor& health) noexcept {
  if (!rigid_body_.has_value()) {
    return {};
  }
  const VehicleState vs{y};
  const Eigen::Vector3d p_n = vs.p_n();
  const Eigen::Vector3d p_next = p_n + ctx.dt * rigid_body_->earth().p_n_dot(vs);
  if (!rigid_body_->earth().in_domain(p_n) || !rigid_body_->earth().in_domain(p_next)) {
    return health.report_out_of_domain(earth_plugin_path_, ctx, index_);
  }
  if (!rigid_body_->gravity().in_domain(p_n) || !rigid_body_->gravity().in_domain(p_next)) {
    return health.report_out_of_domain(gravity_plugin_path_, ctx, index_);
  }
  return {};
}

// ADR-0020 (docs/plans/pr3d-adr0020-implementation.md, Q4): factored out of integrate()'s own
// derivative functor to stay under clang-tidy's readability-function-cognitive-complexity
// threshold, the same reason check_domain above is its own function. Separates two distinct
// atmosphere failure classes at the source, before either check needs to disambiguate an
// already-produced ambiguous value. An altitude out of the model's own declared valid_range() is a
// legitimate, documented condition (AtmoState's own all-NaN-outside-range contract), never itself a
// fault -- checked and named here, before atmosphere is ever sampled, so it can never reach
// report_non_finite_environment below to be misattributed as a genuinely broken model.
//
// Returns whether this stage's wrench_sources_ loop should be skipped entirely -- true whenever
// either check below fires, regardless of the configured action, since neither returned HaltReason
// can itself answer "was a fault detected" (both are falsy under "no fault" and under a configured
// "warn" action too) -- a bug this session's own test run found (nan_count over-counted, because a
// later stage's own real fault was silently dropped once an earlier stage had already set
// wrench_fault, the same !wrench_fault guard that must gate the REPORTED reason, not the skip
// decision, incorrectly gated both). wrench_fault itself is updated under the correct, narrower
// !wrench_fault guard (first-fault-wins).
//
// What this function writes into stage_env:
// - omega_ie_b (ADR-0032): set first, on every stage, before either early return.
// - atmosphere: sampled fresh here when in range. Otherwise it keeps the latched value, which no
//   wrench_b() reads (the stage is skipped) and integrate_state() still receives.
//
// Per-frame path: no allocation, no exceptions.
//
// domain:off/nan:off interaction, and up-to-4x-per-frame counting under a non-halting action: see
// HealthMonitor::report_non_finite_environment's own doc comment (health_monitor.hpp), which is the
// authoritative account -- not restated here to avoid the two drifting apart.
bool Vehicle::prepare_stage_environment(const VehicleState& vs_stage, EnvironmentSample& stage_env,
                                        const StepContext& ctx, HealthMonitor& health,
                                        HaltReason& wrench_fault) noexcept {
  // Only ever called from integrate()'s own derivative functor, itself only reached after that
  // function's own rigid_body_.has_value() early return -- this guard is redundant for correctness
  // but keeps clang-tidy's bugprone-unchecked-optional-access satisfied within this function's own
  // body, the same reason check_domain's own guard above does (its own comment explains why).
  if (!rigid_body_.has_value()) {
    return false;
  }
  // ADR-0032: first, before either early return below. A stage those checks skip calls no
  // wrench_b(), but evaluate_derivative_stage still hands this same stage_env to every
  // integrate_state().
  stage_env.omega_ie_b = earth_rate_b(rigid_body_->earth(), vs_stage);
  bool skip_wrench_stage = false;
  if (atmosphere_ != nullptr) {
    const double altitude_m = rigid_body_->earth().altitude(vs_stage);
    if (health.domain_enabled()) {
      const AltitudeRange range = atmosphere_->valid_range();
      // NaN-safe form, matching Simulation::from_config's own init-time check (simulation.cpp) --
      // the "simplified" a<min||a>max form is false for a NaN altitude, which would silently
      // accept it instead of rejecting it.
      // NOLINTNEXTLINE(readability-simplify-boolean-expr)
      if (!(altitude_m >= range.min_m && altitude_m <= range.max_m)) {
        if (const HaltReason halt =
                health.report_out_of_domain(atmosphere_plugin_path_, ctx, index_);
            halt && !wrench_fault) {
          wrench_fault = halt;
        }
        skip_wrench_stage = true;
      }
    }
    if (!skip_wrench_stage) {
      stage_env.atmosphere = atmosphere_->at(altitude_m);  // fresh per stage
    }
  }
  if (skip_wrench_stage) {
    return true;
  }

  // Codex review finding: detection must not be inferred from the source name's own emptiness --
  // a caller that ever configured wind/atmosphere but left the diagnostic path string empty
  // (today's callers never do, but nothing here enforced it) would have silently skipped both the
  // report and the wrench-stage skip, exactly the silent corruption this guard exists to prevent.
  // non_finite_environment is tracked as its own bool, independent of what name (if any) ends up
  // in the halt. Wind checked first, matching report_non_finite_environment's own "whichever fails
  // first is the one named" rule (health_monitor.hpp).
  bool non_finite_environment = false;
  std::string_view non_finite_source;
  if (wind_configured_ && !stage_env.wind_n.allFinite()) {
    non_finite_environment = true;
    non_finite_source = wind_plugin_path_;
  } else if (atmosphere_ != nullptr && !(std::isfinite(stage_env.atmosphere.temperature_k) &&
                                         std::isfinite(stage_env.atmosphere.pressure_pa) &&
                                         std::isfinite(stage_env.atmosphere.density_kg_m3) &&
                                         std::isfinite(stage_env.atmosphere.speed_of_sound_m_s))) {
    non_finite_environment = true;
    non_finite_source = atmosphere_plugin_path_;
  }
  if (non_finite_environment) {
    if (const HaltReason halt =
            health.report_non_finite_environment(non_finite_source, ctx, index_);
        halt && !wrench_fault) {
      wrench_fault = halt;
    }
    return true;
  }
  return false;
}

// ADR-0020: see this method's own declaration comment (vehicle.hpp) for why it is factored out of
// integrate()'s own derivative functor. Per-frame path: no allocation, no exceptions.
Wrench Vehicle::accumulate_wrench(const VehicleState& vs_stage, const EnvironmentSample& stage_env,
                                  const StepContext& ctx, HealthMonitor& health,
                                  HaltReason& wrench_fault) noexcept {
  Wrench total;
  for (const WrenchEntry& entry : wrench_sources_) {
    const Wrench contribution = entry.model->wrench_b(vs_stage, stage_env);
    if (const HaltReason halt = health.check_wrench(contribution, entry.plugin_path, ctx, index_);
        halt && !wrench_fault) {
      wrench_fault = halt;  // first fault wins; keep evaluating so total/logging stay well-defined
    }
    total.force_b += contribution.force_b;
    total.moment_b += contribution.moment_b;
  }
  // Codex review (pre-existing): individually finite contributions can still sum to a non-finite
  // total (e.g. two sources each near DBL_MAX), which the per-contribution checks above cannot
  // catch -- check the accumulated wrench too, before it reaches the dynamics. Called
  // unconditionally, deliberately not short-circuited once wrench_fault is set within the loop
  // above: a short-circuit gated on a returned HaltReason would only actually skip anything under a
  // halt-configured action (check_wrench returns falsy under warn regardless of whether a fault was
  // detected), an action-dependent counting inconsistency worse than evaluating uniformly (PR 3c
  // plan critique).
  if (const HaltReason halt = health.check_wrench(total, wrench_total_name_, ctx, index_);
      halt && !wrench_fault) {
    wrench_fault = halt;
  }
  return total;
}

// M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, Design decisions 1/1b/1e/2/6): see this method's
// own declaration comment (vehicle.hpp) for the full contract. This is integrate()'s own former
// inline lambda body, factored out unchanged so derivative() below can call the identical
// arithmetic -- not a second implementation of either. Only ever called from integrate() (already
// behind its own rigid_body_/stage_ has_value() guard) or derivative() (same guard, its own top);
// the check below is redundant for correctness but keeps clang-tidy's
// bugprone-unchecked-optional-access satisfied, the same precedent check_domain's own comment
// documents for rigid_body_.
void Vehicle::evaluate_derivative_stage(double t, const double* y, double* dy,
                                        const StepContext& ctx, HealthMonitor& health,
                                        HaltReason& wrench_fault,
                                        Wrench* wrench_destination) noexcept {
  if (!stage_.has_value() || !rigid_body_.has_value()) {
    return;
  }
  const VehicleState vs_stage{y};
  EnvironmentSample stage_env = environment_;  // copies the frame-latched wind_n verbatim -- wind
                                               // stays frame-latched (Dryden's begin_frame/RNG
                                               // draw timing is unaffected by this ADR)

  const bool skip_wrench_stage =
      prepare_stage_environment(vs_stage, stage_env, ctx, health, wrench_fault);

  // ADR-0021: this stage's own trial state, copied into the stage buffer before any
  // wrench_b()/integrate_state() call this stage runs -- bind_state()/SignalRegistry reads of a
  // state-owning model's own value are sub-stage-exact.
  std::copy_n(y, stage_->size(), stage_->data());

  // A stage skipped by either check above never calls a single configured WrenchSource -- total
  // stays Wrench{}'s own zero default, an explicit, disclosed choice under a non-halting action
  // (report_non_finite_environment's own header comment documents this), not the last-known-good
  // value.
  const Wrench total = skip_wrench_stage
                           ? Wrench{}
                           : accumulate_wrench(vs_stage, stage_env, ctx, health, wrench_fault);
  if (wrench_destination != nullptr) {
    // integrate()'s own stage 0 (k1, frame-start state) only, via published_wrench_.get() --
    // publishing a later stage would be one frame ahead of what any pre-step-sampled consumer
    // (tests/physics/nesc/nesc_cases_test.cpp) expects (PR 3c plan critique, Finding 2).
    // derivative()'s own probe path always passes its own scratch destination here, since a probe
    // is always logically "stage 0" and must never write through published_wrench_ itself (M3
    // PR 5 Design decision 1).
    *wrench_destination = total;
  }
  rigid_body_->set_applied_wrench(total);
  rigid_body_->derivative(t, y, dy);
  for (const StateOwnerEntry& owner : state_owners_) {
    owner.model->integrate_state(t, vs_stage, stage_env, dy + owner.offset);
  }
}

HaltReason Vehicle::integrate(const StepContext& ctx, HealthMonitor& health) noexcept {
  // ADR-0021: init() always emplaces combined_/stage_/pre_step_scratch_/integrator_ together with
  // rigid_body_ (never independently), so this is one no-physics early return, not five -- the
  // extra has_value() checks are redundant for correctness but keep clang-tidy's
  // bugprone-unchecked-optional-access satisfied for every access below, the same precedent
  // sync_stage_buffer's own comment (vehicle.hpp) documents.
  if (!rigid_body_.has_value() || !combined_.has_value() || !stage_.has_value() ||
      !pre_step_scratch_.has_value() || !integrator_.has_value()) {
    return {};
  }

  // ADR-0012: frame 0's pre-step state has no earlier frame's post-step check behind it -- from
  // frame 1 on, a frame's pre-step state IS the previous frame's already-checked post-step state,
  // so this call is needed only here, once, to make the post-step-plus-lookahead invariant hold
  // from the very first RK4 stage the integrator ever evaluates.
  if (ctx.frame == 0 && health.domain_enabled()) {
    if (const HaltReason halt = check_domain(combined_->data(), ctx, health); halt) {
      return halt;  // combined_ untouched, like a halting wrench check below.
    }
  }

  // A trivial copy into a Vehicle-owned scratch buffer sized once at init (R2), no allocation.
  pre_step_scratch_->copy_from(*combined_);
  HaltReason wrench_fault;  // local; first fault wins, read after integrator.step()
  int stage = 0;            // 0 == k1 (frame-start state): the only stage whose
                            // wrench gets published (see below)

  // PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md): wrench_b()/integrate_state() and the
  // atmosphere sample the former reads are evaluated fresh at each of the four RK4 sub-stages
  // Integrator::step calls, using that stage's own trial state y -- not once at frame-start as
  // before. M3 PR 5: the functor now only tracks the stage counter and forwards to
  // evaluate_derivative_stage(), the shared body derivative() below also calls.
  const auto derivative = [&](double t, const double* y, double* dy) noexcept {
    evaluate_derivative_stage(t, y, dy, ctx, health, wrench_fault,
                              stage == 0 ? published_wrench_.get() : nullptr);
    ++stage;
  };

  rigid_body_->begin_frame();  // unchanged: still once per frame (mass/inertia are constant in v1
                               // -- ADR-0003 -- so re-evaluating them per stage would be pure
                               // waste, not a modeling choice, unlike wrench/atmosphere above)
  integrator_->step(derivative, ctx.t, combined_->data());  // always runs all four stages; every
                                                            // check above fires every stage, no
                                                            // early skip (see accumulate_wrench's
                                                            // own uniform-evaluation comment)

  if (wrench_fault) {
    // integrator.step() ran to completion (RK4 has no early-abort hook) before this fault was
    // discovered, so combined_ was overwritten by its own final assignment -- restore it
    // explicitly. The outward guarantee (state() reads as if the frame never ran) is identical to
    // before this ADR; only the mechanism changed, from "never touched" to "touched, then
    // restored."
    combined_->copy_from(*pre_step_scratch_);
    // ADR-0021: the stage buffer equals the combined buffer outside any derivative call, halted
    // frame or not.
    stage_->copy_from(*combined_);
    return wrench_fault;
  }

  const StateNames names{state_names_[0], state_names_[1], state_names_[2], state_names_[3]};
  const HaltReason state_halt =
      health.check_state(std::span<const double>(combined_->data(), combined_->size()), names,
                         std::span<const NamedStateSlice>(state_owner_health_slices_), ctx, index_);
  // ADR-0012: the post-step check, with its one-frame lookahead -- skipped when the state check
  // already halted (nothing new to name; a non-finite p_n cannot usefully be domain-checked).
  const HaltReason domain_halt = (!state_halt && health.domain_enabled())
                                     ? check_domain(combined_->data(), ctx, health)
                                     : HaltReason{};
  // Unconditional: ADR-0001 pins renormalization after every integrator step, halt or not. A
  // non-finite quaternion leaves post_step's own no-op branch, and q_nb untouched -- there is
  // nothing to normalize to, and the halt above already names it.
  rigid_body_->post_step(combined_->data());
  // ADR-0021: the stage buffer equals the combined buffer outside any derivative call, on a normal
  // frame too.
  stage_->copy_from(*combined_);
  return state_halt ? state_halt : domain_halt;
}

// M3 PR 5 (docs/plans/pr5-nanobind-bindings.md): see this method's own declaration comment
// (vehicle.hpp) for the full contract. Not on the per-frame path: may allocate, may throw.
void Vehicle::derivative(const StepContext& ctx, std::span<const double> y, std::span<double> dq,
                         const HealthMonitor& health) {
  if (!rigid_body_.has_value() || !combined_.has_value() || !stage_.has_value()) {
    throw Error("Vehicle::derivative: no physics configured for vehicle '" + name_ + "'");
  }
  if (wind_configured_ && !environment_latched_) {
    throw Error("Vehicle::derivative: " + wind_plugin_path_ +
                " is configured but no environment sample has ever been latched for vehicle '" +
                name_ +
                "' -- call Simulation::step()/run() at least once before evaluating the "
                "derivative of a wind-configured vehicle");
  }
  if (y.size() != combined_->size() || dq.size() != combined_->size()) {
    throw Error("Vehicle::derivative: expected a state of length " +
                std::to_string(combined_->size()) + " for vehicle '" + name_ + "', got " +
                std::to_string(y.size()) + " in and " + std::to_string(dq.size()) + " out");
  }

  // M3 PR 5 Design decision 1: lazily copy-constructs a policy-matching, counter-isolated probe
  // monitor on first use, reused thereafter -- HealthMonitor holds no SignalRegistry reference
  // (health_monitor.hpp's own private section), so this copy carries the real action/policy
  // settings forward with no registry involvement and no allocation on any call after the first.
  if (!probe_health_.has_value()) {
    probe_health_.emplace(health);
  }

  // Design decision 1d: mirrors integrate()'s own begin_frame() call site exactly -- mass/inertia
  // are constant in v1 (ADR-0003), so calling this every probe is idempotent and cheap, not a
  // special-cased "first call only" optimization.
  rigid_body_->begin_frame();

  Wrench probe_wrench;  // never published_wrench_ itself (Design decision 1)
  HaltReason wrench_fault;
  evaluate_derivative_stage(ctx.t, y.data(), dq.data(), ctx, *probe_health_, wrench_fault,
                            &probe_wrench);

  // Design decision 1e: restores the stage buffer to match the accepted combined state on every
  // exit path, including the raise below -- a probe must never leave anything behind (a
  // bind_state()/SignalRegistry-visible stale trial state) that would change the very next real
  // frame's own k1. combined_ itself was never touched by this call (y is the caller's own
  // buffer, never combined_->data()), so there is nothing to roll back on that side.
  stage_->copy_from(*combined_);

  if (wrench_fault) {
    // Design decision 1c: raise rather than return a plausible-looking wrong derivative -- the
    // same "loud failure over silent wrong answer" principle write_input's own fault check uses.
    throw Error("Vehicle::derivative: probe evaluation faulted for vehicle '" + name_ +
                "': " + std::string(wrench_fault.source));
  }
}

}  // namespace astra
