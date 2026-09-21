// aircraft/notional_transport/physical/aero (docs/plans/pr4-notional-aero-turbofan.md, R2/R2b):
// exercises the real plugin's wrench_b() end to end -- real Config (loaded from the shipped
// cruise.yaml, not a hand-built fixture string, so a coefficient edit there must move this test's
// own answer, the same reasoning notional_transport_mass_test.cpp already documents), hand-built
// VehicleState/EnvironmentSample, no Simulation. Conditions N and O reproduce
// tests/unit/aero/notional_transport_aero_math_test.cpp's own literals exactly (proving the
// plugin's Config-reading and air_data:: wiring reproduces the pure math, not a second independent
// oracle); Condition Z (V=0) and the R-001-shaped raw-density fault are new here, at the wrench_b()
// level, per the plan's own Open item 5/6.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::AeroModel;
using astra::Config;
using astra::EnvironmentSample;
using astra::InputRegistry;
using astra::SignalRegistry;
using astra::StateVector;
using astra::VehicleState;

// StateVector outlives the VehicleState built over it -- tests/unit/wrench/nesc_aero_test.cpp's own
// make_state() pattern.
StateVector make_state(const Eigen::Vector3d& v_b, const Eigen::Vector3d& omega_b) {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity q_nb: body == nav axes, alpha/beta read v_b as-is
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

// The static LoadedPlugin/InputRegistry are reassigned each call -- earth_kinematics_test.cpp's own
// established pattern (see nesc_aero_test.cpp's identical comment); every TEST() below calls this
// once and does not retain the reference past its own body.
AeroModel& load_with_bound_controls(double elevator_rad, double aileron_rad, double rudder_rad) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  static InputRegistry inputs;
  loaded = loader.load("aircraft/notional_transport/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());

  const Config config = Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
                                          "/notional_transport/configs/cruise.yaml");
  SignalRegistry registry;
  aero->init(config.at("vehicle.models").at(0), registry);
  inputs = InputRegistry{};
  aero->bind_inputs(inputs, registry);
  inputs.write("controls.elevator", elevator_rad);
  inputs.write("controls.aileron", aileron_rad);
  inputs.write("controls.rudder", rudder_rad);
  EXPECT_TRUE(inputs.latch().empty());
  return *aero;
}

void expect_matches(double actual, double expected, const char* what) {
  if (expected == 0.0) {
    EXPECT_EQ(actual, 0.0) << what;
  } else {
    EXPECT_NEAR(actual, expected, std::abs(expected) * 1e-9) << what;
  }
}

// Condition N (docs/plans/pr4-notional-aero-turbofan.md's "Worked numbers"): reproduces
// notional_transport_aero_math_test.cpp's own ConditionNNominalWingsLevel literals. v_b chosen (the
// same construction Condition O below uses) so air_data::angle_of_attack/true_airspeed reproduce
// alpha=0.025484, true_airspeed_mps=230 exactly, with q_nb identity and wind_n zero (v_rel_b ==
// v_b).
TEST(NotionalTransportAeroWrench, ConditionNReproducesThePureMathAnswer) {
  AeroModel& aero = load_with_bound_controls(0.0, 0.0, 0.0);
  constexpr double kTrueAirspeedMps = 230.0;
  constexpr double kAlphaRad = 0.025484;
  const Eigen::Vector3d v_b(kTrueAirspeedMps * std::cos(kAlphaRad), 0.0,
                            kTrueAirspeedMps * std::sin(kAlphaRad));
  const StateVector y = make_state(v_b, Eigen::Vector3d::Zero());
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;

  const astra::Wrench wrench = aero.wrench_b(vs, env);
  expect_matches(wrench.force_b(0), -20461.3632868957, "force_b.x");
  expect_matches(wrench.force_b(1), 0.0, "force_b.y");
  expect_matches(wrench.force_b(2), -369817.2356669478, "force_b.z");
  expect_matches(wrench.moment_b(0), 0.0, "moment_b.x");
  expect_matches(wrench.moment_b(1), 96622.4239007194, "moment_b.y");
  expect_matches(wrench.moment_b(2), 0.0, "moment_b.z");
}

// Condition O: reproduces ConditionOOffNominalAllTermsNonzero's own literals. v_b/omega_b/wind
// chosen so air_data::angle_of_attack/sideslip/true_airspeed reproduce alpha=0, beta=0.1,
// true_airspeed_mps=100 exactly: with q_nb identity and wind_n zero, v_rel_b == v_b, so
// v_b = true_airspeed_mps * (cos(alpha)*cos(beta), sin(beta), sin(alpha)*cos(beta)).
TEST(NotionalTransportAeroWrench, ConditionOReproducesThePureMathAnswer) {
  AeroModel& aero = load_with_bound_controls(0.1, -0.05, 0.02);
  constexpr double kTrueAirspeedMps = 100.0;
  constexpr double kAlphaRad = 0.0;
  constexpr double kBetaRad = 0.1;
  const Eigen::Vector3d v_b(kTrueAirspeedMps * std::cos(kAlphaRad) * std::cos(kBetaRad),
                            kTrueAirspeedMps * std::sin(kBetaRad),
                            kTrueAirspeedMps * std::sin(kAlphaRad) * std::cos(kBetaRad));
  const StateVector y = make_state(v_b, Eigen::Vector3d(0.05, -0.02, 0.03));
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 1.0;

  const astra::Wrench wrench = aero.wrench_b(vs, env);
  expect_matches(wrench.force_b(0), -7061.8221796725, "force_b.x");
  expect_matches(wrench.force_b(1), -55376.3870330663, "force_b.y");
  expect_matches(wrench.force_b(2), -119668.405824, "force_b.z");
  expect_matches(wrench.moment_b(0), -325716.8717090227, "moment_b.x");
  expect_matches(wrench.moment_b(1), -99667.9663201383, "moment_b.y");
  expect_matches(wrench.moment_b(2), 301932.9197071403, "moment_b.z");
}

// Condition Z: V=0 with nonzero rates/controls -- every component is EXACTLY zero (Open item 5),
// not merely finite.
TEST(NotionalTransportAeroWrench, ConditionZIsExactlyZeroAtZeroTrueAirspeed) {
  AeroModel& aero = load_with_bound_controls(0.3, -0.2, 0.1);
  const StateVector y = make_state(Eigen::Vector3d::Zero(), Eigen::Vector3d(0.1, -0.2, 0.15));
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;

  const astra::Wrench wrench = aero.wrench_b(vs, env);
  EXPECT_TRUE(wrench.force_b.isZero());
  EXPECT_TRUE(wrench.moment_b.isZero());
}

// R2b / R-001 (raw-not-derived check, Open item 6): an infinite density_kg_m3, the one raw
// atmosphere field this plugin actually reads, must halt this plugin's own contribution -- the NaN
// sentinel, never a plausible-looking finite answer.
TEST(NotionalTransportAeroWrench, InfiniteDensityReturnsTheNonFiniteSentinel) {
  AeroModel& aero = load_with_bound_controls(0.0, 0.0, 0.0);
  const StateVector y = make_state(Eigen::Vector3d(230.0, 0.0, 0.0), Eigen::Vector3d::Zero());
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = std::numeric_limits<double>::infinity();

  const astra::Wrench wrench = aero.wrench_b(vs, env);
  EXPECT_FALSE(wrench.force_b.allFinite());
  EXPECT_FALSE(wrench.moment_b.allFinite());
}

}  // namespace
