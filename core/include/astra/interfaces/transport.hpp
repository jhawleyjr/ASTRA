// The sim-side lockstep transport (docs/architecture.md §10, ADR-0022, ADR-0023, ADR-0024; M4 PR 3,
// docs/plans/m4-pr3-core-seam.md). A transport plugin under plugins/sim/transport/ implements this
// interface. It derives from Model only because PluginDescriptor::create returns a Model (the same
// load path every typed slot takes): Simulation never schedules it, never calls its step(), and it
// never writes through the ADR-0017 seam.
//
// Ownership split (ADR-0022): the RECEIVE STATE MACHINE, the reply validation, and every halt
// decision live in core (LockstepExchange, core/include/astra/kernel/lockstep_exchange.hpp). A
// transport owns only what needs the peer or the clock: storing and publishing a request, one
// bounded wait against its own deadline, and copying the reply. Core's loop per exchange is:
//
//   publish(request, seq)
//   loop: obs = wait_ack(last_ack)
//         kExpired -> halt (timeout);  kPeerClosed -> halt (peer closed)
//         obs.ack_seq == last_ack -> keep waiting (spurious wake, EINTR, a wake with no store)
//         obs.ack_seq == seq      -> done; read_reply
//         otherwise               -> halt (out-of-order ack)
//
// so "unchanged means still waiting, never failure" is core logic, and a transport only has to
// guarantee that its waits end at one deadline per publication.
//
// Per-frame methods (publish, wait_ack, read_reply) are noexcept and allocation-free. Init
// (Model::init, open) may allocate and throw.
#pragma once

#include <cstdint>

#include "astra/interfaces/model.hpp"
#include "icd/astra_icd.h"

namespace astra {

// What one wait_ack() call observed.
enum class AckStatus : std::uint8_t {
  kObserved,   // ack_seq holds the ack word read after a wake; it may equal the unchanged value
  kExpired,    // the one deadline for this publication has passed
  kPeerClosed  // the peer is closed or gone (a transport-level fact, e.g. a state word)
};

struct AckObservation {
  AckStatus status = AckStatus::kExpired;
  std::uint32_t ack_seq = 0;
};

// ADR-0022's defaults; peer_timeout_ms's is PR 3's (Joe, 2026-09-25: 5000, not below timeout_ms).
inline constexpr std::uint32_t kDefaultTimeoutMs = 1000;
inline constexpr std::uint32_t kDefaultHandshakeTimeoutMs = 5000;
inline constexpr std::uint32_t kDefaultPeerTimeoutMs = 5000;

// The transport block's timing keys, validated by Simulation::from_config before open()
// (peer_timeout_ms >= timeout_ms).
struct TransportSettings {
  std::uint32_t timeout_ms = kDefaultTimeoutMs;
  std::uint32_t handshake_timeout_ms = kDefaultHandshakeTimeoutMs;
  std::uint32_t peer_timeout_ms = kDefaultPeerTimeoutMs;
};

// Why the transport is being closed. What the shm plugin does after a failure is PR 4's to decide
// under ADR-0024; kNormal is the normal completion ADR-0024 describes (sim_state = CLOSED).
enum class CloseReason : std::uint8_t { kNormal, kAfterFailure };

class Transport : public Model {
 public:
  ~Transport() override;

  // The ICD version this transport's frames carry; Simulation::from_config rejects a transport
  // whose value is not ASTRA_ICD_VERSION (docs/architecture.md §15: "ICD version for transports").
  [[nodiscard]] virtual std::uint32_t icd_version() const noexcept = 0;

  // Init-time: the header-only handshake (ADR-0024) within settings.handshake_timeout_ms. Throws on
  // any failure; Simulation reports it as a ConfigError at "transport". Returns the ack word's
  // value after the handshake (UINT32_MAX, the ADR-0024 sentinel), the first "unchanged" value.
  [[nodiscard]] virtual std::uint32_t open(const TransportSettings& settings) = 0;

  // Per frame: stores `request` into the outbound slot, then publishes `seq` (in shm: a release
  // store of req_seq, then a wake). Core guarantees seq < UINT32_MAX - 1 (ADR-0022).
  virtual void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept = 0;

  // Per frame: one wait for the ack word to differ from `unchanged`. May return kObserved with
  // ack_seq == unchanged (a spurious wake); core then calls again. Every call between two
  // publish() calls shares ONE absolute deadline, fixed at the first such call, and a call after
  // that deadline returns kExpired: this is what bounds core's loop. The deadline's
  // CLOCK_MONOTONIC read (ADR-0023) is confined to this one function; a transport that needs no
  // clock (the loopback) reads none.
  [[nodiscard]] virtual AckObservation wait_ack(std::uint32_t unchanged) noexcept = 0;

  // Per frame, only after wait_ack observed the published seq: copies the reply payload (in shm,
  // after the acquire load of ack_seq) into `reply`, all 64 bytes.
  virtual void read_reply(astra_sw_to_sim_v1& reply) noexcept = 0;

  // Once, when the owning Simulation is destroyed.
  virtual void close(CloseReason reason) noexcept = 0;
};

}  // namespace astra
