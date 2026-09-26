// M4 PR 3 (docs/plans/m4-pr3-core-seam.md; ADR-0022): the lockstep exchange stage driven by the
// loopback transport through Simulation::from_config -- R5 (every failure halts kTransport,
// spurious wakeups keep waiting), R7 (zero-order hold, same-frame commit), R8 (no transport block
// changes nothing), the recorder, and the critique additions F-1 to F-6 plus Joe's all-halts-sticky
// rule.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/lockstep_exchange.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/air_data.hpp"
#include "icd/astra_icd.h"

namespace {

using astra::CloseReason;
using astra::HaltReason;
using astra::HealthCheck;
using astra::TransportFault;

constexpr std::string_view kLoopback = "plugins/sim/transport/loopback";
constexpr double kElevatorA = 0.01;
constexpr double kElevatorB = 0.02;
constexpr double kElevatorC = 0.03;

// A flat-Earth fixture (no_alloc_write_seam.yaml's own in-domain state) plus a loopback transport.
struct Spec {
  std::uint64_t frames = 100;
  std::uint32_t divisor = 10;
  std::string sim_extra;  // lines under sim:, each "  key: ..."
  std::string models = "    - { plugin: tests/plugins/input_consumer }\n";
  std::string commands = "{ elevator: controls.elevator }";
  std::string limits =
      "{ elevator: [-1.0, 1.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], throttle: [0.0, 1.0] }";
  std::string transport_extra;  // lines under transport:, each "  key: ..."
  std::string peer;             // lines under transport.peer:, each "    key: ..."
  bool transport = true;
};

std::string to_yaml(const Spec& spec) {
  std::ostringstream out;
  out << "sim:\n  dt: 0.001\n  frames: " << spec.frames << "\n"
      << spec.sim_extra << "vehicle:\n  models:\n"
      << spec.models
      << "  initial_controls:\n    controls:\n      elevator: 0.0\n"
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
         "izz: 1.0 }\n"
         "initial:\n  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n";
  if (spec.transport) {
    out << "transport:\n  plugin: " << kLoopback << "\n  divisor: " << spec.divisor
        << "\n  commands: " << spec.commands << "\n  limits: " << spec.limits << "\n"
        << spec.transport_extra;
    if (!spec.peer.empty()) {
      out << "  peer:\n" << spec.peer;
    }
  }
  return out.str();
}

astra::Simulation build(const Spec& spec) {
  return astra::Simulation::from_config(astra::Config::from_string(to_yaml(spec), "transport test"),
                                        ASTRA_TEST_PLUGIN_DIR);
}

double signal(const astra::Simulation& sim, std::string_view name) {
  const double* value = sim.registry().find(name);
  EXPECT_NE(value, nullptr) << name;
  return value == nullptr ? std::numeric_limits<double>::quiet_NaN() : *value;
}

bool same_halt(const HaltReason& a, const HaltReason& b) {
  return a.check == b.check && a.source == b.source && a.source.data() == b.source.data() &&
         a.frame == b.frame && a.vehicle_index == b.vehicle_index;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// aircraft/notional_transport/configs/cruise.yaml verbatim (Dryden, isa1976, wgs84_rotating),
// optionally with a loopback transport appended whose peer replies with exactly the file's own
// initial_controls literals. Used where a changing environment matters (F-4) and for R8.
constexpr std::string_view kCruiseTrimTransport =
    "transport:\n"
    "  plugin: plugins/sim/transport/loopback\n"
    "  divisor: DIVISOR\n"
    "  commands: { elevator: controls.elevator, aileron: controls.aileron, rudder: "
    "controls.rudder, throttle: [controls.throttle, controls.throttle] }\n"
    "  limits: { elevator: [-0.5, 0.5], aileron: [-0.5, 0.5], rudder: [-0.5, 0.5], throttle: "
    "[0.0, 1.0] }\n"
    "  peer:\n"
    "    commands: { elevator: 0.026674474137989946, aileron: 0.0, rudder: 0.0, throttle: "
    "[0.4985979490507746, 0.4985979490507746] }\n";

astra::Simulation build_cruise(bool with_transport, std::string_view divisor = "10") {
  std::string text =
      read_file(std::string{ASTRA_AIRCRAFT_DIR} + "/notional_transport/configs/cruise.yaml");
  if (with_transport) {
    std::string block{kCruiseTrimTransport};
    block.replace(block.find("DIVISOR"), std::string_view{"DIVISOR"}.size(), divisor);
    text += "\n" + block;
  }
  return astra::Simulation::from_config(astra::Config::from_string(text, "cruise + transport"),
                                        ASTRA_TEST_PLUGIN_DIR);
}

// ---------------------------------------------------------------------------------------------
// Nominal exchange, counters, recorder.

TEST(SimulationTransport, LoopbackExchangesAtEveryDivisorFrameAndCountsThem) {
  Spec spec;
  spec.peer = "    commands: { elevator: 0.01 }\n    mode_flags: 7\n";
  astra::Simulation sim = build(spec);
  ASSERT_FALSE(sim.run());
  EXPECT_EQ(sim.clock().frame(), 100U);
  EXPECT_EQ(signal(sim, "transport.exchanges"), 10.0);
  EXPECT_EQ(signal(sim, "transport.missed"), 0.0);
  EXPECT_EQ(signal(sim, "transport.fault"), 0.0);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kNone);
  EXPECT_EQ(sim.transport_close_reason(), CloseReason::kNormal);

  const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
  ASSERT_NE(recorder, nullptr);
  ASSERT_EQ(recorder->requests().size(), 10U);
  ASSERT_EQ(recorder->replies().size(), 10U);
  ASSERT_EQ(recorder->states().size(), 101U);
  for (std::size_t i = 0; i < 10; ++i) {
    const astra_sim_to_sw_v1& request = recorder->requests()[i];
    const astra_sw_to_sim_v1& reply = recorder->replies()[i];
    EXPECT_EQ(request.frame, i * 10);
    EXPECT_EQ(request.magic, ASTRA_ICD_MAGIC);
    EXPECT_EQ(request.reserved, 0U);
    EXPECT_EQ(request.t, static_cast<double>(i * 10) * 0.001);
    EXPECT_EQ(reply.frame, request.frame);
    EXPECT_EQ(reply.elevator, kElevatorA);
    EXPECT_EQ(reply.mode_flags, 7U);
    EXPECT_EQ(reply.fault_flags, 0U);
  }
  for (std::size_t i = 0; i < recorder->states().size(); ++i) {
    EXPECT_EQ(recorder->states()[i].frame, i);
  }
  // The final state record is the vehicle's state now.
  const std::span<const double> now = sim.vehicles()[0].combined_state();
  for (std::size_t j = 0; j < 13; ++j) {
    EXPECT_EQ(recorder->states().back().y[j], now[j]);
  }
}

TEST(SimulationTransport, NoTransportBlockRegistersNoTransportSignals) {
  Spec spec;
  spec.transport = false;
  const astra::Simulation sim = build(spec);
  EXPECT_EQ(sim.registry().find("transport.exchanges"), nullptr);
  EXPECT_EQ(sim.registry().find("transport.missed"), nullptr);
  EXPECT_EQ(sim.registry().find("transport.fault"), nullptr);
  EXPECT_EQ(sim.exchange_recorder(), nullptr);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kNone);
}

