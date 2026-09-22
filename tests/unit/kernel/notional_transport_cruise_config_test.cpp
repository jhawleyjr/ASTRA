// aircraft/notional_transport/configs/cruise.yaml (PLAN.md M3 PR 1/PR 4, docs/architecture.md §13):
// the shipped config actually loads through Simulation::from_config and steps -- not just
// documentation. As of PR 4, vehicle.models carries the real aero/turbofan plugins plus an
// initial_controls block (since PR 8, the still-air trim's elevator/throttle, aileron/rudder=0.0);
// this test proves loading, control-write-seam wiring, and 1000 frames with no halt -- not that the
// flight stays trimmed (the trim itself is checked by tests/python/test_cruise_phugoid.py, and the
// run includes Dryden turbulence). A wrong/missing initial_controls entry would halt frame 0 on
// HealthMonitor::check_input naming the unwritten control; this test's own green result is
// therefore also this PR's own R6 acceptance evidence for that seam.
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
