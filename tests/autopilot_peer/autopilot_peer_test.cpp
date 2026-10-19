// M7b's oracle (b) (docs/plans/m7b-pr3-autopilot-peer.md §8): the notional transport's shipped
// sim-side autopilot peer as a Transport, and in a Simulation.
// - AutopilotPeerTransport: the shipped AutopilotPeer, held through its header, driven through
//   open/publish/wait_ack/read_reply on a seeded request sequence, against embedded/autopilot's
//   Autopilot::update(in, in.t, out), which is what the autopilot's main loop does with hal_now()
//   (embedded/autopilot/main.cpp:59); and its config refusals against the autopilot's own command
//   line (embedded/autopilot/cli.cpp).
// - AutopilotPeerSmoke: the tracked light peer config and the variant's four porting defects in a
//   Simulation for 2,000 frames, under every preset's flags, the sanitizers included, with zero
//   heap allocations over the frames. The oracle itself is tests/python/test_autopilot_peer.py.
#include "aircraft/notional_transport/sim/transport/autopilot/autopilot_peer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/signals/signal_registry.hpp"
#include "embedded/autopilot/autopilot.hpp"
#include "embedded/autopilot/cli.hpp"
#include "icd/astra_icd.h"
#include "tests/autopilot_peer/seeded_frames.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

namespace ap = astra_autopilot;
namespace peer = astra::aircraft_notional_transport_autopilot;

const std::string kLightPeer = std::string{ASTRA_SOURCE_DIR} +
                               "/aircraft/notional_transport/configs/"
                               "cruise_light_turbulence_autopilot_peer.yaml";
constexpr std::uint64_t kSmokeFrames = 2000;
constexpr std::size_t kTransportFrames = 10000;

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Replaces the one occurrence of `from` in `text` by `to`; fails the test unless there is exactly
// one.
void replace_once(std::string& text, std::string_view from, std::string_view to) {
  const std::size_t first = text.find(from);
  ASSERT_NE(first, std::string::npos) << "'" << from << "' not found";
  ASSERT_EQ(text.find(from, first + 1), std::string::npos) << "'" << from << "' found twice";
  text.replace(first, from.size(), to);
}

// The six values, written as YAML text for the peer's transport block and as the autopilot's
// command-line text.
struct Values {
  std::string altitude = "10000.0";
  std::string airspeed = "230.0";
  std::string heading = "0.0";
  std::string elevator = "0.026674474137989946";
  std::string throttle = "0.4985979490507746";
  std::string theta = "0.023586859573503434";
};

std::string peer_block(const Values& v, std::string_view omit = {}) {
  std::string text = "peer:\n  setpoints:\n";
  const auto line = [&text, omit](std::string_view key, const std::string& value) {
    if (key != omit) {
      text += std::format("    {}: {}\n", key.substr(key.find('.') + 1), value);
    }
  };
  line("setpoints.altitude", v.altitude);
  line("setpoints.airspeed", v.airspeed);
  line("setpoints.heading", v.heading);
  text += "  trim:\n";
  line("trim.elevator", v.elevator);
  line("trim.throttle", v.throttle);
  line("trim.theta", v.theta);
  return text;
}

// The YAML spelling of a probe value, and the command line's.
std::string yaml_text(double value) {
  if (std::isnan(value)) {
    return ".nan";
  }
  if (std::isinf(value)) {
    return value > 0.0 ? ".inf" : "-.inf";
  }
  return std::format("{}", value);
}
std::string cli_text(double value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value > 0.0 ? "inf" : "-inf";
  }
  return std::format("{}", value);
}

bool cli_accepts(const Values& v) {
  const std::array<std::string, 19> args = {"astra_autopilot",
                                            "--segment",
                                            "astra_1_x",
                                            "--handshake-timeout-ms",
                                            "5000",
                                            "--peer-timeout-ms",
                                            "2000",
                                            "--altitude-m",
                                            v.altitude,
                                            "--airspeed-mps",
                                            v.airspeed,
                                            "--heading-rad",
                                            v.heading,
                                            "--elevator-trim",
                                            v.elevator,
                                            "--throttle-trim",
                                            v.throttle,
                                            "--theta-trim",
                                            v.theta};
  std::array<const char*, args.size()> argv{};
  for (std::size_t i = 0; i < args.size(); ++i) {
    argv[i] = args[i].c_str();
  }
  ap::Options options;
  std::string error;
  return ap::parse_arguments(static_cast<int>(argv.size()), argv.data(), options, error);
}

}  // namespace

