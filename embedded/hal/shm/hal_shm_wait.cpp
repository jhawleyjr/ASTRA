// ADR-0023 clock site 2: the HAL shm backend's bounded wait. The only function in embedded/hal that
// reads a clock; alone in this TU on purpose (hal_shm_internal.h explains the --wrap test hook).
//
// Algorithm (plan "The single clock function per side", critique F-1/F-2):
//   1. a latched expiry or fault returns at once, no syscall;
//   2. arm on first use: abs = CLOCK_MONOTONIC now + timeout_ms;
//   3. one FUTEX_WAIT_BITSET(word, expected, abs) -- or, with no word, a sleep of at most 1 ms;
//   4. acquire-load *word, THEN read the clock: ETIMEDOUT or now >= abs latches expiry. An absolute
//      futex timeout alone does not bound a caller's loop: the kernel's value compare (EAGAIN) and
//      a concurrent FUTEX_WAKE both return ahead of ETIMEDOUT, so a peer that keeps waking could
//      otherwise keep a waiter alive past its deadline. Loading before the clock read means any
//      kReturned value was observed before the deadline.
// The readings never leave this function except as the three-way kind (ADR-0023).
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>

#include "embedded/hal/shm/hal_shm_internal.h"

namespace {

constexpr long long kNanosPerSecond = 1000000000LL;
constexpr long long kNanosPerMilli = 1000000LL;
constexpr std::uint32_t kMillisPerSecond = 1000U;
constexpr long long kAttachPollNanos = kNanosPerMilli;  // the attach loop's sleep, before mapping

timespec add_nanos(timespec t, long long nanos) {
  const long long total = static_cast<long long>(t.tv_nsec) + nanos;
  t.tv_sec += static_cast<time_t>(total / kNanosPerSecond);
  t.tv_nsec = static_cast<long>(total % kNanosPerSecond);
  return t;
}

bool before(const timespec& a, const timespec& b) {
  return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
}

std::uint32_t load_or_zero(const std::uint32_t* word) {
  return word != nullptr ? astra_hal_shm::load_acquire(word) : 0U;
}

}  // namespace

extern "C" astra_hal_shm::WaitResult astra_hal_shm_wait_until(const std::uint32_t* word,
                                                              std::uint32_t expected,
                                                              astra_hal_shm::Deadline* deadline) {
  using astra_hal_shm::WaitKind;
  if (deadline->faulted) {
    return {WaitKind::kFault, load_or_zero(word)};
  }
  if (deadline->expired) {
    return {WaitKind::kExpired, load_or_zero(word)};
  }
  timespec now{};
  if (!deadline->armed) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    const long long timeout_nanos =
        static_cast<long long>(deadline->timeout_ms / kMillisPerSecond) * kNanosPerSecond +
        static_cast<long long>(deadline->timeout_ms % kMillisPerSecond) * kNanosPerMilli;
    deadline->abs = add_nanos(now, timeout_nanos);
    deadline->armed = true;
  }

  int error = 0;
  if (word != nullptr) {
    // Absolute CLOCK_MONOTONIC deadline (no FUTEX_CLOCK_REALTIME), process-shared (no
    // FUTEX_PRIVATE_FLAG): the kernel sleeps only while *word == expected.
    if (syscall(SYS_futex, word, FUTEX_WAIT_BITSET, expected, &deadline->abs, nullptr,
                FUTEX_BITSET_MATCH_ANY) == -1) {
      error = errno;
    }
  } else {
    clock_gettime(CLOCK_MONOTONIC, &now);
    timespec until = add_nanos(now, kAttachPollNanos);
    if (!before(until, deadline->abs)) {
      until = deadline->abs;
    }
    error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr);
  }

  const std::uint32_t value = load_or_zero(word);
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (error == ETIMEDOUT || !before(now, deadline->abs)) {
    deadline->expired = true;
    return {WaitKind::kExpired, value};
  }
  if (error != 0 && error != EAGAIN && error != EINTR) {
    deadline->faulted = true;
    return {WaitKind::kFault, value};
  }
  return {WaitKind::kReturned, value};
}

extern "C" void astra_hal_shm_wake(const std::uint32_t* word) {
  syscall(SYS_futex, word, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}
