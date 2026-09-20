// A minimal concrete PropulsionModel (ADR-0018) for integration tests exercising the real,
// unconditional requires_environment().atmosphere = true path end to end through Simulation. No
// writable input of its own -- tests/plugins/aero_stub already exercises ADR-0017's write seam;
// this plugin is loaded alongside it under one vehicle to prove both new WrenchSource
// specializations load and run together. compute() returns a force deliberately simple enough to
// hand-check in a test, not a real propulsion model -- on force_b(1), not force_b(0), so a test
// summing both stubs under one vehicle can attribute each axis of the combined wrench to one stub.
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class PropulsionStub final : public astra::PropulsionModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "propulsion_stub"; }

  [[nodiscard]] astra::Wrench compute(
      const astra::PropulsionInputs& inputs) const noexcept override {
    astra::Wrench wrench;
    wrench.force_b(1) = 100.0 * inputs.atmosphere.density_kg_m3;
    return wrench;
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<PropulsionStub>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "propulsion_stub", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
