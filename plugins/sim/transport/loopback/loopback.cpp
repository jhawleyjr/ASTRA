// LoopbackTransport (ADR-0007, docs/architecture.md §10; M4 PR 3, docs/plans/m4-pr3-core-seam.md):
// an in-process peer for tests, with no second process, no shared memory, no thread, and no clock
// (ADR-0023: the loopback reads none). Its reply comes from a responder built from config -- Joe's
// Q-B decision reads ADR-0007's "calls an in-process functor" as this config-scripted callable, so
// every test stays on Simulation::from_config under all four presets.
//
// Everything below is read from `transport.peer` (all optional; the sim-owned keys of `transport`
// are Simulation's):
//
//   peer:
//     commands: { elevator: 0.0, aileron: 0.0, rudder: 0.0, throttle: [0.5, 0.5] }  # default 0
//     mode_flags: 0              # echoed as astra_sw_to_sim_v1.mode_flags; fault_flags is 0
//     schedule:                  # from `frame` on, the listed fields change (frames ascending)
//       - { frame: 10, elevator: 0.03 }
//     spurious_wakeups: 0        # unchanged ack observations before every ack (never a failure)
//     handshake_fail: false      # open() throws: a handshake failure
//     faults:                    # applied at exactly `frame`; several may share one frame
//       - { frame: 30, kind: out_of_order_ack }
//
// Fault kinds: out_of_order_ack (the ack moves to seq + 1), stale_reply_frame (the reply echoes
// the previous exchange's frame, or frame + 1 before any), bad_magic, bad_version, timeout (the
// deadline "expires": a late ack), peer_closed, and non_finite { field: elevator | aileron | rudder
// | throttle_0 | throttle_1, value: nan | inf | -inf }. Throttle mismatches and limit violations
// need no fault kind: script unequal throttles or an out-of-range value through commands/schedule.
//
// Per-frame methods allocate nothing: the schedule and faults are vectors built at init and
// walked by index.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/plugin/descriptor.hpp"
#include "icd/astra_icd.h"

