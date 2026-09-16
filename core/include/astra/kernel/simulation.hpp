// Loads a config into a runnable simulation (docs/architecture.md §4, §5, §13): one VirtualClock,
// one SignalRegistry, and -- for v1 -- exactly one Vehicle (ADR-0011: nothing here may assume
// there will only ever be one). Reads "sim.dt", "sim.frames", "vehicle.models" (PR 2's generic
// model-list schema), and -- when present -- the typed "physical.earth" / "physical.gravity" /
// "physical.mass" slots §13 shows plus "initial.state" (PLAN.md M1 PR 3). A config with no
// "physical" section runs with no EOM at all, exactly as before this PR (PR 2's dummy-plugin
// fixture): physics is opt-in, all three slots or none.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/scheduler.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/virtual_clock.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

class EarthModel;  // core/include/astra/interfaces/earth_model.hpp
class WindModel;   // core/include/astra/interfaces/wind_model.hpp

class Simulation {
 public:
  // Builds and initializes every vehicle's models from config, resolving plugin paths against
  // plugin_dir (ASTRA_STATIC_PLUGINS builds ignore it). Throws ConfigError naming the first
  // config key no model consumed (docs/architecture.md §15: unknown keys are errors), or a
  // PluginError naming a plugin that failed to load.
  [[nodiscard]] static Simulation from_config(const Config& config,
                                              std::filesystem::path plugin_dir);

  // Test-only: builds a Simulation with one Vehicle per entry of initial_states (index i, name
  // "v<i>"), each carrying the same models list (docs/architecture.md §13's vehicle.models schema).
  // Mirrors from_config's SCHEDULING AND TOPOLOGY only -- non-null earth/gravity/wind added to the
  // environment scheduler in from_config's own order (earth, gravity, wind), the same
  // EnvironmentAvailability wiring, and the same D4 shareability guard run after building its
  // vehicles (may throw ConfigError the same way, check_wind_shareable's own comment). Exists
  // because ADR-0011 keeps v1's from_config to exactly one vehicle by design (multi-vehicle configs
  // are a v2 idea): the multi-vehicle call contract §7/D1/D4 make (begin_frame/wind_n once per
  // vehicle per frame, against THAT vehicle's own state; the shareability guard) had no way to be
  // exercised at all otherwise (Cowork review, post-merge-ready).
  //
  // Deliberately does not mirror from_config's config-driven VALIDATION -- named as a class, not an
  // instance, because this is the third time in this PR a for_testing/from_config divergence was
  // found by omission rather than decision (wind's divisor guard existed without atmosphere's; wind
  // was scheduled without earth/gravity; now this) (Cowork re-review). Specifically not run here,
  // and why each is a legitimate gap for a test helper rather than an oversight:
  // check_position_representation (gravity's/wind's assumed representation against the configured
  // Earth model) -- for_testing's caller already holds live model pointers, not Config, so there is
  // no plugin-path-resolution step to hang a ConfigError off; check_sampled_every_frame --
  // for_testing has no divisor concept at all (scheduler.add is always called at the implicit
  // default of 1); earth->validate_p_n / gravity->validate_p_n (is an initial state within a
  // model's declared domain) and the atmosphere valid_range check -- these validate one
  // initial_states entry against a model's declared domain, and a test that needs to reach step()
  // from a state one of those checks would reject (e.g. to prove a downstream health check catches
  // what an init check would have) is a legitimate use of this helper, not a bug in it. A test that
  // needs production-equivalent validation should call from_config, not for_testing.
  //
  // Never called by from_config; does not change what from_config itself does with a real config.
  // wind/earth may both be null (matching physical.wind/physical.earth both being absent) -- but a
  // non-null wind with a null earth is rejected (Error), not merely disclosed: Simulation::step's
  // wind branch dereferences earth_ unconditionally whenever wind_ is non-null
  // (earth_->altitude(vs), simulation.cpp), and that branch runs inside step()'s own noexcept, so a
  // null earth_ there is undefined behavior reachable with a single vehicle -- not the same
  // "plausible but domain-wrong answer" shape as check_position_representation/validate_p_n/the
  // atmosphere valid_range check above, which is why it is enforced while those remain disclosed
  // only (an earlier draft of this comment argued otherwise from a stale analogy; adversarial
  // review's second round on this same fix caught it).
  //
  // gravity/mass_spec default to null (no physics, matching a config with no "physical" section):
  // Vehicle::integrate is then a no-op, and NO WrenchSource is ever called at all (it is gated on
  // eom_.has_value(), core/src/kernel/vehicle.cpp), the same as from_config's own physics-off path.
  // A test needing a WrenchSource actually invoked (e.g. to check what EnvironmentSample it
  // received) must supply both, mirroring Vehicle::PhysicsSpec exactly (mass is loaded fresh per
  // vehicle from mass_spec, the same way from_config's own single vehicle gets its own mass model;
  // gravity, like earth and wind, is shared across every vehicle here) -- and, like wind/earth just
  // above, THIS combination is enforced too: gravity and mass_spec must both be null or both be
  // non-null, and if both are non-null, earth must be too (Error, not ConfigError -- for_testing
  // takes no Config to name a path in). This closes a real gap adversarial review found, not a
  // disclosed one: gravity+mass_spec without earth used to build a Vehicle::PhysicsSpec holding a
  // null EarthModel*, which Vehicle::init then dereferenced unconditionally
  // (eom_.emplace(*physics->earth, ...), vehicle.cpp) -- undefined behavior, not a thrown error.
  //
  // frames defaults to 0, matching a freshly constructed Simulation's own frames_ default: a caller
  // that only ever uses step()/run(N) explicitly, as every test before this parameter existed does,
  // never needs it set. A caller relying on run() with no argument (from_config's own "run the
  // configured frame count" contract) must pass frames here explicitly -- there is no config for
  // for_testing to read "sim.frames" from. Before this parameter existed, run() with no argument
  // silently ran zero frames against a for_testing Simulation (Cowork re-review).
  //
  // Public on the shipped Simulation class, not gated behind a test-only build: accepted explicitly
  // here, not an oversight -- see PLAN.md's PR 2 line for the fuller reasoning (Cowork re-review).
  // Nothing at the compiler/linker level stops production code from calling it; the guard is the
  // name and this comment, the same posture the non-const vehicles() overload below already takes.
  [[nodiscard]] static Simulation for_testing(const PluginLoader& loader,
                                              std::span<const StateVector> initial_states,
                                              const std::vector<Vehicle::ModelSpec>& models,
                                              WindModel* wind, EarthModel* earth,
                                              GravityModel* gravity = nullptr,
                                              const Vehicle::ModelSpec* mass_spec = nullptr,
                                              std::uint64_t frames = 0);

