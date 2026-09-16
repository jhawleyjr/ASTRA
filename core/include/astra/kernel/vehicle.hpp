// One vehicle's own state, models, and schedule (docs/architecture.md §7, ADR-0011): a Simulation
// holds a list of these of length one in v1, but nothing here assumes that -- each Vehicle owns
// its own StateVector and its own signal namespace ("v0.aero.CL").
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/eom.hpp"
#include "astra/kernel/health_monitor.hpp"
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

  // Which optional environment slots Simulation::step will actually populate into EnvironmentSample
  // this frame (environment_sample.hpp). init() rejects a WrenchSource whose requires_environment()
  // asks for wind when this is false, naming physical.wind. Deliberately no "atmosphere" field yet:
  // see init()'s own comment for why that requirement is rejected unconditionally instead.
  struct EnvironmentAvailability {
    bool wind = false;
  };

  // name is the signal-namespace prefix ("v0"); state starts zeroed until init()'s caller sets it
  // (Simulation::from_config reads "initial.state", or leaves it zeroed when physics is off).
  Vehicle(std::uint32_t index, std::string name);

  // Loads and initializes each model in models, in order, adding it to this vehicle's Scheduler,
  // then -- when physics is non-null -- loads this vehicle's own mass-properties model the same way
  // and constructs this vehicle's Eom against it and the shared earth/gravity models physics names.
  // Every model's init() runs under a SignalRegistry::PrefixScope for this vehicle's name, so
  // "dummy.steps" is published as "v0.dummy.steps" (ADR-0011). As each WrenchSource is discovered,
  // checks its requires_environment() (wrench_source.hpp) against availability: a wind requirement
  // with availability.wind false throws ConfigError naming physical.wind; an atmosphere requirement
  // throws unconditionally naming physical.atmosphere -- no PR before M2 PR 3 can populate
  // EnvironmentSample::atmosphere (environment_sample.hpp), so no config, not even one naming
  // physical.atmosphere, can satisfy that requirement yet. Init-time only: may allocate, may throw
  // (PluginError, ConfigError).
  void init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
            SignalRegistry& registry, const PhysicsSpec* physics,
            EnvironmentAvailability availability);

  void step(const StepContext& ctx) noexcept { scheduler_.step(ctx); }

  // This frame's environment sample (docs/architecture.md §5), latched by Simulation::step before
  // step()/integrate() run (environment_sample.hpp). Defaults to still air / all-NaN atmosphere,
  // matching a Simulation with no physical.wind/physical.atmosphere configured.
  void set_environment(const EnvironmentSample& sample) noexcept { environment_ = sample; }
  [[nodiscard]] const EnvironmentSample& environment() const noexcept { return environment_; }

  // Runs this frame's wrench accumulation, EOM derivative, integrator step, and post-step
  // renormalize (docs/architecture.md §5): a no-op returning a falsey HaltReason when this vehicle
  // has no Eom (physics off). Order: health-check each WrenchSource's contribution before summing
  // it (a halt here leaves state() untouched -- the EOM never sees a bad wrench); health-check the
  // accumulated total too (Codex review: individually finite contributions can overflow to Inf on
  // summation, e.g. two sources each near DBL_MAX -- a check per contribution alone would miss
  // that and hand the EOM a broken wrench with no wrench fault ever reported); integrate; health-
  // check the resulting state before renormalization (PLAN.md M1 PR 4: "|q| drift > 1e-6 before
  // renormalization"); renormalize unconditionally, halt or not (ADR-0001: renormalized after every
  // integrator step, an invariant a halt does not suspend); return the state check's halt, if any.
  // Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason integrate(const Integrator& integrator, const StepContext& ctx,
                                     HealthMonitor& health) noexcept;

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] StateVector& state() noexcept { return state_; }
  [[nodiscard]] const StateVector& state() const noexcept { return state_; }
  [[nodiscard]] const Scheduler& scheduler() const noexcept { return scheduler_; }

 private:
  // A vehicle.models entry that resolved to a WrenchSource, kept alongside the plugin path its
  // config named it by (§15: "the offending plugin named") -- Scheduler::Entry only stores Model*.
  struct WrenchEntry {
    WrenchSource* model;
    std::string plugin_path;
  };

  std::uint32_t index_;
  std::string name_;
  StateVector state_ = StateVector::Zero();
  EnvironmentSample environment_;
  std::vector<LoadedPlugin> plugins_;
  Scheduler scheduler_;
  std::optional<Eom> eom_;
  std::vector<WrenchEntry> wrench_sources_;
  // "<name_>.state.{p_n,v_b,q_nb,omega_b}", built once at init from this vehicle's own name so a
  // halt never carries a "v0" literal (ADR-0011).
  std::array<std::string, 4> state_names_;
  // "<name_>.wrench_total": names a halt on the accumulated wrench, which is not any one plugin's
  // fault to name (Codex review finding above) -- a sum can be non-finite even when every summed
  // contribution was finite.
  std::string wrench_total_name_;
};

}  // namespace astra
