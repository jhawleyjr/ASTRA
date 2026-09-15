// The two named PLAN.md M1 PR 4 acceptance cases, end to end through Simulation (the unit-level
// checks/actions/naming rules live in tests/unit/health/health_monitor_test.cpp): a test plugin
// injecting a NaN wrench at frame 500 halts naming that plugin; q set to norm 1.1 halts naming
// v0.state.q_nb. Also the Codex review finding on this PR: two finite wrench contributions that
// overflow to Inf on summation.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"

namespace {

using astra::Config;
using astra::HaltReason;
using astra::HealthCheck;
using astra::Simulation;

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

// A NaN wrench from frame 500 on halts naming the plugin, at frame 500, with the clock not advanced
// past it. tests/configs/health_nan_wrench.yaml carries no "sim.health" block at all, so this also
// proves the default action is halt and that an absent block is not an unknown-key error
// (docs/lessons.md: a bare YAML::Node{} is Null-typed but still IsDefined(), so the read path here
// must go through Config::has, never a raw node).
TEST(SimulationHalt, NanWrenchAtFrameFiveHundredHaltsNamingThePlugin) {
  const Config config =
      Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/health_nan_wrench.yaml");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
  EXPECT_EQ(halt.source, "tests/plugins/nan_wrench");
  EXPECT_EQ(halt.frame, 500U);
  EXPECT_EQ(sim.clock().frame(), 500U) << "the clock does not advance on a halt";
}

// q_nb scaled to norm 1.1 halts naming v0.state.q_nb, and the halting frame still renormalized
// (ADR-0001: renormalized after every integrator step, an invariant a halt does not suspend).
TEST(SimulationHalt, QuaternionNormOneOnePointOneHaltsNamingVehicleStateQNb) {
  const Config config =
      Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/physics_ballistic_level.yaml");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  state.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) *= 1.1;
  ASSERT_NEAR(state.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion).norm(), 1.1,
              1e-12);

  const HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kQuaternionNorm);
  EXPECT_EQ(halt.source, "v0.state.q_nb");
  EXPECT_NEAR(sim.vehicles()
                  .front()
                  .state()
                  .segment<astra::state::kQuaternionSize>(astra::state::kQuaternion)
                  .norm(),
              1.0, 1e-12);
}

// Codex review on this PR: individually finite wrench contributions can overflow to a non-finite
// total on summation, which no per-contribution check can catch -- two sources each carrying force
// near DBL_MAX sum past it to +Inf. The halt cannot name either plugin (neither one was at fault by
// itself), so it names the vehicle's accumulated total instead. (With only one wrench source, as in
// the tests above, the total is that source's own contribution, so both checks fire on the same
// value -- see the next test's comment for what that does to the health.nan count under "warn".)
TEST(SimulationHalt, TwoFiniteWrenchContributionsThatOverflowOnSummationHaltNamingTheTotal) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 1
vehicle:
  models:
    - { plugin: tests/plugins/nan_wrench, force_x: 1.0e308 }
    - { plugin: tests/plugins/nan_wrench, force_x: 1.0e308 }
physical:
  earth: plugins/physical/earth/flat
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "overflow-wrench-fixture");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
  EXPECT_EQ(halt.source, "v0.wrench_total");
}

// The default (halt) proven above; this is the loosened side of §15's "a config can loosen them per
// run": a warn action never stops the run, so the NaN wrench keeps flowing into the EOM every frame
// from 500 on, and the resulting state (p_n and v_b, corrupted by frame 500's own integration step)
// stays NaN for the rest of the run too. Three checks detect it every remaining frame -- the single
// contribution, the accumulated total (identical to the contribution with only one wrench source),
// and the state -- 3 x 500 = 1500 total on "health.nan". That accumulation, not a clean single
// count, is the real cost §15 means by "a config can loosen them per run and the loosening is
// recorded in the log": warn on "nan" does not contain the fault, it only stops it from stopping
// the run.
TEST(SimulationHalt, WarnActionRunsAllFramesAndCountsEveryNanDetection) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 1000
  health: { nan: warn }
vehicle:
  models:
    - { plugin: tests/plugins/nan_wrench, nan_frame: 500 }
physical:
  earth: plugins/physical/earth/flat
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "warn-nan-fixture");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  EXPECT_FALSE(halt);
  EXPECT_EQ(sim.clock().frame(), 1000U);
  const double* nan_count = sim.registry().find("health.nan");
  ASSERT_NE(nan_count, nullptr);
  EXPECT_EQ(*nan_count, 1500.0);
}

}  // namespace
