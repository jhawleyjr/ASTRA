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

// physical.atmosphere and physical.wind joined earth/gravity/mass in M2 PR 1b/PR 2; a slot name
// none of the five recognize is still rejected as an unconsumed key (§15). "magnetic" -- not
// "wind" -- is the unrecognized-slot fixture now that physical.wind is a real slot (M2 PR 2); see
// AcceptsAPhysicalSpecWithWind/RejectsAWindPluginThatDoesNotImplementWindModel below for wind's own
// coverage.
TEST(SimulationPhysicalConfig, RejectsAnUnknownPhysicalSlotName) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "  magnetic: plugins/physical/atmosphere/isa1976\n",
      "unknown-slot-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "physical.magnetic")) << error.what();
  }
}

// physical.atmosphere is optional (M2 PR 1b): a physics config naming it loads like any other slot.
TEST(SimulationPhysicalConfig, AcceptsAPhysicalSpecWithAtmosphere) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "with-atmosphere-fixture");
  static_cast<void>(load(config));  // must not throw
}

// The converse: every fixture above that omits physical.atmosphere already exercises this, but one
// test says so explicitly -- the optionality guarantee every M2-PR-1-vintage config relies on.
TEST(SimulationPhysicalConfig, AcceptsAPhysicalSpecWithoutAtmosphere) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "without-atmosphere-fixture");
  static_cast<void>(load(config));  // must not throw
}

// physical.wind is optional (M2 PR 2): a physics config naming it loads like any other slot.
TEST(SimulationPhysicalConfig, AcceptsAPhysicalSpecWithWind) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: { plugin: plugins/physical/wind/uniform, north: 5.0 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "with-wind-fixture");
  static_cast<void>(load(config));  // must not throw
}

// The converse: every fixture above that omits physical.wind already exercises this, but one test
// says so explicitly -- the same optionality guarantee physical.atmosphere already has.
TEST(SimulationPhysicalConfig, AcceptsAPhysicalSpecWithoutWind) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "without-wind-fixture");
  static_cast<void>(load(config));  // must not throw
}

// A plugin loaded under physical.wind that does not implement WindModel (here, an atmosphere
// plugin) is rejected the same way the earth/gravity/atmosphere slots already reject a mismatched
// plugin -- see load_environment_model's dynamic_cast in simulation.cpp.
TEST(SimulationPhysicalConfig, RejectsAWindPluginThatDoesNotImplementWindModel) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "wind-wrong-type-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected PluginLoadError";
  } catch (const astra::PluginLoadError& error) {
    EXPECT_TRUE(mentions(error, "WindModel")) << error.what();
  }
}

// begin_frame/wind_n are sampled every major frame by Simulation::step regardless of
// physical.wind's own scheduler divisor (D6, PLAN.md M2 PR 2): a divisor other than 1 has no
// defined meaning yet, so it is rejected at init rather than silently desynchronized.
TEST(SimulationPhysicalConfig, RejectsAWindDivisorOtherThanOne) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: { plugin: plugins/physical/wind/uniform, divisor: 10 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "wind-divisor-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.wind.divisor");
  }
}

// §15: "altitude inside the atmosphere model's valid range". isa1976's range is [-5000, 86000] m
// geometric (plugins/physical/atmosphere/isa1976/isa1976.cpp); flat Earth's p_n(2) is NED altitude
// directly, so 90000 is unambiguously outside it.
TEST(SimulationPhysicalConfig, RejectsAnInitialAltitudeOutsideTheAtmosphereModelsValidRange) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { p_n: [0.0, 0.0, -90000.0] } }\n",
      "altitude-outside-atmosphere-range-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "initial.state.p_n");
    EXPECT_TRUE(mentions(error, "physical/atmosphere/isa1976")) << error.what();
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

// Codex adversarial review: earth/round and gravity/inverse_square each carry their own,
// independently configurable "radius", and the representation check alone does not relate them.
// Earth radius 3000 m accepts altitude -2000 m at init (-2000 > -3000); gravity radius 2000 m would
// divide by exactly zero in g_n() at that same altitude were it not for GravityModel::validate_p_n
// (gravity_model.hpp) rejecting it too, independently of earth/round's own check.
TEST(SimulationPhysicalConfig, RejectsAnInverseSquareRadiusInconsistentWithTheInitialAltitude) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: { plugin: plugins/physical/earth/round, radius: 3000.0 }\n"
      "  gravity: { plugin: plugins/physical/gravity/inverse_square, radius: 2000.0 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { p_n: [0.0, 0.0, -2000.0] } }\n",
      "inconsistent-radius-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "radius")) << error.what();
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
