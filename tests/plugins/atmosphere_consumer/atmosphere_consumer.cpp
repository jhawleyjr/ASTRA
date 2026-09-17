// A WrenchSource that requires atmosphere and nothing else -- no Cd*A, no physics -- for M2 PR 2's
// call-contract tests (tests/unit/wind/wind_test.cpp): it exists to make the atmosphere requirement
// rejection assertable when physical.atmosphere is absent. M2 PR 3: also records what
// EnvironmentSample::atmosphere it was actually handed (recorded_density_, published), so a test
// can assert Simulation::step's merged sampling block reaches this WrenchSource with a finite
// density when the vehicle's altitude is inside the configured AtmosphereModel's valid_range(), and
// an unclamped NaN when it is outside -- the same "records rather than acts on it" instrumentation
// tests/plugins/wind_consumer.cpp already uses for wind_n.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class AtmosphereConsumer final : public astra::WrenchSource {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    registry.add("atmosphere_consumer.density", &recorded_density_, "kg/m^3");
  }
  void reset() override { recorded_density_ = 0.0; }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "atmosphere_consumer"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& env) const noexcept override {
    // mutable: test instrumentation only, the same exception tests/plugins/wind_consumer.cpp
    // documents for its own const wrench_b.
    recorded_density_ = env.atmosphere.density_kg_m3;
    return {};
  }

  [[nodiscard]] astra::EnvironmentRequirements requires_environment() const noexcept override {
    astra::EnvironmentRequirements requirements;
    requirements.atmosphere = true;
    return requirements;
  }

 private:
  mutable double recorded_density_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<AtmosphereConsumer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "atmosphere_consumer", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
