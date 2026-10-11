// The F-16 aero build-up, hand-ported from F16_aero.dml (NASA's DAVE-ML "F-16 Subsonic Aerodynamics
// Model (a la Garza)", Mod P; docs/plans/m7-f16-inventory.md section 2) as an SI realization
// (docs/plans/m7-pr6b-f16-aircraft.md; the M7 plan's Q26 pattern; ADR-0002).
//
// Every quantity here is SI: angles in rad, rates in rad/s, speeds in m/s, lengths in m. The DML
// states its inputs in deg and ft/s; each of its constants that carries a unit is written below as
// the DML's value times a named core/util/units conversion, never as a pre-multiplied number, and
// the breakpoints are derived from the generated header's verbatim degree values at compile time.
// The table values are coefficients and need no conversion.
//
// Two of the DML's choices are kept as the DML makes them:
// - rtd = 180/3.14159265 (:355-366), its own truncated pi, used only in cz1's (beta/rtd)^2. It
//   divides beta in degrees there, as in the file, so the truncation's effect is the DML's.
// - Only the table lookups clip (f16_interp.hpp). del, dail and drdr, and beta in cy0 and cz1,
//   enter their linear terms unclipped: the file says of del "note range is beyond breakpoint
//   range" (:392).
//
// The model returns coefficients at the moment reference center (MRC, 35 % MAC; :167-176). This
// header also dimensionalizes them and transfers the moments to the center of mass (D4), which the
// file leaves to its user.
//
// Pure functions of their arguments: no Config, no VehicleState, no allocation, no exceptions. The
// tests call build_up() directly and compare every field with the shots' internalValues.
#pragma once

#include <Eigen/Core>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "../../common/f16_interp.hpp"
#include "aero_tables.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/util/air_data.hpp"
#include "astra/util/units.hpp"