TEST(SimulationTransport, RecorderSerializesAFixedHeaderThenTheStreams) {
  Spec spec;
  spec.frames = 30;
  astra::Simulation sim = build(spec);
  ASSERT_FALSE(sim.run());
  const std::vector<std::byte> bytes = sim.exchange_recorder()->serialize();
  astra::ExchangeRecorder::Header header{};
  ASSERT_GE(bytes.size(), sizeof header);
  std::memcpy(&header, bytes.data(), sizeof header);
  EXPECT_EQ(header.magic, astra::ExchangeRecorder::kMagic);
  EXPECT_EQ(header.version, astra::ExchangeRecorder::kVersion);
  EXPECT_EQ(header.request_size, 200U);
  EXPECT_EQ(header.reply_size, 64U);
  EXPECT_EQ(header.state_size, 112U);
  EXPECT_EQ(header.reserved, 0U);
  EXPECT_EQ(header.request_count, 3U);
  EXPECT_EQ(header.reply_count, 3U);
  EXPECT_EQ(header.state_count, 31U);
  EXPECT_EQ(bytes.size(), 48U + (3 * 200U) + (3 * 64U) + (31 * 112U));
}

TEST(SimulationTransport, RecorderBytesAreIdenticalAcrossTwoRunsAndDifferWithTheSeed) {
  const auto dump = [](std::string_view seed) {
    std::string text =
        read_file(std::string{ASTRA_AIRCRAFT_DIR} + "/notional_transport/configs/cruise.yaml");
    const std::size_t at = text.find("seed: 42");
    EXPECT_NE(at, std::string::npos);
    text.replace(at, std::string_view{"seed: 42"}.size(), "seed: " + std::string{seed});
    std::string block{kCruiseTrimTransport};
    block.replace(block.find("DIVISOR"), std::string_view{"DIVISOR"}.size(), "10");
    text += "\n" + block;
    astra::Simulation sim = astra::Simulation::from_config(
        astra::Config::from_string(text, "cruise + transport"), ASTRA_TEST_PLUGIN_DIR);
    EXPECT_FALSE(sim.run(200));
    return sim.exchange_recorder()->serialize();
  };
  const std::vector<std::byte> first = dump("42");
  const std::vector<std::byte> second = dump("42");
  const std::vector<std::byte> other = dump("43");
  EXPECT_EQ(first, second);
  EXPECT_NE(first, other) << "a different Dryden seed must change the bytes (the check is live)";
}

