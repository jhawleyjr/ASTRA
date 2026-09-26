// The HAL's shm backend (docs/architecture.md §11; ADR-0024, ADR-0023 site 2; M4 PR 4 plan
// docs/plans/m4-pr4-shm-transport.md). The peer side of the protocol in astra_shm_layout.h.
//
// Per-exchange calls (hal_wait_frame, hal_send, hal_now) allocate nothing and read no clock; the
// only clock reads are inside astra_hal_shm_wait_until (hal_shm_wait.cpp). Every shared word is
// read with an acquire load and every word this side owns (peer_state, ack_seq) is published with a
// release store after the payload it covers.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "embedded/hal/astra_hal.h"
#include "embedded/hal/astra_shm_layout.h"
#include "embedded/hal/shm/hal_shm_internal.h"
#include "icd/astra_icd.h"

namespace {

using astra_hal_shm::Deadline;
using astra_hal_shm::load_acquire;
using astra_hal_shm::store_release;
using astra_hal_shm::WaitKind;
using astra_hal_shm::WaitResult;

// "/" + a segment name of at most NAME_MAX - 1 characters, NUL-terminated.
constexpr std::size_t kPathCapacity = 256;
constexpr std::size_t kRegionSize = sizeof(astra_shm_region_v1);

struct Session {
  astra_shm_region_v1* region = nullptr;
  std::uint32_t peer_timeout_ms = 0;
  std::uint32_t last_req = ASTRA_SHM_SEQ_INIT;  // the sequence of the last request returned
  bool pending = false;                         // a request returned, its reply not yet sent
  double now = 0.0;                             // hal_now(): t of the last request returned
};

Session session;

// Builds "/<segment>" into path; false for a null, empty, over-long, or '/'-containing name.
bool make_path(const char* segment, std::array<char, kPathCapacity>& path) {
  if (segment == nullptr || segment[0] == '\0') {
    return false;
  }
  const std::size_t length = std::strlen(segment);
  if (length + 2 > path.size() || std::strchr(segment, '/') != nullptr) {
    return false;
  }
  path[0] = '/';
  std::memcpy(&path[1], segment, length + 1);
  return true;
}

void unmap(astra_shm_region_v1* region) {
  if (region != nullptr) {
    munmap(region, kRegionSize);
  }
}

// Handshake steps 1-3 (ADR-0024): retry shm_open until it succeeds, never map a segment smaller
// than the layout, then map it. The caller's deadline covers every retry.
astra_hal_status attach(const std::array<char, kPathCapacity>& path, Deadline& deadline,
                        astra_shm_region_v1*& region) {
  int fd = -1;
  for (;;) {
    fd = shm_open(path.data(), O_RDWR, 0);
    if (fd >= 0) {
      struct stat info {};
      if (fstat(fd, &info) != 0) {
        close(fd);
        return ASTRA_HAL_ERROR;
      }
      if (info.st_size >= static_cast<off_t>(kRegionSize)) {
        break;
      }
      close(fd);  // the creator has not sized it yet: never map a short segment
    } else if (errno != ENOENT) {
      return ASTRA_HAL_ERROR;
    }
    const WaitResult waited = astra_hal_shm_wait_until(nullptr, 0, &deadline);
    if (waited.kind == WaitKind::kExpired) {
      return ASTRA_HAL_HANDSHAKE_TIMEOUT;
    }
    if (waited.kind == WaitKind::kFault) {
      return ASTRA_HAL_ERROR;
    }
  }
  void* mapped = mmap(nullptr, kRegionSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    return ASTRA_HAL_ERROR;
  }
  region = static_cast<astra_shm_region_v1*>(mapped);
  return ASTRA_HAL_OK;
}

// Handshake steps 4-7 on a mapped region: acquire-wait for SIM_READY (CLOSED first is a protocol
// fault, Joe's change 1), validate, reject a stale session, then publish PEER_READY.
astra_hal_status complete_handshake(astra_shm_region_v1* region, Deadline& deadline) {
  astra_shm_header_v1& header = region->header;
  for (;;) {
    const std::uint32_t state = load_acquire(&header.sim_state);
    if (state == ASTRA_SHM_SIM_CLOSED) {
      return ASTRA_HAL_PROTOCOL;
    }
    if (state == ASTRA_SHM_SIM_READY) {
      break;
    }
    const WaitResult waited = astra_hal_shm_wait_until(&header.sim_state, state, &deadline);
    if (waited.kind == WaitKind::kExpired) {
      return ASTRA_HAL_HANDSHAKE_TIMEOUT;
    }
    if (waited.kind == WaitKind::kFault) {
      return ASTRA_HAL_ERROR;
    }
  }
  // Only now, after the acquire of SIM_READY, is any other header field read.
  if (header.magic != ASTRA_SHM_MAGIC || header.layout_version != ASTRA_SHM_LAYOUT_VERSION ||
      header.layout_size != kRegionSize || header.icd_version != ASTRA_ICD_VERSION) {
    return ASTRA_HAL_BAD_LAYOUT;
  }
  // A live sim publishes nothing before PEER_READY, so a moved req_seq is a stale or foreign
  // session (a dead sim's segment, or one another peer already served).
  if (load_acquire(&header.req_seq) != ASTRA_SHM_SEQ_INIT) {
    return ASTRA_HAL_PROTOCOL;
  }
  header.peer_pid = static_cast<std::int32_t>(getpid());  // published by the release below
  store_release(&header.peer_state, ASTRA_SHM_PEER_READY);
  astra_hal_shm_wake(&header.peer_state);
  return ASTRA_HAL_OK;
}

bool closed(const astra_shm_header_v1& header, std::uint32_t req) {
  return req == ASTRA_SHM_CLOSE_SEQ || load_acquire(&header.sim_state) == ASTRA_SHM_SIM_CLOSED;
}

}  // namespace

