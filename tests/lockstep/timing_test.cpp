// M4 PR 4 (docs/plans/m4-pr4-shm-transport.md): every test that asserts Joe's M4 Q3 bound -- a 500
// ms timeout plus the greater of 20 ms and 10 % (so [500, 550] ms) -- or otherwise depends on wall
// time. This executable runs RUN_SERIAL (plan Q-8) so ctest --parallel load stays out of the wake
// latency; the bound itself is never widened. Wait duration and kill-to-halt are measured
// separately (M4 R4).
#include <gtest/gtest.h>
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "astra/errors.hpp"
#include "astra/kernel/lockstep_exchange.hpp"
#include "embedded/hal/astra_hal.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using astra::AckStatus;
using astra::HealthCheck;
using astra::TransportFault;
using shm_test::milliseconds;

constexpr double kLowerMs = static_cast<double>(shm_test::kTimeoutMs);
constexpr double kUpperMs = kLowerMs + static_cast<double>(shm_test::kAllowance.count());

class ShmTransport : public shm_test::LockstepTest {};
class HalShm : public shm_test::LockstepTest {
 protected:
  void TearDown() override {
    hal_close();
    LockstepTest::TearDown();
  }
};
class EchoPeer : public shm_test::LockstepTest {};

#define EXPECT_WITHIN_BOUND(ms)                                                 \
  do {                                                                          \
    EXPECT_GE((ms), kLowerMs) << "expired before the deadline";                 \
    EXPECT_LE((ms), kUpperMs) << "exceeded timeout + max(20 ms, 10 %) (M4 Q3)"; \
  } while (false)

// ---------------------------------------------------------------------------------------------
// R4 on the sim side: one deadline per publication.

TEST_F(ShmTransport, SilentPeerWaitDurationIsTheTimeout) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  const shm_test::Outcome outcome =
      shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
  EXPECT_EQ(outcome.status, AckStatus::kExpired);
  EXPECT_WITHIN_BOUND(outcome.ms);
}

TEST_F(ShmTransport, RepeatedWakeWithoutStoreDoesNotRestartDeadline) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  std::atomic<bool> done{false};
  std::jthread waker([&] {
    while (!done) {
      shm_test::wake(opened->peer->header().ack_seq);  // FUTEX_WAKE, never a store
      std::this_thread::sleep_for(milliseconds{2});
    }
  });
  const shm_test::Outcome outcome =
      shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
  done = true;
  EXPECT_EQ(outcome.status, AckStatus::kExpired);
  EXPECT_GE(outcome.unchanged, 100) << "the wakes were reported (unchanged), not classified";
  EXPECT_WITHIN_BOUND(outcome.ms);
}

extern "C" void count_signal(int /*signal*/) {}

TEST_F(ShmTransport, SignalsDuringWaitDoNotRestartDeadline) {
  struct sigaction action {};
  struct sigaction previous {};
  action.sa_handler = &count_signal;  // no SA_RESTART: an interrupted futex wait returns EINTR
  ASSERT_EQ(sigaction(SIGUSR1, &action, &previous), 0);
  const auto opened = shm_test::open_with_raw_peer();  // a silent peer: nothing else wakes the wait
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  const pthread_t waiter = pthread_self();
  std::atomic<bool> done{false};
  std::jthread storm([&] {
    while (!done) {
      pthread_kill(waiter, SIGUSR1);
      std::this_thread::sleep_for(milliseconds{2});
    }
  });
  const shm_test::Outcome outcome =
      shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
  done = true;
  storm.join();
  sigaction(SIGUSR1, &previous, nullptr);
  EXPECT_EQ(outcome.status, AckStatus::kExpired);
  EXPECT_GE(outcome.unchanged, 10)
      << "with a silent peer, every unchanged return is an interrupted wait (critique F-5)";
  EXPECT_WITHIN_BOUND(outcome.ms);
}

