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

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::Simulation;
using astra::StateVector;
using astra::StepContext;
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

TEST(WindCallContract, BeginFrameThenWindNAreEachCalledExactlyOncePerVehiclePerFrame) {
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

}  // namespace
