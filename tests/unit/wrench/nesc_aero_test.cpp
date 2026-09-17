// tests/plugins/nesc_aero (PLAN.md M2 PR 3): sphere drag and brick rate damping, checked against
// clean synthetic values so the assertions pin the formula itself rather than a unit-converted
// NESC number (nesc_aero.cpp's own file header records the actual NESC check-case cross-checks --
// case 7 sim_03's drag at t = 0 and case 3 sim_04's damping at t = 10/30 s -- that were performed
// during this PR's own design). Also pins the wind-requirement blocker fix: this WrenchSource must
// not declare requires_environment().wind unconditionally, since eight of the ten NESC cases
// configure no physical.wind at all.
#include <gtest/gtest.h>

#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::EnvironmentSample;
using astra::Simulation;
using astra::StateVector;
using astra::VehicleState;
using astra::WrenchSource;

constexpr double kTolerance = 1e-12;

WrenchSource& load(std::string_view yaml, astra::SignalRegistry& registry) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("tests/plugins/nesc_aero");
  auto* const source = dynamic_cast<WrenchSource*>(loaded.model.get());
  source->init(Config::from_string(yaml, "nesc-aero-fixture"), registry);
  return *source;
}

// VehicleState (vehicle_state.hpp) holds a const StateVector& -- a helper that built one over a
// function-local StateVector and returned VehicleState by value would hand back a dangling
// reference the instant it returned. make_state() therefore returns the StateVector itself,
// matching earth_kinematics_test.cpp's own established pattern: the caller keeps the StateVector
// alive in its own scope and constructs VehicleState there.
StateVector make_state(const Eigen::Vector3d& v_b, const Eigen::Vector3d& omega_b) {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity q_nb: body == nav axes
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

TEST(NescAero, DragMatchesTheQuadraticDragLawWithWind) {
  astra::SignalRegistry registry;
  WrenchSource& source = load("s: 2.0\ncd: 0.5\n", registry);
  const StateVector y = make_state(Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero());
  const VehicleState vs{y};
  EnvironmentSample env;
  env.wind_n = Eigen::Vector3d{0.0, 10.0, 0.0};  // air east; sphere at rest -> v_rel_b = (0,-10,0)
  env.atmosphere.density_kg_m3 = 1.2;
  const astra::Wrench wrench = source.wrench_b(vs, env);
  // F = -Cd * 0.5 * rho * S * |v| * v = -0.5*0.5*1.2*2.0*10*(0,-10,0) = (0, 60, 0).
  EXPECT_NEAR(0.0, wrench.force_b(0), kTolerance);
  EXPECT_NEAR(60.0, wrench.force_b(1), kTolerance);
  EXPECT_NEAR(0.0, wrench.force_b(2), kTolerance);
  EXPECT_TRUE(wrench.moment_b.isZero()) << "no damping coefficients configured";
}

TEST(NescAero, DragIsExactlyZeroAtZeroRelativeVelocityNoNaN) {
  astra::SignalRegistry registry;
  WrenchSource& source = load("s: 2.0\ncd: 0.5\n", registry);
  const StateVector y = make_state(Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero());
  const VehicleState vs{y};
  EnvironmentSample env;  // wind_n zero by default -> v_rel_b == v_b == 0
  env.atmosphere.density_kg_m3 = 1.2;
  const astra::Wrench wrench = source.wrench_b(vs, env);
  EXPECT_TRUE(wrench.force_b.isZero());
  EXPECT_TRUE(wrench.force_b.allFinite()) << "the |v|*v form must not divide by |v|";
}

TEST(NescAero, DampingMatchesTheLinearFormAndUsesInertialBodyRates) {
  astra::SignalRegistry registry;
  WrenchSource& source =
      load("s: 1.0\nb: 1.0\ncbar: 1.0\nclp: -1.0\ncmq: -1.0\ncnr: -1.0\n", registry);
  const StateVector y = make_state({5.0, 0.0, 0.0}, {0.1, 0.2, 0.3});
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 1.0;
  const astra::Wrench wrench = source.wrench_b(vs, env);
  // L = 0.25*rho*V*S*b^2*Clp*p = 0.25*1*5*1*1*(-1)*0.1 = -0.125
  // M = 0.25*1*5*1*1*(-1)*0.2 = -0.25
  // N = 0.25*1*5*1*1*(-1)*0.3 = -0.375
  EXPECT_NEAR(-0.125, wrench.moment_b(0), kTolerance);
  EXPECT_NEAR(-0.25, wrench.moment_b(1), kTolerance);
  EXPECT_NEAR(-0.375, wrench.moment_b(2), kTolerance);
  EXPECT_TRUE(wrench.force_b.isZero()) << "cd defaults to 0.0";
}

// The blocker fix (PLAN.md M2 PR 3): requires_environment().wind must be config-driven, not
// unconditionally true, since eight of the ten NESC cases configure no physical.wind at all.
TEST(NescAero, RequiresWindDefaultsToFalse) {
  astra::SignalRegistry registry;
  WrenchSource& source = load("{}\n", registry);
  EXPECT_FALSE(source.requires_environment().wind);
  EXPECT_TRUE(source.requires_environment().atmosphere);
}

TEST(NescAero, RequiresWindTrueWhenConfigured) {
  astra::SignalRegistry registry;
  WrenchSource& source = load("requires_wind: true\n", registry);
  EXPECT_TRUE(source.requires_environment().wind);
}

// The three states the actual ten NESC configs exercise (PLAN.md M2 PR 3's blocker-fix test):
// requires_wind true with physical.wind present (accepts), requires_wind true with physical.wind
// absent (rejects, naming physical.wind), requires_wind false (default) with no physical.wind
// (accepts) -- through Simulation::from_config, the real init-time path, not the plugin in
// isolation.
Simulation load_sim(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

TEST(NescAero, RequiresWindTrueWithWindConfiguredIsAccepted) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/nesc_aero, requires_wind: true } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  wind: { plugin: plugins/physical/wind/uniform, north: 1.0 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "nesc-aero-requires-wind-with-wind-fixture");
  Simulation sim = load_sim(config);
  EXPECT_FALSE(sim.run());
}

TEST(NescAero, RequiresWindTrueWithoutWindConfiguredIsRejected) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/nesc_aero, requires_wind: true } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "nesc-aero-requires-wind-without-wind-fixture");
  try {
    static_cast<void>(load_sim(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.wind");
  }
}

TEST(NescAero, RequiresWindFalseWithoutWindConfiguredIsAccepted) {
  const Config config = Config::from_string(
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/nesc_aero } ] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n",
      "nesc-aero-no-wind-required-fixture");
  Simulation sim = load_sim(config);
  EXPECT_FALSE(sim.run());
}

}  // namespace
