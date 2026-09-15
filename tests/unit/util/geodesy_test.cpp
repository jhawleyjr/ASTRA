// core/util/geodesy.hpp against closed-form identities and known reference points (docs/
// architecture.md §4's "core/util/... geodesy" slot, PLAN.md M2 PR 1). Every expected value here is
// derived from the WGS-84 defining parameters directly, independent of geodesy.hpp's own
// implementation, so these tests judge the header against the ellipsoid geometry, not against
// itself.
#include "astra/util/geodesy.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace {

using astra::geodesy::kEccentricitySquared;
using astra::geodesy::kSemiMajorAxis;

constexpr double kTolerance = 1e-6;

// At the equator (lat = 0), the prime-vertical radius of curvature N(0) is exactly the semi-major
// axis a, and the meridian radius of curvature M(0) is a*(1 - e^2) -- both closed forms, not
// derived from geodesy.hpp's own code.
TEST(Geodesy, RadiiOfCurvatureAtTheEquatorMatchClosedForm) {
  EXPECT_NEAR(kSemiMajorAxis, astra::geodesy::prime_vertical_radius(0.0), kTolerance);
  EXPECT_NEAR(kSemiMajorAxis * (1.0 - kEccentricitySquared), astra::geodesy::meridian_radius(0.0),
              kTolerance);
}

// At the pole, an ellipsoid of revolution's two principal curvatures coincide: M(pi/2) == N(pi/2)
// == a / sqrt(1 - e^2), the polar radius of curvature.
TEST(Geodesy, RadiiOfCurvatureCoincideAtThePole) {
  const double lat = std::numbers::pi / 2.0;
  const double expected = kSemiMajorAxis / std::sqrt(1.0 - kEccentricitySquared);
  EXPECT_NEAR(expected, astra::geodesy::meridian_radius(lat), kTolerance);
  EXPECT_NEAR(expected, astra::geodesy::prime_vertical_radius(lat), kTolerance);
}

// At 45 deg, both radii lie strictly between their equatorial and polar values (a monotonic
// transition, not a closed-form number, but a real property either radius formula could violate
// with a sign or exponent error).
TEST(Geodesy, RadiiOfCurvatureAt45DegreesLieBetweenEquatorAndPole) {
  const double m45 = astra::geodesy::meridian_radius(std::numbers::pi / 4.0);
  const double n45 = astra::geodesy::prime_vertical_radius(std::numbers::pi / 4.0);
  const double m0 = astra::geodesy::meridian_radius(0.0);
  const double n0 = astra::geodesy::prime_vertical_radius(0.0);
  const double m90 = astra::geodesy::meridian_radius(std::numbers::pi / 2.0);
  const double n90 = astra::geodesy::prime_vertical_radius(std::numbers::pi / 2.0);
  EXPECT_GT(m45, m0);
  EXPECT_LT(m45, m90);
  EXPECT_GT(n45, n0);
  EXPECT_LT(n45, n90);
}

// geodetic_to_ecef at the equator, on the ellipsoid, at longitude 0 and 90 deg: two textbook
// reference points with no free parameters.
TEST(Geodesy, GeodeticToEcefAtKnownEquatorialReferencePoints) {
  const Eigen::Vector3d at_greenwich = astra::geodesy::geodetic_to_ecef(0.0, 0.0, 0.0);
  EXPECT_NEAR(kSemiMajorAxis, at_greenwich(0), kTolerance);
  EXPECT_NEAR(0.0, at_greenwich(1), kTolerance);
  EXPECT_NEAR(0.0, at_greenwich(2), kTolerance);

  const Eigen::Vector3d at_90east =
      astra::geodesy::geodetic_to_ecef(0.0, std::numbers::pi / 2.0, 0.0);
  EXPECT_NEAR(0.0, at_90east(0), kTolerance);
  EXPECT_NEAR(kSemiMajorAxis, at_90east(1), kTolerance);
  EXPECT_NEAR(0.0, at_90east(2), kTolerance);
}

