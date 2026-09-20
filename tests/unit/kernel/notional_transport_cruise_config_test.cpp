// aircraft/notional_transport/configs/cruise.yaml (PLAN.md M3 PR 1, docs/architecture.md §13):
// the shipped config actually loads through Simulation::from_config and steps -- not just
// documentation. No aero/propulsion yet (vehicle.models is empty; PR 3/4), so this exercises
// mass+earth+gravity+atmosphere+wind wiring only; the vehicle free-falls under gravity for the
// short run below, which is expected and not a physics claim.
#include <gtest/gtest.h>

#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"

namespace {

using astra::Config;
using astra::HaltReason;
using astra::Simulation;

TEST(NotionalTransportCruiseConfig, LoadsAndStepsWithNoHalt) {
  const Config config = Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
                                          "/notional_transport/configs/cruise.yaml");
  Simulation sim = Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  const HaltReason halt = sim.run();  // "sim.frames" from the config (1000)
  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame;
  EXPECT_EQ(sim.clock().frame(), 1000U);
}

}  // namespace
