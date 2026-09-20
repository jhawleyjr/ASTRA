// AeroModel (docs/architecture.md §7, ADR-0018): wrench_b()'s own assembly of AeroInputs from
// (VehicleState, EnvironmentSample) -- wind subtraction, the atan2-based aerodynamic angles, the
// V->0 boundary, and the unconditional isfinite guard -- pinned against a stub compute() with no
// simulator involved (R1-R7, R9's no-alloc extension lives in tests/physics/no_alloc_test.cpp;
// R8/R8-guard live in tests/unit/kernel/, since they need a real Simulation).
#include "astra/interfaces/aero_model.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::AeroInputs;
using astra::AeroModel;
using astra::Config;
using astra::EnvironmentSample;
using astra::SignalRegistry;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::Wrench;

constexpr double kTolerance = 1e-12;

// A stub AeroModel that never reads its own inputs -- the hardest case for the isfinite guard
// (R5): if compute() ran on a non-finite input, this stub's own math would not surface it, since
// it does not use inputs.mach or any other field. Records the last AeroInputs it was called with
// so a test can check the assembly step in isolation from what compute() does with it (R2/R3/R4).
class StubAeroModel final : public AeroModel {
 public:
  void init(const Config& /*config*/, SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "stub_aero"; }

  [[nodiscard]] Wrench compute(const AeroInputs& inputs) const noexcept override {
    ++call_count;
    last_inputs = inputs;
    Wrench wrench;
    wrench.force_b(0) = 1.0;  // fixed, deliberately independent of every input field
    return wrench;
  }

  mutable std::uint32_t call_count = 0;
  mutable AeroInputs last_inputs{};
};

StateVector make_state(const Eigen::Vector3d& v_b, const Eigen::Vector3d& omega_b,
                       const astra::frames::Quaternion& q_nb) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) = q_nb;
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

const astra::frames::Quaternion kIdentity{1.0, 0.0, 0.0, 0.0};

// R2: still air (no wind, identity attitude) -- AeroInputs must match the aerodynamic-angle
// formulas computed independently here, not copied from aero_model.cpp.
TEST(AeroModel, StillAirAssemblesInputsMatchingTheAerodynamicAngleFormulas) {
  StubAeroModel model;
  const Eigen::Vector3d v_b{100.0, 10.0, 5.0};
  const Eigen::Vector3d omega_b{0.1, 0.2, 0.3};
  const StateVector y = make_state(v_b, omega_b, kIdentity);
  const VehicleState vs{y};
  EnvironmentSample env;  // wind_n zero by default -> v_rel_b == v_b
  env.atmosphere.density_kg_m3 = 1.225;
  env.atmosphere.speed_of_sound_m_s = 340.0;

  static_cast<void>(model.wrench_b(vs, env));
  ASSERT_EQ(model.call_count, 1U);

  const double u = v_b(0);
  const double v = v_b(1);
  const double w = v_b(2);
  const double expected_speed = std::sqrt(u * u + v * v + w * w);
  const double expected_alpha = std::atan2(w, u);
  const double expected_beta = std::atan2(v, std::hypot(u, w));
  const double expected_q = 0.5 * 1.225 * expected_speed * expected_speed;
  const double expected_mach = expected_speed / 340.0;

  EXPECT_NEAR(expected_speed, model.last_inputs.true_airspeed_mps, kTolerance);
  EXPECT_NEAR(expected_alpha, model.last_inputs.alpha_rad, kTolerance);
  EXPECT_NEAR(expected_beta, model.last_inputs.beta_rad, kTolerance);
  EXPECT_NEAR(expected_q, model.last_inputs.dynamic_pressure_pa, kTolerance);
  EXPECT_NEAR(expected_mach, model.last_inputs.mach, kTolerance);
  EXPECT_TRUE(model.last_inputs.omega_b.isApprox(omega_b))
      << "body rates pass straight through, inertial-referenced, no wind term";
}

