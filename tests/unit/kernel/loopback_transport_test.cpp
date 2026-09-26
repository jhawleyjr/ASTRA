// plugins/sim/transport/loopback on its own (M4 PR 3, docs/plans/m4-pr3-core-seam.md): loaded by
// path like any plugin and driven through the Transport interface directly, no Simulation --
// the scripted responder the SimulationTransport suite relies on, checked here first.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "icd/astra_icd.h"

namespace {

using astra::AckStatus;

constexpr std::uint32_t kSentinel = std::numeric_limits<std::uint32_t>::max();

struct Loaded {
  astra::LoadedPlugin plugin;
  astra::Transport* transport = nullptr;
};

Loaded load(std::string_view transport_yaml) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  Loaded loaded{loader.load("plugins/sim/transport/loopback"), nullptr};
  loaded.transport = dynamic_cast<astra::Transport*>(loaded.plugin.model.get());
  if (loaded.transport != nullptr) {
    astra::SignalRegistry registry;
    loaded.transport->init(astra::Config::from_string(transport_yaml, "loopback test"), registry);
  }
  return loaded;
}

astra_sim_to_sw_v1 request_at(std::uint64_t frame) {
  astra_sim_to_sw_v1 request{};
  request.magic = ASTRA_ICD_MAGIC;
  request.version = ASTRA_ICD_VERSION;
  request.frame = frame;
  return request;
}

TEST(LoopbackTransport, IsATransportSpeakingThisIcdVersionAndStartsAtTheSentinel) {
  Loaded loaded = load("{}\n");
  ASSERT_NE(loaded.transport, nullptr);
  EXPECT_EQ(loaded.transport->icd_version(), ASTRA_ICD_VERSION);
  EXPECT_EQ(loaded.transport->open(astra::TransportSettings{}), kSentinel);
}

TEST(LoopbackTransport, EchoesTheFrameWithItsCommandsInAZeroedReply) {
  Loaded loaded = load(
      "peer: { commands: { elevator: 0.1, aileron: -0.2, rudder: 0.3, throttle: [0.4, 0.4] }, "
      "mode_flags: 9 }\n");
  ASSERT_NE(loaded.transport, nullptr);
  static_cast<void>(loaded.transport->open(astra::TransportSettings{}));
  loaded.transport->publish(request_at(40), 40);
  const astra::AckObservation ack = loaded.transport->wait_ack(kSentinel);
  EXPECT_EQ(ack.status, AckStatus::kObserved);
  EXPECT_EQ(ack.ack_seq, 40U);
  astra_sw_to_sim_v1 reply;
  std::memset(&reply, 0xFF, sizeof reply);
  loaded.transport->read_reply(reply);
  EXPECT_EQ(reply.magic, ASTRA_ICD_MAGIC);
  EXPECT_EQ(reply.version, ASTRA_ICD_VERSION);
  EXPECT_EQ(reply.frame, 40U);
  EXPECT_EQ(reply.elevator, 0.1);
  EXPECT_EQ(reply.aileron, -0.2);
  EXPECT_EQ(reply.rudder, 0.3);
  EXPECT_EQ(reply.throttle[0], 0.4);
  EXPECT_EQ(reply.throttle[1], 0.4);
  EXPECT_EQ(reply.mode_flags, 9U);
  EXPECT_EQ(reply.fault_flags, 0U);
}

TEST(LoopbackTransport, SpuriousWakeupsReturnTheUnchangedValueBeforeTheAck) {
  Loaded loaded = load("peer: { spurious_wakeups: 3 }\n");
  ASSERT_NE(loaded.transport, nullptr);
  static_cast<void>(loaded.transport->open(astra::TransportSettings{}));
  for (const std::uint64_t frame : {0U, 10U}) {
    const std::uint32_t unchanged = frame == 0 ? kSentinel : 0U;
    loaded.transport->publish(request_at(frame), static_cast<std::uint32_t>(frame));
    for (int i = 0; i < 3; ++i) {
      const astra::AckObservation ack = loaded.transport->wait_ack(unchanged);
      EXPECT_EQ(ack.status, AckStatus::kObserved);
      EXPECT_EQ(ack.ack_seq, unchanged);
    }
    EXPECT_EQ(loaded.transport->wait_ack(unchanged).ack_seq, frame);
  }
}

