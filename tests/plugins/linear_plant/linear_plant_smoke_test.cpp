// R4 of M7b's oracle (a) (docs/plans/m7b-pr2-sampled-data.md, §12): the plant and the state-feedback
// peer under every preset's flags, the sanitizers included, with zero heap allocations over their
// frames. 200 frames per run; the oracle itself is tests/python/test_sampled_data.py. The configs
// are the plan's §2 nominal N = 10 config with its gains, cut to 200 frames, and its variants: N = 1,
// the three plant defects (long_hold at frame 100), and a peer refusal. Its source sits beside the
// plant to keep PR 2 inside its named paths (the plan's §11, item 2).
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

constexpr std::uint64_t kFrames = 200;

// The plan's §6 gains for N = 10 and N = 1.
constexpr std::string_view kGainsTen = "[0.0625, 0.40625, 1.0572916666666667, 1.4010416666666667]";
constexpr std::string_view kGainsOne = "[625.0, 406.25, 105.72916666666667, 14.010416666666666]";

struct Smoke {
  std::uint32_t transport_divisor = 10;
  std::uint32_t peer_divisor = 10;
  std::string_view gains = kGainsTen;
  std::string_view g = "0.0625";
  std::string_view defect;  // the plant's extra key lines, empty for none
};

Smoke with_defect(std::string_view defect) {
  Smoke smoke;
  smoke.defect = defect;
  return smoke;
}

std::string config_text(const Smoke& smoke) {
  std::string text;
  text += "sim: { dt: 0.001, frames: " + std::to_string(kFrames) + " }\n";
  text += "vehicle:\n";
  text += "  models:\n";
  text += "    - plugin: tests/plugins/linear_plant\n";
  text += "      a: [[0.0, 100.0, 0.0, 0.0], [0.0, 0.0, 100.0, 0.0], [0.0, 0.0, 0.0, 100.0], "
          "[0.0, 0.0, 0.0, 0.0]]\n";
  text += "      b: [[0.0], [0.0], [0.0], [100.0]]\n";
  text += "      x0: [0.0, 0.0, 0.0, 1.0]\n";
  text += "      inputs: [plant.u]\n";
  if (!smoke.defect.empty()) {
    text += "      " + std::string{smoke.defect} + "\n";
  }
  text += "  initial_controls: { plant: { u: 0.0 } }\n";
  text += "physical:\n";
  text += "  earth: plugins/physical/earth/flat\n";
  text += "  gravity: plugins/physical/gravity/constant\n";
  text += "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
          "izz: 1.0 }\n";
  text += "initial:\n";
  text += "  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n";
  text += "transport:\n";
  text += "  plugin: tests/plugins/state_feedback_peer\n";
  text += "  divisor: " + std::to_string(smoke.transport_divisor) + "\n";
  text += "  commands: { elevator: plant.u }\n";
  text += "  limits: { elevator: [-1000.0, 1000.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], "
          "throttle: [0.0, 1.0] }\n";
  text += "  peer:\n";
  text += "    state: v0.linear_plant.state\n";
  text += "    divisor: " + std::to_string(smoke.peer_divisor) + "\n";
  text += "    dt: 0.001\n";
  text += "    k: " + std::string{smoke.gains} + "\n";
  text += "    g: " + std::string{smoke.g} + "\n";
  text += "    reference: 0.0\n";
  text += "    schedule: [{ frame: 107, reference: 1.0 }]\n";
  return text;
}

// Runs the config for kFrames frames and returns the halt, asserting no allocation over the run.
astra::HaltReason run_without_allocating(const Smoke& smoke) {
  const astra::Config config =
      astra::Config::from_string(config_text(smoke), "linear_plant_smoke_test");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run(kFrames);
  const std::size_t after = astra_test::allocation_count();
  EXPECT_EQ(after, before) << "zero allocations over the run";
  return halt;
}

void expect_complete(const Smoke& smoke) {
  const astra::HaltReason halt = run_without_allocating(smoke);
  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame;
}

}  // namespace

TEST(LinearPlant, TwoHundredFramesAtDivisorTenAllocateNothing) { expect_complete(Smoke{}); }

TEST(LinearPlant, TwoHundredFramesAtDivisorOneAllocateNothing) {
  Smoke smoke;
  smoke.transport_divisor = 1;
  smoke.peer_divisor = 1;
  smoke.gains = kGainsOne;
  smoke.g = "625.0";
  expect_complete(smoke);
}

TEST(LinearPlant, TheLateCommandDefectAllocatesNothing) {
  expect_complete(with_defect("test_defect: late_command"));
}

TEST(LinearPlant, TheLongHoldDefectAllocatesNothing) {
  expect_complete(with_defect("test_defect: long_hold\n      test_defect_frame: 100"));
}

TEST(LinearPlant, TheStageSwitchDefectAllocatesNothing) {
  expect_complete(with_defect("test_defect: stage_one_previous"));
}

// A peer whose own divisor is 10 under a transport divisor of 11 refuses the second request
// (frame 11), and the exchange halts kTransport there, allocating nothing on the halt.
TEST(LinearPlant, APeerRefusalHaltsTheTransportAndAllocatesNothing) {
  Smoke smoke;
  smoke.transport_divisor = 11;
  const astra::HaltReason halt = run_without_allocating(smoke);
  EXPECT_TRUE(halt.check == astra::HealthCheck::kTransport)
      << "halted on check " << static_cast<int>(halt.check);
  EXPECT_EQ(halt.frame, 11U);
}
