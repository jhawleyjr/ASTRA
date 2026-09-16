// The MIL-F-8785C Dryden turbulence math, split out of dryden.cpp so tests/unit/wind/
// dryden_filter_test.cpp can check it deterministically -- with no Rng, no Simulation, and no
// plugin instance -- against tests/golden/wind/mil_f_8785c_dryden.hpp's independently-transcribed
// constants. Every constant below is ALSO independently transcribed here, from
// docs/refs/MIL-F-8785C.pdf, with the same page citations that golden header carries (its own
// header comment explains why this file does not #include it: plugins/ is shipped production code
// and must never depend on anything under tests/). PLAN.md M2 PR 2b.
//
// --- The two facts this file states rather than leaves implied ---
//
// (1) FROZEN-COEFFICIENT APPROXIMATION. mil_f_8785c_parameters() and the discretization
// coefficients below are recomputed every frame from that frame's V/h, but the filter STATE (the
// values step_scalar_filter/step_two_state_filter carry forward) was shaped by the PREVIOUS frame's
// coefficients. This is the standard quasi-stationary MIL-F-8785C implementation (V and h are
// assumed to vary slowly relative to the filter's own time constant L/V), not an exact
// time-varying-LTI solution.
//
// (2) DISCRETIZATION IS EXACT (matched), not Euler-Maruyama. Each gust axis is an LTI shaping
// filter driven by unit-intensity white noise; the coefficients below are its exact discrete-time
// equivalent at whatever dt the scheduler runs, so the stationary variance is exactly sigma^2
// regardless of dt -- no discretization bias hides inside the PSD acceptance tolerance
// (tests/physics/dryden_psd_test.cpp).
//
// MIL-F-8785C's Phi(omega) is ONE-SIDED in spatial frequency Omega (sigma^2 = integral_0^inf
// Phi(Omega) dOmega); Taylor's frozen-turbulence hypothesis converts to a temporal filter via
// s = j*V*Omega, giving, with a = V/L (all forms verified to integrate to sigma^2 and matched to
// MIL-F-8785C's own spectra, section 3.7.1.2, p.47 -- Joe: do not re-derive):
//
//   u:    G_u(s)   = sigma_u * sqrt(2a) / (s + a)                          -- first-order (OU)
//   v, w: G_vw(s)  = sigma * sqrt(a) * (sqrt(3)*s + a) / (s + a)^2         -- second-order
//
// u's exact AR(1) step is standard (see step_scalar_filter below). v/w's canonical controllable
// realization is A = [[0, 1], [-a^2, -2a]], B = [0, 1]', y = sigma*sqrt(a)*(a*x1 + sqrt(3)*x2)
// (verified: X2/W = s/(s+a)^2, X1/W = 1/(s+a)^2, so Y/W = G_vw(s) exactly). Its exact discrete
// step, derived from A = -a*I + N with N = [[a,1],[-a^2,-a]] nilpotent (N^2 = 0, so
// exp(A*dt) = exp(-a*dt)*(I + N*dt) exactly -- Joe: verified, do not re-derive):
//
//   Ad = exp(-a*dt) * [[1+a*dt, dt], [-a^2*dt, 1-a*dt]]
//   Qd = integral_0^dt exp(-2a*tau) * [[tau^2, tau*(1-a*tau)], [tau*(1-a*tau), (1-a*tau)^2]] dtau
//      (Joe: integrand verified, do not re-derive)
//
// Qd's three distinct entries reduce to I0, I1, I2 = integral_0^dt tau^k * exp(-2a*tau) dtau:
// Qd(0,0)=I2, Qd(0,1)=Qd(1,0)=I1-a*I2, Qd(1,1)=I0-2a*I1+a^2*I2. I0's closed form
// (-expm1(-y)/(2a), y = 2a*dt) never cancels. I1/I2's closed forms DO cancel catastrophically as
// y -> 0 (their leading O(1) terms cancel to leave an O(y) or O(y^2) result, so the double's ~16
// digits of input precision buy only ~16-log10(1/y) digits of output precision -- at y ~ 2e-8, that
// is essentially nothing left). two_state_coefficients() below switches to a directly-summed Taylor
// series in y below a stated crossover, where no such cancellation occurs (each series term is
// already small and additive, not a difference of two large near-equal quantities).
//
// --- Stationary start, and why "freezes at V=0" is the wrong intuition ---
//
// The continuous stationary covariance of (x1, x2) is P = diag(1/(4a^3), 1/(4a)), zero cross-term
// (Joe: verified, do not re-derive), so x1 and x2 are statistically INDEPENDENT and must be
// initialized from two SEPARATE normal draws, not one value reused for both (a same-draw
// initialization would give the two components a spurious perfect correlation the true stationary
// distribution does not have). two_state_stationary_init()/scalar_stationary_init() below are
// called once, on the filter's very first frame (or the frame after reset()), instead of stepping
// the filter from a zero state -- without this, every run opens with several L/V of artificially
// suppressed turbulence while the filter's own dynamics spin up from rest.
//
// a = V/L is FLOORED at kMinRate (never special-cased at a == 0): y = sigma*sqrt(a)*(a*x1 +
// sqrt(3)*x2) goes to ZERO as a -> 0 -- it does not "freeze" at some fixed value, and Var(y) =
// sigma^2 exactly, independent of a (Joe: verified). But the stationary-init divisions
// (1/(2*a^1.5), 1/(2*sqrt(a))) are 0/0 at a == 0 exactly, and even a tiny nonzero a (say 1e-200)
// overflows 1/a^1.5 to Inf while any "is a exactly zero" guard never fires. Flooring a at
// kMinRate = 1e-6 s^-1 (V on the order of a millimetre per second at a typical L -- far below any
// airspeed this sim will ever see) keeps every division finite (1/(2*kMinRate^1.5) = 5e8) with no
// discontinuous branch and no special case, while Var(y) stays exact regardless of a.
#pragma once

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "astra/util/units.hpp"