// geodetic_to_ecef at the pole: x = y = 0 (cos(lat) = 0), z = a*sqrt(1 - e^2), the WGS-84
// semi-minor axis b (published value ~6356752.314245 m) -- a known reference value, not derived
// from this header.
TEST(Geodesy, GeodeticToEcefAtThePoleGivesTheSemiMinorAxis) {
  const Eigen::Vector3d at_pole =
      astra::geodesy::geodetic_to_ecef(std::numbers::pi / 2.0, 0.3, 0.0);
  EXPECT_NEAR(0.0, at_pole(0), kTolerance);
  EXPECT_NEAR(0.0, at_pole(1), kTolerance);
  constexpr double kPublishedSemiMinorAxis = 6356752.314245;
  EXPECT_NEAR(kPublishedSemiMinorAxis, at_pole(2), 1e-3);
}

// geodetic_to_ecef with nonzero altitude adds exactly altitude along the local "up" direction at
// the equator (where up is purely radial along x): a direct check of the +altitude term.
TEST(Geodesy, GeodeticToEcefAddsAltitudeAlongLocalUpAtTheEquator) {
  const Eigen::Vector3d sea_level = astra::geodesy::geodetic_to_ecef(0.0, 0.0, 0.0);
  const Eigen::Vector3d at_1000m = astra::geodesy::geodetic_to_ecef(0.0, 0.0, 1000.0);
  EXPECT_NEAR(sea_level(0) + 1000.0, at_1000m(0), kTolerance);
}

// ecef_to_ned_dcm's rows (North, East, Down) must be an orthonormal basis at every latitude and
// longitude, exactly what a rotation matrix requires.
TEST(Geodesy, EcefToNedDcmIsOrthonormal) {
  for (const double lat_deg : {-89.0, -45.0, 0.0, 30.0, 89.0}) {
    for (const double lon_deg : {-170.0, -30.0, 0.0, 90.0, 179.0}) {
      SCOPED_TRACE(std::to_string(lat_deg) + "," + std::to_string(lon_deg));
      const double lat = lat_deg * std::numbers::pi / 180.0;
      const double lon = lon_deg * std::numbers::pi / 180.0;
      const Eigen::Matrix3d c_ne = astra::geodesy::ecef_to_ned_dcm(lat, lon);
      const Eigen::Matrix3d should_be_identity = c_ne * c_ne.transpose();
      EXPECT_TRUE(should_be_identity.isApprox(Eigen::Matrix3d::Identity(), 1e-9))
          << should_be_identity;
    }
  }
}

// At the equator on the Greenwich meridian, Down points along -x (toward the center), North along
// +z (toward the pole), East along +y: three unit vectors read straight off the geometry.
TEST(Geodesy, EcefToNedDcmAtTheEquatorGreenwichMeridianMatchesGeometry) {
  const Eigen::Matrix3d c_ne = astra::geodesy::ecef_to_ned_dcm(0.0, 0.0);
  const Eigen::Vector3d north = c_ne.row(0);
  const Eigen::Vector3d east = c_ne.row(1);
  const Eigen::Vector3d down = c_ne.row(2);
  EXPECT_TRUE(north.isApprox(Eigen::Vector3d{0.0, 0.0, 1.0}, kTolerance));
  EXPECT_TRUE(east.isApprox(Eigen::Vector3d{0.0, 1.0, 0.0}, kTolerance));
  EXPECT_TRUE(down.isApprox(Eigen::Vector3d{-1.0, 0.0, 0.0}, kTolerance));
}

TEST(Geodesy, KMaxLatitudeIsJustInsideTheRightAngle) {
  EXPECT_LT(astra::geodesy::kMaxLatitude, std::numbers::pi / 2.0);
  EXPECT_GT(astra::geodesy::kMaxLatitude, std::numbers::pi / 2.0 - 1e-5);
}

}  // namespace
