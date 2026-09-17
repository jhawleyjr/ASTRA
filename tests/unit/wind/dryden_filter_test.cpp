// plugins/physical/wind/dryden/dryden_filter.hpp against tests/golden/wind/mil_f_8785c_dryden.hpp
// (PLAN.md M2 PR 2b). The MIL parameter-law tests below deliberately reimplement the formulas from
// the golden header's raw constants, independently of dryden_filter.hpp's own low_altitude_
// parameters()/high_altitude_parameters() -- see mil_f_8785c_dryden.hpp's header comment for why:
// sharing only the transcribed numbers, not the arithmetic that combines them, is what makes this a
// real cross-check rather than the implementation checked against itself.
#include "plugins/physical/wind/dryden/dryden_filter.hpp"

#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>

#include "astra/util/units.hpp"
#include "golden/wind/mil_f_8785c_dryden.hpp"

namespace {

using astra::dryden::MilParameters;
using astra::dryden::TwoStateCoefficients;
namespace golden = astra::test::mil_f_8785c;

constexpr double kSigmaWLow = 3.0;  // m/s, arbitrary but fixed test input
constexpr double kSigmaHigh = 5.0;  // m/s, arbitrary but fixed test input
constexpr double kTol = 1e-9;

double golden_bracket(double h_ft) {
  return golden::kLowAltitudeBracketConst + golden::kLowAltitudeBracketSlopePerFoot * h_ft;
}
double golden_length_uv_m(double h_ft) {
  return astra::units::feet_to_meters(
      h_ft / std::pow(golden_bracket(h_ft), golden::kLowAltitudeLengthExponent));
}
double golden_sigma_ratio(double h_ft) {
  return 1.0 / std::pow(golden_bracket(h_ft), golden::kLowAltitudeIntensityExponent);
}

// --- Low-altitude MIL parameter law (Figures 10/11, 10 < h < 1000 ft) ---

TEST(DrydenFilter, LowAltitudeParametersMatchTheGoldenFormula) {
  for (const double h_ft : {10.0, 100.0, 500.0, 1000.0}) {
    SCOPED_TRACE(h_ft);
    const double altitude_m = astra::units::feet_to_meters(h_ft);
    const MilParameters p =
        astra::dryden::mil_f_8785c_parameters(altitude_m, kSigmaWLow, kSigmaHigh);
    EXPECT_NEAR(golden_length_uv_m(h_ft), p.length_u_m, kTol);
    EXPECT_NEAR(golden_length_uv_m(h_ft), p.length_v_m, kTol);
    ASSERT_TRUE(golden::kLowAltitudeVerticalScaleIsAltitude);
    EXPECT_NEAR(altitude_m, p.length_w_m, kTol);  // L_w = h
    EXPECT_NEAR(kSigmaWLow, p.sigma_w_m_s, kTol);
    EXPECT_NEAR(kSigmaWLow * golden_sigma_ratio(h_ft), p.sigma_u_m_s, kTol);
    EXPECT_NEAR(kSigmaWLow * golden_sigma_ratio(h_ft), p.sigma_v_m_s, kTol);
  }
}

// --- 10 ft clamp: Figure 10's own domain is 10 < h < 1000 ft; below 10 ft, altitude is clamped to
// 10 ft rather than extrapolating the bracket toward zero. ---

TEST(DrydenFilter, BelowTenFeetClampsToTenFeet) {
  const MilParameters at_zero = astra::dryden::mil_f_8785c_parameters(0.0, kSigmaWLow, kSigmaHigh);
  const MilParameters at_negative = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(-50.0), kSigmaWLow, kSigmaHigh);
  const MilParameters at_ten = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(10.0), kSigmaWLow, kSigmaHigh);
  EXPECT_NEAR(at_ten.length_u_m, at_zero.length_u_m, kTol);
  EXPECT_NEAR(at_ten.length_w_m, at_zero.length_w_m, kTol);
  EXPECT_NEAR(at_ten.length_u_m, at_negative.length_u_m, kTol);
  EXPECT_NEAR(at_ten.length_w_m, at_negative.length_w_m, kTol);
}

// --- Medium/high-altitude model (section 3.7.2.1, p.48: isotropic, 1750 ft, above 2000 ft) ---