// R3: wind subtraction happens once, in body axes, before alpha/beta/dynamic pressure -- proven
// with a non-identity attitude (90 deg yaw) so a bug that subtracted wind in the wrong frame (nav
// instead of body) would show up as a wrong angle, not just a wrong magnitude.
TEST(AeroModel, WindIsSubtractedInBodyAxesBeforeAnglesAndDynamicPressure) {
  StubAeroModel model;
  const Eigen::Vector3d v_b{50.0, 0.0, 0.0};
  const astra::frames::Quaternion q_nb =
      astra::frames::quaternion_from_euler({M_PI_2, 0.0, 0.0});  // 90 deg yaw, level
  const StateVector y = make_state(v_b, Eigen::Vector3d::Zero(), q_nb);
  const VehicleState vs{y};
  EnvironmentSample env;
  env.wind_n = Eigen::Vector3d{10.0, 0.0, 0.0};  // wind blowing toward geographic north
  env.atmosphere.density_kg_m3 = 1.0;
  env.atmosphere.speed_of_sound_m_s = 300.0;

  static_cast<void>(model.wrench_b(vs, env));
  ASSERT_EQ(model.call_count, 1U);

  // Independently reuses the already-tested frames::rotate_n_to_b utility to compute the expected
  // relative velocity -- the object under test is whether wrench_b() applies it once, in the right
  // place, not whether the utility itself is correct (that is frames_test.cpp's own job).
  const Eigen::Vector3d expected_v_rel_b = v_b - astra::frames::rotate_n_to_b(q_nb, env.wind_n);
  const double u = expected_v_rel_b(0);
  const double v = expected_v_rel_b(1);
  const double w = expected_v_rel_b(2);
  const double expected_speed = std::sqrt(u * u + v * v + w * w);

  EXPECT_NEAR(expected_speed, model.last_inputs.true_airspeed_mps, kTolerance);
  EXPECT_NEAR(std::atan2(w, u), model.last_inputs.alpha_rad, kTolerance);
  EXPECT_NEAR(std::atan2(v, std::hypot(u, w)), model.last_inputs.beta_rad, kTolerance);
  EXPECT_NEAR(0.5 * 1.0 * expected_speed * expected_speed, model.last_inputs.dynamic_pressure_pa,
              kTolerance);
}

// R4: true_airspeed_mps == 0 (still air, vehicle at rest relative to it) is a legitimate physical
// state, not a fault -- the atan2-based angle forms must be finite, and here (both components
// exactly +0.0) exactly zero.
TEST(AeroModel, ZeroRelativeVelocityGivesExactlyZeroAnglesAndDynamicPressure) {
  StubAeroModel model;
  const StateVector y = make_state(Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero(), kIdentity);
  const VehicleState vs{y};
  EnvironmentSample env;  // wind_n zero -> v_rel_b == (+0, +0, +0)
  env.atmosphere.density_kg_m3 = 1.225;
  env.atmosphere.speed_of_sound_m_s = 340.0;

  static_cast<void>(model.wrench_b(vs, env));
  ASSERT_EQ(model.call_count, 1U);
  EXPECT_EQ(0.0, model.last_inputs.true_airspeed_mps);
  EXPECT_EQ(0.0, model.last_inputs.alpha_rad);
  EXPECT_EQ(0.0, model.last_inputs.beta_rad);
  EXPECT_EQ(0.0, model.last_inputs.dynamic_pressure_pa);
  EXPECT_EQ(0.0, model.last_inputs.mach);
}

// R4's own signed-zero correction (Codex plan critique F4): a bit-exact negative-zero component
// gives IEEE-754 atan2 a different branch (atan2(+0,-0) == pi), so alpha/beta are finite but not
// necessarily exactly 0 here -- a real, disclosed, cosmetic discontinuity, never a NaN. This test
// pins that it stays finite; it deliberately does not assert a particular angle value.
TEST(AeroModel, SignedZeroRelativeVelocityComponentStaysFiniteNotNecessarilyZero) {
  StubAeroModel model;
  const StateVector y =
      make_state(Eigen::Vector3d{-0.0, 0.0, 0.0}, Eigen::Vector3d::Zero(), kIdentity);
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 1.225;
  env.atmosphere.speed_of_sound_m_s = 340.0;

  static_cast<void>(model.wrench_b(vs, env));
  ASSERT_EQ(model.call_count, 1U);
  EXPECT_TRUE(std::isfinite(model.last_inputs.alpha_rad));
  EXPECT_TRUE(std::isfinite(model.last_inputs.beta_rad));
}

