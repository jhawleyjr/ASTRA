// The two named PLAN.md M1 PR 4 acceptance cases, end to end through Simulation (the unit-level
// checks/actions/naming rules live in tests/unit/health/health_monitor_test.cpp): a test plugin
// injecting a NaN wrench at frame 500 halts naming that plugin; q set to norm 1.1 halts naming
// v0.state.q_nb. Also the Codex review finding on this PR: two finite wrench contributions that
// overflow to Inf on summation.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/geodesy.hpp"

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

// ADR-0012 acceptance, end to end through Simulation (the unit-level parsing/counting/reporting
// rules for HealthCheck::kOutOfDomain live in tests/unit/health/health_monitor_test.cpp; the
// in_domain/validate_p_n split per plugin lives in tests/unit/earth/earth_kinematics_test.cpp and
// tests/unit/gravity/{inverse_square,wgs84_j2}_test.cpp). Each fixture below loads a config whose
// OWN initial state is comfortably in domain (so from_config's init-time validate_p_n never
// rejects it), then overwrites the state directly -- the same idiom
// QuaternionNormOneOnePointOneHaltsNamingVehicleStateQNb above uses -- so the exact boundary
// values (astra::geodesy::kMaxLatitude) are used verbatim rather than retyped as YAML literals.

// A trajectory driven toward kMaxLatitude with a small velocity relative to the polar margin (so
// the one-frame lookahead detects the approach well before either p_n_dot's cos(lat) divisor or
// omega_en_n's tan(lat) term sees anything close to the true pi/2 singularity).
TEST(SimulationHalt, ApproachingThePolarMarginHaltsNamingTheEarthPluginBeforeGoingNonFinite) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 2000
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "polar-approach-fixture");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  state(astra::state::kPosition) = astra::geodesy::kMaxLatitude - 1e-5;  // lat, just inside
  state(astra::state::kVelocity) = 100.0;  // v_north (body x, identity attitude)

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/earth/round");
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

// Same shape, the altitude floor: earth/round's p_n_dot/omega_en_n divide by (radius_ + h). A
// small custom radius makes the floor reachable in a handful of frames; pure-vertical motion means
// the domain check fires from the altitude comparison alone, independent of whether the horizontal
// rate terms would themselves have diverged (they would not here, at zero horizontal velocity).
TEST(SimulationHalt, DescendingTowardTheAltitudeFloorHaltsNamingTheEarthPlugin) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 200
vehicle:
  models: []
physical:
  earth: { plugin: plugins/physical/earth/round, radius: 2000.0 }
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
initial:
  state:
    p_n: [0.0, 0.0, -1999.99]
    v_b: [0.0, 0.0, 1.0]
)",
                                            "earth-altitude-floor-fixture");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/earth/round");
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

// The Codex finding round.cpp/inverse_square.cpp's own comments already name: earth's own floor
// says nothing about gravity's separately configured one. earth/round keeps its (far away)
// default radius; only gravity/inverse_square's radius is small, so only IT can fire -- proving
// check_domain checks the two models independently, not through a shared bound. Dropped from rest
// at a modest height above gravity's floor (not from a near-zero gap) so g_n's early values stay
// physically reasonable and the approach is a genuine free fall, not a single-frame jump.
TEST(SimulationHalt, DescendingTowardGravitysOwnFloorHaltsNamingTheGravityPlugin) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 20000
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: { plugin: plugins/physical/gravity/inverse_square, radius: 2000.0 }
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
initial:
  state:
    p_n: [0.0, 0.0, -1000.0]
)",
                                            "gravity-floor-fixture");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/gravity/inverse_square");
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

// A run leaving physical.atmosphere's valid_range() mid-run halts naming that plugin, checked
// post-step in Simulation::step against the same range from_config's own init-time check reads
// (isa1976's [-5000, 86000] m). earth/flat's altitude is -p_n's down component, so a positive,
// growing down component drives the altitude below the floor.
TEST(SimulationHalt, LeavingTheAtmosphereValidRangeMidRunHaltsNamingTheAtmospherePlugin) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 3000
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/flat
  gravity: { plugin: plugins/physical/gravity/constant, g: 0.0 }
  atmosphere: plugins/physical/atmosphere/isa1976
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
initial:
  state:
    p_n: [0.0, 0.0, 4900.0]
    v_b: [0.0, 0.0, 100.0]
)",
                                            "atmosphere-excursion-fixture");
  Simulation sim = load(config);

  const HaltReason halt = sim.run();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/atmosphere/isa1976");
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

