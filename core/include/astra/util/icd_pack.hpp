// Packs astra_sim_to_sw_v1 (icd/astra_icd.h, ADR-0006) from the sim's truth (M4 PR 2,
// docs/plans/m4-lockstep.md PR 2 row, R12a, Q11). Header-only and inline like air_data.hpp; not
// wired into Simulation (PR 3 does that). Per-frame path: no allocation, no exceptions, no
// std::string.
//
// Inputs: the frame's StepContext (t is exactly VirtualClock::t(), ADR-0010), the frame-start
// rigid-body state, the frame's EnvironmentSample, the Earth model, and the PREVIOUS frame's
// published wrench_total and mass. Every field's definition, and every status_flags bit, is
// documented in icd/astra_icd.h; the bit macros used here are that header's, with no second copy.
#pragma once

#include <Eigen/Core>
#include <cmath>
#include <cstring>

#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/air_data.hpp"
#include "astra/util/frames.hpp"
#include "icd/astra_icd.h"

namespace astra::icd_pack {

// ISA sea-level reference (the same constants plugins/physical/atmosphere/isa1976 uses: U.S.
// Standard Atmosphere 1976 pp. 8-9) for the calibrated-airspeed relation.
inline constexpr double kSeaLevelPressure = 101325.0;    // p0, Pa
inline constexpr double kSeaLevelTemperature = 288.15;   // T0, K
inline constexpr double kGasConstant = 8314.32;          // R*, N.m/(kmol.K)
inline constexpr double kMeanMolecularWeight = 28.9644;  // M0, kg/kmol
inline constexpr double kSpecificHeatRatio = 1.4;        // gamma

// a0 = sqrt(gamma * (R* / M0) * T0) = 340.29411 m/s, the ISA sea-level speed of sound.
[[nodiscard]] inline double sea_level_speed_of_sound() noexcept {
  return std::sqrt(kSpecificHeatRatio * (kGasConstant / kMeanMolecularWeight) *
                   kSeaLevelTemperature);
}

// Calibrated airspeed from static pressure and Mach, subsonic only (Mach < 1; the caller flags the
// rest). Isentropic impact pressure qc = p * ((1 + (gamma-1)/2 M^2)^(gamma/(gamma-1)) - 1), then
// the same relation inverted at sea level: cas = a0 * sqrt(2/(gamma-1) * ((qc/p0 + 1)^((gamma-1)/
// gamma) - 1)). gamma = 1.4 gives the exponents 3.5 and 2/7 and the factors 0.2 and 5 below.
[[nodiscard]] inline double calibrated_airspeed(double static_pressure, double mach) noexcept {
  constexpr double kHalfGammaMinusOne = 0.2;      // (gamma-1)/2
  constexpr double kPitotExponent = 3.5;          // gamma/(gamma-1)
  constexpr double kInverseExponent = 2.0 / 7.0;  // (gamma-1)/gamma
  constexpr double kTwoOverGammaMinusOne = 5.0;   // 2/(gamma-1)
  const double impact_pressure =
      static_pressure * (std::pow(1.0 + kHalfGammaMinusOne * mach * mach, kPitotExponent) - 1.0);
  return sea_level_speed_of_sound() *
         std::sqrt(kTwoOverGammaMinusOne *
                   (std::pow(impact_pressure / kSeaLevelPressure + 1.0, kInverseExponent) - 1.0));
}

// The previous frame's published wrench_total (the k1-stage sum of the wrench sources, gravity
// excluded, at that frame's start state with its latched commands) and the mass in force then.
struct PreviousFrame {
  Wrench wrench_total;
  double mass = 0.0;  // kg
};

// Fills `out` completely: zeroed with memset first, so every byte is deterministic (R3), then
// every field. `previous` is null when no previous frame exists; frame 0 is invalid regardless.
inline void pack_sim_to_sw(astra_sim_to_sw_v1& out, const StepContext& ctx, const VehicleState& vs,
                           const EnvironmentSample& env, const EarthModel& earth,
                           const PreviousFrame* previous) noexcept {
  std::memset(&out, 0, sizeof out);
  out.magic = ASTRA_ICD_MAGIC;
  out.version = ASTRA_ICD_VERSION;
  out.frame = ctx.frame;
  out.t = ctx.t;

  // frames::EulerAngles is laid out (psi, theta, phi); the ICD is (phi, theta, psi).
  const frames::EulerAngles euler_nb = frames::euler_from_quaternion(vs.q_nb());
  out.psi = euler_nb(0);
  out.theta = euler_nb(1);
  out.phi = euler_nb(2);

  const Eigen::Vector3d omega_b = vs.omega_b();
  out.p = omega_b(0);
  out.q = omega_b(1);
  out.r = omega_b(2);

  const Eigen::Vector3d v_b = vs.v_b();
  out.u = v_b(0);
  out.v = v_b(1);
  out.w = v_b(2);

  out.altitude = earth.altitude(vs);
  out.vertical_speed = -frames::rotate_b_to_n(vs.q_nb(), v_b)(2);
  if (earth.position_representation() == PositionRepresentation::kGeodetic) {
    const Eigen::Vector3d p_n = vs.p_n();
    out.lat = p_n(0);
    out.lon = p_n(1);
  } else {
    out.status_flags |= ASTRA_ICD_STATUS_LATLON_NOT_MODELED;
  }

  // Air data. A non-finite wind makes the whole air-relative set unknown; a bad atmosphere loses
  // only mach and cas; Mach >= 1 loses only cas. Every lost field is a defined 0, never NaN.
  const Eigen::Vector3d v_rel_b = air_data::relative_velocity_b(vs, env);
  if (!env.wind_n.allFinite() || !v_rel_b.allFinite()) {
    out.status_flags |= ASTRA_ICD_STATUS_WIND_INVALID;
  } else {
    out.alpha = air_data::angle_of_attack(v_rel_b);
    out.beta = air_data::sideslip(v_rel_b);
    out.tas = air_data::true_airspeed(v_rel_b);
    const double pressure = env.atmosphere.pressure_pa;
    const double sound_speed = env.atmosphere.speed_of_sound_m_s;
    const bool atmosphere_ok = std::isfinite(pressure) && pressure > 0.0 &&
                               std::isfinite(sound_speed) && sound_speed > 0.0;
    if (!atmosphere_ok) {
      out.status_flags |= ASTRA_ICD_STATUS_ATMOSPHERE_INVALID;
    } else {
      out.mach = air_data::mach(out.tas, sound_speed);
      if (out.mach >= 1.0) {
        out.status_flags |= ASTRA_ICD_STATUS_CAS_SUPERSONIC;
      } else {
        out.cas = calibrated_airspeed(pressure, out.mach);
      }
    }
  }

  // Specific force: previous frame's wrench_total force over mass (IMU convention, body axes).
  if (previous != nullptr && ctx.frame > 0 && previous->wrench_total.force_b.allFinite() &&
      std::isfinite(previous->mass) && previous->mass > 0.0) {
    const Eigen::Vector3d specific_force_b = previous->wrench_total.force_b / previous->mass;
    out.ax = specific_force_b(0);
    out.ay = specific_force_b(1);
    out.az = specific_force_b(2);
  } else {
    out.status_flags |= ASTRA_ICD_STATUS_ACCEL_INVALID;
  }
}

}  // namespace astra::icd_pack
