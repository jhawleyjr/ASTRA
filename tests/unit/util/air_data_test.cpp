// air_data.hpp (docs/architecture.md §7, ADR-0020): the atan2-based aerodynamic-angle forms, wind
// subtraction, and dynamic-pressure/Mach formulas -- relocated verbatim from ADR-0018's own
// AeroModel::wrench_b() (pre-ADR-0020), pinned here directly against the free functions rather than
// through a stub AeroModel/wrench_b() call, since core no longer assembles these on a plugin's
// behalf at all.
#include "astra/util/air_data.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// ADR-0032's accessor (M7 PR 3b, tests C1 to C3).
std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

StateVector make_rate_state(const Eigen::Vector3d& omega_b, const astra::frames::Quaternion& q_nb) {
  StateVector y = make_state(Eigen::Vector3d{100.0, 0.0, 0.0}, q_nb);
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

// C1: omega_eb_b is the state's rate minus the Earth's, component by component, bit for bit. Both
// are already in body axes, so nothing is rotated: a non-identity attitude gives the same bits.
TEST(AirData, EarthRelativeBodyRateIsTheStateRateMinusTheEarthRateInBodyAxes) {
  const Eigen::Vector3d omega_b{0.3, -0.2, 0.1};
  EnvironmentSample env;
  env.omega_ie_b = Eigen::Vector3d{1e-4, 2e-4, -3e-4};
  const std::array<double, 3> expected{0.3 - 1e-4, -0.2 - 2e-4, 0.1 - -3e-4};

  const astra::frames::Quaternion rotated = astra::frames::quaternion_from_euler({2.2, 0.3, -0.4});
  for (const astra::frames::Quaternion& q_nb : {kIdentity, rotated}) {
    const StateVector y = make_rate_state(omega_b, q_nb);
    const Eigen::Vector3d omega_eb_b = astra::air_data::omega_eb_b(VehicleState{y}, env);
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(bits(omega_eb_b(i)), bits(expected.at(static_cast<std::size_t>(i))))
          << "component " << i;
    }
  }
}

// C2: the IEEE behavior ADR-0032's +0.0 canonicalization exists for (environment_sample.hpp). A
// -0.0 rate minus a +0.0 Earth rate stays -0.0; minus a -0.0 Earth rate it becomes +0.0.
TEST(AirData, EarthRelativeBodyRateKeepsANegativeZeroRateAgainstAPositiveZeroEarthRate) {
  const StateVector y = make_rate_state(Eigen::Vector3d::Constant(-0.0), kIdentity);
  const VehicleState vs{y};
  constexpr std::uint64_t kNegativeZero = 0x8000000000000000ULL;
  constexpr std::uint64_t kPositiveZero = 0x0000000000000000ULL;

  EnvironmentSample positive_zero;
  positive_zero.omega_ie_b = Eigen::Vector3d::Constant(0.0);
  const Eigen::Vector3d kept = astra::air_data::omega_eb_b(vs, positive_zero);
  EnvironmentSample negative_zero;
  negative_zero.omega_ie_b = Eigen::Vector3d::Constant(-0.0);
  const Eigen::Vector3d flipped = astra::air_data::omega_eb_b(vs, negative_zero);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(bits(kept(i)), kNegativeZero) << "component " << i;
    EXPECT_EQ(bits(flipped(i)), kPositiveZero) << "component " << i;
  }
}

// C3: a sample whose omega_ie_b was never set (its NaN default) gives a NaN rate, never omega_b.
TEST(AirData, EarthRelativeBodyRateIsNaNWhenTheEarthRateWasNeverSet) {
  const StateVector y = make_rate_state(Eigen::Vector3d{0.3, -0.2, 0.1}, kIdentity);
  const EnvironmentSample env;
  EXPECT_TRUE(astra::air_data::omega_eb_b(VehicleState{y}, env).array().isNaN().all());
}

}  // namespace
