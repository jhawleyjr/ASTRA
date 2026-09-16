// plugins/physical/wind/uniform and M2 PR 2's WindModel call contract (docs/architecture.md §7,
// wind_model.hpp): begin_frame/wind_n are sampled exactly once per vehicle per frame, at
// frame-start state, in the environment stage before the wrench stage, and the sampled value
// reaches a WrenchSource through EnvironmentSample -- the wind injection point CLAUDE.md's M2
// trigger list named. tests/plugins/counting_wind and tests/plugins/wind_consumer make the call
// contract assertable; tests/plugins/atmosphere_consumer makes D3's unconditional atmosphere
// rejection assertable (PLAN.md M2 PR 2).
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::EarthModel;
using astra::Simulation;
using astra::StateVector;
using astra::StepContext;
using astra::Vehicle;
using astra::VehicleState;
using astra::WindModel;

bool mentions(const std::exception& error, std::string_view text) {
  return std::string_view{error.what()}.find(text) != std::string_view::npos;
}

Simulation load_sim(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

// uniform's init() overwrites every field it reads on each call, so re-init on a static instance
// with a fresh local registry is harmless -- the isa1976_test.cpp / wgs84_j2_test.cpp pattern. Not
// used by tests that need the registry to outlive this call (RegistersTheThreeSignals... below
// builds its own loader/registry locally instead).
WindModel& load(std::string_view yaml_fields) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded = loader.load("plugins/physical/wind/uniform");
  auto* const wind = dynamic_cast<WindModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  wind->init(Config::from_string("{ " + std::string{yaml_fields} + " }\n", "uniform-fixture"),
             registry);
  return *wind;
}

StateVector level_at_rest() {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity: level, heading north
  return y;
}

// ---- plugins/physical/wind/uniform ----

TEST(WindUniform, ReturnsTheConfiguredAirVelocityInNed) {
  WindModel& wind = load("north: 5.0, east: -2.0, down: 0.5");
  const StateVector y = level_at_rest();  // named: VehicleState stores a reference, not a copy
  const VehicleState vs{y};
  const StepContext ctx{0.0, 0.001, 0};
  const Eigen::Vector3d result = wind.wind_n(ctx, vs);
  EXPECT_DOUBLE_EQ(result(0), 5.0);
  EXPECT_DOUBLE_EQ(result(1), -2.0);
  EXPECT_DOUBLE_EQ(result(2), 0.5);
}

TEST(WindUniform, DefaultsToStillAirOnAnEmptyConfig) {
  WindModel& wind = load("");
  const StateVector y = level_at_rest();  // named: VehicleState stores a reference, not a copy
  const VehicleState vs{y};
  const StepContext ctx{0.0, 0.001, 0};
  EXPECT_TRUE(wind.wind_n(ctx, vs).isZero());
}

// wind_n itself must not depend on attitude -- only a consumer's rotation into body axes does (D5).
TEST(WindUniform, IsAttitudeIndependent) {
  WindModel& wind = load("north: 3.0, east: 1.0, down: 0.0");
  const StepContext ctx{0.0, 0.001, 0};
  StateVector level = StateVector::Zero();
  level(astra::state::kQuaternion) = 1.0;
  const Eigen::Vector3d level_result = wind.wind_n(ctx, VehicleState{level});

  StateVector yawed = StateVector::Zero();  // 90 deg yaw: q_nb = (cos45, 0, 0, sin45)
  yawed(astra::state::kQuaternion) = std::sqrt(0.5);
  yawed(astra::state::kQuaternion + 3) = std::sqrt(0.5);  // NOLINT(readability-magic-numbers)
  const Eigen::Vector3d yawed_result = wind.wind_n(ctx, VehicleState{yawed});

  EXPECT_TRUE(level_result.isApprox(yawed_result));
}

