// Plugin-side "unit-tested with no simulator" pattern (docs/plans/pr4-notional-aero-turbofan.md,
// ADR-0020): Inputs/Coefficients are plain data, compute_wrench() is pure (no Config, no
// VehicleState, no EnvironmentSample, no I/O), so a test can call it directly with hand-built
// values -- the same shape plugins/physical/wind/dryden/dryden_filter.hpp already demonstrates for
// a plugin-adjacent header included by both the plugin's own .cpp and an independent unit test.
// Identical between aircraft/notional_transport and aircraft/_template; only each aircraft's own
// config supplies the Coefficients values (aero.cpp reads them).
//
// Force/moment assembly: S&L 3rd ed. Table 2.3-1 (p.78), Eq. (2.3-2)/(2.3-3a)/(2.3-4a,b) --
// docs/plans/pr4-notional-aero-turbofan.md's own "Force/moment assembly" section has the full
// citation and derivation, including why this plugin's own wind-axis vector (-D, Y, -L) replaces
// S&L's literal (-D, -C, -L): JSBSim's SIDE-axis coefficient (Y = cy_beta_per_rad * beta * qbar *
// S) is already the signed +right force component directly, not S&L's own oppositely-signed
// crosswind quantity C. Moments are computed about AERORP (r_aero_b's own origin, the source's own
// force/ moment reference point) then transferred to CG: moment_b_cg = moment_native_b + r_aero_b x
// force_b (the standard rigid-body moment-transfer theorem).
//
// Rate-damping terms (croll_p/r, cm_q, cn_r) use the division-free, V-linear reduction of
// qbar*S*L*(L/(2*V))*rate (the plan's own "Nondimensional-rate V->0 obligation" section): naively
// computing L/(2*V) is +Inf at V=0, and Inf*0 (qbar=0 at V=0) is NaN. The reduced form
// 0.25*rho*V*S*L^2*Coeff*rate has no division anywhere and is exactly 0 at V=0 by construction.
#pragma once

#include <Eigen/Core>
#include <cmath>
#include <limits>

#include "astra/interfaces/wrench.hpp"
#include "astra/util/air_data.hpp"

