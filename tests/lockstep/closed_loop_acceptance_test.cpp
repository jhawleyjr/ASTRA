// M4 acceptance in ctest (docs/plans/m4-pr6-lockstep-acceptance.md, "ctest"; M4 plan "Proposed
// placement"): the real astra_autopilot flying aircraft/notional_transport/configs/
// cruise_light_turbulence.yaml over the shm transport. Suite ClosedLoopAcceptance (A6's selector).
//
// Built twice from this one file (tests/lockstep/CMakeLists.txt): on every preset, the limits test,
// a shortened closed loop (1,000 exchanges) and a shortened R3 pair; with
// ASTRA_CLOSED_LOOP_FULL_LENGTH (the asan preset only, label acceptance), the full 10,000-exchange
// run. The full-length R1 to R4b and R12b gate is pytest's (tests/python/test_lockstep.py,
// Release).
//
// PR 4's Child spawns ASTRA_ECHO_PEER_PATH; this executable points it at the autopilot binary, as
// tests/embedded does (a naming quirk that keeps PR 4's files untouched). The trim comes from the
// config's initial_controls and euler_nb[1] (theta = alpha); test_lockstep.py's
// test_trim_matches_config_initial_state links those to astra.trim.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/simulation.hpp"
#include "embedded/autopilot/limits.hpp"
#include "icd/astra_icd.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using shm_test::Child;
using shm_test::milliseconds;

const std::string kConfigDir =
    std::string{ASTRA_SOURCE_DIR} + "/aircraft/notional_transport/configs";
const std::string kLight = kConfigDir + "/cruise_light_turbulence.yaml";
const std::string kModerate = kConfigDir + "/cruise_moderate_turbulence.yaml";

constexpr double kAltitudeSetpoint = 10000.0;  // m, the config's p_n[2]
constexpr double kAirspeedSetpoint = 230.0;    // m/s, the trim's TAS (cruise.yaml header)
constexpr double kAltitudeBand = 30.0;         // m, M4 acceptance
constexpr double kHeadingBandDeg = 2.0;
constexpr std::uint64_t kDivisor = 10;
// A liveness bound under a 32-way parallel sanitizer load, not an accuracy tolerance: lockstep
// trajectories do not depend on it (plan, "ctest"). The config's own values are 1000/2000.
constexpr std::uint32_t kTimeoutMs = 5000;
constexpr std::uint32_t kPeerTimeoutMs = 10000;

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Replaces the one line of `text` that starts with `prefix` by `line`; fails the test unless there
// is exactly one such line (the config text is edited, never re-emitted, so every other value keeps
// its exact spelling).
void replace_line(std::string& text, std::string_view prefix, const std::string& line) {
  std::size_t found = std::string::npos;
  int count = 0;
  for (std::size_t start = 0; start < text.size();) {
    const std::size_t end = text.find('\n', start);
    const std::size_t stop = end == std::string::npos ? text.size() : end;
    if (std::string_view{text}.substr(start, stop - start).starts_with(prefix)) {
      found = start;
      ++count;
    }
    start = stop + 1;
  }
  ASSERT_EQ(count, 1) << "expected exactly one line starting with '" << prefix << "'";
  const std::size_t end = text.find('\n', found);
  text.replace(found, (end == std::string::npos ? text.size() : end) - found, line);
}

std::string lockstep_config(const std::string& segment, std::uint64_t frames) {
  std::string text = read_file(kLight);
  replace_line(text, "  frames:", std::format("  frames: {}", frames));
  replace_line(text, "transport:", "transport:\n  segment: " + segment);
  replace_line(text, "  timeout_ms:", std::format("  timeout_ms: {}", kTimeoutMs));
  replace_line(text, "  peer_timeout_ms:", std::format("  peer_timeout_ms: {}", kPeerTimeoutMs));
  return text;
}

