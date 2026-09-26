// ADR-0023 clock site 1: the sim-side shm transport's bounded wait, used by the handshake (open)
// and every acknowledgment wait (wait_ack). The only function in this plugin that reads a clock or
// issues a futex wait; alone in this TU so a grep (or a future symbol scan) can allowlist it.
//
// Algorithm, identical to the HAL's (embedded/hal/shm/hal_shm_wait.cpp; plan critique F-1/F-2):
//   1. a latched expiry or fault returns at once, no syscall;
//   2. arm on first use: abs = CLOCK_MONOTONIC now + timeout_ms;
//   3. one FUTEX_WAIT_BITSET(word, expected, abs);
//   4. acquire-load *word, THEN read the clock: ETIMEDOUT or now >= abs latches expiry. The
//   kernel's
//      value compare (EAGAIN) and a concurrent FUTEX_WAKE both return ahead of ETIMEDOUT, so an
//      absolute timeout alone would not bound core's receive loop against a peer that keeps waking.
//      Loading before the clock read means any kReturned value was observed before the deadline.
// The readings never leave this function except as the three-way kind (ADR-0023).
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>

#include "shm_transport.hpp"

namespace astra::shm_transport {
namespace {

constexpr long long kNanosPerSecond = 1000000000LL;
constexpr long long kNanosPerMilli = 1000000LL;
constexpr std::uint32_t kMillisPerSecond = 1000U;

timespec add_nanos(timespec t, long long nanos) noexcept {
  const long long total = static_cast<long long>(t.tv_nsec) + nanos;
  t.tv_sec += static_cast<time_t>(total / kNanosPerSecond);
  t.tv_nsec = static_cast<long>(total % kNanosPerSecond);
  return t;
}

bool before(const timespec& a, const timespec& b) noexcept {
  return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
}

}  // namespace

ShmTransport::WaitResult ShmTransport::wait_until(const std::uint32_t* word, std::uint32_t expected,
                                                  Deadline& deadline) noexcept {
  if (deadline.faulted) {
    return {WaitKind::kFault, load_acquire(*word)};
  }
  if (deadline.expired) {
    return {WaitKind::kExpired, load_acquire(*word)};
  }
  timespec now{};
  if (!deadline.armed) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    const long long timeout_nanos =
        static_cast<long long>(deadline.timeout_ms / kMillisPerSecond) * kNanosPerSecond +
        static_cast<long long>(deadline.timeout_ms % kMillisPerSecond) * kNanosPerMilli;
    deadline.abs = add_nanos(now, timeout_nanos);
    deadline.armed = true;
  }

  // Absolute CLOCK_MONOTONIC deadline (no FUTEX_CLOCK_REALTIME), process-shared (no
  // FUTEX_PRIVATE_FLAG): the kernel sleeps only while *word == expected.
  int error = 0;
  if (syscall(SYS_futex, word, FUTEX_WAIT_BITSET, expected, &deadline.abs, nullptr,
              FUTEX_BITSET_MATCH_ANY) == -1) {
    error = errno;
  }

  const std::uint32_t value = load_acquire(*word);
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (error == ETIMEDOUT || !before(now, deadline.abs)) {
    deadline.expired = true;
    return {WaitKind::kExpired, value};
  }
  if (error != 0 && error != EAGAIN && error != EINTR) {
    deadline.faulted = true;
    return {WaitKind::kFault, value};
  }
  return {WaitKind::kReturned, value};
}

void ShmTransport::wake(const std::uint32_t* word) noexcept {
  syscall(SYS_futex, word, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

}  // namespace astra::shm_transport