// Longitude has no domain_fault of its own (ADR-0012's Context: nothing in the tree wraps it, and
// nothing is singular in it) -- crossing pi must not halt, unlike every case above. g: 0.0 removes
// gravity's own coupling into velocity, isolating the check to exactly what ADR-0012 says it
// covers.
TEST(SimulationHalt, EastWestTrajectoryCrossingLongitudePiDoesNotHalt) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 400
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: { plugin: plugins/physical/gravity/constant, g: 0.0 }
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "antimeridian-fixture");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  state(astra::state::kPosition + 1) = std::numbers::pi - 0.01;  // lon, just under pi
  state(astra::state::kVelocity + 1) = 500000.0;                 // v_east (body y)

  const HaltReason halt = sim.run();

  EXPECT_FALSE(halt);
  EXPECT_GT(sim.vehicles().front().state()(astra::state::kPosition + 1), std::numbers::pi)
      << "the crossing actually happened, not merely that it never got there";
}

// sim.health: { domain: off } restores current (pre-PR-2c) behavior: the exact fixture that halts
// above now runs to completion instead, with latitude having advanced past kMaxLatitude -- the
// same "enormous but finite" territory
// tests/unit/earth/earth_kinematics_test.cpp's
// CrossingThePolarMarginMidRunProducesAnEnormousMagnitude already characterizes at the unit level.
TEST(SimulationHalt, DomainOffRestoresCurrentBehaviorOnTheSamePolarApproachFixture) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 2000
  health: { domain: off }
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "polar-approach-domain-off-fixture");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  state(astra::state::kPosition) = astra::geodesy::kMaxLatitude - 1e-5;
  state(astra::state::kVelocity) = 100.0;

  const HaltReason halt = sim.run();

  EXPECT_FALSE(halt);
  EXPECT_EQ(sim.clock().frame(), 2000U);
  EXPECT_GT(sim.vehicles().front().state()(astra::state::kPosition), astra::geodesy::kMaxLatitude);
}

// warn counts without halting, the same shape as the existing nan/quaternion warn tests above --
// domain's own action key gets the same three-way treatment, not a special case.
TEST(SimulationHalt, DomainWarnCountsWithoutHaltingOnTheSamePolarApproachFixture) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 2000
  health: { domain: warn }
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "polar-approach-domain-warn-fixture");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  state(astra::state::kPosition) = astra::geodesy::kMaxLatitude - 1e-5;
  state(astra::state::kVelocity) = 100.0;

  const HaltReason halt = sim.run();

  EXPECT_FALSE(halt);
  EXPECT_EQ(sim.clock().frame(), 2000U);
  const double* domain_count = sim.registry().find("health.domain");
  ASSERT_NE(domain_count, nullptr);
  EXPECT_GT(*domain_count, 0.0);
}

// ADR-0012 decision 5c: frame 0's own pre-step check. From frame 1 on, a frame's pre-step state is
// the previous frame's already-checked post-step state -- but frame 0 has no earlier frame behind
// it, so this is the one case where the initial state itself passes in_domain while its own
// one-frame lookahead already fails. Only the pre-step check at frame 0 can catch this; a
// post-step- only design would let the integrator's first RK4 stages evaluate past the margin
// before ever checking anything.
TEST(SimulationHalt, FrameZeroPreStepCheckCatchesALookaheadViolationTheInitialStateItselfPasses) {
  const Config config = Config::from_string(R"(
sim:
  dt: 0.001
  frames: 10
vehicle:
  models: []
physical:
  earth: plugins/physical/earth/round
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 1.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
)",
                                            "frame-zero-lookahead-fixture");
  Simulation sim = load(config);
  astra::StateVector& state = sim.vehicles().front().state();
  // 1e-9 rad inside the margin: in_domain(p_n) is true, but one frame's worth of v_north (a
  // ~1.57e-8 rad lat step) already carries the lookahead past it.
  state(astra::state::kPosition) = astra::geodesy::kMaxLatitude - 1e-9;
  state(astra::state::kVelocity) = 100.0;
  ASSERT_TRUE(state.allFinite());
  const double initial_lat = state(astra::state::kPosition);

  const HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/earth/round");
  EXPECT_EQ(halt.frame, 0U);
  EXPECT_EQ(sim.clock().frame(), 0U);
  // The pre-step check runs before the wrench loop or the integrator: state untouched.
  EXPECT_DOUBLE_EQ(sim.vehicles().front().state()(astra::state::kPosition), initial_lat);
}

}  // namespace
