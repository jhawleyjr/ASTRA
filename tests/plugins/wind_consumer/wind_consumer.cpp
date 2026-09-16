// A WrenchSource that requires wind and nothing else -- no Cd*A, no physics -- for M2 PR 2's
// call-contract tests (tests/unit/wind/wind_test.cpp): it exists to make the init-error contract
// (a WrenchSource requiring wind with no physical.wind is a ConfigError naming physical.wind,
// core/src/kernel/vehicle.cpp) and the latch (the EnvironmentSample it is handed matches what the
// configured WindModel returned that frame) assertable rather than only claimed. Records what it
// was handed rather than acting on it; returns a zero wrench always. wrench_b is const noexcept
// (wrench_source.hpp), so its recording members are `mutable` -- test instrumentation only, the
// same exception tests/plugins/counting_wind/counting_wind.cpp documents.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class WindConsumer final : public astra::WrenchSource {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    registry.add("wind_consumer.wind_n_north", &recorded_wind_n_(0), "m/s");
    registry.add("wind_consumer.wind_n_east", &recorded_wind_n_(1), "m/s");
    registry.add("wind_consumer.wind_n_down", &recorded_wind_n_(2), "m/s");
    registry.add("wind_consumer.calls", &calls_, "1");
  }
  void reset() override {
    recorded_wind_n_.setZero();
    calls_ = 0.0;
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "wind_consumer"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& env) const noexcept override {
    recorded_wind_n_ = env.wind_n;  // mutable: test instrumentation only, see file header
    ++calls_;
    return {};
  }

  [[nodiscard]] astra::EnvironmentRequirements requires_environment() const noexcept override {
    astra::EnvironmentRequirements requirements;
    requirements.wind = true;
    return requirements;
  }

 private:
  mutable Eigen::Vector3d recorded_wind_n_ = Eigen::Vector3d::Zero();
  mutable double calls_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<WindConsumer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "wind_consumer", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
