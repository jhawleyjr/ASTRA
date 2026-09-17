// A broken AtmosphereModel that returns an all-NaN AtmoState at every altitude, inside its own
// (deliberately enormous) valid_range() -- for the disclosed-gap regression test in
// tests/unit/kernel/simulation_halt_test.cpp (NanAtmosphereInRangeHaltsNamingTheWrenchConsumer,
// Codex adversarial review, M2 PR 3): a NaN AtmoState reaching a WrenchSource halts naming the
// WRENCH source, not this plugin, the same misattribution wind's own NaN already has
// (environment_sample.hpp). This plugin exists to make that wrong attribution assertable, not
// just claimed -- and, by declaring every altitude in range, to isolate a genuinely broken
// implementation from the documented (and separately tested, isa1976_test.cpp) out-of-range
// contract at() itself defines.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class NanAtmosphere final : public astra::AtmosphereModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "nan_atmosphere"; }

  [[nodiscard]] astra::AtmoState at(double /*altitude_m*/) const noexcept override {
    return astra::AtmoState{};  // every field defaults to NaN (atmosphere_model.hpp)
  }

  // Wide enough to cover any altitude a test scenario could use, so a halt this plugin causes is
  // never confused with at()'s own documented out-of-range contract.
  [[nodiscard]] astra::AltitudeRange valid_range() const noexcept override {
    return {-1.0e9, 1.0e9};  // NOLINT(readability-magic-numbers): a deliberately huge bound
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<NanAtmosphere>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "nan_atmosphere", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
