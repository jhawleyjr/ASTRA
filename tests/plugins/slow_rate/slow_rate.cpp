// A WrenchSource with a zero wrench whose one signal lives in a slower rate group (M6 PR 2,
// docs/plans/m6-pr2-finalization.md): no shipped model registers a divisor above 1, so this is the
// fixture that puts a /rate_<divisor> group other than /rate_1 into a real log through
// Simulation::from_config (ADR-0027's R16 kill tests and R18 byte identity).
//
// It registers "slow_rate.value" (unit "1") at the divisor of its own model entry (the "divisor"
// key Simulation reads for the scheduler; default 1), so the signal's rate group matches the frames
// its step() runs on. step() sets the value to sqrt(frame + 0.5): distinct, non-trivial bits on
// every due frame, so a byte comparison of the group is not vacuous.
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class SlowRate final : public astra::WrenchSource {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    // Simulation has already checked the entry's divisor (checked_divisor in simulation.cpp).
    const std::int64_t divisor = config.integer("divisor", 1);
    registry.add("slow_rate.value", &value_, "1", static_cast<std::uint32_t>(divisor));
  }

  void reset() override { value_ = 0.0; }

  void step(const astra::StepContext& ctx) override {
    value_ = std::sqrt(static_cast<double>(ctx.frame) + 0.5);
  }

  [[nodiscard]] std::string_view name() const override { return "slow_rate"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    return {};
  }

 private:
  double value_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<SlowRate>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "slow_rate", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
