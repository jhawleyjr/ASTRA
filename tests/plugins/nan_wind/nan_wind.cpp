// A broken WindModel that returns an all-NaN wind_n every frame, mirroring
// tests/plugins/nan_atmosphere's own shape exactly, for ADR-0020's report_non_finite_environment
// acceptance tests (docs/plans/pr3d-adr0020-implementation.md, R4/R6): a configured, genuinely
// non-finite wind_n must halt naming this plugin, before any configured WrenchSource's wrench_b()
// runs, the same guarantee nan_atmosphere already proves for atmosphere.
#include <limits>
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class NanWind final : public astra::WindModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "nan_wind"; }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Constant(std::numeric_limits<double>::quiet_NaN());
  }

  // Stateless (begin_frame is the default empty no-op) -- correct under more than one vehicle for
  // the identical reason wind/uniform already declares this true.
  [[nodiscard]] bool shareable_across_vehicles() const noexcept override { return true; }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<NanWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "nan_wind", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
