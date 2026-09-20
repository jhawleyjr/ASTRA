#include "astra/interfaces/propulsion_model.hpp"

#include <cmath>
#include <limits>

#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

// The key function; see the comment in propulsion_model.hpp.
PropulsionModel::~PropulsionModel() = default;

namespace {

[[nodiscard]] bool all_finite(const PropulsionInputs& inputs) noexcept {
  return std::isfinite(inputs.true_airspeed_mps) &&
         std::isfinite(inputs.atmosphere.temperature_k) &&
         std::isfinite(inputs.atmosphere.pressure_pa) &&
         std::isfinite(inputs.atmosphere.density_kg_m3) &&
         std::isfinite(inputs.atmosphere.speed_of_sound_m_s);
}

// ADR-0018: the identical explicit non-finite sentinel AeroModel::wrench_b() uses (aero_model.cpp)
// -- never Wrench{}'s own zero default.
[[nodiscard]] Wrench non_finite_wrench() noexcept {
  Wrench wrench;
  wrench.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  wrench.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return wrench;
}

}  // namespace

Wrench PropulsionModel::wrench_b(const VehicleState& vs,
                                 const EnvironmentSample& env) const noexcept {
  const Eigen::Vector3d v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n);

  PropulsionInputs inputs;
  inputs.true_airspeed_mps = v_rel_b.norm();
  inputs.atmosphere = env.atmosphere;

  if (!all_finite(inputs)) {
    return non_finite_wrench();
  }
  return compute(inputs);
}

EnvironmentRequirements PropulsionModel::requires_environment() const noexcept {
  return {.wind = false, .atmosphere = true};
}

}  // namespace astra
