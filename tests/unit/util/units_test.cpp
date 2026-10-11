// core/util/units.hpp (ADR-0002: "every boundary conversion is a named function in core/util/units
// with a unit test"). Exactness claims below hold specifically because of the implementation pinned
// in units.hpp's own header comment (multiply/divide by the SAME named constant, never a separately
// rounded reciprocal) -- do not weaken that pin to make a failing test pass.
#include "astra/util/units.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

namespace {

using astra::units::degrees_to_radians;
using astra::units::feet_to_meters;
using astra::units::kilograms_to_slugs;
using astra::units::kKilogramsPerSlug;
using astra::units::kMetersPerFoot;
using astra::units::kMetersPerSecondPerKnot;
using astra::units::kNewtonsPerPoundForce;
using astra::units::knots_to_meters_per_second;
using astra::units::kRadiansPerDegree;
using astra::units::meters_per_second_to_knots;
using astra::units::meters_to_feet;
using astra::units::newtons_to_pound_force;
using astra::units::pound_force_to_newtons;
using astra::units::radians_to_degrees;
using astra::units::slug_square_feet_to_kilogram_square_meters;
using astra::units::slugs_to_kilograms;

static_assert(kMetersPerFoot == 0.3048);
static_assert(kMetersPerSecondPerKnot == 1852.0 / 3600.0);

TEST(Units, FeetToMetersMatchesTheDefiningConstantExactly) {
  EXPECT_EQ(feet_to_meters(1.0), 0.3048);
}

// meters_to_feet is implemented as division by kMetersPerFoot (not multiplication by a precomputed
// reciprocal), so meters_to_feet(kMetersPerFoot) is x / x for the same finite nonzero x -- exactly
// 1.0 in IEEE 754, not merely close to it.
TEST(Units, MetersToFeetOfOneFootIsExactlyOne) { EXPECT_EQ(meters_to_feet(0.3048), 1.0); }

// 10000 * 0.3048 is exact in double precision (no rounding: 0.3048 has a short enough mantissa and
// 10000 is a power-of-ten integer well within 53 bits), so this is an exact identity, not a
// near-equality.
TEST(Units, TenThousandFeetIsExactlyThirtyFortyEightMeters) {
  EXPECT_EQ(feet_to_meters(10000.0), 3048.0);
}

// 1 kt asserted against 1852.0/3600.0 computed at the assertion site, not a rounded decimal
// literal: 1852.0/3600.0 == 0.51444444444444..., and a literal like 0.514444 is off by ~4.4e-7,
// which would fail even a 1e-6 tolerance -- exactly the mistake this test exists to catch, not
// commit.
TEST(Units, OneKnotMatchesTheDefiningRatio) {
  EXPECT_EQ(knots_to_meters_per_second(1.0), 1852.0 / 3600.0);
}

TEST(Units, FeetToMetersRoundTripsThroughMetersToFeet) {
  for (const double feet : {0.0, 1.0, 10.0, 1000.0, 86000.0 / 0.3048, -500.0}) {
    EXPECT_NEAR(feet, meters_to_feet(feet_to_meters(feet)), 1e-9);
  }
}

TEST(Units, KnotsRoundTripThroughMetersPerSecond) {
  for (const double knots : {0.0, 1.0, 20.0, 150.0, -30.0}) {
    EXPECT_NEAR(knots, meters_per_second_to_knots(knots_to_meters_per_second(knots)), 1e-9);
  }
}

// --- Knots from SI: a reciprocal guard (M6 PR 4) --------------------------------------------

// kMetersPerSecondPerKnot * (1 / kMetersPerSecondPerKnot) rounds to 1.0, so no x / x identity can
// tell the pinned division from a precomputed reciprocal, and the round trip above is only
// EXPECT_NEAR. 3.0 / kMetersPerSecondPerKnot, rounded once, is ...161p+2 (exact rational arithmetic
// on the constant); 3.0 * (1 / kMetersPerSecondPerKnot) is ...162p+2. Feet need no such pin:
// MetersToFeetOfOneFootIsExactlyOne already fails under a reciprocal (0.3048 * (1 / 0.3048) is
// 0.9999999999999999).
TEST(Units, ThreeMetersPerSecondInKnotsIsTheOnceRoundedQuotient) {
  EXPECT_EQ(meters_per_second_to_knots(3.0), 0x1.7537d830a8161p+2);
}

// --- The angle pair (M6 PR 4, docs/plans/m6-pr4-units.md) ----------------------------------
// Every exact and near claim below was run before it was written here. The oracle for the pinned
// hex values is exact rational arithmetic (Python fractions) on a 50-digit decimal pi, rounded
// once to double, never this header, math.pi or numpy (np.rad2deg multiplies by 180/pi and
// differs from the pinned division for about 12% of inputs).

// The correctly rounded pi/180. std::numbers::pi is itself the correctly rounded pi.
static_assert(kRadiansPerDegree == 0x1.1df46a2529d39p-6);

TEST(Units, DegreesToRadiansKnownValuesAreExact) {
  constexpr double kPi = std::numbers::pi;
  EXPECT_EQ(degrees_to_radians(180.0), kPi);
  EXPECT_EQ(degrees_to_radians(-180.0), -kPi);
  EXPECT_EQ(degrees_to_radians(90.0), kPi / 2.0);
  EXPECT_EQ(degrees_to_radians(45.0), kPi / 4.0);
  EXPECT_EQ(degrees_to_radians(360.0), 2.0 * kPi);
  EXPECT_EQ(degrees_to_radians(1.0), 0x1.1df46a2529d39p-6);
}

TEST(Units, RadiansToDegreesKnownValuesAreExact) {
  constexpr double kPi = std::numbers::pi;
  EXPECT_EQ(radians_to_degrees(kPi), 180.0);
  EXPECT_EQ(radians_to_degrees(kPi / 2.0), 90.0);
  EXPECT_EQ(radians_to_degrees(kPi / 4.0), 45.0);
  EXPECT_EQ(radians_to_degrees(2.0 * kPi), 360.0);
  // 57.29577951308232, the correctly rounded 180/pi.
  EXPECT_EQ(radians_to_degrees(1.0), 0x1.ca5dc1a63c1f8p+5);
}

// x / x for the same finite nonzero double: exactly 1.0, the angle twin of
// MetersToFeetOfOneFootIsExactlyOne.
TEST(Units, RadiansToDegreesOfOneDegreeIsExactlyOne) {
  EXPECT_EQ(radians_to_degrees(kRadiansPerDegree), 1.0);
}

// Near only, not exact: pi/3 and pi/6 come back one ulp below 60 and 30.
TEST(Units, RadiansToDegreesOfPiOverThreeAndSixAreOneUlpShort) {
  constexpr double kPi = std::numbers::pi;
  EXPECT_EQ(radians_to_degrees(kPi / 3.0), std::nextafter(60.0, 0.0));
  EXPECT_EQ(radians_to_degrees(kPi / 6.0), std::nextafter(30.0, 0.0));
}

// One correctly rounded operation by the named constant, pinned where a precomputed reciprocal
// rounds differently: 1.125 / kRadiansPerDegree is ...1bp+6 (1.125 * (180/pi) gives ...1cp+6), and
// 3.0 * kRadiansPerDegree is ...d6p-5 (3.0 / (180/pi) gives ...d5p-5).
TEST(Units, AnglePairIsOneRoundingByTheNamedConstant) {
  EXPECT_EQ(radians_to_degrees(1.125), 0x1.01d4bced81d1bp+6);
  EXPECT_EQ(degrees_to_radians(3.0), 0x1.acee9f37bebd6p-5);
}

// degrees -> radians -> degrees is exact for every integer |d| <= 14, and d = 15 is the first
// integer where it is not (run over [-720, 720]). A precomputed reciprocal in either direction
// breaks the exact range (at 3 or at 13).
TEST(Units, DegreesRoundTripExactlyForEveryIntegerUpToFourteen) {
  for (int d = -14; d <= 14; ++d) {
    const auto degrees = static_cast<double>(d);
    EXPECT_EQ(radians_to_degrees(degrees_to_radians(degrees)), degrees) << "d = " << d;
  }
  EXPECT_NE(radians_to_degrees(degrees_to_radians(15.0)), 15.0);
}

// In general a round trip is near, never more than 1 ulp off (measured over 10^6 seeded values in
// [-1e4, 1e4]: about 8% are 1 ulp off in each direction).
TEST(Units, AnglesRoundTripWithinOneUlp) {
  for (const double degrees : {0.0, 15.0, 29.0, 30.0, 60.0, 123.456, -250.0, 1.0e4, 86000.0}) {
    const double back = radians_to_degrees(degrees_to_radians(degrees));
    EXPECT_NEAR(degrees, back, 1e-9);
    EXPECT_TRUE(back == degrees || back == std::nextafter(degrees, back > degrees ? 1e300 : -1e300))
        << degrees;
  }
  for (const double radians : {0.0, 0.1, 1.0, 1.0471975511965976, -2.5, 100.0}) {
    const double back = degrees_to_radians(radians_to_degrees(radians));
    EXPECT_NEAR(radians, back, 1e-12);
    EXPECT_TRUE(back == radians || back == std::nextafter(radians, back > radians ? 1e300 : -1e300))
        << radians;
  }
}

TEST(Units, AnglePairPassesSpecialValuesThroughIeee) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  EXPECT_TRUE(std::signbit(degrees_to_radians(-0.0)));
  EXPECT_TRUE(std::signbit(radians_to_degrees(-0.0)));
  EXPECT_FALSE(std::signbit(radians_to_degrees(0.0)));
  EXPECT_EQ(degrees_to_radians(kInf), kInf);
  EXPECT_EQ(radians_to_degrees(-kInf), -kInf);
  EXPECT_TRUE(std::isnan(degrees_to_radians(std::numeric_limits<double>::quiet_NaN())));
  EXPECT_TRUE(std::isnan(radians_to_degrees(std::numeric_limits<double>::quiet_NaN())));
  EXPECT_EQ(radians_to_degrees(1e307), kInf);                                     // overflow
  EXPECT_EQ(degrees_to_radians(std::numeric_limits<double>::denorm_min()), 0.0);  // underflow
}

