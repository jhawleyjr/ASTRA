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
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
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

// load_atmosphere's return type; see load_wind's own LoadedWind just below for why the plugin path
// travels with the pointer.
struct LoadedAtmosphere {
  AtmosphereModel* model = nullptr;
  std::string plugin_path;
};

// M2: rejects a model whose assumed position representation (position_representation.hpp) does not
// match what the configured Earth model provides -- e.g. gravity/wgs84_j2 paired with earth/flat
// would otherwise read NED metres as radians. kAny ("does not read p_n's geometry") is always
// compatible. config_path names the assuming model's own slot ("physical.gravity"); the message
// also names the Earth slot it disagrees with. physical.atmosphere (M2 PR 1b) does not take this
// check: AtmosphereModel::at() is handed a plain altitude, never p_n, so no conforming atmosphere
// model can assume a representation to mismatch in the first place. physical.wind (M2 PR 2) DOES
// take it, unlike atmosphere: begin_frame hands a WindModel a VehicleState (for v_b/q_nb, both
// representation-independent) alongside the Earth-derived altitude_m, and nothing stops a plugin
// from reading vs.p_n() directly instead -- if one does, this is the same guard gravity already
// relies on (Codex adversarial review, M2 PR 2 round 2).
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

// physical.atmosphere (M2 PR 1b): optional, no position-representation check (see
// check_position_representation's comment above), not part of Vehicle::PhysicsSpec -- the EOM has
// no atmosphere input; PR 3's drag WrenchSource is the first consumer. Factored out alongside
// load_wind (M2 PR 2, just below) for the same reason: clang-tidy's
// readability-function-cognitive-complexity threshold on from_config. Returns a default
// LoadedAtmosphere{nullptr, ""} when physical.atmosphere is absent.
LoadedAtmosphere load_atmosphere(const PluginLoader& loader, const Config& physical,
                                 Scheduler& scheduler, std::vector<LoadedPlugin>& plugins,
                                 SignalRegistry& registry) {
  if (!physical.has("atmosphere")) {
    return {};
  }
  const Vehicle::ModelSpec atmosphere_spec = resolve_model_spec(physical.at("atmosphere"));
  LoadedPlugin atmosphere_loaded = load_environment_model(loader, atmosphere_spec, registry);
  auto* const atmosphere = dynamic_cast<AtmosphereModel*>(atmosphere_loaded.model.get());
  if (atmosphere == nullptr) {
    throw PluginLoadError(atmosphere_spec.plugin_path, "does not implement AtmosphereModel");
  }
  scheduler.add(*atmosphere, atmosphere_spec.divisor);
  plugins.push_back(std::move(atmosphere_loaded));
  return LoadedAtmosphere{atmosphere, atmosphere_spec.plugin_path};
}

// load_wind's return type: the model plus the plugin path it was loaded from, kept alongside
// AtmosphereModel*/EarthModel*/GravityModel*'s own plain-pointer from_config locals only because
// the D4 shared-model check after sim.vehicles_.push_back needs the plugin path too.
struct LoadedWind {
  WindModel* model = nullptr;
  std::string plugin_path;
};

