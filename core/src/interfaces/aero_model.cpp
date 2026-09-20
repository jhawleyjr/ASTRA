#include "astra/interfaces/aero_model.hpp"

#include <cmath>
#include <limits>

#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

// The key function; see the comment in aero_model.hpp.
AeroModel::~AeroModel() = default;

namespace {

// Checks the raw atmosphere fields this class consumes, not only the derived AeroInputs fields
// built from them: dividing a finite true_airspeed_mps by an infinite speed_of_sound_m_s gives a
// finite mach == 0.0 (IEEE-754: finite/Inf == 0), and 0.5 * a finite density * a finite
// true_airspeed_mps^2 is finite regardless of speed_of_sound_m_s -- so checking only the derived
// fields would let a broken (infinite, not NaN) speed_of_sound_m_s produce an all-finite
// AeroInputs and reach compute() silently (Codex review + adversarial review, both independently:
// an infinite speed_of_sound_m_s with otherwise-finite values passed the derived-field-only guard
// and let compute() run on data that looks valid but is not).
[[nodiscard]] bool all_finite(const AeroInputs& inputs, const AtmoState& atmosphere) noexcept {
  return std::isfinite(inputs.true_airspeed_mps) && std::isfinite(inputs.alpha_rad) &&
         std::isfinite(inputs.beta_rad) && inputs.omega_b.allFinite() &&
         std::isfinite(inputs.dynamic_pressure_pa) && std::isfinite(inputs.mach) &&
         std::isfinite(atmosphere.density_kg_m3) && std::isfinite(atmosphere.speed_of_sound_m_s);
}

// ADR-0018: an explicit "this contribution is invalid" sentinel, deliberately never Wrench{}'s own
// zero default (which would read as "this model applies no force," a silent wrong answer).
[[nodiscard]] Wrench non_finite_wrench() noexcept {
  Wrench wrench;
  wrench.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  wrench.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return wrench;
}

}  // namespace

Wrench AeroModel::wrench_b(const VehicleState& vs, const EnvironmentSample& env) const noexcept {
  const Eigen::Vector3d v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n);
  const double u_rel = v_rel_b(0);
  const double v_rel = v_rel_b(1);
  const double w_rel = v_rel_b(2);

  // S&L Eq. (2.2-1): q_bar = 0.5 * rho * V_T^2. Named so the coefficient is not a magic number.
  constexpr double kDynamicPressureCoefficient = 0.5;

  AeroInputs inputs;
  inputs.true_airspeed_mps = v_rel_b.norm();
  inputs.alpha_rad = std::atan2(w_rel, u_rel);
  inputs.beta_rad = std::atan2(v_rel, std::hypot(u_rel, w_rel));
  inputs.omega_b = vs.omega_b();
  inputs.dynamic_pressure_pa = kDynamicPressureCoefficient * env.atmosphere.density_kg_m3 *
                               inputs.true_airspeed_mps * inputs.true_airspeed_mps;
  inputs.mach = inputs.true_airspeed_mps / env.atmosphere.speed_of_sound_m_s;

  if (!all_finite(inputs, env.atmosphere)) {
    return non_finite_wrench();
  }
  return compute(inputs);
}

EnvironmentRequirements AeroModel::requires_environment() const noexcept {
  return {.wind = false, .atmosphere = true};
}

}  // namespace astra
