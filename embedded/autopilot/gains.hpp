// The autopilot's gains (docs/plans/m4-pr5-autopilot.md D3, D7).
// tests/python/test_autopilot_design.py reads THIS FILE, so the design it analyzes is the design
// that ships: keep every gain one `constexpr double kName = value;` line (no expressions, no other
// constants), and change a value only together with a rerun of that test.
//
// Units: rad, m, m/s, s. Elevator (positive = trailing-edge-down = nose-down):
//   delta_e = trim - (kAltP*e_h + kAltI*int(e_h)) + kAltD*vertical_speed + kPitchTheta*(theta -
//   theta_trim)
//             + kPitchRate*q,                     e_h = altitude_cmd - altitude.
// Throttle:  throttle = trim + kSpeedP*e_v + kSpeedI*int(e_v),  e_v = airspeed_cmd - tas.
// Aileron (positive = right wing down):
//   phi_c = kHeadingP*e_psi + kHeadingI*int(e_psi)  (clamped), delta_a = kBankP*(phi_c - phi) -
//   kRollRate*p.
// Rudder (positive = trailing-edge-left = nose-LEFT, a NEGATIVE yaw moment: cn_rudder = -0.20):
//   delta_r = kYawRate*r, a pure yaw-rate damper (r > 0 gives nose-left rudder, which opposes it).
// There is deliberately no sideslip term. beta feedback with the wrong sign (rudder nose-left for
// beta > 0, i.e. delta_r = +k*beta) subtracts from the weathervane stability, cn_beta_eff = cn_beta
// + cn_rudder*k (0.26 - 0.2*k); only k <= 0 stiffens it, and the search showed the floors are met
// without it (evidence, correction round 1).
#ifndef ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP

namespace astra_autopilot {

// Provenance (plan D7, evidence docs/reviews/evidence/m4-pr5-gain-design.md): a Nelder-Mead search
// on the tests/python/autopilot_design.py models. Longitudinal gains (round 0) meet stricter
// targets than the test asserts (largest real part -0.05 1/s, damping 0.4, phase margin 60 deg,
// gain margin 10 dB). Lateral gains were re-searched in correction round 1 against those targets
// PLUS an inner-loop criterion (the bank-attitude loop alone has every pole at Re <= -0.6 1/s, a
// time constant of at most 1.7 s), with no small-gain preference and kBankP capped at 0.6 so a full
// 0.35 rad bank error asks for at most 60 percent of the aileron limit. kHeadingP = 2.5 is the
// smallest value tried (1.5, 2.0, 2.5, 3.0, 4.0) that meets every target; 2.0 misses the real-part
// target (-0.043), and a larger value only makes the bank command (clamped at 0.35 rad) saturate at
// a smaller heading error (0.35 / kHeadingP: 8 degrees at 2.5). Rounded to three figures.
constexpr double kAltP = 1.54e-4;
constexpr double kAltI = 7.97e-6;
constexpr double kAltD = 2.24e-4;
constexpr double kPitchTheta = 0.389;
constexpr double kPitchRate = 0.184;
constexpr double kSpeedP = 0.0353;
constexpr double kSpeedI = 7.69e-4;
constexpr double kHeadingP = 2.5;
constexpr double kHeadingI = 0.0816;
constexpr double kBankP = 0.394;
constexpr double kRollRate = 0.360;
constexpr double kYawRate = 0.494;

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP
