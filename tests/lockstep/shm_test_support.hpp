// Test support for the shm lockstep tests (M4 PR 4, docs/plans/m4-pr4-shm-transport.md): unique
// segment names, a test-side creator and raw peer built on the shared layout header, the real shm
// plugin loaded in-process, the echo-peer child process guard, the dead-creator sweep ADR-0024
// allows, and the link-time wrap hooks (wraps.cpp). Test code may read clocks; the product may not.
#pragma once

#include <gtest/gtest.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "astra/interfaces/transport.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "embedded/hal/astra_shm_layout.h"
#include "embedded/hal/shm/hal_shm_internal.h"

namespace shm_test {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// Joe's M4 Q3 bound: timeout 500 ms, allowance the greater of 20 ms and 10 % of it (50 ms).
inline constexpr std::uint32_t kTimeoutMs = 500;
inline constexpr milliseconds kTimeout{kTimeoutMs};
inline constexpr milliseconds kAllowance{50};
inline constexpr std::string_view kShmPlugin = "plugins/sim/transport/shm";

[[nodiscard]] double elapsed_ms(Clock::time_point since);

// Byte equality of two ICD/region structs: the protocol copies whole structs, padding included, so
// the tests compare exactly what crossed the wire.
template <class T>
[[nodiscard]] bool same_bytes(const T& a, const T& b) {
  return std::ranges::equal(std::as_bytes(std::span{&a, 1}), std::as_bytes(std::span{&b, 1}));
}

// astra_<getpid()>_<counter><salt>: unique per test process and call (ADR-0024's driver form).
[[nodiscard]] std::string unique_segment();

// Word helpers on a raw mapping, the same ordering vocabulary as the product.
[[nodiscard]] std::uint32_t load(const std::uint32_t& word);
void store(std::uint32_t& word, std::uint32_t value);
void wake(const std::uint32_t& word);
// Futex-waits while word == expected, at most `limit` (relative; test code).
void wait_while_equal(const std::uint32_t& word, std::uint32_t expected, milliseconds limit);
// Polls `ready` every millisecond until true or `limit`; returns the final value.
[[nodiscard]] bool eventually(const std::function<bool()>& ready, milliseconds limit);

// A mapping of an existing segment, opened by name once it exists and is fully sized.
class Mapping {
 public:
  [[nodiscard]] static std::unique_ptr<Mapping> attach(const std::string& segment,
                                                       milliseconds limit);
  ~Mapping();
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  Mapping(Mapping&&) = delete;
  Mapping& operator=(Mapping&&) = delete;
  [[nodiscard]] astra_shm_region_v1& region() { return *region_; }
  [[nodiscard]] astra_shm_header_v1& header() { return region_->header; }

 private:
  explicit Mapping(astra_shm_region_v1* region) : region_(region) {}
  astra_shm_region_v1* region_;
};

// The test playing the peer against the real plugin: attach, wait SIM_READY, publish PEER_READY.
[[nodiscard]] std::unique_ptr<Mapping> raw_peer_handshake(const std::string& segment,
                                                          milliseconds limit);
// Raw-peer reply to request `seq`: zeroed reply echoing the request's frame, then the ack.
void raw_peer_reply(Mapping& peer, std::uint32_t seq);

// The test playing the sim for the HAL: ADR-0024's creation order, one step per call.
class Creator {
 public:
  explicit Creator(std::string segment);  // shm_open(O_CREAT | O_EXCL)
  ~Creator();                             // munmap, close, unlink
  Creator(const Creator&) = delete;
  Creator& operator=(const Creator&) = delete;
  Creator(Creator&&) = delete;
  Creator& operator=(Creator&&) = delete;
  void size();         // ftruncate to the layout, then mmap
  void init_header();  // a valid header, sim_state still INIT
  void ready();        // release SIM_READY, wake
  void create_ready() {
    size();
    init_header();
    ready();
  }
  [[nodiscard]] bool wait_peer_ready(milliseconds limit);
  // Fills a valid request for `frame` (t = frame * 1e-3), release req_seq = (uint32_t)frame, wake.
  void publish(std::uint64_t frame);
  void close_normal();  // CLOSED, CLOSE_SEQ, wake both (ADR-0024 order)
  [[nodiscard]] astra_shm_region_v1& region() { return *region_; }
  [[nodiscard]] astra_shm_header_v1& header() { return region_->header; }
  [[nodiscard]] const std::string& segment() const { return segment_; }

