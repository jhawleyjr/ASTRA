// M3 PR 8 (D20, docs/plans/pr8-cruise-regression.md): the 300 s open-loop run from the trimmed
// aircraft/notional_transport/configs/cruise.yaml, sampled at 1 Hz by
// tests/support/cruise_regression_run.hpp. O5: a regression trajectory, not an oracle -- it freezes
// what this build computes, so any later physics change fails it by design. The run includes
// cruise.yaml's seed-42 Dryden turbulence from frame 0: a turbulence response from trim, not a
// cruise hold. A halt anywhere in the 300 s is a finding, asserted here, never frozen.
#include <gtest/gtest.h>

#include <cstddef>

#include "astra/kernel/health_monitor.hpp"
#include "cruise_regression_run.hpp"

namespace {

namespace cr = astra::test::cruise_regression;

TEST(NotionalTransportCruiseRegression, TwoRunsFromTheSameConfigAreBitwiseIdentical) {
  const cr::RunResult first = cr::run_cruise_regression(ASTRA_AIRCRAFT_DIR, ASTRA_TEST_PLUGIN_DIR);
  const cr::RunResult second = cr::run_cruise_regression(ASTRA_AIRCRAFT_DIR, ASTRA_TEST_PLUGIN_DIR);

  ASSERT_EQ(first.halt_check, astra::HealthCheck::kNone)
      << "first run halted at frame " << first.halt_frame;
  ASSERT_EQ(second.halt_check, astra::HealthCheck::kNone)
      << "second run halted at frame " << second.halt_frame;
  ASSERT_EQ(first.samples_taken, cr::kSamples);
  ASSERT_EQ(second.samples_taken, cr::kSamples);

  for (std::size_t k = 0; k < cr::kSamples; ++k) {
    for (std::size_t i = 0; i < first.trajectory[k].size(); ++i) {
      EXPECT_EQ(first.trajectory[k][i], second.trajectory[k][i])
          << "sample " << k << " (t = " << k << " s), component " << i;
    }
  }
}

}  // namespace
