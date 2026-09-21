#include "astra/kernel/vehicle.hpp"

#include <cmath>
#include <utility>

#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"

namespace astra {

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry, const PhysicsSpec* physics,
                   EnvironmentAvailability availability, std::string wind_plugin_path) {
  state_names_ = {name_ + ".state.p_n", name_ + ".state.v_b", name_ + ".state.q_nb",
                  name_ + ".state.omega_b"};
  wrench_total_name_ = name_ + ".wrench_total";
  // ADR-0020: what report_non_finite_environment names on a non-finite wind_n halt, and whether
  // wind_n is even a real, checkable value this run (vs. still air's own legitimate zero default).
  wind_configured_ = availability.wind;
  wind_plugin_path_ = std::move(wind_plugin_path);

  plugins_.reserve(models.size() + (physics != nullptr ? 1 : 0));
  wrench_sources_.reserve(models.size());  // upper bound: not every model is a WrenchSource
  for (const ModelSpec& spec : models) {
    LoadedPlugin loaded = loader.load(spec.plugin_path);
    // Captured before the move below: .get() does not affect ownership, and the model stays alive
    // for the vehicle's lifetime once plugins_ owns it.
    WrenchSource* wrench_source = nullptr;
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      loaded.model->init(spec.config, registry);
      // ADR-0017 (Joe's review round 1, finding 1): bind_inputs() must run while this PrefixScope
      // is still open, so every write-seam name lands correctly namespaced ("v0.controls.elevator",
      // not "controls.elevator") -- moved inside this block for that reason; the requires_
      // environment() check below never touches the registry's prefix, so it stays outside.
      wrench_source = dynamic_cast<WrenchSource*>(loaded.model.get());
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
    plugins_.push_back(std::move(loaded));
  }

  if (physics != nullptr) {
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
    eom_.emplace(*physics->earth, *physics->gravity, *mass_model);
    plugins_.push_back(std::move(mass_loaded));
    // ADR-0012: what a domain halt names. Eom (just above) retains the Earth/gravity pointers
    // themselves; this vehicle retains their names, the same lifetime discipline as
    // state_names_/wrench_total_name_.
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

// ADR-0012: checked at p_n and the one-frame lookahead p_n + dt * p_n_dot(y) -- the lookahead is
// what makes a post-step check sufficient (a state close enough to a boundary that the next
// frame's RK4 stages would cross it fails THIS frame, before any stage evaluates there). Earth is
// checked before gravity, matching Simulation::from_config's own earth-then-gravity validate_p_n
// order; whichever fails first is the one named. Only ever called when eom_.has_value() (both call
// sites in integrate() below are already behind that function's own early return); the guard here
// is redundant for correctness but keeps clang-tidy's bugprone-unchecked-optional-access satisfied
// within this function's own body, the same way integrate()'s pre-existing guard satisfies it for
// eom_->set_applied_wrench()/begin_frame() there.
HaltReason Vehicle::check_domain(const StateVector& y, const StepContext& ctx,
                                 HealthMonitor& health) noexcept {
  if (!eom_.has_value()) {
    return {};
  }
  const VehicleState vs{y};
  const Eigen::Vector3d p_n = vs.p_n();
  const Eigen::Vector3d p_next = p_n + ctx.dt * eom_->earth().p_n_dot(vs);
  if (!eom_->earth().in_domain(p_n) || !eom_->earth().in_domain(p_next)) {
    return health.report_out_of_domain(earth_plugin_path_, ctx, index_);
  }
  if (!eom_->gravity().in_domain(p_n) || !eom_->gravity().in_domain(p_next)) {
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
// !wrench_fault guard (first-fault-wins). stage_env.atmosphere is sampled fresh here when in range,
// left untouched (never read) otherwise. Per-frame path: no allocation, no exceptions.
//
// domain:off/nan:off interaction, and up-to-4x-per-frame counting under a non-halting action: see
// HealthMonitor::report_non_finite_environment's own doc comment (health_monitor.hpp), which is the
// authoritative account -- not restated here to avoid the two drifting apart.
bool Vehicle::prepare_stage_environment(const VehicleState& vs_stage, EnvironmentSample& stage_env,
                                        const StepContext& ctx, HealthMonitor& health,
                                        HaltReason& wrench_fault) noexcept {
  // Only ever called from integrate()'s own derivative functor, itself only reached after that
  // function's own eom_.has_value() early return -- this guard is redundant for correctness but
  // keeps clang-tidy's bugprone-unchecked-optional-access satisfied within this function's own
  // body, the same reason check_domain's own guard above does (its own comment explains why).
  if (!eom_.has_value()) {
    return false;
  }
  bool skip_wrench_stage = false;
  if (atmosphere_ != nullptr) {
    const double altitude_m = eom_->earth().altitude(vs_stage);
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

HaltReason Vehicle::integrate(const Integrator& integrator, const StepContext& ctx,
                              HealthMonitor& health) noexcept {
  if (!eom_.has_value()) {
    return {};
  }

  // ADR-0012: frame 0's pre-step state has no earlier frame's post-step check behind it -- from
  // frame 1 on, a frame's pre-step state IS the previous frame's already-checked post-step state,
  // so this call is needed only here, once, to make the post-step-plus-lookahead invariant hold
  // from the very first RK4 stage the integrator ever evaluates.
  if (ctx.frame == 0 && health.domain_enabled()) {
    if (const HaltReason halt = check_domain(state_, ctx, health); halt) {
      return halt;  // state_ untouched, like a halting wrench check below.
    }
  }

  // PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md): wrench_b() and the atmosphere sample it
  // reads are evaluated fresh at each of the four RK4 sub-stages Integrator::step calls, using that
  // stage's own y -- not once at frame-start as before. Eom itself is unchanged: this functor wraps
  // it, calling set_applied_wrench() again immediately before each of the four (*eom_)(t, y) calls.
  const StateVector pre_step_state = state_;  // trivial 13-double stack copy, no allocation
  HaltReason wrench_fault;  // local; first fault wins, read after integrator.step()
  int stage = 0;            // 0 == k1 (frame-start state): the only stage whose
                            // wrench gets published (see below)

  const auto derivative = [&](double t, const StateVector& y) noexcept -> StateVector {
    const VehicleState vs_stage{y};
    EnvironmentSample stage_env = environment_;  // copies the frame-latched wind_n verbatim -- wind
                                                 // stays frame-latched (Dryden's begin_frame/RNG
                                                 // draw timing is unaffected by this PR)

    const bool skip_wrench_stage =
        prepare_stage_environment(vs_stage, stage_env, ctx, health, wrench_fault);

    // A stage skipped by either check above never calls a single configured WrenchSource -- total
    // stays Wrench{}'s own zero default, an explicit, disclosed choice under a non-halting action
    // (report_non_finite_environment's own header comment documents this), not the last-known-good
    // value.
    Wrench total;
    if (!skip_wrench_stage) {
      for (const WrenchEntry& entry : wrench_sources_) {
        const Wrench contribution = entry.model->wrench_b(vs_stage, stage_env);
        if (const HaltReason halt =
                health.check_wrench(contribution, entry.plugin_path, ctx, index_);
            halt && !wrench_fault) {
          wrench_fault =
              halt;  // first fault wins; keep evaluating so total/logging stay well-defined
        }
        total.force_b += contribution.force_b;
        total.moment_b += contribution.moment_b;
      }
      // Codex review (pre-existing): individually finite contributions can still sum to a
      // non-finite total (e.g. two sources each near DBL_MAX), which the per-contribution checks
      // above cannot catch -- check the accumulated wrench too, before it reaches the EOM. Called
      // uniformly every stage the loop above actually runs, deliberately not short-circuited once
      // wrench_fault is set within this loop: a short-circuit gated on a returned HaltReason would
      // only actually skip anything under a halt-configured action (check_wrench returns falsy
      // under warn regardless of whether a fault was detected), an action-dependent counting
      // inconsistency worse than evaluating uniformly (PR 3c plan critique). This is a materially
      // different skip than the pre-emptive one above: that one never starts the loop at all, based
      // on input already known bad before any wrench_b() call; this rule is about not stopping
      // partway through a loop that did start.
      if (const HaltReason halt = health.check_wrench(total, wrench_total_name_, ctx, index_);
          halt && !wrench_fault) {
        wrench_fault = halt;
      }
    }
    if (stage == 0) {
      // The vehicle's published applied-wrench signal is the frame-start (k1) total only, never
      // k2..k4's -- publishing a later stage would be one frame ahead of what any pre-step-sampled
      // consumer (tests/physics/nesc/nesc_cases_test.cpp) expects (PR 3c plan critique, Finding 2).
      // No WrenchSource signature change: wrench_b() takes no t, so this is tracked by this plain
      // stage counter, not by inspecting t.
      *published_wrench_ = total;
    }
    ++stage;
    eom_->set_applied_wrench(total);
    return (*eom_)(t, y);
  };

  eom_->begin_frame();  // unchanged: still once per frame (mass/inertia are constant in v1 --
                        // ADR-0003 -- so re-evaluating them per stage would be pure waste, not a
                        // modeling choice, unlike wrench/atmosphere above)
  integrator.step(derivative, ctx.t, state_);  // always runs all four stages; every check above
                                               // fires every stage, no early skip (see the
                                               // uniform-evaluation comment above)

  if (wrench_fault) {
    // integrator.step() ran to completion (RK4 has no early-abort hook) before this fault was
    // discovered, so state_ was overwritten by its own final assignment -- restore it explicitly.
    // The outward guarantee (state() reads as if the frame never ran) is identical to before this
    // PR; only the mechanism changed, from "never touched" to "touched, then restored."
    state_ = pre_step_state;
    return wrench_fault;
  }

  const StateNames names{state_names_[0], state_names_[1], state_names_[2], state_names_[3]};
  const HaltReason state_halt = health.check_state(state_, names, ctx, index_);
  // ADR-0012: the post-step check, with its one-frame lookahead -- skipped when the state check
  // already halted (nothing new to name; a non-finite p_n cannot usefully be domain-checked).
  const HaltReason domain_halt =
      (!state_halt && health.domain_enabled()) ? check_domain(state_, ctx, health) : HaltReason{};
  // Unconditional: ADR-0001 pins renormalization after every integrator step, halt or not. A
  // non-finite quaternion leaves post_step_renormalize's own false return, and q untouched -- there
  // is nothing to normalize to, and the halt above already names it.
  static_cast<void>(post_step_renormalize(state_));
  return state_halt ? state_halt : domain_halt;
}

}  // namespace astra
