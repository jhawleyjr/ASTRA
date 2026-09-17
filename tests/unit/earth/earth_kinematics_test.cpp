// plugins/physical/earth/{round,wgs84_rotating} against the standard navigation-equation closed
// forms (Stevens & Lewis / Titterton & Weston), tested as pure functions on hand-built
// VehicleStates -- not by integrating a trajectory, which would need a lift force no plugin in this
// PR supplies and would compare the code to itself rather than to an independent closed form
// (docs/process/workflow.md §Review: a Codex plan-review finding on the M2 PR 1 plan). Loads the
// real .so through PluginLoader, like tests/unit/plugin/loader_test.cpp.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/geodesy.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::EarthModel;
using astra::StateVector;
using astra::VehicleState;

astra::PluginLoader make_loader() { return astra::PluginLoader{ASTRA_TEST_PLUGIN_DIR}; }

EarthModel& load(std::string_view plugin_path, const Config& config) {
  static astra::PluginLoader loader = make_loader();
  static astra::LoadedPlugin loaded;
  loaded = loader.load(plugin_path);
  auto* const earth = dynamic_cast<EarthModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  earth->init(config, registry);
  return *earth;
}

EarthModel& load_round(double radius = 6371000.0) {
  return load("plugins/physical/earth/round",
              Config::from_string("radius: " + std::to_string(radius) + "\n", "round-fixture"));
}

EarthModel& load_wgs84() {
  return load("plugins/physical/earth/wgs84_rotating",
              Config::from_string("{}\n", "wgs84-fixture"));
}

// M2 PR 3, NESC check-case 5: `rotating: true` is round Earth's own opt-in to a nonzero
// omega_ie_n, off by default (PLAN.md M2 PR 1's own scope).
EarthModel& load_round_rotating(double radius = 6371000.0) {
  return load("plugins/physical/earth/round",
              Config::from_string("radius: " + std::to_string(radius) + "\nrotating: true\n",
                                  "round-rotating-fixture"));
}

// A level-attitude, wings-level VehicleState at the given latitude/longitude/altitude with body
// velocity v_b: at identity q_nb, rotate_b_to_n is the identity, so v_n == v_b componentwise, which
// keeps every closed form below in terms of v_b's own (u, v, w) directly.
StateVector make_state(double lat, double lon, double alt, const Eigen::Vector3d& v_b) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kPositionSize>(astra::state::kPosition) = Eigen::Vector3d{lat, lon, alt};
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y(astra::state::kQuaternion) = 1.0;  // identity quaternion
  return y;
}

constexpr double kTolerance = 1e-9;

TEST(EarthKinematics, RoundDueNorthMatchesClosedFormAtSeveralLatitudes) {
  EarthModel& earth = load_round();
  constexpr double kRadius = 6371000.0;
  constexpr double kSpeed = 200.0;
  for (const double lat_deg : {-60.0, -30.0, 0.0, 30.0, 60.0}) {
    SCOPED_TRACE(lat_deg);
    const double lat = lat_deg * std::numbers::pi / 180.0;
    const StateVector y = make_state(lat, 0.0, 0.0, {kSpeed, 0.0, 0.0});
    const VehicleState vs{y};

    const Eigen::Vector3d p_dot = earth.p_n_dot(vs);
    EXPECT_NEAR(kSpeed / kRadius, p_dot(0), kTolerance);
    EXPECT_NEAR(0.0, p_dot(1), kTolerance);
    EXPECT_NEAR(0.0, p_dot(2), kTolerance);

    const Eigen::Vector3d omega_en = earth.omega_en_n(vs);
    EXPECT_NEAR(0.0, omega_en(0), kTolerance);
    EXPECT_NEAR(-kSpeed / kRadius, omega_en(1), kTolerance);
    EXPECT_NEAR(0.0, omega_en(2), kTolerance);

    EXPECT_TRUE(earth.omega_ie_n(vs).isZero()) << "round Earth is non-rotating";
  }
}

