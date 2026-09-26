// M4 PR 4 (docs/plans/m4-pr4-shm-transport.md): the sim-side shm transport plugin, driven directly
// the way LockstepExchange drives it, against a raw test peer built on the layout header -- and
// through Simulation where core's classification matters. The timing-bound tests are in
// timing_test.cpp (RUN_SERIAL).
#include "plugins/sim/transport/shm/shm_transport.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "astra/errors.hpp"
#include "astra/kernel/lockstep_exchange.hpp"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using astra::AckStatus;
using astra::CloseReason;
using astra::HealthCheck;
using astra::TransportFault;
using shm_test::milliseconds;

class ShmTransport : public shm_test::LockstepTest {};

bool segment_exists(const std::string& segment) {
  return std::filesystem::exists("/dev/shm/" + segment);
}

// Any open descriptor of this process that refers to the segment (live or unlinked).
bool fd_open_on(const std::string& segment) {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", error)) {
    std::error_code link_error;
    const std::string target = std::filesystem::read_symlink(entry.path(), link_error).string();
    if (target.find(segment) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool mapped(const std::string& segment) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    if (line.find(segment) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------------------------
// Handshake, publish, reply.

TEST_F(ShmTransport, HandshakeReturnsSentinelInitializesHeaderAndUnlinksName) {
  const auto opened = shm_test::open_with_raw_peer();
  EXPECT_EQ(opened->initial_ack, ASTRA_SHM_SEQ_INIT);
  const astra_shm_header_v1& header = opened->peer->header();
  EXPECT_EQ(header.magic, ASTRA_SHM_MAGIC);
  EXPECT_EQ(header.layout_version, ASTRA_SHM_LAYOUT_VERSION);
  EXPECT_EQ(header.layout_size, sizeof(astra_shm_region_v1));
  EXPECT_EQ(header.icd_version, ASTRA_ICD_VERSION);
  EXPECT_EQ(shm_test::load(header.sim_state), ASTRA_SHM_SIM_READY);
  EXPECT_EQ(shm_test::load(header.req_seq), ASTRA_SHM_SEQ_INIT);
  EXPECT_EQ(header.sim_pid, static_cast<std::int32_t>(getpid()));
  EXPECT_FALSE(segment_exists(opened->segment))
      << "the name is unlinked once the handshake completes (plan Q-6)";
}

TEST_F(ShmTransport, PublishStoresPayloadThenSequence) {
  const auto opened = shm_test::open_with_raw_peer();
  astra_sim_to_sw_v1 request = shm_test::request_for(10);
  request.phi = 0.25;
  opened->shm->transport->publish(request, 10);
  EXPECT_EQ(shm_test::load(opened->peer->header().req_seq), 10U);
  EXPECT_TRUE(shm_test::same_bytes(opened->peer->region().request, request));
}

TEST_F(ShmTransport, EchoedAckIsObservedAndReplyCopiedWhole) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  astra_sw_to_sim_v1 reply{};
  reply.magic = ASTRA_ICD_MAGIC;
  reply.version = ASTRA_ICD_VERSION;
  reply.elevator = 0.125;
  reply.throttle[1] = 0.75;
  reply.mode_flags = 7;
  reply.fault_flags = 9;
  std::memcpy(&opened->peer->region().reply, &reply, sizeof reply);
  shm_test::store(opened->peer->header().ack_seq, 0);
  shm_test::wake(opened->peer->header().ack_seq);

  const shm_test::Outcome outcome =
      shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
  EXPECT_EQ(outcome.status, AckStatus::kObserved);
  EXPECT_EQ(outcome.ack, 0U);
  astra_sw_to_sim_v1 copied{};
  opened->shm->transport->read_reply(copied);
  EXPECT_TRUE(shm_test::same_bytes(copied, reply)) << "all 64 bytes";
}

// ---------------------------------------------------------------------------------------------
// Reported, never classified: the plugin returns what it loaded; core decides.

TEST_F(ShmTransport, UnchangedAckIsReportedNotClassified) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  std::atomic<bool> done{false};
  std::jthread waker([&] {
    while (!done) {
      shm_test::wake(opened->peer->header().ack_seq);  // no store
      std::this_thread::sleep_for(milliseconds{1});
    }
  });
  const astra::AckObservation observed = opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT);
  done = true;
  EXPECT_EQ(observed.status, AckStatus::kObserved) << "a wake with no store is not expiry";
  EXPECT_EQ(observed.ack_seq, ASTRA_SHM_SEQ_INIT) << "reported unchanged, for core to retry";
}