TEST_F(ShmTransport, EachPublicationGetsItsOwnDeadline) {
  const auto opened = shm_test::open_with_raw_peer();
  std::uint32_t last = ASTRA_SHM_SEQ_INIT;
  for (std::uint32_t frame = 0; frame < 30; frame += 10) {
    SCOPED_TRACE(frame);
    opened->shm->transport->publish(shm_test::request_for(frame), frame);
    std::jthread peer([&] {
      std::this_thread::sleep_for(milliseconds{300});  // 3 x 300 ms > one 500 ms deadline
      shm_test::raw_peer_reply(*opened->peer, frame);
    });
    const shm_test::Outcome outcome = shm_test::drive_wait(*opened->shm->transport, last);
    EXPECT_EQ(outcome.status, AckStatus::kObserved) << "a deadline carried over would expire";
    EXPECT_EQ(outcome.ack, frame);
    last = frame;
  }
}

TEST_F(ShmTransport, HandshakeTimeoutWithNoPeerIsTheBudgetAndLeavesNoSegment) {
  const std::string segment = shm_test::unique_segment();
  const auto shm = shm_test::load_shm(segment);
  const auto start = shm_test::Clock::now();
  EXPECT_THROW(static_cast<void>(shm->transport->open(
                   shm_test::settings(shm_test::kTimeoutMs, shm_test::kTimeoutMs))),
               std::runtime_error);
  EXPECT_WITHIN_BOUND(shm_test::elapsed_ms(start));
  // Through Simulation: an init error naming transport (LockstepTest checks no segment is left).
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  spec.handshake_timeout_ms = shm_test::kTimeoutMs;
  EXPECT_THROW(static_cast<void>(shm_test::build_sim(spec)), astra::ConfigError);
}

// ---------------------------------------------------------------------------------------------
// R4 with the real echo peer: kill-to-halt, measured from the kill, separately from the above.

struct KillRun {
  astra::HaltReason halt;
  TransportFault fault = TransportFault::kNone;
  std::uint64_t clock_frame = 0;
  double exchanges = 0.0;
  bool finite = false;
  double since_event_ms = 0.0;  // kill (or the abort peer's last reply) to halt
  std::optional<int> peer_status;
};

KillRun run_until_peer_dies(const std::vector<std::string>& fault_flag, bool kill_it) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  spec.frames = 1000;
  std::vector<std::string> extra = shm_test::kHold;
  extra.insert(extra.end(), fault_flag.begin(), fault_flag.end());
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, 5000, extra), true);
  astra::Simulation sim = shm_test::build_sim(spec);
  std::atomic<std::int64_t> event_ns{0};
  std::jthread killer([&] {
    if (!peer.wait_notify(milliseconds{10000})) {
      return;
    }
    if (kill_it) {
      peer.kill_now();
    }
    event_ns = shm_test::Clock::now().time_since_epoch().count();
  });
  KillRun run;
  run.halt = sim.run();
  const std::int64_t halted_ns = shm_test::Clock::now().time_since_epoch().count();
  killer.join();
  run.since_event_ms = static_cast<double>(halted_ns - event_ns.load()) / 1e6;
  run.fault = sim.transport_fault();
  run.clock_frame = sim.clock().frame();
  const double* exchanges = sim.registry().find("transport.exchanges");
  run.exchanges = exchanges == nullptr ? -1.0 : *exchanges;
  run.finite = true;
  for (const double value : sim.vehicles()[0].state()) {
    run.finite = run.finite && std::isfinite(value);
  }
  run.peer_status = peer.wait_exit(milliseconds{5000});
  return run;
}

