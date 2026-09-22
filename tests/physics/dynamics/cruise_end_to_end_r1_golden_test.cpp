// ADR-0021 R1-Sim (end-to-end companion to R1): drives the post-change Simulation through the
// frozen cruise.yaml fixture this golden was captured with (tests/golden/dynamics/
// cruise_end_to_end_r1_fixture.yaml, tests/golden/dynamics/rigid_body_6dof_r1_manifest.md), and
// asserts representation-level equality against the frozen final rigid-body state. Never touches
// the golden or its fixture -- both are human-owned, frozen at capture time.
#include "golden/dynamics/cruise_end_to_end_r1_golden.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::Simulation;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

TEST(CruiseEndToEndR1Golden, MatchesFrozenGoldenBitwiseAfterTheFrozenFixturesOwnFrameCount) {
  const Config config = Config::from_file(std::string{ASTRA_TEST_GOLDEN_DIR} +
                                          "/dynamics/cruise_end_to_end_r1_fixture.yaml");
  Simulation sim = Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), astra::test::r1sim::kCruiseEndToEndR1Frames);

  const astra::HaltReason halt = sim.run();
  EXPECT_EQ(static_cast<bool>(halt), astra::test::r1sim::kCruiseEndToEndR1Halted);

  const auto state = sim.vehicles().front().state();
  for (int i = 0; i < astra::kStateSize; ++i) {
    EXPECT_EQ(bits(state(i)),
              astra::test::r1sim::kCruiseEndToEndR1Result[static_cast<std::size_t>(i)])
        << "component " << i;
  }
}

}  // namespace
