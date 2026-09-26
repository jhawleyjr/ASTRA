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

// PLACEHOLDER values from the D7 feasibility trial; frozen from the design step (plan sequence step
// 5).
constexpr double kAltP = 0.00029;
constexpr double kAltI = 0.00002;
constexpr double kAltD = 0.00127;
constexpr double kPitchTheta = 0.23;
constexpr double kPitchRate = 0.42;
constexpr double kSpeedP = 0.067;
constexpr double kSpeedI = 0.0024;
constexpr double kHeadingP = 1.987;
constexpr double kHeadingI = 0.0423;
constexpr double kBankP = 0.124;
constexpr double kRollRate = 0.468;
constexpr double kYawRate = 0.22;
constexpr double kSideslip = 0.528;

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_GAINS_HPP
