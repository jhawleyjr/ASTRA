// The F-16's per-frame path allocates nothing (docs/architecture.md §14/§15; M7 PR 6b and PR 6c,
// docs/plans/m7-pr6b-f16-aircraft.md and docs/plans/m7-pr6c-f16-mass-propulsion.md). This
// executable links tests/support/alloc_counter.cpp, which counts every global operator new, so each
// case brackets only the calls under test.
//
// WrenchPathAllocatesNothing runs each plugin's wrench_b() over a sweep:
// - the aero's reaches every table edge, the unclipped linear terms, the 0.1 ft/s floor and V = 0;
// - the engine's reaches both edges of both table axes, Mach 0 and past Mach 1, and throttles
//   outside 0 to 1.
// OneThousandFramesAllocateNothing runs all three F-16 plugins in a Simulation.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

namespace units = astra::units;

// State buffers; each VehicleState is built at the call as a view over one (ADR-0021).
std::vector<astra::StateVector> sweep() {
  std::vector<astra::StateVector> states;
  for (const double vt_ft : {0.0, 0.05, 0.2, 300.0, 900.0}) {
    for (const double alpha_deg : {-30.0, -10.0, 5.0, 45.0, 70.0}) {
      for (const double beta_deg : {-45.0, -30.0, -0.0, 2.0, 30.0, 45.0}) {
        const double vt = units::feet_to_meters(vt_ft);
        const double alpha = units::degrees_to_radians(alpha_deg);
        const double beta = units::degrees_to_radians(beta_deg);
        astra::StateVector y = astra::StateVector::Zero();
        y.segment<3>(astra::state::kVelocity) =
            vt * Eigen::Vector3d(std::cos(alpha) * std::cos(beta), std::sin(beta),
                                 std::sin(alpha) * std::cos(beta));
        y(astra::state::kQuaternion) = 1.0;
        y.segment<3>(astra::state::kAngularRate) = Eigen::Vector3d(0.4, -0.3, 0.2);
        states.push_back(y);
      }
    }
  }
  return states;
}

TEST(F16NoAlloc, WrenchPathAllocatesNothing) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded_aero = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  astra::LoadedPlugin loaded_engine =
      loader.load(ASTRA_THIS_AIRCRAFT "/hardware/propulsion/engine");
  auto* const aero = dynamic_cast<astra::AeroModel*>(loaded_aero.model.get());
  auto* const engine = dynamic_cast<astra::PropulsionModel*>(loaded_engine.model.get());
  ASSERT_NE(aero, nullptr);
  ASSERT_NE(engine, nullptr);
  astra::SignalRegistry registry;
  aero->init(astra::Config::from_string("{}", "f16-no-alloc"), registry);
  engine->init(astra::Config::from_string("{}", "f16-no-alloc"), registry);
  astra::InputRegistry inputs;
  aero->bind_inputs(inputs, registry);
  engine->bind_inputs(inputs, registry);

  const std::vector<astra::StateVector> states = sweep();
  astra::EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 1.225;
  env.omega_ie_b = Eigen::Vector3d(1e-5, -2e-5, 3e-5);

  std::size_t allocations = 0;
  std::size_t calls = 0;
  double checksum = 0.0;  // keeps every call observable

  // The aero, over the controls.
  inputs.write("controls.throttle", 0.5);
  for (const double control_deg : {-40.0, -24.0, 0.0, 12.5, 24.0, 45.0}) {
    const double control = units::degrees_to_radians(control_deg);
    inputs.write("controls.elevator", control);
    inputs.write("controls.aileron", -control);
    inputs.write("controls.rudder", control);
    ASSERT_TRUE(inputs.latch().empty());

    const std::size_t before = astra_test::allocation_count();
    for (const astra::StateVector& y : states) {
      const astra::Wrench w = aero->wrench_b(astra::VehicleState{y}, env);
      checksum += w.force_b.sum() + w.moment_b.sum();
      ++calls;
    }
    allocations += astra_test::allocation_count() - before;
  }

  // The engine, over throttles and altitudes past both ends. A speed of sound of 250 m/s puts the
  // sweep's 900 ft/s (274 m/s) at Mach 1.1, past the tables' Mach edge.
  env.atmosphere.speed_of_sound_m_s = 250.0;
  for (const double throttle : {-0.1, 0.0, 0.5, 1.0, 1.2}) {
    inputs.write("controls.throttle", throttle);
    ASSERT_TRUE(inputs.latch().empty());
    for (const double altitude_ft : {-1000.0, 0.0, 23507.0, 50000.0, 60000.0}) {
      env.altitude_m = units::feet_to_meters(altitude_ft);

      const std::size_t before = astra_test::allocation_count();
      for (const astra::StateVector& y : states) {
        const astra::Wrench w = engine->wrench_b(astra::VehicleState{y}, env);
        checksum += w.force_b.sum() + w.moment_b.sum();
        ++calls;
      }
      allocations += astra_test::allocation_count() - before;
    }
  }
  EXPECT_EQ(allocations, 0U) << "over " << calls << " wrench_b calls";
  EXPECT_TRUE(std::isfinite(checksum));
}

// The tests/physics/no_alloc_test.cpp pattern: zero allocations, and the run must complete all
// 1000 frames with no halt, since a run that halted early would pass the allocation count having
// measured fewer frames. Untrimmed, but at 3,000 m and 150 m/s the F-16 stays inside every domain
// for 1 s. Every write-seam input needs a value before the run, so initial_controls sets all four.
TEST(F16NoAlloc, OneThousandFramesAllocateNothing) {
  const std::string aircraft = ASTRA_THIS_AIRCRAFT;
  std::string yaml = "sim: { dt: 0.001, frames: 1000 }\n";
  yaml += "vehicle:\n";
  yaml += "  models:\n";
  yaml += "    - { plugin: " + aircraft + "/physical/aero }\n";
  yaml += "    - { plugin: " + aircraft + "/hardware/propulsion/engine }\n";
  yaml += "  initial_controls:\n";
  yaml += "    controls: { elevator: 0.0, aileron: 0.0, rudder: 0.0, throttle: 0.6 }\n";
  yaml += "physical:\n";
  yaml += "  earth: plugins/physical/earth/flat\n";
  yaml += "  gravity: plugins/physical/gravity/constant\n";
  yaml += "  atmosphere: plugins/physical/atmosphere/isa1976\n";
  yaml += "  mass: { plugin: " + aircraft + "/physical/mass }\n";
  yaml += "initial:\n";
  yaml += "  state: { p_n: [0.0, 0.0, -3000.0], v_b: [150.0, 0.0, 0.0] }\n";
  const astra::Config config = astra::Config::from_string(yaml, "f16-no-alloc-one-thousand-frames");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame << " (" << halt.source << ")";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with the F-16's mass, aero and "
                              "engine (docs/architecture.md §14/§15)";
}

}  // namespace