// ---------------------------------------------------------------------------------------------
// R7: zero-order hold and same-frame commit.

TEST(SimulationTransport, ZeroOrderHoldHoldsBetweenExchangesAndCommitsOnTheArrivalFrame) {
  Spec spec;
  spec.peer =
      "    commands: { elevator: 0.01 }\n"
      "    schedule:\n"
      "      - { frame: 10, elevator: 0.02 }\n"
      "      - { frame: 20, elevator: 0.03 }\n";
  astra::Simulation sim = build(spec);
  for (std::uint64_t frame = 0; frame < 30; ++frame) {
    ASSERT_FALSE(sim.step()) << frame;
    double expected = kElevatorC;
    if (frame < 10) {
      expected = kElevatorA;
    } else if (frame < 20) {
      expected = kElevatorB;
    }
    SCOPED_TRACE(frame);
    EXPECT_EQ(signal(sim, "v0.controls.elevator"), expected) << "latched value";
    // What the wrench source itself read during this frame's integration (k1 and k4): the command
    // that arrived this frame is already in force, with no one-frame lag.
    EXPECT_EQ(signal(sim, "v0.input_consumer.k1"), expected);
    EXPECT_EQ(signal(sim, "v0.input_consumer.k4"), expected);
  }
}

// ---------------------------------------------------------------------------------------------
// R5: every failure halts kTransport; unchanged acks keep waiting.

TEST(SimulationTransport, EachProtocolFaultHaltsNamingThePluginAtItsFrame) {
  struct Case {
    std::string_view kind;
    TransportFault fault;
  };
  constexpr std::array<Case, 6> kCases = {{
      {"out_of_order_ack", TransportFault::kOutOfOrderAck},
      {"stale_reply_frame", TransportFault::kFrameMismatch},
      {"bad_magic", TransportFault::kBadMagic},
      {"bad_version", TransportFault::kBadVersion},
      {"timeout", TransportFault::kTimeout},
      {"peer_closed", TransportFault::kPeerClosed},
  }};
  for (const Case& c : kCases) {
    SCOPED_TRACE(std::string{c.kind});
    Spec spec;
    spec.peer =
        "    commands: { elevator: 0.01 }\n"
        "    schedule: [ { frame: 30, elevator: 0.02 } ]\n"
        "    faults: [ { frame: 30, kind: " +
        std::string{c.kind} + " } ]\n";
    astra::Simulation sim = build(spec);
    const HaltReason halt = sim.run();
    EXPECT_EQ(halt.check, HealthCheck::kTransport);
    EXPECT_EQ(halt.source, kLoopback);
    EXPECT_EQ(halt.frame, 30U);
    EXPECT_EQ(halt.vehicle_index, 0U);
    EXPECT_EQ(sim.clock().frame(), 30U) << "the clock does not advance on a transport halt";
    EXPECT_EQ(sim.transport_fault(), c.fault);
    EXPECT_EQ(signal(sim, "transport.fault"), static_cast<double>(c.fault));
    EXPECT_EQ(signal(sim, "transport.missed"), 1.0);
    EXPECT_EQ(signal(sim, "transport.exchanges"), 3.0);
    EXPECT_EQ(signal(sim, "v0.controls.elevator"), kElevatorA) << "the failed reply is not applied";
  }
}

