// Loads a config into a runnable simulation (docs/architecture.md §4, §5, §13): one VirtualClock,
// one SignalRegistry, and -- for v1 -- exactly one Vehicle (ADR-0011: nothing here may assume
// there will only ever be one). Reads "sim.dt", "sim.frames", and "vehicle.models" (this PR's one
// generic model-list schema; the typed physical./hardware. slots of §13 arrive with the
// milestones that create those interfaces).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/virtual_clock.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

class Simulation {
 public:
  // Builds and initializes every vehicle's models from config, resolving plugin paths against
  // plugin_dir (ASTRA_STATIC_PLUGINS builds ignore it). Throws ConfigError naming the first
  // config key no model consumed (docs/architecture.md §15: unknown keys are errors), or a
  // PluginError naming a plugin that failed to load.
  [[nodiscard]] static Simulation from_config(const Config& config,
                                              std::filesystem::path plugin_dir);

  // Advances exactly one major frame: docs/architecture.md §5's order, restricted for now to the
  // stage this PR has a model for ("vehicle models"). environment / EOM / integrator / post-step /
  // logger join this as PR 3 and PR 4 create them.
  void step() noexcept;
  // Steps `frames` major frames.
  void run(std::uint64_t frames) noexcept;
  // Steps the frame count read from "sim.frames" at from_config time.
  void run() noexcept { run(frames_); }

  [[nodiscard]] const VirtualClock& clock() const noexcept { return clock_; }
  [[nodiscard]] const SignalRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] std::span<const Vehicle> vehicles() const noexcept { return vehicles_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

 private:
  Simulation() = default;

  VirtualClock clock_;
  SignalRegistry registry_;
  std::vector<Vehicle> vehicles_;
  std::uint64_t frames_ = 0;
};

}  // namespace astra
