// ADR-0021 acceptance (docs/plans/pr4a-dynamics-model.md's Mapping table): sub-stage-exact
// cross-model state reads (own, via bind_state(), and a peer's, via SignalRegistry), mutual
// dependence resolving regardless of vehicle.models order, the stage-buffer invariant outside any
// derivative call, duplicate published state names and a missing required peer both throwing
// ConfigError, and a bind_state()-resolved pointer surviving Vehicle's own move into
// Simulation::vehicles_. tests/plugins/state_owner is the one configurable state-owning Model these
// all reuse (this repo's frugal-plugin-count style, matching counting_aero/nan_wrench).
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::HaltReason;
using astra::Simulation;

// flat/constant/constant physics, a level rest state (a stable configuration under this fixture:
// no quaternion drift, no domain excursion within the handful of frames these tests run), plus
// whatever vehicle.models entries the caller supplies.
std::string fixture(std::string_view models_yaml) {
  return "sim: { dt: 0.001, frames: 1 }\n"
         "vehicle:\n"
         "  models:\n" +
         std::string{models_yaml} +
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
         "1.0 }\n";
}

Simulation load(const std::string& yaml, std::string_view origin) {
  return Simulation::from_config(Config::from_string(yaml, origin), ASTRA_TEST_PLUGIN_DIR);
}

// RK4 is exact for a polynomial derivative up to degree 3 (docs/lessons.md-style closed-form
// discipline: a plausible-looking number is not a proof). driver's own derivative is the constant
// `rate` (state(t) = rate*t, degree 1); reader's own derivative is driver's CURRENT value
// (state(t) = rate*t^2/2, degree 2) -- both well inside RK4's exactness, so a genuinely sub-stage-
// fresh read reproduces this closed form to double-rounding precision, while a stale (e.g.
// frame-latched) read of driver would integrate a wrong, distinguishably different value.
TEST(VehicleStateCoupling, StateOwnerAndPeerReadTheIdenticalSubStageValueAtEveryStage) {
  const std::string yaml = fixture(
      "    - { plugin: tests/plugins/state_owner, instance: driver, rate: 2.0 }\n"
      "    - { plugin: tests/plugins/state_owner, instance: reader, resolve_peer_name: "
      "\"v0.state_owner_driver.state.0\" }\n");
  Simulation sim = load(yaml, "coupling-sub-stage-fresh");

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);

  const double* reader_value = sim.registry().find("v0.state_owner_reader.state.0");
  ASSERT_NE(reader_value, nullptr);
  constexpr double kDt = 0.001;
  constexpr double kRate = 2.0;
  const double expected = kRate * kDt * kDt / 2.0;
  EXPECT_NEAR(*reader_value, expected, 1e-15);
}

// Codex general review (M3 PR 4a): the tests above exercise a peer's SignalRegistry-resolved read
// and a plain constant rate, but never bind_state() itself -- they would still pass if Vehicle
// stopped calling bind_state() or bound the wrong buffer. self_rate closes that gap: an isolated
// instance's own derivative depends on its own bind_state()-bound current value (dq/dt = k*q,
// self-referential exponential growth), so a stale or unbound own-state read diverges from the
// closed form q(t) = q0*exp(k*t) far beyond RK4's own O(dt^5) truncation error, and an unbound
// own_state_ (Vehicle never calling bind_state()) dereferences null instead of silently passing.
TEST(VehicleStateCoupling, OwnStateBindingIsSubStageFreshAndMatchesExponentialGrowth) {
  const std::string yaml =
      fixture("    - { plugin: tests/plugins/state_owner, initial_value: 1.0, self_rate: 1.0 }\n");
  Simulation sim = load(yaml, "own-state-sub-stage-fresh");

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);

  const double* value = sim.registry().find("v0.state_owner.state.0");
  ASSERT_NE(value, nullptr);
  constexpr double kDt = 0.001;
  constexpr double kSelfRate = 1.0;
  const double expected = std::exp(kSelfRate * kDt);
  EXPECT_NEAR(*value, expected, 1e-10);
}

class MutualStateDependence : public ::testing::TestWithParam<bool> {};