  // Advances exactly one major frame: docs/architecture.md §5's order -- environment models,
  // vehicle models, then (when physics is configured) each vehicle's wrench accumulation, EOM
  // derivative, integrator step, and post-step renormalize, health-checked throughout (PLAN.md M1
  // PR 4). Returns the first vehicle's halt, if any; the clock does not advance on a halt, so
  // halt.frame == clock().frame() afterwards. Transport exchange and the logger join this as later
  // milestones create them.
  [[nodiscard]] HaltReason step() noexcept;
  // Steps `frames` major frames, or until one halts. Returns the halt that stopped the run, if any.
  [[nodiscard]] HaltReason run(std::uint64_t frames) noexcept;
  // Steps the frame count read from "sim.frames" at from_config time.
  [[nodiscard]] HaltReason run() noexcept { return run(frames_); }

  [[nodiscard]] const VirtualClock& clock() const noexcept { return clock_; }
  [[nodiscard]] const SignalRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] const HealthMonitor& health() const noexcept { return *health_; }
  [[nodiscard]] std::span<const Vehicle> vehicles() const noexcept { return vehicles_; }
  // Non-const overload: tests inject a broken state (e.g. scaling q_nb) through this to exercise a
  // health halt end to end.
  [[nodiscard]] std::span<Vehicle> vehicles() noexcept { return vehicles_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

 private:
  Simulation() = default;

  VirtualClock clock_;
  SignalRegistry registry_;
  std::vector<Vehicle> vehicles_;
  std::uint64_t frames_ = 0;
  Integrator integrator_;
  Scheduler environment_scheduler_;
  std::vector<LoadedPlugin> environment_plugins_;
  // Non-owning: the model lives in environment_plugins_ above (set alongside physics whenever
  // "physical" is configured, so it is always set whenever wind_ below is -- earth is required,
  // wind is not, and wind is loaded strictly after earth within the same "physical" branch).
  // Used only to compute the Earth-derived altitude WindModel::begin_frame takes, so a WindModel
  // never has to read vs.p_n() directly under an assumed representation (wind_model.hpp).
  EarthModel* earth_ = nullptr;
  // Non-owning: the model lives in environment_plugins_ above. nullptr when physical.wind is not
  // configured -- Simulation::step then samples still air (EnvironmentSample's own default).
  WindModel* wind_ = nullptr;
  // unique_ptr, not by value: from_config returns Simulation by value, and HealthMonitor::init
  // registers &counter_ pointers into registry_, so the monitor's address must survive every move
  // this Simulation goes through (a by-value member would dangle those pointers on the first move).
  std::unique_ptr<HealthMonitor> health_;
};

}  // namespace astra
