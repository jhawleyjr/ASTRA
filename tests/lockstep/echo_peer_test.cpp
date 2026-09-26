// M4 PR 4 (docs/plans/m4-pr4-shm-transport.md): two processes -- the real Simulation with the shm
// plugin, and the echo peer (HAL + icd only) -- for R5 on shm: both start orders, the
// 10,000-exchange run, normal completion (exit 0, promptly), a failure close (not a clean exit,
// plan Q-3), and no orphans (LockstepTest). The kill/abort/pause tests are in timing_test.cpp
// (RUN_SERIAL).
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/lockstep_exchange.hpp"
#include "embedded/hal/astra_hal.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using astra::HealthCheck;
using astra::TransportFault;
using shm_test::milliseconds;

constexpr milliseconds kExitWait{5000};
constexpr milliseconds kPromptExit{1000};  // far below the 5000 ms peer_timeout_ms

class EchoPeer : public shm_test::LockstepTest {};

double signal(const astra::Simulation& sim, std::string_view name) {
  const double* value = sim.registry().find(name);
  return value == nullptr ? -1.0 : *value;
}

std::string summary(std::uint64_t handled, astra_hal_status status) {
  return "echo_peer: handled " + std::to_string(handled) + " status " +
         std::to_string(static_cast<int>(status));
}

TEST_F(EchoPeer, PeerStartedFirstCompletesHandshake) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, 5000, shm_test::kHold), false);
  {
    astra::Simulation sim = shm_test::build_sim(spec);
    const astra::HaltReason halt = sim.run();
    EXPECT_FALSE(halt) << static_cast<int>(sim.transport_fault());
    EXPECT_EQ(signal(sim, "transport.exchanges"), 10.0);
  }
  EXPECT_EQ(peer.wait_exit(kExitWait), 0);
  EXPECT_NE(peer.output().find(summary(10, ASTRA_HAL_CLOSED)), std::string::npos);
}

TEST_F(EchoPeer, SimStartedFirstCompletesHandshake) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  std::unique_ptr<shm_test::Child> peer;
  {
    const std::jthread late([&] {
      std::this_thread::sleep_for(milliseconds{300});  // from_config is already waiting
      peer = std::make_unique<shm_test::Child>(
          shm_test::peer_args(spec.segment, 5000, 5000, shm_test::kHold), false);
    });
    astra::Simulation sim = shm_test::build_sim(spec);
    EXPECT_FALSE(sim.run());
    EXPECT_EQ(signal(sim, "transport.exchanges"), 10.0);
  }
  ASSERT_NE(peer, nullptr);
  EXPECT_EQ(peer->wait_exit(kExitWait), 0);
}

TEST_F(EchoPeer, NormalCompletionExitsPeerZeroPromptly) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, 5000, shm_test::kHold), false);
  auto sim = std::make_unique<astra::Simulation>(shm_test::build_sim(spec));
  ASSERT_FALSE(sim->run());
  const auto closed = shm_test::Clock::now();
  sim.reset();  // close(kNormal): CLOSED, CLOSE_SEQ, wake
  const std::optional<int> status = peer.wait_exit(kExitWait);
  const double ms = shm_test::elapsed_ms(closed);
  EXPECT_EQ(status, 0);
  EXPECT_LT(ms, static_cast<double>(kPromptExit.count()))
      << "the close released the peer, not its 5000 ms deadline";
}

TEST_F(EchoPeer, SimulationCompletesTenThousandExchanges) {
  constexpr std::uint64_t kExchanges = 10000;
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  spec.frames = kExchanges;
  spec.divisor = 1;
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, 5000, shm_test::kHold), false);
  {
    astra::Simulation sim = shm_test::build_sim(spec);
    const astra::HaltReason halt = sim.run();
    ASSERT_FALSE(halt) << static_cast<int>(sim.transport_fault()) << " at " << halt.frame;
    EXPECT_EQ(signal(sim, "transport.exchanges"), static_cast<double>(kExchanges));
    EXPECT_EQ(signal(sim, "transport.missed"), 0.0);
    const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
    ASSERT_NE(recorder, nullptr);
    ASSERT_EQ(recorder->requests().size(), kExchanges);
    ASSERT_EQ(recorder->replies().size(), kExchanges);
    int mismatched = 0;
    for (std::size_t i = 0; i < kExchanges; ++i) {
      mismatched += recorder->replies()[i].frame == recorder->requests()[i].frame &&
                            recorder->replies()[i].elevator == shm_test::kHoldElevator
                        ? 0
                        : 1;
    }
    EXPECT_EQ(mismatched, 0) << "every reply echoes its request frame";
  }
  EXPECT_EQ(peer.wait_exit(kExitWait), 0);
  EXPECT_NE(peer.output().find(summary(kExchanges, ASTRA_HAL_CLOSED)), std::string::npos)
      << "the peer's own, independent count";
}

TEST_F(EchoPeer, FailureCloseDoesNotReportCleanExit) {
  constexpr std::uint32_t kPeerMs = 800;
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  spec.peer_timeout_ms = kPeerMs;
  spec.limits =
      "{ elevator: [-0.005, 0.005], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], throttle: [0.0, "
      "1.0] }";  // the hold elevator 0.01 violates it
  shm_test::Child peer(shm_test::peer_args(spec.segment, 5000, kPeerMs, shm_test::kHold), false);
  auto sim = std::make_unique<astra::Simulation>(shm_test::build_sim(spec));
  const astra::HaltReason halt = sim->run();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(sim->transport_fault(), TransportFault::kCommandLimit);
  const auto closed = shm_test::Clock::now();
  sim.reset();  // close(kAfterFailure): nothing on the wire
  const std::optional<int> status = peer.wait_exit(kExitWait);
  EXPECT_EQ(status, 2) << "the peer times out (exit 2); CLOSED is reserved for normal completion";
  EXPECT_GE(shm_test::elapsed_ms(closed), 0.5 * kPeerMs) << "not released by a close";
}

}  // namespace