TEST(EarthKinematics, RoundDueEastMatchesClosedFormAtSeveralLatitudes) {
  EarthModel& earth = load_round();
  constexpr double kRadius = 6371000.0;
  constexpr double kSpeed = 200.0;
  for (const double lat_deg : {-60.0, -30.0, 0.0, 30.0, 60.0}) {
    SCOPED_TRACE(lat_deg);
    const double lat = lat_deg * std::numbers::pi / 180.0;
    const StateVector y = make_state(lat, 0.0, 0.0, {0.0, kSpeed, 0.0});
    const VehicleState vs{y};

    const Eigen::Vector3d p_dot = earth.p_n_dot(vs);
    EXPECT_NEAR(0.0, p_dot(0), kTolerance);
    EXPECT_NEAR(kSpeed / (kRadius * std::cos(lat)), p_dot(1), kTolerance);
    EXPECT_NEAR(0.0, p_dot(2), kTolerance);

    const Eigen::Vector3d omega_en = earth.omega_en_n(vs);
    EXPECT_NEAR(kSpeed / kRadius, omega_en(0), kTolerance);
    EXPECT_NEAR(0.0, omega_en(1), kTolerance);
    EXPECT_NEAR(-kSpeed * std::tan(lat) / kRadius, omega_en(2), kTolerance);
  }
}

TEST(EarthKinematics, RoundClimbRateIsNegativeVD) {
  EarthModel& earth = load_round();
  const StateVector y = make_state(0.3, 0.0, 1000.0, {0.0, 0.0, -5.0});  // v_D = -5 (climbing)
  const Eigen::Vector3d p_dot = earth.p_n_dot(VehicleState{y});
  EXPECT_NEAR(5.0, p_dot(2), kTolerance);  // altitude rate = -v_D = +5
}

TEST(EarthKinematics, RoundAltitudeIsPnThirdComponent) {
  EarthModel& earth = load_round();
  const StateVector y = make_state(0.1, 0.2, 12345.0, Eigen::Vector3d::Zero());
  EXPECT_DOUBLE_EQ(12345.0, earth.altitude(VehicleState{y}));
}

// M2 PR 3: `rotating: true` gives round Earth the same omega_ie_n(lat) formula wgs84_rotating uses
// unconditionally -- depends only on latitude, so the round-vs-ellipsoid distinction (which affects
// p_n_dot/omega_en_n's radius of curvature) does not enter here at all.
TEST(EarthKinematics, RoundRotatingOmegaIeAtEquatorPointsFullyNorth) {
  EarthModel& earth = load_round_rotating();
  const StateVector y = make_state(0.0, 0.0, 0.0, Eigen::Vector3d::Zero());
  const Eigen::Vector3d omega_ie = earth.omega_ie_n(VehicleState{y});
  EXPECT_NEAR(astra::geodesy::kOmegaIe, omega_ie(0), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(1), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(2), kTolerance);
}

TEST(EarthKinematics, RoundRotatingOmegaIeAtMidLatitudeMatchesClosedForm) {
  EarthModel& earth = load_round_rotating();
  const double lat = 45.0 * std::numbers::pi / 180.0;
  const StateVector y = make_state(lat, 0.0, 0.0, Eigen::Vector3d::Zero());
  const Eigen::Vector3d omega_ie = earth.omega_ie_n(VehicleState{y});
  EXPECT_NEAR(astra::geodesy::kOmegaIe * std::cos(lat), omega_ie(0), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(1), kTolerance);
  EXPECT_NEAR(-astra::geodesy::kOmegaIe * std::sin(lat), omega_ie(2), kTolerance);
}

// The default is still non-rotating -- `rotating` defaults to false, matching PLAN.md M2 PR 1's
// original scope (RoundDueNorthMatchesClosedFormAtSeveralLatitudes above already pins this for
// load_round()'s own default; this pins it explicitly against the same fixture the two tests
// above use, so a future default flip is caught here too).
TEST(EarthKinematics, RoundRotatingDefaultsToFalse) {
  EarthModel& earth = load_round();
  const StateVector y = make_state(0.5, 0.0, 0.0, Eigen::Vector3d::Zero());
  EXPECT_TRUE(earth.omega_ie_n(VehicleState{y}).isZero());
}