TEST(WindUniform, RejectsANonFiniteComponentNamingTheKey) {
  try {
    static_cast<void>(load("north: .nan"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "north");
  }
}

TEST(WindUniform, IsShareableAcrossVehicles) { EXPECT_TRUE(load("").shareable_across_vehicles()); }

// D5: wind_n is "wind-to", not the meteorological "wind-from". A 10 m/s wind FROM the north --
// blowing air toward the south -- is configured as north: -10.0; a level vehicle facing north at
// rest sees that as a headwind (positive body-x airspeed), not a tailwind.
TEST(WindUniform, SignConventionIsAirVelocityNotWindFrom) {
  WindModel& wind = load("north: -10.0");
  const StateVector y = level_at_rest();  // v_b = 0, q_nb = identity (facing north)
  const VehicleState vs{y};               // named: VehicleState stores a reference, not a copy
  const StepContext ctx{0.0, 0.001, 0};
  const Eigen::Vector3d wind_n = wind.wind_n(ctx, vs);
  const Eigen::Vector3d v_rel_b = vs.v_b() - astra::frames::rotate_n_to_b(vs.q_nb(), wind_n);
  EXPECT_DOUBLE_EQ(v_rel_b(0), 10.0) << "a northerly is a headwind to a vehicle facing north";
}

// The three signals carry the values init() computed, checked with a registry this test keeps
// alive itself -- unlike load()'s helper above, whose local registry is gone once it returns.
TEST(WindUniform, RegistersTheThreeSignalsWithUnitMetresPerSecond) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("plugins/physical/wind/uniform");
  auto* const wind = dynamic_cast<WindModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  wind->init(Config::from_string("{ north: 5.0, east: -2.0, down: 0.5 }\n", "signals-fixture"),
             registry);
  registry.freeze();

  const std::array<std::string_view, 3> names{"wind.north", "wind.east", "wind.down"};
  const std::array<double, 3> expected{5.0, -2.0, 0.5};
  for (std::size_t i = 0; i < names.size(); ++i) {
    const double* value = registry.find(names[i]);
    ASSERT_NE(value, nullptr) << names[i];
    EXPECT_DOUBLE_EQ(*value, expected[i]);
  }
  bool found_north_unit = false;
  for (const astra::Signal& signal : registry.signals()) {
    if (signal.name == "wind.north") {
      EXPECT_EQ(signal.unit, "m/s");
      found_north_unit = true;
    }
  }
  EXPECT_TRUE(found_north_unit);
}

// §15's determinism guard names "seeds present for every stochastic plugin" -- uniform is not
// stochastic and takes none; writing one anyway is an unread, therefore unknown, config key. Only
// Simulation::from_config's whole-document unknown_keys() scan can catch this (a direct plugin
// init() call, as load() above makes, never sees the rest of the document).
TEST(WindUniform, TakesNoSeed) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: { plugin: plugins/physical/wind/uniform, seed: 42 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "wind-seed-fixture");
  try {
    static_cast<void>(load_sim(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "seed")) << error.what();
  }
}

// ---- The call contract (docs/architecture.md §5, wind_model.hpp) ----

Config call_contract_config(std::uint64_t frames) {
  return Config::from_string(
      "sim: { dt: 0.001, frames: " + std::to_string(frames) +
          " }\n"
          "vehicle: { models: [ { plugin: tests/plugins/wind_consumer } ] }\n"
          "physical:\n"
          "  earth: plugins/physical/earth/flat\n"
          "  gravity: plugins/physical/gravity/constant\n"
          "  wind: tests/plugins/counting_wind\n"
          "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
          "1.0 }\n",
      "call-contract-fixture");
}

