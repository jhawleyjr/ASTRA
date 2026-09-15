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
#include <span>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/scheduler.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/virtual_clock.hpp"
#include "astra/plugin/loader.hpp"
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

  // Advances exactly one major frame: docs/architecture.md §5's order -- environment models,
  // vehicle models, then (when physics is configured) each vehicle's EOM derivative, integrator
  // step, and post-step renormalize. Transport exchange and the logger join this as PR 4 and later
  // milestones create them.
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
  Integrator integrator_;
  Scheduler environment_scheduler_;
  std::vector<LoadedPlugin> environment_plugins_;
};

}  // namespace astra
