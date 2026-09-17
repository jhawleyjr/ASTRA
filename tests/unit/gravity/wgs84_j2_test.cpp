// plugins/physical/gravity/wgs84_j2 against the Somigliana closed form for normal gravity on the
// WGS-84 ellipsoid (docs/decisions/0000-founding-decisions.md's Earth-model row; PLAN.md M2 PR 1).
// Two independent formulations of the same physics -- a J2-truncated point-mass gravitation model
// (Groves, "Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems," 2nd ed.,
// eq 2.142) plus centripetal, versus the exact normal-gravity formula for a level ellipsoid -- so
// this test judges the plugin's implementation, not itself. The two theories are not the same
// truncation of the true field (Somigliana implicitly sums every even zonal harmonic; the plugin
// keeps only J2), so they agree closely but not to machine precision: verified numerically (Python,
// this session) before choosing kTolerance below -- the discrepancy grows toward the poles, peaking
// at ~1.18e-4 m/s^2 near |lat| = 90 deg, which is the physical residual of the truncation, not
// implementation error.
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/geodesy.hpp"

namespace {

using astra::Config;
using astra::GravityModel;
using astra::StateVector;
using astra::VehicleState;

// WGS-84 published normal gravity at the equator and pole (NIMA TR8350.2, 3rd ed.), independent of
// geodesy.hpp and of the plugin under test.
constexpr double kGammaEquator = 9.7803253359;
constexpr double kGammaPole = 9.8321849378;
constexpr double kSemiMinorAxis = 6356752.314245;

// Somigliana's formula: gamma(lat) = gamma_e * (1 + k*sin^2(lat)) / sqrt(1 - e^2*sin^2(lat)).
double somigliana_gravity(double lat) {
  const double k = (kSemiMinorAxis * kGammaPole - astra::geodesy::kSemiMajorAxis * kGammaEquator) /
                   (astra::geodesy::kSemiMajorAxis * kGammaEquator);
  const double s = std::sin(lat);
  return kGammaEquator * (1.0 + k * s * s) /
         std::sqrt(1.0 - astra::geodesy::kEccentricitySquared * s * s);
}

// wgs84_j2's init() reads no config and registers no signal (an empty override), so calling it
// again on every load() is harmless and keeps this helper simple.
GravityModel& load() {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded = loader.load("plugins/physical/gravity/wgs84_j2");
  auto* const gravity = dynamic_cast<GravityModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  gravity->init(Config::from_string("{}\n", "wgs84-j2-fixture"), registry);
  return *gravity;
}

StateVector state_on_surface(double lat, double lon) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kPositionSize>(astra::state::kPosition) = Eigen::Vector3d{lat, lon, 0.0};
  y(astra::state::kQuaternion) = 1.0;
  return y;
}

// The discrepancy peaks near the poles (verified numerically: ~1.18e-4 m/s^2 at |lat| -> 90 deg);
// 2e-4 covers every latitude tested here with margin.
constexpr double kDownTolerance = 2e-4;
// North/East residuals (the "deflection of the vertical" the J2-only truncation leaves in): at most
// a few times 1e-5 m/s^2 at the latitudes tested, verified numerically before setting this bound.
constexpr double kHorizontalTolerance = 1e-4;

TEST(Wgs84J2Gravity, DownComponentMatchesSomiglianaAcrossLatitude) {
  GravityModel& gravity = load();
  for (const double lat_deg : {0.0, 15.0, 30.0, 45.0, 60.0, 75.0, 85.0}) {
    SCOPED_TRACE(lat_deg);
    const double lat = lat_deg * std::numbers::pi / 180.0;
    const Eigen::Vector3d g_n = gravity.g_n(VehicleState{state_on_surface(lat, 0.647)});
    EXPECT_NEAR(somigliana_gravity(lat), g_n(2), kDownTolerance);
    EXPECT_NEAR(0.0, g_n(0), kHorizontalTolerance) << "north residual";
    EXPECT_NEAR(0.0, g_n(1), kHorizontalTolerance) << "east residual";
  }
}

