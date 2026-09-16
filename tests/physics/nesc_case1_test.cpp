// NASA/TM-2015-218675 Volume II Table 25, atmospheric check-case 1 ("dragless sphere"), the PLAN.md
// M2 PR 1 acceptance's "WGS-84 rotating sphere drop from the NESC set": see
// tests/golden/nesc/case1_dragless_sphere.hpp for the scenario and why its reference final state is
// an independent integration rather than a number the report itself publishes.
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "golden/nesc/case1_dragless_sphere.hpp"

namespace {

using astra::test::nesc::kAltitudeTolerance;
using astra::test::nesc::kAngleTolerance;
using astra::test::nesc::kExpectedFinalState;
using astra::test::nesc::kOmegaTolerance;
using astra::test::nesc::kQuaternionTolerance;
using astra::test::nesc::kVelocityTolerance;

TEST(Nesc, Case1DraglessSphereMatchesTheIndependentReference) {
  const astra::Config config = astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} +
                                                        "/nesc_case1_dragless_sphere.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

  ASSERT_FALSE(sim.run());
  const astra::StateVector& y = sim.vehicles().front().state();

  EXPECT_NEAR(kExpectedFinalState.latitude, y(astra::state::kPosition), kAngleTolerance);
  EXPECT_NEAR(kExpectedFinalState.longitude, y(astra::state::kPosition + 1), kAngleTolerance);
  EXPECT_NEAR(kExpectedFinalState.altitude, y(astra::state::kPosition + 2), kAltitudeTolerance);

  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(std::string{"v_b component "} + std::to_string(i));
    EXPECT_NEAR(kExpectedFinalState.v_b[i], y(astra::state::kVelocity + i), kVelocityTolerance);
  }
  for (int i = 0; i < 4; ++i) {
    SCOPED_TRACE(std::string{"q_nb component "} + std::to_string(i));
    EXPECT_NEAR(kExpectedFinalState.q_nb[i], y(astra::state::kQuaternion + i),
                kQuaternionTolerance);
  }
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(std::string{"omega_b component "} + std::to_string(i));
    EXPECT_NEAR(kExpectedFinalState.omega_b[i], y(astra::state::kAngularRate + i), kOmegaTolerance);
  }
}

}  // namespace
