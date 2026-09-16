// core/util/units.hpp (ADR-0002: "every boundary conversion is a named function in core/util/units
// with a unit test"). Exactness claims below hold specifically because of the implementation pinned
// in units.hpp's own header comment (multiply/divide by the SAME named constant, never a separately
// rounded reciprocal) -- do not weaken that pin to make a failing test pass.
#include "astra/util/units.hpp"

#include <gtest/gtest.h>

namespace {

using astra::units::feet_to_meters;
using astra::units::kMetersPerFoot;
using astra::units::kMetersPerSecondPerKnot;
using astra::units::knots_to_meters_per_second;
using astra::units::meters_per_second_to_knots;
using astra::units::meters_to_feet;

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

}  // namespace