TEST(Wgs84J2Gravity, MatchesThePublishedEquatorialAndPolarValues) {
  GravityModel& gravity = load();
  const Eigen::Vector3d g_equator = gravity.g_n(VehicleState{state_on_surface(0.0, 0.0)});
  EXPECT_NEAR(kGammaEquator, g_equator(2), kDownTolerance);

  const Eigen::Vector3d g_near_pole =
      gravity.g_n(VehicleState{state_on_surface(astra::geodesy::kMaxLatitude, 0.0)});
  EXPECT_NEAR(kGammaPole, g_near_pole(2), kDownTolerance);
}

TEST(Wgs84J2Gravity, IsIndependentOfLongitude) {
  GravityModel& gravity = load();
  const double lat = 37.0 * std::numbers::pi / 180.0;
  const Eigen::Vector3d at_0 = gravity.g_n(VehicleState{state_on_surface(lat, 0.0)});
  const Eigen::Vector3d at_180 =
      gravity.g_n(VehicleState{state_on_surface(lat, std::numbers::pi - 0.01)});
  EXPECT_NEAR(at_0(2), at_180(2), 1e-9);
}

TEST(Wgs84J2Gravity, AssumesGeodeticPositionRepresentation) {
  EXPECT_EQ(astra::PositionRepresentation::kGeodetic, load().assumed_position_representation());
}

// Codex adversarial review: earth/wgs84_rotating's own meridian-radius floor does not protect
// gravity/wgs84_j2's ECEF-based geometry -- verified numerically that lat = kMaxLatitude,
// alt = -N(lat)*(1-e^2) makes the ECEF radius collapse to ~0.043 m even though it passes that
// floor. gravity/wgs84_j2's own validate_p_n must reject it independently.
TEST(Wgs84J2Gravity, ValidatePnRejectsAnEcefRadiusThatCollapsesNearThePolarMargin) {
  GravityModel& gravity = load();
  const Config node = Config::from_string("{}\n", "near-singular-fixture");
  const double lat = astra::geodesy::kMaxLatitude;
  const double collapsing_alt =
      -astra::geodesy::prime_vertical_radius(lat) * (1.0 - astra::geodesy::kEccentricitySquared);
  EXPECT_THROW(gravity.validate_p_n({lat, 0.0, collapsing_alt}, node), astra::ConfigError);

  // A sensible altitude at the same latitude must not be rejected.
  EXPECT_NO_THROW(gravity.validate_p_n({lat, 0.0, 10000.0}, node));
}

// ADR-0012 (M2 PR 2c): this plugin's own validate_p_n was never a no-op -- decision 5b's fix,
// not this ADR's default. in_domain is the same ECEF-radius check, exposed as a pure predicate;
// the collapsing case above is exactly docs/lessons.md's own numeric example, and this is also
// where earth/wgs84_rotating's in_domain (tests/unit/earth/earth_kinematics_test.cpp's
// Wgs84InDomainTrueInsideAndFalseAtTheMeridianRadiusFloor) is TRUE at the very same p_n -- proof
// the two models' domains are genuinely independent, the whole reason gravity/wgs84_j2 needs its
// own predicate rather than trusting earth/wgs84_rotating's M(lat) floor.
TEST(Wgs84J2Gravity, InDomainMatchesValidatePnAtThePolarCollapseAndIsIndependentOfEarthsFloor) {
  GravityModel& gravity = load();
  const double lat = astra::geodesy::kMaxLatitude;
  const double collapsing_alt =
      -astra::geodesy::prime_vertical_radius(lat) * (1.0 - astra::geodesy::kEccentricitySquared);
  EXPECT_FALSE(gravity.in_domain({lat, 0.0, collapsing_alt}));
  EXPECT_TRUE(gravity.in_domain({lat, 0.0, 10000.0}));

  // earth/wgs84_rotating's own floor is -M(lat), and collapsing_alt (~ -N(lat)*(1-e^2)) is well
  // above it at this near-polar latitude (M(lat) < N(lat)*(1-e^2) there) -- so the Earth model
  // alone would accept this p_n.
  EXPECT_GT(collapsing_alt, -astra::geodesy::meridian_radius(lat))
      << "collapsing_alt must be inside earth/wgs84_rotating's own floor for this to be the "
         "independent-singularity case the plugin's own comment describes";
}

}  // namespace
