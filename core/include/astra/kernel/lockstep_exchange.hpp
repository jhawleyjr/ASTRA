// The lockstep exchange stage (ADR-0022; M4 PR 3, docs/plans/m4-pr3-core-seam.md). Owned by
// Simulation, which calls it at docs/architecture.md §5's point in the frame: at frame % divisor
// == 0, for the bound vehicle (v1: index 0), after that frame's environment sample and before
// vehicle.step(). It packs truth (icd_pack.hpp), publishes through the Transport, runs the receive
// state machine (transport.hpp's header comment), validates the reply in ADR-0022's order, and
// stages the config-declared command map through InputRegistry handles, so the same frame's
// latch() commits it (zero-order hold between exchanges).
//
// Every failure is terminal: HaltReason{kTransport, source, frame, vehicle_index}, where source is
// the transport plugin's path for a protocol failure, a static ICD field name for a non-finite,
// throttle-mismatch, or out-of-limit command, and "transport.recorder" for a full recorder. The
// stage stores the halt and returns it unchanged from every later pre_frame() -- and, per Joe's
// PR 3 addition, it stores EVERY halt Simulation::step() returns while a transport is configured
// (note_halt), so a physics halt on an exchange frame is never followed by a re-publication.
//
// Per-frame methods are noexcept and allocation-free; construction and open() may allocate/throw.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "astra/interfaces/transport.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "icd/astra_icd.h"

namespace astra {

class Config;              // core/include/astra/config/config.hpp
class EarthModel;          // core/include/astra/interfaces/earth_model.hpp
class SignalRegistry;      // core/include/astra/signals/signal_registry.hpp
class Vehicle;             // core/include/astra/kernel/vehicle.hpp
struct EnvironmentSample;  // core/include/astra/kernel/environment_sample.hpp

// Which transport failure halted the run, for diagnosis: HaltReason alone cannot tell a timeout
// from an out-of-order ack (both name the plugin). Also published as the "transport.fault" signal.
enum class TransportFault : std::uint8_t {
  kNone,
  kTimeout,
  kPeerClosed,
  kOutOfOrderAck,
  kBadMagic,
  kBadVersion,
  kFrameMismatch,
  kFrameDiscontinuity,
  kSequenceExhausted,
  kNonFiniteCommand,
  kThrottleMismatch,
  kCommandLimit,
  kRecorderFull
};

class LockstepExchange {
 public:
  // The five astra_sw_to_sim_v1 command values, in ADR-0022's checking order.
  static constexpr std::size_t kCommandCount = 5;
  // The four config keys under transport.commands / transport.limits (throttle covers both
  // throttle entries).
  static constexpr std::size_t kCommandKeyCount = 4;

  struct Limit {
    double lo = 0.0;
    double hi = 0.0;
  };

  // Init-time: reads every sim-owned key of `transport` (plugin, divisor, timeout_ms,
  // handshake_timeout_ms, peer_timeout_ms, commands, limits), loads and initializes the transport
  // plugin (whose own Model::init reads anything else, e.g. the loopback's "peer"), resolves the
  // command map through vehicle.inputs().find() -- so every bind_inputs() must already have run --
  // and registers the transport.* signals. Recorder capacity comes from sim_frames (Joe, Q-C).
  // Throws ConfigError naming the key, or a PluginError naming the plugin.
  LockstepExchange(const Config& transport, const PluginLoader& loader, Vehicle& vehicle,
                   SignalRegistry& registry, std::uint64_t sim_frames);
  ~LockstepExchange();

  LockstepExchange(const LockstepExchange&) = delete;
  LockstepExchange& operator=(const LockstepExchange&) = delete;
  LockstepExchange(LockstepExchange&&) = delete;
  LockstepExchange& operator=(LockstepExchange&&) = delete;

  // Init-time, last: records the initial state (frame `frame`), then runs the transport's
  // handshake. A handshake failure is a ConfigError at "transport" (ADR-0022), from `origin`.
  void open(const Vehicle& vehicle, std::uint64_t frame, std::string_view origin);

  // Top of every Simulation::step(): the stored halt, if any (sticky), else a kRecorderFull halt
  // when this frame's records (a state, plus a request and reply on an exchange frame) cannot fit.
  [[nodiscard]] HaltReason pre_frame(const StepContext& ctx) noexcept;

  [[nodiscard]] bool due(std::uint64_t frame) const noexcept { return frame % divisor_ == 0; }
  [[nodiscard]] std::uint32_t vehicle_index() const noexcept { return vehicle_index_; }

  // One exchange for `vehicle` at ctx.frame (see the header comment). `sample` is this frame's
  // environment sample, `earth` the configured Earth model.
  [[nodiscard]] HaltReason exchange(const StepContext& ctx, Vehicle& vehicle,
                                    const EnvironmentSample& sample,
                                    const EarthModel& earth) noexcept;

  // After a successful frame (the clock already advanced to new_frame): records the bound
  // vehicle's state and notes frame new_frame - 1 as completed (PreviousFrame's validity).
  void post_step(const Vehicle& vehicle, std::uint64_t new_frame) noexcept;

  // Stores `halt` if nothing is stored yet (Joe's PR 3 addition: every halt is sticky).
  void note_halt(const HaltReason& halt) noexcept;

  [[nodiscard]] TransportFault fault() const noexcept { return fault_; }
  // What the destructor will pass to Transport::close(): kAfterFailure once any halt is stored.
  [[nodiscard]] CloseReason close_reason() const noexcept {
    return halt_ ? CloseReason::kAfterFailure : CloseReason::kNormal;
  }
  [[nodiscard]] const ExchangeRecorder& recorder() const noexcept { return recorder_; }

 private:
  [[nodiscard]] HaltReason fail(TransportFault fault, std::string_view source,
                                const StepContext& ctx) noexcept;
  // Receive state machine, then read, record, validate, stage. Split out of exchange() for
  // readability; the same per-frame rules apply.
  [[nodiscard]] HaltReason receive(const StepContext& ctx, std::uint32_t seq) noexcept;
  [[nodiscard]] HaltReason validate_and_stage(const StepContext& ctx) noexcept;

  LoadedPlugin plugin_{};
  Transport* transport_ = nullptr;
  std::string plugin_path_;
  std::uint32_t vehicle_index_ = 0;
  std::uint32_t divisor_ = 1;
  TransportSettings settings_;
  std::array<InputHandle, kCommandKeyCount> handles_{};  // falsy = unmapped
  std::array<Limit, kCommandKeyCount> limits_{};
  ExchangeRecorder recorder_;

  astra_sim_to_sw_v1 request_{};
  astra_sw_to_sim_v1 reply_{};
  std::uint32_t last_ack_ = 0;
  std::uint64_t last_published_frame_ = 0;
  bool published_any_ = false;
  std::uint64_t last_completed_frame_ = 0;
  bool completed_any_ = false;
  bool opened_ = false;

  HaltReason halt_;
  TransportFault fault_ = TransportFault::kNone;
  double exchanges_ = 0.0;   // "transport.exchanges": completed, validated exchanges
  double missed_ = 0.0;      // "transport.missed": 1 once a transport failure halted the run
  double fault_code_ = 0.0;  // "transport.fault": static_cast<double>(fault_)
};

}  // namespace astra