TEST_F(ShmTransport, OutOfOrderAckIsReportedVerbatim) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(30), 30);
  shm_test::store(opened->peer->header().ack_seq, 37);
  shm_test::wake(opened->peer->header().ack_seq);
  const astra::AckObservation observed = opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT);
  EXPECT_EQ(observed.status, AckStatus::kObserved);
  EXPECT_EQ(observed.ack_seq, 37U);
}

TEST_F(ShmTransport, OutOfOrderAckHaltsThroughCore) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  const shm_test::ScriptedPeer peer(
      spec.segment, [](shm_test::Mapping& raw, std::uint32_t seq, const std::atomic<bool>& stop) {
        if (seq == 20) {
          shm_test::store(raw.header().ack_seq, seq + 1);
          shm_test::wake(raw.header().ack_seq);
          return;
        }
        shm_test::reply_correctly(raw, seq, stop);
      });
  astra::Simulation sim = shm_test::build_sim(spec);
  const astra::HaltReason halt = sim.run();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, 20U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kOutOfOrderAck);
}

TEST_F(ShmTransport, BadReplyMagicAndStaleReplyFrameHaltThroughCore) {
  struct Case {
    const char* name;
    bool bad_magic;
    TransportFault fault;
  };
  for (const Case& c : {Case{"bad magic", true, TransportFault::kBadMagic},
                        Case{"stale reply frame", false, TransportFault::kFrameMismatch}}) {
    SCOPED_TRACE(c.name);
    shm_test::SimSpec spec;
    spec.segment = shm_test::unique_segment();
    const shm_test::ScriptedPeer peer(spec.segment, [&c](shm_test::Mapping& raw, std::uint32_t seq,
                                                         const std::atomic<bool>& stop) {
      if (seq != 30) {
        shm_test::reply_correctly(raw, seq, stop);
        return;
      }
      // The faulty payload first, then the (correct) ack that publishes it.
      astra_sw_to_sim_v1 reply{};
      reply.magic = c.bad_magic ? (ASTRA_ICD_MAGIC ^ 1U) : ASTRA_ICD_MAGIC;
      reply.version = ASTRA_ICD_VERSION;
      reply.frame = c.bad_magic ? 30U : 20U;  // 20: the previous exchange's frame, stale
      std::memcpy(&raw.region().reply, &reply, sizeof reply);
      shm_test::store(raw.header().ack_seq, seq);
      shm_test::wake(raw.header().ack_seq);
    });
    astra::Simulation sim = shm_test::build_sim(spec);
    const astra::HaltReason halt = sim.run();
    EXPECT_EQ(halt.check, HealthCheck::kTransport);
    EXPECT_EQ(halt.frame, 30U);
    EXPECT_EQ(sim.transport_fault(), c.fault);
  }
}

// ---------------------------------------------------------------------------------------------
// The deadline (critique F-1, F-2): expiry is established by the transport's own clock comparison
// and is latched; an ack first observed at or after the deadline is expiry, never a result.

TEST_F(ShmTransport, AckObservedAfterDeadlineIsExpiry) {
  {
    SCOPED_TRACE("stored before the deadline, first observed after it (the waiter was late)");
    const auto opened = shm_test::open_with_raw_peer();
    opened->shm->transport->publish(shm_test::request_for(0), 0);
    std::atomic<bool> armed{false};
    std::jthread peer([&] {
      while (!armed) {  // wake with no store until the transport's first wait has returned
        shm_test::wake(opened->peer->header().ack_seq);
        std::this_thread::sleep_for(milliseconds{1});
      }
      shm_test::store(opened->peer->header().ack_seq, 0);  // well before the 500 ms deadline
    });
    const astra::AckObservation first = opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT);
    ASSERT_EQ(first.status, AckStatus::kObserved);
    ASSERT_EQ(first.ack_seq, ASTRA_SHM_SEQ_INIT);
    armed = true;
    peer.join();
    std::this_thread::sleep_for(shm_test::kTimeout + milliseconds{200});  // descheduled past it
    const astra::AckObservation late = opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT);
    EXPECT_EQ(late.status, AckStatus::kExpired);
    EXPECT_EQ(late.ack_seq, 0U) << "the ack was there, but only observed after the deadline";
  }
  {
    SCOPED_TRACE("stored after the deadline: the latch holds");
    const auto opened = shm_test::open_with_raw_peer();
    opened->shm->transport->publish(shm_test::request_for(0), 0);
    const shm_test::Outcome first =
        shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
    ASSERT_EQ(first.status, AckStatus::kExpired);
    shm_test::store(opened->peer->header().ack_seq, 0);
    shm_test::wake(opened->peer->header().ack_seq);
    const astra::AckObservation again = opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT);
    EXPECT_EQ(again.status, AckStatus::kExpired);
  }
}

