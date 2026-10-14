// A second transcription of F16_control.dml (Revision F; docs/plans/m7-f16-inventory.md section 5),
// in the DML's own units: ft, KEAS (nmi_h), deg, rad/s for the body rates, frac for the sticks, and
// percent for PWR (the M7 plan's R11; docs/plans/m7-pr11a-control-peer.md, Q2). The F16Control
// tests compare the plugin's SI realization (sim/transport/nesc_control/control_law.hpp) with it,
// on hand-built and seeded ICD v1 frames, after this file's own conversions to and from the ICD.
//
// It shares no code with control_law.hpp: its constants are the DML's numbers as printed, its
// conversions are this file's literals (the foot, the knot, pi/180), and its EAS, Earth rate and
// DCM are written out here. Its own correctness is checked separately against the DML's MathML,
// evaluated by docs/reviews/evidence/m7-pr11a-law/mathml_commands.py (the plan's D5). Test code
// only.
//
// evaluate() also returns every quantity the plan's Q2 margin reads before saturation, so a seeded
// frame within the margin of a saturation or of chiErr's wrap can be redrawn.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "icd/astra_icd.h"

namespace f16_control_reference {

// NOLINTBEGIN(readability-magic-numbers): F16_control.dml's constants, cited by its line numbers.

// This file's own conversions: the international foot, the exact knot, and pi/180.
inline constexpr double kFoot = 0.3048;                      // m
inline constexpr double kKnot = 1852.0 / 3600.0;             // m/s
inline constexpr double kDegree = std::numbers::pi / 180.0;  // rad

struct Inputs {
  double throttle = 0.0;  // pilotControl_throttle, frac
  double long_stk = 0.0;
  double lat_stk = 0.0;
  double pedal = 0.0;
  double sas_on = 1.0;
  double ap_on = 1.0;
  double keas_cmd = 0.0;      // KEAS
  double alt_cmd = 0.0;       // ft
  double lat_offset = 0.0;    // ft, +RT
  double base_chi_cmd = 0.0;  // deg
  double alt_msl = 0.0;       // ft
  double v_equiv = 0.0;       // KEAS
  double alpha = 0.0;         // deg
  double beta = 0.0;
  double phi = 0.0;
  double theta = 0.0;
  double psi = 0.0;
  double pb = 0.0;  // rad/s
  double qb = 0.0;
  double rb = 0.0;
  double throttle_trim = 0.1390191130965607;  // :226-235's defaults
  double long_stk_trim = 0.1296382327486013;
  // The LQR's design references (:471-487); the peer's decided ones, the trim's (the M7 plan's Q11
  // as amended 2026-10-13), override them.
  double trimmed_alpha = 2.653813535191715;  // deg
  double trimmed_theta = 2.653813535191715;  // deg
  double trimmed_keas = 2.878088596053291e+02;
};

// The DML's outputs, and the quantities before each saturation or switch.
struct Outputs {
  double el = 0.0;   // deg, +TED
  double ail = 0.0;  // deg, +LWD
  double rdr = 0.0;  // deg, +TEL
  double pwr = 0.0;  // percent
  // Before saturation (the plan's Q2 margin).
  double delta_theta_cmd = 0.0;    // deg, limit +-5
  double delta_chi_cmd = 0.0;      // deg, limit +-30
  double chi_err_unwrapped = 0.0;  // deg, wrap at +-180
  double phi_cmd = 0.0;            // deg, limit +-30
  double tot_long_stk = 0.0;       // limit +-1
  double tot_lat_stk = 0.0;
  double tot_pedal = 0.0;
  double tot_throttle = 0.0;  // limit 0 and 1
};

inline double limit(double x, double lo, double hi) { return std::min(std::max(x, lo), hi); }

// One planted defect at a time, for the plan's Q2 deviation (Joe's item 3): each must still fail
// the comparison by at least 1e8 times its bound. All default to the DML as written.
struct Plant {
  double long_theta_gain_factor = 1.0;  // a deg <-> rad slip on longLQR14 (pass kDegree)
  double altitude_gain_factor = 1.0;    // a ft <-> m slip on the altitude gain (pass kFoot)
};

inline Outputs evaluate(const Inputs& in, const Plant& plant = {}) {
  Outputs out;
  const double throttle = limit(in.throttle, 0.0, 1.0);
  const double long_stk = limit(in.long_stk, -1.0, 1.0);
  const double lat_stk = limit(in.lat_stk, -1.0, 1.0);
  const double pedal = limit(in.pedal, -1.0, 1.0);
  const double fsas_on = in.sas_on + in.ap_on;  // :242

  // Altitude autopilot (:251-301).
  const double alt_err = in.alt_msl - in.alt_cmd;
  out.delta_theta_cmd = alt_err * -0.05 * plant.altitude_gain_factor;
  const double delta_theta_cmd = limit(out.delta_theta_cmd, -5.0, 5.0);
  const double theta_cmd = delta_theta_cmd + in.trimmed_theta;

  // Course autopilot (:319-470).
  const double chi_est = in.beta + in.psi;
  out.delta_chi_cmd = -0.01 * in.lat_offset;
  const double chi_cmd = limit(out.delta_chi_cmd, -30.0, 30.0) + in.base_chi_cmd;
  out.chi_err_unwrapped = chi_est - chi_cmd;
  double chi_err = out.chi_err_unwrapped;
  if (std::abs(chi_err) > 180.0) {
    chi_err = chi_err > 0.0 ? chi_err - 360.0 : chi_err + 360.0;
  }
  out.phi_cmd = -10.0 * chi_err;
  const double phi_cmd = limit(out.phi_cmd, -30.0, 30.0);

  // LQR input selection (:488-566).
  const bool ap = in.ap_on > 0.5;
  const double keas_sw = ap ? in.keas_cmd : in.trimmed_keas;
  const double theta_sw = ap ? theta_cmd : in.trimmed_theta;
  const double phi_sw = ap ? phi_cmd : 0.0;
  const double d_v = in.v_equiv - keas_sw;
  const double d_alpha = in.alpha - in.trimmed_alpha;
  const double d_phi = in.phi - phi_sw;
  const double d_theta = in.theta - theta_sw;

  // The LQR (:571-853).
  const double long_lqr =
      -(-0.063009074230494 * d_v + 0.113230403179271 * d_alpha + 10.113432224566077 * in.qb +
        3.154983341632913 * plant.long_theta_gain_factor * d_theta);
  const double throttle_lqr = -(0.997260602961658 * d_v + -0.025467711176391 * d_alpha +
                                1.213308488207827 * in.qb + 0.208744369535208 * d_theta);
  const double lat_lqr = -(3.078043941515770 * d_phi + 0.032365863044163 * in.beta +
                           4.557858908828332 * in.pb + 0.589443156647647 * in.rb);
  const double dir_lqr = -(-0.705817452754520 * d_phi + -0.256362860634868 * in.beta +
                           -1.073666149713151 * in.pb + 0.822114635953878 * in.rb);

  // The engage and limit-and-scale block (:854-1185).
  const bool sas = fsas_on > 0.5;
  const bool pilot = in.ap_on < 0.5;
  out.tot_long_stk = in.long_stk_trim + (pilot ? long_stk : 0.0) + (sas ? long_lqr : 0.0);
  out.tot_lat_stk = (pilot ? lat_stk : 0.0) + (sas ? lat_lqr : 0.0);
  out.tot_pedal = (pilot ? pedal : 0.0) + (sas ? dir_lqr : 0.0);
  out.tot_throttle = in.throttle_trim + (pilot ? throttle : 0.0) + (sas ? throttle_lqr : 0.0);
  const double tot_long = limit(out.tot_long_stk, -1.0, 1.0);
  const double tot_lat = limit(out.tot_lat_stk, -1.0, 1.0);
  const double tot_pedal = limit(out.tot_pedal, -1.0, 1.0);
  const double tot_throttle = limit(out.tot_throttle, 0.0, 1.0);
  out.el = -25.0 * tot_long;
  out.ail = -21.5 * tot_lat;
  out.rdr = -30.0 * tot_pedal + 0.008 * out.ail;
  out.pwr = 100.0 * tot_throttle;
  return out;
}

// --- This file's own route from an ICD v1 frame to the DML's inputs ---

// EAS in knots from the frame's cas and mach: the subsonic pitot relation with gamma = 1.4 and the
// ICD's stated sea-level reference (icd/astra_icd.h: p0 = 101325 Pa, a0 = sqrt(1.4 * (8314.32 /
// 28.9644) * 288.15) m/s), solved for p/p0 and multiplied out as EAS = a0 M sqrt(p/p0).
inline double keas_from(double cas, double mach) {
  const double a0 = std::sqrt(1.4 * (8314.32 / 28.9644) * 288.15);
  const double qc_over_p0 = std::pow(1.0 + 0.2 * (cas / a0) * (cas / a0), 3.5) - 1.0;
  const double qc_over_p = std::pow(1.0 + 0.2 * mach * mach, 3.5) - 1.0;
  return a0 * mach * std::sqrt(qc_over_p0 / qc_over_p) / kKnot;
}

// The body rate relative to the Earth: (p, q, r) minus the Earth rate in body axes, with the 3-2-1
// DCM written out (S&L eq. 1.3-10) and Vol II Table 73's 7.292115e-5 rad/s.
inline void earth_relative_rate(const astra_sim_to_sw_v1& f, double& p, double& q, double& r) {
  const double w = 7.292115e-5;
  const double wn = w * std::cos(f.lat);
  const double wd = -w * std::sin(f.lat);
  const double cph = std::cos(f.phi);
  const double sph = std::sin(f.phi);
  const double cth = std::cos(f.theta);
  const double sth = std::sin(f.theta);
  const double cps = std::cos(f.psi);
  const double sps = std::sin(f.psi);
  // Row i of C_nb times (wn, 0, wd).
  const double wx = cth * cps * wn + (-sth) * wd;
  const double wy = (sph * sth * cps - cph * sps) * wn + sph * cth * wd;
  const double wz = (cph * sth * cps + sph * sps) * wn + cph * cth * wd;
  p = f.p - wx;
  q = f.q - wy;
  r = f.r - wz;
}

// The DML's inputs from a frame, commands in the DML's units, and the trimmed controls.
inline Inputs from_frame(const astra_sim_to_sw_v1& f, double keas_cmd, double alt_cmd_ft,
                         double base_chi_cmd_deg, double lat_offset_ft, double throttle_trim,
                         double long_stk_trim) {
  Inputs in;
  in.keas_cmd = keas_cmd;
  in.alt_cmd = alt_cmd_ft;
  in.base_chi_cmd = base_chi_cmd_deg;
  in.lat_offset = lat_offset_ft;
  in.throttle_trim = throttle_trim;
  in.long_stk_trim = long_stk_trim;
  in.alt_msl = f.altitude / kFoot;
  in.v_equiv = keas_from(f.cas, f.mach);
  in.alpha = f.alpha / kDegree;
  in.beta = f.beta / kDegree;
  in.phi = f.phi / kDegree;
  in.theta = f.theta / kDegree;
  in.psi = f.psi / kDegree;
  earth_relative_rate(f, in.pb, in.qb, in.rb);
  return in;
}

// The ICD commands: rad, the aileron's sign flipped to +RWD, PLA percent to a 0..1 throttle.
struct Icd {
  double elevator = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;
  double throttle = 0.0;
};

inline Icd to_icd(const Outputs& out) {
  return Icd{out.el * kDegree, -out.ail * kDegree, out.rdr * kDegree, out.pwr / 100.0};
}

// NOLINTEND(readability-magic-numbers)

}  // namespace f16_control_reference
