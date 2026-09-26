// The link-time test hooks (plan Q-4, critique F-4/F-5): this executable links the UNMODIFIED
// astra_hal with
//   -Wl,--wrap=astra_hal_shm_wait_until -Wl,--wrap=fstat -Wl,--wrap=mmap
// so every undefined reference to those symbols from a static object (the HAL's hal_shm.o, and
// this test code) resolves to the __wrap_ functions below, which call the real ones through
// __real_. Shared libraries (libc's own internals, the ASan runtime, gtest, dlopen'd plugins) are
// untouched. No shipped object carries a hook, a macro, or a hook symbol.
//
// The __wrap_/__real_ names are fixed by the linker's --wrap contract, hence the
// reserved-identifier suppressions.
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "embedded/hal/shm/hal_shm_internal.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

constexpr std::size_t kPageSize = 4096;  // mappings are page-aligned; offsets are within the header
constexpr long long kRegionSize = static_cast<long long>(sizeof(astra_shm_region_v1));

// True when the calling thread is the observed one and `fd` is the watched segment (readlink of
// /proc/self/fd/<fd> is /dev/shm/<segment>). Caller holds hooks.mutex.
bool is_watched(int fd, const shm_test::Hooks& hooks) {
  const std::string& watched = hooks.watched;
  if (watched.empty() || fd < 0 || std::this_thread::get_id() != hooks.observed_thread) {
    return false;
  }
  std::array<char, 512> target{};
  const std::string link = "/proc/self/fd/" + std::to_string(fd);
  const ssize_t length = readlink(link.c_str(), target.data(), target.size() - 1);
  return length > 0 && std::string(target.data(), static_cast<std::size_t>(length)) == watched;
}

}  // namespace

// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" {

astra_hal_shm::WaitResult __real_astra_hal_shm_wait_until(const std::uint32_t* word,
                                                          std::uint32_t expected,
                                                          astra_hal_shm::Deadline* deadline);
int __real_fstat(int fd, struct stat* info);
void* __real_mmap(void* address, std::size_t length, int prot, int flags, int fd, off_t offset);

astra_hal_shm::WaitResult __wrap_astra_hal_shm_wait_until(const std::uint32_t* word,
                                                          std::uint32_t expected,
                                                          astra_hal_shm::Deadline* deadline) {
  shm_test::Hooks& hooks = shm_test::hooks();
  const shm_test::WaitCall call{
      word == nullptr ? shm_test::kNoWord : reinterpret_cast<std::uintptr_t>(word) % kPageSize};
  ++hooks.wait_calls;
  if (call.offset == shm_test::offset_of_sim_state()) {
    const std::lock_guard lock(hooks.mutex);
    hooks.waited_on_sim_state = true;
    hooks.changed.notify_all();
  }
  if (hooks.before_wait) {
    hooks.before_wait(call);
  }
  astra_hal_shm::WaitResult result = __real_astra_hal_shm_wait_until(word, expected, deadline);
  if (hooks.after_wait) {
    hooks.after_wait(call, result);
  }
  return result;
}

int __wrap_fstat(int fd, struct stat* info) {
  const int rc = __real_fstat(fd, info);
  shm_test::Hooks& hooks = shm_test::hooks();
  const std::lock_guard lock(hooks.mutex);
  if (rc == 0 && is_watched(fd, hooks)) {
    hooks.fstat_sizes.push_back(static_cast<long long>(info->st_size));
    hooks.changed.notify_all();
  }
  return rc;
}

void* __wrap_mmap(void* address, std::size_t length, int prot, int flags, int fd, off_t offset) {
  {
    shm_test::Hooks& hooks = shm_test::hooks();
    const std::lock_guard lock(hooks.mutex);
    if (is_watched(fd, hooks)) {
      ++hooks.mmaps;
      if (hooks.fstat_sizes.empty() || hooks.fstat_sizes.back() < kRegionSize) {
        hooks.mmap_while_short = true;
      }
      hooks.changed.notify_all();
    }
  }
  return __real_mmap(address, length, prot, flags, fd, offset);
}

}  // extern "C"
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
