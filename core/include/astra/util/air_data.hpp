// Air-data math as core utilities (docs/architecture.md §7, ADR-0020): relative velocity, airspeed,
// angle of attack, sideslip, dynamic pressure, Mach -- a model composes whichever it needs, never a
// mandatory input struct or a sealed method. Formulas are relocated verbatim from ADR-0018's own
// AeroModel::wrench_b() (core/src/interfaces/aero_model.cpp, pre-ADR-0020), not re-derived: the
// same atan2-based forms, finite (never 0/0) at true_airspeed_mps == 0 -- S&L 3rd ed. Table 2.3-1 /
// Eq. (2.3-6a,b), p.78, and Eq. (2.2-1), p.67. Also the body rate relative to the Earth
// (omega_eb_b, ADR-0032), which is Earth-relative, not air-relative.
#pragma once

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace astra::air_data {

// v_rel_b = v_b - rotate_n_to_b(q_nb, wind_n): the vehicle's velocity relative to the air mass,
// body axes.
[[nodiscard]] inline Eigen::Vector3d relative_velocity_b(const VehicleState& vs,
                                                         const EnvironmentSample& env) noexcept {
  return vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n);
}

// |v_rel_b|. m/s.
[[nodiscard]] inline double true_airspeed(const Eigen::Vector3d& v_rel_b) noexcept {
  return v_rel_b.norm();
}

// atan2(w_rel, u_rel) (S&L Eq. (2.3-6b)): finite at v_rel_b == 0 (never 0/0), though not always
// exactly 0 in the IEEE-754 signed-zero branch (atan2(+0,-0) == pi) -- a disclosed, accepted
// cosmetic edge case, never a NaN. rad.
[[nodiscard]] inline double angle_of_attack(const Eigen::Vector3d& v_rel_b) noexcept {
  return std::atan2(v_rel_b(2), v_rel_b(0));
}

// atan2(v_rel, hypot(u_rel, w_rel)): equivalent to S&L's own asin(v_rel/V_T) form for
// true_airspeed > 0, but finite (never 0/0) at true_airspeed == 0 too. rad.
[[nodiscard]] inline double sideslip(const Eigen::Vector3d& v_rel_b) noexcept {
  return std::atan2(v_rel_b(1), std::hypot(v_rel_b(0), v_rel_b(2)));
}

// 0.5 * rho * v_true^2 (S&L Eq. (2.2-1), p.67). Pa.
[[nodiscard]] inline double dynamic_pressure(double rho, double v_true) noexcept {
  constexpr double kDynamicPressureCoefficient = 0.5;
  return kDynamicPressureCoefficient * rho * v_true * v_true;
}

// v_true / speed_of_sound. Dimensionless ("1").
[[nodiscard]] inline double mach(double v_true, double speed_of_sound) noexcept {
  return v_true / speed_of_sound;
}

// ADR-0032: the body rate relative to the Earth, omega_eb_b = omega_b - omega_ie_b, in body axes,
// rad/s. vs.omega_b() is the state's own rate, relative to inertial (ADR-0003), and env.omega_ie_b
// is the Earth's rate in the same body axes (environment_sample.hpp), so nothing is rotated here.
// Earth-relative, not air-relative: under a non-uniform wind (a shear, or turbulence's rotational
// part) the air mass rotates, and that rotation is not in here. NaN when env.omega_ie_b was never
// set.
[[nodiscard]] inline Eigen::Vector3d omega_eb_b(const VehicleState& vs,
                                                const EnvironmentSample& env) noexcept {
  return vs.omega_b() - env.omega_ie_b;
}

// Recommended, not enforced (ADR-0020: the isfinite guard becomes a utility a plugin may call, not
// a sealed core method). A plugin composing its own Inputs-shaped struct from the functions above
// can check every field with this before proceeding, matching ADR-0018's own former guard -- but
// core no longer requires it: air_data.hpp / aircraft/_template's own stubs cite R-001 by name
// (docs/reviews/pr3-aero-propulsion-review.md) as the concrete failure mode a finiteness check over
// every raw field a plugin reads, not just derived ones, prevents.
[[nodiscard]] inline bool all_finite(std::initializer_list<double> values) noexcept {
  return std::ranges::all_of(values, [](double value) { return std::isfinite(value); });
}

}  // namespace astra::air_data
