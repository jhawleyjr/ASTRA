// Linear-coefficient AeroModel starting point for a new aircraft (docs/plans/
// pr4-notional-aero-turbofan.md, ADR-0020). `just new-aircraft <name>` (or `cp -r
// aircraft/_template aircraft/<name>`) copies this file as-is: nothing here needs editing except
// the numbers in the aircraft's own config -- see aircraft/_template/configs/cruise.yaml's
// TODO-marked vehicle.models[physical/aero] block. Left un-edited, this plugin's own "wing_area_m2
// must be positive" check below throws a ConfigError naming the config key that still reads 0.0, so
// an un-edited template can never silently fly. Identical to aircraft/notional_transport/physical/
// aero/aero.cpp except this comment, the class name (PR 1's physical/mass.cpp precedent), and the
// body rate it damps (ADR-0032, below).
//
// Overrides WrenchSource::wrench_b() directly (ADR-0020: no compute() step, no core Inputs struct).
// aero_math.hpp's own header comment has the full force/moment-assembly citation and the V->0
// algebraic fix; this file only assembles that header's Inputs from (VehicleState,
// EnvironmentSample) via air_data:: and this plugin's own bound controls, then delegates.
#include <Eigen/Core>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "aero_math.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/air_data.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

std::string keyed_path(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

double checked_positive(const Config& config, std::string_view key) {
  const double value = config.number(key);
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw ConfigError(config.origin(), keyed_path(config, key),
                      std::string{key} + " must be finite and positive");
  }
  return value;
}

class TemplateAero final : public astra::AeroModel {
 public:
  void init(const Config& config, astra::SignalRegistry& /*registry*/) override {
    coeffs_.wing_area_m2 = checked_positive(config, "wing_area_m2");
    coeffs_.wingspan_m = checked_positive(config, "wingspan_m");
    coeffs_.mean_chord_m = checked_positive(config, "mean_chord_m");
    coeffs_.r_aero_b = Eigen::Vector3d(config.number("r_aero_b_x_m"), config.number("r_aero_b_y_m"),
                                       config.number("r_aero_b_z_m"));
    coeffs_.cl_0 = config.number("cl_0");
    coeffs_.cl_alpha_per_rad = config.number("cl_alpha_per_rad");
    coeffs_.cl_elevator_per_rad = config.number("cl_elevator_per_rad");
    coeffs_.cd_0 = config.number("cd_0");
    coeffs_.cd_k = config.number("cd_k");
    coeffs_.cy_beta_per_rad = config.number("cy_beta_per_rad");
    coeffs_.croll_beta_per_rad = config.number("croll_beta_per_rad");
    coeffs_.croll_p_per_rad = config.number("croll_p_per_rad");
    coeffs_.croll_r_per_rad = config.number("croll_r_per_rad");
    coeffs_.croll_aileron_per_rad = config.number("croll_aileron_per_rad");
    coeffs_.croll_rudder_per_rad = config.number("croll_rudder_per_rad");
    coeffs_.cm_alpha_per_rad = config.number("cm_alpha_per_rad");
    coeffs_.cm_q_per_rad = config.number("cm_q_per_rad");
    coeffs_.cm_elevator_per_rad = config.number("cm_elevator_per_rad");
    coeffs_.cn_beta_per_rad = config.number("cn_beta_per_rad");
    coeffs_.cn_r_per_rad = config.number("cn_r_per_rad");
    coeffs_.cn_rudder_per_rad = config.number("cn_rudder_per_rad");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "aero"; }

  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    elevator_ = inputs.add(registry, "controls.elevator", "rad");
    aileron_ = inputs.add(registry, "controls.aileron", "rad");
    rudder_ = inputs.add(registry, "controls.rudder", "rad");
  }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& vs, const astra::EnvironmentSample& env) const noexcept override {
    namespace air_data = astra::air_data;
    namespace aero = astra::aircraft_template_aero_math;

    const Eigen::Vector3d v_rel_b = air_data::relative_velocity_b(vs, env);

    aero::Inputs in;
    in.true_airspeed_mps = air_data::true_airspeed(v_rel_b);
    in.alpha_rad = air_data::angle_of_attack(v_rel_b);
    in.beta_rad = air_data::sideslip(v_rel_b);
    // ADR-0032: the rate terms damp the body rate relative to the Earth, omega_b - omega_ie_b, not
    // the state's own vs.omega_b(), which is relative to inertial.
    // - The core supplies omega_ie_b from the configured Earth model, in every RK4 stage, so this
    //   aero never carries an Earth rate of its own.
    // - On earth/flat (this template's config) and on a non-rotating earth/round, the core's
    //   omega_ie_b is exactly +0.0, and the two rates are equal bit for bit.
    // - Under a rotating Earth they differ by the Earth's rate in body axes.
    // - A hand-built EnvironmentSample (a test, an independent evaluator) must set omega_ie_b; its
    //   NaN default makes this wrench non-finite.
    // aircraft/notional_transport's aero and tests/plugins/nesc_aero still damp the inertial
    // rate, until PLAN.md's B2 item.
    in.omega_b = air_data::omega_eb_b(vs, env);
    in.dynamic_pressure_pa =
        air_data::dynamic_pressure(env.atmosphere.density_kg_m3, in.true_airspeed_mps);
    in.density_kg_m3 = env.atmosphere.density_kg_m3;
    in.elevator_rad = *elevator_;
    in.aileron_rad = *aileron_;
    in.rudder_rad = *rudder_;

    if (!aero::finite(in)) {
      return aero::non_finite_wrench();
    }
    return aero::compute_wrench(in, coeffs_);
  }

 private:
  astra::aircraft_template_aero_math::Coefficients coeffs_;
  const double* elevator_ = nullptr;
  const double* aileron_ = nullptr;
  const double* rudder_ = nullptr;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<TemplateAero>(); }

// "aero", not "template_aero": new-aircraft.sh only rewrites config YAML paths on copy, never .cpp
// string literals (PR 1's mass.cpp precedent) -- the plugin path itself already disambiguates which
// aircraft's copy a run log's descriptor came from.
constexpr astra::PluginDescriptor kDescriptor{"physical", "aero", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
