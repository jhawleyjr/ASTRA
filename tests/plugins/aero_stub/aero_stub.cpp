// A minimal concrete AeroModel (ADR-0018) for integration tests exercising the real, unconditional
// requires_environment().atmosphere = true path end to end through Simulation, including ADR-0017's
// write seam actually driving compute()'s output (PLAN.md M3 PR 3's own acceptance line -- not
// satisfied by a plugin with no writable input). Registers one writable input,
// "controls.elevator", the same name/unit tests/plugins/input_consumer already uses. compute()
// returns a force deliberately simple enough to hand-check in a test: dynamic pressure scaled by
// the currently-latched elevator value, not a real aerodynamic model.
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class AeroStub final : public astra::AeroModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "aero_stub"; }

  // ADR-0017: registers this plugin's one writable input; elevator_ is a const double* into
  // InputRegistry's own storage, not a plugin-owned member.
  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    elevator_ = inputs.add(registry, "controls.elevator", "rad");
  }

  [[nodiscard]] astra::Wrench compute(const astra::AeroInputs& inputs) const noexcept override {
    astra::Wrench wrench;
    wrench.force_b(0) = inputs.dynamic_pressure_pa * (*elevator_);
    return wrench;
  }

 private:
  const double* elevator_ = nullptr;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<AeroStub>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "aero_stub", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
