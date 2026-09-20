// A WrenchSource exercising ADR-0017's write seam end to end (docs/plans/pr2-write-seam.md's R2-R7,
// R9-R11): registers one writable input, "controls.elevator", via bind_inputs(), caching both the
// registry-owned const double* (add()) and a repeatable InputHandle (find()). Publishes the
// canonical value it read on each of a frame's four RK4 wrench_b() calls as signals named
// "input_consumer.<id>.k1" through "k4" (id disambiguates two instances loaded under one vehicle,
// R11 -- the write-seam input name itself stays fixed so both instances register it and share one
// value, per (h)).
//
// Optional "write_new_value" (default: no write at all -- a passive consumer for R2/R6/R7/R9/R10/
// R11) plus "write_from_step" (default false) select which of the two sanctioned/exceptional write
// points a test exercises: false writes from inside wrench_b() itself, on the call where
// call_count_ == 2 (RK4 stage k2) -- a labeled, test-only exception to the amended
// wrench_source.hpp contract, the same "test instrumentation only" exception
// tests/plugins/nan_wrench's own mutable call_count_ already documents for a const wrench_b() (R3's
// own acceptance evidence). true writes from step() instead -- the one sanctioned write point a
// real WrenchSource may use (R4's own acceptance evidence, simulating a future M4 ActuatorModel).
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class InputConsumer final : public astra::WrenchSource {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    const std::string id = config.text("id", "");
    const std::string prefix = id.empty() ? "input_consumer." : "input_consumer." + id + ".";
    registry.add(prefix + "k1", recorded_.data() + 0, "rad");
    registry.add(prefix + "k2", recorded_.data() + 1, "rad");
    registry.add(prefix + "k3", recorded_.data() + 2, "rad");
    registry.add(prefix + "k4", recorded_.data() + 3, "rad");
    if (config.has("write_new_value")) {
      new_value_ = config.number("write_new_value");
      write_from_step_ = config.boolean("write_from_step", false);
      should_write_ = true;
    }
  }

  // ADR-0017: registers this plugin's one writable input. canonical_ is the registry-owned const
  // double* every wrench_b() call reads through; handle_ is the repeatable write handle R3/R4's own
  // test behavior below writes through -- neither is a plugin-owned value.
  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    canonical_ = inputs.add(registry, "controls.elevator", "rad");
    handle_ = inputs.find("controls.elevator");
  }

  void reset() override {
    call_count_ = 0;
    recorded_.fill(0.0);
    // ADR-0017: a conforming WrenchSource's reset() must never write through a cached InputHandle
    // (wrench_source.hpp) -- handle_ is left untouched here, on purpose.
  }

  // Resets this frame's call counter before Vehicle::integrate() ever calls wrench_b()
  // (nan_wrench's own exact pattern). The sanctioned, non-exceptional write point (R4): committed
  // by THIS frame's latch(), which runs after step(), before integrate() (simulation.cpp).
  void step(const astra::StepContext& /*ctx*/) override {
    call_count_ = 0;
    if (should_write_ && write_from_step_) {
      handle_.write(new_value_);
    }
  }

  [[nodiscard]] std::string_view name() const override { return "input_consumer"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    // 1-based: the first call of the frame makes call_count_ 1 (k1), the second 2 (k2), and so on
    // -- R3's own labeled, test-only exception to wrench_source.hpp's amended purity contract fires
    // on the SECOND call (k2): a real WrenchSource must never do this (this plugin's own header
    // comment says so explicitly, the same shape tests/plugins/nan_wrench's own mutable call_count_
    // documents for a different, 0-based counting convention).
    ++call_count_;
    if (should_write_ && !write_from_step_ && call_count_ == 2) {
      handle_.write(new_value_);
    }
    recorded_[call_count_ - 1] = *canonical_;
    return {};
  }

 private:
  const double* canonical_ = nullptr;
  astra::InputHandle handle_;
  double new_value_ = 0.0;
  bool should_write_ = false;
  bool write_from_step_ = false;
  mutable std::uint32_t call_count_ = 0;
  mutable std::array<double, 4> recorded_{};
};

std::unique_ptr<astra::Model> create() { return std::make_unique<InputConsumer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "input_consumer", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