extern "C" astra_hal_status hal_init(const astra_hal_config* config) {
  std::array<char, kPathCapacity> path{};
  if (session.region != nullptr || config == nullptr || config->handshake_timeout_ms == 0 ||
      config->peer_timeout_ms == 0 || !make_path(config->segment, path)) {
    return ASTRA_HAL_ERROR;
  }
  Deadline deadline;
  deadline.timeout_ms = config->handshake_timeout_ms;
  astra_shm_region_v1* region = nullptr;
  const astra_hal_status attached = attach(path, deadline, region);
  if (attached != ASTRA_HAL_OK) {
    return attached;
  }
  const astra_hal_status handshake = complete_handshake(region, deadline);
  if (handshake != ASTRA_HAL_OK) {
    unmap(region);
    return handshake;
  }
  session = Session{};
  session.region = region;
  session.peer_timeout_ms = config->peer_timeout_ms;
  return ASTRA_HAL_OK;
}

extern "C" astra_hal_status hal_wait_frame(astra_sim_to_sw_v1* in) {
  if (session.region == nullptr || in == nullptr || session.pending) {
    return ASTRA_HAL_ERROR;  // pending: the last frame's reply was never sent (Joe's nit)
  }
  astra_shm_region_v1& region = *session.region;
  Deadline deadline;
  deadline.timeout_ms = session.peer_timeout_ms;
  std::uint32_t observed = load_acquire(&region.header.req_seq);
  for (;;) {
    if (closed(region.header, observed)) {
      return ASTRA_HAL_CLOSED;  // checked first: close takes precedence (ADR-0024)
    }
    if (observed != session.last_req) {
      // `observed` came from an acquire load, so the request payload behind it is visible; the sim
      // does not rewrite it until this side acks.
      std::memcpy(in, &region.request, sizeof *in);
      if (in->magic != ASTRA_ICD_MAGIC || in->version != ASTRA_ICD_VERSION ||
          in->frame != static_cast<std::uint64_t>(observed)) {
        return ASTRA_HAL_PROTOCOL;
      }
      session.last_req = observed;
      session.pending = true;
      session.now = in->t;
      return ASTRA_HAL_OK;
    }
    const WaitResult waited =
        astra_hal_shm_wait_until(&region.header.req_seq, session.last_req, &deadline);
    if (waited.kind != WaitKind::kReturned) {
      // Expiry or fault: only a close outranks it (ADR-0024). An ordinary request observed now
      // arrived after the deadline and is a timeout (critique F-2).
      if (closed(region.header, load_acquire(&region.header.req_seq))) {
        return ASTRA_HAL_CLOSED;
      }
      return waited.kind == WaitKind::kExpired ? ASTRA_HAL_TIMEOUT : ASTRA_HAL_ERROR;
    }
    observed = waited.value;
  }
}

extern "C" astra_hal_status hal_send(const astra_sw_to_sim_v1* out) {
  if (session.region == nullptr || out == nullptr || !session.pending) {
    return ASTRA_HAL_ERROR;
  }
  astra_shm_region_v1& region = *session.region;
  std::memcpy(&region.reply, out, sizeof *out);
  // The ack is the request's own sequence word, never out->frame: a wrong echo stays visible to the
  // sim's reply.frame check (ADR-0022).
  store_release(&region.header.ack_seq, session.last_req);
  astra_hal_shm_wake(&region.header.ack_seq);
  session.pending = false;
  return ASTRA_HAL_OK;
}

extern "C" double hal_now(void) { return session.now; }

extern "C" void hal_close(void) {
  unmap(session.region);
  session = Session{};
}

extern "C" int hal_exit_status(astra_hal_status status) {
  switch (status) {
    case ASTRA_HAL_OK:
    case ASTRA_HAL_CLOSED:
      return 0;
    case ASTRA_HAL_TIMEOUT:
    case ASTRA_HAL_HANDSHAKE_TIMEOUT:
    case ASTRA_HAL_BAD_LAYOUT:
    case ASTRA_HAL_PROTOCOL:
    case ASTRA_HAL_ERROR:
      return static_cast<int>(status);
  }
  return static_cast<int>(ASTRA_HAL_ERROR);
}