TEST(SimulationTransport, UnchangedAckObservationsKeepWaitingAndNeverFail) {
  Spec spec;
  spec.peer = "    spurious_wakeups: 5\n";
  astra::Simulation sim = build(spec);
  EXPECT_FALSE(sim.run());
  EXPECT_EQ(sim.clock().frame(), 100U);
  EXPECT_EQ(signal(sim, "transport.exchanges"), 10.0);
}

TEST(SimulationTransport, NonFiniteCommandHaltsNamingTheFieldUnderEveryInputAction) {
  constexpr std::array<std::string_view, 3> kActions = {"halt", "warn", "off"};
  constexpr std::array<std::string_view, 5> kFields = {"elevator", "aileron", "rudder",
                                                       "throttle_0", "throttle_1"};
  constexpr std::array<std::string_view, 5> kIcdFields = {
      "astra_sw_to_sim_v1.elevator", "astra_sw_to_sim_v1.aileron", "astra_sw_to_sim_v1.rudder",
      "astra_sw_to_sim_v1.throttle[0]", "astra_sw_to_sim_v1.throttle[1]"};
  constexpr std::array<std::string_view, 3> kValues = {"nan", "inf", "-inf"};
  for (const std::string_view action : kActions) {
    for (std::size_t f = 0; f < kFields.size(); ++f) {
      for (const std::string_view value : kValues) {
        SCOPED_TRACE(std::string{action} + " " + std::string{kFields[f]} + " " +
                     std::string{value});
        Spec spec;
        spec.sim_extra = "  health: { input: " + std::string{action} + " }\n";
        spec.peer =
            "    commands: { elevator: 0.01 }\n"
            "    faults: [ { frame: 20, kind: non_finite, field: " +
            std::string{kFields[f]} + ", value: " + std::string{value} + " } ]\n";
        astra::Simulation sim = build(spec);
        const HaltReason halt = sim.run();
        EXPECT_EQ(halt.check, HealthCheck::kTransport);
        EXPECT_EQ(halt.source, kIcdFields[f]);
        EXPECT_EQ(halt.frame, 20U);
        EXPECT_EQ(sim.transport_fault(), TransportFault::kNonFiniteCommand);
        EXPECT_EQ(signal(sim, "v0.controls.elevator"), kElevatorA);
      }
    }
  }
}

TEST(SimulationTransport, ThrottleMismatchHaltsNamingThrottle) {
  Spec spec;
  spec.peer = "    commands: { throttle: [0.5, 0.6] }\n";
  astra::Simulation sim = build(spec);
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.source, "astra_sw_to_sim_v1.throttle");
  EXPECT_EQ(halt.frame, 0U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kThrottleMismatch);
}

TEST(SimulationTransport, CommandLimitsAreInclusiveAndAViolationHaltsWithoutClamping) {
  Spec spec;
  spec.peer =
      "    commands: { elevator: 1.0 }\n"  // exactly the upper limit: accepted
      "    schedule: [ { frame: 20, elevator: 1.0000000000000002 } ]\n";
  astra::Simulation sim = build(spec);
  const HaltReason halt = sim.run();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.source, "astra_sw_to_sim_v1.elevator");
  EXPECT_EQ(halt.frame, 20U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kCommandLimit);
  EXPECT_EQ(signal(sim, "v0.controls.elevator"), 1.0) << "not clamped, not applied";
}

// Critique F-2: limits cover every field, not only the mapped ones.
TEST(SimulationTransport, AnUnmappedFieldOutsideItsLimitStillHalts) {
  Spec spec;  // maps only the elevator
  spec.peer = "    commands: { rudder: 2.0 }\n";
  astra::Simulation sim = build(spec);
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.source, "astra_sw_to_sim_v1.rudder");
  EXPECT_EQ(sim.transport_fault(), TransportFault::kCommandLimit);
}

