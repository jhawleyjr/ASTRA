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
  // "v<i>"), each carrying the same models list (docs/architecture.md §13's vehicle.models schema)
  // and wired to wind/earth/gravity the same way from_config wires the single vehicle it ever
  // builds -- including scheduling non-null earth/gravity/wind in from_config's own order (earth,
  // gravity, wind) and running the same shareability guard from_config runs after building its
  // vehicles (may throw ConfigError the same way). Exists because ADR-0011 keeps v1's from_config
  // to exactly one vehicle by design (multi-vehicle configs are a v2 idea): the multi-vehicle call
  // contract §7/D1/D4 make (begin_frame/wind_n once per vehicle per frame, against THAT vehicle's
  // own state; the shareability guard) had no way to be exercised at all otherwise (Cowork review,
  // post-merge-ready). Never called by from_config; does not change what from_config itself does
  // with a real config. wind/earth may be null (matching physical.wind/physical.earth both being
  // absent); a non-null wind with a null earth is a caller error (Simulation::step would then
  // dereference a null earth_ -- the same invariant from_config's own wiring already relies on).
  //
  // gravity/mass_spec default to null (no physics, matching a config with no "physical" section):
  // Vehicle::integrate is then a no-op, and NO WrenchSource is ever called at all (it is gated on
  // eom_.has_value(), core/src/kernel/vehicle.cpp), the same as from_config's own physics-off path.
  // A test needing a WrenchSource actually invoked (e.g. to check what EnvironmentSample it
  // received) must supply both, mirroring Vehicle::PhysicsSpec exactly (mass is loaded fresh per
  // vehicle from mass_spec, the same way from_config's own single vehicle gets its own mass model;
  // gravity, like earth and wind, is shared across every vehicle here).
  [[nodiscard]] static Simulation for_testing(const PluginLoader& loader,
                                              std::span<const StateVector> initial_states,
                                              const std::vector<Vehicle::ModelSpec>& models,
                                              WindModel* wind, EarthModel* earth,
                                              GravityModel* gravity = nullptr,
                                              const Vehicle::ModelSpec* mass_spec = nullptr);

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