// R5 (unconditional, Joe's own follow-up review): a non-finite atmosphere sample must halt before
// compute() ever runs, regardless of which fields compute() itself would have read -- the hard
// case, since StubAeroModel's own compute() (above) ignores every input field.
TEST(AeroModel, NonFiniteAtmosphereNeverCallsComputeAndReturnsAnAllNanWrench) {
  StubAeroModel model;
  const StateVector y = make_state({50.0, 0.0, 0.0}, Eigen::Vector3d::Zero(), kIdentity);
  const VehicleState vs{y};
  EnvironmentSample env;  // atmosphere defaults to all-NaN (AtmoState's own default)

  const Wrench wrench = model.wrench_b(vs, env);

  EXPECT_EQ(model.call_count, 0U) << "compute() must not run on a non-finite input, even one a "
                                     "plugin that ignores every field would have tolerated";
  EXPECT_FALSE(wrench.force_b.allFinite());
  EXPECT_FALSE(wrench.moment_b.allFinite());
  EXPECT_TRUE(std::isnan(wrench.force_b(0)));
}

// Codex review + adversarial review (both independently): an infinite (not NaN) speed_of_sound_m_s
// with otherwise-finite atmosphere/state values used to pass this guard -- dividing a finite
// true_airspeed_mps by an infinite speed of sound gives a finite mach == 0.0 (IEEE-754), so
// checking only the derived AeroInputs fields let compute() run on data that looked valid but was
// not. Fixed by also checking the raw atmosphere fields this class consumes. Both signs of
// infinity, since -Inf would be caught the same way and is worth pinning too.
TEST(AeroModel, InfiniteSpeedOfSoundWithOtherwiseFiniteValuesNeverCallsCompute) {
  for (const double speed_of_sound :
       {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
    StubAeroModel model;
    const StateVector y = make_state({50.0, 0.0, 0.0}, Eigen::Vector3d::Zero(), kIdentity);
    const VehicleState vs{y};
    EnvironmentSample env;
    env.atmosphere.density_kg_m3 = 1.225;  // finite; only speed_of_sound_m_s is broken
    env.atmosphere.speed_of_sound_m_s = speed_of_sound;

    const Wrench wrench = model.wrench_b(vs, env);

    EXPECT_EQ(model.call_count, 0U)
        << "an infinite speed_of_sound_m_s must not silently produce mach == finite/Inf == 0.0 "
           "and let compute() run on it (speed_of_sound="
        << speed_of_sound << ")";
    EXPECT_FALSE(wrench.force_b.allFinite());
    EXPECT_FALSE(wrench.moment_b.allFinite());
  }
}

// R7: compute() is unit-testable with a hand-built AeroInputs alone -- no VehicleState,
// EnvironmentSample, or wrench_b() involved at all (docs/architecture.md §7's own promise).
TEST(AeroModel, ComputeIsUnitTestableWithHandBuiltInputsAndNoSimulator) {
  StubAeroModel model;
  AeroInputs inputs;
  inputs.true_airspeed_mps = 123.0;
  inputs.alpha_rad = 0.05;
  inputs.beta_rad = -0.02;
  inputs.omega_b = Eigen::Vector3d{0.0, 0.0, 0.0};
  inputs.dynamic_pressure_pa = 9000.0;
  inputs.mach = 0.36;

  const Wrench wrench = model.compute(inputs);

  EXPECT_EQ(model.call_count, 1U);
  EXPECT_EQ(model.last_inputs.true_airspeed_mps, 123.0);
  EXPECT_EQ(wrench.force_b(0), 1.0);
}

}  // namespace
