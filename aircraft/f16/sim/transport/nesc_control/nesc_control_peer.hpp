// The sim-side F16_control peer (ADR-0029 Decision 1; docs/plans/m7-pr11a-control-peer.md): an
// in-process Transport, after LoopbackTransport's precedent, that runs control_law.hpp's SI
// realization of NASA's F16_control.dml on each astra_sim_to_sw_v1 frame the exchange packs and
// replies with its astra_sw_to_sim_v1 commands. Its config keys, and the plugin's descriptor, are
// in nesc_control.cpp. The class lives in this header so that tests/plugins/nesc_control_variant
// can hold one by including it, never by linking the shipped .so (the plan's amendment 1).
//
// What it does at each exchange (publish()):
// 1. the refusal check (ADR-0029 Decision 2): a frame carrying CAS_SUPERSONIC, ATMOSPHERE_INVALID,
//    WIND_INVALID or LATLON_NOT_MODELED, or whose Mach is not above zero, is never computed from.
//    The reply's five commands are quiet NaN, so the exchange halts kTransport at its non-finite
//    check, naming astra_sw_to_sim_v1.elevator, and transport.peer.refusal holds the refused bits;
// 2. EAS from cas and mach, the body rate relative to the Earth, the law, and the three boundary
//    steps (control_law.hpp);
// 3. the reply, zeroed first (ADR-0022), echoing the request's frame.
// Its commands reach the inputs only through the exchange's command map (ADR-0017, ADR-0022),
// staged and latched the same frame. It reads no clock (ADR-0023) and holds no write handle.
//
// The assumption it cannot check (the plan's Q4, Joe's decision): it subtracts a rotating WGS-84
// Earth's rate, geodesy::kOmegaIe, from ICD v1's inertial body rates. The peer sees only the ICD
// frame and its own config block, so it cannot see the configured Earth model. A flat Earth is
// refused at every frame (LATLON_NOT_MODELED); a geodetic non-rotating Earth is not detected.
//
// Signals (registered at init, held between exchanges):
//   transport.peer.refusal             | 1   | the refused bits of the last exchange, 0 when none
//   transport.peer.equivalent_airspeed | m/s | the last exchange's EAS; NaN when refused
//
// Per-frame methods allocate nothing and throw nothing; init() may allocate and throws a
// ConfigError naming the key.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"
#include "control_law.hpp"
#include "icd/astra_icd.h"

namespace astra::aircraft_f16_nesc_control {

namespace law = astra::aircraft_f16_control_law;

// What a variant of the peer may change (tests/plugins/nesc_control_variant). The defaults are the
// decided ones (the M7 plan's Q11; ADR-0029 Decision 6), and the shipped plugin uses only those.
struct Options {
  law::References references{};
};

class NescControlPeer : public astra::Transport {
 public:
  explicit NescControlPeer(const Options& options = {}) : options_(options) {}

  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    const astra::Config peer = config.at("peer");
    read_trim(peer.at("trim"));
    read_commands(peer.at("commands"));
    registry.add("transport.peer.refusal", &refusal_, "1");
    registry.add("transport.peer.equivalent_airspeed", &equivalent_airspeed_, "m/s");
  }
  void reset() override {
    refusal_ = 0.0;
    equivalent_airspeed_ = std::numeric_limits<double>::quiet_NaN();
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "nesc_control"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }

  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    ack_ = kSentinel;
    return ack_;
  }

  // The peer computes as soon as it sees the request, as the loopback does.
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    std::memset(&reply_, 0, sizeof reply_);  // ADR-0022 Determinism: the peer zeroes its reply
    reply_.magic = ASTRA_ICD_MAGIC;
    reply_.version = ASTRA_ICD_VERSION;
    reply_.frame = request.frame;
    pending_ = seq;

    const std::uint32_t refused = law::refusal_bits(request);
    refusal_ = static_cast<double>(refused);
    if (refused != 0U) {
      constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
      equivalent_airspeed_ = kNan;
      reply_.elevator = kNan;
      reply_.aileron = kNan;
      reply_.rudder = kNan;
      reply_.throttle[0] = kNan;
      reply_.throttle[1] = kNan;
      return;
    }

    const law::Inputs in = law::inputs_from(request);
    equivalent_airspeed_ = in.eas;
    const law::IcdCommands icd =
        law::to_icd(law::evaluate(in, commands_, trim_, law::Modes{}, options_.references));
    reply_.elevator = icd.elevator;
    reply_.aileron = icd.aileron;
    reply_.rudder = icd.rudder;
    reply_.throttle[0] = icd.throttle;
    reply_.throttle[1] = icd.throttle;
  }

  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t /*unchanged*/) noexcept override {
    ack_ = pending_;
    return astra::AckObservation{astra::AckStatus::kObserved, ack_};
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    std::memcpy(&reply, &reply_, sizeof reply);
  }

  void close(astra::CloseReason /*reason*/) noexcept override {}

  // The trimmed pilot controls the peer derived at init (R23's route).
  [[nodiscard]] const law::Trim& trim() const noexcept { return trim_; }
  [[nodiscard]] const law::Commands& commands() const noexcept { return commands_; }

 private:
  static constexpr std::uint32_t kSentinel = 0xFFFFFFFFU;  // ADR-0024

  static std::string keyed(const astra::Config& config, std::string_view key) {
    return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
  }

  static double finite_number(const astra::Config& block, std::string_view key) {
    const double value = block.number(key);
    if (!std::isfinite(value)) {
      throw astra::ConfigError(block.origin(), keyed(block, key), "must be finite");
    }
    return value;
  }

  // The trim's physical controls, inverted through the law's own limit-and-scale block (R23).
  void read_trim(const astra::Config& trim) {
    const double elevator = finite_number(trim, "elevator");
    const double throttle = finite_number(trim, "throttle");
    trim_ = law::trim_from_controls(elevator, throttle);
    if (std::abs(trim_.long_stick) > 1.0) {
      throw astra::ConfigError(
          trim.origin(), keyed(trim, "elevator"),
          "gives a trimmed stick outside [-1, 1]: the elevator must lie within "
          "the law's +-25 deg");
    }
    if (throttle < 0.0 || throttle > 1.0) {
      throw astra::ConfigError(trim.origin(), keyed(trim, "throttle"), "must lie in [0, 1]");
    }
  }

  // The commands from frame 0, each converted once, here (ADR-0029 Decision 3, ADR-0030 rule 3).
  void read_commands(const astra::Config& commands) {
    commands_.altitude = astra::units::feet_to_meters(finite_number(commands, "altitude_ft"));
    const double knots = finite_number(commands, "equivalent_airspeed_knots");
    if (knots <= 0.0) {
      throw astra::ConfigError(commands.origin(), keyed(commands, "equivalent_airspeed_knots"),
                               "must be above 0");
    }
    commands_.eas = astra::units::knots_to_meters_per_second(knots);
    commands_.course = astra::units::degrees_to_radians(finite_number(commands, "course_deg"));
    commands_.lateral_deviation_error = 0.0;
  }

  Options options_;
  law::Trim trim_{};
  law::Commands commands_{};

  astra_sw_to_sim_v1 reply_{};
  std::uint32_t ack_ = kSentinel;
  std::uint32_t pending_ = kSentinel;

  double refusal_ = 0.0;
  double equivalent_airspeed_ = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace astra::aircraft_f16_nesc_control