// Proves "once per frame" through the real from_config pipeline (config parsing, plugin loading,
// the requirement check, the real wiring) -- with exactly the one vehicle from_config ever builds
// (ADR-0011), so it cannot by itself distinguish "once per frame" from "once per vehicle per
// frame". WindMultiVehicle's tests below prove the "per vehicle" half, through
// Simulation::for_testing instead, since from_config has no way to build more than one vehicle
// (Cowork review, post-merge-ready) -- the two are complementary, not redundant: this test is the
// only one exercising the real config-driven path end to end.
TEST(WindCallContract, BeginFrameThenWindNAreEachCalledExactlyOncePerFrame) {
  constexpr std::uint64_t kFrames = 20;
  Simulation sim = load_sim(call_contract_config(kFrames));
  EXPECT_FALSE(sim.run());

  const double* begin_frame_calls = sim.registry().find("counting_wind.begin_frame_calls");
  const double* wind_n_calls = sim.registry().find("counting_wind.wind_n_calls");
  // wind_consumer is a vehicle.models entry, so its init() ran under Vehicle::init's PrefixScope
  // (ADR-0011: "v0.aero.CL") -- unlike counting_wind, which is an environment model and publishes
  // unprefixed (load_environment_model opens no PrefixScope).
  const double* consumer_calls = sim.registry().find("v0.wind_consumer.calls");
  ASSERT_NE(begin_frame_calls, nullptr);
  ASSERT_NE(wind_n_calls, nullptr);
  ASSERT_NE(consumer_calls, nullptr);
  // This fails under any design that evaluates wind inside the wrench stage or per consumer
  // (D1): a per-RK4-stage or per-consumer design would inflate wind_n_calls/consumer_calls past
  // kFrames, and a design that lets wind_n advance state would desynchronize the two counts.
  EXPECT_DOUBLE_EQ(*begin_frame_calls, static_cast<double>(kFrames));
  EXPECT_DOUBLE_EQ(*wind_n_calls, static_cast<double>(kFrames));
  EXPECT_DOUBLE_EQ(*consumer_calls, static_cast<double>(kFrames));
}

TEST(WindCallContract, WindIsSampledAtFrameStartStateBeforeTheWrenchStage) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 2 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/wind_consumer } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: tests/plugins/counting_wind\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { v_b: [100.0, 0.0, 0.0] } }\n",
      "frame-start-fixture");
  Simulation sim = load_sim(config);

  // Frame 0: begin_frame must see the configured initial state -- p_n's north component is still
  // exactly zero, before this frame's integration has run at all.
  EXPECT_FALSE(sim.step());
  const double* last_p_n_north = sim.registry().find("counting_wind.last_p_n_north");
  ASSERT_NE(last_p_n_north, nullptr);
  EXPECT_DOUBLE_EQ(*last_p_n_north, 0.0) << "frame 0's begin_frame must see the pre-step state";
  const double post_frame0_p_n_north = sim.vehicles().front().state()(astra::state::kPosition);
  ASSERT_GT(post_frame0_p_n_north, 0.0) << "one frame of v_b = 100 m/s must have moved p_n north";

  // Frame 1: begin_frame must see frame 0's post-step state (frame 1's pre-step state), not frame
  // 1's own post-step state.
  EXPECT_FALSE(sim.step());
  EXPECT_DOUBLE_EQ(*last_p_n_north, post_frame0_p_n_north)
      << "frame 1's begin_frame must see frame 0's post-step state, not its own";
}

TEST(WindCallContract, TheSampledWindReachesTheWrenchSource) {
  constexpr std::uint64_t kFrames = 5;
  Simulation sim = load_sim(call_contract_config(kFrames));
  EXPECT_FALSE(sim.run());

  // counting_wind's wind_n encodes the latching frame in component 0 (its own header comment); the
  // last call is for frame kFrames - 1 (0-indexed), so wind_consumer must have recorded that.
  // Prefixed "v0." like the consumer_calls lookup above -- wind_consumer is a vehicle.models entry.
  const double* recorded_north = sim.registry().find("v0.wind_consumer.wind_n_north");
  ASSERT_NE(recorded_north, nullptr);
  EXPECT_DOUBLE_EQ(*recorded_north, static_cast<double>(kFrames - 1));
}

