// See aero_math.hpp's own header comment for what this fixture proves and why.
#include <memory>
#include <string_view>

#include "aero_math.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class AeroMathOdrCheck final : public astra::AeroModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "aero_math_odr_check"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    return astra::odr_check_aero_math::compute_wrench(astra::odr_check_aero_math::Inputs{},
                                                      astra::odr_check_aero_math::Coefficients{});
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<AeroMathOdrCheck>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "aero_math_odr_check", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
