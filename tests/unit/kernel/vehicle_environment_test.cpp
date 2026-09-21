// ADR-0020's own acceptance tests (docs/plans/pr3d-adr0020-implementation.md, R3/R4/R6), end to end
// through Simulation, using tests/plugins/counting_aero/nan_wind/flaky_atmosphere: proves the
// no-silent-NaN guarantee ADR-0018's removed final wrench_b() guard gave is restored core-side, not
// merely inferred from an unchanged published force -- Codex adversarial review (M3 PR 3d) found
// the existing aero_propulsion_seam_test.cpp/simulation_halt_test.cpp coverage proved correct
// attribution but not the actual zero-call guarantee, a k2-only fault, the isolated infinite-
// speed-of-sound case, or a configured non-finite wind at this level.
#include <gtest/gtest.h>

#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::HealthCheck;
using astra::Simulation;

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

std::string fixture(std::string_view wind_plugin, std::string_view atmosphere_plugin) {
  std::string physical =
      "physical:\n  earth: plugins/physical/earth/flat\n  gravity: "
      "plugins/physical/gravity/constant\n";
  if (!wind_plugin.empty()) {
    physical += "  wind: " + std::string{wind_plugin} + "\n";
  }
  physical += "  atmosphere: " + std::string{atmosphere_plugin} +
              "\n  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
              "izz: 1.0 }\n";
  return "sim: { dt: 0.001, frames: 1 }\n"
         "vehicle:\n"
         "  models:\n"
         "    - { plugin: tests/plugins/counting_aero }\n" +
         physical +
         "initial:\n"
         "  state: { p_n: [0.0, 0.0, -1000.0], v_b: [60.0, 0.0, 0.0] }\n";
}

// R4: a stage whose atmosphere is non-finite from frame start (tests/plugins/nan_atmosphere) never
// reaches counting_aero's own wrench_b() at all -- the actual call count, not an inference from an
// unchanged published force.
TEST(VehicleEnvironment, ZeroWrenchSourceCallsWhenAtmosphereIsNonFiniteFromFrameStart) {
  const Config config =
      Config::from_string(fixture("", "tests/plugins/nan_atmosphere"), "flaky-k1-fault");
  Simulation sim = load(config);

  const astra::HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
  EXPECT_EQ(halt.source, "tests/plugins/nan_atmosphere");
  const double* calls = sim.registry().find("v0.counting_aero.wrench_b_calls");
  ASSERT_NE(calls, nullptr);
  EXPECT_EQ(*calls, 0.0);
  EXPECT_TRUE(sim.vehicles().front().state().allFinite())
      << "a wrench fault reverts state -- it must never leak a partially-integrated step";
}

// R3/R4: tests/plugins/flaky_atmosphere is finite on its own first call this frame (k1) and carries
// an infinite (not NaN) speed_of_sound_m_s -- with every other field finite, the R-001 case -- on
// every call after that (k2 onward). Proves: (a) k1 succeeds, calling counting_aero exactly once
// and publishing its real force; (b) the fault first appearing at k2 is still caught, naming
// flaky_atmosphere, not misattributed to counting_aero; (c) k2/k3/k4 are all skipped, so
// counting_aero's own total call count stays at exactly 1, not 4.
TEST(VehicleEnvironment, WrenchSourceCalledOnceBeforeAFaultFirstAppearingAtK2) {
  const Config config =
      Config::from_string(fixture("", "tests/plugins/flaky_atmosphere"), "flaky-k2-fault");
  Simulation sim = load(config);

  const astra::HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
  EXPECT_EQ(halt.source, "tests/plugins/flaky_atmosphere");
  const double* calls = sim.registry().find("v0.counting_aero.wrench_b_calls");
  ASSERT_NE(calls, nullptr);
  EXPECT_EQ(*calls, 1.0) << "k1 succeeded and called wrench_b() once; k2-k4 were all skipped, not "
                            "evaluated 3 more times against a known-broken sample";
  const double* published_force = sim.registry().find("v0.wrench_total.force_b_x");
  ASSERT_NE(published_force, nullptr);
  EXPECT_EQ(*published_force, 1.0)
      << "the k1 (published) stage's own real total survives -- a later stage's fault does not "
         "retroactively zero out an already-published, genuinely successful k1 total";
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

// R4/R6: a configured, genuinely non-finite wind_n (tests/plugins/nan_wind) halts naming the wind
// plugin, before counting_aero's wrench_b() ever runs -- atmosphere is real and working
// (isa1976) so this isolates the wind-specific path from the atmosphere one already covered above.
TEST(VehicleEnvironment, NonFiniteWindHaltsNamingTheWindPluginBeforeAnyWrenchSourceRuns) {
  const Config config = Config::from_string(
      fixture("tests/plugins/nan_wind", "plugins/physical/atmosphere/isa1976"), "nan-wind-fixture");
  Simulation sim = load(config);

  const astra::HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
  EXPECT_EQ(halt.source, "tests/plugins/nan_wind");
  const double* calls = sim.registry().find("v0.counting_aero.wrench_b_calls");
  ASSERT_NE(calls, nullptr);
  EXPECT_EQ(*calls, 0.0);
  EXPECT_TRUE(sim.vehicles().front().state().allFinite());
}

}  // namespace