// --- The Transport against the autopilot ------------------------------------------------------

TEST(AutopilotPeerTransport, RepliesEqualTheAutopilotsBitwise) {
  const astra::Config config = astra::Config::from_string(peer_block(Values{}), "transport");
  autopilot_peer_test::SeededFrames frames(1);  // §8: seed 1's sequence
  peer::AutopilotPeer transport;
  astra::SignalRegistry registry;
  transport.init(config, registry);
  const double* handled = registry.find("transport.peer.handled");
  const double* held = registry.find("transport.peer.held");
  ASSERT_NE(handled, nullptr);
  ASSERT_NE(held, nullptr);
  EXPECT_EQ(transport.icd_version(), ASTRA_ICD_VERSION);
  EXPECT_EQ(transport.open(astra::TransportSettings{}), 0xFFFFFFFFU);  // ADR-0024's sentinel

  ap::Autopilot autopilot(
      ap::Setpoints{10000.0, 230.0, 0.0},
      ap::Trims{0.026674474137989946, 0.4985979490507746, 0.023586859573503434});
  for (std::size_t i = 0; i < kTransportFrames; ++i) {
    const astra_sim_to_sw_v1 in = frames.next();
    const auto seq = static_cast<std::uint32_t>(i * 10);
    transport.publish(in, seq);
    const astra::AckObservation ack = transport.wait_ack(0xFFFFFFFFU);
    EXPECT_EQ(ack.status, astra::AckStatus::kObserved);
    EXPECT_EQ(ack.ack_seq, seq);
    astra_sw_to_sim_v1 reply{};
    transport.read_reply(reply);
    astra_sw_to_sim_v1 expected{};
    autopilot.update(in, in.t, expected);
    ASSERT_TRUE(autopilot_peer_test::same_bytes(reply, expected)) << "frame " << i;
    ASSERT_EQ(*handled, static_cast<double>(autopilot.handled())) << "frame " << i;
    ASSERT_EQ(*held, static_cast<double>(autopilot.held())) << "frame " << i;
  }
  EXPECT_GT(autopilot.held(), 0U);
}

TEST(AutopilotPeerTransport, RefusesWhatTheCliRefusesNamingTheKey) {
  {
    const Values v;
    ASSERT_TRUE(cli_accepts(v));
    astra::SignalRegistry registry;
    peer::AutopilotPeer transport;
    EXPECT_NO_THROW(transport.init(astra::Config::from_string(peer_block(v), "t"), registry));
  }
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInf = std::numeric_limits<double>::infinity();
  struct Probe {
    std::string_view key;
    std::string Values::*field;
    std::vector<double> values;
  };
  const std::array<Probe, 6> probes = {{
      {"setpoints.altitude", &Values::altitude, {kNan, kInf, -kInf, -500.0, 0.0, 30000.0}},
      {"setpoints.airspeed", &Values::airspeed, {kNan, kInf, -kInf, 0.0, -1.0, 1e-9, 400.0}},
      {"setpoints.heading", &Values::heading, {kNan, kInf, -kInf, -7.0, 3.5}},
      {"trim.elevator", &Values::elevator, {kNan, kInf, -kInf, 0.31, -0.31, 0.3, -0.3, 0.0}},
      {"trim.throttle", &Values::throttle, {kNan, kInf, -kInf, -0.1, 1.1, 0.0, 1.0}},
      {"trim.theta", &Values::theta, {kNan, kInf, -kInf, -1.0, 1.0}},
  }};
  for (const Probe& probe : probes) {
    for (const double value : probe.values) {
      Values yaml;
      Values cli;
      yaml.*probe.field = yaml_text(value);
      cli.*probe.field = cli_text(value);
      const bool cli_ok = cli_accepts(cli);
      astra::SignalRegistry registry;
      peer::AutopilotPeer transport;
      bool peer_ok = true;
      try {
        transport.init(astra::Config::from_string(peer_block(yaml), "t"), registry);
      } catch (const astra::ConfigError& error) {
        peer_ok = false;
        EXPECT_EQ(error.config_path(), "peer." + std::string{probe.key}) << error.what();
      }
      EXPECT_EQ(peer_ok, cli_ok) << probe.key << " = " << value;
    }
    // A missing key: the CLI's rule is "required", and the peer's ConfigError names the key.
    astra::SignalRegistry registry;
    peer::AutopilotPeer transport;
    try {
      transport.init(astra::Config::from_string(peer_block(Values{}, probe.key), "t"), registry);
      ADD_FAILURE() << probe.key << " missing was accepted";
    } catch (const astra::ConfigError& error) {
      EXPECT_EQ(error.config_path(), "peer." + std::string{probe.key}) << error.what();
    }
  }
}

