// A plugin whose descriptor claims a core_api_version this core does not have, standing in for a
// plugin left behind by a stale build. The loader must reject it with PluginApiVersionError naming
// the plugin.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class BadVersionModel final : public astra::Model {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "bad_version"; }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<BadVersionModel>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "bad_version", astra::kCoreApiVersion + 1,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
