// AeroModel/PropulsionModel end to end through Simulation (docs/plans/pr3-aero-propulsion.md's
// Acceptance R8/R8-guard, ADR-0018): the real, unconditional requires_environment().atmosphere =
// true path -- Simulation::for_testing() cannot exercise this at all (Codex plan critique F1: it
// has no atmosphere parameter), so this uses Simulation::from_config throughout, matching
// tests/unit/kernel/simulation_input_seam_test.cpp's own literal-YAML precedent. Uses
// tests/plugins/aero_stub (registers "controls.elevator", ADR-0017) and
// tests/plugins/propulsion_stub.
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::HealthCheck;
using astra::Simulation;

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

// A minimal physical section so Vehicle::integrate() has a real RigidBody6DofDynamics and calls
// wrench_b() (simulation_input_seam_test.cpp's own precedent, kPhysical), plus physical.atmosphere
// -- the real requirement AeroModel/PropulsionModel both declare (ADR-0018).
std::string fixture(std::string_view atmosphere_plugin, double elevator) {
  return "sim: { dt: 0.001, frames: 1 }\n"
         "vehicle:\n"
         "  models:\n"
         "    - { plugin: tests/plugins/aero_stub }\n"
         "    - { plugin: tests/plugins/propulsion_stub }\n"
         "  initial_controls: { controls: { elevator: " +
         std::to_string(elevator) +
         " } }\n"
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  atmosphere: " +
         std::string{atmosphere_plugin} +
         "\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
         "1.0 }\n"
         "initial:\n"
         "  state: { p_n: [0.0, 0.0, -1000.0], v_b: [60.0, 0.0, 0.0] }\n";
}

// R8: the write seam actually drives compute()'s output, end to end -- a config literal writes
// "controls.elevator" (ADR-0017), aero_stub's own compute() multiplies it into force_b(0)
// (dynamic_pressure_pa * elevator); doubling the elevator value must exactly double the resulting
// force, since v_b/altitude (and therefore dynamic_pressure_pa) are unchanged between the two runs
// -- proof that does not depend on isa1976's own exact density value at this altitude.
TEST(AeroPropulsionSeam, WriteSeamDrivesAeroStubsComputeOutput) {
  const Config config_one =
      Config::from_string(fixture("plugins/physical/atmosphere/isa1976", 1.0), "seam-elevator-one");
  Simulation sim_one = load(config_one);
  ASSERT_FALSE(sim_one.step());
  const double* force_one = sim_one.registry().find("v0.wrench_total.force_b_x");
  ASSERT_NE(force_one, nullptr);
  ASSERT_TRUE(std::isfinite(*force_one));
  ASSERT_NE(*force_one, 0.0) << "a nonzero elevator through a nonzero dynamic pressure must not "
                                "yield an exact zero force";

  const Config config_two =
      Config::from_string(fixture("plugins/physical/atmosphere/isa1976", 2.0), "seam-elevator-two");
  Simulation sim_two = load(config_two);
  ASSERT_FALSE(sim_two.step());
  const double* force_two = sim_two.registry().find("v0.wrench_total.force_b_x");
  ASSERT_NE(force_two, nullptr);

  EXPECT_DOUBLE_EQ(*force_two, 2.0 * *force_one)
      << "doubling the write-seam input must double aero_stub's own contribution";
}

// R8: both new WrenchSource specializations load and run together under one vehicle -- the
// combined wrench sums aero_stub's force_b(0) and propulsion_stub's force_b(1) (deliberately
// separate axes, tests/plugins/propulsion_stub's own comment), so each stub's presence is
// independently checkable in the summed v0.wrench_total signal.
TEST(AeroPropulsionSeam, BothSpecializationsContributeToTheCombinedWrench) {
  const Config config =
      Config::from_string(fixture("plugins/physical/atmosphere/isa1976", 1.0), "seam-combined");
  Simulation sim = load(config);
  ASSERT_FALSE(sim.step());
  const double* force_x = sim.registry().find("v0.wrench_total.force_b_x");
  const double* force_y = sim.registry().find("v0.wrench_total.force_b_y");
  ASSERT_NE(force_x, nullptr);
  ASSERT_NE(force_y, nullptr);
  EXPECT_NE(*force_x, 0.0) << "aero_stub's own contribution";
  EXPECT_NE(*force_y, 0.0) << "propulsion_stub's own contribution";
}

// ADR-0020: the unconditional no-silent-NaN guarantee ADR-0018's removed final wrench_b() guard
// gave is restored core-side, applying on this real, end-to-end path too, not only in
// tests/unit/util/air_data_test.cpp's/health_monitor_test.cpp's own unit-level tests -- a broken
// atmosphere sample (tests/plugins/nan_atmosphere, already established by
// tests/unit/kernel/simulation_halt_test.cpp) now halts naming the atmosphere plugin itself,
// before any configured WrenchSource's wrench_b() ever runs -- corrected from this test's own
// prior assertion (misattributed by design, disclosed not fixed, until this PR): the halt used to
// name aero_stub, the consuming plugin, never tests/plugins/nan_atmosphere, which actually
// produced the NaN.
TEST(AeroPropulsionSeam, NonFiniteAtmosphereHaltsNamingTheAtmospherePluginEndToEnd) {
  const Config config =
      Config::from_string(fixture("tests/plugins/nan_atmosphere", 1.0), "seam-nan-atmosphere");
  Simulation sim = load(config);

  const astra::HaltReason halt = sim.step();

  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
  EXPECT_EQ(halt.source, "tests/plugins/nan_atmosphere")
      << "report_non_finite_environment names the environment source, never the consuming "
         "WrenchSource -- "
         "nan_atmosphere's own enormous valid_range() keeps it \"in range\" by construction, so "
         "the domain pre-check does not intercept it first (ADR-0020, Q4)";
  const double* aero_force = sim.registry().find("v0.wrench_total.force_b_x");
  ASSERT_NE(aero_force, nullptr);
  EXPECT_EQ(*aero_force, 0.0)
      << "report_non_finite_environment's own failure skips the wrench_sources_ loop entirely -- "
         "aero_stub's "
         "wrench_b() is never called this stage, so the published total stays at Wrench{}'s own "
         "zero default, not a value aero_stub computed from broken input";
}

}  // namespace