TEST(SimulationTransport, ValidationFollowsTheAdrOrder) {
  struct Case {
    std::string_view name;
    std::string peer;
    TransportFault fault;
    std::string_view source;
  };
  const std::array<Case, 3> cases = {{
      {"bad magic before non-finite",
       "    faults: [ { frame: 0, kind: bad_magic }, { frame: 0, kind: non_finite, field: "
       "elevator, value: nan } ]\n",
       TransportFault::kBadMagic, kLoopback},
      {"non-finite before throttle mismatch",
       "    commands: { throttle: [0.5, 0.6] }\n"
       "    faults: [ { frame: 0, kind: non_finite, field: rudder, value: inf } ]\n",
       TransportFault::kNonFiniteCommand, "astra_sw_to_sim_v1.rudder"},
      {"throttle mismatch before limit", "    commands: { throttle: [0.5, 1.5] }\n",
       TransportFault::kThrottleMismatch, "astra_sw_to_sim_v1.throttle"},
  }};
  for (const Case& c : cases) {
    SCOPED_TRACE(std::string{c.name});
    Spec spec;
    spec.peer = c.peer;
    astra::Simulation sim = build(spec);
    const HaltReason halt = sim.step();
    EXPECT_EQ(halt.check, HealthCheck::kTransport);
    EXPECT_EQ(sim.transport_fault(), c.fault);
    EXPECT_EQ(halt.source, c.source);
  }
}

// Nothing is staged unless every check passes: a valid elevator beside an out-of-limit rudder in
// the same reply leaves the elevator's pending value untouched (proved by latching by hand).
TEST(SimulationTransport, AFailedReplyStagesNoInputAtAll) {
  Spec spec;
  spec.peer =
      "    commands: { elevator: 0.01 }\n"
      "    schedule: [ { frame: 20, elevator: 0.5, rudder: 5.0 } ]\n";
  astra::Simulation sim = build(spec);
  const HaltReason halt = sim.run();
  ASSERT_EQ(sim.transport_fault(), TransportFault::kCommandLimit);
  EXPECT_EQ(halt.source, "astra_sw_to_sim_v1.rudder");
  EXPECT_TRUE(sim.vehicles()[0].inputs().latch().empty());
  EXPECT_EQ(signal(sim, "v0.controls.elevator"), kElevatorA);
}

TEST(SimulationTransport, ATransportHaltIsStickyAndNeverRepublishes) {
  Spec spec;
  spec.peer = "    faults: [ { frame: 30, kind: timeout } ]\n";
  astra::Simulation sim = build(spec);
  const HaltReason first = sim.run();
  ASSERT_EQ(first.check, HealthCheck::kTransport);
  const std::size_t requests = sim.exchange_recorder()->requests().size();
  EXPECT_EQ(requests, 4U);
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(same_halt(sim.step(), first));
  }
  EXPECT_TRUE(same_halt(sim.run(10), first));
  EXPECT_EQ(sim.clock().frame(), 30U);
  EXPECT_EQ(sim.exchange_recorder()->requests().size(), requests);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kTimeout);
  EXPECT_EQ(sim.transport_close_reason(), CloseReason::kAfterFailure);
}

// Joe's PR 3 addition: with a transport, EVERY halt is sticky -- here a wrench NaN on exchange
// frame 20, after that frame's exchange already published.
TEST(SimulationTransport, NonTransportHaltOnExchangeFrameIsSticky) {
  Spec spec;
  spec.models =
      "    - { plugin: tests/plugins/input_consumer }\n"
      "    - { plugin: tests/plugins/nan_wrench, nan_frame: 20 }\n";
  astra::Simulation sim = build(spec);
  const HaltReason first = sim.run();
  ASSERT_EQ(first.check, HealthCheck::kNonFiniteWrench);
  EXPECT_EQ(first.source, "tests/plugins/nan_wrench");
  EXPECT_EQ(first.frame, 20U);
  ASSERT_EQ(sim.exchange_recorder()->requests().size(), 3U) << "frames 0, 10, 20 published";
  for (int i = 0; i < 3; ++i) {
    const HaltReason again = sim.step();
    EXPECT_TRUE(same_halt(again, first)) << "the original HaltReason, not a re-run";
  }
  EXPECT_EQ(sim.exchange_recorder()->requests().size(), 3U) << "frame 20 published exactly once";
  EXPECT_EQ(sim.transport_fault(), TransportFault::kNone) << "never kFrameDiscontinuity";
  EXPECT_EQ(sim.clock().frame(), 20U);
  EXPECT_EQ(signal(sim, "transport.missed"), 0.0);
  EXPECT_EQ(sim.transport_close_reason(), CloseReason::kAfterFailure);
}