// --- Pound-force and slug (M7 PR 3c, docs/plans/m7-pr3c-altitude-and-units.md, test D) ---------
// Every exact and near claim below was run before it was written here. The oracle for the pinned
// hex values is exact rational arithmetic (Python fractions) on the decimal definitions
// 4.4482216152605 N/lbf and 0.3048 m/ft, rounded once to double, never this header.

static_assert(kNewtonsPerPoundForce == 4.4482216152605);
// The correctly rounded 4.4482216152605/0.3048, and the NESC band generator's SLUG_TO_KG
// (1 / (0.3048 / 4.4482216152605)) bit for bit.
static_assert(kKilogramsPerSlug == 0x1.d30140bb8996fp+3);
static_assert(kKilogramsPerSlug == 1.0 / (0.3048 / 4.4482216152605));

// The disclosure: the plain quotient of the two doubles lands one ulp below the correctly rounded
// value (0.3048 has no exact binary value), so "simplifying" the constant to it fails here.
TEST(Units, ThePlainSlugQuotientIsOneUlpBelowTheCorrectlyRoundedConstant) {
  EXPECT_EQ(4.4482216152605 / 0.3048, std::nextafter(kKilogramsPerSlug, 0.0));
  EXPECT_NE(4.4482216152605 / 0.3048, kKilogramsPerSlug);
}

