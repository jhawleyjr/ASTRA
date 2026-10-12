// SharedMemoryTransport: lifecycle, handshake, publish, reply, close (shm_transport.hpp; the wait
// and its clock read are in shm_wait.cpp). Per-frame methods allocate nothing and never throw; init
// and open may allocate and throw (open's failures become a ConfigError at "transport" in core).
//
// Config keys:
//   segment | text | - | required | The shared-memory segment name, astra_<pid>_<nonce> (1 to 10
//          digits, 1 to 32 letters or digits). astra.lockstep supplies a fresh one for each run, so
//          a config file leaves it out and runs through astra.lockstep.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/plugin/descriptor.hpp"
#include "shm_transport.hpp"

namespace astra::shm_transport {
namespace {

constexpr std::size_t kRegionSize = sizeof(astra_shm_region_v1);
constexpr std::string_view kPrefix = "astra_";
constexpr std::size_t kMaxPidDigits = 10;
constexpr std::size_t kMaxNonce = 32;
constexpr mode_t kSegmentMode = 0600;

// ADR-0024's astra_<driver pid>_<nonce>: "astra_", 1-10 digits, "_", 1-32 ASCII alphanumerics.
bool valid_segment(std::string_view name) {
  if (!name.starts_with(kPrefix)) {
    return false;
  }
  name.remove_prefix(kPrefix.size());
  const std::size_t underscore = name.find('_');
  if (underscore == std::string_view::npos || underscore == 0 || underscore > kMaxPidDigits) {
    return false;
  }
  const std::string_view pid = name.substr(0, underscore);
  const std::string_view nonce = name.substr(underscore + 1);
  const auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
  const auto alnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
  return std::ranges::all_of(pid, digit) && !nonce.empty() && nonce.size() <= kMaxNonce &&
         std::ranges::all_of(nonce, alnum);
}

std::runtime_error system_error(std::string_view what, std::string_view segment) {
  return std::runtime_error(std::string{what} + " failed for shm segment '" + std::string{segment} +
                            "': " + std::strerror(errno));
}

// Owns a descriptor from a successful shm_open until it is closed (critique F-3).
class Descriptor {
 public:
  explicit Descriptor(int fd) noexcept : fd_(fd) {}
  ~Descriptor() { reset(); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  [[nodiscard]] int get() const noexcept { return fd_; }
  void reset() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_;
};

}  // namespace

ShmTransport::~ShmTransport() { release(); }

void ShmTransport::init(const Config& config, SignalRegistry& /*registry*/) {
  const std::string key = config.path().empty() ? "segment" : config.path() + ".segment";
  if (!config.has("segment")) {
    throw ConfigError(config.origin(), key,
                      "is required: the driver-chosen shm segment name astra_<pid>_<nonce>");
  }
  segment_ = config.text("segment");
  if (!valid_segment(segment_)) {
    throw ConfigError(
        config.origin(), key,
        "must match astra_<1-10 digits>_<1-32 letters or digits>, got '" + segment_ + "'");
  }
  path_ = "/" + segment_;
}

std::uint32_t ShmTransport::open(const TransportSettings& settings) {
  timeout_ms_ = settings.timeout_ms;
  ack_deadline_ = Deadline{};
  ack_deadline_.timeout_ms = timeout_ms_;

  Descriptor fd{shm_open(path_.c_str(), O_CREAT | O_EXCL | O_RDWR, kSegmentMode)};
  if (fd.get() < 0) {
    if (errno == EEXIST) {
      // ADR-0024: nothing safely identifies an old segment, so it is neither unlinked nor touched.
      throw std::runtime_error("shm segment '" + segment_ +
                               "' already exists (stale or foreign); it is left in place");
    }
    throw system_error("shm_open", segment_);
  }
  owns_name_ = true;
  try {
    if (ftruncate(fd.get(), static_cast<off_t>(kRegionSize)) != 0) {
      throw system_error("ftruncate", segment_);
    }
    void* mapped = mmap(nullptr, kRegionSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (mapped == MAP_FAILED) {
      throw system_error("mmap", segment_);
    }
    region_ = static_cast<astra_shm_region_v1*>(mapped);
    fd.reset();

    // The whole header, peer-owned words included, before SIM_READY (ADR-0024). ftruncate zeroed
    // the region, so sim_state already reads INIT and peer_state NONE; payloads are zero.
    astra_shm_header_v1& header = region_->header;
    header.magic = ASTRA_SHM_MAGIC;
    header.layout_version = ASTRA_SHM_LAYOUT_VERSION;
    header.layout_size = static_cast<std::uint32_t>(kRegionSize);
    header.icd_version = ASTRA_ICD_VERSION;
    header.peer_state = ASTRA_SHM_PEER_NONE;
    header.req_seq = ASTRA_SHM_SEQ_INIT;
    header.ack_seq = ASTRA_SHM_SEQ_INIT;
    header.sim_pid = static_cast<std::int32_t>(getpid());
    header.peer_pid = 0;
    store_release(header.sim_state, ASTRA_SHM_SIM_READY);  // publishes everything above
    wake(&header.sim_state);

    // The peer's PEER_READY, against its own handshake deadline (ADR-0024: same budget, separate
    // wait). The decision uses the bounded wait's own value, loaded before its clock read, never a
    // later reload (adversarial review R-1): expected NONE returns at once (EAGAIN) when the peer
    // is already ready, and PEER_READY first observed after the deadline is expiry, not readiness.
    Deadline handshake;
    handshake.timeout_ms = settings.handshake_timeout_ms;
    for (;;) {
      const WaitResult waited = wait_until(&header.peer_state, ASTRA_SHM_PEER_NONE, handshake);
      if (waited.kind == WaitKind::kExpired) {
        throw std::runtime_error("no peer reached PEER_READY on shm segment '" + segment_ +
                                 "' within handshake_timeout_ms (" +
                                 std::to_string(settings.handshake_timeout_ms) + ")");
      }
      if (waited.kind == WaitKind::kFault) {
        throw system_error("futex wait", segment_);
      }
      if (waited.value == ASTRA_SHM_PEER_READY) {
        break;
      }
      if (waited.value != ASTRA_SHM_PEER_NONE) {
        throw std::runtime_error("the peer on shm segment '" + segment_ +
                                 "' wrote an unknown peer_state " + std::to_string(waited.value));
      }
    }
    if (load_acquire(header.ack_seq) != ASTRA_SHM_SEQ_INIT) {
      throw std::runtime_error("the peer on shm segment '" + segment_ +
                               "' published an ack before frame 0");
    }
    unlink_name();  // plan Q-6: nothing needs the name once both sides are mapped
    return ASTRA_SHM_SEQ_INIT;
  } catch (...) {
    // Core never calls close() after a failed open(): release everything here. The descriptor
    // closes with `fd`; the mapping and the owned name go now. Nothing is written to the region.
    release();
    throw;
  }
}

void ShmTransport::publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept {
  std::memcpy(&region_->request, &request, sizeof request);
  store_release(region_->header.req_seq, seq);  // publishes the payload above
  wake(&region_->header.req_seq);
  ack_deadline_ =
      Deadline{};  // disarmed: the next wait_ack fixes this publication's deadline (Q-2)
  ack_deadline_.timeout_ms = timeout_ms_;
}

AckObservation ShmTransport::wait_ack(std::uint32_t unchanged) noexcept {
  const WaitResult waited = wait_until(&region_->header.ack_seq, unchanged, ack_deadline_);
  if (waited.kind != WaitKind::kReturned) {
    // Expiry wins over whatever was loaded: an ack first seen after the deadline is a late ack,
    // which halts (transport.hpp; critique F-2). A futex fault is terminal too.
    return AckObservation{AckStatus::kExpired, waited.value};
  }
  return AckObservation{AckStatus::kObserved, waited.value};  // verbatim; core classifies it
}

void ShmTransport::read_reply(astra_sw_to_sim_v1& reply) noexcept {
  // Ordered by the acquire load in the wait_ack call that returned this publication's seq.
  std::memcpy(&reply, &region_->reply, sizeof reply);
}

void ShmTransport::close(CloseReason reason) noexcept {
  if (closed_) {
    return;
  }
  closed_ = true;
  if (reason == CloseReason::kNormal && region_ != nullptr) {
    // ADR-0024's normal shutdown, in this order: the HAL waits on req_seq, so the close must change
    // THAT word, or a wake landing between its state check and its FUTEX_WAIT would be lost.
    astra_shm_header_v1& header = region_->header;
    store_release(header.sim_state, ASTRA_SHM_SIM_CLOSED);
    store_release(header.req_seq, ASTRA_SHM_CLOSE_SEQ);
    wake(&header.req_seq);
    wake(&header.sim_state);
  }
  // kAfterFailure writes nothing (plan Q-3): the peer ends at its own peer_timeout_ms.
  release();
}

void ShmTransport::release() noexcept {
  if (region_ != nullptr) {
    munmap(region_, kRegionSize);
    region_ = nullptr;
  }
  unlink_name();
}

void ShmTransport::unlink_name() noexcept {
  if (owns_name_) {
    shm_unlink(path_.c_str());  // ENOENT (the driver got there first) is fine
    owns_name_ = false;
  }
}

namespace {

std::unique_ptr<Model> create() { return std::make_unique<ShmTransport>(); }

constexpr PluginDescriptor kDescriptor{"sim", "transport_shm", kCoreApiVersion, kBuiltWith,
                                       &create};

}  // namespace
}  // namespace astra::shm_transport

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() {
  return &astra::shm_transport::kDescriptor;
}