// GetParam() selects vehicle.models order: false = a-then-b, true = b-then-a. Both must resolve
// (Vehicle's phase two runs resolve_peers() for every model only after every model's own phase one
// -- including publishing -- has finished, regardless of load order).
TEST_P(MutualStateDependence, ResolvesRegardlessOfConfigOrder) {
  const std::string a =
      "    - { plugin: tests/plugins/state_owner, instance: a, initial_value: 1.0, "
      "resolve_peer_name: \"v0.state_owner_b.state.0\" }\n";
  const std::string b =
      "    - { plugin: tests/plugins/state_owner, instance: b, initial_value: 2.0, "
      "resolve_peer_name: \"v0.state_owner_a.state.0\" }\n";
  const std::string models = GetParam() ? (b + a) : (a + b);
  Simulation sim = load(fixture(models), "mutual-dependence");

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);

  const double* a_value = sim.registry().find("v0.state_owner_a.state.0");
  const double* b_value = sim.registry().find("v0.state_owner_b.state.0");
  ASSERT_NE(a_value, nullptr);
  ASSERT_NE(b_value, nullptr);
  EXPECT_NE(*a_value, 1.0) << "a's own state must have evolved by reading b's, not stayed static";
  EXPECT_NE(*b_value, 2.0) << "b's own state must have evolved by reading a's, not stayed static";
}

// Codex adversarial review (M3 PR 4a): the original init() ordering called each state-owning
// model's own bind_state() (and, in phase two, resolve_peers()) before ever syncing the stage
// buffer from the just-seeded combined buffer -- either hook could dereference its bound pointer
// immediately and observe StateBuffer's default zeroed contents instead of the real seeded initial
// value. Fixed by seeding every model's initial_state() into combined_ and syncing stage_ to match
// BEFORE any bind_state()/resolve_peers() call. tests/plugins/state_owner's own
// own_value_at_bind/peer_value_at_resolve diagnostics capture exactly what each hook saw, at the
// moment it ran, so this checks the real ordering guarantee, not a value read back after the fact.
TEST_P(MutualStateDependence, HookTimeReadsSeeTheRealSeededInitialValuesNotZeroInBothModelOrders) {
  const std::string a =
      "    - { plugin: tests/plugins/state_owner, instance: a, initial_value: 3.0, "
      "resolve_peer_name: \"v0.state_owner_b.state.0\" }\n";
  const std::string b =
      "    - { plugin: tests/plugins/state_owner, instance: b, initial_value: 5.0, "
      "resolve_peer_name: \"v0.state_owner_a.state.0\" }\n";
  const std::string models = GetParam() ? (b + a) : (a + b);
  Simulation sim = load(fixture(models), "hook-time-initial-values");

  const double* a_own_at_bind = sim.registry().find("v0.state_owner_a.own_value_at_bind");
  const double* b_own_at_bind = sim.registry().find("v0.state_owner_b.own_value_at_bind");
  const double* a_peer_at_resolve = sim.registry().find("v0.state_owner_a.peer_value_at_resolve");
  const double* b_peer_at_resolve = sim.registry().find("v0.state_owner_b.peer_value_at_resolve");
  ASSERT_NE(a_own_at_bind, nullptr);
  ASSERT_NE(b_own_at_bind, nullptr);
  ASSERT_NE(a_peer_at_resolve, nullptr);
  ASSERT_NE(b_peer_at_resolve, nullptr);
  EXPECT_EQ(*a_own_at_bind, 3.0) << "a's own bind_state() must see a's real seeded initial value";
  EXPECT_EQ(*b_own_at_bind, 5.0) << "b's own bind_state() must see b's real seeded initial value";
  EXPECT_EQ(*a_peer_at_resolve, 5.0)
      << "a's resolve_peers() must see b's real seeded initial value";
  EXPECT_EQ(*b_peer_at_resolve, 3.0)
      << "b's resolve_peers() must see a's real seeded initial value";
}

INSTANTIATE_TEST_SUITE_P(ConfigOrder, MutualStateDependence, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "BThenA" : "AThenB";
                         });

TEST(VehicleStateCoupling, StageBufferMatchesCombinedBufferAfterInit) {
  const std::string yaml =
      fixture("    - { plugin: tests/plugins/state_owner, initial_value: 3.0 }\n");
  const Simulation sim = load(yaml, "stage-sync-after-init");

  const auto combined = sim.vehicles().front().combined_state_for_testing();
  const auto stage = sim.vehicles().front().stage_state_for_testing();
  ASSERT_EQ(combined.size(), stage.size());
  for (std::size_t i = 0; i < combined.size(); ++i) {
    EXPECT_EQ(combined[i], stage[i]) << "index " << i;
  }
}

