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

// ADR-0012, PLAN.md M2 PR 2c: the per-frame domain check (Earth/gravity in_domain plus the
// one-frame lookahead, and physical.atmosphere's altitude-vs-valid_range comparison) must not
// allocate either, on the most expensive pairing that exercises all three
// (tests/configs/no_alloc_domain.yaml's own comment). Asserts both zero allocations AND that the
// run actually completed all 1000 frames with no halt -- a fixture whose trajectory drifted out of
// domain would halt early and pass the allocation assertion having measured far fewer frames than
// it claims (Joe's own correction on this PR's plan).
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithTheDomainCheckActive) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_domain.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the fixture is chosen to stay in domain for the full run -- a halt here "
                        "means the allocation count below was measured over fewer than 1000 frames";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with the domain check active "
                              "(docs/architecture.md §14/§15, ADR-0012)";
}

// PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md), R5: the new per-stage code path -- a fresh
// EnvironmentSample copy, a fresh AtmosphereModel::at() call, and a fresh wrench_b() call (plus its
// health.check_wrench()) at each of the four RK4 sub-stages, plus the new core-owned
// wrench_total.* signal write on the first stage -- must not allocate either. None of the three
// fixtures above combines a WrenchSource with physical.atmosphere configured, so none of them
// reaches this specific code path at all.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithSubstageWrenchAndAtmosphere) {
  const astra::Config config = astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} +
                                                        "/no_alloc_substage_wrench.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the fixture is chosen to stay in domain for the full run -- a halt here "
                        "means the allocation count below was measured over fewer than 1000 frames";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with a WrenchSource and "
                              "physical.atmosphere both configured (PLAN.md M2 PR 3c)";
}

// ADR-0017 (docs/plans/pr2-write-seam.md), R6: InputRegistry::latch()'s own per-frame commit --
// running every frame unconditionally, whether or not anything wrote a new pending value that frame
// -- must not allocate either. This PR's only implemented writer (a config literal) writes once, at
// init; the allocation-free property under test is latch()'s own unconditional per-frame cost.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithAWriteSeamInputConfigured) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_write_seam.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the fixture is chosen to stay in domain for the full run -- a halt here "
                        "means the allocation count below was measured over fewer than 1000 frames";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with a write-seam input "
                              "configured (ADR-0017)";
}

// PLAN.md M3 PR 3 (ADR-0018), R9: AeroModel/PropulsionModel's own new wrench_b() -- wind
// subtraction, the atan2-based angle assembly, and the isfinite guard's own std::isfinite calls
// and all-NaN Wrench construction -- must not allocate either, with a real write-seam input
// (tests/plugins/aero_stub) and a real physical.atmosphere both configured.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithAeroAndPropulsionModels) {
  const astra::Config config = astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} +
                                                        "/no_alloc_aero_propulsion.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "the fixture is chosen to stay in domain for the full run -- a halt here "
                        "means the allocation count below was measured over fewer than 1000 frames";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with AeroModel/PropulsionModel's "
                              "own wrench_b() running (ADR-0018)";
}

}  // namespace
