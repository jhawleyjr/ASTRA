// docs/architecture.md §15: "Allocation detector in debug and sanitizer presets: any heap
// allocation in the per-frame path fails the test suite (tests/physics/test_no_alloc)." No warm-up:
// a frame-0 allocation is a real finding to fix, not one to paper over by skipping past it.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

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

// PLAN.md M3 PR 3d (ADR-0020): aero_stub/propulsion_stub's own wrench_b() overrides -- wind
// subtraction and the atan2-based angle assembly via air_data.hpp -- and the new per-stage domain
// pre-check plus HealthMonitor::report_non_finite_environment (replacing ADR-0018's removed
// per-plugin isfinite guard) must not allocate either, with a real write-seam input
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
  EXPECT_EQ(after, before)
      << "zero allocations over 1000 frames with aero_stub/propulsion_stub's "
         "own wrench_b() and the new domain pre-check/report_non_finite_environment path "
         "running (ADR-0020)";
}

// docs/plans/pr4-notional-aero-turbofan.md, R7: the real notional_transport aero/turbofan plugins'
// own wrench_b() (Eigen matrix/cross-product arithmetic, the finiteness check, the AERORP/CG moment
// transfer) must not allocate either. Reuses the shipped cruise.yaml directly -- a real aircraft
// config, not a synthetic no_alloc fixture -- via ASTRA_AIRCRAFT_DIR, the same file
// notional_transport_cruise_config_test.cpp exercises for correctness; this test exercises it for
// allocation only.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithTheNotionalTransportCruiseConfig) {
  const astra::Config config = astra::Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
                                                        "/notional_transport/configs/cruise.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame;
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with the real aero/turbofan "
                              "plugins configured (docs/plans/pr4-notional-aero-turbofan.md)";
}

// ADR-0021, R2: a state-owning model (tests/plugins/state_owner) must not allocate either --
// combined_/stage_/pre_step_scratch_ (StateBuffer, Vehicle) and the Integrator's own k1..k4/trial
// scratch are all sized exactly once at init (running_offset, known once every model's own
// state_count() has been read), never resized inside step()/integrate().
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithAStateOwningModel) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_state_owner.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame;
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before)
      << "zero allocations over 1000 frames with a state-owning model configured (ADR-0021, R2)";
}

// M4 PR 3 (docs/plans/m4-pr3-core-seam.md, R6): the lockstep exchange -- icd_pack, publish, the
// receive state machine with unchanged acks, validation, InputHandle staging, and the exchange
// recorder -- must not allocate on the success path, over 100 exchanges.
TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithLoopbackExchanges) {
  const astra::Config config =
      astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_transport.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame;
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  const double* exchanges = sim.registry().find("transport.exchanges");
  ASSERT_NE(exchanges, nullptr);
  EXPECT_EQ(*exchanges, 100.0) << "every exchange ran, so the count below covers all 100";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames and 100 lockstep exchanges";
}

// M4 PR 3 (critique F-5): the failure paths too -- a faulting exchange of each HaltReason source
// kind (a protocol fault naming the plugin path, a limit fault naming an ICD field), the
// recorder-capacity halt, a non-transport halt made sticky by the stage (Joe's PR 3 addition),
// and ten repeated step() calls on each stored halt. Each case must actually halt, or it measured
// nothing.
TEST(NoAlloc, ZeroHeapAllocationsOnTransportFaultsAndStickyHalts) {
  const std::string base = [] {
    std::ifstream in(std::string{ASTRA_TEST_CONFIG_DIR} + "/no_alloc_transport.yaml");
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
  }();
  struct Case {
    std::string_view name;
    std::string_view frames;      // replaces "frames: 1000"
    std::string_view models;      // extra vehicle.models entry, or ""
    std::string_view peer_extra;  // appended under transport.peer
    astra::HealthCheck check;
    std::uint64_t halt_frame;
  };
  const std::array<Case, 4> cases = {{
      {"protocol fault naming the plugin (timeout)", "1000", "",
       "    faults: [ { frame: 500, kind: timeout } ]\n", astra::HealthCheck::kTransport, 500},
      {"command fault naming an ICD field (non-finite)", "1000", "",
       "    faults: [ { frame: 500, kind: non_finite, field: elevator, value: inf } ]\n",
       astra::HealthCheck::kTransport, 500},
      {"recorder capacity", "400", "", "", astra::HealthCheck::kTransport, 400},
      {"sticky non-transport halt", "1000",
       "    - { plugin: tests/plugins/nan_wrench, nan_frame: 500 }\n", "",
       astra::HealthCheck::kNonFiniteWrench, 500},
  }};
  for (const Case& c : cases) {
    SCOPED_TRACE(std::string{c.name});
    std::string text = base;
    text.replace(text.find("frames: 1000"), std::string_view{"frames: 1000"}.size(),
                 "frames: " + std::string{c.frames});
    const std::string models_anchor = "    - { plugin: tests/plugins/input_consumer }\n";
    text.insert(text.find(models_anchor) + models_anchor.size(), std::string{c.models});
    text += std::string{c.peer_extra};
    const astra::Config config = astra::Config::from_string(text, c.name);
    astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

    const std::size_t before = astra_test::allocation_count();
    const astra::HaltReason halt = sim.run(1000);
    astra::HaltReason again;
    for (int i = 0; i < 10; ++i) {
      again = sim.step();
    }
    const std::size_t after = astra_test::allocation_count();

    EXPECT_EQ(halt.check, c.check);
    EXPECT_EQ(halt.frame, c.halt_frame);
    EXPECT_EQ(again.check, halt.check) << "the halt is sticky";
    EXPECT_EQ(again.frame, halt.frame);
    EXPECT_EQ(after, before) << "zero allocations on the failure path and on sticky re-steps";
  }
}

}  // namespace
