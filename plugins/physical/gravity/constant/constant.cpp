// Constant, uniform gravity (docs/architecture.md §7, pulled forward from M2 per PLAN.md M1 PR 3):
// g_n = (0, 0, g), g from config defaulting to standard gravity. Publishes the value actually used
// so it lands in the log even when the config leaves it at the default.
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

// Standard gravity, m/s^2 (the config default; ADR-0002 SI).
constexpr double kStandardGravity = 9.80665;

class ConstantGravity final : public astra::GravityModel {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    g_ = config.number("g", kStandardGravity);
    // Codex review: an unvalidated NaN/Inf here propagates into every EOM evaluation with no
    // init-time error, instead of the §15 pre-run "no NaN/Inf anywhere in ... parameters" check.
    if (!std::isfinite(g_)) {
      const std::string path = config.path().empty() ? "g" : config.path() + ".g";
      throw astra::ConfigError(config.origin(), path, "g must be finite");
    }
    registry.add("gravity.g", &g_, "m/s^2");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "gravity"; }

  [[nodiscard]] Eigen::Vector3d g_n(const astra::VehicleState& /*vs*/) const noexcept override {
    return {0.0, 0.0, g_};
  }

 private:
  double g_ = kStandardGravity;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<ConstantGravity>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "gravity_constant",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