TEST(LoopbackTransport, ScriptedFaultsShapeTheObservationOrThePayload) {
  Loaded loaded = load(
      "peer:\n  faults:\n"
      "    - { frame: 10, kind: timeout }\n"
      "    - { frame: 20, kind: peer_closed }\n"
      "    - { frame: 30, kind: out_of_order_ack }\n"
      "    - { frame: 40, kind: bad_magic }\n"
      "    - { frame: 50, kind: non_finite, field: throttle_1, value: -inf }\n");
  ASSERT_NE(loaded.transport, nullptr);
  static_cast<void>(loaded.transport->open(astra::TransportSettings{}));
  loaded.transport->publish(request_at(10), 10);
  EXPECT_EQ(loaded.transport->wait_ack(kSentinel).status, AckStatus::kExpired);
  loaded.transport->publish(request_at(20), 20);
  EXPECT_EQ(loaded.transport->wait_ack(kSentinel).status, AckStatus::kPeerClosed);
  loaded.transport->publish(request_at(30), 30);
  EXPECT_EQ(loaded.transport->wait_ack(kSentinel).ack_seq, 31U);
  astra_sw_to_sim_v1 reply{};
  loaded.transport->publish(request_at(40), 40);
  static_cast<void>(loaded.transport->wait_ack(31));
  loaded.transport->read_reply(reply);
  EXPECT_NE(reply.magic, ASTRA_ICD_MAGIC);
  loaded.transport->publish(request_at(50), 50);
  static_cast<void>(loaded.transport->wait_ack(40));
  loaded.transport->read_reply(reply);
  EXPECT_TRUE(std::isinf(reply.throttle[1]) && reply.throttle[1] < 0.0);
  EXPECT_EQ(reply.throttle[0], 0.0);
}

TEST(LoopbackTransport, ScheduleChangesTakeEffectFromTheirFrame) {
  Loaded loaded = load(
      "peer: { commands: { elevator: 0.1 }, schedule: [ { frame: 15, elevator: "
      "0.2 } ] }\n");
  ASSERT_NE(loaded.transport, nullptr);
  static_cast<void>(loaded.transport->open(astra::TransportSettings{}));
  astra_sw_to_sim_v1 reply{};
  loaded.transport->publish(request_at(10), 10);
  loaded.transport->read_reply(reply);
  EXPECT_EQ(reply.elevator, 0.1);
  loaded.transport->publish(request_at(20), 20);
  loaded.transport->read_reply(reply);
  EXPECT_EQ(reply.elevator, 0.2) << "a change dated between exchanges arrives at the next one";
}

TEST(LoopbackTransport, AScriptedHandshakeFailureThrowsFromOpen) {
  Loaded loaded = load("peer: { handshake_fail: true }\n");
  ASSERT_NE(loaded.transport, nullptr);
  EXPECT_ANY_THROW(static_cast<void>(loaded.transport->open(astra::TransportSettings{})));
}

TEST(LoopbackTransport, MalformedPeerConfigIsAConfigErrorNamingTheKey) {
  const auto path_of = [](std::string_view yaml) -> std::string {
    try {
      static_cast<void>(load(yaml));
    } catch (const astra::ConfigError& error) {
      return error.config_path();
    }
    return "(no error)";
  };
  EXPECT_EQ(path_of("peer: { faults: [ { frame: 1, kind: explode } ] }\n"), "peer.faults.0.kind");
  EXPECT_EQ(path_of("peer: { faults: [ { frame: 1, kind: non_finite, field: flaps, value: nan } ] "
                    "}\n"),
            "peer.faults.0.field");
  EXPECT_EQ(path_of("peer: { schedule: [ { frame: 5 }, { frame: 5 } ] }\n"),
            "peer.schedule.1.frame");
  EXPECT_EQ(path_of("peer: { spurious_wakeups: -1 }\n"), "peer.spurious_wakeups");
}

}  // namespace