// The autopilot's command line, trims from the config (std::format "{}" is the shortest round
// trip).
std::vector<std::string> autopilot_args(const std::string& segment) {
  const astra::Config config = astra::Config::from_file(kLight);
  const astra::Config controls = config.at("vehicle").at("initial_controls").at("controls");
  const double theta = config.at("initial").at("state").at("euler_nb").at(std::size_t{1}).number();
  return {"--segment",
          segment,
          "--handshake-timeout-ms",
          "5000",
          "--peer-timeout-ms",
          std::to_string(kPeerTimeoutMs),
          "--altitude-m",
          std::format("{}", kAltitudeSetpoint),
          "--airspeed-mps",
          std::format("{}", kAirspeedSetpoint),
          "--heading-rad",
          "0",
          "--elevator-trim",
          std::format("{}", controls.number("elevator")),
          "--throttle-trim",
          std::format("{}", controls.number("throttle")),
          "--theta-trim",
          std::format("{}", theta)};
}

// psi from q_nb (w, x, y, z), 3-2-1: atan2(C12, C11) of the n->b DCM, written out here rather than
// taken from core's frames:: (test-side arithmetic, M4 plan R2b).
double heading_deg(const std::array<double, astra::kStateSize>& y) {
  const double w = y[6];
  const double x = y[7];
  const double yq = y[8];
  const double z = y[9];
  const double psi =
      std::atan2(2.0 * ((x * yq) + (w * z)), (w * w) + (x * x) - (yq * yq) - (z * z));
  return psi * 180.0 / std::numbers::pi;
}

struct ClosedLoopRun {
  std::vector<std::byte> recorder;
  std::size_t requests = 0;
  std::size_t replies = 0;
  std::size_t states = 0;
  double max_altitude_error = 0.0;
  double max_heading_error_deg = 0.0;
  std::optional<int> peer_exit;
  std::string peer_output;
};

// One closed-loop run of `frames`: R1's counters and echo checks and R2's bands are asserted here;
// the caller checks the returned shape.
ClosedLoopRun fly(std::uint64_t frames) {
  ClosedLoopRun run;
  const std::string segment = shm_test::unique_segment();
  Child autopilot(autopilot_args(segment), false);
  {
    astra::Simulation sim = astra::Simulation::from_config(
        astra::Config::from_string(lockstep_config(segment, frames), kLight),
        ASTRA_TEST_PLUGIN_DIR);
    const astra::HaltReason halt = sim.run();
    EXPECT_FALSE(halt) << "halted at frame " << halt.frame << " by " << halt.source;
    EXPECT_EQ(sim.clock().frame(), frames);
    const std::uint64_t expected_exchanges = frames / kDivisor;
    const double* exchanges = sim.registry().find("transport.exchanges");
    const double* missed = sim.registry().find("transport.missed");
    EXPECT_TRUE(exchanges != nullptr && *exchanges == static_cast<double>(expected_exchanges));
    EXPECT_TRUE(missed != nullptr && *missed == 0.0);

    const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
    EXPECT_NE(recorder, nullptr);
    if (recorder == nullptr) {
      return run;
    }
    const auto requests = recorder->requests();
    const auto replies = recorder->replies();
    const auto states = recorder->states();
    run.requests = requests.size();
    run.replies = replies.size();
    run.states = states.size();
    for (std::size_t k = 0; k < requests.size() && k < replies.size(); ++k) {
      EXPECT_EQ(requests[k].frame, k * kDivisor);
      EXPECT_EQ(replies[k].frame, requests[k].frame);  // every reply echoes its request
      EXPECT_TRUE(requests[k].magic == ASTRA_ICD_MAGIC && requests[k].version == ASTRA_ICD_VERSION);
      EXPECT_TRUE(replies[k].magic == ASTRA_ICD_MAGIC && replies[k].version == ASTRA_ICD_VERSION);
    }
    for (std::size_t i = 0; i < states.size(); ++i) {
      EXPECT_EQ(states[i].frame, i);
      run.max_altitude_error =
          std::max(run.max_altitude_error, std::abs(states[i].y[2] - kAltitudeSetpoint));
      run.max_heading_error_deg =
          std::max(run.max_heading_error_deg, std::abs(heading_deg(states[i].y)));
    }
    run.recorder = recorder->serialize();
  }  // the Simulation closes (CLOSED): the autopilot exits 0
  run.peer_exit = autopilot.wait_exit(milliseconds{10000});
  run.peer_output = autopilot.output();
  EXPECT_LE(run.max_altitude_error, kAltitudeBand);
  EXPECT_LE(run.max_heading_error_deg, kHeadingBandDeg);
  EXPECT_EQ(run.peer_exit, std::optional<int>{0}) << run.peer_output;
  EXPECT_NE(
      run.peer_output.find(std::format("autopilot: handled {} held 0 status", frames / kDivisor)),
      std::string::npos)
      << run.peer_output;
  return run;
}

