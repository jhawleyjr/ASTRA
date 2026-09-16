// A WrenchSource that requires atmosphere and nothing else -- no Cd*A, no physics -- for M2 PR 2's
// call-contract tests (tests/unit/wind/wind_test.cpp): it exists to make D3's unconditional
// atmosphere rejection assertable. No PR before M2 PR 3 populates EnvironmentSample::atmosphere
// (core/include/astra/kernel/environment_sample.hpp), so Vehicle::init rejects a source declaring
// this requirement even when physical.atmosphere is configured -- the rejection does not depend on
// that slot's presence, and a test using this plugin proves it, not just "slot absent".
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class AtmosphereConsumer final : public astra::WrenchSource {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "atmosphere_consumer"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    return {};
  }

  [[nodiscard]] astra::EnvironmentRequirements requires_environment() const noexcept override {
    astra::EnvironmentRequirements requirements;
    requirements.atmosphere = true;
    return requirements;
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<AtmosphereConsumer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "atmosphere_consumer", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
