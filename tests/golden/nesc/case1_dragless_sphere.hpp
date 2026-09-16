// NASA/TM-2015-218675 Volume II, Table 25 (atmospheric check-case 1, "dragless sphere") oracle.
// Human-owned: lives under tests/golden/ so guard-oracle.sh blocks agent edits outside a session
// Joe armed with the touch-goldens marker.
//
// Scenario, verbatim from the report: geodesy WGS-84 rotating; gravitation J2; atmosphere US 1976
// STD with no wind (irrelevant here -- CD is set to zero, so the sphere has no reaction with the
// atmosphere at all); duration 30 s. Initial position 30,000 ft MSL over the Equator/Prime Meridian
// intersection; initial velocity matched the Earth's eastward rotation so it was motionless
// relative to the still atmosphere (i.e. v_b = 0, exactly ADR-0003's "velocity relative to the
// Earth-fixed frame"); level attitude (body x north, y east, z down); **zero inertial rotation**
// (the report's own words) -- so omega_b = 0 under ADR-0003 (omega_b is relative to inertial). The
// report's own tabulated body-axis rate, -0.004178073 deg/s, is *not* this scenario's omega_b: it
// is the rate *relative to the rotating Earth* that zero inertial rotation implies, verified this
// session to be exactly -omega_ie in deg/s (omega_ie = 7.292115e-5 rad/s = 0.004178073 deg/s) --
// the same "omega_b is inertial-relative, not Earth-relative" distinction the M2 PR 1 plan review
// caught in the opposite case (a body at rest *relative to the Earth*).
//
// The report deliberately does not publish a single numeric reference trajectory or a fixed
// tolerance: Volume I's "Known Risks and Mitigations" section describes presenting comparison
// plots across independently developed simulation tools as a verification guide, explicitly
// instead of requiring agreement to a predefined tolerance. So kExpectedFinalState below is an
// INDEPENDENT reference this session computed for exactly this scenario -- Python + scipy
// solve_ivp, method DOP853, rtol=1e-13, atol=1e-14, a from-scratch derivative function following
// the same public equations (Stevens & Lewis EOM form; Groves' J2 gravity, eq 2.142) but no ASTRA
// code -- so the test judges ASTRA's implementation against that independent integration, not
// against itself. Internal consistency, checked before trusting this reference: latitude stays
// exactly 0 (equatorial symmetry, since v_N and any north-south force are identically zero
// throughout); the fall distance (~4389 m over 30 s) and the final down-velocity (~293 m/s) both
// match g*t and 0.5*g*t^2 for g ~ 9.76 m/s^2 (equatorial gravity between sea level and 9144 m); the
// small accumulated roll (~0.1254 deg) matches omega_ie * 30 s to three figures, exactly the
// "small residual rotation relative to the Earth" the report's own prose describes.
#pragma once

namespace astra::test::nesc {

inline constexpr double kAltitudeFeet = 30000.0;
inline constexpr double kFeetToMetres = 0.3048;
inline constexpr double kDuration = 30.0;

// NOLINTBEGIN(modernize-avoid-c-arrays): a data record; C arrays keep the braces one level deep.
struct FinalState {
  double latitude;    // rad
  double longitude;   // rad
  double altitude;    // m
  double v_b[3];      // m/s
  double q_nb[4];     // w, x, y, z
  double omega_b[3];  // rad/s
};

inline constexpr FinalState kExpectedFinalState = {
    0.0,
    1.0027827850208136e-06,
    4754.54604547749,
    {0.0, -0.00022112366935873903, 292.6980266831141},
    {0.9999994012334155, -0.0010943184229786893, 0.0, 0.0},
    {0.0, 0.0, 0.0},
};
// NOLINTEND(modernize-avoid-c-arrays)

// Tolerances against the independent integration above -- not a NASA-published number (see
// header comment), so set from this integration's own precision (rtol=1e-13 over a smooth, 30 s,
// non-stiff trajectory) with generous margin for ASTRA's fixed-step RK4 at 1 kHz.
inline constexpr double kAngleTolerance = 1e-8;     // rad (latitude, longitude)
inline constexpr double kAltitudeTolerance = 1e-3;  // m
inline constexpr double kVelocityTolerance = 1e-4;  // m/s
inline constexpr double kQuaternionTolerance = 1e-6;
inline constexpr double kOmegaTolerance = 1e-9;  // rad/s

}  // namespace astra::test::nesc