// Without a transport, halts keep their pre-M4 behavior: a repeated step() re-runs the frame (R8).
TEST(SimulationTransport, WithoutATransportHaltsAreNotSticky) {
  Spec spec;
  spec.transport = false;
  spec.models =
      "    - { plugin: tests/plugins/input_consumer }\n"
      "    - { plugin: tests/plugins/nan_wrench, nan_frame: 20 }\n";
  astra::Simulation sim = build(spec);
  const HaltReason first = sim.run();
  ASSERT_EQ(first.check, HealthCheck::kNonFiniteWrench);
  const double nan_before = signal(sim, "health.nan");
  const HaltReason again = sim.step();
  EXPECT_EQ(again.check, HealthCheck::kNonFiniteWrench);
  EXPECT_GT(signal(sim, "health.nan"), nan_before) << "the frame was evaluated again";
}

TEST(SimulationTransport, SequenceRangeHaltsAtTheReservedValueIncludingOnRepeatedSteps) {
  Spec spec;
  spec.divisor = 1;
  spec.frames = 10;
  astra::Simulation sim = build(spec);
  constexpr std::uint64_t kLimit = std::numeric_limits<std::uint32_t>::max() - 1U;
  sim.preset_frame_for_testing(kLimit - 2);
  EXPECT_FALSE(sim.step());  // publishes UINT32_MAX - 3
  EXPECT_FALSE(sim.step());  // publishes UINT32_MAX - 2
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, kLimit);
  EXPECT_EQ(halt.source, kLoopback);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kSequenceExhausted);
  EXPECT_EQ(sim.exchange_recorder()->requests().size(), 2U) << "the reserved value is never sent";
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(same_halt(sim.step(), halt));
    EXPECT_TRUE(same_halt(sim.run(5), halt));
  }
  EXPECT_EQ(sim.clock().frame(), kLimit);
}

TEST(SimulationTransport, AFrameCounterDiscontinuityHalts) {
  Spec spec;
  astra::Simulation sim = build(spec);
  ASSERT_FALSE(sim.run(10));  // exchange at frame 0
  sim.preset_frame_for_testing(30);
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.frame, 30U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kFrameDiscontinuity);
}

// Critique F-1: architecture §15, "Log ring-buffer overflow (halt)". Capacity is sim.frames.
TEST(SimulationTransport, RecorderCapacityHaltsAtTheFirstFrameBeyondSimFrames) {
  Spec spec;
  spec.frames = 25;
  astra::Simulation sim = build(spec);
  ASSERT_FALSE(sim.run());
  ASSERT_FALSE(sim.run(0));
  const HaltReason halt = sim.run(10);
  EXPECT_EQ(halt.check, HealthCheck::kTransport);
  EXPECT_EQ(halt.source, "transport.recorder");
  EXPECT_EQ(halt.frame, 25U);
  EXPECT_EQ(sim.transport_fault(), TransportFault::kRecorderFull);
  EXPECT_EQ(sim.clock().frame(), 25U);
  const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
  EXPECT_EQ(recorder->requests().size(), 3U);
  EXPECT_EQ(recorder->replies().size(), 3U);
  EXPECT_EQ(recorder->states().size(), 26U) << "frames 0..25, every one recorded";
  EXPECT_TRUE(same_halt(sim.step(), halt));
}

// ---------------------------------------------------------------------------------------------
// The request's content (PR 2 packing, wired here).

