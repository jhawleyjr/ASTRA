// Discrete state feedback as an in-process lockstep peer, the law of M7b's oracle (a)
// (docs/plans/m7b-pr2-sampled-data.md, §2; docs/plans/m7b-closed-loop-oracles.md, "Oracle (a)").
// It is a Transport in nesc_control's shape (ADR-0029 Decisions 1 to 3): in-process, its keys under
// transport.peer, a schedule keyed to frames, a zeroed reply that echoes the frame, no clock, and
// no allocation per frame.
//
// At each exchange it replies u = G r - K x in ICD v1's elevator field, which reaches the plant
// through the exchange's validation, its limits, the command map and the latch (ADR-0022). It reads
// x from the plant's published stage-buffer pointers, resolved at init (the M7b plan's Q4, decided
// (R)): at an exchange the stage buffer holds the accepted frame-start state (ADR-0021). It still
// checks the ICD request and refuses one whose magic or version is not ICD v1's, whose frame is not
// its next expected k * divisor, or whose t is not frame * dt. A refusal replies a NaN elevator, so
// the exchange halts kTransport naming astra_sw_to_sim_v1.elevator.
//
// The plan's §5 charges the law's operation order (acc = g * r, then acc -= k[i] * x[i] in turn) in
// L's bound, so publish()'s loop is part of the oracle (ADR-0034 Decision 4). The peer carries no
// defect key: the wrong-divisor control is a configuration (the plan's §9).
//
// Config keys, under transport.peer (tests/plugins is skipped by scripts/config_keys.py):
//   state              | text           | required | the state signal prefix; <state>.0 to
//          <state>.(n-1) are resolved at init, n being k's length
//   divisor            | integer >= 1   | required | its own exchange divisor N
//   dt                 | number > 0     | required | its own dt, for the request's t check
//   k[]                | number         | required | the n gains
//   g                  | number         | required | the reference gain G
//   reference          | number         | required | r before the first schedule entry
//   schedule[].frame   | integer >= 0   | required | r changes from the first exchange at or after
//          this frame; strictly ascending
//   schedule[].reference | number       | required | r from that exchange on
//
// Published signals: transport.peer.k.<i>, .g, .dt, .divisor (the parsed values, for the plan's
// parse-identity check), .reference (r in force) and .refused (the count of refused requests).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "icd/astra_icd.h"

