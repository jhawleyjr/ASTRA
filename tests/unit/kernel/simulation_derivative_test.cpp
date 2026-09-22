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

}  // namespace
