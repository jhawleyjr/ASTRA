// M1 acceptance (docs/architecture.md §16, PLAN.md M1 PR 3): torque-free asymmetric rigid body --
// kinetic energy and the nav-frame angular momentum vector are exact invariants of the EOM with no
// applied wrench and g = 0, so their relative drift over 1000 s at 1 kHz bounds the integrator's
// error, not a closed-form solution (there isn't a simple one for a general asymmetric top).
// |h_n|'s DIRECTION staying constant, not just its magnitude, is the real test of the quaternion
// kinematics: |h_n| == |h_b| holds under any rotation, correct or not, but a wrong product order or
// sign in quaternion_derivative moves h_n's direction over time.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace {

// Must match tests/configs/physics_torque_free.yaml's physical.mass entry.
constexpr double kIxx = 1.0;
constexpr double kIyy = 2.0;
constexpr double kIzz = 3.0;

constexpr int kCheckpoints = 10;
constexpr std::uint64_t kFramesPerCheckpoint = 100000;  // 100 s at 1 kHz
constexpr double kRelativeDriftTolerance = 1e-9;

double kinetic_energy(const astra::StateVector& y) {
  const Eigen::Vector3d omega_b =
      y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate);
  return 0.5 * (kIxx * omega_b(0) * omega_b(0) + kIyy * omega_b(1) * omega_b(1) +
                kIzz * omega_b(2) * omega_b(2));
}

Eigen::Vector3d angular_momentum_n(const astra::StateVector& y) {
  const astra::frames::Quaternion q_nb =
      y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
  const Eigen::Vector3d omega_b =
      y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate);
  const Eigen::Vector3d h_b{kIxx * omega_b(0), kIyy * omega_b(1), kIzz * omega_b(2)};
  return astra::frames::rotate_b_to_n(q_nb, h_b);
}

TEST(TorqueFree, EnergyAndAngularMomentumDriftBelowOneEMinusNineOverOneThousandSeconds) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/physics_torque_free.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), static_cast<std::uint64_t>(kCheckpoints) * kFramesPerCheckpoint);

  const astra::StateVector& y0 = sim.vehicles().front().state();
  const double energy0 = kinetic_energy(y0);
  const Eigen::Vector3d h_n0 = angular_momentum_n(y0);
  ASSERT_GT(energy0, 0.0);
  ASSERT_GT(h_n0.norm(), 0.0);
  const Eigen::Vector3d h_n0_direction = h_n0.normalized();

  for (int checkpoint = 1; checkpoint <= kCheckpoints; ++checkpoint) {
    ASSERT_FALSE(sim.run(kFramesPerCheckpoint)) << "checkpoint " << checkpoint;
    const astra::StateVector& y = sim.vehicles().front().state();

    const double energy = kinetic_energy(y);
    const double energy_drift = std::abs(energy - energy0) / energy0;
    EXPECT_LT(energy_drift, kRelativeDriftTolerance) << "checkpoint " << checkpoint;

    const Eigen::Vector3d h_n = angular_momentum_n(y);
    const double magnitude_drift = std::abs(h_n.norm() - h_n0.norm()) / h_n0.norm();
    EXPECT_LT(magnitude_drift, kRelativeDriftTolerance) << "checkpoint " << checkpoint;

    // |h_n| conservation alone can't tell a correct quaternion product from a wrong one -- both
    // preserve the vector's length under any rotation. Its direction not drifting is what actually
    // exercises quaternion_derivative's sign and product order.
    const double direction_drift = (h_n.normalized() - h_n0_direction).norm();
    EXPECT_LT(direction_drift, kRelativeDriftTolerance) << "checkpoint " << checkpoint;
  }
}

}  // namespace
