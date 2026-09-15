// The M0 dummy plugin: the smallest thing astra_add_plugin can build and the loader can load
// (ADR-0009). Its shape is the shape of every real plugin: a Model subclass, a create(), a
// descriptor, one export. M1 PR 2 gives it two published signals ("dummy.steps", "dummy.t") so a
// Simulation run has something to observe under the "v0." namespace (ADR-0011); it still ignores
// its own config, since nothing here needs a parameter yet.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class DummyModel final : public astra::Model {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    registry.add("dummy.steps", &steps_, "1");
    registry.add("dummy.t", &t_, "s");
  }
  void reset() override {
    steps_ = 0.0;
    t_ = 0.0;
  }
  void step(const astra::StepContext& ctx) override {
    ++steps_;
    t_ = ctx.t;
  }
  [[nodiscard]] std::string_view name() const override { return "dummy"; }

 private:
  double steps_ = 0.0;
  double t_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<DummyModel>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "dummy", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