namespace astra::aircraft_f16_aero_math {

namespace tables = astra::aircraft_f16_aero_tables;
namespace interp = astra::aircraft_f16_interp;
namespace units = astra::units;

// NOLINTBEGIN(readability-magic-numbers): each literal below is a constant of F16_aero.dml (or of
// F16_inertia.dml and the README for the CM position), cited on its line.

// Reference geometry (:368-384).
inline constexpr double kCbar = units::feet_to_meters(11.32);
inline constexpr double kBspan = units::feet_to_meters(30.0);
// 300 ft^2: one foot-to-meter conversion per length dimension.
inline constexpr double kSref = units::feet_to_meters(units::feet_to_meters(300.0));

// The DML's own pi and radians-to-degrees factor (:355-366), not the SI kRadiansPerDegree.
// NOLINTNEXTLINE(modernize-use-std-numbers): the DML's truncated pi, kept on purpose (addendum 8).
inline constexpr double kDmlPi = 3.14159265;
inline constexpr double kRtd = 180.0 / kDmlPi;

// vt's minValue="0.1" ft/s (:292; Mod L, :140-147, "to avoid divide-by-zero"), which the Simulink
// realizes as a Saturate (F16_aero_create.m:92). It feeds only tvt.
inline constexpr double kVtMin = units::feet_to_meters(0.1);

// The control normalizations (:390-429): del = el/25 deg, dail = ail/20 deg, drdr = rdr/30 deg.
inline constexpr double kElevatorScale = units::degrees_to_radians(25.0);
inline constexpr double kAileronScale = units::degrees_to_radians(20.0);
inline constexpr double kRudderScale = units::degrees_to_radians(30.0);

// cy0 = -0.02 * beta(deg) + 0.021 * dail + 0.086 * drdr (:441-469); the beta slope per rad.
inline constexpr double kCy0Beta = -0.02 / units::degrees_to_radians(1.0);
inline constexpr double kCy0Dail = 0.021;
inline constexpr double kCy0Drdr = 0.086;

// cz1 = czt * (1 - (beta/rtd)^2) - 0.19 * del (:479-532).
inline constexpr double kCz1Del = -0.19;

// tvt = 2 * vt (:536-580), the denominator of b2v and cq2v.
inline constexpr double kTvtPerVt = 2.0;

// D4: the CM lies DXCG = 0.01 * CBAR * (35 - CG_PCT_MAC) = 1.132 ft forward of the MRC
// (F16_inertia.dml:141-162, +FWD), with CG_PCT_MAC = 25 % (README.html:1234; Vol II Table 7).
inline constexpr double kMrcPercentMac = 35.0;
inline constexpr double kCmPercentMac = 25.0;
inline constexpr double kCmForwardOfMrc =
    units::feet_to_meters(0.01 * 11.32 * (kMrcPercentMac - kCmPercentMac));

// D6 (docs/plans/m7-f16-inventory.md section 10, item 6): the aero's Mach limit, read from P1's
// "Subsonic" under ADR-0031's data-domain rule. M7 PR 13 declares it through the envelope hook;
// nothing in this PR declares or reads it.
inline constexpr double kMachLimit = 1.0;

// NOLINTEND(readability-magic-numbers)

namespace detail {

template <std::size_t N>
constexpr std::array<double, N> to_radians(const std::array<double, N>& degrees) {
  std::array<double, N> radians{};
  for (std::size_t i = 0; i < N; ++i) {
    radians[i] = units::degrees_to_radians(degrees[i]);
  }
  return radians;
}

}  // namespace detail

// The breakpoints in rad, from the generated header's degree values (:952-970).
inline constexpr std::array<double, 12> kAlpha1 = detail::to_radians(tables::kAlpha1);
inline constexpr std::array<double, 7> kBeta1 = detail::to_radians(tables::kBeta1);
inline constexpr std::array<double, 7> kBeta2 = detail::to_radians(tables::kBeta2);
inline constexpr std::array<double, 5> kDe1 = detail::to_radians(tables::kDe1);

// The DML's nine inputs (:292-349), in SI. ail is already in the DML's +LWD sense: the plugin
// boundary (aero.cpp) flips ICD v1's +RWD aileron once, there.
struct Inputs {
  double vt = 0.0;     // true airspeed, m/s (before the floor)
  double alpha = 0.0;  // rad
  double beta = 0.0;   // rad, +nose left of the velocity ("wind in right ear")
  double p = 0.0;      // rad/s, the body rate relative to the Earth (ADR-0032)
  double q = 0.0;
  double r = 0.0;
  double el = 0.0;   // rad, +TED
  double ail = 0.0;  // rad, +LWD
  double rdr = 0.0;  // rad, +TEL
};

// One field per internalValues varID of the file's checkData, SI where dimensional. abs_cl0 and
// abs_cn0 are absCl0 and absCn0.
struct BuildUp {
  double vt = 0.0;
  double alpha = 0.0;
  double beta = 0.0;
  double p = 0.0;
  double q = 0.0;
  double r = 0.0;
  double el = 0.0;
  double ail = 0.0;
  double rdr = 0.0;
  double rtd = 0.0;
  double cbar = 0.0;
  double bspan = 0.0;
  double sref = 0.0;
  double del = 0.0;
  double dail = 0.0;
  double drdr = 0.0;
  double cxt = 0.0;
  double cy0 = 0.0;
  double czt = 0.0;
  double cz1 = 0.0;
  double tvt = 0.0;
  double b2v = 0.0;
  double cq2v = 0.0;
  double absbeta = 0.0;
  double abs_cl0 = 0.0;
  double abs_cn0 = 0.0;
  double clt = 0.0;
  double cmt = 0.0;
  double cnt = 0.0;
  double cxq = 0.0;
  double cyr = 0.0;
  double cyp = 0.0;
  double czq = 0.0;
  double clr = 0.0;
  double clp = 0.0;
  double cmq = 0.0;
  double cnr = 0.0;
  double cnp = 0.0;
  double dclda = 0.0;
  double dcldr = 0.0;
  double dcnda = 0.0;
  double dcndr = 0.0;
  double cl1 = 0.0;
  double cn1 = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  double cz = 0.0;
  double cl = 0.0;
  double cm = 0.0;
  double cn = 0.0;
};

[[nodiscard]] inline bool finite(const Inputs& in) noexcept {
  return astra::air_data::all_finite(
      {in.vt, in.alpha, in.beta, in.p, in.q, in.r, in.el, in.ail, in.rdr});
}

// Addendum section 2's build-up, term for term.
[[nodiscard]] inline BuildUp build_up(const Inputs& in) noexcept {
  BuildUp b;
  // The floor (minValue, :292); a NaN stays NaN rather than becoming the floor.
  b.vt = std::isnan(in.vt) || in.vt > kVtMin ? in.vt : kVtMin;
  b.alpha = in.alpha;
  b.beta = in.beta;
  b.p = in.p;
  b.q = in.q;
  b.r = in.r;
  b.el = in.el;
  b.ail = in.ail;
  b.rdr = in.rdr;
  b.rtd = kRtd;
  b.cbar = kCbar;
  b.bspan = kBspan;
  b.sref = kSref;

  b.del = in.el / kElevatorScale;
  b.dail = in.ail / kAileronScale;
  b.drdr = in.rdr / kRudderScale;

  b.cxt = interp::lookup(kDe1, kAlpha1, tables::kCxt, in.el, in.alpha);
  b.cy0 = kCy0Beta * in.beta + kCy0Dail * b.dail + kCy0Drdr * b.drdr;
  b.czt = interp::lookup(kAlpha1, tables::kCzt, in.alpha);
  const double beta_over_rtd = units::radians_to_degrees(in.beta) / kRtd;
  b.cz1 = b.czt * (1.0 - beta_over_rtd * beta_over_rtd) + kCz1Del * b.del;

  b.tvt = kTvtPerVt * b.vt;
  b.b2v = b.bspan / b.tvt;
  b.cq2v = b.cbar * in.q / b.tvt;

  b.absbeta = std::abs(in.beta);
  b.abs_cl0 = interp::lookup(kBeta1, kAlpha1, tables::kAbsCl0, b.absbeta, in.alpha);
  b.abs_cn0 = interp::lookup(kBeta1, kAlpha1, tables::kAbsCn0, b.absbeta, in.alpha);
  // piecewise: -absCl0 when beta < 0, otherwise absCl0 (:607-663); -0.0 takes "otherwise".
  b.clt = in.beta < 0.0 ? -b.abs_cl0 : b.abs_cl0;
  b.cmt = interp::lookup(kDe1, kAlpha1, tables::kCmt, in.el, in.alpha);
  b.cnt = in.beta < 0.0 ? -b.abs_cn0 : b.abs_cn0;

  b.cxq = interp::lookup(kAlpha1, tables::kCxq, in.alpha);
  b.cyr = interp::lookup(kAlpha1, tables::kCyr, in.alpha);
  b.cyp = interp::lookup(kAlpha1, tables::kCyp, in.alpha);
  b.czq = interp::lookup(kAlpha1, tables::kCzq, in.alpha);
  b.clr = interp::lookup(kAlpha1, tables::kClr, in.alpha);
  b.clp = interp::lookup(kAlpha1, tables::kClp, in.alpha);
  b.cmq = interp::lookup(kAlpha1, tables::kCmq, in.alpha);
  b.cnr = interp::lookup(kAlpha1, tables::kCnr, in.alpha);
  b.cnp = interp::lookup(kAlpha1, tables::kCnp, in.alpha);

  b.dclda = interp::lookup(kBeta2, kAlpha1, tables::kDclda, in.beta, in.alpha);
  b.dcldr = interp::lookup(kBeta2, kAlpha1, tables::kDcldr, in.beta, in.alpha);
  b.dcnda = interp::lookup(kBeta2, kAlpha1, tables::kDcnda, in.beta, in.alpha);
  b.dcndr = interp::lookup(kBeta2, kAlpha1, tables::kDcndr, in.beta, in.alpha);

  b.cl1 = b.clt + b.dclda * b.dail + b.dcldr * b.drdr;
  b.cn1 = b.cnt + b.dcnda * b.dail + b.dcndr * b.drdr;

  // The outputs (:784-946): the nine damping derivatives on cq2v and b2v.
  b.cx = b.cxt + b.cq2v * b.cxq;
  b.cy = b.cy0 + b.b2v * (b.cyp * in.p + b.cyr * in.r);
  b.cz = b.cz1 + b.cq2v * b.czq;
  b.cl = b.cl1 + b.b2v * (b.clp * in.p + b.clr * in.r);
  b.cm = b.cmt + b.cq2v * b.cmq;
  b.cn = b.cn1 + b.b2v * (b.cnp * in.p + b.cnr * in.r);
  return b;
}

// The force at the MRC and the moment about the CM, body axes (+fwd, +right, +down), from the
// coefficients and the dynamic pressure. The MRC lies kCmForwardOfMrc aft of the CM, at
// r = (-d, 0, 0), so M_cm = M_mrc + r x F: pitch gains d * Fz, yaw loses d * Fy, roll is
// unchanged (DYCG = DZCG = 0).
[[nodiscard]] inline astra::Wrench wrench(const BuildUp& b, double dynamic_pressure) noexcept {
  const double qs = dynamic_pressure * kSref;
  astra::Wrench out;
  out.force_b = Eigen::Vector3d(qs * b.cx, qs * b.cy, qs * b.cz);
  const double roll_mrc = qs * kBspan * b.cl;
  const double pitch_mrc = qs * kCbar * b.cm;
  const double yaw_mrc = qs * kBspan * b.cn;
  out.moment_b = Eigen::Vector3d(roll_mrc, pitch_mrc + kCmForwardOfMrc * out.force_b.z(),
                                 yaw_mrc - kCmForwardOfMrc * out.force_b.y());
  return out;
}

// ADR-0018's "this contribution is invalid" sentinel, never Wrench{}'s zero.
[[nodiscard]] inline astra::Wrench non_finite_wrench() noexcept {
  astra::Wrench out;
  out.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  out.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return out;
}

}  // namespace astra::aircraft_f16_aero_math
