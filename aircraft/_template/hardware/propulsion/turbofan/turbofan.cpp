// Linear thrust-lapse PropulsionModel starting point for a new aircraft (docs/plans/
// pr4-notional-aero-turbofan.md, ADR-0020). `just new-aircraft <name>` copies this file as-is:
// nothing here needs editing except the numbers in the aircraft's own config -- see
// aircraft/_template/configs/cruise.yaml's TODO-marked vehicle.models[hardware/propulsion/turbofan]
// blocks. Left un-edited, this plugin's own "thrust_ref_n must be positive" check below throws a
// ConfigError naming the config key that still reads 0.0. Identical to aircraft/notional_transport/
// hardware/propulsion/turbofan/turbofan.cpp except this comment and the class name (PR 1's
// physical/mass.cpp precedent).
//
// Two instances are configured (station: left/right), each with its own x/y/z_offset_from_cg_m --
// no "left implies negative y" logic here, the config is explicit per instance. Both share one
// "controls.throttle" InputRegistry entry by name (symmetric thrust; InputRegistry's own same-name
// dedup, PR 2, needs no code here to make that work).
//
// Overrides WrenchSource::wrench_b() directly (ADR-0020). turbofan_math.hpp's own header comment
// has the thrust law and the (trivial, no-op) V->0 argument; this file only assembles that header's
// Inputs from EnvironmentSample and this plugin's own bound throttle, then delegates.
#include <Eigen/Core>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "turbofan_math.hpp"

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

class TemplateTurbofan final : public astra::PropulsionModel {
 public:
  void init(const Config& config, astra::SignalRegistry& /*registry*/) override {
    // "station" is a plugin-defined label only, not read for geometry -- read here only so it
    // counts as a consumed config key (an unread key is a ConfigError, §15), never stored.
    static_cast<void>(config.text("station"));
    coeffs_.r_engine_b =
        Eigen::Vector3d(config.number("x_offset_from_cg_m"), config.number("y_offset_from_cg_m"),
                        config.number("z_offset_from_cg_m"));
    coeffs_.thrust_ref_n = checked_positive(config, "thrust_ref_n");
    coeffs_.density_ref_kgm3 = checked_positive(config, "density_ref_kgm3");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "turbofan"; }

  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    throttle_ = inputs.add(registry, "controls.throttle", "1");
  }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& env) const noexcept override {
    namespace turbofan = astra::thrust_lapse;

    turbofan::Inputs in;
    in.throttle = *throttle_;
    in.density_kg_m3 = env.atmosphere.density_kg_m3;

    if (!turbofan::finite(in)) {
      return turbofan::non_finite_wrench();
    }
    return turbofan::compute_wrench(in, coeffs_);
  }

 private:
  astra::thrust_lapse::Coefficients coeffs_;
  const double* throttle_ = nullptr;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<TemplateTurbofan>(); }

// "turbofan", not "template_turbofan" (PR 1's mass.cpp precedent, same reasoning).
constexpr astra::PluginDescriptor kDescriptor{"hardware", "turbofan", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
