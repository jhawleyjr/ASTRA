#include "astra/kernel/simulation.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "astra/errors.hpp"
#include "astra/plugin/loader.hpp"

namespace astra {

namespace {

// A signed config value cast straight to an unsigned field wraps instead of erroring on a negative
// input (Codex review, PR M1-2): "sim.frames: -1" became UINT64_MAX, turning a typo'd config into
// an effectively-infinite run instead of the typed init-time error docs/architecture.md §15
// requires. Separate from checked_divisor below because UINT64_MAX does not fit in an int64_t (the
// type every yaml-cpp integer comes back as), so the two checks cannot share one bound: only the
// lower bound is at risk here, since every non-negative int64_t already fits in a uint64_t.
std::uint64_t checked_frame_count(const Config& config, std::int64_t value) {
  if (value < 0) {
    throw ConfigError(config.origin(), "sim.frames", "sim.frames must not be negative");
  }
  return static_cast<std::uint64_t>(value);
}

// A divisor outside [1, UINT32_MAX] wraps on the cast to Scheduler's divisor type (Codex review,
// PR M1-2): "divisor: -1" becomes UINT32_MAX, silently turning a typo into "run once every 4.29
// billion frames" instead of a rejected config; "divisor: 0" is likewise a config-path-naming
// ConfigError here rather than Scheduler::add's generic Error.
std::uint32_t checked_divisor(const Config& entry, std::int64_t value) {
  constexpr auto kMaxDivisor = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
  if (value < 1 || value > kMaxDivisor) {
    const std::string path = entry.path().empty() ? "divisor" : entry.path() + ".divisor";
    throw ConfigError(entry.origin(), path, "divisor must be between 1 and 4294967295");
  }
  return static_cast<std::uint32_t>(value);
}

// Accepts both docs/architecture.md §13 spellings for one plugin slot: a plain scalar
// ("plugins/physical/earth/flat") or a map with its own config next to "plugin"
// ({ plugin: ..., seed: 42, divisor: 10 }). Either way the returned ModelSpec::config is the
// entry itself, so a model that reads its own keys can (dummy reads none, in this PR).
Vehicle::ModelSpec resolve_model_spec(const Config& entry) {
  if (entry.is_scalar()) {
    return Vehicle::ModelSpec{entry.text(), entry, 1};
  }
  std::string plugin_path = entry.text("plugin");
  const std::uint32_t divisor = checked_divisor(entry, entry.integer("divisor", 1));
  return Vehicle::ModelSpec{std::move(plugin_path), entry, divisor};
}

}  // namespace

Simulation Simulation::from_config(const Config& config, std::filesystem::path plugin_dir) {
  Simulation sim;
  const double dt = config.number("sim.dt");
  sim.frames_ = checked_frame_count(config, config.integer("sim.frames"));
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
