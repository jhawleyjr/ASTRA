// Physics-derived inputs to a concrete AeroModel::compute() (docs/architecture.md §7, ADR-0018):
// airspeed, aerodynamic angles, body rates, dynamic pressure, and Mach, assembled by
// AeroModel::wrench_b() from a VehicleState/EnvironmentSample pair before compute() ever runs.
// Deliberately carries no control-surface values -- ADR-0018's own Decision: a concrete plugin
// reads its own bound const double* (WrenchSource::bind_inputs(), ADR-0017), the same way any
// other WrenchSource reads any other member, not through this struct. Plain struct so a plugin's
// compute() can be unit-tested with hand-built values and no simulator (docs/architecture.md §7's
// own promise, preserved).
#pragma once

#include <Eigen/Core>
#include <limits>

namespace astra {

// SI (ADR-0002); body axes (ADR-0001) unless noted. All-NaN by default, the same reasoning
// AtmoState's own default already uses (atmosphere_model.hpp): AeroModel::wrench_b() always fully
// assembles every field before its own isfinite guard runs, so a default value is never actually
// observed on that path -- but a hand-built test fixture (docs/architecture.md §7's "no simulator"
// promise) that forgets a field should see a loud NaN, not a plausible-looking zero.
struct AeroInputs {
  // |v_rel_b|, air-relative (wind-corrected) speed. m/s.
  double true_airspeed_mps = std::numeric_limits<double>::quiet_NaN();
  // Angle of attack, atan2(w_rel, u_rel) (S&L 3rd ed. Table 2.3-1 / Eq. (2.3-6b), p.78). rad.
  double alpha_rad = std::numeric_limits<double>::quiet_NaN();
  // Sideslip: atan2(v_rel, hypot(u_rel, w_rel)) -- equivalent to S&L's own asin(v_rel/V_T) form for
  // true_airspeed_mps > 0, but finite (never 0/0) at true_airspeed_mps == 0 too (ADR-0018's own
  // division-by-zero avoidance). rad.
  double beta_rad = std::numeric_limits<double>::quiet_NaN();
  // p, q, r: straight VehicleState::omega_b(), inertial-referenced, no wind term
  // (tests/plugins/nesc_aero.cpp's own comment already states this distinction). rad/s.
  Eigen::Vector3d omega_b = Eigen::Vector3d::Constant(std::numeric_limits<double>::quiet_NaN());
  // 0.5 * rho * true_airspeed_mps^2 (S&L Eq. (2.2-1), p.67). Pa.
  double dynamic_pressure_pa = std::numeric_limits<double>::quiet_NaN();
  // true_airspeed_mps / speed_of_sound_m_s. Dimensionless ("1").
  double mach = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace astra