TEST(Units, PoundForceAndSlugKnownValuesAreExact) {
  EXPECT_EQ(pound_force_to_newtons(1.0), 4.4482216152605);
  EXPECT_EQ(slugs_to_kilograms(1.0), 0x1.d30140bb8996fp+3);
  // The correctly rounded 4.4482216152605 * 0.3048 kg m^2 per slug ft^2.
  EXPECT_EQ(slug_square_feet_to_kilogram_square_meters(1.0), 0x1.5b16e29369c65p+0);
}

// x / x for the same finite nonzero double: exactly 1.0. Neither guards against a precomputed
// reciprocal here, because kNewtonsPerPoundForce * (1 / kNewtonsPerPoundForce) and
// kKilogramsPerSlug * (1 / kKilogramsPerSlug) both round to 1.0; the pins below do.
TEST(Units, PoundForceAndSlugOfTheirOwnConstantAreExactlyOne) {
  EXPECT_EQ(newtons_to_pound_force(kNewtonsPerPoundForce), 1.0);
  EXPECT_EQ(kilograms_to_slugs(kKilogramsPerSlug), 1.0);
}

// The generator's KG_TO_SLUG, 0.3048 / 4.4482216152605, evaluated here: bitwise equal to one
// kilogram in slugs.
TEST(Units, OneKilogramInSlugsIsTheGeneratorsKgToSlug) {
  EXPECT_EQ(kilograms_to_slugs(1.0), 0.3048 / 4.4482216152605);
}

