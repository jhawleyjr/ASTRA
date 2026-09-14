// A plugin whose descriptor carries a toolchain string this core was not built with, standing in
// for a plugin compiled by an older configure. The loader must reject it with PluginToolchainError.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class BadToolchainModel final : public astra::Model {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "bad_toolchain"; }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<BadToolchainModel>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "bad_toolchain", astra::kCoreApiVersion,
                                              "stale toolchain", &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