namespace astra::dryden {

// NOLINTBEGIN(readability-magic-numbers): every literal below is either a MIL-F-8785C constant
// (cited by name/page above), a coefficient of the exact discretization or Taylor series derived
// above (2.0, 0.5, the factorial denominators), or a structural index (0, 1) into a 2-vector/2x2
// matrix -- naming each one individually would make the formulas harder to check against the file
// header's derivation, not easier, the same reasoning frames.hpp/geodesy.hpp give for the same
// pattern.

// --- MIL-F-8785C turbulence scale lengths and intensities (section 3.7, p.47-56) ---

namespace detail {
// Figure 10 (p.55): L_u = L_v = h / (0.177 + 0.000823*h)^1.2, for 10 < h < 1000 ft.
inline constexpr double kLowBracketConst = 0.177;
inline constexpr double kLowBracketSlopePerFoot = 0.000823;
inline constexpr double kLowLengthExponent = 1.2;
// Figure 10's own text label states this domain; below 10 ft the curve is not defined, and the
// bracket -> 0.177 + 0.00823 = 0.18523 as h -> 10 ft stays bounded well away from zero, so the
// floor below is what keeps L_u/L_v/L_w finite and positive at every altitude this plugin sees.
inline constexpr double kLowMinFt = 10.0;
inline constexpr double kLowMaxFt = 1000.0;
// L_w = h below 1000 ft: not printed as a formula on MIL-F-8785C's own Figure 10 page (the second,
// unlabeled curve there implies it graphically); confirmed in words from MathWorks' "Dryden Wind
// Turbulence Model (Continuous)" documentation, which states explicitly, citing MIL-F-8785C:
// "Lw = h". Needs no separate constant -- it is the identity.

// Figure 11 (p.56): sigma_u/sigma_w = sigma_v/sigma_w = 1 / (0.177 + 0.000823*h)^0.4, below 1000 ft
// -- same bracket as Figure 10, a different exponent.
inline constexpr double kLowIntensityExponent = 0.4;

// Section 3.7.2.1 (p.48): "L_u = L_v = L_w = 1,750 feet using the Dryden form,"
// medium/high-altitude model, isotropic above 2000 ft (section 3.7.2, same page). The von Karman
// form's 2,500 ft on the same page is not used -- this plugin implements the Dryden form only.
inline constexpr double kHighScaleLengthFt = 1750.0;
inline constexpr double kHighMinFt = 2000.0;
}  // namespace detail

// Section 3.7.3.4 (p.53): "The turbulence intensities to be used are sigma_w = 0.1*u20" -- a
// mixed-unit empirical relation (sigma_w in ft/s, u20 in knots), not a unit conversion. Public (not
// detail::): dryden.cpp applies this to a knots-valued config key before any unit conversion, so it
// belongs to the plugin's own config-reading code, not just this header's internal math.
inline constexpr double kSigmaWToW20Ratio = 0.1;

struct MilParameters {
  double length_u_m = 0.0;
  double length_v_m = 0.0;
  double length_w_m = 0.0;
  double sigma_u_m_s = 0.0;
  double sigma_v_m_s = 0.0;
  double sigma_w_m_s = 0.0;
};

// h_ft is clamped to [kLowMinFt, ...) inside -- see kLowMinFt's comment.
[[nodiscard]] inline MilParameters low_altitude_parameters(double h_ft,
                                                           double sigma_w_low_m_s) noexcept {
  const double h_clamped_ft = std::max(h_ft, detail::kLowMinFt);
  const double bracket = detail::kLowBracketConst + detail::kLowBracketSlopePerFoot * h_clamped_ft;
  const double length_uv_ft = h_clamped_ft / std::pow(bracket, detail::kLowLengthExponent);
  const double sigma_ratio = 1.0 / std::pow(bracket, detail::kLowIntensityExponent);

  MilParameters p;
  p.length_u_m = units::feet_to_meters(length_uv_ft);
  p.length_v_m = p.length_u_m;
  p.length_w_m = units::feet_to_meters(h_clamped_ft);  // L_w = h
  p.sigma_w_m_s = sigma_w_low_m_s;
  p.sigma_u_m_s = sigma_w_low_m_s * sigma_ratio;
  p.sigma_v_m_s = p.sigma_u_m_s;
  return p;
}

[[nodiscard]] inline MilParameters high_altitude_parameters(double sigma_high_m_s) noexcept {
  MilParameters p;
  p.length_u_m = p.length_v_m = p.length_w_m = units::feet_to_meters(detail::kHighScaleLengthFt);
  p.sigma_u_m_s = p.sigma_v_m_s = p.sigma_w_m_s = sigma_high_m_s;
  return p;
}

// Below 1000 ft: low_altitude_parameters(). Above 2000 ft: high_altitude_parameters(). Between:
// linear interpolation in h between the low-altitude model evaluated AT 1000 ft and the
// high-altitude model (which takes no altitude), matching the standard implementation confirmed via
// MathWorks (not itself printed as a formula in MIL-F-8785C, which treats the two regimes
// separately) -- see dryden.cpp's own header for the full provenance note.
[[nodiscard]] inline MilParameters mil_f_8785c_parameters(double altitude_m, double sigma_w_low_m_s,
                                                          double sigma_high_m_s) noexcept {
  const double h_ft = units::meters_to_feet(altitude_m);
  if (h_ft <= detail::kLowMaxFt) {
    return low_altitude_parameters(h_ft, sigma_w_low_m_s);
  }
  if (h_ft >= detail::kHighMinFt) {
    return high_altitude_parameters(sigma_high_m_s);
  }
  const MilParameters low = low_altitude_parameters(detail::kLowMaxFt, sigma_w_low_m_s);
  const MilParameters high = high_altitude_parameters(sigma_high_m_s);
  const double t = (h_ft - detail::kLowMaxFt) / (detail::kHighMinFt - detail::kLowMaxFt);
  return {
      .length_u_m = low.length_u_m + t * (high.length_u_m - low.length_u_m),
      .length_v_m = low.length_v_m + t * (high.length_v_m - low.length_v_m),
      .length_w_m = low.length_w_m + t * (high.length_w_m - low.length_w_m),
      .sigma_u_m_s = low.sigma_u_m_s + t * (high.sigma_u_m_s - low.sigma_u_m_s),
      .sigma_v_m_s = low.sigma_v_m_s + t * (high.sigma_v_m_s - low.sigma_v_m_s),
      .sigma_w_m_s = low.sigma_w_m_s + t * (high.sigma_w_m_s - low.sigma_w_m_s),
  };
}

// --- Rate floor (see the file header's "stationary start" section) ---

inline constexpr double kMinRate = 1e-6;  // s^-1

// length_m > 0 always: mil_f_8785c_parameters never returns a non-positive length (kLowMinFt's
// floor keeps the low-altitude bracket, and hence L_w = h, bounded away from 0).
[[nodiscard]] inline double gust_rate(double airspeed_m_s, double length_m) noexcept {
  return std::max(airspeed_m_s / length_m, kMinRate);
}

// --- Scalar (u) channel: exact AR(1) ---

struct ScalarCoefficients {
  double phi = 0.0;  // x_{k+1} = phi*x_k + q_std*xi
  double q_std = 0.0;
};

[[nodiscard]] inline ScalarCoefficients scalar_coefficients(double rate, double sigma,
                                                            double dt) noexcept {
  // expm1 avoids the cancellation 1 - exp(-x) would suffer at the rate*dt ~ 1e-4 this model
  // actually runs at; no separate small-x branch is needed here (unlike the two-state case below)
  // because expm1 alone is already exact down to rate*dt == 0.
  return {std::exp(-rate * dt), sigma * std::sqrt(-std::expm1(-2.0 * rate * dt))};
}

[[nodiscard]] inline double scalar_stationary_init(double sigma, double xi) noexcept {
  return sigma * xi;
}

// --- Two-state (v, w) channel: exact discrete LTI step ---

struct TwoStateCoefficients {
  Eigen::Matrix2d ad = Eigen::Matrix2d::Identity();
  Eigen::Matrix2d qd = Eigen::Matrix2d::Zero();  // process-noise covariance, SPD for rate > 0
};

[[nodiscard]] inline TwoStateCoefficients two_state_coefficients(double rate, double dt) noexcept {
  const double a = rate;
  const double y = 2.0 * a * dt;
  const double e = std::exp(-a * dt);

  TwoStateCoefficients out;
  // clang-format off
  out.ad << 1.0 + a * dt, dt,
           -a * a * dt,   1.0 - a * dt;
  // clang-format on
  out.ad *= e;

  // I0, I1, I2 = integral_0^dt tau^k * exp(-2*a*tau) dtau (see the file header for the full
  // derivation of both branches).
  double i0 = 0.0;
  double i1 = 0.0;
  double i2 = 0.0;
  constexpr double kCrossover = 1e-3;  // in y = 2*a*dt
  if (y >= kCrossover) {
    // Closed forms, rewritten through expm1 so only the SECOND cancellation (an O(y) result from
    // O(y) terms, not an O(1) result from O(1) terms) remains -- accurate well past this crossover,
    // see the file header.
    const double em1 = std::expm1(-y);
    i0 = -dt * em1 / y;
    i1 = dt * dt * (-y - (1.0 + y) * em1) / (y * y);
    i2 = dt * dt * dt * (-y * (y + 2.0) - (y * y + 2.0 * y + 2.0) * em1) / (y * y * y);
  } else {
    // Directly-summed Taylor series in y (each term additive, no cancellation), derived by
    // expanding exp(-y*u) termwise inside integral_0^1 u^k exp(-y*u) du and integrating (see the
    // file header). Accurate through the crossover and down to y -> 0.
    const double y2 = y * y;
    const double y3 = y2 * y;
    const double y4 = y3 * y;
    const double s0 = 1.0 - y / 2.0 + y2 / 6.0 - y3 / 24.0 + y4 / 120.0;
    const double s1 = 0.5 - y / 3.0 + y2 / 8.0 - y3 / 30.0 + y4 / 144.0;
    const double s2 = 1.0 / 3.0 - y / 4.0 + y2 / 10.0 - y3 / 36.0 + y4 / 168.0;
    i0 = dt * s0;
    i1 = dt * dt * s1;
    i2 = dt * dt * dt * s2;
  }

  out.qd(0, 0) = i2;
  out.qd(0, 1) = out.qd(1, 0) = i1 - a * i2;
  out.qd(1, 1) = i0 - 2.0 * a * i1 + a * a * i2;
  return out;
}

// Explicit 2x2 lower Cholesky factor (Qd = L * L^T): qd is SPD for rate > 0 (a covariance matrix of
// a nondegenerate process), so the diagonal clamp below only guards roundoff, never a real
// indefiniteness.
[[nodiscard]] inline Eigen::Matrix2d cholesky_lower_2x2(const Eigen::Matrix2d& qd) noexcept {
  Eigen::Matrix2d l = Eigen::Matrix2d::Zero();
  l(0, 0) = std::sqrt(std::max(qd(0, 0), 0.0));
  l(1, 0) = (l(0, 0) > 0.0) ? qd(1, 0) / l(0, 0) : 0.0;
  l(1, 1) = std::sqrt(std::max(qd(1, 1) - l(1, 0) * l(1, 0), 0.0));
  return l;
}

// Stationary covariance P = diag(1/(4*rate^3), 1/(4*rate)), zero cross-term -- x1 and x2 are
// independent, so xi1/xi2 must be two SEPARATE normal draws (see the file header).
[[nodiscard]] inline Eigen::Vector2d two_state_stationary_init(double rate, double xi1,
                                                               double xi2) noexcept {
  return {xi1 / (2.0 * std::pow(rate, 1.5)), xi2 / (2.0 * std::sqrt(rate))};
}

[[nodiscard]] inline double two_state_output(double rate, double sigma,
                                             const Eigen::Vector2d& x) noexcept {
  return sigma * std::sqrt(rate) * (rate * x(0) + std::numbers::sqrt3 * x(1));
}

// NOLINTEND(readability-magic-numbers)

}  // namespace astra::dryden