// One correctly rounded division by the named constant, pinned where a precomputed reciprocal
// rounds differently: 39.375 / kNewtonsPerPoundForce is ...97cp+3 (39.375 * (1/c) gives ...97bp+3;
// 39.375 is the first multiple of 1/8 that tells them apart), and 9.0 / kKilogramsPerSlug is
// ...2abp-1 (9.0 * (1/c) gives ...2acp-1; 9 is the first integer that does).
TEST(Units, PoundForceAndSlugAreOneRoundingByTheNamedConstant) {
  EXPECT_EQ(newtons_to_pound_force(39.375), 0x1.1b425f685d97cp+3);
  EXPECT_EQ(kilograms_to_slugs(9.0), 0x1.3bbf90658b2abp-1);
}

// A round trip is near, never more than 1 ulp off (measured over 10^6 seeded values in
// [-1e5, 1e5]: lbf -> N -> lbf is 1 ulp off for 5.8% of them and N -> lbf -> N for 8.6%; slug ->
// kg -> slug for 7.6% and kg -> slug -> kg for 5.2%). Not exact even at integers: 982 lbf is not.
TEST(Units, PoundForceAndSlugRoundTripWithinOneUlp) {
  const auto within_one_ulp = [](double x, double back) {
    return back == x || back == std::nextafter(x, back > x ? 1e300 : -1e300);
  };
  for (const double x : {0.0, 1.0, 982.0, 637.1595, 9496.0, 55814.0, 63100.0, -250.5, 1.0e5}) {
    const double lbf = newtons_to_pound_force(pound_force_to_newtons(x));
    const double n = pound_force_to_newtons(newtons_to_pound_force(x));
    const double slug = kilograms_to_slugs(slugs_to_kilograms(x));
    const double kg = slugs_to_kilograms(kilograms_to_slugs(x));
    EXPECT_NEAR(x, lbf, 1e-9);
    EXPECT_NEAR(x, slug, 1e-9);
    EXPECT_TRUE(within_one_ulp(x, lbf)) << x;
    EXPECT_TRUE(within_one_ulp(x, n)) << x;
    EXPECT_TRUE(within_one_ulp(x, slug)) << x;
    EXPECT_TRUE(within_one_ulp(x, kg)) << x;
  }
  EXPECT_NE(newtons_to_pound_force(pound_force_to_newtons(982.0)), 982.0);
}

TEST(Units, PoundForceAndSlugPassSpecialValuesThroughIeee) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  for (double (*const convert)(double) :
       {&pound_force_to_newtons, &newtons_to_pound_force, &slugs_to_kilograms, &kilograms_to_slugs,
        &slug_square_feet_to_kilogram_square_meters}) {
    EXPECT_TRUE(std::signbit(convert(-0.0)));
    EXPECT_FALSE(std::signbit(convert(0.0)));
    EXPECT_EQ(convert(kInf), kInf);
    EXPECT_EQ(convert(-kInf), -kInf);
    EXPECT_TRUE(std::isnan(convert(kNan)));
  }
}

}  // namespace