TEST(EarthKinematics, Wgs84OmegaIeAtEquatorPointsFullyNorth) {
  EarthModel& earth = load_wgs84();
  const StateVector y = make_state(0.0, 0.0, 0.0, Eigen::Vector3d::Zero());
  const Eigen::Vector3d omega_ie = earth.omega_ie_n(VehicleState{y});
  EXPECT_NEAR(astra::geodesy::kOmegaIe, omega_ie(0), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(1), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(2), kTolerance);
}

TEST(EarthKinematics, Wgs84OmegaIeAtMidLatitudeMatchesClosedForm) {
  EarthModel& earth = load_wgs84();
  const double lat = 45.0 * std::numbers::pi / 180.0;
  const StateVector y = make_state(lat, 0.0, 0.0, Eigen::Vector3d::Zero());
  const Eigen::Vector3d omega_ie = earth.omega_ie_n(VehicleState{y});
  EXPECT_NEAR(astra::geodesy::kOmegaIe * std::cos(lat), omega_ie(0), kTolerance);
  EXPECT_NEAR(0.0, omega_ie(1), kTolerance);
  EXPECT_NEAR(-astra::geodesy::kOmegaIe * std::sin(lat), omega_ie(2), kTolerance);
}

TEST(EarthKinematics, Wgs84DueNorthMatchesClosedFormUsingMeridianRadius) {
  EarthModel& earth = load_wgs84();
  constexpr double kSpeed = 200.0;
  const double lat = 30.0 * std::numbers::pi / 180.0;
  const StateVector y = make_state(lat, 0.0, 0.0, {kSpeed, 0.0, 0.0});
  const Eigen::Vector3d p_dot = earth.p_n_dot(VehicleState{y});
  const double m = astra::geodesy::meridian_radius(lat);
  EXPECT_NEAR(kSpeed / m, p_dot(0), kTolerance);
}

TEST(EarthKinematics, RoundValidatePnRejectsADegreeValuedLatitudeNamingAdr0003) {
  EarthModel& earth = load_round();
  const Config node = Config::from_string("{}\n", "degrees-fixture");
  // 1.5 rad (~86 deg) is still a legitimate latitude -- must not be rejected.
  EXPECT_NO_THROW(earth.validate_p_n({1.5, 0.0, 0.0}, node));
  // 45 rad is unmistakably a degree-valued latitude typed into a radians field.
  try {
    earth.validate_p_n({45.0, 0.0, 0.0}, node);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string_view{error.what()}.find("radians, not"), std::string_view::npos)
        << error.what();
  }
}

TEST(EarthKinematics, RoundValidatePnRejectsLatitudeWithinThePolarSingularityMargin) {
  EarthModel& earth = load_round();
  const Config node = Config::from_string("{}\n", "pole-fixture");
  EXPECT_THROW(earth.validate_p_n({std::numbers::pi / 2.0, 0.0, 0.0}, node), ConfigError);
  EXPECT_NO_THROW(earth.validate_p_n({astra::geodesy::kMaxLatitude - 1e-9, 0.0, 0.0}, node));
}

TEST(EarthKinematics, RoundValidatePnRejectsLongitudeOutsidePiRange) {
  EarthModel& earth = load_round();
  const Config node = Config::from_string("{}\n", "longitude-fixture");
  EXPECT_THROW(earth.validate_p_n({0.0, std::numbers::pi + 0.1, 0.0}, node), ConfigError);
  EXPECT_NO_THROW(earth.validate_p_n({0.0, std::numbers::pi - 0.1, 0.0}, node));
}

TEST(EarthKinematics, RoundValidatePnRejectsAltitudeAtOrBelowTheCenter) {
  EarthModel& earth = load_round(1000.0);  // small radius makes the bound easy to hit
  const Config node = Config::from_string("{}\n", "depth-fixture");
  EXPECT_THROW(earth.validate_p_n({0.0, 0.0, -1000.0}, node), ConfigError);
  EXPECT_NO_THROW(earth.validate_p_n({0.0, 0.0, -999.0}, node));
}

