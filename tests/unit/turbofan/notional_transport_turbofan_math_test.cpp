// aircraft/notional_transport/hardware/propulsion/turbofan/turbofan_math.hpp (docs/plans/
// pr4-notional-aero-turbofan.md, R4): compute_wrench() called directly with hand-built Inputs/
// Coefficients literals -- no plugin instance, no Config, no Simulation. Conditions N and O and the
// symmetric-yaw check are computed once in the plan itself (an independent re-implementation, not
// this plugin's own code) -- see the plan's own "Worked numbers" section. No V=0 case here: the
// thrust law has no V dependence at all, so a pure-math V=0 case would be vacuous (checked instead
// at the wrench_b() level, R5).
#include <gtest/gtest.h>

#include <cmath>

#include "aircraft/notional_transport/hardware/propulsion/turbofan/turbofan_math.hpp"

namespace {

using astra::thrust_lapse::Coefficients;
using astra::thrust_lapse::compute_wrench;
using astra::thrust_lapse::Inputs;

// docs/plans/pr4-notional-aero-turbofan.md's own "Worked numbers": thrust_ref_n derived from
// Condition N's own drag (aero_math_test.cpp's ConditionNNominalWingsLevel), so throttle=0.5 at
// rho=density_ref_kgm3 reproduces that drag exactly.
constexpr double kThrustRefN = 29878.121867743088;
constexpr double kDensityRefKgm3 = 0.4128;

Coefficients left_engine_coefficients() {
  Coefficients c;
  c.r_engine_b = Eigen::Vector3d(2.5146, -4.9022, 0.0);
  c.thrust_ref_n = kThrustRefN;
  c.density_ref_kgm3 = kDensityRefKgm3;
  return c;
}

Coefficients right_engine_coefficients() {
  Coefficients c;
  c.r_engine_b = Eigen::Vector3d(2.5146, 4.9022, 0.0);
  c.thrust_ref_n = kThrustRefN;
  c.density_ref_kgm3 = kDensityRefKgm3;
  return c;
}

void expect_matches(double actual, double expected, const char* what) {
  if (expected == 0.0) {
    EXPECT_EQ(actual, 0.0) << what;
  } else {
    EXPECT_NEAR(actual, expected, std::abs(expected) * 1e-9) << what;
  }
}

TEST(NotionalTransportTurbofanMath, ConditionNNominalHalfThrottle) {
  Inputs in;
  in.throttle = 0.5;
  in.density_kg_m3 = 0.4128;

  const astra::Wrench wrench = compute_wrench(in, left_engine_coefficients());
  expect_matches(wrench.force_b(0), 14939.0609338715, "force_b.x");
  expect_matches(wrench.force_b(1), 0.0, "force_b.y");
  expect_matches(wrench.force_b(2), 0.0, "force_b.z");
  expect_matches(wrench.moment_b(0), 0.0, "moment_b.x");
  expect_matches(wrench.moment_b(1), 0.0, "moment_b.y");
  expect_matches(wrench.moment_b(2), 73234.2645100251, "moment_b.z");
}

TEST(NotionalTransportTurbofanMath, ConditionOOffNominalThrottleAndDensity) {
  Inputs in;
  in.throttle = 0.8;
  in.density_kg_m3 = 0.5;

  const astra::Wrench wrench = compute_wrench(in, left_engine_coefficients());
  expect_matches(wrench.force_b(0), 28951.6684764952, "force_b.x");
}

TEST(NotionalTransportTurbofanMath, SymmetricEqualThrottleNetYawMomentIsExactlyZero) {
  Inputs in;
  in.throttle = 0.5;
  in.density_kg_m3 = 0.4128;

  const astra::Wrench left = compute_wrench(in, left_engine_coefficients());
  const astra::Wrench right = compute_wrench(in, right_engine_coefficients());
  const Eigen::Vector3d moment_sum = left.moment_b + right.moment_b;

  // Open item 5: exact equality, not 1e-9 relative -- the x/y components are each individually a
  // zero-times-finite product (r_z=0 for both engines), and the z component is T' + (-T') for one
  // identical floating-point value T' computed the same way on both sides (r_engine_b's own y
  // components are exact negatives of each other as literals): x - x == 0.0 exactly in IEEE-754.
  EXPECT_EQ(moment_sum(0), 0.0);
  EXPECT_EQ(moment_sum(1), 0.0);
  EXPECT_EQ(moment_sum(2), 0.0);
}

}  // namespace
