// NASA's F16_control.dml (Revision F, 2013-10-22; docs/plans/m7-f16-inventory.md section 5), the
// NESC check cases 13.1 to 13.4's autopilot, as an SI realization (ADR-0029 Decisions 2, 5 and 7;
// docs/plans/m7-pr11a-control-peer.md). The peer (nesc_control_peer.hpp) runs it on each packed
// astra_sim_to_sw_v1 frame; the F-16 tests compare it with an independent evaluation of the DML in
// its own units (aircraft/f16/tests/control_reference.hpp).
//
// Every quantity here is SI: angles in rad, rates in rad/s, speeds in m/s, lengths in m. The DML
// states its gains and limits in deg, ft, KEAS and frac; each one that carries a unit is written
// below as the DML's value times a named core/util/units conversion, where it is declared, never as
// a premultiplied number (Q26). KEAS converts with the exact knot. The sticks stay dimensionless
// (frac), and the law keeps the DML's PLA in percent until the boundary.
//
// The law (each DML varID beside its SI name):
// - fsasOn = sasOn + apOn > 0.5: Rev F's OR (:242).
// - altitude: deltaThetaCmd = -0.05 deg/ft * (altMsl - altCmd), limited to +-5 deg;
//   thetaCmd = deltaThetaCmd + trimmedTheta.
// - course: chiEst = beta + psi; deltaChiCmd = -0.01 deg/ft * latOffset, limited to +-30 deg;
//   chiCmd = deltaChiCmd + baseChiCmd; chiErr = chiEst - chiCmd, wrapped by one turn when its
//   magnitude exceeds a half turn; phiCmd = -10 deg/deg * chiErr, limited to +-30 deg.
// - the LQR's references switch on apOn: (keasCmd, thetaCmd, phiCmd), else (trimmedKEAS,
//   trimmedTheta, 0). trimmedAlpha always feeds deltaAlpha.
// - longLQR, throttleLQR, latLQR and dirLQR: each the negated sum, in the DML's order, of its four
//   gains times (deltaV, deltaAlpha, q, deltaTheta) or (deltaPhi, beta, p, r).
// - the engage block: the LQR terms pass when fsasOn > 0.5, the pilot's when apOn < 0.5.
//   totLongStk, totLatStk and totPedal are limited to [-1, 1], totThrottle to [0, 1].
// - the outputs: el = -25 deg * totLongStk (+TED); ail = -21.5 deg * totLatStk (+LWD); rdr = -30
//   deg * totPedal + 0.008 * ail (+TEL; the interconnect reads the +LWD aileron); PWR = 100 *
//   totThrottle (percent).
// Every DAVE-ML minValue/maxValue is a saturation, inputs included (addendum section 8).
//
// The three boundary steps, and only three (ADR-0029 Decision 5), in to_reply():
// - the aileron's sign, from the DML's +LWD to ICD v1's right wing down, after the interconnect;
// - PLA percent to a 0..1 throttle, written to both throttle entries;
// - the sticks stay dimensionless (no conversion).
//
// The law's inputs from the frame (ADR-0029 Decisions 2 and 7):
// - EAS from cas and mach (equivalent_airspeed below), for unflagged subsonic frames only;
// - the body rate relative to the Earth, omega_eb = (p, q, r) - C_nb Omega (cos lat, 0, -sin lat),
//   with C_nb from the frame's 3-2-1 Euler angles through core/util/frames and Omega =
//   geodesy::kOmegaIe. The peer cannot see the configured Earth model, so it assumes a rotating
//   WGS-84 Earth (the plan's Q4): under a non-rotating geodetic Earth it still subtracts Omega.
//   EarthRate::kNone subtracts nothing; only tests/plugins/nesc_control_variant selects it (the M7b
//   plan's Q10).
//
// Pure functions of their arguments: no Config, no allocation, no exceptions, no clock.
#pragma once

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "astra/util/frames.hpp"
#include "astra/util/geodesy.hpp"
#include "astra/util/icd_pack.hpp"
#include "astra/util/units.hpp"
#include "icd/astra_icd.h"

