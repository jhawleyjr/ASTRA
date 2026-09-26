// M4 PR 5 (docs/plans/m4-pr5-autopilot.md R6): the real astra_autopilot binary against a test-side
// sim (shm_test::Creator, PR 4's support code, unmodified). Scripted frames in, the reply bytes
// out, and every exit status: hal_exit_status() of the last HAL status, passed through unchanged
// (D6), 1 only for a bad command line. Child (PR 4's process guard) spawns ASTRA_ECHO_PEER_PATH,
// which this target defines as the autopilot binary.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "embedded/autopilot/autopilot.hpp"
#include "embedded/autopilot/cli.hpp"
#include "embedded/hal/astra_hal.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using shm_test::Child;
using shm_test::Creator;
using shm_test::milliseconds;
namespace ap = astra_autopilot;

constexpr milliseconds kReplyWait{2000};
constexpr milliseconds kExitWait{5000};
constexpr double kFrameSeconds = 0.001;  // t = frame * 1 ms, as the sim publishes it

class AutopilotProcess : public shm_test::LockstepTest {};

std::string exact(double value) {
  std::array<char, 40> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
  return std::string{buffer.data()};
}

std::vector<std::string> autopilot_args(const std::string& segment, std::uint32_t handshake_ms,
                                        std::uint32_t peer_ms) {
  return {"--segment",
          segment,
          "--handshake-timeout-ms",
          std::to_string(handshake_ms),
          "--peer-timeout-ms",
          std::to_string(peer_ms),
          "--altitude-m",
          exact(10000.0),
          "--airspeed-mps",
          exact(230.0),
          "--heading-rad",
          exact(0.0),
          "--elevator-trim",
          exact(0.026674474137989946),
          "--throttle-trim",
          exact(0.4985979490507746),
          "--theta-trim",
          exact(0.023586859573503434)};
}

// The same arguments through the autopilot's own parser, so the in-process reference is built from
// the same doubles the process parsed.
ap::Autopilot reference_autopilot(const std::vector<std::string>& args) {
  std::vector<const char*> argv{"astra_autopilot"};
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }
  ap::Options options;
  std::string error;
  EXPECT_TRUE(ap::parse_arguments(static_cast<int>(argv.size()), argv.data(), options, error))
      << error;
  return {options.setpoints, options.trims};
}

astra_sim_to_sw_v1 request(std::uint64_t frame) {
  astra_sim_to_sw_v1 in{};
  in.magic = ASTRA_ICD_MAGIC;
  in.version = ASTRA_ICD_VERSION;
  in.frame = frame;
  in.t = static_cast<double>(frame) * kFrameSeconds;
  in.altitude = 10000.0;
  in.tas = 230.0;
  in.theta = 0.023586859573503434;
  return in;
}

// Publishes `in` as the request for `in.frame`, req_seq = (uint32_t)frame.
void publish(Creator& sim, const astra_sim_to_sw_v1& in) {
  std::memcpy(&sim.region().request, &in, sizeof in);
  shm_test::store(sim.header().req_seq, static_cast<std::uint32_t>(in.frame));
  shm_test::wake(sim.header().req_seq);
}

bool wait_ack(Creator& sim, std::uint32_t seq) {
  return shm_test::eventually([&] { return shm_test::load(sim.header().ack_seq) == seq; },
                              kReplyWait);
}

// A varied script: sinusoids on every read field, a WIND_INVALID frame, and a NaN frame (both
// held).
astra_sim_to_sw_v1 scripted(std::uint64_t k) {
  astra_sim_to_sw_v1 in = request(k * 10);  // exchange every 10 frames
  const double phase = static_cast<double>(k) * 0.05;
  in.altitude += 20.0 * std::sin(phase);
  in.vertical_speed = std::cos(phase);
  in.tas += 2.0 * std::sin(1.3 * phase);
  in.psi = 0.05 * std::sin(0.7 * phase);
  in.phi = 0.02 * std::cos(0.9 * phase);
  in.theta += 0.01 * std::sin(phase);
  in.q = 0.01 * std::cos(phase);
  in.p = 0.005 * std::sin(phase);
  in.r = 0.004 * std::cos(phase);
  in.beta = 0.003 * std::sin(phase);
  if (k == 60) {
    in.status_flags = ASTRA_ICD_STATUS_WIND_INVALID;
  }
  if (k == 120) {
    in.altitude = std::nan("");
  }
  return in;
}

