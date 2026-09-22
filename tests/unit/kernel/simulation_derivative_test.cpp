// M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, R9): the "not a second implementation" proof --
// Simulation::derivative() must be bitwise identical to the real integrator's own k1 RK4 sub-stage
// for the same starting state. The required, same-object form (Joe's plan review) additionally
// proves that a probe leaves nothing behind that changes the very next real frame's own k1
// (Design decision 1e). tests/plugins/state_owner (already shipped by PR 4a) supplies the N>13
// fixture: bit-identity is checked over the model's own slice too, not only the rigid-body 13.
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::HaltReason;
using astra::Simulation;

std::uint64_t bits(double d) { return std::bit_cast<std::uint64_t>(d); }

// flat/constant/constant physics, a level rest state (identity attitude, the default
// Simulation::from_config applies when "initial.state" is absent -- read_initial_state()'s own
// "absent euler_nb means identity attitude" default), plus whatever vehicle.models entries the
// caller supplies. Mirrors tests/unit/kernel/vehicle_state_coupling_test.cpp's own fixture().
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

// Required (Joe's plan review): derivative() first, then a real frame, on the SAME Simulation --
// proves both bit-identity and that the probe left nothing behind that changed the next real
// frame's own k1 (Design decision 1e), in one comparison. state_owner's own self-referential rate
// (dq/dt = k*q) gives the model slice a real, non-trivial derivative to compare, not a placeholder
// zero.
TEST(SimulationDerivative,
     DerivativeThenARealFrameOnTheSameSimulationAgreeOnK1ForAStateOwningModel) {
  const std::string yaml =
      fixture("    - { plugin: tests/plugins/state_owner, initial_value: 1.0, self_rate: 1.0 }\n");
  Simulation sim = load(yaml, "derivative-same-object-state-owner");

  const auto& vehicle = sim.vehicles().front();
  ASSERT_GT(vehicle.combined_state().size(), 13U) << "fixture must actually own N>13 state";
  const std::vector<double> state(vehicle.combined_state().begin(), vehicle.combined_state().end());

  const std::vector<double> probe = sim.derivative(state);
  ASSERT_EQ(probe.size(), state.size());

  const HaltReason halt = sim.step();
  ASSERT_FALSE(halt);

  const auto k1 = vehicle.k1_for_testing();
  ASSERT_EQ(k1.size(), probe.size());
  for (std::size_t i = 0; i < k1.size(); ++i) {
    EXPECT_EQ(bits(k1[i]), bits(probe[i])) << "index " << i;
  }
}

// Secondary (optional, kept for extra coverage): two separately-constructed, identically
// configured Simulations agree -- the same claim, proven a different way.
TEST(SimulationDerivative, MatchesTheRealIntegratorsK1AcrossTwoIdenticallyConfiguredSimulations) {
  const std::string models =
      "    - { plugin: tests/plugins/state_owner, initial_value: 1.0, self_rate: 1.0 }\n";
  Simulation probe_sim = load(fixture(models), "derivative-two-object-probe");
  Simulation real_sim = load(fixture(models), "derivative-two-object-real");

  const std::vector<double> state(probe_sim.vehicles().front().combined_state().begin(),
                                  probe_sim.vehicles().front().combined_state().end());
  const std::vector<double> probe = probe_sim.derivative(state);

  const HaltReason halt = real_sim.step();
  ASSERT_FALSE(halt);
  const auto k1 = real_sim.vehicles().front().k1_for_testing();

  ASSERT_EQ(k1.size(), probe.size());
  for (std::size_t i = 0; i < k1.size(); ++i) {
    EXPECT_EQ(bits(k1[i]), bits(probe[i])) << "index " << i;
  }
}