TEST(DrydenFilter, HighAltitudeParametersAreIsotropicAt1750Feet) {
  for (const double h_ft : {2000.0, 5000.0}) {
    SCOPED_TRACE(h_ft);
    const double altitude_m = astra::units::feet_to_meters(h_ft);
    const MilParameters p =
        astra::dryden::mil_f_8785c_parameters(altitude_m, kSigmaWLow, kSigmaHigh);
    const double expected_length_m =
        astra::units::feet_to_meters(golden::kMediumHighAltitudeScaleLengthFt);
    EXPECT_NEAR(expected_length_m, p.length_u_m, kTol);
    EXPECT_NEAR(expected_length_m, p.length_v_m, kTol);
    EXPECT_NEAR(expected_length_m, p.length_w_m, kTol);
    EXPECT_NEAR(kSigmaHigh, p.sigma_u_m_s, kTol);
    EXPECT_NEAR(kSigmaHigh, p.sigma_v_m_s, kTol);
    EXPECT_NEAR(kSigmaHigh, p.sigma_w_m_s, kTol);
  }
}

// --- Continuity at both regime joins, and the interpolated band between them ---

TEST(DrydenFilter, ContinuousAtTheThousandFootJoin) {
  const double h_ft = golden::kLowAltitudeMaxFt;
  const MilParameters below = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(h_ft - 1e-6), kSigmaWLow, kSigmaHigh);
  const MilParameters at = astra::dryden::mil_f_8785c_parameters(astra::units::feet_to_meters(h_ft),
                                                                 kSigmaWLow, kSigmaHigh);
  EXPECT_NEAR(below.length_u_m, at.length_u_m, 1e-6);
  EXPECT_NEAR(below.length_w_m, at.length_w_m, 1e-6);
  EXPECT_NEAR(below.sigma_u_m_s, at.sigma_u_m_s, 1e-6);
  // At exactly 1000 ft, the bracket is 0.177 + 0.000823*1000 == 1.0, so length_u == length_w and
  // sigma_u == sigma_w -- the low-altitude model's own internal continuity, not just this test's.
  EXPECT_NEAR(at.length_u_m, at.length_w_m, kTol);
  EXPECT_NEAR(at.sigma_u_m_s, at.sigma_w_m_s, kTol);
}

TEST(DrydenFilter, ContinuousAtTheTwoThousandFootJoin) {
  const double h_ft = golden::kMediumHighAltitudeMinFt;
  const MilParameters below = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(h_ft - 1e-6), kSigmaWLow, kSigmaHigh);
  const MilParameters at = astra::dryden::mil_f_8785c_parameters(astra::units::feet_to_meters(h_ft),
                                                                 kSigmaWLow, kSigmaHigh);
  EXPECT_NEAR(below.length_u_m, at.length_u_m, 1e-3);
  EXPECT_NEAR(below.sigma_u_m_s, at.sigma_u_m_s, 1e-3);
}

TEST(DrydenFilter, InterpolatedBandLiesBetweenTheTwoModels) {
  const MilParameters low_end = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(golden::kLowAltitudeMaxFt), kSigmaWLow, kSigmaHigh);
  const MilParameters high_end = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(golden::kMediumHighAltitudeMinFt), kSigmaWLow, kSigmaHigh);
  const MilParameters mid = astra::dryden::mil_f_8785c_parameters(
      astra::units::feet_to_meters(1500.0), kSigmaWLow, kSigmaHigh);
  EXPECT_GT(mid.length_u_m, low_end.length_u_m);
  EXPECT_LT(mid.length_u_m, high_end.length_u_m);
  EXPECT_GT(mid.sigma_u_m_s, low_end.sigma_u_m_s);
  EXPECT_LT(mid.sigma_u_m_s, high_end.sigma_u_m_s);
}

