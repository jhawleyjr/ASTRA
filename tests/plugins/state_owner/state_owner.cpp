// A configurable state-owning Model (ADR-0021, docs/plans/pr4a-dynamics-model.md), reused via
// different YAML fixtures across every coupling/duplicate/missing-peer/pointer-stability test --
// this repo's existing frugal-plugin-count style (counting_aero, nan_wrench).
//
// state_count() == 1. Config keys:
//   instance          (optional string): folded into name() ("state_owner" with none configured,
//                      "state_owner_<instance>" otherwise) -- the disambiguation mechanism the
//                      Vehicle two-phase init design assumes a plugin author uses when more than
//                      one instance publishes state on one vehicle, mirroring turbofan's own
//                      "station" key. Two instances with the SAME resolved name collide on their
//                      own published signal name by design -- see vehicle.hpp's own top comment.
//   initial_value     (optional double, default 0.0): what initial_state() writes.
//   rate              (optional double, default 0.0): this instance's own constant derivative,
//                      used when resolve_peer_name is absent (the "driver" role in a coupling
//                      test: a known, closed-form-integrable ramp).
//   resolve_peer_name (optional string): if present, resolve_peers() resolves it via
//                      registry.find(), throwing ConfigError naming it if not found; when
//                      resolved, integrate_state() reads *that* pointer's CURRENT (sub-stage-
//                      fresh) value as this instance's own derivative instead of rate (the
//                      "reader" role: RK4-integrates the peer's own live value, so a stale,
//                      frame-latched read would integrate to a different, independently
//                      computable closed form than a genuinely sub-stage-fresh one).
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

class StateOwner final : public astra::Model {
 public:
  void init(const Config& config, astra::SignalRegistry& /*registry*/) override {
    origin_ = std::string(config.origin());
    config_path_ = config.path();
    const std::string instance = config.text("instance", "");
    name_ = instance.empty() ? "state_owner" : "state_owner_" + instance;
    initial_value_ = config.number("initial_value", 0.0);
    rate_ = config.number("rate", 0.0);
    resolve_peer_name_ = config.text("resolve_peer_name", "");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return name_; }

  [[nodiscard]] int state_count() const noexcept override { return 1; }

  void initial_state(double* q) const noexcept override { q[0] = initial_value_; }

  void integrate_state(double /*t*/, const astra::VehicleState& /*vs*/,
                       const astra::EnvironmentSample& /*env*/,
                       double* dq) const noexcept override {
    dq[0] = (peer_value_ != nullptr) ? *peer_value_ : rate_;
  }

  void resolve_peers(const astra::SignalRegistry& registry) override {
    if (resolve_peer_name_.empty()) {
      return;
    }
    peer_value_ = registry.find(resolve_peer_name_);
    if (peer_value_ == nullptr) {
      throw ConfigError(origin_, config_path_ + ".resolve_peer_name",
                        "no signal named '" + resolve_peer_name_ + "'");
    }
  }

 private:
  std::string name_ = "state_owner";
  std::string origin_;
  std::string config_path_;
  double initial_value_ = 0.0;
  double rate_ = 0.0;
  std::string resolve_peer_name_;
  const double* peer_value_ = nullptr;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<StateOwner>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "state_owner", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