TEST_F(AutopilotProcess, RepliesMatchTheLibraryOnScriptedFramesAndTheSummaryCountsThem) {
  constexpr std::uint64_t kFrames = 200;
  const std::string segment = shm_test::unique_segment();
  const std::vector<std::string> args = autopilot_args(segment, 5000, 5000);
  ap::Autopilot expected_autopilot = reference_autopilot(args);
  Creator sim(segment);
  sim.create_ready();
  Child peer(args, false);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  for (std::uint64_t k = 0; k < kFrames; ++k) {
    const astra_sim_to_sw_v1 in = scripted(k);
    astra_sw_to_sim_v1 expected{};
    expected_autopilot.update(in, in.t, expected);
    publish(sim, in);
    ASSERT_TRUE(wait_ack(sim, static_cast<std::uint32_t>(in.frame))) << "frame " << in.frame;
    EXPECT_TRUE(shm_test::same_bytes(sim.region().reply, expected)) << "frame " << in.frame;
  }
  sim.close_normal();
  EXPECT_EQ(peer.wait_exit(kExitWait), 0);
  EXPECT_EQ(expected_autopilot.held(), 2U);
  EXPECT_NE(peer.output().find("autopilot: handled 200 held 2 status 1"), std::string::npos)
      << peer.output();
}

TEST_F(AutopilotProcess, FirstReplyAtSetpointIsTheTrim) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  Child peer(autopilot_args(segment, 5000, 5000), false);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  publish(sim, request(0));
  ASSERT_TRUE(wait_ack(sim, 0));
  const astra_sw_to_sim_v1& reply = sim.region().reply;
  EXPECT_EQ(reply.frame, 0U);
  EXPECT_EQ(reply.elevator, 0.026674474137989946);
  EXPECT_EQ(reply.aileron, 0.0);
  EXPECT_EQ(reply.rudder, 0.0);
  EXPECT_EQ(reply.throttle[0], 0.4985979490507746);
  EXPECT_EQ(reply.throttle[1], 0.4985979490507746);
  EXPECT_EQ(reply.mode_flags, ap::kModeFlags);
  EXPECT_EQ(reply.fault_flags, 0U);
  sim.close_normal();
  EXPECT_EQ(peer.wait_exit(kExitWait), 0);
}

// --- Exit statuses (D6): hal_exit_status() of the last HAL status, unchanged
// ----------------------

TEST_F(AutopilotProcess, CleanCloseExitsZeroWithAStatusOneSummary) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  Child peer(autopilot_args(segment, 5000, 5000), false);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  sim.close_normal();
  EXPECT_EQ(peer.wait_exit(kExitWait), 0);
  EXPECT_NE(peer.output().find("autopilot: handled 0 held 0 status 1"), std::string::npos)
      << peer.output();
}

TEST_F(AutopilotProcess, SilentSimExitsWithTheHalTimeoutStatus) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  Child peer(autopilot_args(segment, 5000, 300), false);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  EXPECT_EQ(peer.wait_exit(kExitWait), hal_exit_status(ASTRA_HAL_TIMEOUT));
  EXPECT_EQ(hal_exit_status(ASTRA_HAL_TIMEOUT), 2);
  EXPECT_NE(peer.output().find("status 2"), std::string::npos) << peer.output();
}

TEST_F(AutopilotProcess, MissingSegmentExitsWithTheHandshakeTimeoutStatus) {
  Child peer(autopilot_args(shm_test::unique_segment(), 200, 1000), false);
  EXPECT_EQ(peer.wait_exit(kExitWait), 3);
  EXPECT_NE(peer.output().find("status 3"), std::string::npos) << peer.output();
}

TEST_F(AutopilotProcess, ForeignLayoutExitsWithTheBadLayoutStatus) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.size();
  sim.init_header();
  sim.header().layout_version = ASTRA_SHM_LAYOUT_VERSION + 1;
  sim.ready();
  Child peer(autopilot_args(segment, 5000, 1000), false);
  EXPECT_EQ(peer.wait_exit(kExitWait), 4);
  EXPECT_NE(peer.output().find("status 4"), std::string::npos) << peer.output();
}

