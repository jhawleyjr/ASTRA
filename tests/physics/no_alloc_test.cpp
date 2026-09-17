// docs/architecture.md §15: "Allocation detector in debug and sanitizer presets: any heap
// allocation in the per-frame path fails the test suite (tests/physics/test_no_alloc)." No warm-up:
// a frame-0 allocation is a real finding to fix, not one to paper over by skipping past it.
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFrames) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the per-frame path must not fault, let alone allocate";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames (docs/architecture.md §14/§15)";
}

// PLAN.md M2 PR 2b: physical.wind = dryden's per-frame path (Rng draws, the two-state filter's
// Cholesky, the MIL parameter law) must not allocate either -- the same rule as every other
// per-frame model, checked the same way, not assumed from the header comments above.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithDrydenWind) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_dryden.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the per-frame path must not fault, let alone allocate";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames (docs/architecture.md §14/§15)";
}

}  // namespace