namespace {

constexpr std::size_t kFieldCount = 5;  // elevator, aileron, rudder, throttle[0], throttle[1]
constexpr std::size_t kThrottle0 = 3;
constexpr std::size_t kThrottle1 = 4;
constexpr std::uint32_t kSentinel = std::numeric_limits<std::uint32_t>::max();  // ADR-0024

enum class FaultKind : std::uint8_t {
  kOutOfOrderAck,
  kStaleReplyFrame,
  kBadMagic,
  kBadVersion,
  kTimeout,
  kPeerClosed,
  kNonFinite
};

struct Fault {
  std::uint64_t frame = 0;
  FaultKind kind = FaultKind::kTimeout;
  std::size_t field = 0;  // kNonFinite only
  double value = 0.0;     // kNonFinite only
};

// One schedule entry: from `frame` on, each field with has[i] takes value[i].
struct Change {
  std::uint64_t frame = 0;
  std::array<bool, kFieldCount> has{};
  std::array<double, kFieldCount> value{};
};

std::string keyed(const astra::Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

std::uint64_t read_frame(const astra::Config& entry) {
  const std::int64_t frame = entry.integer("frame");
  if (frame < 0) {
    throw astra::ConfigError(entry.origin(), keyed(entry, "frame"), "must not be negative");
  }
  return static_cast<std::uint64_t>(frame);
}

// Reads elevator/aileron/rudder/throttle ([t0, t1]) present in `entry` into has/value.
void read_fields(const astra::Config& entry, std::array<bool, kFieldCount>& has,
                 std::array<double, kFieldCount>& value) {
  constexpr std::array<std::string_view, 3> kSurfaces = {"elevator", "aileron", "rudder"};
  for (std::size_t i = 0; i < kSurfaces.size(); ++i) {
    if (entry.has(kSurfaces[i])) {
      has[i] = true;
      value[i] = entry.number(kSurfaces[i]);
    }
  }
  if (entry.has("throttle")) {
    const astra::Config throttle = entry.at("throttle");
    if (!throttle.is_sequence() || throttle.size() != 2) {
      throw astra::ConfigError(throttle.origin(), throttle.path(), "must be a list [t0, t1]");
    }
    has[kThrottle0] = has[kThrottle1] = true;
    value[kThrottle0] = throttle.at(std::size_t{0}).number();
    value[kThrottle1] = throttle.at(std::size_t{1}).number();
  }
}

Fault read_fault(const astra::Config& entry) {
  Fault fault;
  fault.frame = read_frame(entry);
  const std::string kind = entry.text("kind");
  if (kind == "out_of_order_ack") {
    fault.kind = FaultKind::kOutOfOrderAck;
  } else if (kind == "stale_reply_frame") {
    fault.kind = FaultKind::kStaleReplyFrame;
  } else if (kind == "bad_magic") {
    fault.kind = FaultKind::kBadMagic;
  } else if (kind == "bad_version") {
    fault.kind = FaultKind::kBadVersion;
  } else if (kind == "timeout") {
    fault.kind = FaultKind::kTimeout;
  } else if (kind == "peer_closed") {
    fault.kind = FaultKind::kPeerClosed;
  } else if (kind == "non_finite") {
    fault.kind = FaultKind::kNonFinite;
    constexpr std::array<std::string_view, kFieldCount> kFields = {"elevator", "aileron", "rudder",
                                                                   "throttle_0", "throttle_1"};
    const std::string field = entry.text("field");
    std::size_t index = kFieldCount;
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      if (field == kFields[i]) {
        index = i;
      }
    }
    if (index == kFieldCount) {
      throw astra::ConfigError(entry.origin(), keyed(entry, "field"),
                               "must be elevator, aileron, rudder, throttle_0, or throttle_1");
    }
    fault.field = index;
    const std::string value = entry.text("value");
    if (value == "nan") {
      fault.value = std::numeric_limits<double>::quiet_NaN();
    } else if (value == "inf") {
      fault.value = std::numeric_limits<double>::infinity();
    } else if (value == "-inf") {
      fault.value = -std::numeric_limits<double>::infinity();
    } else {
      throw astra::ConfigError(entry.origin(), keyed(entry, "value"), "must be nan, inf, or -inf");
    }
  } else {
    throw astra::ConfigError(entry.origin(), keyed(entry, "kind"), "unknown loopback fault kind");
  }
  return fault;
}

class LoopbackTransport final : public astra::Transport {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& /*registry*/) override {
    if (!config.has("peer")) {
      return;
    }
    const astra::Config peer = config.at("peer");
    if (peer.has("commands")) {
      std::array<bool, kFieldCount> has{};
      read_fields(peer.at("commands"), has, commands_);
    }
    const std::int64_t mode_flags = peer.integer("mode_flags", 0);
    if (mode_flags < 0 || mode_flags > std::numeric_limits<std::uint32_t>::max()) {
      throw astra::ConfigError(peer.origin(), keyed(peer, "mode_flags"), "must fit in uint32");
    }
    mode_flags_ = static_cast<std::uint32_t>(mode_flags);
    const std::int64_t spurious = peer.integer("spurious_wakeups", 0);
    if (spurious < 0 || spurious > std::numeric_limits<std::uint32_t>::max()) {
      throw astra::ConfigError(peer.origin(), keyed(peer, "spurious_wakeups"),
                               "must fit in uint32");
    }
    spurious_wakeups_ = static_cast<std::uint32_t>(spurious);
    handshake_fail_ = peer.boolean("handshake_fail", false);
    if (peer.has("schedule")) {
      const astra::Config schedule = peer.at("schedule");
      for (std::size_t i = 0; i < schedule.size(); ++i) {
        const astra::Config entry = schedule.at(i);
        Change change;
        change.frame = read_frame(entry);
        if (!schedule_.empty() && change.frame <= schedule_.back().frame) {
          throw astra::ConfigError(entry.origin(), keyed(entry, "frame"),
                                   "schedule frames must be strictly ascending");
        }
        read_fields(entry, change.has, change.value);
        schedule_.push_back(change);
      }
    }
    if (peer.has("faults")) {
      const astra::Config faults = peer.at("faults");
      for (std::size_t i = 0; i < faults.size(); ++i) {
        faults_.push_back(read_fault(faults.at(i)));
      }
    }
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "loopback"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }

  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    if (handshake_fail_) {
      throw std::runtime_error("scripted handshake failure (transport.peer.handshake_fail)");
    }
    ack_ = kSentinel;
    return ack_;
  }

  // Builds the reply now (the peer "computes" as soon as it sees the request) and arms this
  // publication's scripted ack sequence.
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    while (next_change_ < schedule_.size() && schedule_[next_change_].frame <= request.frame) {
      const Change& change = schedule_[next_change_];
      for (std::size_t i = 0; i < kFieldCount; ++i) {
        if (change.has[i]) {
          commands_[i] = change.value[i];
        }
      }
      ++next_change_;
    }
    std::array<double, kFieldCount> values = commands_;

    std::memset(&reply_, 0, sizeof reply_);  // ADR-0022 Determinism: the peer zeroes its reply
    reply_.magic = ASTRA_ICD_MAGIC;
    reply_.version = ASTRA_ICD_VERSION;
    reply_.frame = request.frame;
    reply_.mode_flags = mode_flags_;
    reply_.fault_flags = 0;
    ack_result_ = astra::AckObservation{astra::AckStatus::kObserved, seq};
    for (const Fault& fault : faults_) {
      if (fault.frame != request.frame) {
        continue;
      }
      switch (fault.kind) {
        case FaultKind::kOutOfOrderAck:
          ack_result_.ack_seq = seq + 1U;
          break;
        case FaultKind::kStaleReplyFrame:
          reply_.frame = replied_any_ ? last_replied_frame_ : request.frame + 1U;
          break;
        case FaultKind::kBadMagic:
          reply_.magic = ASTRA_ICD_MAGIC ^ 1U;
          break;
        case FaultKind::kBadVersion:
          reply_.version = ASTRA_ICD_VERSION + 1U;
          break;
        case FaultKind::kTimeout:
          ack_result_ = astra::AckObservation{astra::AckStatus::kExpired, ack_};
          break;
        case FaultKind::kPeerClosed:
          ack_result_ = astra::AckObservation{astra::AckStatus::kPeerClosed, ack_};
          break;
        case FaultKind::kNonFinite:
          values[fault.field] = fault.value;
          break;
      }
    }
    reply_.elevator = values[0];
    reply_.aileron = values[1];
    reply_.rudder = values[2];
    reply_.throttle[0] = values[kThrottle0];
    reply_.throttle[1] = values[kThrottle1];
    last_replied_frame_ = request.frame;
    replied_any_ = true;
    spurious_left_ = spurious_wakeups_;
  }

  // Scripted: spurious_wakeups unchanged observations, then the armed result. The loopback's
  // "deadline" is the script itself (no clock, ADR-0023).
  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t unchanged) noexcept override {
    if (spurious_left_ > 0) {
      --spurious_left_;
      return astra::AckObservation{astra::AckStatus::kObserved, unchanged};
    }
    if (ack_result_.status == astra::AckStatus::kObserved) {
      ack_ = ack_result_.ack_seq;
    }
    return ack_result_;
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    std::memcpy(&reply, &reply_, sizeof reply);
  }

  void close(astra::CloseReason /*reason*/) noexcept override {}

 private:
  std::array<double, kFieldCount> commands_{};
  std::uint32_t mode_flags_ = 0;
  std::uint32_t spurious_wakeups_ = 0;
  bool handshake_fail_ = false;
  std::vector<Change> schedule_;
  std::vector<Fault> faults_;

  std::size_t next_change_ = 0;
  astra_sw_to_sim_v1 reply_{};
  astra::AckObservation ack_result_{};
  std::uint32_t ack_ = kSentinel;
  std::uint32_t spurious_left_ = 0;
  std::uint64_t last_replied_frame_ = 0;
  bool replied_any_ = false;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<LoopbackTransport>(); }

constexpr astra::PluginDescriptor kDescriptor{"sim", "transport_loopback", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