class ClosedLoopAcceptance : public shm_test::LockstepTest {};

#ifndef ASTRA_CLOSED_LOOP_FULL_LENGTH

// Plan D2: transport.limits in both configs EQUAL embedded/autopilot/limits.hpp (the autopilot's
// clamp values), so a clamped command never trips the sim's v < lo || v > hi halt while a broken
// clamp still does.
TEST_F(ClosedLoopAcceptance, ConfigLimitsEqualAutopilotLimits) {
  struct Expected {
    const char* key;
    astra_autopilot::Range range;
  };
  const std::array<Expected, 4> expected = {{{"elevator", astra_autopilot::kElevatorLimit},
                                             {"aileron", astra_autopilot::kAileronLimit},
                                             {"rudder", astra_autopilot::kRudderLimit},
                                             {"throttle", astra_autopilot::kThrottleLimit}}};
  for (const std::string& path : {kLight, kModerate}) {
    const astra::Config limits = astra::Config::from_file(path).at("transport").at("limits");
    ASSERT_EQ(limits.size(), expected.size()) << path;
    for (const Expected& e : expected) {
      const astra::Config pair = limits.at(e.key);
      ASSERT_EQ(pair.size(), 2U) << path << " " << e.key;
      EXPECT_EQ(pair.at(std::size_t{0}).number(), e.range.lo) << path << " " << e.key;
      EXPECT_EQ(pair.at(std::size_t{1}).number(), e.range.hi) << path << " " << e.key;
    }
  }
}

// R1 and R2, shortened: 1,000 exchanges (10,000 frames) on every preset.
TEST_F(ClosedLoopAcceptance, ShortLight) {
  const ClosedLoopRun run = fly(10000);
  EXPECT_EQ(run.requests, 1000U);
  EXPECT_EQ(run.replies, 1000U);
  EXPECT_EQ(run.states, 10001U);
  RecordProperty("max_altitude_error_m", std::format("{}", run.max_altitude_error));
  RecordProperty("max_heading_error_deg", std::format("{}", run.max_heading_error_deg));
}

// R3, shortened: two runs in this one process, byte-equal recorders (the fresh-process R3, with
// jitter and a different seed, is pytest's).
TEST_F(ClosedLoopAcceptance, ShortReproducible) {
  const ClosedLoopRun first = fly(10000);
  const ClosedLoopRun second = fly(10000);
  ASSERT_FALSE(first.recorder.empty());
  EXPECT_EQ(first.recorder.size(), second.recorder.size());
  EXPECT_TRUE(first.recorder == second.recorder);
}

#else

// The full 10,000-exchange light run under ASan/UBSan (asan preset only, label acceptance).
TEST_F(ClosedLoopAcceptance, FullLengthLight) {
  const ClosedLoopRun run = fly(100000);
  EXPECT_EQ(run.requests, 10000U);
  EXPECT_EQ(run.replies, 10000U);
  EXPECT_EQ(run.states, 100001U);
  RecordProperty("max_altitude_error_m", std::format("{}", run.max_altitude_error));
  RecordProperty("max_heading_error_deg", std::format("{}", run.max_heading_error_deg));
}

#endif

}  // namespace
