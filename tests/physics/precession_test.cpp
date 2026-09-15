// M1 acceptance (docs/architecture.md §16, PLAN.md M1 PR 3): symmetric-top precession vs closed
// form. inertia_b = diag(it, it, ia), g = 0, no applied wrench. Euler's equations reduce to
// r_dot = 0 (the spin about the symmetry axis is constant) and p_dot = lambda*q, q_dot = -lambda*p
// with lambda = (it - ia) / it * r0, whose solution from (p0, q0) is p(t) = p0*cos(lambda t) +
// q0*sin(lambda t), q(t) = q0*cos(lambda t) - p0*sin(lambda t). The plan's first draft of this test
// had the wrong sign on the sin terms, which a Codex plan-review round caught by deriving the
// solution independently before any code existed -- worth re-deriving once more here, not trusted
// blind.
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"

namespace {

// Must match tests/configs/physics_precession.yaml's physical.mass and initial.state.omega_b.
constexpr double kIt = 2.0;
constexpr double kIa = 1.0;
constexpr double kP0 = 0.3;
constexpr double kR0 = 2.0;
constexpr double kLambda = (kIt - kIa) / kIt * kR0;

constexpr double kAbsoluteTolerance = 1e-6;

TEST(Precession, TransverseRatesMatchClosedFormAndSpinStaysConstant) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/physics_precession.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

  ASSERT_FALSE(sim.run());
  const double duration = static_cast<double>(sim.frames()) * sim.clock().dt();
  const astra::StateVector& y = sim.vehicles().front().state();
  const Eigen::Vector3d omega_b =
      y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate);

  const double expected_p = kP0 * std::cos(kLambda * duration);
  const double expected_q = -kP0 * std::sin(kLambda * duration);
  EXPECT_NEAR(expected_p, omega_b(0), kAbsoluteTolerance);
  EXPECT_NEAR(expected_q, omega_b(1), kAbsoluteTolerance);
  EXPECT_NEAR(kR0, omega_b(2), kAbsoluteTolerance) << "spin about the symmetry axis is conserved";

  // The transverse magnitude is exactly conserved regardless of lambda's sign, so it alone can't
  // catch the sign error the plan-review round found; kept as a second, independent check.
  const double transverse = std::hypot(omega_b(0), omega_b(1));
  EXPECT_NEAR(kP0, transverse, kAbsoluteTolerance);
}

}  // namespace