TEST(SimulationTransport, RequestCarriesThePreviousFramesPublishedWrench) {
  Spec spec;
  spec.models =
      "    - { plugin: tests/plugins/input_consumer }\n"
      "    - { plugin: tests/plugins/nan_wrench, force_x: 5.0 }\n";
  astra::Simulation sim = build(spec);
  ASSERT_FALSE(sim.run(11));
  const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
  ASSERT_EQ(recorder->requests().size(), 2U);
  const astra_sim_to_sw_v1& frame0 = recorder->requests()[0];
  EXPECT_NE(frame0.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
  EXPECT_EQ(frame0.ax, 0.0);
  const astra_sim_to_sw_v1& frame10 = recorder->requests()[1];
  EXPECT_EQ(frame10.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
  EXPECT_EQ(frame10.ax, 5.0 / 10.0) << "frame 9's published wrench_total.force_b_x over the mass";
}

// Critique F-6: a derivative() probe latches a real mass without integrating; the first exchange
// after a clock preset must still report ACCEL_INVALID, not a valid zero.
TEST(SimulationTransport, PreviousFrameIsInvalidWithoutACompletedFrameEvenAfterAProbe) {
  Spec spec;
  spec.divisor = 1;
  spec.frames = 10;
  astra::Simulation sim = build(spec);
  const std::span<const double> state = sim.vehicles()[0].combined_state();
  const std::vector<double> probe(state.begin(), state.end());
  static_cast<void>(sim.derivative(probe));
  EXPECT_EQ(sim.vehicles()[0].latched_mass(), 10.0) << "the probe latched the mass";
  sim.preset_frame_for_testing(5);
  ASSERT_FALSE(sim.step());
  ASSERT_FALSE(sim.step());
  const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
  ASSERT_EQ(recorder->requests().size(), 2U);
  EXPECT_NE(recorder->requests()[0].status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
  EXPECT_EQ(recorder->requests()[1].status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U)
      << "frame 5 was completed by this Simulation, so frame 6 has a valid previous frame";
}

// Critique F-4: the exchange packs THIS frame's environment sample (after the sample, before
// vehicle.step), not the previous frame's. Dryden changes the wind every frame, and the test
// asserts that it does, so packing a stale sample would fail.
TEST(SimulationTransport, RequestUsesThisFramesEnvironmentSample) {
  astra::Simulation sim = build_cruise(true, "1");
  astra::EnvironmentSample previous;
  for (std::uint64_t frame = 0; frame < 50; ++frame) {
    ASSERT_FALSE(sim.step()) << frame;
    const astra::EnvironmentSample env = sim.vehicles()[0].environment();
    const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
    const astra_sim_to_sw_v1& request = recorder->requests()[frame];
    const astra::RecordedState& start = recorder->states()[frame];  // frame-start state
    ASSERT_EQ(request.frame, frame);
    ASSERT_EQ(start.frame, frame);
    const astra::VehicleState vs{start.y.data()};
    const Eigen::Vector3d v_rel_b = astra::air_data::relative_velocity_b(vs, env);
    SCOPED_TRACE(frame);
    EXPECT_EQ(request.alpha, astra::air_data::angle_of_attack(v_rel_b));
    EXPECT_EQ(request.beta, astra::air_data::sideslip(v_rel_b));
    EXPECT_EQ(request.tas, astra::air_data::true_airspeed(v_rel_b));
    if (frame > 0) {
      EXPECT_NE(env.wind_n, previous.wind_n) << "adjacent samples must differ for this to bite";
    }
    previous = env;
  }
}

// ---------------------------------------------------------------------------------------------
// R8: the stage reads but never perturbs a run.

// Limited claim (critique F-4): trajectory non-perturbation only. A loopback replying the
// config's own initial_controls every exchange leaves every state bit equal to the same config
// with no transport block, over 300 frames of physics, isa1976, wgs84_rotating, and Dryden.
TEST(SimulationTransport, ConstantTrimRepliesMatchNoTransportRunBitForBit) {
  astra::Simulation plain = build_cruise(false);
  astra::Simulation lockstep = build_cruise(true);
  ASSERT_FALSE(plain.run(300));
  ASSERT_FALSE(lockstep.run(300));
  EXPECT_EQ(lockstep.exchange_recorder()->requests().size(), 30U);
  const std::span<const double> a = plain.vehicles()[0].combined_state();
  const std::span<const double> b = lockstep.vehicles()[0].combined_state();
  ASSERT_EQ(a.size(), b.size());
  EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(double)), 0);
}

// ---------------------------------------------------------------------------------------------
// Config and init errors name their path.

void expect_config_error(const Spec& spec, std::string_view path) {
  SCOPED_TRACE(std::string{path});
  try {
    static_cast<void>(build(spec));
    ADD_FAILURE() << "expected a ConfigError at " << path;
  } catch (const astra::ConfigError& error) {
    EXPECT_EQ(error.config_path(), path) << error.what();
  }
}

TEST(SimulationTransport, ConfigErrorsNameTheirPath) {
  {
    Spec spec;
    spec.transport_extra = "  bogus: 1\n";
    expect_config_error(spec, "transport.bogus");
  }
  {
    Spec spec;
    spec.peer = "    bogus: 1\n";
    expect_config_error(spec, "transport.peer.bogus");
  }
  {
    Spec spec;
    spec.commands = "{ elevator: controls.nope }";
    expect_config_error(spec, "transport.commands.elevator");
  }
  {
    Spec spec;
    spec.commands = "{}";
    expect_config_error(spec, "transport.commands");
  }
  {
    Spec spec;
    spec.commands = "{ throttle: [controls.elevator, controls.aileron] }";
    expect_config_error(spec, "transport.commands.throttle");
  }
  {
    Spec spec;
    spec.commands = "{ throttle: controls.elevator }";
    expect_config_error(spec, "transport.commands.throttle");
  }
  {
    Spec spec;
    spec.limits = "{ elevator: [-1.0, 1.0], aileron: [-1.0, 1.0], throttle: [0.0, 1.0] }";
    expect_config_error(spec, "transport.limits.rudder");
  }
  {
    Spec spec;
    spec.limits =
        "{ elevator: [1.0, -1.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], throttle: [0.0, 1.0] "
        "}";
    expect_config_error(spec, "transport.limits.elevator");
  }
  {
    Spec spec;
    spec.divisor = 0;
    expect_config_error(spec, "transport.divisor");
  }
  {
    Spec spec;
    spec.transport_extra = "  timeout_ms: 1000\n  peer_timeout_ms: 999\n";
    expect_config_error(spec, "transport.peer_timeout_ms");
  }
  {
    Spec spec;
    spec.peer = "    handshake_fail: true\n";
    expect_config_error(spec, "transport");
  }
}

// Critique F-3: each timeout is range-checked before narrowing to uint32_t.
TEST(SimulationTransport, TimeoutsOutsideTheUint32RangeAreRejected) {
  for (const std::string_view key : {"timeout_ms", "handshake_timeout_ms", "peer_timeout_ms"}) {
    for (const std::string_view value : {"0", "-1", "4294967296"}) {
      Spec spec;
      spec.transport_extra = "  " + std::string{key} + ": " + std::string{value} + "\n";
      expect_config_error(spec, "transport." + std::string{key});
    }
  }
}

TEST(SimulationTransport, ATransportWithoutPhysicsIsAConfigErrorAtTransport) {
  const std::string text =
      "sim: { dt: 0.001, frames: 10 }\n"
      "vehicle: { models: [ { plugin: tests/plugins/dummy } ] }\n"
      "transport:\n  plugin: plugins/sim/transport/loopback\n"
      "  commands: { elevator: controls.elevator }\n"
      "  limits: { elevator: [-1, 1], aileron: [-1, 1], rudder: [-1, 1], throttle: [0, 1] }\n";
  try {
    static_cast<void>(astra::Simulation::from_config(astra::Config::from_string(text, "no physics"),
                                                     ASTRA_TEST_PLUGIN_DIR));
    ADD_FAILURE() << "expected a ConfigError";
  } catch (const astra::ConfigError& error) {
    EXPECT_EQ(error.config_path(), "transport");
  }
}

TEST(SimulationTransport, APluginThatIsNotATransportOrSpeaksAnotherIcdVersionIsRejected) {
  for (const std::string_view plugin :
       {"plugins/physical/earth/flat", "tests/plugins/wrong_icd_transport"}) {
    SCOPED_TRACE(std::string{plugin});
    std::string text = to_yaml(Spec{});
    text.replace(text.find(kLoopback), kLoopback.size(), plugin);
    EXPECT_THROW(static_cast<void>(astra::Simulation::from_config(
                     astra::Config::from_string(text, "wrong plugin"), ASTRA_TEST_PLUGIN_DIR)),
                 astra::PluginLoadError);
  }
}

}  // namespace