// wind_model.hpp: begin_frame receives an Earth-derived altitude, never raw p_n -- Codex's
// adversarial review caught that vs.p_n()(2) means opposite things under earth/flat (NED down,
// metres) and earth/round (geodetic altitude, metres, opposite sign). This proves the fix: the SAME
// physical altitude, expressed correctly under each Earth model's own p_n convention, produces the
// SAME value in counting_wind.last_altitude_m.
TEST(WindCallContract, UsesEarthDerivedAltitudeNotRawPN) {
  constexpr double kAltitudeM = 5000.0;
  auto config_for = [](std::string_view earth_plugin, double p_n_z) {
    return Config::from_string(
        "sim: { dt: 0.001, frames: 1 }\n"
        "vehicle: { models: [] }\n"
        "physical:\n"
        "  earth: " +
            std::string{earth_plugin} +
            "\n"
            "  gravity: plugins/physical/gravity/constant\n"
            "  wind: tests/plugins/counting_wind\n"
            "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
            "izz: "
            "1.0 }\n"
            "initial: { state: { p_n: [0.0, 0.0, " +
            std::to_string(p_n_z) + "] } }\n",
        "altitude-fixture");
  };
  // earth/flat: p_n(2) is NED down, so +5000 m altitude is p_n_z = -5000.
  Simulation sim_flat = load_sim(config_for("plugins/physical/earth/flat", -kAltitudeM));
  // earth/round: p_n(2) is geodetic altitude directly, so +5000 m altitude is p_n_z = +5000.
  Simulation sim_round = load_sim(config_for("plugins/physical/earth/round", kAltitudeM));

  EXPECT_FALSE(sim_flat.step());
  EXPECT_FALSE(sim_round.step());

  const double* altitude_flat = sim_flat.registry().find("counting_wind.last_altitude_m");
  const double* altitude_round = sim_round.registry().find("counting_wind.last_altitude_m");
  ASSERT_NE(altitude_flat, nullptr);
  ASSERT_NE(altitude_round, nullptr);
  // Not bitwise: one frame of gravity/constant moves the state by a few micrometres before
  // begin_frame reads it, and flat/round Earth's p_n_dot formulas differ at that scale. The point
  // of this test is representation correctness (no sign flip, no order-of-magnitude error), not
  // integrator precision.
  EXPECT_NEAR(*altitude_flat, *altitude_round, 1e-6);
  EXPECT_NEAR(*altitude_flat, kAltitudeM, 1e-2);
}

