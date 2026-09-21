// core/kernel/integrator.hpp: RK4 vs Euler convergence order (ADR-0004), the write-into-buffer
// functor contract (ADR-0021), and that step() itself never touches q_nb's norm (post-step
// renormalization is a separate stage, owned by RigidBody6DofDynamics since this ADR).
#include "astra/kernel/integrator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace {

// ODE for the convergence-order test: p_dot = v, v_dot = -k p, carried in the first component of
// the state's position and velocity slots; q stays identity and omega stays zero throughout (their
// derivative components are always written zero), so renormalizing would be a no-op and cannot
// pollute the order measurement. Closed form with p(0) = 1, v(0) = 0, k = 1: p(t) = cos(t), v(t) =
// -sin(t).
constexpr double kOscillatorK = 1.0;

// ADR-0021: dy aliases Integrator's own scratch, sized once and never re-zeroed between calls -- a
// conforming derivative functor writes every element every call, not only the ones it cares about
// (see Model::integrate_state's own doc comment for the full reasoning). std::fill_n first, then
// the two components that actually vary, is the simplest way to honor that here.
void oscillator_derivative(double /*t*/, const double* y, double* dy) noexcept {
  std::fill_n(dy, astra::kStateSize, 0.0);
  dy[astra::state::kPosition] = y[astra::state::kVelocity];
  dy[astra::state::kVelocity] = -kOscillatorK * y[astra::state::kPosition];
}

double integrate_oscillator_error(astra::IntegratorKind kind, double dt, double duration) {
  const astra::Integrator integrator(astra::kStateSize, kind, dt);
  astra::StateVector y = astra::StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;
  y(astra::state::kPosition) = 1.0;
  const auto steps = static_cast<std::size_t>(std::llround(duration / dt));
  for (std::size_t i = 0; i < steps; ++i) {
    const double t = static_cast<double>(i) * dt;
    integrator.step(oscillator_derivative, t, y.data());
  }
  const double p_err = y(astra::state::kPosition) - std::cos(duration);
  const double v_err = y(astra::state::kVelocity) - (-std::sin(duration));
  return std::hypot(p_err, v_err);
}

// Constant body rate omega_b = (0, 0, 1) rad/s from the identity attitude: the closed form for a
// constant body rate is q(t) = q(0) (x) (cos(|omega|t/2), (omega/|omega|) sin(|omega|t/2)), which
// here (q(0) = identity) is the pure yaw q(t) = (cos(t/2), 0, 0, sin(t/2)).
void rotation_derivative(double /*t*/, const double* y, double* dy) noexcept {
  std::fill_n(dy, astra::kStateSize, 0.0);
  const astra::frames::Quaternion q_nb =
      Eigen::Map<const astra::StateVector>(y).segment<astra::state::kQuaternionSize>(
          astra::state::kQuaternion);
  const Eigen::Vector3d omega_b =
      Eigen::Map<const astra::StateVector>(y).segment<astra::state::kAngularRateSize>(
          astra::state::kAngularRate);
  Eigen::Map<astra::StateVector>(dy).segment<astra::state::kQuaternionSize>(
      astra::state::kQuaternion) = astra::frames::quaternion_derivative(q_nb, omega_b);
}

TEST(Integrator, DefaultsMatchAdr0004) {
  const astra::Integrator integrator(astra::kStateSize);
  EXPECT_EQ(integrator.kind(), astra::IntegratorKind::kRk4);
  EXPECT_DOUBLE_EQ(integrator.dt(), astra::kDefaultDt);
  EXPECT_DOUBLE_EQ(astra::kDefaultDt, 1e-3);
  EXPECT_EQ(integrator.state_count(), static_cast<std::size_t>(astra::kStateSize));
}

TEST(Integrator, ConstructorRejectsNonPositiveDt) {
  EXPECT_THROW((astra::Integrator{astra::kStateSize, astra::IntegratorKind::kRk4, 0.0}),
               astra::Error);
  EXPECT_THROW((astra::Integrator{astra::kStateSize, astra::IntegratorKind::kRk4, -1e-3}),
               astra::Error);
}