// Required (Joe's human-review addition, 2026-09-21): the first two tests above both use
// state_owner's own dq/dt = k*q as their only nonzero derivative term -- the rigid-body block's
// own v_b/omega_b rows (indices 3-9) never carry a real wrench or atmosphere contribution, only
// gravity. This test drives a real WrenchSource (tests/plugins/nesc_aero: sphere drag +
// rate damping, NASA/TM-2015-218675 Vol II SB.1.1/B.1.3) reading isa1976 atmosphere density and a
// deliberately nonzero v_b/omega_b/euler_nb initial state, so the shared derivative method's own
// prepare_stage_environment()/accumulate_wrench() calls carry real, nonzero bits through the same
// arithmetic the bit-identity claim covers -- not simplified away by an all-zero or gravity-only
// state, per the same "closed form over a plausible-looking number" discipline docs/lessons.md
// asks for.
std::string wrench_fixture(bool with_aero) {
  return "sim: { dt: 0.001, frames: 1 }\n"
         "vehicle:\n"
         "  models:\n" +
         std::string{with_aero ? "    - { plugin: tests/plugins/nesc_aero, s: 0.2, cd: 0.5, b: "
                                 "2.0, cbar: 1.0, clp: -0.5, cmq: -0.5, cnr: -0.5 }\n"
                               : ""} +
         "    - { plugin: tests/plugins/state_owner, initial_value: 1.0, self_rate: 1.0 }\n"
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
         "1.0 }\n"
         "  atmosphere: plugins/physical/atmosphere/isa1976\n"
         "initial:\n"
         "  state:\n"
         "    v_b: [60.0, 5.0, -2.0]\n"
         "    omega_b: [0.1, -0.05, 0.2]\n"
         "    euler_nb: [0.3, 0.1, -0.2]\n";
}

TEST(SimulationDerivative,
     DerivativeAgreesWithARealFrameAndTheWrenchAtmosphereContributionIsDiscriminating) {
  // Same-object form (required): derivative() first, then a real frame, on the WITH-aero
  // Simulation -- bit-identical over all N, including the rigid-body rows nesc_aero's own wrench
  // and isa1976's own density now actually populate.
  Simulation with_aero = load(wrench_fixture(/*with_aero=*/true), "derivative-wrench-with-aero");
  const auto& vehicle = with_aero.vehicles().front();
  const std::vector<double> state(vehicle.combined_state().begin(), vehicle.combined_state().end());

  const std::vector<double> probe = with_aero.derivative(state);
  ASSERT_EQ(probe.size(), state.size());

  const HaltReason halt = with_aero.step();
  ASSERT_FALSE(halt);

  const auto k1 = vehicle.k1_for_testing();
  ASSERT_EQ(k1.size(), probe.size());
  for (std::size_t i = 0; i < k1.size(); ++i) {
    EXPECT_EQ(bits(k1[i]), bits(probe[i])) << "index " << i;
  }

  // Discriminating, not assumed: a second, otherwise-identical Simulation (same initial state,
  // same physics/atmosphere, no nesc_aero) gives the actual gravity-only dv_b via the identical
  // derivative() code path -- a real second evaluation, not a hand-derived expected number. If
  // nesc_aero's own force/moment were silently zero (e.g. a wiring bug reading the wrong signal),
  // this comparison would false-pass; requiring a real difference here catches that class of bug
  // the first two tests' state-owner-only fixture cannot.
  Simulation gravity_only =
      load(wrench_fixture(/*with_aero=*/false), "derivative-wrench-gravity-only");
  const auto& gravity_only_vehicle = gravity_only.vehicles().front();
  const std::vector<double> gravity_only_state(gravity_only_vehicle.combined_state().begin(),
                                               gravity_only_vehicle.combined_state().end());
  const std::vector<double> gravity_only_probe = gravity_only.derivative(gravity_only_state);
  ASSERT_EQ(gravity_only_probe.size(), probe.size());

  bool any_v_b_row_differs = false;
  for (std::size_t i = 3; i < 6; ++i) {  // v_b's own three rows of the derivative (dv_b/dt)
    if (bits(probe[i]) != bits(gravity_only_probe[i])) {
      any_v_b_row_differs = true;
    }
  }
  EXPECT_TRUE(any_v_b_row_differs)
      << "nesc_aero's own wrench must change dv_b relative to the gravity-only fixture, or the "
         "wrench/atmosphere path this test exists to exercise is not actually contributing";
}

}  // namespace