// simulation.cpp's Vehicle::init requirement check (D3): the contract "a wrench source that
// requires wind and finds none is an init error naming physical.wind" has existed as a comment
// since M2 PR 1b; this is the first test to assert it.
TEST(WindCallContract, AWrenchSourceRequiringWindWithNoWindSlotIsAnInitError) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/wind_consumer } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "wind-consumer-no-wind-fixture");
  try {
    static_cast<void>(load_sim(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.wind");
  }
}

TEST(WindCallContract,
     AWrenchSourceRequiringAtmosphereIsAlwaysRejectedWithoutAtmosphereConfigured) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/atmosphere_consumer } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "atmosphere-consumer-fixture");
  try {
    static_cast<void>(load_sim(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.atmosphere");
  }
}

// The rejection is unconditional (D3), not "physical.atmosphere is absent": even a config naming
// physical.atmosphere cannot satisfy atmosphere_consumer's requirement in M2 PR 2 -- no PR before
// PR 3 connects the two (environment_sample.hpp).
TEST(WindCallContract,
     AWrenchSourceRequiringAtmosphereIsAlwaysRejectedEvenWithAtmosphereConfigured) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/atmosphere_consumer } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "atmosphere-consumer-with-atmosphere-fixture");
  try {
    static_cast<void>(load_sim(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.atmosphere");
  }
}

// A minimal WindModel that overrides only what Model requires, leaving begin_frame and
// shareable_across_vehicles at WindModel's own defaults -- so this test probes the interface's
// default, not any one plugin's override of it (wind/uniform and tests/plugins/counting_wind both
// override shareable_across_vehicles to true, since both are stateless or single-vehicle-only by
// design; nothing in this tree leaves it at the default except this fixture).
class MinimalWind final : public astra::WindModel {
 public:
  void init(const Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "minimal_wind"; }
  [[nodiscard]] Eigen::Vector3d wind_n(const StepContext& /*ctx*/,
                                       const VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
};

// D4: refusing to run beats a plausible-looking wrong answer -- a WindModel whose author has not
// declared it safe to share must default to unsafe, not safe.
TEST(WindCallContract, NonShareableWindModelPredicate) {
  const MinimalWind wind;
  EXPECT_FALSE(wind.shareable_across_vehicles());
}

// ---- The multi-vehicle call contract (D1's "once per vehicle", D4's shareability guard;
// ADR-0011: "nothing may assume there will only ever be one" vehicle) ----
//
// from_config never builds more than one vehicle in v1 (ADR-0011), so the "per vehicle" half of
// the call contract above, and the D4 guard, had no way to be exercised at all before
// Simulation::for_testing existed (Cowork review, post-merge-ready).

StateVector state_with_p_n_north(double p_n_north) {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;
  y(astra::state::kPosition) = p_n_north;  // NOLINT(readability-magic-numbers): p_n's 1st component
  return y;
}

TEST(WindMultiVehicle, BeginFrameThenWindNAreCalledOncePerVehiclePerFrameWithDistinctState) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin wind_loaded = loader.load("tests/plugins/counting_wind");
  auto* const wind = dynamic_cast<WindModel*>(wind_loaded.model.get());
  astra::LoadedPlugin earth_loaded = loader.load("plugins/physical/earth/flat");
  auto* const earth = dynamic_cast<EarthModel*>(earth_loaded.model.get());
  astra::SignalRegistry wind_registry;
  wind->init(Config::from_string("{}\n", "counting-wind-fixture"), wind_registry);
  astra::SignalRegistry earth_registry;
  earth->init(Config::from_string("{}\n", "earth-flat-fixture"), earth_registry);

  const std::array<StateVector, 2> initial_states{state_with_p_n_north(0.0),
                                                  state_with_p_n_north(1000.0)};
  constexpr std::uint64_t kFrames = 10;
  Simulation sim = Simulation::for_testing(loader, initial_states, {}, wind, earth);
  EXPECT_FALSE(sim.run(kFrames));

  const double* begin_frame_calls = wind_registry.find("counting_wind.begin_frame_calls");
  const double* wind_n_calls = wind_registry.find("counting_wind.wind_n_calls");
  const double* frame_min = wind_registry.find("counting_wind.frame_min_p_n_north");
  const double* frame_max = wind_registry.find("counting_wind.frame_max_p_n_north");
  ASSERT_NE(begin_frame_calls, nullptr);
  ASSERT_NE(wind_n_calls, nullptr);
  ASSERT_NE(frame_min, nullptr);
  ASSERT_NE(frame_max, nullptr);
  // This call supplies no gravity/mass_spec, so for_testing passes physics = nullptr to
  // Vehicle::init (its own doc comment): Vehicle::integrate is then a no-op and neither vehicle's
  // state ever changes, so every one of the 2*kFrames begin_frame calls sees exactly {0.0, 1000.0}
  // across the two vehicles. EachVehicleWrenchSourceReceivesItsOwnEnvironmentSample below supplies
  // both and gets real physics instead.
  EXPECT_DOUBLE_EQ(*begin_frame_calls, static_cast<double>(2 * kFrames));
  EXPECT_DOUBLE_EQ(*wind_n_calls, static_cast<double>(2 * kFrames));
  // Distinct within the (last) frame, proving begin_frame ran against each vehicle's OWN
  // VehicleState, not the same one twice -- this is exactly what
  // BeginFrameThenWindNAreEachCalledExactlyOncePerFrame's single vehicle cannot show.
  EXPECT_DOUBLE_EQ(*frame_min, 0.0);
  EXPECT_DOUBLE_EQ(*frame_max, 1000.0);
}

// Cowork re-review: for_testing's first draft never set frames_, so sim.run() with no argument
// silently ran zero frames against a for_testing Simulation regardless of what a test might expect
// -- every existing test happened to use run(N) or step() directly, so nothing caught it. Proves
// the fix: passing frames to for_testing makes run() step exactly that many, the same contract
// from_config's own run() (reading "sim.frames") already has.
TEST(WindMultiVehicle, RunWithNoArgumentStepsTheFramesPassedToForTesting) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin wind_loaded = loader.load("tests/plugins/counting_wind");
  auto* const wind = dynamic_cast<WindModel*>(wind_loaded.model.get());
  astra::LoadedPlugin earth_loaded = loader.load("plugins/physical/earth/flat");
  auto* const earth = dynamic_cast<EarthModel*>(earth_loaded.model.get());
  astra::SignalRegistry wind_registry;
  wind->init(Config::from_string("{}\n", "counting-wind-fixture"), wind_registry);
  astra::SignalRegistry earth_registry;
  earth->init(Config::from_string("{}\n", "earth-flat-fixture"), earth_registry);

  const std::array<StateVector, 1> initial_states{state_with_p_n_north(0.0)};
  constexpr std::uint64_t kFrames = 7;
  Simulation sim =
      Simulation::for_testing(loader, initial_states, {}, wind, earth, nullptr, nullptr, kFrames);
  EXPECT_EQ(sim.frames(), kFrames);
  EXPECT_FALSE(sim.run());  // no argument: must read the frames passed above, not default to 0

  const double* begin_frame_calls = wind_registry.find("counting_wind.begin_frame_calls");
  ASSERT_NE(begin_frame_calls, nullptr);
  EXPECT_DOUBLE_EQ(*begin_frame_calls, static_cast<double>(kFrames));
}