namespace astra::aircraft_f16_aero_math {

// Assembled by wrench_b() from (VehicleState, EnvironmentSample) via air_data:: plus this plugin's
// own bound control inputs. density_kg_m3 is the one raw atmosphere field this model's own math
// reads (through dynamic_pressure_pa and, separately, the rate-damping terms' own explicit rho
// factor) -- carried here, not only inside dynamic_pressure_pa, so finite() below can validate it
// directly (see the file's own R-001 citation on finite()).
struct Inputs {
  double true_airspeed_mps = 0.0;
  double alpha_rad = 0.0;
  double beta_rad = 0.0;
  Eigen::Vector3d omega_b = Eigen::Vector3d::Zero();  // p, q, r
  double dynamic_pressure_pa = 0.0;
  double density_kg_m3 = 0.0;
  double elevator_rad = 0.0;
  double aileron_rad = 0.0;
  double rudder_rad = 0.0;
};

// Config-resolved constants (docs/plans/pr4-notional-aero-turbofan.md's own "Config keys" section).
struct Coefficients {
  double wing_area_m2 = 0.0;
  double wingspan_m = 0.0;
  double mean_chord_m = 0.0;
  Eigen::Vector3d r_aero_b = Eigen::Vector3d::Zero();  // AERORP relative to CG, body axes, m
  double cl_0 = 0.0;
  double cl_alpha_per_rad = 0.0;
  double cl_elevator_per_rad = 0.0;
  double cd_0 = 0.0;
  double cd_k = 0.0;
  double cy_beta_per_rad = 0.0;
  double croll_beta_per_rad = 0.0;
  double croll_p_per_rad = 0.0;
  double croll_r_per_rad = 0.0;
  double croll_aileron_per_rad = 0.0;
  double croll_rudder_per_rad = 0.0;
  double cm_alpha_per_rad = 0.0;
  double cm_q_per_rad = 0.0;
  double cm_elevator_per_rad = 0.0;
  double cn_beta_per_rad = 0.0;
  double cn_r_per_rad = 0.0;
  double cn_rudder_per_rad = 0.0;
};

// R-001 (docs/reviews/pr3-aero-propulsion-review.md) is cited here for the GENERAL lesson, not a
// literal replay of its exact mechanism: checking only a struct's DERIVED fields can let a broken
// RAW field pass silently if some derived quantity happens to mask it algebraically (there, an
// infinite speed_of_sound_m_s divided a finite airspeed down to a finite mach == 0.0). This plugin
// never computes Mach at all (its coefficients are constants, not Mach-scheduled -- docs/plans/
// pr4-notional-aero-turbofan.md's Open item 6, a deliberate deviation from the original instruction
// to use air_data::mach here), so that specific masking shape cannot recur -- but the underlying
// habit (validate the raw field your own math actually consumes, not only what you derived from it)
// still applies to density_kg_m3, the one raw atmosphere field this model reads. Core-side
// HealthMonitor::check_environment (ADR-0020) already halts on a non-finite density_kg_m3 before
// wrench_b() runs; this check is this plugin's own second, redundant guarantee.
[[nodiscard]] inline bool finite(const Inputs& in) noexcept {
  return astra::air_data::all_finite({in.true_airspeed_mps, in.alpha_rad, in.beta_rad,
                                      in.dynamic_pressure_pa, in.density_kg_m3, in.elevator_rad,
                                      in.aileron_rad, in.rudder_rad}) &&
         in.omega_b.allFinite();
}

// ADR-0018's own explicit "this contribution is invalid" sentinel, deliberately never Wrench{}'s
// own zero default (which would read as "this model applies no force," a silent wrong answer).
[[nodiscard]] inline astra::Wrench non_finite_wrench() noexcept {
  astra::Wrench wrench;
  wrench.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  wrench.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return wrench;
}

[[nodiscard]] inline astra::Wrench compute_wrench(const Inputs& in,
                                                  const Coefficients& c) noexcept {
  const double cl =
      c.cl_0 + c.cl_alpha_per_rad * in.alpha_rad + c.cl_elevator_per_rad * in.elevator_rad;
  const double cd = c.cd_0 + c.cd_k * cl * cl;
  const double drag_n = in.dynamic_pressure_pa * c.wing_area_m2 * cd;
  const double lift_n = in.dynamic_pressure_pa * c.wing_area_m2 * cl;
  // JSBSim-native SIDE convention (+right), NOT S&L's literal -C -- see this file's own header.
  const double side_n = in.dynamic_pressure_pa * c.wing_area_m2 * c.cy_beta_per_rad * in.beta_rad;

  const double ca = std::cos(in.alpha_rad);
  const double sa = std::sin(in.alpha_rad);
  const double cb = std::cos(in.beta_rad);
  const double sb = std::sin(in.beta_rad);
  // S&L Eq. (2.3-2): C_{w/bf}(alpha, beta). force_b = C_{w/bf}^T * force_w.
  Eigen::Matrix3d c_w_bf;
  // clang-format off
  c_w_bf << ca * cb,  sb, sa * cb,
           -ca * sb,  cb, -sa * sb,
           -sa,      0.0, ca;
  // clang-format on
  const Eigen::Vector3d force_w(-drag_n, side_n, -lift_n);
  const Eigen::Vector3d force_b = c_w_bf.transpose() * force_w;

  const double p = in.omega_b(0);
  const double q = in.omega_b(1);
  const double r = in.omega_b(2);
  const double b = c.wingspan_m;
  const double cbar = c.mean_chord_m;
  // Division-free V-linear rate-damping form (this file's own header comment).
  const double rate_common_n_per_m2 =
      0.25 * in.density_kg_m3 * in.true_airspeed_mps * c.wing_area_m2;

  const double roll_n_m =
      in.dynamic_pressure_pa * c.wing_area_m2 * b *
          (c.croll_beta_per_rad * in.beta_rad + c.croll_aileron_per_rad * in.aileron_rad +
           c.croll_rudder_per_rad * in.rudder_rad) +
      rate_common_n_per_m2 * b * b * (c.croll_p_per_rad * p + c.croll_r_per_rad * r);
  const double pitch_n_m =
      in.dynamic_pressure_pa * c.wing_area_m2 * cbar *
          (c.cm_alpha_per_rad * in.alpha_rad + c.cm_elevator_per_rad * in.elevator_rad) +
      rate_common_n_per_m2 * cbar * cbar * c.cm_q_per_rad * q;
  const double yaw_n_m =
      in.dynamic_pressure_pa * c.wing_area_m2 * b *
          (c.cn_beta_per_rad * in.beta_rad + c.cn_rudder_per_rad * in.rudder_rad) +
      rate_common_n_per_m2 * b * b * c.cn_r_per_rad * r;

  const Eigen::Vector3d moment_native_b(roll_n_m, pitch_n_m, yaw_n_m);
  const Eigen::Vector3d moment_b = moment_native_b + c.r_aero_b.cross(force_b);

  astra::Wrench wrench;
  wrench.force_b = force_b;
  wrench.moment_b = moment_b;
  return wrench;
}

}  // namespace astra::aircraft_f16_aero_math