TEST(Integrator, ConstructorRejectsNonFiniteDt) {
  EXPECT_THROW((astra::Integrator{astra::kStateSize, astra::IntegratorKind::kRk4,
                                  std::numeric_limits<double>::infinity()}),
               astra::Error);
  EXPECT_THROW((astra::Integrator{astra::kStateSize, astra::IntegratorKind::kRk4,
                                  std::numeric_limits<double>::quiet_NaN()}),
               astra::Error);
}

TEST(Integrator, ConstructorRejectsUnknownIntegratorKind) {
  // Deliberately out of range: this is the bad-config-value case is_valid_kind exists to catch.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  const auto bad_kind = static_cast<astra::IntegratorKind>(99);
  EXPECT_THROW((astra::Integrator{astra::kStateSize, bad_kind, astra::kDefaultDt}), astra::Error);
}

TEST(Integrator, Rk4ConvergesAtFourthOrderOnALinearOscillator) {
  constexpr double kDuration = 1.0;
  const double coarse = integrate_oscillator_error(astra::IntegratorKind::kRk4, 0.05, kDuration);
  const double fine = integrate_oscillator_error(astra::IntegratorKind::kRk4, 0.025, kDuration);
  ASSERT_GT(fine, 0.0);
  EXPECT_NEAR(coarse / fine, 16.0, 0.5);
}

TEST(Integrator, EulerConvergesAtFirstOrderOnALinearOscillator) {
  constexpr double kDuration = 1.0;
  const double coarse = integrate_oscillator_error(astra::IntegratorKind::kEuler, 0.05, kDuration);
  const double fine = integrate_oscillator_error(astra::IntegratorKind::kEuler, 0.025, kDuration);
  ASSERT_GT(fine, 0.0);
  EXPECT_NEAR(coarse / fine, 2.0, 0.1);
}

TEST(Integrator, StepDoesNotRenormalizeQuaternion) {
  const astra::Integrator integrator(astra::kStateSize, astra::IntegratorKind::kRk4,
                                     astra::kDefaultDt);
  astra::StateVector y = astra::StateVector::Zero();
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) =
      astra::frames::Quaternion{1.1, 0.0, 0.0, 0.0};
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) =
      Eigen::Vector3d{0.0, 0.0, 1.0};

  integrator.step(rotation_derivative, 0.0, y.data());
  const double norm_after_step =
      y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion).norm();
  EXPECT_GT(norm_after_step, 1.05);

  EXPECT_TRUE(astra::frames::normalize_quaternion(
      y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion)));
  EXPECT_NEAR(y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion).norm(), 1.0,
              1e-12);
}

TEST(Integrator, ConstantRateRotationStaysNormalizedAndMatchesClosedForm) {
  const astra::Integrator integrator(astra::kStateSize, astra::IntegratorKind::kRk4,
                                     astra::kDefaultDt);
  astra::StateVector y = astra::StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) =
      Eigen::Vector3d{0.0, 0.0, 1.0};

  constexpr int kSteps = 1000;
  for (int i = 0; i < kSteps; ++i) {
    const double t = static_cast<double>(i) * astra::kDefaultDt;
    integrator.step(rotation_derivative, t, y.data());
    ASSERT_TRUE(astra::frames::normalize_quaternion(
        y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion)))
        << "frame " << i;
    const double norm = y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion).norm();
    ASSERT_NEAR(norm, 1.0, 1e-12) << "frame " << i;
  }

  const double duration = static_cast<double>(kSteps) * astra::kDefaultDt;
  const double half_angle = duration / 2.0;
  const astra::frames::Quaternion expected{std::cos(half_angle), 0.0, 0.0, std::sin(half_angle)};
  const astra::frames::Quaternion actual =
      y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(expected(i), actual(i), 1e-9);
  }
}

}  // namespace