TEST(WindMultiVehicle, EachVehicleWrenchSourceReceivesItsOwnEnvironmentSample) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin wind_loaded = loader.load("tests/plugins/counting_wind");
  auto* const wind = dynamic_cast<WindModel*>(wind_loaded.model.get());
  astra::LoadedPlugin earth_loaded = loader.load("plugins/physical/earth/flat");
  auto* const earth = dynamic_cast<EarthModel*>(earth_loaded.model.get());
  astra::SignalRegistry wind_registry;
  wind->init(Config::from_string("{}\n", "counting-wind-fixture"), wind_registry);
  astra::SignalRegistry earth_registry;
  earth->init(Config::from_string("{}\n", "earth-flat-fixture"), earth_registry);
  // WrenchSource::wrench_b is only ever called when Vehicle::integrate has a real Eom
  // (eom_.has_value(), core/src/kernel/vehicle.cpp) -- so this test needs physics configured, not
  // just wind/earth, or wind_consumer would never run at all and both signals would silently read
  // their untouched zero default (caught by this test itself before this fix: vehicle 0's expected
  // value happened to be zero too, so only vehicle 1's assertion failed).
  astra::LoadedPlugin gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto* const gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  astra::SignalRegistry gravity_registry;
  gravity->init(Config::from_string("{ g: 0.0 }\n", "gravity-constant-fixture"), gravity_registry);
  const Vehicle::ModelSpec mass_spec{
      "plugins/physical/mass/constant",
      Config::from_string("{ mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }\n", "mass-fixture"), 1};

  const std::array<StateVector, 2> initial_states{state_with_p_n_north(0.0),
                                                  state_with_p_n_north(1000.0)};
  const std::vector<Vehicle::ModelSpec> models{Vehicle::ModelSpec{
      "tests/plugins/wind_consumer", Config::from_string("{}\n", "wind-consumer-fixture"), 1}};
  Simulation sim =
      Simulation::for_testing(loader, initial_states, models, wind, earth, gravity, &mass_spec);
  EXPECT_FALSE(sim.step());

  // component 1 (east) of counting_wind's wind_n carries the p_n north value begin_frame last saw
  // for the SAME vehicle wind_n is then evaluated for (counting_wind.cpp's own header comment) --
  // if both vehicles' WrenchSources recorded the same value, that would mean the same
  // EnvironmentSample reached both, rather than each vehicle sampled against its own state.
  const double* v0_east = sim.registry().find("v0.wind_consumer.wind_n_east");
  const double* v1_east = sim.registry().find("v1.wind_consumer.wind_n_east");
  ASSERT_NE(v0_east, nullptr);
  ASSERT_NE(v1_east, nullptr);
  EXPECT_DOUBLE_EQ(*v0_east, 0.0);
  EXPECT_DOUBLE_EQ(*v1_east, 1000.0);
}