TEST_F(AutopilotProcess, RequestDisagreeingWithTheSequenceWordExitsWithTheProtocolStatus) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  Child peer(autopilot_args(segment, 5000, 1000), false);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  astra_sim_to_sw_v1 in = request(5);
  std::memcpy(&sim.region().request, &in, sizeof in);
  shm_test::store(sim.header().req_seq, 7U);  // the word says frame 7, the payload says 5
  shm_test::wake(sim.header().req_seq);
  EXPECT_EQ(peer.wait_exit(kExitWait), 5);
  EXPECT_NE(peer.output().find("status 5"), std::string::npos) << peer.output();
}

// --- The command line: exit 1, and the segment is never touched
// ----------------------------------------

TEST_F(AutopilotProcess, BadCommandLinesExitOneWithoutTouchingTheHalOrPrintingASummary) {
  const std::string segment = shm_test::unique_segment();
  const std::vector<std::string> good = autopilot_args(segment, 200, 200);
  std::vector<std::vector<std::string>> bad;
  bad.emplace_back();  // no arguments at all
  {
    std::vector<std::string> missing = good;
    missing.resize(missing.size() - 2);  // --theta-trim gone
    bad.push_back(missing);
  }
  {
    std::vector<std::string> out_of_range = good;
    for (std::size_t i = 0; i + 1 < out_of_range.size(); ++i) {
      if (out_of_range[i] == "--elevator-trim") {
        out_of_range[i + 1] = "0.31";
      }
    }
    bad.push_back(out_of_range);
  }
  {
    std::vector<std::string> duplicate = good;
    duplicate.emplace_back("--segment");
    duplicate.push_back(segment);
    bad.push_back(duplicate);
  }
  {
    std::vector<std::string> hook = good;  // --stop-after without --notify-fd
    hook.emplace_back("--stop-after");
    hook.emplace_back("3");
    bad.push_back(hook);
  }
  for (const std::vector<std::string>& args : bad) {
    Child peer(args, false);
    EXPECT_EQ(peer.wait_exit(kExitWait), 1);
    EXPECT_TRUE(peer.output().empty()) << peer.output();  // usage is on stderr; no summary
  }
}

// --- The test hooks, with the echo peer's semantics (parent plan R4)
// -----------------------------------

TEST_F(AutopilotProcess, StopAfterNotifiesThenPausesUntilKilled) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  std::vector<std::string> args = autopilot_args(segment, 5000, 5000);
  args.emplace_back("--stop-after");
  args.emplace_back("3");
  Child peer(args, true);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  for (std::uint64_t k = 0; k < 3; ++k) {
    publish(sim, request(k * 10));
    ASSERT_TRUE(wait_ack(sim, static_cast<std::uint32_t>(k * 10)));
  }
  EXPECT_TRUE(peer.wait_notify(kExitWait));
  EXPECT_FALSE(peer.wait_exit(milliseconds{100}).has_value());  // paused, not exited
  peer.kill_now();
  EXPECT_EQ(peer.wait_exit(kExitWait), 128 + 9);
}

TEST_F(AutopilotProcess, AbortAfterNotifiesThenAborts) {
  const std::string segment = shm_test::unique_segment();
  Creator sim(segment);
  sim.create_ready();
  std::vector<std::string> args = autopilot_args(segment, 5000, 5000);
  args.emplace_back("--abort-after");
  args.emplace_back("2");
  Child peer(args, true);
  ASSERT_TRUE(sim.wait_peer_ready(kExitWait));
  for (std::uint64_t k = 0; k < 2; ++k) {
    publish(sim, request(k * 10));
    ASSERT_TRUE(wait_ack(sim, static_cast<std::uint32_t>(k * 10)));
  }
  EXPECT_TRUE(peer.wait_notify(kExitWait));
  EXPECT_EQ(peer.wait_exit(kExitWait), 128 + 6);  // SIGABRT
  EXPECT_TRUE(peer.output().empty()) << peer.output();
}

}  // namespace
