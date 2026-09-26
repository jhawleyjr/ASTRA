// M4 PR 4 (docs/plans/m4-pr4-shm-transport.md): the HAL's shm backend (R10), its handshake and
// exit-status table (Q-3 with Joe's change 1), close-versus-expiry precedence, and ADR-0024's two
// interleaving tests -- against a test-side creator, or the REAL shm plugin where the sim's own
// close is the point. The --wrap hooks are in wraps.cpp; the timing-bound tests in timing_test.cpp.
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "embedded/hal/astra_hal.h"
#include "tests/lockstep/shm_test_support.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

using astra::CloseReason;
using astra_hal_shm::WaitKind;
using shm_test::milliseconds;

constexpr std::uint32_t kLongMs = 5000;
constexpr milliseconds kPrompt{1000};   // "immediately": far below the 5000 ms budgets
constexpr milliseconds kBarrier{2000};  // every interleaving barrier times out (Joe's change 3)
constexpr std::uint32_t kShortPeerMs = 200;

class HalShm : public shm_test::LockstepTest {
 protected:
  void TearDown() override {
    hal_close();
    LockstepTest::TearDown();
  }
};

astra_hal_config hal_config(const std::string& segment, std::uint32_t handshake_ms = kLongMs,
                            std::uint32_t peer_ms = kLongMs) {
  return astra_hal_config{segment.c_str(), handshake_ms, peer_ms};
}

// A HAL session against the real shm plugin: hal_init on a helper thread while the plugin's open()
// runs its side of the handshake here.
struct RealSim {
  std::string segment = shm_test::unique_segment();
  std::unique_ptr<shm_test::LoadedShm> shm = shm_test::load_shm(segment);
};

std::unique_ptr<RealSim> real_sim_and_hal(std::uint32_t peer_ms) {
  auto sim = std::make_unique<RealSim>();
  std::atomic<astra_hal_status> init{ASTRA_HAL_ERROR};
  {
    const astra_hal_config config = hal_config(sim->segment, kLongMs, peer_ms);
    const std::jthread hal([&] { init = hal_init(&config); });
    static_cast<void>(sim->shm->transport->open(shm_test::settings()));
  }
  EXPECT_EQ(init.load(), ASTRA_HAL_OK);
  return sim;
}

bool barrier(const std::function<bool(const shm_test::Hooks&)>& ready) {
  shm_test::Hooks& hooks = shm_test::hooks();
  std::unique_lock lock(hooks.mutex);
  return hooks.changed.wait_for(lock, kBarrier, [&] { return ready(hooks); });
}

// ---------------------------------------------------------------------------------------------
// R10: the calls.

TEST_F(HalShm, NowIsFrameDerivedAndSendPublishesTheRequestsSequence) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  EXPECT_TRUE(sim.wait_peer_ready(kPrompt));
  EXPECT_EQ(sim.header().peer_pid, static_cast<std::int32_t>(getpid()));
  EXPECT_EQ(hal_now(), 0.0) << "no frame yet";

  sim.publish(42);
  astra_sim_to_sw_v1 in{};
  ASSERT_EQ(hal_wait_frame(&in), ASTRA_HAL_OK);
  EXPECT_EQ(in.frame, 42U);
  EXPECT_EQ(hal_now(), in.t) << "frame-derived: the request's own t";
  EXPECT_EQ(hal_now(), static_cast<double>(42) * 1e-3);

  astra_sw_to_sim_v1 out{};
  out.magic = ASTRA_ICD_MAGIC;
  out.version = ASTRA_ICD_VERSION;
  out.frame = 99;  // deliberately wrong: the ack is the request's sequence, never out->frame
  out.elevator = 0.5;
  ASSERT_EQ(hal_send(&out), ASTRA_HAL_OK);
  EXPECT_EQ(shm_test::load(sim.header().ack_seq), 42U);
  EXPECT_TRUE(shm_test::same_bytes(sim.region().reply, out)) << "all 64 bytes";
}

