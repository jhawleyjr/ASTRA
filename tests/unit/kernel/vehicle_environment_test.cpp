// ADR-0020's own acceptance tests (docs/plans/pr3d-adr0020-implementation.md, R3/R4/R6), end to end
// through Simulation, using tests/plugins/counting_aero/nan_wind/flaky_atmosphere: proves the
// no-silent-NaN guarantee ADR-0018's removed final wrench_b() guard gave is restored core-side, not
// merely inferred from an unchanged published force -- Codex adversarial review (M3 PR 3d) found
// the existing aero_propulsion_seam_test.cpp/simulation_halt_test.cpp coverage proved correct
// attribution but not the actual zero-call guarantee, a k2-only fault, the isolated infinite-
// speed-of-sound case, or a configured non-finite wind at this level.
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

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

// ADR-0033 (M7 PR 3c, test C): EnvironmentSample::altitude_m defaults to NaN, so a WrenchSource
// handed a sample built outside the stage path with the field unset computes a NaN wrench, never a
// sea-level number, and HealthMonitor::check_wrench halts on it naming that plugin. The reader is
// tests/plugins/earth_rate_recorder, whose force_b.x() is altitude_m - altitude_m. Inside a
// simulation the core always sets the field (simulation_earth_rate_test.cpp's
// SimulationStageAltitude), so this case exists only outside the stage path.
TEST(VehicleEnvironment, AnUnsetAltitudeGivesItsReaderANaNWrench) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("tests/plugins/earth_rate_recorder");
  auto* const reader = dynamic_cast<astra::WrenchSource*>(loaded.model.get());
  ASSERT_NE(reader, nullptr);
  astra::SignalRegistry registry;
  reader->init(Config::from_string("{}\n", "unset-altitude-reader"), registry);

  astra::StateVector y = astra::StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;
  const astra::VehicleState vs{y.data()};

  const astra::EnvironmentSample unset;
  const astra::Wrench nan_wrench = reader->wrench_b(vs, unset);
  EXPECT_TRUE(std::isnan(nan_wrench.force_b.x())) << "an unset altitude read as a number";

  astra::EnvironmentSample set;
  set.altitude_m = 1000.0;
  const astra::Wrench zero_wrench = reader->wrench_b(vs, set);
  EXPECT_EQ(std::bit_cast<std::uint64_t>(zero_wrench.force_b.x()), 0U) << "not +0.0";

  astra::SignalRegistry health_registry;
  astra::HealthMonitor monitor;
  monitor.init(Config::from_string("sim: {}\n", "unset-altitude-health"), health_registry);
  const astra::HaltReason halt =
      monitor.check_wrench(nan_wrench, "tests/plugins/earth_rate_recorder",
                           astra::StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
  EXPECT_EQ(halt.source, "tests/plugins/earth_rate_recorder");
}

}  // namespace