// Codex adversarial review: validate_p_n only checks the *initial* state -- a trajectory that
// later crosses the altitude floor or the polar margin is not re-checked mid-run. This proves the
// documented consequence (round.cpp's validate_p_n comment): querying the kinematics themselves at
// or beyond either boundary produces non-finite output, which is exactly what would propagate into
// the integrated state and trip HealthMonitor's existing per-frame NaN/Inf check (M1 PR 4) -- a
// named halt, not silent garbage. No new domain-tracking machinery; this is the same halt-on-NaN
// backstop every model already relies on.
TEST(EarthKinematics, CrossingTheAltitudeFloorMidRunProducesNonFiniteKinematics) {
  EarthModel& earth = load_round(1000.0);
  const StateVector y = make_state(0.0, 0.0, -1000.0, {100.0, 0.0, 0.0});  // exactly at the floor
  EXPECT_FALSE(earth.p_n_dot(VehicleState{y}).allFinite());
  EXPECT_FALSE(earth.omega_en_n(VehicleState{y}).allFinite());
}

// At the pole, std::numbers::pi / 2.0 (a finite double approximation of the irrational pi/2) gives
// cos(lat) ~ 6.1e-17, not exactly zero -- so 1/cos(lat) is an enormous but still *finite* double
// (~1.6e16), not literally Inf/NaN, unlike the altitude-floor case above where radius_ + h hits an
// exact floating-point zero. Documented honestly rather than overclaiming: this magnitude is still
// wildly unphysical (compare kTolerance's ~1e-9 scale used everywhere else in this file) and would
// almost certainly cascade to a non-finite or wildly-drifted quaternion within a frame or two of
// integration, tripping HealthMonitor's NaN/Inf or quaternion-norm-drift check -- but that
// cascade is not asserted here, only the enormous magnitude at the boundary itself.
TEST(EarthKinematics, CrossingThePolarMarginMidRunProducesAnEnormousMagnitude) {
  EarthModel& earth = load_round();
  const StateVector y =
      make_state(std::numbers::pi / 2.0, 0.0, 0.0, {0.0, 100.0, 0.0});  // exactly at the pole
  // libm's cos() near this singularity is not identically rounded across optimization levels
  // (observed 1.63e16 under one preset's math library, 2.56e11 under another's) -- both are
  // wildly unphysical, so the threshold stays far below either, not tuned to one build.
  constexpr double kUnphysicallyLarge = 1e6;
  EXPECT_GT(earth.p_n_dot(VehicleState{y}).cwiseAbs().maxCoeff(), kUnphysicallyLarge);
  EXPECT_TRUE(earth.p_n_dot(VehicleState{y}).allFinite())
      << "finite but enormous, not Inf/NaN -- see comment above";
}

// Codex review finding: M(lat) <= N(lat) everywhere (equal only at the poles), so the bound must be
// the *meridian* radius M(lat), the more restrictive of the two p_n_dot/omega_en_n actually divide
// by -- bounding on N(lat) alone would accept an altitude that already makes (M(lat) + h) negative.
TEST(EarthKinematics, Wgs84ValidatePnUsesTheMeridianRadiusAsItsSurfaceBound) {
  EarthModel& earth = load_wgs84();
  const Config node = Config::from_string("{}\n", "wgs84-depth-fixture");
  const double m_at_equator = astra::geodesy::meridian_radius(0.0);
  const double n_at_equator = astra::geodesy::prime_vertical_radius(0.0);
  ASSERT_LT(m_at_equator, n_at_equator) << "the equator is where M and N differ the most";

  EXPECT_THROW(earth.validate_p_n({0.0, 0.0, -m_at_equator - 1.0}, node), ConfigError);
  EXPECT_NO_THROW(earth.validate_p_n({0.0, 0.0, -m_at_equator + 1.0}, node));

  // The specific regression: an altitude between -N(0) and -M(0) already makes (M(lat) + h)
  // negative and must be rejected, even though it lies inside the (looser, wrong) N(0) bound.
  const double between_m_and_n = -(m_at_equator + n_at_equator) / 2.0;
  EXPECT_THROW(earth.validate_p_n({0.0, 0.0, between_m_and_n}, node), ConfigError);
}

