// A plugin whose descriptor carries the core API version just before this core's, standing in for a
// plugin built before the latest kCoreApiVersion bump. For example, one built against version 9,
// before ADR-0032 (M7 PR 3b) gave EnvironmentSample its omega_ie_b. The loader must reject it at
// load with PluginApiVersionError naming the plugin, never hand it a struct whose layout it does
// not know (core_api.hpp.in). tests/plugins/bad_version covers the version after this core's; the
// "- 1" keeps this one the preceding version through any later bump.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class StaleApiVersionModel final : public astra::Model {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "stale_api_version"; }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<StaleApiVersionModel>(); }

constexpr astra::PluginDescriptor kDescriptor{
    "test", "stale_api_version", astra::kCoreApiVersion - 1, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
