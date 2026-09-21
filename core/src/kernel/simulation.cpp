#include "astra/kernel/simulation.hpp"

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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

// ADR-0011: "nothing may assume there will only ever be one [vehicle]," and environment models are
// shared across whatever vehicles exist -- a WindModel that has not declared itself safe to share
// (shareable_across_vehicles(), wind_model.hpp) must not silently run under more than one. Shared
// by from_config (currently always exactly one vehicle, so unreachable there in production) and
// Simulation::for_testing below, the only way this guard can actually be exercised today (Cowork
// review, post-merge-ready: it had no way to be exercised at all before for_testing existed).
// config_origin names whatever document or fixture is asking; from_config passes its own config's
// origin(), for_testing a literal fixture description.
void check_wind_shareable(WindModel* wind, std::size_t vehicle_count,
                          const std::string& wind_plugin_path, std::string_view config_origin) {
  if (wind != nullptr && vehicle_count > 1 && !wind->shareable_across_vehicles()) {
    throw ConfigError(config_origin, "physical.wind",
                      wind_plugin_path + " is not safe to share across more than one vehicle");
  }
}

// physical.atmosphere and physical.wind are both sampled into EnvironmentSample every major frame
// by Simulation::step (see its own comment), regardless of either model's own scheduler divisor --
// so a divisor other than 1 on either slot would silently desynchronize step()'s per-frame sampling
// from what the model's own step() last computed. No defined hold contract exists for that yet, so
// both slots reject it rather than each inventing (or, worse, disagreeing on) an answer under
// review pressure. Stated once here, shared by load_atmosphere and load_wind below, precisely so
// the two slots cannot drift apart the way they once did (Cowork review, post-merge-ready:
// load_atmosphere had no such check at all, though load_wind already did -- an omission that would
// have applied to atmosphere too the moment PR 3 starts sampling it into EnvironmentSample, per
// exactly this reasoning).
void check_sampled_every_frame(const Vehicle::ModelSpec& spec) {
  if (spec.divisor != 1) {
    const std::string path =
        spec.config.path().empty() ? "divisor" : spec.config.path() + ".divisor";
    throw ConfigError(spec.config.origin(), path,
                      spec.config.path() + " must run every major frame (divisor 1)");
  }
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
  check_sampled_every_frame(atmosphere_spec);
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

// physical.wind (M2 PR 2): resolves, validates the divisor (check_sampled_every_frame above --
// shared with load_atmosphere, not a second copy of the same reasoning), loads, checks position-
// representation compatibility against the configured Earth model (see
// check_position_representation's own comment above for why wind takes this check, unlike
// atmosphere), and registers one WindModel with the scheduler. Factored out of from_config
// (clang-tidy's readability-function-cognitive-complexity threshold) rather than inlined like the
// required earth/gravity/mass slots -- this is the one optional slot with enough of its own logic
// to earn a name. Returns a default LoadedWind{nullptr, ""} when physical.wind is absent.
LoadedWind load_wind(const PluginLoader& loader, const Config& physical, EarthModel& earth,
                     const Vehicle::ModelSpec& earth_spec, Scheduler& scheduler,
                     std::vector<LoadedPlugin>& plugins, SignalRegistry& registry) {
  if (!physical.has("wind")) {
    return {};
  }
  const Vehicle::ModelSpec wind_spec = resolve_model_spec(physical.at("wind"));
  check_sampled_every_frame(wind_spec);
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

// ADR-0017: applies vehicle.initial_controls's config literals to vehicle's own write-seam inputs,
// validates every declared input has a real, finite value, then freezes the registry -- the one
// unconditional, init-time commit that seeds canonical storage before frame 0 ever runs. One call
// per declared input -- vehicle.inputs().names() (registered by bind_inputs(), already known before
// any config is consulted) -- looked up by its own relative dotted name (e.g. "controls.elevator",
// no "vehicle.initial_controls." prefix and no "v0." prefix, matching physical.mass's own existing
// unprefixed-key convention) under the "vehicle.initial_controls" subtree, via Config's existing
// multi-level dot-navigation: no new parsing code.
//
// **Correction, Codex adversarial review**: an earlier draft enumerated names off config.
// unknown_keys() instead -- wrong, because unknown_keys() diffs against the Config document's own
// shared, mutable read-tracking (Config's document_ is a shared_ptr, so even a copy shares it): a
// SECOND Simulation built from the same Config object (e.g. a future Monte Carlo driver reusing one
// loaded document, PLAN.md M5) would find every input's literal already marked read by the first
// construction and silently skip applying it the second time, throwing
// first_unwritten_or_non_finite instead. Fixed by enumerating from InputRegistry's own names()
// (this call's actual "declared input" list) and reading through ordinary has()/number() calls,
// neither of which depends on prior read-state -- idempotent across as many Simulations as one
// Config is reused for.
//
// Throws ConfigError naming the input's own config-literal path (not merely its write-seam name)
// when first_unwritten_or_non_finite() finds a gap -- Simulation::for_testing calls none of this
// (its own documented list of from_config-only validations).
void apply_initial_controls(const Config& config, Vehicle& vehicle) {
  if (config.has("vehicle.initial_controls")) {
    const Config initial_controls = config.at("vehicle.initial_controls");
    for (const std::string_view name : vehicle.inputs().names()) {
      if (initial_controls.has(name)) {
        vehicle.inputs().write(name, initial_controls.number(name));
      }
    }
  }
  if (const std::string_view bad_input = vehicle.inputs().first_unwritten_or_non_finite();
      !bad_input.empty()) {
    throw ConfigError(config.origin(), "vehicle.initial_controls." + std::string{bad_input},
                      "write-seam input not written, or written a non-finite value "
                      "(docs/decisions/0017-control-input-write-seam.md)");
  }
  vehicle.inputs().freeze();
}

// ADR-0017: latch()es vehicle's own write-seam inputs and reports the first non-finite one, if any,
// through health -- factored out of Simulation::step (clang-tidy's readability-function-cognitive-
// complexity threshold), the same reason Vehicle::check_domain is factored out of Vehicle::
// integrate. Per-frame path: no allocation, no exceptions.
HaltReason latch_vehicle_inputs(Vehicle& vehicle, HealthMonitor& health,
                                const StepContext& ctx) noexcept {
  const std::string_view bad_input = vehicle.inputs().latch();
  if (bad_input.empty()) {
    return {};
  }
  return health.check_input(bad_input, ctx, vehicle.index());
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
    // ADR-0012: retained past this function's own LoadedAtmosphere local (which used to go out of
    // scope right after the init-time valid_range() check below) so Simulation::step's post-step
    // domain check has both the model and what a halt on it names.
    sim.atmosphere_ = atmosphere;
    sim.atmosphere_plugin_path_ = atmosphere_plugin_path;

    const LoadedWind loaded_wind =
        load_wind(loader, physical, *earth, earth_spec, sim.environment_scheduler_,
                  sim.environment_plugins_, sim.registry_);
    wind = loaded_wind.model;
    wind_plugin_path = loaded_wind.plugin_path;
    sim.wind_ = wind;

    const Vehicle::PhysicsSpec physics_spec{std::move(mass_spec),
                                            earth,
                                            gravity,
                                            earth_spec.plugin_path,
                                            gravity_spec.plugin_path,
                                            atmosphere,
                                            atmosphere_plugin_path};
    vehicle.init(loader, specs, sim.registry_, &physics_spec,
                 Vehicle::EnvironmentAvailability{wind != nullptr, atmosphere != nullptr},
                 wind_plugin_path);
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
      // Init-time only, like the two validate_p_n calls above: this checks p_n(0) alone. A
      // trajectory that leaves the range mid-run is checked post-step by Simulation::step below
      // (ADR-0012), against sim.atmosphere_ set just above -- the gap PR 1b disclosed, closed by
      // PR 2c, not this check's job.
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

  apply_initial_controls(config, vehicle);

  sim.vehicles_.push_back(std::move(vehicle));

  // Checked here so it composes correctly once from_config loops over more than one vehicle spec
  // -- currently unreachable in production (exactly one Vehicle is ever constructed above); see
  // check_wind_shareable's own comment for how it is actually exercised (Simulation::for_testing).
  check_wind_shareable(wind, sim.vehicles_.size(), wind_plugin_path, config.origin());

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

Simulation Simulation::for_testing(const PluginLoader& loader,
                                   std::span<const StateVector> initial_states,
                                   const std::vector<Vehicle::ModelSpec>& models, WindModel* wind,
                                   EarthModel* earth, GravityModel* gravity,
                                   const Vehicle::ModelSpec* mass_spec, std::uint64_t frames) {
  // Adversarial-review finding (Cowork re-review round 2, "high"): has_physics used to be computed
  // from gravity/mass_spec alone, so a caller supplying both without earth reached Vehicle::init's
  // eom_.emplace(*physics->earth, ...) (vehicle.cpp) with a null earth -- undefined behavior, not a
  // thrown error, and caught only by review, since nothing in this tree had called for_testing that
  // way. earth, gravity, and mass_spec are Vehicle::PhysicsSpec's three required fields together
  // (vehicle.hpp); rejecting a partial combination here is enforcing that existing invariant, not a
  // new domain-validity design decision of the kind this round's Cowork review already declined to
  // add for_testing (check_position_representation, validate_p_n, the atmosphere valid_range check
  // remain unenforced, per for_testing's own doc comment above).
  if ((gravity != nullptr) != (mass_spec != nullptr)) {
    throw Error(
        "Simulation::for_testing: gravity and mass_spec must both be supplied or both be null -- "
        "one without the other silently built a no-physics Simulation, ignoring the one passed");
  }
  if (gravity != nullptr && mass_spec != nullptr && earth == nullptr) {
    throw Error(
        "Simulation::for_testing: gravity and mass_spec require a non-null earth too, mirroring "
        "Vehicle::PhysicsSpec's third required field");
  }
  // A second re-run of adversarial review (same round) reclassified this one: not the same
  // domain-validity shape as check_position_representation/validate_p_n/the atmosphere valid_range
  // check above (a plausible-but-wrong answer that needs a real trajectory to observe), but the
  // same crash shape as the gravity/mass_spec/earth check just above -- Simulation::step's wind
  // branch dereferences earth_ unconditionally whenever wind_ is non-null (earth_->altitude(vs)
  // below), and that branch runs inside step()'s own noexcept, so a null earth_ here is UB
  // reachable with a single vehicle, not just through the D4 shareability guard's multi-vehicle
  // path. Enforced, not merely disclosed, for that reason -- the doc comment above previously
  // argued otherwise from a stale analogy and has been corrected alongside this.
  if (wind != nullptr && earth == nullptr) {
    throw Error(
        "Simulation::for_testing: a non-null wind requires a non-null earth (begin_frame's "
        "Earth-derived altitude_m, computed in Simulation::step)");
  }
  Simulation sim;
  sim.frames_ = frames;
  sim.wind_ = wind;
  sim.earth_ = earth;
  // Same order as from_config: earth, then gravity, then wind. Both earth/flat and
  // gravity/constant's own step() are empty today, so this had no observable effect on any test
  // before this fix (Codex adversarial review, M2 PR 2 Cowork-fix round: scheduling only wind left
  // for_testing silently non-equivalent to from_config's wiring for a future stateful Earth or
  // gravity model, which would then never actually be stepped here).
  if (earth != nullptr) {
    sim.environment_scheduler_.add(*earth);
  }
  if (gravity != nullptr) {
    sim.environment_scheduler_.add(*gravity);
  }
  if (wind != nullptr) {
    sim.environment_scheduler_.add(*wind);
  }
  const bool has_physics = gravity != nullptr && mass_spec != nullptr;
  // ADR-0020: the same placeholder idiom earth_plugin_path/gravity_plugin_path above already use --
  // for_testing takes a live WindModel*, not a Config, so there is no real plugin path for
  // report_non_finite_environment to name on a non-finite wind_n halt either. atmosphere has no
  // equivalent parameter here at all (this function's own doc comment above), so PhysicsSpec::
  // atmosphere_plugin_path is left at its own default-constructed empty string below, never
  // actually named (atmosphere is always nullptr in a for_testing-built PhysicsSpec).
  const std::string wind_plugin_path = wind != nullptr ? "for_testing's wind" : std::string{};
  std::uint32_t index = 0;
  for (const StateVector& initial_state : initial_states) {
    Vehicle vehicle(index, "v" + std::to_string(index));
    if (has_physics) {
      // Constructed fresh per vehicle (mass is loaded fresh from mass_spec inside Vehicle::init,
      // exactly as from_config's own single vehicle gets its own mass model) rather than hoisted
      // out of the loop, since Vehicle::PhysicsSpec has no default constructor to hoist it past.
      // earth_plugin_path/gravity_plugin_path (ADR-0012) get placeholder names, the same
      // "for_testing's wind" idiom check_wind_shareable already uses below -- for_testing takes a
      // live EarthModel*/GravityModel*, not a Config, so there is no real plugin path to give a
      // domain halt here.
      const Vehicle::PhysicsSpec physics_spec{
          *mass_spec, earth, gravity, "for_testing's earth", "for_testing's gravity", nullptr, ""};
      vehicle.init(loader, models, sim.registry_, &physics_spec,
                   Vehicle::EnvironmentAvailability{wind != nullptr}, wind_plugin_path);
    } else {
      vehicle.init(loader, models, sim.registry_, nullptr,
                   Vehicle::EnvironmentAvailability{wind != nullptr}, wind_plugin_path);
    }
    vehicle.state() = initial_state;
    sim.vehicles_.push_back(std::move(vehicle));
    ++index;
  }

  // Same guard, same call shape, as from_config's own (check_wind_shareable's own comment) --
  // this is the only way it is exercised today.
  check_wind_shareable(wind, sim.vehicles_.size(), "for_testing's wind", "Simulation::for_testing");

  sim.health_ = std::make_unique<HealthMonitor>();
  sim.health_->init(Config::from_string("{}\n", "Simulation::for_testing"), sim.registry_);
  sim.registry_.freeze();
  return sim;
}

HaltReason Simulation::step() noexcept {
  const StepContext ctx = clock_.context();
  environment_scheduler_.step(ctx);  // scheduler-driven evolution only (e.g. a stochastic RNG draw)
  for (Vehicle& vehicle : vehicles_) {
    // This frame's environment sample (docs/architecture.md §5, environment_sample.hpp), latched
    // here at frame-start state, before the wrench stage -- the same discipline Eom::begin_frame
    // and Vehicle::integrate's own VehicleState latch already use. wind and atmosphere are sampled
    // together, from the same frame-start VehicleState and the same single earth_->altitude(vs)
    // call, but they remain independent slots: either may be configured without the other (M2 PR
    // 3). This altitude_m is deliberately a different call than the post-step altitude ADR-0012's
    // domain check below takes -- that one runs after integration, on a different state, and has
    // nothing to share with this one.
    EnvironmentSample sample;  // wind zero / atmosphere all-NaN by default
    if (wind_ != nullptr || atmosphere_ != nullptr) {
      const VehicleState vs{vehicle.state()};
      // earth_ is always set whenever wind_ or atmosphere_ is (all three load only under a
      // "physical" section, and earth loads first, unconditionally -- see the sim.earth_
      // assignment above in from_config).
      const double altitude_m = earth_->altitude(vs);
      if (wind_ != nullptr) {
        wind_->begin_frame(ctx, vs, altitude_m);  // per-vehicle evolution: uses this frame's V, h
        sample.wind_n = wind_->wind_n(ctx, vs);   // pure read of what begin_frame just latched
      }
      if (atmosphere_ != nullptr) {
        // Never clamped: at() itself returns all-NaN outside valid_range() (atmosphere_model.hpp),
        // and ADR-0012's post-step check below is the halt path for that, not this assignment.
        sample.atmosphere = atmosphere_->at(altitude_m);
      }
    }
    vehicle.set_environment(sample);
    vehicle.step(ctx);
    // ADR-0017: latch() runs here -- after vehicle.step(ctx), before vehicle.integrate(...) -- so a
    // write issued from within THIS frame's own step() (a future M4 ActuatorModel's use case) is
    // committed by this same frame's latch(), matching §5's own canonical order ("...receive
    // commands) -> integrator step..."). Runs once per vehicle per frame unconditionally, even when
    // nothing wrote anything new this frame (the config-literal's own freeze()-time seed, or the
    // last successful latch(), is what wrench_b() reads either way).
    if (const HaltReason halt = latch_vehicle_inputs(vehicle, *health_, ctx); halt) {
      return halt;
    }
    if (const HaltReason halt = vehicle.integrate(integrator_, ctx, *health_); halt) {
      // The clock does not advance: halt.frame == clock_.frame() and this frame's other vehicles
      // (ADR-0011: there can be more than one) do not step -- a halt stops the run.
      return halt;
    }
    // ADR-0012: the post-step altitude check, against the POST-step state -- a different state
    // than the frame-start altitude_m computed above for wind (that read happens before
    // vehicle.integrate(); this one after), so there is no call to share between the two. No
    // lookahead here (unlike Vehicle::check_domain's Earth/gravity check): an out-of-range
    // altitude is not a singularity, at() simply returns an all-NaN AtmoState, and Vehicle::
    // integrate's own domain check already covers the p_n singularities on this same post-step
    // state. Skipped entirely when atmosphere_ is null (no physical.atmosphere configured) or
    // health_->domain_enabled() is false -- no altitude() call, matching "domain: off restores
    // current behavior".
    if (atmosphere_ != nullptr && health_->domain_enabled()) {
      const double altitude_m = earth_->altitude(VehicleState{vehicle.state()});
      const AltitudeRange range = atmosphere_->valid_range();
      // Same NaN-safety reasoning as isa1976.cpp's own valid_range check and from_config's
      // init-time twin above: the "simplified" a<min||a>max form is false for a NaN altitude,
      // which would silently accept it instead of rejecting it.
      // NOLINTNEXTLINE(readability-simplify-boolean-expr)
      if (!(altitude_m >= range.min_m && altitude_m <= range.max_m)) {
        if (const HaltReason halt =
                health_->report_out_of_domain(atmosphere_plugin_path_, ctx, vehicle.index());
            halt) {
          return halt;
        }
      }
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
