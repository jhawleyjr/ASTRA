#include "astra/kernel/simulation.hpp"

#include <Eigen/Core>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/util/frames.hpp"

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
// entry itself, so a model that reads its own keys can. Shared by vehicle.models entries and by
// the typed physical.earth / physical.gravity / physical.mass slots (§13 uses the same convention
// for both).
Vehicle::ModelSpec resolve_model_spec(const Config& entry) {
  if (entry.is_scalar()) {
    return Vehicle::ModelSpec{entry.text(), entry, 1};
  }
  std::string plugin_path = entry.text("plugin");
  const std::uint32_t divisor = checked_divisor(entry, entry.integer("divisor", 1));
  return Vehicle::ModelSpec{std::move(plugin_path), entry, divisor};
}

// Loads and initializes one environment model with no PrefixScope open: earth and gravity are
// shared across vehicles (ADR-0011), so their signals publish unprefixed ("gravity.g", not
// "v0.gravity.g"), unlike every vehicle.models entry and the per-vehicle mass model.
LoadedPlugin load_environment_model(const PluginLoader& loader, const Vehicle::ModelSpec& spec,
                                    SignalRegistry& registry) {
  LoadedPlugin loaded = loader.load(spec.plugin_path);
  loaded.model->init(spec.config, registry);
  return loaded;
}

// M2: rejects a gravity model whose assumed position representation (position_representation.hpp)
// does not match what the configured Earth model provides -- e.g. gravity/wgs84_j2 paired with
// earth/flat would otherwise read NED metres as radians. kAny ("does not read p_n's geometry") is
// always compatible. config_path names the assuming model's own slot ("physical.gravity"); the
// message also names the Earth slot it disagrees with. The same check will apply to
// physical.atmosphere once that slot exists (a follow-up PR: AtmosphereModel grew too large to land
// alongside the Earth/gravity models in one PR, PLAN.md's own size contingency).
void check_position_representation(PositionRepresentation assumed, PositionRepresentation provided,
                                   const Vehicle::ModelSpec& assuming_spec,
                                   const Vehicle::ModelSpec& earth_spec) {
  if (assumed == PositionRepresentation::kAny || assumed == provided) {
    return;
  }
  throw ConfigError(assuming_spec.config.origin(), assuming_spec.config.path(),
                    assuming_spec.plugin_path + " assumes a position representation that " +
                        earth_spec.plugin_path + " (" + earth_spec.config.path() +
                        ") does not provide");
}

// A 3-vector at parent.key, or zero when the key is absent -- "initial.state" reads every field
// this way, so a config that gives only euler_nb still gets zero position/velocity/rate rather than
// an error. Each component must be finite (Codex review: an unvalidated NaN/Inf here starts the sim
// already broken instead of failing the §15 pre-run "no NaN/Inf anywhere in state" check).
Eigen::Vector3d read_vector3(const Config& parent, std::string_view key) {
  if (!parent.has(key)) {
    return Eigen::Vector3d::Zero();
  }
  const Config entry = parent.at(key);
  Eigen::Vector3d v;
  for (std::size_t i = 0; i < 3; ++i) {
    const Config component = entry.at(i);
    const double value = component.number();
    if (!std::isfinite(value)) {
      throw ConfigError(component.origin(), component.path(), "must be finite");
    }
    v(static_cast<Eigen::Index>(i)) = value;
  }
  return v;
}

// "initial.state" (PLAN.md M1 PR 3): p_n, v_b, omega_b as 3-vectors and attitude as
// euler_nb = [psi, theta, phi] (rad, ADR-0002), converted once with quaternion_from_euler. Any
// missing field, or the whole "initial.state" block, defaults to zero -- absent euler_nb means
// identity attitude. Read only when physics is configured (from_config's caller only calls this
// under "physical"); "initial.state" given without a "physical" section is meaningless and is left
// unread, so it surfaces as an unknown-key error like any other unconsumed key (§15).
StateVector read_initial_state(const Config& config) {
  Eigen::Vector3d p_n = Eigen::Vector3d::Zero();
  Eigen::Vector3d v_b = Eigen::Vector3d::Zero();
  Eigen::Vector3d omega_b = Eigen::Vector3d::Zero();
  frames::EulerAngles euler_nb = frames::EulerAngles::Zero();
  if (config.has("initial.state")) {
    const Config state_config = config.at("initial.state");
    // Codex review: without this, "initial: { state: [1, 2, 3] }" or a bare scalar marks the whole
    // block consumed (Config::at's own bookkeeping) while every field lookup below silently reads
    // as absent, so a malformed block produces a valid-looking all-zero state instead of an error.
    if (!state_config.is_map()) {
      throw ConfigError(state_config.origin(), state_config.path(), "must be a map");
    }
    // p_n's units depend on the configured Earth model (ADR-0003): NED metres for flat, geodetic
    // latitude/longitude in rad and altitude in m for round/wgs84_rotating.
    // EarthModel::validate_p_n (called below, once the Earth model is loaded) rejects a value that
    // would be meaningless or singular under that Earth model's own representation.
    p_n = read_vector3(state_config, "p_n");
    v_b = read_vector3(state_config, "v_b");
    omega_b = read_vector3(state_config, "omega_b");
    euler_nb = read_vector3(state_config, "euler_nb");
  }
  StateVector y0;
  y0.segment<state::kPositionSize>(state::kPosition) = p_n;
  y0.segment<state::kVelocitySize>(state::kVelocity) = v_b;
  y0.segment<state::kQuaternionSize>(state::kQuaternion) = frames::quaternion_from_euler(euler_nb);
  y0.segment<state::kAngularRateSize>(state::kAngularRate) = omega_b;
  return y0;
}

}  // namespace

