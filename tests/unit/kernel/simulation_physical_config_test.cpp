// Simulation's "physical.earth" / "physical.gravity" / "physical.mass" slots and "initial.state"
// (docs/architecture.md §13, PLAN.md M1 PR 3): physics is opt-in, all three slots or none; a bad
// mass config is rejected naming the offending key (§15's pre-run sanity checks); "initial.trim"
// and an unknown "physical.*" slot are rejected the same way any unconsumed key is (§15).
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::Simulation;

bool mentions(const std::exception& error, std::string_view text) {
  return std::string_view{error.what()}.find(text) != std::string_view::npos;
}

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

// A minimal physics config with every field the caller supplies substituted into the mass slot, so
// each test below only has to name what's different about it.
Simulation load_with_mass_fields(std::string_view mass_fields) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, " +
          std::string{mass_fields} + " }\n",
      "mass-fixture");
  return load(config);
}

TEST(SimulationPhysicalConfig, RejectsNonPositiveMassNamingTheKey) {
  try {
    static_cast<void>(load_with_mass_fields("mass: 0.0, ixx: 1.0, iyy: 1.0, izz: 1.0"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.mass.mass");
  }
}

TEST(SimulationPhysicalConfig, RejectsNonFiniteMassNamingTheKey) {
  try {
    static_cast<void>(load_with_mass_fields("mass: .nan, ixx: 1.0, iyy: 1.0, izz: 1.0"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.mass.mass");
  }
}

// ixx = iyy = izz = 1, ixy = 2: the resulting matrix is symmetric but has a negative eigenvalue
// (the off-diagonal term violates the triangle inequality a positive-definite inertia tensor must
// satisfy).
TEST(SimulationPhysicalConfig, RejectsNonPositiveDefiniteInertia) {
  try {
    static_cast<void>(load_with_mass_fields("mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0, ixy: 2.0"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.mass");
    EXPECT_TRUE(mentions(error, "positive-definite")) << error.what();
  }
}

TEST(SimulationPhysicalConfig, RejectsInitialTrimAsAnUnknownKey) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { trim: { altitude: 10000 } }\n",
      "initial-trim-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "initial.trim")) << error.what();
  }
}

// "atmosphere" is not yet a config slot: AtmosphereModel/isa1976 grew too large to land alongside
// the Earth/gravity models in this PR (PLAN.md's own size contingency) and are a follow-up PR.
TEST(SimulationPhysicalConfig, RejectsAnUnknownPhysicalSlotName) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n",
      "unknown-slot-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "physical.atmosphere")) << error.what();
  }
}

// gravity/wgs84_j2 assumes geodetic p_n (PositionRepresentation::kGeodetic); earth/flat provides
// kFlatNed. Pairing them would read NED metres as radians, so it is rejected at init.
TEST(SimulationPhysicalConfig, RejectsAGeodeticGravityModelPairedWithFlatEarth) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/wgs84_j2\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "representation-mismatch-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.gravity");
    EXPECT_TRUE(mentions(error, "physical.earth")) << error.what();
  }
}

// The converse of the above: a gravity model that does not read p_n's geometry (kAny) is
// compatible with every Earth model, geodetic or not. frames: 0, so gravity never gets a chance to
// integrate and change the state -- this test is about the representation check accepting the
// pairing at init, not about what one frame of falling does to the state. Checked against p_n/v_b/
// omega_b individually, not the whole vector's isZero(): with "physical" configured, q_nb defaults
// to the identity quaternion (w = 1), so the full StateVector is never all-zero.
TEST(SimulationPhysicalConfig, AcceptsConstantGravityWithRoundEarth) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/round\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "compatible-representation-fixture");
  const Simulation sim = load(config);
  const astra::StateVector& y = sim.vehicles().front().state();
  EXPECT_TRUE(y.segment<astra::state::kPositionSize>(astra::state::kPosition).isZero());
  EXPECT_TRUE(y.segment<astra::state::kVelocitySize>(astra::state::kVelocity).isZero());
  EXPECT_TRUE(y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate).isZero());
  EXPECT_DOUBLE_EQ(1.0, y(astra::state::kQuaternion))
      << "identity attitude when euler_nb is absent";
}

TEST(SimulationPhysicalConfig, RejectsAPartialPhysicalSpecNamingTheMissingSlot) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n",
      "partial-physical-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.gravity");
  }
}

TEST(SimulationPhysicalConfig, InitialStateDefaultsToIdentityAttitudeWhenEulerNbIsAbsent) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { v_b: [100.0, 0.0, 0.0] } }\n",
      "identity-attitude-fixture");
  const Simulation sim = load(config);
  const astra::StateVector& y = sim.vehicles().front().state();
  EXPECT_DOUBLE_EQ(y(astra::state::kQuaternion), 1.0) << "identity quaternion when euler_nb absent";
  for (int i = 1; i < astra::state::kQuaternionSize; ++i) {
    EXPECT_DOUBLE_EQ(y(astra::state::kQuaternion + i), 0.0);
  }
  EXPECT_DOUBLE_EQ(y(astra::state::kVelocity), 100.0) << "v_b still read when euler_nb is absent";
}

// Codex review: an unvalidated NaN/Inf gravity value used to propagate into every EOM evaluation
// with no init-time error, instead of failing §15's pre-run parameter sanity check.
TEST(SimulationPhysicalConfig, RejectsNonFiniteGravityNamingTheKey) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: { plugin: plugins/physical/gravity/constant, g: .nan }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "nonfinite-gravity-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.gravity.g");
  }
}

// Codex review: an unvalidated NaN/Inf initial-state component used to start the sim already
// broken instead of failing §15's pre-run "no NaN/Inf anywhere in state" check.
TEST(SimulationPhysicalConfig, RejectsNonFiniteInitialStateComponent) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { v_b: [.inf, 0.0, 0.0] } }\n",
      "nonfinite-initial-state-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "initial.state.v_b.0");
  }
}

// Codex review: a non-map "initial.state" (a scalar or sequence) used to mark the whole block
// consumed while every field lookup silently read as absent, producing a valid-looking all-zero
// state instead of an error.
TEST(SimulationPhysicalConfig, RejectsANonMapInitialState) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: [1, 2, 3] }\n",
      "non-map-initial-state-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "initial.state");
  }
}

TEST(SimulationPhysicalConfig, PhysicsOffLeavesStateZeroedExactlyAsBeforePr3) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/dummy, divisor: 1 } ] }\n",
      "physics-off-fixture");
  const Simulation sim = load(config);
  EXPECT_TRUE(sim.vehicles().front().state().isZero());
}

}  // namespace
