// M1 acceptance (docs/architecture.md §16, PLAN.md M1 PR 3): ballistic arc vs closed form at two
// attitudes. With omega_b = 0 the attitude never changes, so v_b_dot = rotate_n_to_b(q_nb0, g_n) is
// constant in body axes and p_n_dot is linear in t -- both within RK4's exactness class (a degree-4
// polynomial), so the closed form should match to near machine precision, not just within a loose
// bound. A level or yaw-only attitude leaves NED gravity on the down axis unchanged and so cannot
// expose a transposed n<->b rotation; the pitched case (nonzero pitch, no special angle) can, which
// is why both configs exist (Codex plan-review finding). v_b's own closed form is checked
// independently of the trajectory, for the same reason: composing a transposed position rotation
// with a transposed gravity rotation can cancel in p_n alone.
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace {

// Must match every tests/configs/physics_ballistic_*.yaml: g from physical.gravity's default
// (constant.cpp), v_b from initial.state.
constexpr double kG = 9.80665;
constexpr double kAbsoluteTolerance = 1e-6;

void run_and_check(std::string_view config_name) {
  SCOPED_TRACE(config_name);
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/" + std::string{config_name});
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

  const astra::StateVector y0 = sim.vehicles().front().state();
  const astra::frames::Quaternion q_nb0 =
      y0.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
  const Eigen::Vector3d p_n0 = y0.segment<astra::state::kPositionSize>(astra::state::kPosition);
  const Eigen::Vector3d v_b0 = y0.segment<astra::state::kVelocitySize>(astra::state::kVelocity);
  const Eigen::Vector3d g_n{0.0, 0.0, kG};
  const Eigen::Vector3d v_n0 = astra::frames::rotate_b_to_n(q_nb0, v_b0);

  sim.run();
  const double duration = static_cast<double>(sim.frames()) * sim.clock().dt();
  const astra::StateVector& y = sim.vehicles().front().state();

  // Attitude never changes (omega_b = 0 throughout, and torque-free too, so |omega| stays 0):
  // independent confirmation that the test's own closed form and the sim agree on that premise.
  const astra::frames::Quaternion q_nb =
      y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
  for (int i = 0; i < astra::state::kQuaternionSize; ++i) {
    EXPECT_NEAR(q_nb0(i), q_nb(i), kAbsoluteTolerance);
  }

  // v_b's own closed form, independent of the position trajectory (Codex plan-review finding): pins
  // the direction of the gravity-vector rotation on its own.
  const Eigen::Vector3d expected_v_b = v_b0 + astra::frames::rotate_n_to_b(q_nb0, g_n) * duration;
  const Eigen::Vector3d v_b = y.segment<astra::state::kVelocitySize>(astra::state::kVelocity);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(expected_v_b(i), v_b(i), kAbsoluteTolerance) << "component " << i;
  }

  const Eigen::Vector3d expected_p_n = p_n0 + v_n0 * duration + 0.5 * g_n * duration * duration;
  const Eigen::Vector3d p_n = y.segment<astra::state::kPositionSize>(astra::state::kPosition);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(expected_p_n(i), p_n(i), kAbsoluteTolerance) << "component " << i;
  }
}

TEST(Ballistic, LevelAttitudeMatchesClosedForm) { run_and_check("physics_ballistic_level.yaml"); }

TEST(Ballistic, PitchedAttitudeMatchesClosedFormAndPinsNToB) {
  run_and_check("physics_ballistic_pitched.yaml");
}

}  // namespace