Simulation Simulation::from_config(const Config& config, std::filesystem::path plugin_dir) {
  Simulation sim;
  const double dt = config.number("sim.dt");
  sim.frames_ = checked_frame_count(config, config.integer("sim.frames"));
  sim.clock_ = VirtualClock(dt);
  sim.integrator_ = Integrator(IntegratorKind::kRk4, dt);

  const PluginLoader loader(std::move(plugin_dir));
  const Config vehicle_models = config.at("vehicle.models");
  std::vector<Vehicle::ModelSpec> specs;
  specs.reserve(vehicle_models.size());
  for (std::size_t i = 0; i < vehicle_models.size(); ++i) {
    specs.push_back(resolve_model_spec(vehicle_models.at(i)));
  }

  Vehicle vehicle(0, "v0");
  // Physics is opt-in, all three slots or none: a config with no "physical" section (PR 2's
  // dummy-plugin fixture) runs exactly as before this PR, with no EarthModel/GravityModel/
  // MassPropertiesModel loaded and no Eom, so Vehicle::integrate is a no-op for it.
  if (config.has("physical")) {
    const Config physical = config.at("physical");
    const Vehicle::ModelSpec earth_spec = resolve_model_spec(physical.at("earth"));
    const Vehicle::ModelSpec gravity_spec = resolve_model_spec(physical.at("gravity"));
    Vehicle::ModelSpec mass_spec = resolve_model_spec(physical.at("mass"));

    LoadedPlugin earth_loaded = load_environment_model(loader, earth_spec, sim.registry_);
    auto* const earth = dynamic_cast<EarthModel*>(earth_loaded.model.get());
    if (earth == nullptr) {
      throw PluginLoadError(earth_spec.plugin_path, "does not implement EarthModel");
    }
    LoadedPlugin gravity_loaded = load_environment_model(loader, gravity_spec, sim.registry_);
    auto* const gravity = dynamic_cast<GravityModel*>(gravity_loaded.model.get());
    if (gravity == nullptr) {
      throw PluginLoadError(gravity_spec.plugin_path, "does not implement GravityModel");
    }
    check_position_representation(gravity->assumed_position_representation(),
                                  earth->position_representation(), gravity_spec, earth_spec);

    sim.environment_scheduler_.add(*earth, earth_spec.divisor);
    sim.environment_scheduler_.add(*gravity, gravity_spec.divisor);
    sim.environment_plugins_.push_back(std::move(earth_loaded));
    sim.environment_plugins_.push_back(std::move(gravity_loaded));

    const Vehicle::PhysicsSpec physics_spec{std::move(mass_spec), earth, gravity};
    vehicle.init(loader, specs, sim.registry_, &physics_spec);
    vehicle.state() = read_initial_state(config);

    const Eigen::Vector3d p_n = vehicle.state().segment<state::kPositionSize>(state::kPosition);
    const Config initial_state_node =
        config.has("initial.state") ? config.at("initial.state") : config;
    earth->validate_p_n(p_n, initial_state_node);
  } else {
    vehicle.init(loader, specs, sim.registry_, nullptr);
  }

  sim.vehicles_.push_back(std::move(vehicle));

  // Before freeze(): HealthMonitor::init registers "health.nan" / "health.quaternion" /
  // "health.max_q_drift", and SignalRegistry::add throws once frozen.
  sim.health_ = std::make_unique<HealthMonitor>();
  sim.health_->init(config, sim.registry_);

  sim.registry_.freeze();

  const std::vector<std::string> unknown = config.unknown_keys();
  if (!unknown.empty()) {
    throw ConfigError(config.origin(), unknown.front(),
                      "unknown config key (docs/architecture.md §15: unknown keys are errors)");
  }
  return sim;
}

HaltReason Simulation::step() noexcept {
  const StepContext ctx = clock_.context();
  environment_scheduler_.step(ctx);
  for (Vehicle& vehicle : vehicles_) {
    vehicle.step(ctx);
    if (const HaltReason halt = vehicle.integrate(integrator_, ctx, *health_); halt) {
      // The clock does not advance: halt.frame == clock_.frame() and this frame's other vehicles
      // (ADR-0011: there can be more than one) do not step -- a halt stops the run.
      return halt;
    }
  }
  clock_.advance();
  return {};
}

HaltReason Simulation::run(std::uint64_t frames) noexcept {
  for (std::uint64_t i = 0; i < frames; ++i) {
    if (const HaltReason halt = step(); halt) {
      return halt;
    }
  }
  return {};
}

}  // namespace astra