namespace astra::aircraft_f16_control_law {

namespace units = astra::units;

// NOLINTBEGIN(readability-magic-numbers): each literal below is a constant of F16_control.dml,
// cited by line, or of the plan's refusal-bit layout.

// --- The altitude loop (:251-301) ---
inline constexpr double kAltitudeErrorGain =
    units::degrees_to_radians(-0.05) / units::feet_to_meters(1.0);  // rad/m (-0.05 deg/ft)
inline constexpr double kDeltaThetaLimit = units::degrees_to_radians(5.0);

// --- The course loop (:319-470) ---
inline constexpr double kLateralOffsetGain =
    units::degrees_to_radians(-0.01) / units::feet_to_meters(1.0);  // rad/m (-0.01 deg/ft)
inline constexpr double kDeltaChiLimit = units::degrees_to_radians(30.0);
inline constexpr double kTrackErrorGain = -10.0;  // deg/deg, so rad/rad
inline constexpr double kPhiLimit = units::degrees_to_radians(30.0);
inline constexpr double kHalfTurn = units::degrees_to_radians(180.0);
inline constexpr double kFullTurn = units::degrees_to_radians(360.0);

// --- The LQR's references. The defaults are the DML's hard-coded values (:471-487; trimmedKEAS's
// units are written "nim_h", the file's typo for nmi_h, addendum section 5). The peer sets them
// from the trim instead (the M7 plan's Q11 as amended 2026-10-13, nesc_control_peer.hpp), and uses
// these defaults only in Q11's reported variant ---
struct References {
  double alpha = units::degrees_to_radians(2.653813535191715);            // trimmedAlpha
  double theta = units::degrees_to_radians(2.653813535191715);            // trimmedTheta
  double eas = units::knots_to_meters_per_second(2.878088596053291e+02);  // trimmedKEAS
};

// --- The LQR gains (:571-655). h_nmi is per knot, _deg per degree, s_r is s/rad (unchanged) ---
struct Row {
  double first;   // deltaV (per m/s) or deltaPhi (per rad)
  double second;  // deltaAlpha or beta, per rad
  double third;   // q or p, s/rad
  double fourth;  // deltaTheta (per rad) or r (s/rad)
};
inline constexpr double kPerKnot = 1.0 / units::knots_to_meters_per_second(1.0);  // s/m per (1/kt)
inline constexpr double kPerDegree = 1.0 / units::degrees_to_radians(1.0);        // per rad
inline constexpr Row kLong = {-0.063009074230494 * kPerKnot, 0.113230403179271 * kPerDegree,
                              10.113432224566077, 3.154983341632913 * kPerDegree};
inline constexpr Row kThrottle = {0.997260602961658 * kPerKnot, -0.025467711176391 * kPerDegree,
                                  1.213308488207827, 0.208744369535208 * kPerDegree};
inline constexpr Row kLat = {3.078043941515770 * kPerDegree, 0.032365863044163 * kPerDegree,
                             4.557858908828332, 0.589443156647647};
inline constexpr Row kDir = {-0.705817452754520 * kPerDegree, -0.256362860634868 * kPerDegree,
                             -1.073666149713151, 0.822114635953878};

// --- The mixer (:1117-1185) and the PLA scale ---
inline constexpr double kElevatorPerStick = units::degrees_to_radians(-25.0);  // rad, +TED
inline constexpr double kAileronPerStick = units::degrees_to_radians(-21.5);   // rad, +LWD
inline constexpr double kRudderPerPedal = units::degrees_to_radians(-30.0);    // rad, +TEL
inline constexpr double kAileronToRudder = 0.008;                              // deg/deg
inline constexpr double kPlaPerThrottle = 100.0;                               // percent

// The law's own saturated output magnitudes, in the same expressions the outputs above reach at a
// saturated total: the exchange limits in the 13.x configs are the %.17g text of these (the plan's
// D9, amendment 3 with its 2026-10-13 wording fix).
inline constexpr double kElevatorRange = units::degrees_to_radians(25.0);
inline constexpr double kAileronRange = units::degrees_to_radians(21.5);
inline constexpr double kRudderRange =
    units::degrees_to_radians(30.0) + 0.008 * units::degrees_to_radians(21.5);

// --- The refusal bits (ADR-0029 Decision 2; the plan's Q8, amendment 7). The ICD's own status bits
// keep their values; the peer adds two of its own, above ICD v1's highest status bit. ---
inline constexpr std::uint32_t kRefusedStatusBits =
    ASTRA_ICD_STATUS_LATLON_NOT_MODELED | ASTRA_ICD_STATUS_ATMOSPHERE_INVALID |
    ASTRA_ICD_STATUS_CAS_SUPERSONIC | ASTRA_ICD_STATUS_WIND_INVALID;
inline constexpr std::uint32_t kAllIcdStatusBits =
    ASTRA_ICD_STATUS_ACCEL_INVALID | ASTRA_ICD_STATUS_LATLON_NOT_MODELED |
    ASTRA_ICD_STATUS_ATMOSPHERE_INVALID | ASTRA_ICD_STATUS_CAS_SUPERSONIC |
    ASTRA_ICD_STATUS_WIND_INVALID;
inline constexpr std::uint32_t kRefusalMachNotPositive = 0x100U;
inline constexpr std::uint32_t kRefusalBasePointFrame = 0x200U;
static_assert((kRefusalMachNotPositive & kAllIcdStatusBits) == 0U &&
                  (kRefusalBasePointFrame & kAllIcdStatusBits) == 0U &&
                  (kRefusalMachNotPositive & ASTRA_ICD_STATUS_ACCEL_INVALID) == 0U &&
                  (kRefusalMachNotPositive & ASTRA_ICD_STATUS_LATLON_NOT_MODELED) == 0U &&
                  (kRefusalMachNotPositive & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID) == 0U &&
                  (kRefusalMachNotPositive & ASTRA_ICD_STATUS_CAS_SUPERSONIC) == 0U &&
                  (kRefusalMachNotPositive & ASTRA_ICD_STATUS_WIND_INVALID) == 0U &&
                  (kRefusalBasePointFrame & ASTRA_ICD_STATUS_ACCEL_INVALID) == 0U &&
                  (kRefusalBasePointFrame & ASTRA_ICD_STATUS_LATLON_NOT_MODELED) == 0U &&
                  (kRefusalBasePointFrame & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID) == 0U &&
                  (kRefusalBasePointFrame & ASTRA_ICD_STATUS_CAS_SUPERSONIC) == 0U &&
                  (kRefusalBasePointFrame & ASTRA_ICD_STATUS_WIND_INVALID) == 0U &&
                  kRefusalMachNotPositive > kAllIcdStatusBits &&
                  kRefusalBasePointFrame > kAllIcdStatusBits,
              "the peer's refusal bits must not overlap any ASTRA_ICD_STATUS_* mask");

// NOLINTEND(readability-magic-numbers)

// The autopilot's commands, SI: the DML's altitudeMslCommand, equivalentAirspeedCommand,
// trueBaseCourseCommand and lateralDeviationError (+RT).
struct Commands {
  double altitude = 0.0;                 // m
  double eas = 0.0;                      // m/s
  double course = 0.0;                   // rad, true
  double lateral_deviation_error = 0.0;  // m, positive right of the desired course
};

// The DML's discrete and pilot inputs. The peer always engages both (Vol II C.1.13 to C.1.16) with
// the pilot's inputs at 0 (Vol II p.16: "No pilot inputs were used"); R23 drives both off.
struct Modes {
  bool sas_on = true;
  bool ap_on = true;
  double throttle = 0.0;  // pilotControl_throttle, frac
  double long_stick = 0.0;
  double lat_stick = 0.0;
  double pedal = 0.0;
};

// trimmedPilotControl_long and trimmedPilotControl_throttle (inputs since Mod D).
struct Trim {
  double long_stick = 0.0;
  double throttle = 0.0;
};

// The law's SI inputs from one frame.
struct Inputs {
  double altitude = 0.0;  // m, the Earth model's altitude (the DML's altitudeMsl)
  double eas = 0.0;       // m/s
  double alpha = 0.0;
  double beta = 0.0;
  double phi = 0.0;
  double theta = 0.0;
  double psi = 0.0;
  double p = 0.0;  // rad/s, relative to the Earth (ADR-0029 Decision 7)
  double q = 0.0;
  double r = 0.0;
};

// The law's outputs in its own conventions: surfaces in rad (aileron +LWD), PLA in percent.
struct Outputs {
  double elevator = 0.0;     // +TED
  double aileron_lwd = 0.0;  // +LWD
  double rudder = 0.0;       // +TEL
  double pla_percent = 0.0;
};

// The ICD v1 commands: aileron positive right wing down, throttle 0..1 (icd/astra_icd.h).
struct IcdCommands {
  double elevator = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;
  double throttle = 0.0;
};

[[nodiscard]] inline double saturate(double x, double lo, double hi) noexcept {
  return std::min(std::max(x, lo), hi);
}

// The DML's piecewise wrap of chiErrUnwrapped (:385-452): one turn off when |x| exceeds a half
// turn.
[[nodiscard]] inline double wrap_track_error(double unwrapped) noexcept {
  if (std::abs(unwrapped) > kHalfTurn) {
    return unwrapped > 0.0 ? unwrapped - kFullTurn : unwrapped + kFullTurn;
  }
  return unwrapped;
}

// The negated sum of one LQR row, in the DML's order (:714-853).
[[nodiscard]] inline double lqr(const Row& k, double a, double b, double c, double d) noexcept {
  return -(k.first * a + k.second * b + k.third * c + k.fourth * d);
}

[[nodiscard]] inline Outputs evaluate(const Inputs& in, const Commands& cmd, const Trim& trim,
                                      const Modes& modes = {},
                                      const References& ref = {}) noexcept {
  const bool fsas_on = modes.sas_on || modes.ap_on;
  const bool ap_on = modes.ap_on;

  // The pilot's inputs are saturated as their minValue/maxValue say.
  const double pilot_throttle = saturate(modes.throttle, 0.0, 1.0);
  const double pilot_long = saturate(modes.long_stick, -1.0, 1.0);
  const double pilot_lat = saturate(modes.lat_stick, -1.0, 1.0);
  const double pilot_pedal = saturate(modes.pedal, -1.0, 1.0);

  const double altitude_error = in.altitude - cmd.altitude;
  const double delta_theta_cmd =
      saturate(altitude_error * kAltitudeErrorGain, -kDeltaThetaLimit, kDeltaThetaLimit);
  const double theta_cmd = delta_theta_cmd + ref.theta;

  const double chi_est = in.beta + in.psi;
  const double delta_chi_cmd =
      saturate(kLateralOffsetGain * cmd.lateral_deviation_error, -kDeltaChiLimit, kDeltaChiLimit);
  const double chi_cmd = delta_chi_cmd + cmd.course;
  const double chi_err = wrap_track_error(chi_est - chi_cmd);
  const double phi_cmd = saturate(kTrackErrorGain * chi_err, -kPhiLimit, kPhiLimit);

  const double eas_ref = ap_on ? cmd.eas : ref.eas;
  const double theta_ref = ap_on ? theta_cmd : ref.theta;
  const double phi_ref = ap_on ? phi_cmd : 0.0;

  const double delta_v = in.eas - eas_ref;
  const double delta_alpha = in.alpha - ref.alpha;
  const double delta_phi = in.phi - phi_ref;
  const double delta_theta = in.theta - theta_ref;

  const double long_lqr = fsas_on ? lqr(kLong, delta_v, delta_alpha, in.q, delta_theta) : 0.0;
  const double throttle_lqr =
      fsas_on ? lqr(kThrottle, delta_v, delta_alpha, in.q, delta_theta) : 0.0;
  const double lat_lqr = fsas_on ? lqr(kLat, delta_phi, in.beta, in.p, in.r) : 0.0;
  const double dir_lqr = fsas_on ? lqr(kDir, delta_phi, in.beta, in.p, in.r) : 0.0;

  const double long_pilot = ap_on ? 0.0 : pilot_long;
  const double throttle_pilot = ap_on ? 0.0 : pilot_throttle;
  const double lat_pilot = ap_on ? 0.0 : pilot_lat;
  const double pedal_pilot = ap_on ? 0.0 : pilot_pedal;

  const double total_long = saturate(trim.long_stick + long_pilot + long_lqr, -1.0, 1.0);
  const double total_lat = saturate(lat_pilot + lat_lqr, -1.0, 1.0);
  const double total_pedal = saturate(pedal_pilot + dir_lqr, -1.0, 1.0);
  const double total_throttle = saturate(trim.throttle + throttle_pilot + throttle_lqr, 0.0, 1.0);

  Outputs out;
  out.elevator = kElevatorPerStick * total_long;
  out.aileron_lwd = kAileronPerStick * total_lat;
  out.rudder = kRudderPerPedal * total_pedal + kAileronToRudder * out.aileron_lwd;
  out.pla_percent = kPlaPerThrottle * total_throttle;
  return out;
}

// The three boundary steps (ADR-0029 Decision 5).
[[nodiscard]] inline IcdCommands to_icd(const Outputs& out) noexcept {
  IcdCommands icd;
  icd.elevator = out.elevator;
  icd.aileron = -out.aileron_lwd;  // the DML's +LWD to ICD v1's right wing down
  icd.rudder = out.rudder;
  icd.throttle = out.pla_percent / kPlaPerThrottle;
  return icd;
}

// Trim's physical controls to the DML's trimmed pilot controls: the inverse of the law's own
// limit-and-scale block with SAS and autopilot off and no pilot input (Vol II p.16; R23). The
// caller checks |long_stick| <= 1 and throttle in [0, 1], where the inverse is exact.
[[nodiscard]] inline Trim trim_from_controls(double elevator, double throttle) noexcept {
  return Trim{elevator / kElevatorPerStick, throttle};
}

// EAS from ICD v1's cas and mach (ADR-0029 Decision 2): icd_pack::calibrated_airspeed inverted for
// the static pressure, then EAS = a0 M sqrt(p / p0), with the packer's own a0 and p0. Valid only
// for a frame refusal_bits() passes.
[[nodiscard]] inline double equivalent_airspeed(double cas, double mach) noexcept {
  constexpr double kHalfGammaMinusOne = 0.2;  // (gamma-1)/2, as icd_pack
  constexpr double kPitotExponent = 3.5;      // gamma/(gamma-1)
  const double a0 = icd_pack::sea_level_speed_of_sound();
  const double cas_ratio = cas / a0;
  const double impact_over_p0 =
      std::pow(1.0 + kHalfGammaMinusOne * cas_ratio * cas_ratio, kPitotExponent) - 1.0;
  const double impact_over_p =
      std::pow(1.0 + kHalfGammaMinusOne * mach * mach, kPitotExponent) - 1.0;
  const double p_over_p0 = impact_over_p0 / impact_over_p;
  return a0 * mach * std::sqrt(p_over_p0);
}

// What the law subtracts from ICD v1's inertial body rate (ADR-0029 Decision 7; the M7b plan's Q10,
// docs/plans/m7b-pr4-f16-symmetry.md section 2). The shipped peer uses only kWgs84Rotating.
enum class EarthRate : std::uint8_t {
  kWgs84Rotating,  // geodesy::kOmegaIe, under any Earth model (the M7 plan's Q4)
  kNone,           // nothing: tests/plugins/nesc_control_variant only (M7b PR 4)
};

// The body rate relative to the Earth from the frame alone (ADR-0029 Decision 7).
[[nodiscard]] inline Eigen::Vector3d earth_relative_rate(const astra_sim_to_sw_v1& frame) noexcept {
  const Eigen::Matrix3d c_nb =
      frames::dcm_from_euler(frames::EulerAngles{frame.psi, frame.theta, frame.phi});
  const Eigen::Vector3d omega_ie_n{geodesy::kOmegaIe * std::cos(frame.lat), 0.0,
                                   -geodesy::kOmegaIe * std::sin(frame.lat)};
  return Eigen::Vector3d{frame.p, frame.q, frame.r} - c_nb * omega_ie_n;
}

// The refusal bits for a frame: the refused ICD status bits it carries (`refused`, by default
// kRefusedStatusBits), plus kRefusalMachNotPositive when Mach is not above zero. 0 means the law
// may run.
[[nodiscard]] inline std::uint32_t refusal_bits(
    const astra_sim_to_sw_v1& frame, std::uint32_t refused = kRefusedStatusBits) noexcept {
  std::uint32_t bits = frame.status_flags & refused;
  if (!(frame.mach > 0.0)) {
    bits |= kRefusalMachNotPositive;
  }
  return bits;
}

// The law's inputs from an unrefused frame. kNone reads neither lat nor lon.
[[nodiscard]] inline Inputs inputs_from(const astra_sim_to_sw_v1& frame,
                                        EarthRate earth_rate = EarthRate::kWgs84Rotating) noexcept {
  const Eigen::Vector3d omega_eb = earth_rate == EarthRate::kWgs84Rotating
                                       ? earth_relative_rate(frame)
                                       : Eigen::Vector3d{frame.p, frame.q, frame.r};
  Inputs in;
  in.altitude = frame.altitude;
  in.eas = equivalent_airspeed(frame.cas, frame.mach);
  in.alpha = frame.alpha;
  in.beta = frame.beta;
  in.phi = frame.phi;
  in.theta = frame.theta;
  in.psi = frame.psi;
  in.p = omega_eb.x();
  in.q = omega_eb.y();
  in.r = omega_eb.z();
  return in;
}

// 13.4's base point and the initial-plane line through it (ADR-0029 Decision 6; the M7 plan's Q27):
// lat0, lon0 and h0 from the frame-0 exchange, M0 and N0 WGS-84's meridian and prime-vertical radii
// at lat0, and the plane's scales fixed there.
struct BasePoint {
  double lat0 = 0.0;
  double lon0 = 0.0;
  double h0 = 0.0;
  double north_scale = 0.0;  // M0 + h0, m/rad
  double east_scale = 0.0;   // (N0 + h0) cos lat0, m/rad
};

[[nodiscard]] inline BasePoint base_point(double lat0, double lon0, double h0) noexcept {
  BasePoint base;
  base.lat0 = lat0;
  base.lon0 = lon0;
  base.h0 = h0;
  base.north_scale = geodesy::meridian_radius(lat0) + h0;
  base.east_scale = (geodesy::prime_vertical_radius(lat0) + h0) * std::cos(lat0);
  return base;
}

// The signed cross-track distance from the base course at true course chi, positive to the right:
// d = -dn sin chi + de cos chi, with dn = (lat - lat0)(M0 + h0) and de = (lon - lon0)(N0 + h0) cos
// lat0. It reads only lat and lon; h0 stays fixed.
[[nodiscard]] inline double cross_track(const BasePoint& base, double chi, double lat,
                                        double lon) noexcept {
  const double dn = (lat - base.lat0) * base.north_scale;
  const double de = (lon - base.lon0) * base.east_scale;
  return -dn * std::sin(chi) + de * std::cos(chi);
}

}  // namespace astra::aircraft_f16_control_law
