// A test-only variant of the notional transport's sim-side autopilot peer: M7b's oracle (b)'s four
// porting controls (docs/plans/m7b-pr3-autopilot-peer.md §2 and §9; the M7b plan's amendment 2;
// ADR-0034 Decision 5). It holds the shipped peer's class from its header,
// aircraft/notional_transport/sim/transport/autopilot/autopilot_peer.hpp, and never links or loads
// the shipped plugin. The shipped peer carries no defect; this plugin reads one key,
// `transport.peer.test_defect`, builds BasicAutopilotPeer<DefectLaw> for it at init, and forwards
// every Transport call to it. Each defect law is one porting slip, written only here:
// - float_elevator_trim: the elevator trim stored in single precision before it enters the offset;
// - output_before_integral: the PID returns its output computed before its integral update;
// - float_pitch_rate: kPitchRate stored in single precision;
// - mode_word_zero: the mode word written 0 instead of kModeFlags.
// tests/plugins is skipped by scripts/config_keys.py, so its key is not documented there.
//
// Test code: the inner peer is allocated at init; the per-frame calls only forward.
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "../../../aircraft/notional_transport/sim/transport/autopilot/autopilot_peer.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/plugin/descriptor.hpp"
#include "icd/astra_icd.h"

namespace {

namespace peer = astra::aircraft_notional_transport_autopilot;

// float_elevator_trim: the elevator trim rounded to float, then widened, before the law sees it.
class FloatElevatorTrimLaw : public peer::Law {
 public:
  FloatElevatorTrimLaw(const peer::Setpoints& setpoints, const peer::Trims& trims)
      : peer::Law(setpoints,
                  peer::Trims{static_cast<double>(static_cast<float>(trims.elevator_rad)),
                              trims.throttle, trims.theta_rad}) {}
};

// output_before_integral: pid.hpp's update with one slip, the output taken before the integral
// advances (so a step's own increment reaches the command one exchange late).
class OutputFirstPid {
 public:
  explicit constexpr OutputFirstPid(const peer::PidGains& gains) : gains_(gains) {}

  [[nodiscard]] double update(double error, double error_rate, double dt, double offset,
                              double sign, const peer::Range& range) {
    const double proportional = gains_.kp * error + gains_.kd * error_rate;
    const double before = offset + sign * (proportional + integral_);
    const double output = peer::Pid::clamp(before, range);
    if (dt > 0.0) {
      const double candidate = integral_ + gains_.ki * error * dt;
      const double after = offset + sign * (proportional + candidate);
      if (std::isfinite(candidate) &&
          (inside(after, range) || distance(after, range) < distance(before, range))) {
        integral_ = candidate;
      }
    }
    return output;
  }

  [[nodiscard]] constexpr double integral() const { return integral_; }

 private:
  [[nodiscard]] static bool inside(double value, const peer::Range& range) {
    return value >= range.lo && value <= range.hi;
  }
  [[nodiscard]] static double distance(double value, const peer::Range& range) {
    // NOLINTNEXTLINE(readability-avoid-nested-conditional-operator): pid.hpp:54's own expression
    return value < range.lo ? range.lo - value : (value > range.hi ? value - range.hi : 0.0);
  }

  peer::PidGains gains_;
  double integral_ = 0.0;
};

using OutputFirstLaw = peer::BasicLaw<OutputFirstPid>;

// float_pitch_rate: kPitchRate rounded to float, then widened.
peer::Gains float_pitch_rate_gains() {
  peer::Gains gains;
  gains.pitch_rate = static_cast<double>(static_cast<float>(peer::kPitchRate));
  return gains;
}

class FloatPitchRateLaw : public peer::Law {
 public:
  FloatPitchRateLaw(const peer::Setpoints& setpoints, const peer::Trims& trims)
      : peer::Law(setpoints, trims, float_pitch_rate_gains()) {}
};

// mode_word_zero: the reply's mode word written 0.
class ModeWordZeroLaw : public peer::Law {
 public:
  using peer::Law::Law;
  void update(const astra_sim_to_sw_v1& in, double now, astra_sw_to_sim_v1& out) noexcept {
    peer::Law::update(in, now, out);
    out.mode_flags = 0U;
  }
};

std::unique_ptr<astra::Transport> make_peer(const astra::Config& peer_block) {
  const std::string name = peer_block.text("test_defect");
  if (name == "float_elevator_trim") {
    return std::make_unique<peer::BasicAutopilotPeer<FloatElevatorTrimLaw>>();
  }
  if (name == "output_before_integral") {
    return std::make_unique<peer::BasicAutopilotPeer<OutputFirstLaw>>();
  }
  if (name == "float_pitch_rate") {
    return std::make_unique<peer::BasicAutopilotPeer<FloatPitchRateLaw>>();
  }
  if (name == "mode_word_zero") {
    return std::make_unique<peer::BasicAutopilotPeer<ModeWordZeroLaw>>();
  }
  throw astra::ConfigError(peer_block.origin(), peer_block.path() + ".test_defect",
                           "unknown test defect: " + name);
}

class VariantPeer final : public astra::Transport {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    inner_ = make_peer(config.at("peer"));
    inner_->init(config, registry);
  }
  void reset() override { inner_->reset(); }
  void step(const astra::StepContext& ctx) override { inner_->step(ctx); }
  [[nodiscard]] std::string_view name() const override { return "autopilot_peer_variant"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }
  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& settings) override {
    return inner_->open(settings);
  }
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    inner_->publish(request, seq);
  }
  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t unchanged) noexcept override {
    return inner_->wait_ack(unchanged);
  }
  void read_reply(astra_sw_to_sim_v1& reply) noexcept override { inner_->read_reply(reply); }
  void close(astra::CloseReason reason) noexcept override { inner_->close(reason); }

 private:
  std::unique_ptr<astra::Transport> inner_;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<VariantPeer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "autopilot_peer_variant",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
