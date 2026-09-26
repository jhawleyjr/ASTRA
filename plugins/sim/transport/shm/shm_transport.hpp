// SharedMemoryTransport (ADR-0007 as amended by ADR-0024; ADR-0022, ADR-0023; M4 PR 4 plan
// docs/plans/m4-pr4-shm-transport.md): the sim side of the lockstep protocol documented in
// embedded/hal/astra_shm_layout.h, which this plugin INCLUDES (never links; ADR-0024, ADR-0008).
//
// Config: transport.segment (required), the driver-chosen name astra_<driver pid>_<nonce>
// (plan Q-5). Every other key of `transport` is Simulation's.
//
// What this transport owns, per the Transport contract
// (core/include/astra/interfaces/transport.hpp): creating and initializing the region, the
// header-only handshake, publishing a request, ONE bounded wait per call against one deadline per
// publication, and copying the reply. It never classifies an ack (core does) and never returns
// kPeerClosed (plan Q-7: ADR-0024 gives the peer no closed state; a vanished peer is a timeout).
// close(kNormal) is ADR-0024's CLOSED sequence; close(kAfterFailure) writes nothing to the region
// (plan Q-3). The name is unlinked as soon as the handshake completes (plan Q-6) and again,
// idempotently, on close or failure.
//
// Clock: ShmTransport::wait_until (shm_wait.cpp) is ADR-0023's site 1 and the only function in
// this plugin that reads a clock or issues a futex wait.
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "astra/interfaces/transport.hpp"
#include "embedded/hal/astra_shm_layout.h"
#include "icd/astra_icd.h"

namespace astra::shm_transport {

static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
              "the shm protocol needs lock-free 32-bit atomics");

// The ordering vocabulary both sides use (the HAL's hal_shm_internal.h has the same two): GCC/Clang
// __atomic builtins on the plain uint32_t words of the mapping, valid in C++17 and C++23 alike.
inline std::uint32_t load_acquire(const std::uint32_t& word) noexcept {
  return __atomic_load_n(&word, __ATOMIC_ACQUIRE);
}

inline void store_release(std::uint32_t& word, std::uint32_t value) noexcept {
  __atomic_store_n(&word, value, __ATOMIC_RELEASE);
}

class ShmTransport final : public Transport {
 public:
  ShmTransport() = default;
  ~ShmTransport() override;
  ShmTransport(const ShmTransport&) = delete;
  ShmTransport& operator=(const ShmTransport&) = delete;
  ShmTransport(ShmTransport&&) = delete;
  ShmTransport& operator=(ShmTransport&&) = delete;

  void init(const Config& config, SignalRegistry& registry) override;
  void reset() override {}
  void step(const StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "shm"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }
  [[nodiscard]] std::uint32_t open(const TransportSettings& settings) override;
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override;
  [[nodiscard]] AckObservation wait_ack(std::uint32_t unchanged) noexcept override;
  void read_reply(astra_sw_to_sim_v1& reply) noexcept override;
  void close(CloseReason reason) noexcept override;

  // One immutable absolute deadline (ADR-0023), armed on its first wait and latched once expired.
  struct Deadline {
    std::uint32_t timeout_ms = 0;
    bool armed = false;
    bool expired = false;
    bool faulted = false;
    timespec abs{};
  };
  enum class WaitKind : std::uint8_t { kReturned, kExpired, kFault };
  struct WaitResult {
    WaitKind kind;
    std::uint32_t value;  // *word, acquire-loaded after the syscall and BEFORE the clock read
  };

  // ADR-0023 site 1 (shm_wait.cpp): waits until *word may differ from `expected`, or the deadline.
  [[nodiscard]] static WaitResult wait_until(const std::uint32_t* word, std::uint32_t expected,
                                             Deadline& deadline) noexcept;
  // FUTEX_WAKE every waiter on *word (process-shared).
  static void wake(const std::uint32_t* word) noexcept;

 private:
  // Unmaps a valid mapping and unlinks an owned name; writes nothing to the region.
  void release() noexcept;
  void unlink_name() noexcept;

  std::string segment_;  // astra_<pid>_<nonce>
  std::string path_;     // "/" + segment_, for shm_open/shm_unlink
  astra_shm_region_v1* region_ = nullptr;
  bool owns_name_ = false;  // created by this transport (O_EXCL) and not yet unlinked
  bool closed_ = false;
  Deadline ack_deadline_;
  std::uint32_t timeout_ms_ = kDefaultTimeoutMs;
};

}  // namespace astra::shm_transport