TEST_F(HalShm, SendWithoutPendingFrameIsError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  const astra_sw_to_sim_v1 out{};
  EXPECT_EQ(hal_send(&out), ASTRA_HAL_ERROR);
  EXPECT_EQ(shm_test::load(sim.header().ack_seq), ASTRA_SHM_SEQ_INIT) << "nothing published";
}

TEST_F(HalShm, WaitWithReplyPendingIsError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  sim.publish(0);
  astra_sim_to_sw_v1 in{};
  ASSERT_EQ(hal_wait_frame(&in), ASTRA_HAL_OK);
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_ERROR) << "the reply to frame 0 was never sent";
}

TEST_F(HalShm, UninitializedAndDoubleInitAreErrors) {
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_ERROR);
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  EXPECT_EQ(hal_init(&config), ASTRA_HAL_ERROR) << "one session per process";
  const astra_hal_config zero_timeout{segment.c_str(), 0, kLongMs};
  hal_close();
  EXPECT_EQ(hal_init(&zero_timeout), ASTRA_HAL_ERROR);
  const astra_hal_config slash{"astra_1/x", kLongMs, kLongMs};
  EXPECT_EQ(hal_init(&slash), ASTRA_HAL_ERROR);
}

TEST_F(HalShm, InitCloseInitCycle) {
  const std::string first = shm_test::unique_segment();
  const std::string second = shm_test::unique_segment();
  shm_test::Creator a(first);
  shm_test::Creator b(second);
  a.create_ready();
  b.create_ready();
  const astra_hal_config config_a = hal_config(first);
  const astra_hal_config config_b = hal_config(second);
  ASSERT_EQ(hal_init(&config_a), ASTRA_HAL_OK);
  hal_close();
  ASSERT_EQ(hal_init(&config_b), ASTRA_HAL_OK);
  EXPECT_TRUE(b.wait_peer_ready(kPrompt));
  EXPECT_EQ(hal_now(), 0.0) << "state reset by hal_close";
}

TEST_F(HalShm, ExitStatusTable) {
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_OK), 0);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_CLOSED), 0);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_TIMEOUT), 2);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_HANDSHAKE_TIMEOUT), 3);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_BAD_LAYOUT), 4);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_PROTOCOL), 5);
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_ERROR), 6);
}

// ---------------------------------------------------------------------------------------------
// Protocol faults.

TEST_F(HalShm, RequestFrameMismatchIsProtocolError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  const astra_sim_to_sw_v1 request = shm_test::request_for(43);
  std::memcpy(&sim.region().request, &request, sizeof request);
  shm_test::store(sim.header().req_seq, 42);
  shm_test::wake(sim.header().req_seq);
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_PROTOCOL);
}

TEST_F(HalShm, RequestFrameHighBitsMismatchIsProtocolError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  // Agrees with req_seq in its low 32 bits only (Joe's nit: compare the full 64-bit frame).
  const astra_sim_to_sw_v1 request = shm_test::request_for((std::uint64_t{1} << 32U) + 42U);
  std::memcpy(&sim.region().request, &request, sizeof request);
  shm_test::store(sim.header().req_seq, 42);
  shm_test::wake(sim.header().req_seq);
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_PROTOCOL);
}

TEST_F(HalShm, RequestBadMagicIsProtocolError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  astra_sim_to_sw_v1 request = shm_test::request_for(0);
  request.magic ^= 1U;
  std::memcpy(&sim.region().request, &request, sizeof request);
  shm_test::store(sim.header().req_seq, 0);
  shm_test::wake(sim.header().req_seq);
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_PROTOCOL);
}

