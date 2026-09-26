#include "astra/kernel/lockstep_exchange.hpp"

#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/icd_pack.hpp"

namespace astra {

namespace {

// ADR-0022's default transport divisor (100 Hz at the 1 ms major frame).
constexpr std::int64_t kDefaultDivisor = 10;

// The four transport.commands / transport.limits keys, and for each of the five command values
// (ADR-0022 checking order) its key index and the ICD field a halt names.
constexpr std::array<std::string_view, LockstepExchange::kCommandKeyCount> kCommandKeys = {
    "elevator", "aileron", "rudder", "throttle"};
constexpr std::size_t kThrottleKey = 3;
constexpr std::size_t kThrottle0 = 3;  // index into the five command values
constexpr std::size_t kThrottle1 = 4;
constexpr auto kRigidBodySize = static_cast<std::size_t>(kStateSize);
constexpr std::array<std::size_t, LockstepExchange::kCommandCount> kValueKey = {0, 1, 2, 3, 3};
constexpr std::array<std::string_view, LockstepExchange::kCommandCount> kFieldNames = {
    "astra_sw_to_sim_v1.elevator", "astra_sw_to_sim_v1.aileron", "astra_sw_to_sim_v1.rudder",
    "astra_sw_to_sim_v1.throttle[0]", "astra_sw_to_sim_v1.throttle[1]"};
constexpr std::string_view kThrottleField = "astra_sw_to_sim_v1.throttle";
constexpr std::string_view kRecorderSource = "transport.recorder";

// ADR-0024: the sentinel and the reserved shutdown value of req_seq; a frame at or above
// UINT32_MAX - 1 can never be published (ADR-0022).
constexpr std::uint64_t kFirstUnpublishableFrame = std::numeric_limits<std::uint32_t>::max() - 1U;

std::string keyed(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

// An integer key in [1, UINT32_MAX], checked BEFORE narrowing (critique F-3: 4294967296 must not
// become 0), the same bound simulation.cpp's checked_divisor applies to divisors.
std::uint32_t checked_u32(const Config& transport, std::string_view key, std::int64_t fallback) {
  constexpr auto kMax = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
  const std::int64_t value = transport.integer(key, fallback);
  if (value < 1 || value > kMax) {
    throw ConfigError(transport.origin(), keyed(transport, key),
                      "must be an integer between 1 and 4294967295");
  }
  return static_cast<std::uint32_t>(value);
}

// transport.commands.<key>: one input name, or for throttle a list of exactly two identical names
// (ADR-0022: both ICD throttles map to the single shared input, and their values must be equal).
std::string command_input_name(const Config& commands, std::string_view key) {
  const Config entry = commands.at(key);
  if (key != kCommandKeys[kThrottleKey]) {
    return entry.text();
  }
  if (!entry.is_sequence() || entry.size() != 2) {
    throw ConfigError(entry.origin(), entry.path(),
                      "must be a list of exactly two input names, one per ICD throttle");
  }
  std::string first = entry.at(std::size_t{0}).text();
  if (entry.at(std::size_t{1}).text() != first) {
    throw ConfigError(entry.origin(), entry.path(),
                      "both entries must name the same input: ICD v1 throttle[2] maps to one "
                      "shared input (ADR-0022)");
  }
  return first;
}

LockstepExchange::Limit read_limit(const Config& limits, std::string_view key) {
  const Config entry = limits.at(key);
  if (!entry.is_sequence() || entry.size() != 2) {
    throw ConfigError(entry.origin(), entry.path(), "must be a list [lo, hi]");
  }
  const LockstepExchange::Limit limit{entry.at(std::size_t{0}).number(),
                                      entry.at(std::size_t{1}).number()};
  // NOLINTNEXTLINE(readability-simplify-boolean-expr): NaN-safe form, as elsewhere in core.
  if (!(std::isfinite(limit.lo) && std::isfinite(limit.hi) && limit.lo <= limit.hi)) {
    throw ConfigError(entry.origin(), entry.path(), "must be finite with lo <= hi");
  }
  return limit;
}

}  // namespace

LockstepExchange::LockstepExchange(const Config& transport, const PluginLoader& loader,
                                   Vehicle& vehicle, SignalRegistry& registry,
                                   std::uint64_t sim_frames)
    : plugin_path_(transport.text("plugin")),
      vehicle_index_(vehicle.index()),
      divisor_(checked_u32(transport, "divisor", kDefaultDivisor)),
      // Joe, Q-C: capacity from sim.frames. Exchange frames in [0, sim.frames) number
      // ceil(sim.frames / divisor); states are frame 0 plus one per completed frame.
      recorder_(static_cast<std::size_t>((sim_frames + divisor_ - 1) / divisor_),
                static_cast<std::size_t>(sim_frames + 1)) {
  settings_.timeout_ms = checked_u32(transport, "timeout_ms", kDefaultTimeoutMs);
  settings_.handshake_timeout_ms =
      checked_u32(transport, "handshake_timeout_ms", kDefaultHandshakeTimeoutMs);
  settings_.peer_timeout_ms = checked_u32(transport, "peer_timeout_ms", kDefaultPeerTimeoutMs);
  if (settings_.peer_timeout_ms < settings_.timeout_ms) {
    throw ConfigError(transport.origin(), keyed(transport, "peer_timeout_ms"),
                      "must not be below timeout_ms (ADR-0022, ADR-0024)");
  }

  const Config commands = transport.at("commands");
  if (!commands.is_map() || commands.size() == 0) {
    throw ConfigError(commands.origin(), commands.path(),
                      "must map at least one ICD command field to an input");
  }
  for (std::size_t k = 0; k < kCommandKeyCount; ++k) {
    if (!commands.has(kCommandKeys[k])) {
      continue;  // unmapped: still validated every exchange, never written (critique F-2)
    }
    const std::string name = command_input_name(commands, kCommandKeys[k]);
    handles_[k] = vehicle.inputs().find(name);
    if (!handles_[k]) {
      throw ConfigError(commands.origin(), keyed(commands, kCommandKeys[k]),
                        "names no registered input of " + vehicle.name() + ": " + name);
    }
  }

  // Every key, mapped or not: ADR-0022 checks each command against its configured limit.
  const Config limits = transport.at("limits");
  if (!limits.is_map()) {
    throw ConfigError(limits.origin(), limits.path(), "must be a map");
  }
  for (std::size_t k = 0; k < kCommandKeyCount; ++k) {
    limits_[k] = read_limit(limits, kCommandKeys[k]);
  }

  plugin_ = loader.load(plugin_path_);
  transport_ = dynamic_cast<Transport*>(plugin_.model.get());
  if (transport_ == nullptr) {
    throw PluginLoadError(plugin_path_, "does not implement Transport");
  }
  if (transport_->icd_version() != ASTRA_ICD_VERSION) {
    throw PluginLoadError(plugin_path_,
                          "carries ICD version " + std::to_string(transport_->icd_version()) +
                              ", this core speaks " + std::to_string(ASTRA_ICD_VERSION));
  }
  transport_->init(transport, registry);

  registry.add("transport.exchanges", &exchanges_, "1");
  registry.add("transport.missed", &missed_, "1");
  registry.add("transport.fault", &fault_code_, "1");
}

LockstepExchange::~LockstepExchange() {
  if (transport_ != nullptr && opened_) {
    transport_->close(close_reason());
  }
}

void LockstepExchange::open(const Vehicle& vehicle, std::uint64_t frame, std::string_view origin) {
  recorder_.record_state(frame, vehicle.combined_state().first(kRigidBodySize));
  try {
    last_ack_ = transport_->open(settings_);
  } catch (const std::exception& error) {
    throw ConfigError(origin, "transport", plugin_path_ + " handshake failed: " + error.what());
  }
  opened_ = true;
}

HaltReason LockstepExchange::fail(TransportFault fault, std::string_view source,
                                  const StepContext& ctx) noexcept {
  halt_ = HaltReason{HealthCheck::kTransport, source, ctx.frame, vehicle_index_};
  fault_ = fault;
  fault_code_ = static_cast<double>(fault);
  missed_ = 1.0;
  return halt_;
}

HaltReason LockstepExchange::pre_frame(const StepContext& ctx) noexcept {
  if (halt_) {
    return halt_;
  }
  if (!recorder_.can_record(due(ctx.frame) ? 1 : 0, 1)) {
    return fail(TransportFault::kRecorderFull, kRecorderSource, ctx);
  }
  return {};
}

void LockstepExchange::note_halt(const HaltReason& halt) noexcept {
  if (!halt_) {
    halt_ = halt;
  }
}

void LockstepExchange::post_step(const Vehicle& vehicle, std::uint64_t new_frame) noexcept {
  recorder_.record_state(new_frame, vehicle.combined_state().first(kRigidBodySize));
  last_completed_frame_ = new_frame - 1;
  completed_any_ = true;
}

HaltReason LockstepExchange::exchange(const StepContext& ctx, Vehicle& vehicle,
                                      const EnvironmentSample& sample,
                                      const EarthModel& earth) noexcept {
  if (ctx.frame >= kFirstUnpublishableFrame) {
    return fail(TransportFault::kSequenceExhausted, plugin_path_, ctx);
  }
  if (published_any_ && ctx.frame != last_published_frame_ + divisor_) {
    return fail(TransportFault::kFrameDiscontinuity, plugin_path_, ctx);
  }

  // PreviousFrame only when this Simulation itself completed frame k - 1 (critique F-6):
  // Vehicle::derivative() latches a mass without integrating, so the mass alone proves nothing.
  const bool previous_valid =
      ctx.frame > 0 && completed_any_ && last_completed_frame_ + 1 == ctx.frame;
  const icd_pack::PreviousFrame previous{vehicle.published_wrench(), vehicle.latched_mass()};
  // .data(), not VehicleState{vehicle.state()}: see Simulation::step's comment on the dangling
  // temporary an Eigen::Map-to-StateVector conversion would create.
  const VehicleState vs{vehicle.state().data()};
  icd_pack::pack_sim_to_sw(request_, ctx, vs, sample, earth, previous_valid ? &previous : nullptr);
  recorder_.record_request(request_);

  const auto seq = static_cast<std::uint32_t>(ctx.frame);
  transport_->publish(request_, seq);
  last_published_frame_ = ctx.frame;
  published_any_ = true;

  return receive(ctx, seq);
}

HaltReason LockstepExchange::receive(const StepContext& ctx, std::uint32_t seq) noexcept {
  // ADR-0022's receive state machine. Termination is the transport's contract: every wait between
  // two publications shares one deadline, after which wait_ack returns kExpired.
  for (;;) {
    const AckObservation observed = transport_->wait_ack(last_ack_);
    if (observed.status == AckStatus::kExpired) {
      return fail(TransportFault::kTimeout, plugin_path_, ctx);
    }
    if (observed.status == AckStatus::kPeerClosed) {
      return fail(TransportFault::kPeerClosed, plugin_path_, ctx);
    }
    if (observed.ack_seq == last_ack_) {
      continue;  // unchanged: still waiting, never a failure
    }
    if (observed.ack_seq != seq) {
      return fail(TransportFault::kOutOfOrderAck, plugin_path_, ctx);
    }
    break;
  }
  last_ack_ = seq;

  std::memset(&reply_, 0, sizeof reply_);
  transport_->read_reply(reply_);
  recorder_.record_reply(reply_);
  return validate_and_stage(ctx);
}

HaltReason LockstepExchange::validate_and_stage(const StepContext& ctx) noexcept {
  // ADR-0022's order; the first failure halts, and nothing is staged unless every check passes.
  // (1) magic, version, echoed frame.
  if (reply_.magic != ASTRA_ICD_MAGIC) {
    return fail(TransportFault::kBadMagic, plugin_path_, ctx);
  }
  if (reply_.version != ASTRA_ICD_VERSION) {
    return fail(TransportFault::kBadVersion, plugin_path_, ctx);
  }
  if (reply_.frame != ctx.frame) {
    return fail(TransportFault::kFrameMismatch, plugin_path_, ctx);
  }
  const std::array<double, kCommandCount> values = {reply_.elevator, reply_.aileron, reply_.rudder,
                                                    reply_.throttle[0], reply_.throttle[1]};
  // (2) every value finite, mapped or not, under every sim.health.input setting.
  for (std::size_t i = 0; i < kCommandCount; ++i) {
    if (!std::isfinite(values[i])) {
      return fail(TransportFault::kNonFiniteCommand, kFieldNames[i], ctx);
    }
  }
  // (3) throttle[2] equality (both map to one shared input).
  if (values[kThrottle0] != values[kThrottle1]) {
    return fail(TransportFault::kThrottleMismatch, kThrottleField, ctx);
  }
  // (4) every value against its configured limit; never clamped.
  for (std::size_t i = 0; i < kCommandCount; ++i) {
    const Limit& limit = limits_[kValueKey[i]];
    if (values[i] < limit.lo || values[i] > limit.hi) {
      return fail(TransportFault::kCommandLimit, kFieldNames[i], ctx);
    }
  }
  // Stage the mapped fields (throttle once); the same frame's latch() commits them.
  for (std::size_t k = 0; k < kCommandKeyCount; ++k) {
    if (handles_[k]) {
      handles_[k].write(values[k]);
    }
  }
  exchanges_ += 1.0;
  return {};
}

}  // namespace astra