// --- Two-state exact discretization: the Lyapunov residual is the real proof ---
//
// P = diag(1/(4*rate^3), 1/(4*rate)) is the continuous stationary covariance (Joe: verified). If
// Ad/Qd are the true exact-discretization pair, P must be a fixed point of the discrete Lyapunov
// recursion: P == Ad*P*Ad^T + Qd, for every (rate, dt). rate is held fixed at 1.0 so P itself stays
// a small, well-scaled constant across the whole sweep -- what varies is dt, so y = 2*rate*dt
// sweeps the crossover from both sides while P never grows to the point where its own magnitude
// would swamp the residual.
TEST(DrydenFilter, TwoStateDiscretizationSatisfiesTheLyapunovEquationAcrossTheCrossover) {
  constexpr double kRate = 1.0;
  Eigen::Matrix2d p = Eigen::Matrix2d::Zero();
  p(0, 0) = 1.0 / (4.0 * kRate * kRate * kRate);
  p(1, 1) = 1.0 / (4.0 * kRate);

  // Integer loop counter (clang-tidy clang-analyzer-security.FloatLoopCounter): a double loop
  // variable accumulates multiplicative rounding error across iterations in a way an integer
  // exponent does not. y = 1e-8 * 10^(step/2) covers 1e-8 .. 10.0 exactly (step 0..18, half a
  // decade per step) -- the same range and density the original sqrt(10)-multiplying loop covered.
  for (int step = 0; step <= 18; ++step) {
    const double y = 1e-8 * std::pow(10.0, static_cast<double>(step) / 2.0);
    const double dt = y / (2.0 * kRate);
    SCOPED_TRACE(y);
    const TwoStateCoefficients c = astra::dryden::two_state_coefficients(kRate, dt);
    const Eigen::Matrix2d residual = p - (c.ad * p * c.ad.transpose()) - c.qd;
    EXPECT_NEAR(0.0, residual(0, 0), 1e-12 * p(0, 0));
    EXPECT_NEAR(0.0, residual(0, 1), 1e-12 * p(0, 0));
    EXPECT_NEAR(0.0, residual(1, 1), 1e-12 * p(1, 1));
  }
}

// The Taylor-series branch and the expm1-based closed-form branch must agree closely right at the
// crossover they switch on, or the discretization would have a discontinuity at y == 1e-3 that no
// physical dt/altitude combination could ever land on exactly, but that would still mean two
// adjacent frames straddling it see inconsistent noise statistics.
TEST(DrydenFilter, SeriesAndDirectBranchesAgreeAcrossTheCrossover) {
  constexpr double kRate = 1.0;
  const double dt_below = 0.999e-3 / (2.0 * kRate);
  const double dt_above = 1.001e-3 / (2.0 * kRate);
  const TwoStateCoefficients below = astra::dryden::two_state_coefficients(kRate, dt_below);
  const TwoStateCoefficients above = astra::dryden::two_state_coefficients(kRate, dt_above);
  // Relative, not absolute: each branch has its own O(y) truncation/rounding floor near the
  // boundary (the Taylor series' dropped y^5 term on one side, the expm1 formulation's residual
  // cancellation on the other) -- the Lyapunov residual test above is the tight (1e-12 relative)
  // proof of correctness across this same range; this test only checks the two branches don't
  // disagree by an amount a real y == 1e-3 discontinuity would produce (order 1, not order 1e-3).
  constexpr double kRelativeTol = 1e-2;
  EXPECT_NEAR(below.qd(0, 0), above.qd(0, 0), std::abs(below.qd(0, 0)) * kRelativeTol);
  EXPECT_NEAR(below.qd(0, 1), above.qd(0, 1), std::abs(below.qd(0, 1)) * kRelativeTol);
  EXPECT_NEAR(below.qd(1, 1), above.qd(1, 1), std::abs(below.qd(1, 1)) * kRelativeTol);
}

// Qd is a covariance matrix (PSD) at every rate this function can be called with, including rate ==
// 0 (two_state_coefficients itself has no singularity there -- see dryden_filter.hpp's file header;
// the a == 0 singularity lives only in two_state_stationary_init's divisions, exercised by
// dryden_test.cpp against the full plugin, where gust_rate's floor keeps rate away from exactly 0).
TEST(DrydenFilter, QdIsPositiveSemiDefiniteAcrossRatesIncludingZero) {
  for (const double rate : {0.0, 1e-6, 1e-3, 1.0, 100.0}) {
    for (const double dt : {1e-6, 0.001, 1.0, 10.0}) {
      SCOPED_TRACE(::testing::Message() << "rate=" << rate << " dt=" << dt);
      const TwoStateCoefficients c = astra::dryden::two_state_coefficients(rate, dt);
      EXPECT_TRUE(std::isfinite(c.qd(0, 0)));
      EXPECT_TRUE(std::isfinite(c.qd(0, 1)));
      EXPECT_TRUE(std::isfinite(c.qd(1, 1)));
      EXPECT_GE(c.qd(0, 0), -1e-15);
      EXPECT_GE(c.qd(1, 1), -1e-15);
      const double det = c.qd(0, 0) * c.qd(1, 1) - c.qd(0, 1) * c.qd(0, 1);
      EXPECT_GE(det, -1e-12 * std::max(1.0, c.qd(0, 0) * c.qd(1, 1)));
    }
  }
}

}  // namespace
