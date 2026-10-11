// The F-16's per-frame path allocates nothing (docs/architecture.md §14/§15; M7 PR 6b,
// docs/plans/m7-pr6b-f16-aircraft.md). This executable links tests/support/alloc_counter.cpp,
// which counts every global operator new, so each case brackets only the calls under test.
//
// The aero's wrench_b() runs over a sweep that reaches every table edge, the unclipped linear
// terms, the 0.1 ft/s floor and V = 0. The engine joins this case, and a 1000-frame Simulation case
// is added, in the propulsion commit.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
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
  astra::LoadedPlugin loaded = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  auto* const aero = dynamic_cast<astra::AeroModel*>(loaded.model.get());
  ASSERT_NE(aero, nullptr);
  astra::SignalRegistry registry;
  aero->init(astra::Config::from_string("{}", "f16-no-alloc"), registry);
  astra::InputRegistry inputs;
  aero->bind_inputs(inputs, registry);

  const std::vector<astra::StateVector> states = sweep();
  astra::EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 1.225;
  env.omega_ie_b = Eigen::Vector3d(1e-5, -2e-5, 3e-5);

  std::size_t allocations = 0;
  std::size_t calls = 0;
  double checksum = 0.0;  // keeps every call observable
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
  EXPECT_EQ(allocations, 0U) << "over " << calls << " wrench_b calls";
  EXPECT_TRUE(std::isfinite(checksum));
}

}  // namespace
