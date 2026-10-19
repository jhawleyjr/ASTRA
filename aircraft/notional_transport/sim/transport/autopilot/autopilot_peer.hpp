// The notional transport's sim-side autopilot peer (M7b's oracle (b),
// docs/plans/m7b-pr3-autopilot-peer.md §2; ADR-0029 Decision 1): an in-process Transport, after
// LoopbackTransport's and nesc_control's precedent, that runs autopilot_law.hpp's second
// implementation of M4's law on each astra_sim_to_sw_v1 frame the exchange packs and replies with
// its astra_sw_to_sim_v1 commands. Its config keys, and the plugin's descriptor, are in
// autopilot.cpp. The class lives in this header so that tests/plugins/autopilot_peer_variant can
// hold one by including it, never by linking the shipped .so.
//
// What it does at each exchange (publish()): the law's update on the request at now = the
// request's t, which is what embedded/autopilot's HAL hands its law (hal_now() returns the t of the
// last request, embedded/hal/shm/hal_shm.cpp:198 and :229; main.cpp:59). The law zeroes the reply
// first (ADR-0022 Determinism) and echoes the request's frame. It never refuses a frame: an
// unusable one holds the previous commands, as the port does. Its commands reach the inputs only
// through the exchange's command map (ADR-0017, ADR-0022), staged and latched the same frame. It
// reads no clock (ADR-0023) and holds no write handle.
//
// LawType is the law it runs: Law in the shipped plugin, which instantiates only
// BasicAutopilotPeer<Law> with Gains{}; the variant's porting controls are its other
// instantiations.
//
// Signals (registered at init; R6 excludes transport.* by construction, and compares the two counts
// with the autopilot's exit line instead):
//   transport.peer.handled           | 1   | frames the law has handled (the autopilot's "handled")
//   transport.peer.held              | 1   | frames it held (the autopilot's "held")
//   transport.peer.altitude_setpoint | m   | peer.setpoints.altitude, as parsed
//   transport.peer.airspeed_setpoint | m/s | peer.setpoints.airspeed, as parsed
//   transport.peer.heading_setpoint  | rad | peer.setpoints.heading, as parsed
//   transport.peer.elevator_trim     | rad | peer.trim.elevator, as parsed
//   transport.peer.throttle_trim     | 1   | peer.trim.throttle, as parsed
//   transport.peer.theta_trim        | rad | peer.trim.theta, as parsed
//
// Per-frame methods allocate nothing and throw nothing; init() may allocate and throws a
// ConfigError naming the key.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/signals/signal_registry.hpp"
#include "autopilot_law.hpp"
#include "icd/astra_icd.h"

namespace astra::aircraft_notional_transport_autopilot {

template <class LawType = Law>
class BasicAutopilotPeer : public astra::Transport {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    const astra::Config peer = config.at("peer");
    read_setpoints(peer.at("setpoints"));
    read_trim(peer.at("trim"));
    reset();
    registry.add("transport.peer.handled", &handled_, "1");
    registry.add("transport.peer.held", &held_, "1");
    registry.add("transport.peer.altitude_setpoint", &setpoints_.altitude_m, "m");
    registry.add("transport.peer.airspeed_setpoint", &setpoints_.airspeed_mps, "m/s");
    registry.add("transport.peer.heading_setpoint", &setpoints_.heading_rad, "rad");
    registry.add("transport.peer.elevator_trim", &trims_.elevator_rad, "rad");
    registry.add("transport.peer.throttle_trim", &trims_.throttle, "1");
    registry.add("transport.peer.theta_trim", &trims_.theta_rad, "rad");
  }
  void reset() override {
    law_ = LawType{setpoints_, trims_};
    handled_ = 0.0;
    held_ = 0.0;
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "autopilot"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }

  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    ack_ = kSentinel;
    return ack_;
  }

  // The peer computes as soon as it sees the request, as the loopback does.
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    law_.update(request, request.t, reply_);
    pending_ = seq;
    handled_ = static_cast<double>(law_.handled());
    held_ = static_cast<double>(law_.held());
  }

  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t /*unchanged*/) noexcept override {
    ack_ = pending_;
    return astra::AckObservation{astra::AckStatus::kObserved, ack_};
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    std::memcpy(&reply, &reply_, sizeof reply);
  }

  void close(astra::CloseReason /*reason*/) noexcept override {}

  // The law in use, for tests (tests/autopilot_peer).
  [[nodiscard]] const LawType& law() const noexcept { return law_; }

 private:
  static constexpr std::uint32_t kSentinel = 0xFFFFFFFFU;  // ADR-0024

  static std::string keyed(const astra::Config& config, std::string_view key) {
    return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
  }

  // embedded/autopilot/cli.cpp:121-133's rule for each value, here a ConfigError naming the key:
  // finite (cli.cpp's parse_finite), and inside `range` where the CLI checks one.
  static double finite_number(const astra::Config& block, std::string_view key) {
    const double value = block.number(key);
    if (!std::isfinite(value)) {
      throw astra::ConfigError(block.origin(), keyed(block, key), "must be finite");
    }
    return value;
  }

  static double number_in(const astra::Config& block, std::string_view key, const Range& range,
                          std::string_view why) {
    const double value = finite_number(block, key);
    if (value < range.lo || value > range.hi) {
      throw astra::ConfigError(block.origin(), keyed(block, key), why);
    }
    return value;
  }

  void read_setpoints(const astra::Config& setpoints) {
    setpoints_.altitude_m = finite_number(setpoints, "altitude");
    setpoints_.airspeed_mps = finite_number(setpoints, "airspeed");
    if (setpoints_.airspeed_mps <= 0.0) {
      throw astra::ConfigError(setpoints.origin(), keyed(setpoints, "airspeed"), "must be above 0");
    }
    setpoints_.heading_rad = finite_number(setpoints, "heading");
  }

  // The trim is itself the reply until the first usable frame, so it must satisfy the output
  // limits (cli.hpp's rule).
  void read_trim(const astra::Config& trim) {
    trims_.elevator_rad = number_in(trim, "elevator", kElevatorLimit,
                                    "must lie within the elevator limit, +-0.3 rad");
    trims_.throttle = number_in(trim, "throttle", kThrottleLimit, "must lie in [0, 1]");
    trims_.theta_rad = finite_number(trim, "theta");
  }

  Setpoints setpoints_{0.0, 0.0, 0.0};
  Trims trims_{0.0, 0.0, 0.0};
  LawType law_{setpoints_, trims_};  // rebuilt by reset() from the config's values

  astra_sw_to_sim_v1 reply_{};
  std::uint32_t ack_ = kSentinel;
  std::uint32_t pending_ = kSentinel;

  double handled_ = 0.0;
  double held_ = 0.0;
};

using AutopilotPeer = BasicAutopilotPeer<>;

}  // namespace astra::aircraft_notional_transport_autopilot