// --- The smoke: the shipped peer and the variant in a Simulation -------------------------------

namespace {

struct SmokeResult {
  astra::HaltReason halt;
  double exchanges = 0.0;
  double handled = 0.0;
  double held = 0.0;
};

SmokeResult fly_without_allocating(const std::string& text) {
  const astra::Config config = astra::Config::from_string(text, "autopilot_peer_test");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  const std::size_t before = astra_test::allocation_count();
  SmokeResult result;
  result.halt = sim.run(kSmokeFrames);
  const std::size_t after = astra_test::allocation_count();
  EXPECT_EQ(after, before) << "zero allocations over the run";
  const auto read = [&sim](std::string_view name) {
    const double* value = sim.registry().find(name);
    EXPECT_NE(value, nullptr) << name;
    return value == nullptr ? std::numeric_limits<double>::quiet_NaN() : *value;
  };
  result.exchanges = read("transport.exchanges");
  result.handled = read("transport.peer.handled");
  result.held = read("transport.peer.held");
  return result;
}

std::string variant_config(std::string_view defect) {
  std::string text = read_file(kLightPeer);
  replace_once(text, "plugin: aircraft/notional_transport/sim/transport/autopilot",
               "plugin: tests/plugins/autopilot_peer_variant");
  replace_once(text, "\n  peer:\n", std::format("\n  peer:\n    test_defect: {}\n", defect));
  return text;
}

}  // namespace

TEST(AutopilotPeerSmoke, ShippedPeerFliesWithoutAllocating) {
  const SmokeResult result = fly_without_allocating(read_file(kLightPeer));
  EXPECT_FALSE(result.halt) << "halted on check " << static_cast<int>(result.halt.check)
                            << " at frame " << result.halt.frame;
  EXPECT_EQ(result.exchanges, 200.0);
  EXPECT_EQ(result.handled, 200.0);
  EXPECT_EQ(result.held, 0.0);
}

TEST(AutopilotPeerSmoke, EachVariantDefectFliesWithoutAllocating) {
  for (const std::string_view defect :
       {"float_elevator_trim", "output_before_integral", "float_pitch_rate", "mode_word_zero"}) {
    const SmokeResult result = fly_without_allocating(variant_config(defect));
    EXPECT_FALSE(result.halt) << defect << ": halted on check "
                              << static_cast<int>(result.halt.check) << " at frame "
                              << result.halt.frame;
    EXPECT_EQ(result.handled, 200.0) << defect;
  }
}

TEST(AutopilotPeerSmoke, AnUnknownDefectIsAConfigErrorNamingTheKey) {
  try {
    static_cast<void>(astra::Simulation::from_config(
        astra::Config::from_string(variant_config("no_such_defect"), "autopilot_peer_test"),
        ASTRA_TEST_PLUGIN_DIR));
    ADD_FAILURE() << "an unknown defect was accepted";
  } catch (const astra::ConfigError& error) {
    EXPECT_EQ(error.config_path(), "transport.peer.test_defect") << error.what();
  }
}