TEST_F(ShmTransport, LateAckHaltsThroughCoreAsTimeout) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  const shm_test::ScriptedPeer peer(
      spec.segment, [](shm_test::Mapping& raw, std::uint32_t seq, const std::atomic<bool>& stop) {
        if (seq == 20) {
          std::this_thread::sleep_for(shm_test::kTimeout + milliseconds{200});
        }
        shm_test::reply_correctly(raw, seq, stop);
      });
  astra::Simulation sim = shm_test::build_sim(spec);
  const astra::HaltReason halt = sim.run();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, 20U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kTimeout);
  EXPECT_EQ(sim.clock().frame(), 20U);
}

TEST_F(ShmTransport, ExpiredDeadlineStaysExpiredUnderContinuousWakes) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  std::atomic<bool> done{false};
  std::jthread waker([&] {
    while (!done) {
      shm_test::wake(opened->peer->header().ack_seq);  // a tight loop, never a store
    }
  });
  const shm_test::Outcome outcome =
      shm_test::drive_wait(*opened->shm->transport, ASTRA_SHM_SEQ_INIT);
  EXPECT_FALSE(outcome.runaway) << "core's loop must end at the deadline despite the wakes";
  EXPECT_EQ(outcome.status, AckStatus::kExpired);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(opened->shm->transport->wait_ack(ASTRA_SHM_SEQ_INIT).status, AckStatus::kExpired);
  }
  done = true;
}

// The deterministic form of critique F-1 (the wake tests above cannot force the race): the kernel
// returns ahead of ETIMEDOUT (here EAGAIN, the value compare) on an already-past deadline, and only
// the explicit clock comparison can report expiry. shm_wait.cpp is compiled into this test.
TEST_F(ShmTransport, WaitUntilLatchesExpiryWhenTheKernelReturnsEarly) {
  using Plugin = astra::shm_transport::ShmTransport;
  std::uint32_t word = 7;
  Plugin::Deadline deadline;
  deadline.timeout_ms = 1;
  const Plugin::WaitResult arming = Plugin::wait_until(&word, 7, deadline);  // arms, then times out
  ASSERT_TRUE(deadline.armed);
  EXPECT_EQ(arming.kind, Plugin::WaitKind::kExpired);

  Plugin::Deadline past;
  past.timeout_ms = 1;
  past.armed = true;
  past.abs = deadline.abs;  // already behind us, not yet latched
  const Plugin::WaitResult early = Plugin::wait_until(&word, 8, past);  // 7 != 8: EAGAIN at once
  EXPECT_EQ(early.kind, Plugin::WaitKind::kExpired) << "the clock, not ETIMEDOUT, decides";
  EXPECT_EQ(early.value, 7U);
  EXPECT_TRUE(past.expired) << "latched";
  EXPECT_EQ(Plugin::wait_until(&word, 8, past).kind, Plugin::WaitKind::kExpired);
}

TEST_F(ShmTransport, ContinuousWakesHaltThroughCoreAsTimeout) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  const shm_test::ScriptedPeer peer(
      spec.segment, [](shm_test::Mapping& raw, std::uint32_t seq, const std::atomic<bool>& stop) {
        if (seq == 20) {
          while (!stop) {
            shm_test::wake(raw.header().ack_seq);
          }
          return;
        }
        shm_test::reply_correctly(raw, seq, stop);
      });
  astra::Simulation sim = shm_test::build_sim(spec);
  const astra::HaltReason halt = sim.run();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, 20U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kTimeout);
}

// ---------------------------------------------------------------------------------------------
// open() failure paths release everything (critique F-3): core never calls close() after them.

TEST_F(ShmTransport, OpenFailureReleasesDescriptorAndName) {
  const std::string segment = shm_test::unique_segment();
  {
    const auto shm = shm_test::load_shm(segment);
    rlimit saved{};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &saved), 0);
    struct sigaction ignore {};
    struct sigaction previous {};
    ignore.sa_handler = SIG_IGN;
    ASSERT_EQ(sigaction(SIGXFSZ, &ignore, &previous), 0);
    const rlimit tiny{64, saved.rlim_max};  // below the 328-byte layout: ftruncate fails (EFBIG)
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &tiny), 0);
    std::string message;
    try {
      static_cast<void>(shm->transport->open(shm_test::settings()));
    } catch (const std::runtime_error& error) {
      message = error.what();
    }
    setrlimit(RLIMIT_FSIZE, &saved);
    sigaction(SIGXFSZ, &previous, nullptr);
    EXPECT_NE(message.find("ftruncate"), std::string::npos) << message;
    EXPECT_FALSE(fd_open_on(segment)) << "the descriptor is closed";
    EXPECT_FALSE(mapped(segment));
    EXPECT_FALSE(segment_exists(segment)) << "the owned name is unlinked";
  }
  // The name is free again: a fresh transport creates and handshakes on it.
  const auto shm = shm_test::load_shm(segment);
  std::unique_ptr<shm_test::Mapping> peer;
  {
    const std::jthread helper(
        [&] { peer = shm_test::raw_peer_handshake(segment, milliseconds{5000}); });
    EXPECT_EQ(shm->transport->open(shm_test::settings()), ASTRA_SHM_SEQ_INIT);
  }
  EXPECT_NE(peer, nullptr);
}