TEST_F(HalShm, StaleSegmentWithPublishedRequestIsProtocolError) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  shm_test::store(sim.header().req_seq, 5);  // a dead sim's session, mid-run
  const astra_hal_config config = hal_config(segment);
  const auto start = shm_test::Clock::now();
  EXPECT_EQ(hal_init(&config), ASTRA_HAL_PROTOCOL);
  EXPECT_LT(shm_test::elapsed_ms(start), static_cast<double>(kPrompt.count()));
  EXPECT_EQ(shm_test::load(sim.header().peer_state), ASTRA_SHM_PEER_NONE) << "never claimed";
}

TEST_F(HalShm, ClosedDuringHandshakeIsProtocolError) {
  {
    SCOPED_TRACE("CLOSED already stored when the HAL attaches");
    const std::string segment = shm_test::unique_segment();
    shm_test::Creator sim(segment);
    sim.size();
    sim.init_header();
    shm_test::store(sim.header().sim_state, ASTRA_SHM_SIM_CLOSED);
    const astra_hal_config config = hal_config(segment);
    const auto start = shm_test::Clock::now();
    EXPECT_EQ(hal_init(&config), ASTRA_HAL_PROTOCOL) << "Joe's change 1: not a clean exit";
    EXPECT_LT(shm_test::elapsed_ms(start), static_cast<double>(kPrompt.count()));
  }
  {
    SCOPED_TRACE("CLOSED stored while the HAL waits on sim_state");
    const std::string segment = shm_test::unique_segment();
    shm_test::Creator sim(segment);
    sim.size();
    std::atomic<astra_hal_status> status{ASTRA_HAL_OK};
    {
      const astra_hal_config config = hal_config(segment);
      const std::jthread hal([&] { status = hal_init(&config); });
      ASSERT_TRUE(barrier([](const shm_test::Hooks& h) { return h.waited_on_sim_state; }))
          << "barrier: the HAL never waited on sim_state";
      shm_test::store(sim.header().sim_state, ASTRA_SHM_SIM_CLOSED);
      shm_test::wake(sim.header().sim_state);
    }
    EXPECT_EQ(status.load(), ASTRA_HAL_PROTOCOL);
  }
}

TEST_F(HalShm, BadHeaderIsImmediateBadLayout) {
  struct Case {
    const char* name;
    std::function<void(astra_shm_header_v1&)> corrupt;
  };
  const std::array<Case, 4> cases = {{
      {"magic", [](astra_shm_header_v1& h) { h.magic ^= 1U; }},
      {"layout version", [](astra_shm_header_v1& h) { h.layout_version += 1U; }},
      {"layout size", [](astra_shm_header_v1& h) { h.layout_size += 8U; }},
      {"icd version", [](astra_shm_header_v1& h) { h.icd_version += 1U; }},
  }};
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    const std::string segment = shm_test::unique_segment();
    shm_test::Creator sim(segment);
    sim.size();
    sim.init_header();
    c.corrupt(sim.header());
    sim.ready();
    const astra_hal_config config = hal_config(segment);
    const auto start = shm_test::Clock::now();
    EXPECT_EQ(hal_init(&config), ASTRA_HAL_BAD_LAYOUT);
    EXPECT_LT(shm_test::elapsed_ms(start), static_cast<double>(kPrompt.count()))
        << "a hard failure, not a retry until the handshake deadline";
    EXPECT_EQ(shm_test::load(sim.header().peer_state), ASTRA_SHM_PEER_NONE);
  }
}

// ---------------------------------------------------------------------------------------------
// Close versus expiry (ADR-0024; critique F-2, F-5): forced deterministically through the
// wait wrapper on a REAL expiry result, with the real plugin as the sim.

TEST_F(HalShm, CloseBeatsExpiry) {
  const auto sim = real_sim_and_hal(kShortPeerMs);
  std::atomic<int> fired{0};
  shm_test::hooks().after_wait = [&](const shm_test::WaitCall& call, astra_hal_shm::WaitResult& r) {
    if (call.offset == shm_test::offset_of_req_seq() && r.kind == WaitKind::kExpired &&
        fired++ == 0) {
      sim->shm->transport->close(CloseReason::kNormal);  // lands after expiry, before the rechecks
    }
  };
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_CLOSED);
  EXPECT_EQ(fired.load(), 1) << "the hook ran on a real expiry";
}

