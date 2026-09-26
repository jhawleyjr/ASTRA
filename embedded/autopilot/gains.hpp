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
// Rudder (positive = nose-left):  delta_r = kYawRate*r + kSideslip*beta.
#ifndef ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP

namespace astra_autopilot {

// Provenance (plan D7, evidence docs/reviews/evidence/m4-pr5-gain-design.md): a Nelder-Mead search
// on the tests/python/autopilot_design.py models against stricter targets than the test asserts
// (largest real part -0.05 1/s, damping 0.4, phase margin 60 deg, gain margin 10 dB), then rounded
// to three figures. kHeadingP is NOT at an arbitrary search bound: with the other lateral gains
// re-optimized, kHeadingP = 1.5 misses those targets (largest real part -0.040) and 2.0 is the
// smallest tried value that meets them, while a larger value only makes the bank command (clamped
// at 0.35 rad) saturate at a smaller heading error (0.35 / kHeadingP: 10 degrees at 2.0, 6.7
// at 3.0).
constexpr double kAltP = 1.54e-4;
constexpr double kAltI = 7.97e-6;
constexpr double kAltD = 2.24e-4;
constexpr double kPitchTheta = 0.389;
constexpr double kPitchRate = 0.184;
constexpr double kSpeedP = 0.0353;
constexpr double kSpeedI = 7.69e-4;
constexpr double kHeadingP = 2.0;
constexpr double kHeadingI = 0.0343;
constexpr double kBankP = 0.0550;
constexpr double kRollRate = 0.243;
constexpr double kYawRate = 0.359;
constexpr double kSideslip = 0.269;

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP
