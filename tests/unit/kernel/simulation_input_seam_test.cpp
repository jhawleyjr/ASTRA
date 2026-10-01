// ADR-0017's control-input write seam, end to end through Simulation
// (docs/plans/pr2-write-seam.md's Acceptance table R2-R7, R9-R11; R1/R8 are pinned at
// InputRegistry/HealthMonitor's own unit level, tests/unit/signals/input_registry_test.cpp and
// tests/unit/health/health_monitor_test.cpp; R6 is tests/physics/no_alloc_test.cpp). Uses
// tests/plugins/input_consumer throughout.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/plugin/loader.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::HaltReason;
using astra::HealthCheck;
using astra::PluginLoader;
using astra::Simulation;
using astra::StateVector;
using astra::Vehicle;

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

// A minimal physical section (flat earth, constant gravity, constant mass) so Vehicle::integrate
// has a real RigidBody6DofDynamics and calls wrench_b() -- WrenchSource::wrench_b is only ever
// called when rigid_body_.has_value() (core/src/kernel/vehicle.cpp), unlike bind_inputs()/latch(),
// which run unconditionally.
constexpr std::string_view kPhysical = R"(
physical:
  earth: plugins/physical/earth/flat
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
initial:
  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }
)";

// Codex adversarial review: applying vehicle.initial_controls must not depend on a Config's own
// read-state, which is shared (via Config's shared_ptr<Document>) across every copy of one Config
// object. Building two Simulations from the SAME Config must apply the literal both times, not just
// the first (an earlier draft used config.unknown_keys(), which would have silently skipped it the
// second time -- see apply_initial_controls's own corrected account, simulation.cpp).
TEST(SimulationInputSeam, ApplyingInitialControlsDoesNotDependOnTheConfigsOwnReadState) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer } ]
  initial_controls: { controls: { elevator: 0.3 } }
)",
                                            "reuse-fixture");
  const Simulation first = load(config);
  const double* first_canonical = first.registry().find("v0.controls.elevator");
  ASSERT_NE(first_canonical, nullptr);
  EXPECT_DOUBLE_EQ(*first_canonical, 0.3);

  const Simulation second = load(config);  // same Config object, built again
  const double* second_canonical = second.registry().find("v0.controls.elevator");
  ASSERT_NE(second_canonical, nullptr);
  EXPECT_DOUBLE_EQ(*second_canonical, 0.3)
      << "the second construction must apply the literal too, not read it as already-consumed";
}

// R2: v0.controls.* is visible in the vehicle's SignalRegistry after Vehicle::init(), from one
// inputs.add(registry, ...) call, matching a plugin's read-side name exactly.
TEST(SimulationInputSeam, WriteSeamInputIsVisibleReadOnlyUnderTheVehiclesOwnNamespace) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer } ]
  initial_controls: { controls: { elevator: 0.0 } }
)",
                                            "r2-fixture");
  const Simulation sim = load(config);
  EXPECT_NE(sim.registry().find("v0.controls.elevator"), nullptr);
}

// R3 + R5: a value written mid-frame, from inside wrench_b() itself (RK4 stage k2), is not observed
// until the NEXT frame -- and, within one frame, all four RK4 stages see the identical value.
TEST(SimulationInputSeam,
     AWriteFromInsideWrenchBIsDeferredToTheNextFrameAndAllFourStagesAgreeWithin) {
  const Config config = Config::from_string(std::string(R"(
sim: { dt: 0.001, frames: 2 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer, write_new_value: 0.5 } ]
  initial_controls: { controls: { elevator: 0.1 } }
)") + std::string(kPhysical),
                                            "r3-r5-fixture");
  Simulation sim = load(config);

  ASSERT_FALSE(sim.step());  // frame 0
  const double* k1 = sim.registry().find("v0.input_consumer.k1");
  const double* k2 = sim.registry().find("v0.input_consumer.k2");
  const double* k3 = sim.registry().find("v0.input_consumer.k3");
  const double* k4 = sim.registry().find("v0.input_consumer.k4");
  ASSERT_NE(k1, nullptr);
  ASSERT_NE(k2, nullptr);
  ASSERT_NE(k3, nullptr);
  ASSERT_NE(k4, nullptr);
  EXPECT_DOUBLE_EQ(*k1, 0.1);
  EXPECT_DOUBLE_EQ(*k2, 0.1);
  EXPECT_DOUBLE_EQ(*k3, 0.1) << "k2's own mid-frame write must not be observed by k3 this frame";
  EXPECT_DOUBLE_EQ(*k4, 0.1) << "nor by k4 this same frame";

  ASSERT_FALSE(sim.step());  // frame 1: latch() at frame-start commits what k2 staged last frame
  EXPECT_DOUBLE_EQ(*k1, 0.5);
  EXPECT_DOUBLE_EQ(*k2, 0.5);
  EXPECT_DOUBLE_EQ(*k3, 0.5);
  EXPECT_DOUBLE_EQ(*k4, 0.5) << "all four stages of the SAME frame see the identical value (R5)";
}

// R4: a value written from within a model's own step() (simulating a future M4 ActuatorModel) is
// observed by THIS SAME frame's wrench_b() calls, not delayed a frame.
TEST(SimulationInputSeam, AWriteFromStepIsObservedByThisSameFramesWrenchBCalls) {
  const Config config = Config::from_string(std::string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer, write_new_value: 0.7, write_from_step: true } ]
  initial_controls: { controls: { elevator: 0.1 } }
)") + std::string(kPhysical),
                                            "r4-fixture");
  Simulation sim = load(config);

  ASSERT_FALSE(sim.step());
  const double* k1 = sim.registry().find("v0.input_consumer.k1");
  const double* k4 = sim.registry().find("v0.input_consumer.k4");
  ASSERT_NE(k1, nullptr);
  ASSERT_NE(k4, nullptr);
  EXPECT_DOUBLE_EQ(*k1, 0.7) << "no one-frame lag for a step()-time writer";
  EXPECT_DOUBLE_EQ(*k4, 0.7);
}

// R7: a registered-but-never-written, or non-finite, input fails at init, naming the input's dotted
// config path.
TEST(SimulationInputSeam, ARegisteredButNeverWrittenInputFailsAtInitNamingItsConfigPath) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer } ]
)",
                                            "r7-unwritten-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "vehicle.initial_controls.controls.elevator");
  }
}

TEST(SimulationInputSeam, ANonFiniteConfigLiteralFailsAtInitNamingItsConfigPath) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer } ]
  initial_controls: { controls: { elevator: .nan } }
)",
                                            "r7-nonfinite-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "vehicle.initial_controls.controls.elevator");
  }
}

// R9: InputRegistry::reset() restores every entry to its config-literal init value, not to whatever
// was last committed.
TEST(SimulationInputSeam, VehicleResetRestoresTheConfigLiteralInitValueNotTheLastCommittedOne) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle:
  models: [ { plugin: tests/plugins/input_consumer } ]
  initial_controls: { controls: { elevator: 0.0 } }
)",
                                            "r9-fixture");
  Simulation sim = load(config);
  Vehicle& vehicle = sim.vehicles()[0];
  const double* canonical = sim.registry().find("v0.controls.elevator");
  ASSERT_NE(canonical, nullptr);
  ASSERT_DOUBLE_EQ(*canonical, 0.0);

  vehicle.inputs().write("controls.elevator", 5.0);
  ASSERT_TRUE(vehicle.inputs().latch().empty());
  ASSERT_DOUBLE_EQ(*canonical, 5.0);

  vehicle.reset();
  EXPECT_DOUBLE_EQ(*canonical, 0.0);
}

// R10: two vehicles (Simulation::for_testing) get independent InputRegistry instances; writing one
// does not affect the other. A companion case proves a forgotten write() fails loudly at that
// vehicle's own first latch(), naming the input via health.input.
TEST(SimulationInputSeam, TwoVehiclesGetIndependentInputRegistryInstances) {
  const PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  const std::array<StateVector, 2> initial_states{StateVector::Zero(), StateVector::Zero()};
  const std::vector<Vehicle::ModelSpec> models{Vehicle::ModelSpec{
      "tests/plugins/input_consumer", Config::from_string("{}\n", "input-consumer-fixture"), 1}};
  Simulation sim = Simulation::for_testing(loader, initial_states, models, nullptr, nullptr);

  sim.vehicles()[0].inputs().write("controls.elevator", 1.0);
  sim.vehicles()[1].inputs().write("controls.elevator", 2.0);
  ASSERT_FALSE(sim.step());

  const double* v0 = sim.registry().find("v0.controls.elevator");
  const double* v1 = sim.registry().find("v1.controls.elevator");
  ASSERT_NE(v0, nullptr);
  ASSERT_NE(v1, nullptr);
  EXPECT_DOUBLE_EQ(*v0, 1.0);
  EXPECT_DOUBLE_EQ(*v1, 2.0);
}

TEST(SimulationInputSeam, AForgottenWriteFailsLoudlyAtThatVehiclesFirstLatchNamingTheInput) {
  const PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  const std::array<StateVector, 2> initial_states{StateVector::Zero(), StateVector::Zero()};
  const std::vector<Vehicle::ModelSpec> models{Vehicle::ModelSpec{
      "tests/plugins/input_consumer", Config::from_string("{}\n", "input-consumer-fixture"), 1}};
  Simulation sim = Simulation::for_testing(loader, initial_states, models, nullptr, nullptr);

  sim.vehicles()[0].inputs().write("controls.elevator", 1.0);
  // vehicle 1's input is left unwritten -- stays at its NaN default (for_testing never calls
  // freeze()).
  const HaltReason halt = sim.step();
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteInput);
  EXPECT_EQ(halt.source, "controls.elevator");
  EXPECT_EQ(halt.vehicle_index, 1U);
}

// R11: two instances of the same WrenchSource plugin under one vehicle, registering the same name,
// do not collide; both read the same shared value, and a single write() is observed by both.
TEST(SimulationInputSeam, TwoInstancesOfOnePluginRegisteringTheSameNameShareOneValue) {
  const Config config = Config::from_string(std::string(R"(
sim: { dt: 0.001, frames: 2 }
vehicle:
  models:
    - { plugin: tests/plugins/input_consumer, id: a }
    - { plugin: tests/plugins/input_consumer, id: b }
  initial_controls: { controls: { elevator: 0.4 } }
)") + std::string(kPhysical),
                                            "r11-fixture");
  Simulation sim = load(config);  // must not throw a duplicate-name Error

  ASSERT_FALSE(sim.step());  // frame 0
  const double* a_k1 = sim.registry().find("v0.input_consumer.a.k1");
  const double* b_k1 = sim.registry().find("v0.input_consumer.b.k1");
  ASSERT_NE(a_k1, nullptr);
  ASSERT_NE(b_k1, nullptr);
  EXPECT_DOUBLE_EQ(*a_k1, 0.4);
  EXPECT_DOUBLE_EQ(*b_k1, 0.4);

  Vehicle& vehicle = sim.vehicles()[0];
  vehicle.inputs().write("controls.elevator", 0.9);
  ASSERT_FALSE(sim.step());  // frame 1: the one write is observed by both instances
  EXPECT_DOUBLE_EQ(*a_k1, 0.9);
  EXPECT_DOUBLE_EQ(*b_k1, 0.9);
}

}  // namespace