TEST_F(HalShm, RequestAfterExpiryIsTimeout) {
  const auto sim = real_sim_and_hal(kShortPeerMs);
  std::atomic<int> fired{0};
  shm_test::hooks().after_wait = [&](const shm_test::WaitCall& call, astra_hal_shm::WaitResult& r) {
    if (call.offset == shm_test::offset_of_req_seq() && r.kind == WaitKind::kExpired &&
        fired++ == 0) {
      sim->shm->transport->publish(shm_test::request_for(0), 0);  // an ordinary request, late
    }
  };
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_TIMEOUT) << "only a close outranks expiry";
  EXPECT_EQ(fired.load(), 1);
}

// The deterministic form of critique F-1 on the HAL side: an already-past deadline and a kernel
// that returns early (EAGAIN) must still report expiry, and the expiry is latched.
TEST_F(HalShm, WaitUntilLatchesExpiryWhenTheKernelReturnsEarly) {
  std::uint32_t word = 7;
  astra_hal_shm::Deadline deadline;
  deadline.timeout_ms = 1;
  ASSERT_EQ(astra_hal_shm_wait_until(&word, 7, &deadline).kind, WaitKind::kExpired);
  astra_hal_shm::Deadline past;
  past.timeout_ms = 1;
  past.armed = true;
  past.abs = deadline.abs;
  const astra_hal_shm::WaitResult early = astra_hal_shm_wait_until(&word, 8, &past);
  EXPECT_EQ(early.kind, WaitKind::kExpired) << "the clock, not ETIMEDOUT, decides";
  EXPECT_EQ(early.value, 7U);
  EXPECT_TRUE(past.expired);
  EXPECT_EQ(astra_hal_shm_wait_until(&word, 8, &past).kind, WaitKind::kExpired);
}

TEST_F(HalShm, OrdinaryExpiryIsTimeout) {
  const auto sim = real_sim_and_hal(kShortPeerMs);
  astra_sim_to_sw_v1 in{};
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_TIMEOUT);
}

// ---------------------------------------------------------------------------------------------
// ADR-0024 interleaving test 2: the sim's shutdown runs between the HAL's pre-wait state check and
// its FUTEX_WAIT (the wrapper runs the REAL plugin's close() before calling the real wait with the
// HAL's stale expected value). Without CLOSE_SEQ on req_seq the HAL would sleep to its deadline.

TEST_F(HalShm, CloseBetweenPreWaitCheckAndFutexWaitExitsPromptly) {
  const auto sim = real_sim_and_hal(kLongMs);
  std::atomic<int> fired{0};
  shm_test::hooks().before_wait = [&](const shm_test::WaitCall& call) {
    if (call.offset == shm_test::offset_of_req_seq() && fired++ == 0) {
      sim->shm->transport->close(CloseReason::kNormal);
    }
  };
  astra_sim_to_sw_v1 in{};
  const auto start = shm_test::Clock::now();
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_CLOSED);
  EXPECT_LT(shm_test::elapsed_ms(start), 100.0) << "prompt, not at the 5000 ms peer deadline";
  EXPECT_EQ(fired.load(), 1) << "the hook ran exactly once, so the interleaving happened";
}

// ---------------------------------------------------------------------------------------------
// ADR-0024 interleaving test 1 (critique F-4, Joe's change 3): the creator paused before
// ftruncate and again after sizing but before SIM_READY. Barriers on the fstat/mmap/wait wrappers
// prove the HAL ran inside each window; every barrier times out rather than hangs.

