// A Transport whose descriptor is valid but whose frames claim ICD version ASTRA_ICD_VERSION + 1,
// so Simulation::from_config must reject it at load (docs/architecture.md §15: "Every plugin's
// descriptor checked: ... ICD version for transports"; M4 PR 3). NO_LOAD_ALL: it exists to be
// rejected. Every per-frame method is inert; none is ever reached.
#include <cstdint>
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/plugin/descriptor.hpp"
#include "icd/astra_icd.h"

namespace {

class WrongIcdTransport final : public astra::Transport {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "wrong_icd_transport"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override {
    return ASTRA_ICD_VERSION + 1U;
  }
  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    return 0;
  }
  void publish(const astra_sim_to_sw_v1& /*request*/, std::uint32_t /*seq*/) noexcept override {}
  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t /*unchanged*/) noexcept override {
    return {};
  }
  void read_reply(astra_sw_to_sim_v1& /*reply*/) noexcept override {}
  void close(astra::CloseReason /*reason*/) noexcept override {}
};

std::unique_ptr<astra::Model> create() { return std::make_unique<WrongIcdTransport>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "wrong_icd_transport", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
