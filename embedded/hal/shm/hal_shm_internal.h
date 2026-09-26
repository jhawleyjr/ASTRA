// Internal to the HAL's shm backend (embedded/hal/shm): the ordering helpers and the one
// bounded-wait function. Not part of the HAL API (astra_hal.h) -- C++17 only.
//
// astra_hal_shm_wait_until is ADR-0023's clock site 2 and the ONLY function in the HAL that reads a
// clock. It lives alone in hal_shm_wait.cpp with external C linkage, and that is load-bearing:
// tests/lockstep links the unmodified library with `-Wl,--wrap=astra_hal_shm_wait_until` to run
// ADR-0024's close-between-check-and-wait interleaving (plan Q-4). --wrap redirects only undefined
// references BETWEEN object files, so merging it into hal_shm.cpp (or making it static/inline)
// would silently disable the test hook -- the test asserts its hook ran, so it would fail loudly.
#pragma once

#include <cstdint>
#include <ctime>

#include "embedded/hal/astra_shm_layout.h"

namespace astra_hal_shm {

// One immutable absolute deadline (ADR-0023): armed on the first wait, then shared by every re-wait
// until the caller starts a new one. Once expiry (or a fault) has been observed it is latched.
struct Deadline {
  std::uint32_t timeout_ms = 0;
  bool armed = false;
  bool expired = false;
  bool faulted = false;
  timespec abs{};
};

enum class WaitKind : std::uint8_t {
  kReturned,  // a wake, EINTR, or EAGAIN before the deadline; `value` may be unchanged
  kExpired,   // the deadline has passed (value loaded, but it is not a result)
  kFault      // an impossible futex/sleep error; terminal
};

struct WaitResult {
  WaitKind kind;
  // *word, acquire-loaded after the syscall and BEFORE the clock read; 0 if there is no word.
  std::uint32_t value;
};

static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
              "the shm protocol needs lock-free 32-bit atomics");

inline std::uint32_t load_acquire(const std::uint32_t* word) {
  return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

inline void store_release(std::uint32_t* word, std::uint32_t value) {
  __atomic_store_n(word, value, __ATOMIC_RELEASE);
}

}  // namespace astra_hal_shm

extern "C" {

// Waits until *word may differ from `expected`, or the deadline. word == nullptr is the attach
// loop's sleep (no mapping yet): at most 1 ms, bounded by the deadline.
astra_hal_shm::WaitResult astra_hal_shm_wait_until(const std::uint32_t* word,
                                                   std::uint32_t expected,
                                                   astra_hal_shm::Deadline* deadline);

// FUTEX_WAKE every waiter on *word (process-shared).
void astra_hal_shm_wake(const std::uint32_t* word);

}  // extern "C"
