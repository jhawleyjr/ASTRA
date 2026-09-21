// An AeroModel that counts its own wrench_b() calls, for ADR-0020's own acceptance tests
// (docs/plans/pr3d-adr0020-implementation.md, R4): the design claims a stage whose environment
// Vehicle::prepare_stage_environment flags (out of range, or genuinely non-finite) never reaches
// any configured WrenchSource's wrench_b() at all -- this plugin is what makes that assertable
// instead of only inferred from an unchanged published force. wrench_b() is const noexcept, per
// the interface, so its call counter is a `mutable` member: test instrumentation only, the same
// documented exception tests/plugins/counting_wind's own wind_n_calls_ already uses -- it observes
// calls, never changes what wrench_b() returns.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class CountingAero final : public astra::AeroModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    registry.add("counting_aero.wrench_b_calls", &wrench_b_calls_, "1");
  }
  void reset() override { wrench_b_calls_ = 0.0; }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "counting_aero"; }

  // A fixed, easily-distinguished-from-zero force: a test proving this stage's total stayed at
  // Wrench{}'s own zero default (never this value) is proof this call never happened, not merely a
  // finite-vs-NaN inference.
  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    ++wrench_b_calls_;  // mutable: test instrumentation only, see file header
    astra::Wrench wrench;
    wrench.force_b(0) = 1.0;
    return wrench;
  }

 private:
  mutable double wrench_b_calls_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<CountingAero>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "counting_aero", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
