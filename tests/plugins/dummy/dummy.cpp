// The M0 dummy plugin: the smallest thing astra_add_plugin can build and the loader can load
// (ADR-0009). Its shape is the shape of every real plugin: a Model subclass, a create(), a
// descriptor, one export.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class DummyModel final : public astra::Model {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "dummy"; }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<DummyModel>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "dummy", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
