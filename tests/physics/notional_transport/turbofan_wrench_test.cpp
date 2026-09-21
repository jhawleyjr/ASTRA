// aircraft/notional_transport/hardware/propulsion/turbofan
// (docs/plans/pr4-notional-aero-turbofan.md, R5): exercises the real plugin's wrench_b() end to end
// -- real Config (loaded from the shipped cruise.yaml), no Simulation. Condition N reproduces
// tests/unit/turbofan/notional_transport_turbofan_math_test.cpp's own literal. Condition O (Codex
// plan-critique finding, resolved) is checked here too, not only at the pure-math level: Condition
// N alone uses rho=0.4128, identical to density_ref_kgm3, so a wrench_b() bug that wired the
// config's own density_ref_kgm3 into the thrust law instead of the sampled
// env.atmosphere.density_kg_m3 would reproduce Condition N's answer by coincidence and pass
// undetected -- Condition O's rho=0.5 forces the two to diverge. Condition Z (V=0) is checked only
// here, not at the pure-math level (vacuous there: the thrust law has no V dependence to break).
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::EnvironmentSample;
using astra::InputRegistry;
using astra::PropulsionModel;
using astra::SignalRegistry;
using astra::StateVector;
using astra::VehicleState;

// PropulsionModel::wrench_b() ignores VehicleState entirely (density-ratio-only thrust law) -- any
// state works; zero is the simplest.
StateVector make_state() { return StateVector::Zero(); }

// vehicle.models index 1 is the left engine, 2 is the right (aircraft/notional_transport/configs/
// cruise.yaml's own ordering: aero, turbofan left, turbofan right).
PropulsionModel& load_with_throttle(std::size_t models_index, double throttle) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  static InputRegistry inputs;
  loaded = loader.load("aircraft/notional_transport/hardware/propulsion/turbofan");
  auto* const turbofan = dynamic_cast<PropulsionModel*>(loaded.model.get());

  const Config config = Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
                                          "/notional_transport/configs/cruise.yaml");
  SignalRegistry registry;
  turbofan->init(config.at("vehicle.models").at(models_index), registry);
  inputs = InputRegistry{};
  turbofan->bind_inputs(inputs, registry);
  inputs.write("controls.throttle", throttle);
  EXPECT_TRUE(inputs.latch().empty());
  return *turbofan;
}

void expect_matches(double actual, double expected, const char* what) {
  if (expected == 0.0) {
    EXPECT_EQ(actual, 0.0) << what;
  } else {
    EXPECT_NEAR(actual, expected, std::abs(expected) * 1e-9) << what;
  }
}

TEST(NotionalTransportTurbofanWrench, ConditionNReproducesThePureMathAnswer) {
  PropulsionModel& turbofan = load_with_throttle(1, 0.5);
  const StateVector y = make_state();
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;

  const astra::Wrench wrench = turbofan.wrench_b(vs, env);
  expect_matches(wrench.force_b(0), 14939.0609338715, "force_b.x");
}

// Codex plan-critique finding: a differing density forces divergence from any
// config/sampled-density mix-up.
TEST(NotionalTransportTurbofanWrench, ConditionOCatchesADensityWiringBug) {
  PropulsionModel& turbofan = load_with_throttle(1, 0.8);
  const StateVector y = make_state();
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.5;

  const astra::Wrench wrench = turbofan.wrench_b(vs, env);
  expect_matches(wrench.force_b(0), 28951.6684764952, "force_b.x");
}

TEST(NotionalTransportTurbofanWrench, ConditionZIsFiniteAndUnaffectedByTrueAirspeed) {
  PropulsionModel& turbofan = load_with_throttle(1, 0.6);
  const StateVector y = make_state();
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;

  const astra::Wrench wrench = turbofan.wrench_b(vs, env);
  expect_matches(wrench.force_b(0), 17926.8731206459, "force_b.x");
}

// Symmetric-yaw check restated at the wrench_b() level (two real plugin instances, left/right
// stations, own Config sub-trees) -- docs/plans/pr4-notional-aero-turbofan.md's own R4/R5 note.
TEST(NotionalTransportTurbofanWrench, SymmetricEqualThrottleNetYawMomentIsExactlyZero) {
  PropulsionModel& left = load_with_throttle(1, 0.5);
  const StateVector y = make_state();
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;
  const astra::Wrench left_wrench = left.wrench_b(vs, env);

  PropulsionModel& right = load_with_throttle(2, 0.5);
  const astra::Wrench right_wrench = right.wrench_b(vs, env);

  const Eigen::Vector3d moment_sum = left_wrench.moment_b + right_wrench.moment_b;
  EXPECT_EQ(moment_sum(0), 0.0);
  EXPECT_EQ(moment_sum(1), 0.0);
  EXPECT_EQ(moment_sum(2), 0.0);
}

}  // namespace
