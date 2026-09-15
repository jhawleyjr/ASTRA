// One vehicle's own state, models, and schedule (docs/architecture.md §7, ADR-0011): a Simulation
// holds a list of these of length one in v1, but nothing here assumes that -- each Vehicle owns
// its own StateVector and its own signal namespace ("v0.aero.CL").
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/kernel/eom.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/scheduler.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/loader.hpp"
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
  // running with no physics, exactly as before).
  struct PhysicsSpec {
    ModelSpec mass;
    EarthModel* earth;
    GravityModel* gravity;
  };

  // name is the signal-namespace prefix ("v0"); state starts zeroed until init()'s caller sets it
  // (Simulation::from_config reads "initial.state", or leaves it zeroed when physics is off).
  Vehicle(std::uint32_t index, std::string name);

  // Loads and initializes each model in models, in order, adding it to this vehicle's Scheduler,
  // then -- when physics is non-null -- loads this vehicle's own mass-properties model the same way
  // and constructs this vehicle's Eom against it and the shared earth/gravity models physics names.
  // Every model's init() runs under a SignalRegistry::PrefixScope for this vehicle's name, so
  // "dummy.steps" is published as "v0.dummy.steps" (ADR-0011). Init-time only: may allocate, may
  // throw (PluginError, ConfigError).
  void init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
            SignalRegistry& registry, const PhysicsSpec* physics);

  void step(const StepContext& ctx) noexcept { scheduler_.step(ctx); }

  // Runs this frame's EOM derivative, integrator step, and post-step renormalize
  // (docs/architecture.md §5): a no-op when this vehicle has no Eom (physics off). Renormalize's
  // [[nodiscard]] bool is discarded explicitly; routing a failed renormalize (a zero or non-finite
  // quaternion) to a halt is PLAN.md M1 PR 4's named acceptance item, not this one's.
  void integrate(const Integrator& integrator, const StepContext& ctx) noexcept {
    if (!eom_.has_value()) {
      return;
    }
    eom_->begin_frame();
    integrator.step(*eom_, ctx.t, state_);
    static_cast<void>(post_step_renormalize(state_));
  }

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] StateVector& state() noexcept { return state_; }
  [[nodiscard]] const StateVector& state() const noexcept { return state_; }
  [[nodiscard]] const Scheduler& scheduler() const noexcept { return scheduler_; }

 private:
  std::uint32_t index_;
  std::string name_;
  StateVector state_ = StateVector::Zero();
  std::vector<LoadedPlugin> plugins_;
  Scheduler scheduler_;
  std::optional<Eom> eom_;
};

}  // namespace astra
