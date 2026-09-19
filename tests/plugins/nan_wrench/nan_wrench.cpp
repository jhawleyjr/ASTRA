// A WrenchSource for PLAN.md M1 PR 4's acceptance: injects a NaN force starting at a configured
// frame, so a test can watch a health halt land on this plugin by its path
// ("tests/plugins/nan_wrench"). "force_x" (default 0, applied every frame as a constant force_b(0))
// keeps this plugin usable as an ordinary, well-behaved wrench source too
// (tests/physics/no_alloc_test.cpp); "nan_frame" (default absent = never NaN) is the frame from
// which force_b(0) becomes NaN. Both default to values that leave this plugin loadable with an
// empty config, because load_all_test constructs every registered plugin with none. Publishes no
// signal of its own (unlike a real plugin) so a test can load two instances under one vehicle to
// exercise an overflowing wrench sum (tests/unit/kernel/simulation_halt_test.cpp) without a
// duplicate-name collision in the registry.
//
// "nan_after_calls" (PLAN.md M2 PR 3c, default 0): within an affected frame (ctx.frame >=
// nan_frame_), wrench_b() is called once per RK4 sub-stage (docs/plans/pr3c-substage-wrench.md) --
// this plugin now stays finite for the first nan_after_calls_ of those calls and only turns NaN
// from call index nan_after_calls_ onward, letting a test target a fault that is invisible at the
// frame-start (k1) state and only appears at k2/k3/k4, distinct from k1's own pre-existing
// coverage. The default 0 preserves every existing test's behavior (NaN from the very first call of
// an affected frame, i.e. k1, unchanged). call_count_ is mutable, reset once per frame in step()
// (before Vehicle::integrate() calls wrench_b() at all), the same "test instrumentation only"
// exception nesc_aero's own former self-publish and tests/plugins/wind_consumer.cpp both document
// for a const wrench_b().
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

std::string keyed_path(const astra::Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

class NanWrench final : public astra::WrenchSource {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& /*registry*/) override {
    force_x_ = config.number("force_x", 0.0);
    if (config.has("nan_frame")) {
      const std::int64_t value = config.integer("nan_frame");
      if (value < 0) {
        throw astra::ConfigError(config.origin(), keyed_path(config, "nan_frame"),
                                 "nan_frame must not be negative");
      }
      nan_frame_ = static_cast<std::uint64_t>(value);
    }
    const std::int64_t after_calls = config.integer("nan_after_calls", 0);
    if (after_calls < 0) {
      throw astra::ConfigError(config.origin(), keyed_path(config, "nan_after_calls"),
                               "nan_after_calls must not be negative");
    }
    nan_after_calls_ = static_cast<std::uint32_t>(after_calls);
  }
  void reset() override {
    nan_from_here_ = false;
    call_count_ = 0;
  }
  // Latches once ctx.frame reaches nan_frame_; resets this frame's call counter (nan_after_calls_'s
  // own comment above): wrench_b() below has no StepContext of its own, so this frame-count
  // bookkeeping (via the scheduler's ordinary step(), which Vehicle::step() runs before
  // Vehicle::integrate() calls wrench_b()) is how it learns which frame it is.
  void step(const astra::StepContext& ctx) override {
    if (nan_frame_.has_value() && ctx.frame >= *nan_frame_) {
      nan_from_here_ = true;
    }
    call_count_ = 0;
  }
  [[nodiscard]] std::string_view name() const override { return "nan_wrench"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    const bool nan_this_call = nan_from_here_ && call_count_ >= nan_after_calls_;
    ++call_count_;
    astra::Wrench wrench;
    wrench.force_b(0) = nan_this_call ? std::numeric_limits<double>::quiet_NaN() : force_x_;
    return wrench;
  }

 private:
  double force_x_ = 0.0;
  std::optional<std::uint64_t> nan_frame_;
  bool nan_from_here_ = false;
  std::uint32_t nan_after_calls_ = 0;
  mutable std::uint32_t call_count_ = 0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<NanWrench>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "nan_wrench", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