namespace {

using astra::Config;
using astra::ConfigError;

constexpr std::uint32_t kSentinel = std::numeric_limits<std::uint32_t>::max();  // ADR-0024

struct Change {
  std::uint64_t frame = 0;
  double reference = 0.0;
};

std::string keyed(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

// A list under key, checked to be a sequence before it is iterated (lesson 2026-10-13).
Config sequence_at(const Config& config, std::string_view key) {
  const Config entry = config.at(key);
  if (!entry.is_sequence() || entry.size() == 0) {
    throw ConfigError(config.origin(), keyed(config, key), "must be a non-empty list");
  }
  return entry;
}

class StateFeedbackPeer final : public astra::Transport {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    const Config peer = config.at("peer");
    const Config gains = sequence_at(peer, "k");
    for (std::size_t i = 0; i < gains.size(); ++i) {
      k_.push_back(gains.at(i).number());
    }
    const std::int64_t divisor = peer.integer("divisor");
    if (divisor < 1) {
      throw ConfigError(peer.origin(), keyed(peer, "divisor"), "must be at least 1");
    }
    divisor_ = static_cast<std::uint64_t>(divisor);
    divisor_signal_ = static_cast<double>(divisor_);
    dt_ = peer.number("dt");
    // NOLINTNEXTLINE(readability-simplify-boolean-expr): NaN-safe form, as elsewhere in the tree.
    if (!(dt_ > 0.0 && dt_ < std::numeric_limits<double>::infinity())) {
      throw ConfigError(peer.origin(), keyed(peer, "dt"), "must be finite and positive");
    }
    g_ = peer.number("g");
    reference_ = peer.number("reference");
    if (peer.has("schedule")) {
      const Config schedule = sequence_at(peer, "schedule");
      for (std::size_t i = 0; i < schedule.size(); ++i) {
        const Config entry = schedule.at(i);
        const std::int64_t frame = entry.integer("frame");
        if (frame < 0) {
          throw ConfigError(entry.origin(), keyed(entry, "frame"), "must not be negative");
        }
        const Change change{static_cast<std::uint64_t>(frame), entry.number("reference")};
        if (!schedule_.empty() && change.frame <= schedule_.back().frame) {
          throw ConfigError(entry.origin(), keyed(entry, "frame"),
                            "schedule frames must be strictly ascending");
        }
        schedule_.push_back(change);
      }
    }
    const std::string prefix = peer.text("state");
    for (std::size_t i = 0; i < k_.size(); ++i) {
      const std::string name = prefix + "." + std::to_string(i);
      const double* value = registry.find(name);
      if (value == nullptr) {
        throw ConfigError(peer.origin(), keyed(peer, "state"), "names no published signal " + name);
      }
      state_.push_back(value);
    }
    // Published after k_ is complete, so no pointer below can move.
    try {
      for (std::size_t i = 0; i < k_.size(); ++i) {
        registry.add("transport.peer.k." + std::to_string(i), &k_[i], "");
      }
      registry.add("transport.peer.g", &g_, "");
      registry.add("transport.peer.dt", &dt_, "s");
      registry.add("transport.peer.divisor", &divisor_signal_, "1");
      registry.add("transport.peer.reference", &reference_, "");
      registry.add("transport.peer.refused", &refused_, "1");
    } catch (const astra::Error& e) {
      throw ConfigError(peer.origin(), peer.path(), e.what());
    }
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "state_feedback_peer"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }

  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    return kSentinel;
  }

  // The peer computes as soon as it sees the request, as the loopback does.
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    while (next_change_ < schedule_.size() && schedule_[next_change_].frame <= request.frame) {
      reference_ = schedule_[next_change_].reference;
      ++next_change_;
    }
    const bool accepted =
        request.magic == ASTRA_ICD_MAGIC && request.version == ASTRA_ICD_VERSION &&
        request.frame == expected_frame_ && request.t == static_cast<double>(request.frame) * dt_;
    double command = std::numeric_limits<double>::quiet_NaN();
    if (accepted) {
      // The plan's §5: L's bound charges this order.
      double acc = g_ * reference_;
      for (std::size_t i = 0; i < k_.size(); ++i) {
        acc -= k_[i] * *state_[i];
      }
      command = acc;
    } else {
      refused_ += 1.0;
    }
    expected_frame_ += divisor_;

    std::memset(&reply_, 0, sizeof reply_);  // ADR-0022 Determinism: the peer zeroes its reply
    reply_.magic = ASTRA_ICD_MAGIC;
    reply_.version = ASTRA_ICD_VERSION;
    reply_.frame = request.frame;
    reply_.elevator = command;
    ack_ = seq;
  }

  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t /*unchanged*/) noexcept override {
    return astra::AckObservation{astra::AckStatus::kObserved, ack_};
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    std::memcpy(&reply, &reply_, sizeof reply);
  }

  void close(astra::CloseReason /*reason*/) noexcept override {}

 private:
  std::vector<double> k_;
  std::vector<const double*> state_;
  std::vector<Change> schedule_;
  std::uint64_t divisor_ = 1;
  double divisor_signal_ = 1.0;
  double dt_ = 0.0;
  double g_ = 0.0;
  double reference_ = 0.0;
  double refused_ = 0.0;

  std::size_t next_change_ = 0;
  std::uint64_t expected_frame_ = 0;
  astra_sw_to_sim_v1 reply_{};
  std::uint32_t ack_ = kSentinel;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<StateFeedbackPeer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "state_feedback_peer", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