 private:
  std::string segment_;
  int fd_ = -1;
  astra_shm_region_v1* region_ = nullptr;
};

// The real shm plugin, loaded and init'ed in-process (as LockstepExchange would), for the HAL tests
// that need the real sim side (the close hooks) and the ShmTransport tests.
struct LoadedShm {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin plugin{};
  astra::Transport* transport = nullptr;
  astra::SignalRegistry registry;
};
[[nodiscard]] std::unique_ptr<LoadedShm> load_shm(const std::string& segment);

[[nodiscard]] astra::TransportSettings settings(std::uint32_t timeout_ms = kTimeoutMs,
                                                std::uint32_t handshake_timeout_ms = 5000);

// A valid request for `frame` (t = frame * 1e-3), as core would publish.
[[nodiscard]] astra_sim_to_sw_v1 request_for(std::uint64_t frame);

// The real plugin opened against a raw test peer (handshake done on a helper thread).
struct OpenedShm {
  std::string segment;
  std::unique_ptr<LoadedShm> shm;
  std::unique_ptr<Mapping> peer;
  std::uint32_t initial_ack = 0;
};
[[nodiscard]] std::unique_ptr<OpenedShm> open_with_raw_peer(
    const astra::TransportSettings& transport_settings = settings());

// Core's receive loop (lockstep_exchange.cpp) replicated over wait_ack: unchanged observations
// are counted and retried; a 10 s wall-clock guard reports a runaway instead of hanging.
struct Outcome {
  astra::AckStatus status = astra::AckStatus::kExpired;
  std::uint32_t ack = 0;
  int unchanged = 0;
  double ms = 0.0;
  bool runaway = false;
};
[[nodiscard]] Outcome drive_wait(astra::Transport& transport, std::uint32_t last);

// A raw peer thread against the real plugin inside a Simulation: handshakes, then calls `respond`
// for every new request sequence until destroyed. `stop` lets a blocking responder bail out.
class ScriptedPeer {
 public:
  using Respond = std::function<void(Mapping&, std::uint32_t seq, const std::atomic<bool>& stop)>;
  ScriptedPeer(std::string segment, Respond respond);
  ~ScriptedPeer();
  ScriptedPeer(const ScriptedPeer&) = delete;
  ScriptedPeer& operator=(const ScriptedPeer&) = delete;
  ScriptedPeer(ScriptedPeer&&) = delete;
  ScriptedPeer& operator=(ScriptedPeer&&) = delete;
  [[nodiscard]] bool handshook() const { return handshook_; }

 private:
  void run(const std::string& segment, const Respond& respond);
  std::atomic<bool> stop_{false};
  std::atomic<bool> handshook_{false};
  std::thread thread_;
};
// The default responder: a correct reply (raw_peer_reply).
void reply_correctly(Mapping& peer, std::uint32_t seq, const std::atomic<bool>& stop);

// A posix_spawned echo peer (tests/lockstep/echo_peer.cpp). The destructor terminates, then kills,
// then reaps on every path (bounded).
class Child {
 public:
  // args exclude the program; notify adds --notify-fd 3 on a pipe this object reads.
  Child(const std::vector<std::string>& args, bool notify);
  ~Child();
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  Child(Child&&) = delete;
  Child& operator=(Child&&) = delete;
  [[nodiscard]] bool wait_notify(milliseconds limit) const;  // one byte on the notify pipe
  // Exit code, or 128 + signal; nullopt if still running after `limit`.
  [[nodiscard]] std::optional<int> wait_exit(milliseconds limit);
  void kill_now() const;
  [[nodiscard]] std::string output() const;  // everything the child wrote to stdout (after exit)
  [[nodiscard]] pid_t pid() const { return pid_; }

 private:
  pid_t pid_ = -1;
  int notify_fd_ = -1;
  int stdout_fd_ = -1;
  std::optional<int> status_;
};

// A flat-Earth Simulation (PR 3's transport fixture: tests/plugins/input_consumer maps only the
// elevator) with a shm transport on `segment`.
struct SimSpec {
  std::string segment;
  std::uint64_t frames = 100;
  std::uint32_t divisor = 10;
  std::uint32_t timeout_ms = kTimeoutMs;
  std::uint32_t handshake_timeout_ms = 5000;
  std::uint32_t peer_timeout_ms = 5000;
  std::string limits =
      "{ elevator: [-1.0, 1.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], throttle: [0.0, 1.0] }";
  bool with_segment_key = true;
};
[[nodiscard]] std::string sim_yaml(const SimSpec& spec);
[[nodiscard]] astra::Simulation build_sim(const SimSpec& spec);

// Echo-peer hold mode with commands the SimSpec limits accept (elevator 0.01, throttle 0.5).
inline const std::vector<std::string> kHold{"--mode", "hold", "--commands", "0.01,0,0,0.5"};
inline constexpr double kHoldElevator = 0.01;

// Echo-peer arguments: --segment, --handshake-timeout-ms, --peer-timeout-ms, then `extra`.
[[nodiscard]] std::vector<std::string> peer_args(const std::string& segment,
                                                 std::uint32_t handshake_ms, std::uint32_t peer_ms,
                                                 const std::vector<std::string>& extra);

// ADR-0024's test fixture: unlink /dev/shm/astra_<pid>_* whose creator pid is gone.
void sweep_dead_creators();
[[nodiscard]] std::vector<std::string> own_segments();  // astra_<getpid()>_* still present

// Every lockstep test: sweep first; afterwards, no segment of ours left and no unreaped child.
class LockstepTest : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;
};

// Link-time wrap hooks (wraps.cpp; plan Q-4). Set only while no HAL thread runs.
struct WaitCall {
  std::size_t offset;  // byte offset of the waited word in the region (mappings are page-aligned);
                       // kNoWord for the attach sleep
};
inline constexpr std::size_t kNoWord = static_cast<std::size_t>(-1);
[[nodiscard]] std::size_t offset_of_req_seq();
[[nodiscard]] std::size_t offset_of_sim_state();

struct Hooks {
  std::function<void(const WaitCall&)> before_wait;
  std::function<void(const WaitCall&, astra_hal_shm::WaitResult&)> after_wait;
  std::atomic<int> wait_calls{0};

  // fstat/mmap observation of one segment ("/dev/shm/<segment>"), guarded by `mutex`.
  std::mutex mutex;
  std::condition_variable changed;
  std::string watched;              // empty: observe nothing
  std::thread::id observed_thread;  // only this thread's calls count (the HAL's, not the creator's)
  std::vector<long long> fstat_sizes;  // every size fstat reported for the watched segment
  int mmaps = 0;                       // mmaps of the watched segment
  bool mmap_while_short = false;       // an mmap when the latest fstat size was below the layout
  bool waited_on_sim_state = false;    // the HAL entered its wait on sim_state
};
Hooks& hooks();
void reset_hooks();

}  // namespace shm_test