// D4: the guard from_config runs after building its vehicles (currently unreachable in
// production, since from_config always builds exactly one) is exercised here through
// Simulation::for_testing instead, the same check_wind_shareable call, same ConfigError.
TEST(WindMultiVehicle, RejectsMoreThanOneVehicleWithANonShareableWindModel) {
  MinimalWind wind;  // leaves shareable_across_vehicles() at the default: false
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  // A real EarthModel, not null: for_testing now rejects non-null wind paired with a null earth
  // before this guard ever runs (RejectsWindWithoutEarth below), so this fixture must supply one to
  // reach the guard this test actually means to exercise.
  astra::LoadedPlugin earth_loaded = loader.load("plugins/physical/earth/flat");
  auto* const earth = dynamic_cast<EarthModel*>(earth_loaded.model.get());
  astra::SignalRegistry earth_registry;
  earth->init(Config::from_string("{}\n", "earth-flat-fixture"), earth_registry);
  const std::array<StateVector, 2> initial_states{StateVector::Zero(), StateVector::Zero()};
  try {
    static_cast<void>(Simulation::for_testing(loader, initial_states, {}, &wind, earth));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.wind");
  }
}

// Adversarial-review finding (Cowork re-review round 2, second pass, "medium"): a non-null wind
// with a null earth reached Simulation::step's wind branch, which dereferences earth_
// unconditionally inside noexcept -- undefined behavior, reachable with a single vehicle, not just
// through the D4 shareability guard above. Proves for_testing now rejects it before step() can be
// reached.
TEST(WindMultiVehicle, RejectsWindWithoutEarth) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin wind_loaded = loader.load("tests/plugins/counting_wind");
  auto* const wind = dynamic_cast<WindModel*>(wind_loaded.model.get());
  astra::SignalRegistry wind_registry;
  wind->init(Config::from_string("{}\n", "counting-wind-fixture"), wind_registry);
  const std::array<StateVector, 1> initial_states{state_with_p_n_north(0.0)};
  try {
    static_cast<void>(Simulation::for_testing(loader, initial_states, {}, wind, nullptr));
    FAIL() << "expected astra::Error";
  } catch (const astra::Error& error) {
    EXPECT_TRUE(mentions(error, "earth")) << error.what();
  }
}

// Adversarial-review finding (Cowork re-review round 2, "high"): gravity supplied without
// mass_spec (or vice versa) used to silently build a no-physics Simulation, discarding whichever
// one was passed. Proves for_testing now rejects the combination instead.
TEST(WindMultiVehicle, RejectsGravityWithoutMassSpec) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto* const gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  astra::SignalRegistry gravity_registry;
  gravity->init(Config::from_string("{ g: 0.0 }\n", "gravity-constant-fixture"), gravity_registry);
  const std::array<StateVector, 1> initial_states{state_with_p_n_north(0.0)};
  try {
    static_cast<void>(
        Simulation::for_testing(loader, initial_states, {}, nullptr, nullptr, gravity, nullptr));
    FAIL() << "expected astra::Error";
  } catch (const astra::Error& error) {
    EXPECT_TRUE(mentions(error, "mass_spec")) << error.what();
  }
}

// Same finding: gravity + mass_spec supplied without earth used to reach Vehicle::init's
// eom_.emplace(*physics->earth, ...) (vehicle.cpp) with a null earth -- undefined behavior, not a
// thrown error, since Vehicle::PhysicsSpec::earth was never checked before this fix.
TEST(WindMultiVehicle, RejectsGravityAndMassSpecWithoutEarth) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto* const gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  astra::SignalRegistry gravity_registry;
  gravity->init(Config::from_string("{ g: 0.0 }\n", "gravity-constant-fixture"), gravity_registry);
  const Vehicle::ModelSpec mass_spec{
      "plugins/physical/mass/constant",
      Config::from_string("{ mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }\n", "mass-fixture"), 1};
  const std::array<StateVector, 1> initial_states{state_with_p_n_north(0.0)};
  try {
    static_cast<void>(
        Simulation::for_testing(loader, initial_states, {}, nullptr, nullptr, gravity, &mass_spec));
    FAIL() << "expected astra::Error";
  } catch (const astra::Error& error) {
    EXPECT_TRUE(mentions(error, "earth")) << error.what();
  }
}

}  // namespace