TEST(EarthKinematics, RoundAndWgs84BothProvideGeodeticPositionRepresentation) {
  EXPECT_EQ(astra::PositionRepresentation::kGeodetic, load_round().position_representation());
  EXPECT_EQ(astra::PositionRepresentation::kGeodetic, load_wgs84().position_representation());
}

// ADR-0012: in_domain is the SINGULARITY subset of validate_p_n -- true inside the domain, false
// at each of validate_p_n's own singularity checks, but (unlike validate_p_n) NOT at an
// input-sanity failure (a degree-valued latitude or an out-of-[-pi,pi] longitude), since neither
// makes p_n_dot/omega_en_n's math undefined. This is the split ADR-0012 is built on, so it is
// tested as such: a value validate_p_n rejects but in_domain accepts.
TEST(EarthKinematics, RoundInDomainTrueInsideAndFalseAtEachSingularity) {
  EarthModel& earth = load_round(1000.0);  // small radius, matching earlier radius-floor tests
  EXPECT_TRUE(earth.in_domain({0.3, 0.0, 500.0}));
  EXPECT_FALSE(earth.in_domain({std::numbers::pi / 2.0, 0.0, 0.0}))  // polar margin
      << "at the pole";
  EXPECT_FALSE(earth.in_domain({0.0, 0.0, -1000.0})) << "at the altitude floor";
  EXPECT_FALSE(earth.in_domain({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}))
      << "non-finite";
}

TEST(EarthKinematics, RoundInDomainIsTrueForALongitudeValidatePnStillRejects) {
  EarthModel& earth = load_round();
  const Eigen::Vector3d p_n{0.0, 4.0, 0.0};  // 4 rad is outside [-pi, pi]
  EXPECT_TRUE(earth.in_domain(p_n)) << "longitude range is input sanity, not a domain excursion "
                                       "(ADR-0012) -- must not fire per frame";
  const Config node = Config::from_string("{}\n", "longitude-in-domain-fixture");
  EXPECT_THROW(earth.validate_p_n(p_n, node), ConfigError)
      << "but validate_p_n still rejects it at init, unchanged";
}

TEST(EarthKinematics, Wgs84InDomainTrueInsideAndFalseAtTheMeridianRadiusFloor) {
  EarthModel& earth = load_wgs84();
  const double m_at_equator = astra::geodesy::meridian_radius(0.0);
  EXPECT_TRUE(earth.in_domain({0.0, 0.0, 500.0}));
  EXPECT_FALSE(earth.in_domain({0.0, 0.0, -m_at_equator - 1.0}));
  EXPECT_FALSE(earth.in_domain({astra::geodesy::kMaxLatitude + 1e-3, 0.0, 0.0}));
  // The same regression earth_kinematics_test's own
  // Wgs84ValidatePnUsesTheMeridianRadiusAsItsSurfaceBound proves for validate_p_n: an altitude
  // between -N(0) and -M(0) is out of domain against the more restrictive M(lat) bound, not merely
  // against the looser N(lat) one.
  const double n_at_equator = astra::geodesy::prime_vertical_radius(0.0);
  EXPECT_FALSE(earth.in_domain({0.0, 0.0, -(m_at_equator + n_at_equator) / 2.0}));
}

TEST(EarthKinematics, Wgs84InDomainIsTrueForALongitudeValidatePnStillRejects) {
  EarthModel& earth = load_wgs84();
  const Eigen::Vector3d p_n{0.0, 4.0, 0.0};
  EXPECT_TRUE(earth.in_domain(p_n));
  const Config node = Config::from_string("{}\n", "wgs84-longitude-in-domain-fixture");
  EXPECT_THROW(earth.validate_p_n(p_n, node), ConfigError);
}

}  // namespace