// physical.wind (M2 PR 2): resolves, validates the divisor, loads, checks position-representation
// compatibility against the configured Earth model (see check_position_representation's own
// comment above for why wind takes this check, unlike atmosphere), and registers one WindModel
// with the scheduler. Factored out of from_config (clang-tidy's
// readability-function-cognitive-complexity threshold) rather than inlined like the required
// earth/gravity/mass slots -- this is the one optional slot with enough of its own logic to earn a
// name. Returns a default LoadedWind{nullptr, ""} when physical.wind is absent.
LoadedWind load_wind(const PluginLoader& loader, const Config& physical, EarthModel& earth,
                     const Vehicle::ModelSpec& earth_spec, Scheduler& scheduler,
                     std::vector<LoadedPlugin>& plugins, SignalRegistry& registry) {
  if (!physical.has("wind")) {
    return {};
  }
  const Vehicle::ModelSpec wind_spec = resolve_model_spec(physical.at("wind"));
  // begin_frame/wind_n are sampled every frame by Simulation::step regardless of this model's own
  // scheduler divisor, so a divisor other than 1 would silently desynchronize step()'s own
  // evolution from what is read every frame. No defined hold contract exists for that yet, so it
  // is rejected rather than designed under review pressure.
  if (wind_spec.divisor != 1) {
    const std::string path =
        wind_spec.config.path().empty() ? "divisor" : wind_spec.config.path() + ".divisor";
    throw ConfigError(wind_spec.config.origin(), path,
                      "physical.wind must run every major frame (divisor 1)");
  }
  LoadedPlugin wind_loaded = load_environment_model(loader, wind_spec, registry);
  auto* const wind = dynamic_cast<WindModel*>(wind_loaded.model.get());
  if (wind == nullptr) {
    throw PluginLoadError(wind_spec.plugin_path, "does not implement WindModel");
  }
  check_position_representation(wind->assumed_position_representation(),
                                earth.position_representation(), wind_spec, earth_spec);
  scheduler.add(*wind, wind_spec.divisor);
  plugins.push_back(std::move(wind_loaded));
  return LoadedWind{wind, wind_spec.plugin_path};
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
  // Declared here, not inside the "physical" branch below, because the D4 shared-model check after
  // sim.vehicles_.push_back needs wind to survive past the if/else (ADR-0011: environment models
  // are shared across vehicles, and nothing here may assume there is exactly one).
  WindModel* wind = nullptr;
  std::string wind_plugin_path;
  // Physics is opt-in. The three required slots -- earth, gravity, mass -- are the Eom's own
  // inputs: all three or none (a config with no "physical" section, PR 2's dummy-plugin fixture,
  // runs exactly as before this PR, with no Eom and Vehicle::integrate a no-op). Atmosphere and
  // wind (M2 PR 2) are optional environment models, loaded only when the config names them --
  // nothing in the EOM reads them, and the M2 NESC dragless cases have no atmosphere at all. A
  // wrench source that requires one and finds none is an init error naming physical.atmosphere or
  // physical.wind, never a silent rho = 0 or a silent zero wind (Vehicle::init's requirement check,
  // wrench_source.hpp).
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
    // Required, so always set whenever "physical" is configured -- WindModel::begin_frame's own
    // altitude_m parameter (wind_model.hpp) depends on this being set before physical.wind loads.
    sim.earth_ = earth;
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

    const LoadedAtmosphere loaded_atmosphere = load_atmosphere(
        loader, physical, sim.environment_scheduler_, sim.environment_plugins_, sim.registry_);
    AtmosphereModel* const atmosphere = loaded_atmosphere.model;
    const std::string& atmosphere_plugin_path = loaded_atmosphere.plugin_path;

    const LoadedWind loaded_wind =
        load_wind(loader, physical, *earth, earth_spec, sim.environment_scheduler_,
                  sim.environment_plugins_, sim.registry_);
    wind = loaded_wind.model;
    wind_plugin_path = loaded_wind.plugin_path;
    sim.wind_ = wind;

    const Vehicle::PhysicsSpec physics_spec{std::move(mass_spec), earth, gravity};
    vehicle.init(loader, specs, sim.registry_, &physics_spec,
                 Vehicle::EnvironmentAvailability{wind != nullptr});
    vehicle.state() = read_initial_state(config);

    const Eigen::Vector3d p_n = vehicle.state().segment<state::kPositionSize>(state::kPosition);
    const Config initial_state_node =
        config.has("initial.state") ? config.at("initial.state") : config;
    earth->validate_p_n(p_n, initial_state_node);
    // The Earth and gravity models each carry their own, independently configurable notion of
    // "radius" (Codex adversarial review): earth's own floor above says nothing about whether the
    // gravity model's own geometry is still well-defined at this altitude.
    gravity->validate_p_n(p_n, initial_state_node);
    if (atmosphere != nullptr) {
      // §15: "altitude inside the atmosphere model's valid range". EarthModel::altitude is the one
      // authority on turning p_n into a height (ADR-0003: p_n's meaning is the Earth model's own).
      // Init-time only, like the two validate_p_n calls above: a trajectory that leaves the range
      // mid-run is the per-frame domain gap PR 1 disclosed (STATE.md, PLAN.md), not this check's
      // job.
      const double altitude_m = earth->altitude(VehicleState{vehicle.state()});
      const AltitudeRange range = atmosphere->valid_range();
      // Same NaN-safety reasoning as isa1976.cpp's own valid_range check -- the "simplified"
      // a<min||a>max form is false for a NaN altitude, which would silently accept it instead of
      // rejecting it.
      // NOLINTNEXTLINE(readability-simplify-boolean-expr)
      if (!(altitude_m >= range.min_m && altitude_m <= range.max_m)) {
        const std::string path = initial_state_node.path().empty()
                                     ? "initial.state.p_n"
                                     : initial_state_node.path() + ".p_n";
        throw ConfigError(
            initial_state_node.origin(), path,
            "initial altitude is outside " + atmosphere_plugin_path + "'s valid range");
      }
    }
  } else {
    vehicle.init(loader, specs, sim.registry_, nullptr, Vehicle::EnvironmentAvailability{});
  }

  sim.vehicles_.push_back(std::move(vehicle));

  // ADR-0011: "nothing may assume there will only ever be one [vehicle]," and environment models
  // are shared across whatever vehicles exist. Checked here so it composes correctly once
  // from_config loops over more than one vehicle spec -- currently unreachable (exactly one Vehicle
  // is ever constructed above), so this is verified only at the predicate level
  // (WindModel::shareable_across_vehicles, tests/unit/wind/wind_test.cpp), not through a config
  // that can actually trigger it.
  if (wind != nullptr && sim.vehicles_.size() > 1 && !wind->shareable_across_vehicles()) {
    throw ConfigError(config.origin(), "physical.wind",
                      wind_plugin_path + " is not safe to share across more than one vehicle");
  }

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
  environment_scheduler_.step(ctx);  // scheduler-driven evolution only (e.g. a stochastic RNG draw)
  for (Vehicle& vehicle : vehicles_) {
    // This frame's environment sample (docs/architecture.md §5, environment_sample.hpp), latched
    // here at frame-start state, before the wrench stage -- the same discipline Eom::begin_frame
    // and Vehicle::integrate's own VehicleState latch already use. wind_ is the only per-vehicle
    // evolution/read pair today; PR 3 adds atmosphere the same way, from an AtmosphereModel* member
    // sampled here too.
    EnvironmentSample sample;  // wind zero / atmosphere all-NaN by default
    if (wind_ != nullptr) {
      const VehicleState vs{vehicle.state()};
      // earth_ is always set whenever wind_ is (both are set only under a "physical" section, and
      // earth loads first, unconditionally -- see the sim.earth_ assignment above in from_config).
      const double altitude_m = earth_->altitude(vs);
      wind_->begin_frame(ctx, vs, altitude_m);  // per-vehicle evolution: uses this frame's V, h
      sample.wind_n = wind_->wind_n(ctx, vs);   // pure read of what begin_frame just latched
    }
    vehicle.set_environment(sample);
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
