// air_data.hpp (docs/architecture.md §7, ADR-0020): the atan2-based aerodynamic-angle forms, wind
// subtraction, and dynamic-pressure/Mach formulas -- relocated verbatim from ADR-0018's own
// AeroModel::wrench_b() (pre-ADR-0020), pinned here directly against the free functions rather than
// through a stub AeroModel/wrench_b() call, since core no longer assembles these on a plugin's
// behalf at all.
#include "astra/util/air_data.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::EnvironmentSample;
using astra::StateVector;
using astra::VehicleState;

constexpr double kTolerance = 1e-12;

StateVector make_state(const Eigen::Vector3d& v_b, const astra::frames::Quaternion& q_nb) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) = q_nb;
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  return y;
}

const astra::frames::Quaternion kIdentity{1.0, 0.0, 0.0, 0.0};

// Still air, identity attitude: v_rel_b must equal v_b exactly, and every derived quantity must
// match the aerodynamic-angle formulas computed independently here, not copied from air_data.hpp.
TEST(AirData, StillAirRelativeVelocityAndDerivedQuantitiesMatchTheFormulas) {
  const Eigen::Vector3d v_b{100.0, 10.0, 5.0};
  const StateVector y = make_state(v_b, kIdentity);
  const VehicleState vs{y};
  EnvironmentSample env;  // wind_n zero by default -> v_rel_b == v_b

  const Eigen::Vector3d v_rel_b = astra::air_data::relative_velocity_b(vs, env);
  EXPECT_TRUE(v_rel_b.isApprox(v_b));

  const double u = v_b(0);
  const double v = v_b(1);
  const double w = v_b(2);
  const double expected_speed = std::sqrt(u * u + v * v + w * w);
  const double expected_alpha = std::atan2(w, u);
  const double expected_beta = std::atan2(v, std::hypot(u, w));

  EXPECT_NEAR(expected_speed, astra::air_data::true_airspeed(v_rel_b), kTolerance);
  EXPECT_NEAR(expected_alpha, astra::air_data::angle_of_attack(v_rel_b), kTolerance);
  EXPECT_NEAR(expected_beta, astra::air_data::sideslip(v_rel_b), kTolerance);

  const double rho = 1.225;
  const double expected_q = 0.5 * rho * expected_speed * expected_speed;
  EXPECT_NEAR(expected_q, astra::air_data::dynamic_pressure(rho, expected_speed), kTolerance);
  const double speed_of_sound = 340.0;
  EXPECT_NEAR(expected_speed / speed_of_sound,
              astra::air_data::mach(expected_speed, speed_of_sound), kTolerance);
}

// Wind subtraction happens in body axes -- proven with a non-identity attitude (90 deg yaw) so a
// bug that subtracted wind in the wrong frame (nav instead of body) would show up as a wrong angle,
// not just a wrong magnitude.
TEST(AirData, WindIsSubtractedInBodyAxesBeforeAnglesAndDynamicPressure) {
  const Eigen::Vector3d v_b{50.0, 0.0, 0.0};
  const astra::frames::Quaternion q_nb =
      astra::frames::quaternion_from_euler({M_PI_2, 0.0, 0.0});  // 90 deg yaw, level
  const StateVector y = make_state(v_b, q_nb);
  const VehicleState vs{y};
  EnvironmentSample env;
  env.wind_n = Eigen::Vector3d{10.0, 0.0, 0.0};  // wind blowing toward geographic north

  const Eigen::Vector3d v_rel_b = astra::air_data::relative_velocity_b(vs, env);
  const Eigen::Vector3d expected_v_rel_b = v_b - astra::frames::rotate_n_to_b(q_nb, env.wind_n);
  EXPECT_TRUE(v_rel_b.isApprox(expected_v_rel_b));

  const double u = expected_v_rel_b(0);
  const double v = expected_v_rel_b(1);
  const double w = expected_v_rel_b(2);
  EXPECT_NEAR(std::atan2(w, u), astra::air_data::angle_of_attack(v_rel_b), kTolerance);
  EXPECT_NEAR(std::atan2(v, std::hypot(u, w)), astra::air_data::sideslip(v_rel_b), kTolerance);
}

// true_airspeed_mps == 0 (still air, vehicle at rest relative to it) is a legitimate physical
// state, not a fault -- the atan2-based angle forms must be finite, and here (both components
// exactly +0.0) exactly zero.
TEST(AirData, ZeroRelativeVelocityGivesExactlyZeroAnglesAndDynamicPressure) {
  const Eigen::Vector3d v_rel_b = Eigen::Vector3d::Zero();
  EXPECT_EQ(0.0, astra::air_data::true_airspeed(v_rel_b));
  EXPECT_EQ(0.0, astra::air_data::angle_of_attack(v_rel_b));
  EXPECT_EQ(0.0, astra::air_data::sideslip(v_rel_b));
  EXPECT_EQ(0.0, astra::air_data::dynamic_pressure(1.225, astra::air_data::true_airspeed(v_rel_b)));
}

// A bit-exact negative-zero component gives IEEE-754 atan2 a different branch (atan2(+0,-0) ==
// pi), so alpha/beta are finite but not necessarily exactly 0 here -- a real, disclosed, cosmetic
// discontinuity, never a NaN (ADR-0018's own original finding, carried forward unchanged by the
// relocation). This test pins that it stays finite; it deliberately does not assert a particular
// angle value.
TEST(AirData, SignedZeroRelativeVelocityComponentStaysFiniteNotNecessarilyZero) {
  const Eigen::Vector3d v_rel_b{-0.0, 0.0, 0.0};
  EXPECT_TRUE(std::isfinite(astra::air_data::angle_of_attack(v_rel_b)));
  EXPECT_TRUE(std::isfinite(astra::air_data::sideslip(v_rel_b)));
}

TEST(AirData, AllFiniteDetectsANonFiniteValueAnywhereInTheList) {
  EXPECT_TRUE(astra::air_data::all_finite({1.0, 2.0, 3.0}));
  EXPECT_FALSE(astra::air_data::all_finite({1.0, std::numeric_limits<double>::quiet_NaN(), 3.0}));
  EXPECT_FALSE(astra::air_data::all_finite({1.0, std::numeric_limits<double>::infinity()}));
}

}  // namespace
