// One vehicle's own state, models, and schedule (docs/architecture.md §7, ADR-0011): a Simulation
// holds a list of these of length one in v1, but nothing here assumes that -- each Vehicle owns
// its own StateVector and its own signal namespace ("v0.aero.CL").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
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

  // name is the signal-namespace prefix ("v0"); state starts zeroed -- PR 3's EarthModel and EOM
  // own giving it a meaningful initial value.
  Vehicle(std::uint32_t index, std::string name);

  // Loads and initializes each model in models, in order, adding it to this vehicle's Scheduler.
  // Every model's init() runs under a SignalRegistry::PrefixScope for this vehicle's name, so
  // "dummy.steps" is published as "v0.dummy.steps" (ADR-0011). Init-time only: may allocate, may
  // throw (PluginError, ConfigError).
  void init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
            SignalRegistry& registry);

  void step(const StepContext& ctx) noexcept { scheduler_.step(ctx); }

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
};

}  // namespace astra
