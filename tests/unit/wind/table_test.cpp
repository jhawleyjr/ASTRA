// plugins/physical/wind/table (docs/architecture.md §7, PLAN.md M2 PR 3, ADR-0013): piecewise-
// linear wind vs. altitude, clamped outside the table's own range rather than extrapolated.
#include <gtest/gtest.h>

#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::WindModel;

// VehicleState (vehicle_state.hpp) holds a const StateVector& -- a helper that built one over a
// function-local StateVector and returned VehicleState by value would hand back a dangling
// reference the instant it returned. make_state()/make_vs() therefore return the StateVector
// itself, matching earth_kinematics_test.cpp's own established pattern: the caller keeps the
// StateVector alive in its own scope and constructs VehicleState there.
StateVector make_state() {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity quaternion; unused by wind/table anyway
  return y;
}

// A fresh WindModel instance per call, unlike wind_test.cpp's load()/reuse-a-static pattern: some
// of these tests need init() itself to throw, which a shared static instance's own prior successful
// init would otherwise mask nothing about (each call here is a distinct object either way).
WindModel& load(std::string_view yaml, astra::SignalRegistry& registry) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("plugins/physical/wind/table");
  auto* const wind = dynamic_cast<WindModel*>(loaded.model.get());
  wind->init(Config::from_string(yaml, "table-fixture"), registry);
  return *wind;
}

Eigen::Vector3d wind_at(WindModel& wind, double altitude_m) {
  const StepContext ctx{0.0, 0.001, 0};
  const StateVector y = make_state();
  const VehicleState vs{y};
  wind.begin_frame(ctx, vs, altitude_m);
  return wind.wind_n(ctx, vs);
}

constexpr double kTolerance = 1e-12;

TEST(WindTable, InterpolatesLinearlyBetweenTwoPoints) {
  astra::SignalRegistry registry;
  // Case 8's own formula (Vol II Table 32): V = 0.003*h_ft - 20 ft/s, h in feet -- converted here
  // to the SI values a config author would actually write (ADR-0002: conversion happens at the
  // config-authoring boundary, not inside the plugin, which reads and stores plain doubles). At
  // h = 0 m: -20 ft/s = -6.096 m/s; at h = 12192 m (40,000 ft): 100 ft/s = 30.48 m/s.
  WindModel& wind =
      load("points: [ { altitude: 0.0, east: -6.096 }, { altitude: 12192.0, east: 30.48 } ]\n",
           registry);
  // At h = 9144 m (30,000 ft), the formula gives V = 0.003*30000 - 20 = 70 ft/s = 21.336 m/s --
  // matches the linear interpolation between the two configured SI endpoints without the plugin
  // itself ever encoding the formula.
  const Eigen::Vector3d wind_n = wind_at(wind, 9144.0);
  EXPECT_NEAR(21.336, wind_n(1), 1e-9);
  EXPECT_NEAR(0.0, wind_n(0), kTolerance);
  EXPECT_NEAR(0.0, wind_n(2), kTolerance);
}

TEST(WindTable, ClampsBelowTheLowestPointRatherThanExtrapolating) {
  astra::SignalRegistry registry;
  WindModel& wind =
      load("points: [ { altitude: 1000.0, north: 5.0 }, { altitude: 2000.0, north: 10.0 } ]\n",
           registry);
  const Eigen::Vector3d wind_n = wind_at(wind, -500.0);
  EXPECT_NEAR(5.0, wind_n(0), kTolerance);
}

TEST(WindTable, ClampsAboveTheHighestPointRatherThanExtrapolating) {
  astra::SignalRegistry registry;
  WindModel& wind =
      load("points: [ { altitude: 1000.0, north: 5.0 }, { altitude: 2000.0, north: 10.0 } ]\n",
           registry);
  const Eigen::Vector3d wind_n = wind_at(wind, 5000.0);
  EXPECT_NEAR(10.0, wind_n(0), kTolerance);
}

TEST(WindTable, DefaultsUnspecifiedComponentsToZero) {
  astra::SignalRegistry registry;
  WindModel& wind = load(
      "points: [ { altitude: 0.0, north: 3.0 }, { altitude: 100.0, north: 3.0 } ]\n", registry);
  const Eigen::Vector3d wind_n = wind_at(wind, 50.0);
  EXPECT_NEAR(3.0, wind_n(0), kTolerance);
  EXPECT_NEAR(0.0, wind_n(1), kTolerance);
  EXPECT_NEAR(0.0, wind_n(2), kTolerance);
}

TEST(WindTable, RejectsFewerThanTwoPoints) {
  astra::SignalRegistry registry;
  try {
    static_cast<void>(load("points: [ { altitude: 0.0, north: 1.0 } ]\n", registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_NE(error.config_path().find("points"), std::string::npos);
  }
}

TEST(WindTable, RejectsNonIncreasingAltitudes) {
  astra::SignalRegistry registry;
  try {
    static_cast<void>(
        load("points: [ { altitude: 1000.0, north: 1.0 }, { altitude: 1000.0, north: 2.0 } ]\n",
             registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_NE(error.config_path().find("altitude"), std::string::npos);
  }
}

TEST(WindTable, RejectsDescendingAltitudes) {
  astra::SignalRegistry registry;
  try {
    static_cast<void>(
        load("points: [ { altitude: 2000.0, north: 1.0 }, { altitude: 1000.0, north: 2.0 } ]\n",
             registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_NE(error.config_path().find("altitude"), std::string::npos);
  }
}

// Never reads vs.p_n() -- only the Earth-derived altitude_m parameter -- so no Earth model's
// position representation can be mismatched (wind_model.hpp; check_position_representation in
// simulation.cpp treats kAny as always compatible).
TEST(WindTable, AssumesAnyPositionRepresentation) {
  astra::SignalRegistry registry;
  WindModel& wind = load("points: [ { altitude: 0.0 }, { altitude: 1000.0 } ]\n", registry);
  EXPECT_EQ(wind.assumed_position_representation(), astra::PositionRepresentation::kAny);
}

// Memoryless (table_test.cpp's own file header explains why): sharing it across more than one
// vehicle is exact, not approximate.
TEST(WindTable, IsShareableAcrossVehicles) {
  astra::SignalRegistry registry;
  WindModel& wind = load("points: [ { altitude: 0.0 }, { altitude: 1000.0 } ]\n", registry);
  EXPECT_TRUE(wind.shareable_across_vehicles());
}

}  // namespace
