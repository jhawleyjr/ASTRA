// aircraft/notional_transport/physical/aero/aero_math.hpp
// (docs/plans/pr4-notional-aero-turbofan.md, R1): compute_wrench() called directly with hand-built
// Inputs/Coefficients literals -- no plugin instance, no Config, no Simulation -- the same
// "unit-tested with no simulator" pattern tests/unit/wind/dryden_filter_test.cpp already
// demonstrates for plugins/physical/wind/dryden/ dryden_filter.hpp.
//
// Conditions N and O and their expected literal Wrench values are computed once in the plan itself
// (an independent re-implementation of the formulas, not this plugin's own code) -- see the plan's
// own "Worked numbers" section for the full derivation. Tolerance: exact equality for every
// precisely-zero expected component (Open item 5 -- each is a zero-times-finite product, never a
// cancellation), 1e-9 relative otherwise.
#include <gtest/gtest.h>

#include <cmath>

#include "aircraft/notional_transport/physical/aero/aero_math.hpp"

namespace {

using astra::linear_aero::Coefficients;
using astra::linear_aero::compute_wrench;
using astra::linear_aero::Inputs;

// docs/plans/pr4-notional-aero-turbofan.md's own "Worked numbers": transcribed from
// docs/refs/aircrafts/737.xml, re-verified against the source this session.
Coefficients notional_transport_coefficients() {
  Coefficients c;
  c.wing_area_m2 = 108.78945984;
  c.wingspan_m = 28.864560;
  c.mean_chord_m = 3.752088;
  c.r_aero_b = Eigen::Vector3d(0.3556, 0.0, -1.6256);
  c.cl_0 = 0.20;
  c.cl_alpha_per_rad = 4.3478260869565215;
  c.cl_elevator_per_rad = 0.2;
  c.cd_0 = 0.021;
  c.cd_k = 0.043;
  c.cy_beta_per_rad = -1.0;
  c.croll_beta_per_rad = -0.09;
  c.croll_p_per_rad = -0.4;
  c.croll_r_per_rad = 0.09;
  c.croll_aileron_per_rad = 0.0743;
  c.croll_rudder_per_rad = 0.01;
  c.cm_alpha_per_rad = -0.6;
  c.cm_q_per_rad = -27.0;
  c.cm_elevator_per_rad = -0.8544;
  c.cn_beta_per_rad = 0.26;
  c.cn_r_per_rad = -0.35;
  c.cn_rudder_per_rad = -0.20;
  return c;
}

// Open item 5: exact equality against a zero expected value (never a cancellation of two large
// quantities here -- see the plan), 1e-9 relative otherwise.
void expect_matches(double actual, double expected, const char* what) {
  if (expected == 0.0) {
    EXPECT_EQ(actual, 0.0) << what;
  } else {
    EXPECT_NEAR(actual, expected, std::abs(expected) * 1e-9) << what;
  }
}

void expect_wrench_matches(const astra::Wrench& actual, const Eigen::Vector3d& force_expected,
                           const Eigen::Vector3d& moment_expected) {
  expect_matches(actual.force_b(0), force_expected(0), "force_b.x");
  expect_matches(actual.force_b(1), force_expected(1), "force_b.y");
  expect_matches(actual.force_b(2), force_expected(2), "force_b.z");
  expect_matches(actual.moment_b(0), moment_expected(0), "moment_b.x");
  expect_matches(actual.moment_b(1), moment_expected(1), "moment_b.y");
  expect_matches(actual.moment_b(2), moment_expected(2), "moment_b.z");
}

TEST(NotionalTransportAeroMath, ConditionNNominalWingsLevel) {
  Inputs in;
  in.true_airspeed_mps = 230.0;
  in.alpha_rad = 0.025484;
  in.beta_rad = 0.0;
  in.omega_b = Eigen::Vector3d::Zero();
  in.dynamic_pressure_pa = 0.5 * 0.4128 * 230.0 * 230.0;
  in.density_kg_m3 = 0.4128;
  in.elevator_rad = 0.0;
  in.aileron_rad = 0.0;
  in.rudder_rad = 0.0;

  const astra::Wrench wrench = compute_wrench(in, notional_transport_coefficients());
  expect_wrench_matches(wrench, Eigen::Vector3d(-20461.3632868957, 0.0, -369817.2356669478),
                        Eigen::Vector3d(0.0, 96622.4239007194, 0.0));
}

TEST(NotionalTransportAeroMath, ConditionOOffNominalAllTermsNonzero) {
  Inputs in;
  in.true_airspeed_mps = 100.0;
  in.alpha_rad = 0.0;
  in.beta_rad = 0.1;
  in.omega_b = Eigen::Vector3d(0.05, -0.02, 0.03);
  in.dynamic_pressure_pa = 0.5 * 1.0 * 100.0 * 100.0;
  in.density_kg_m3 = 1.0;
  in.elevator_rad = 0.1;
  in.aileron_rad = -0.05;
  in.rudder_rad = 0.02;

  const astra::Wrench wrench = compute_wrench(in, notional_transport_coefficients());
  expect_wrench_matches(wrench,
                        Eigen::Vector3d(-7061.8221796725, -55376.3870330663, -119668.405824),
                        Eigen::Vector3d(-325716.8717090227, -99667.9663201383, 301932.9197071403));
}

}  // namespace