TEST_F(HalShm, CreatorPausedBeforeSizingAndBeforeReadyAttachesCleanly) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Hooks& hooks = shm_test::hooks();
  {
    const std::lock_guard lock(hooks.mutex);
    hooks.watched = "/dev/shm/" + segment;
  }
  shm_test::Creator sim(segment);  // exists, zero length
  std::atomic<bool> returned{false};
  std::atomic<astra_hal_status> status{ASTRA_HAL_ERROR};
  constexpr auto kLayout = static_cast<long long>(sizeof(astra_shm_region_v1));
  {
    const astra_hal_config config = hal_config(segment);
    const std::jthread hal([&] {
      {
        const std::lock_guard lock(hooks.mutex);
        hooks.observed_thread = std::this_thread::get_id();
      }
      status = hal_init(&config);
      returned = true;
    });

    ASSERT_TRUE(barrier([](const shm_test::Hooks& h) {
      return std::ranges::any_of(h.fstat_sizes, [](long long size) { return size < kLayout; });
    })) << "barrier 1: the HAL never observed the unsized segment";
    EXPECT_FALSE(returned.load());
    sim.size();  // sized, header still all zero (magic 0, sim_state INIT)

    ASSERT_TRUE(barrier([](const shm_test::Hooks& h) { return h.waited_on_sim_state; }))
        << "barrier 2: the HAL never waited on sim_state in the sized-but-unready window";
    EXPECT_FALSE(returned.load()) << "no premature validation (magic is still 0)";
    EXPECT_EQ(shm_test::load(sim.header().peer_state), ASTRA_SHM_PEER_NONE)
        << "no premature readiness";

    sim.init_header();
    sim.ready();
  }
  EXPECT_EQ(status.load(), ASTRA_HAL_OK);
  EXPECT_EQ(shm_test::load(sim.header().peer_state), ASTRA_SHM_PEER_READY);
  const std::lock_guard lock(hooks.mutex);
  int short_sizes = 0;
  int full_sizes = 0;
  for (const long long size : hooks.fstat_sizes) {
    (size < kLayout ? short_sizes : full_sizes) += 1;
  }
  EXPECT_GE(short_sizes, 1) << "the fstat wrapper fired on the short segment";
  EXPECT_GE(full_sizes, 1) << "the fstat wrapper fired on the sized segment";
  EXPECT_EQ(hooks.mmaps, 1) << "the mmap wrapper fired exactly once";
  EXPECT_FALSE(hooks.mmap_while_short) << "never mapped a short segment";
}

// ---------------------------------------------------------------------------------------------
// The HAL's per-exchange path allocates nothing (plan R6 on the peer side). A raw sim thread,
// itself allocation-free inside the window, publishes 1000 frames.

TEST_F(HalShm, WaitAndSendAllocateNothing) {
  constexpr std::uint64_t kFrames = 1000;
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config = hal_config(segment);
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  std::atomic<bool> start{false};
  std::jthread publisher([&] {
    while (!start) {
      std::this_thread::yield();
    }
    for (std::uint64_t frame = 0; frame < kFrames; ++frame) {
      sim.publish(frame);
      const auto seq = static_cast<std::uint32_t>(frame);
      while (shm_test::load(sim.header().ack_seq) != seq) {
        shm_test::wait_while_equal(sim.header().ack_seq, shm_test::load(sim.header().ack_seq),
                                   milliseconds{10});
      }
    }
  });
  int failures = 0;
  astra_sim_to_sw_v1 in{};
  astra_sw_to_sim_v1 out{};
  const std::size_t before = astra_test::allocation_count();
  start = true;
  for (std::uint64_t frame = 0; frame < kFrames; ++frame) {
    failures += hal_wait_frame(&in) == ASTRA_HAL_OK && in.frame == frame ? 0 : 1;
    out.frame = in.frame;
    failures += hal_send(&out) == ASTRA_HAL_OK ? 0 : 1;
  }
  const std::size_t after = astra_test::allocation_count();
  publisher.join();
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(after, before) << "zero allocations over 1000 hal_wait_frame/hal_send pairs";
}

}  // namespace