TEST(VehicleStateCoupling, StageBufferMatchesCombinedBufferAfterANormalFrame) {
  const std::string yaml = fixture("    - { plugin: tests/plugins/state_owner, rate: 1.0 }\n");
  Simulation sim = load(yaml, "stage-sync-after-normal-frame");

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);

  const auto combined = sim.vehicles().front().combined_state_for_testing();
  const auto stage = sim.vehicles().front().stage_state_for_testing();
  ASSERT_EQ(combined.size(), stage.size());
  for (std::size_t i = 0; i < combined.size(); ++i) {
    EXPECT_EQ(combined[i], stage[i]) << "index " << i;
  }
}

TEST(VehicleStateCoupling, StageBufferMatchesCombinedBufferAfterAHaltedFrame) {
  const std::string yaml =
      "sim: { dt: 0.001, frames: 1 }\n"
      "vehicle:\n"
      "  models:\n"
      "    - { plugin: tests/plugins/nan_wrench, nan_frame: 0 }\n"
      "    - { plugin: tests/plugins/state_owner, rate: 1.0 }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
      "1.0 }\n";
  Simulation sim = load(yaml, "stage-sync-after-halted-frame");

  const HaltReason halt = sim.step();
  ASSERT_TRUE(halt) << "tests/plugins/nan_wrench's own default config halts on frame 0";

  const auto combined = sim.vehicles().front().combined_state_for_testing();
  const auto stage = sim.vehicles().front().stage_state_for_testing();
  ASSERT_EQ(combined.size(), stage.size());
  for (std::size_t i = 0; i < combined.size(); ++i) {
    EXPECT_EQ(combined[i], stage[i]) << "index " << i;
  }
}

// Two instances, neither given a distinguishing `instance`: both resolve to name() ==
// "state_owner" and collide on their shared local index-0 signal name
// (vehicle.hpp's own top comment: deliberate, not an oversight -- the realistic scenario this
// acceptance clause protects against).
TEST(VehicleStateCoupling, DuplicateStateSignalNameThrowsConfigError) {
  const std::string yaml = fixture(
      "    - { plugin: tests/plugins/state_owner, rate: 1.0 }\n"
      "    - { plugin: tests/plugins/state_owner, rate: 2.0 }\n");
  EXPECT_THROW(load(yaml, "duplicate-state-signal-name"), ConfigError);
}

TEST(VehicleStateCoupling, MissingRequiredPeerThrowsConfigErrorNamingIt) {
  const std::string yaml = fixture(
      "    - { plugin: tests/plugins/state_owner, resolve_peer_name: \"v0.nonexistent.state.0\" "
      "}\n");
  try {
    load(yaml, "missing-required-peer");
    FAIL() << "expected a ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_NE(std::string{e.config_path()}.find("resolve_peer_name"), std::string::npos);
    EXPECT_NE(std::string{e.what()}.find("v0.nonexistent.state.0"), std::string::npos);
  }
}

// ADR-0021: Simulation::from_config moves its one Vehicle into vehicles_ after init() has already
// resolved bind_state()/SignalRegistry pointers into it (simulation.cpp) -- StateBuffer's own move
// constructor (never move-assignment, see state_buffer.hpp) must keep those pointers valid across
// that move. A dangling pointer here is exactly the class of bug ASan's stack/heap-use-after-*
// checks exist to catch outright, not merely a value that happens to look plausible.
TEST(VehicleStateCoupling, PointerIntoStageBufferSurvivesTheVehicleMoveIntoSimulation) {
  const std::string yaml =
      fixture("    - { plugin: tests/plugins/state_owner, initial_value: 5.0, rate: 1.0 }\n");
  Simulation sim = load(yaml, "pointer-survives-move");

  const double* value = sim.registry().find("v0.state_owner.state.0");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 5.0);

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);
  EXPECT_NE(*value, 5.0) << "the same pointer, read again after a step, must reflect the live, "
                            "evolved value -- not a stale or dangling one";
}

}  // namespace