void expect_timeout_halt_at_frame_200(const KillRun& run) {
  EXPECT_EQ(run.halt.check, HealthCheck::kTransport);
  EXPECT_EQ(run.fault, TransportFault::kTimeout);
  EXPECT_EQ(run.halt.frame, 200U) << "20 replies (frames 0..190), then the 21st exchange";
  EXPECT_EQ(run.clock_frame, run.halt.frame) << "the clock does not advance past the halt";
  EXPECT_EQ(run.exchanges, 20.0) << "nothing staged after the peer died";
  EXPECT_TRUE(run.finite);
  EXPECT_LE(run.since_event_ms, kUpperMs) << "halts within one timeout (M4 R4)";
}

TEST_F(EchoPeer, KillToHaltWithinOneTimeout) {
  const KillRun run = run_until_peer_dies({"--stop-after", "20"}, true);
  expect_timeout_halt_at_frame_200(run);
  EXPECT_EQ(run.peer_status, 128 + SIGKILL);
}

TEST_F(EchoPeer, AbortToHaltWithinOneTimeout) {
  const KillRun run = run_until_peer_dies({"--abort-after", "20"}, false);
  expect_timeout_halt_at_frame_200(run);
  EXPECT_EQ(run.peer_status, 128 + SIGABRT);
}

// R4b on shm: a sim idle between run() calls for longer than peer_timeout_ms ends the peer
// nonzero, and the sim's next exchange halts kTransport (a detected failure, not a hang).
TEST_F(EchoPeer, SimPauseLongerThanPeerTimeoutEndsPeerNonzeroAndHaltsNextExchange) {
  constexpr std::uint32_t kPeerMs = 700;
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  spec.frames = 1000;
  spec.peer_timeout_ms = kPeerMs;
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, kPeerMs, shm_test::kHold), false);
  astra::Simulation sim = shm_test::build_sim(spec);
  ASSERT_FALSE(sim.run(100));
  const std::optional<int> status = peer.wait_exit(milliseconds{3000});
  EXPECT_EQ(status, 2) << "ASTRA_HAL_TIMEOUT";
  const astra::HaltReason halt = sim.run(100);
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, 100U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kTimeout);
}

// ---------------------------------------------------------------------------------------------
// R10 on the HAL side: the peer's own deadline.

TEST_F(HalShm, WaitReportsTimeoutAtPeerDeadline) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config{segment.c_str(), 5000, shm_test::kTimeoutMs};
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  astra_sim_to_sw_v1 in{};
  const auto start = shm_test::Clock::now();
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_TIMEOUT);
  EXPECT_WITHIN_BOUND(shm_test::elapsed_ms(start));
}

TEST_F(HalShm, RepeatedWakeWithoutStoreDoesNotRestartPeerDeadline) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Creator sim(segment);
  sim.create_ready();
  const astra_hal_config config{segment.c_str(), 5000, shm_test::kTimeoutMs};
  ASSERT_EQ(hal_init(&config), ASTRA_HAL_OK);
  std::atomic<bool> done{false};
  std::jthread waker([&] {
    while (!done) {
      shm_test::wake(sim.header().req_seq);
      std::this_thread::sleep_for(milliseconds{2});
    }
  });
  const int calls_before = shm_test::hooks().wait_calls.load();
  astra_sim_to_sw_v1 in{};
  const auto start = shm_test::Clock::now();
  EXPECT_EQ(hal_wait_frame(&in), ASTRA_HAL_TIMEOUT);
  const double ms = shm_test::elapsed_ms(start);
  done = true;
  EXPECT_GE(shm_test::hooks().wait_calls.load() - calls_before, 100)
      << "the wakes re-entered the wait";
  EXPECT_WITHIN_BOUND(ms);
}

TEST_F(HalShm, NoSegmentIsHandshakeTimeout) {
  const std::string segment = shm_test::unique_segment();
  const astra_hal_config config{segment.c_str(), shm_test::kTimeoutMs, 5000};
  const auto start = shm_test::Clock::now();
  EXPECT_EQ(hal_init(&config), ASTRA_HAL_HANDSHAKE_TIMEOUT);
  EXPECT_WITHIN_BOUND(shm_test::elapsed_ms(start));
}

}  // namespace
