#include "astra/kernel/simulation.hpp"

#include <string>
#include <utility>
#include <vector>

#include "astra/errors.hpp"
#include "astra/plugin/loader.hpp"

namespace astra {

namespace {

// Accepts both docs/architecture.md §13 spellings for one plugin slot: a plain scalar
// ("plugins/physical/earth/flat") or a map with its own config next to "plugin"
// ({ plugin: ..., seed: 42, divisor: 10 }). Either way the returned ModelSpec::config is the
// entry itself, so a model that reads its own keys can (dummy reads none, in this PR).
Vehicle::ModelSpec resolve_model_spec(const Config& entry) {
  if (entry.is_scalar()) {
    return Vehicle::ModelSpec{entry.text(), entry, 1};
  }
  std::string plugin_path = entry.text("plugin");
  const auto divisor = static_cast<std::uint32_t>(entry.integer("divisor", 1));
  return Vehicle::ModelSpec{std::move(plugin_path), entry, divisor};
}

}  // namespace

Simulation Simulation::from_config(const Config& config, std::filesystem::path plugin_dir) {
  Simulation sim;
  const double dt = config.number("sim.dt");
  sim.frames_ = static_cast<std::uint64_t>(config.integer("sim.frames"));
  sim.clock_ = VirtualClock(dt);

  const PluginLoader loader(std::move(plugin_dir));
  const Config vehicle_models = config.at("vehicle.models");
  std::vector<Vehicle::ModelSpec> specs;
  specs.reserve(vehicle_models.size());
  for (std::size_t i = 0; i < vehicle_models.size(); ++i) {
    specs.push_back(resolve_model_spec(vehicle_models.at(i)));
  }

  Vehicle vehicle(0, "v0");
  vehicle.init(loader, specs, sim.registry_);
  sim.vehicles_.push_back(std::move(vehicle));
  sim.registry_.freeze();

  const std::vector<std::string> unknown = config.unknown_keys();
  if (!unknown.empty()) {
    throw ConfigError(config.origin(), unknown.front(),
                      "unknown config key (docs/architecture.md §15: unknown keys are errors)");
  }
  return sim;
}

void Simulation::step() noexcept {
  const StepContext ctx = clock_.context();
  for (Vehicle& vehicle : vehicles_) {
    vehicle.step(ctx);
  }
  clock_.advance();
}

void Simulation::run(std::uint64_t frames) noexcept {
  for (std::uint64_t i = 0; i < frames; ++i) {
    step();
  }
}

}  // namespace astra
