// PLAN.md M2 PR 1 acceptance: earth/round + gravity/inverse_square against the closed-form radial
// free-fall solution (the standard "radial trajectory" result for the two-body/point-mass
// inverse-square problem -- e.g. Vallado, "Fundamentals of Astrodynamics and Applications"),
// verified numerically against an independent scipy integration (this session, DOP853, rtol=1e-13)
// before writing this test. Two independent forward evaluations of the closed form, neither an
// inversion and neither relying on the integrator under test: the energy integral
// v^2 = 2*GM'*(1/r - 1/r0) at the final radius, and the fall time
// t(r) = sqrt(r0^3/(2*GM')) * (acos(sqrt(r/r0)) + sqrt((r/r0)*(1-r/r0))) evaluated at the final
// radius and compared against the run's own duration. GM' = g0*R^2 is the equivalent gravitational
// parameter that makes gravity/inverse_square's g0*(R/r)^2 form exactly the inverse-square law.
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"

namespace {

// Must match tests/configs/physics_round_earth_drop.yaml: both plugins' shared defaults.
constexpr double kG0 = 9.80665;
constexpr double kRadius = 6371000.0;
constexpr double kGmEquivalent = kG0 * kRadius * kRadius;
constexpr double kInitialAltitude = 5000.0;
constexpr double kInitialRadius = kRadius + kInitialAltitude;

double closed_form_fall_time(double r) {
  return std::sqrt(kInitialRadius * kInitialRadius * kInitialRadius / (2.0 * kGmEquivalent)) *
         (std::acos(std::sqrt(r / kInitialRadius)) +
          std::sqrt((r / kInitialRadius) * (1.0 - r / kInitialRadius)));
}

double closed_form_speed(double r) {
  return std::sqrt(2.0 * kGmEquivalent * (1.0 / r - 1.0 / kInitialRadius));
}

TEST(RoundEarthDrop, MatchesTheInverseSquareClosedForm) {
  const astra::Config config = astra::Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} +
                                                        "/physics_round_earth_drop.yaml");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

  ASSERT_FALSE(sim.run());
  const double duration = static_cast<double>(sim.frames()) * sim.clock().dt();
  const astra::StateVector& y = sim.vehicles().front().state();
  const double lat = y(astra::state::kPosition);
  const double lon = y(astra::state::kPosition + 1);
  const double altitude = y(astra::state::kPosition + 2);
  const double v_down = y(astra::state::kVelocity + 2);
  const double final_radius = kRadius + altitude;

  // Purely radial fall: latitude and longitude never move.
  EXPECT_NEAR(0.0, lat, 1e-12);
  EXPECT_NEAR(0.0, lon, 1e-12);

  // Energy integral, evaluated forward at the final radius -- no inversion, no dependence on the
  // integrator's own step count.
  EXPECT_NEAR(closed_form_speed(final_radius), v_down, 1e-6);

  // Fall time, evaluated forward at the final radius and compared against the run's actual
  // duration -- an independent check that the trajectory itself (not just the final speed) matches
  // the closed form's r(t).
  EXPECT_NEAR(duration, closed_form_fall_time(final_radius), 1e-6);
}

}  // namespace