TEST_F(ShmTransport, HandshakeFailureReleasesMappingDescriptorAndName) {
  const std::string segment = shm_test::unique_segment();
  const auto shm = shm_test::load_shm(segment);
  constexpr std::uint32_t kShortHandshakeMs = 200;
  std::string message;
  try {
    static_cast<void>(
        shm->transport->open(shm_test::settings(shm_test::kTimeoutMs, kShortHandshakeMs)));
  } catch (const std::runtime_error& error) {
    message = error.what();
  }
  EXPECT_NE(message.find("PEER_READY"), std::string::npos) << message;
  EXPECT_FALSE(fd_open_on(segment));
  EXPECT_FALSE(mapped(segment)) << "the mapping is released";
  EXPECT_FALSE(segment_exists(segment));
}

// ---------------------------------------------------------------------------------------------
// close(): ADR-0024's normal sequence, and nothing on the wire after a failure (plan Q-3).

TEST_F(ShmTransport, CloseNormalWritesClosedThenCloseSeqAndIsIdempotent) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->close(CloseReason::kNormal);
  EXPECT_EQ(shm_test::load(opened->peer->header().req_seq), ASTRA_SHM_CLOSE_SEQ);
  EXPECT_EQ(shm_test::load(opened->peer->header().sim_state), ASTRA_SHM_SIM_CLOSED);
  shm_test::store(opened->peer->header().sim_state, ASTRA_SHM_SIM_READY);  // test-side scribble
  opened->shm->transport->close(CloseReason::kNormal);  // idempotent: writes nothing the 2nd time
  EXPECT_EQ(shm_test::load(opened->peer->header().sim_state), ASTRA_SHM_SIM_READY);
}

TEST_F(ShmTransport, CloseAfterFailureWritesNothing) {
  const auto opened = shm_test::open_with_raw_peer();
  opened->shm->transport->publish(shm_test::request_for(0), 0);
  opened->shm->transport->close(CloseReason::kAfterFailure);
  EXPECT_EQ(shm_test::load(opened->peer->header().sim_state), ASTRA_SHM_SIM_READY);
  EXPECT_EQ(shm_test::load(opened->peer->header().req_seq), 0U) << "no CLOSE_SEQ";
}

// ---------------------------------------------------------------------------------------------
// Config and lifecycle errors (plan Q-5; ADR-0024).

TEST_F(ShmTransport, SegmentNameMustMatchPattern) {
  shm_test::SimSpec missing;
  missing.with_segment_key = false;
  try {
    static_cast<void>(shm_test::build_sim(missing));
    ADD_FAILURE() << "a missing transport.segment must be a ConfigError";
  } catch (const astra::ConfigError& error) {
    EXPECT_NE(std::string{error.what()}.find("transport.segment"), std::string::npos)
        << error.what();
  }
  for (const char* bad :
       {"astra_x_1", "astra_12_", "astra__n1", "other_12_n1", "astra_12345678901_n", "astra_12_n/1",
        "astra_12_n-1", "astra_12_abcdefghijklmnopqrstuvwxyzABCDEFG"}) {
    SCOPED_TRACE(bad);
    shm_test::SimSpec spec;
    spec.segment = bad;
    try {
      static_cast<void>(shm_test::build_sim(spec));
      ADD_FAILURE() << "accepted a malformed segment name";
    } catch (const astra::ConfigError& error) {
      EXPECT_NE(std::string{error.what()}.find("transport.segment"), std::string::npos)
          << error.what();
    }
  }
}

TEST_F(ShmTransport, StaleSegmentIsAnInitErrorAndIsLeftAlone) {
  shm_test::SimSpec spec;
  spec.segment = shm_test::unique_segment();
  const shm_test::Creator stale(spec.segment);  // exists, never sized: a crashed creator's leftover
  try {
    static_cast<void>(shm_test::build_sim(spec));
    ADD_FAILURE() << "an existing segment must be an init error";
  } catch (const astra::ConfigError& error) {
    const std::string what = error.what();
    EXPECT_NE(what.find("transport"), std::string::npos) << what;
    EXPECT_NE(what.find("already exists"), std::string::npos) << what;
  }
  EXPECT_TRUE(segment_exists(spec.segment)) << "no unlink-on-next-start (ADR-0024)";
}

}  // namespace
