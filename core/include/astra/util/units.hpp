// core/util/ owns boundary unit conversion the way frames.hpp owns frame/attitude conversions and
// geodesy.hpp owns Earth geometry (§4's "frames, quaternions, geodesy"). ADR-0002: SI doubles
// everywhere inside core/, plugins/, aircraft/; "every boundary conversion is a named function in
// core/util/units with a unit test; nothing multiplies by 0.3048 inline." A config read (init-time,
// may allocate/throw) is exactly such a boundary; the per-frame math on the other side of it stays
// SI throughout and never calls anything in this header.
#pragma once

#include <numbers>

namespace astra::units {

// The international foot, exact by definition (1959 international yard/pound agreement).
inline constexpr double kMetersPerFoot = 0.3048;
// 1 knot = 1 international nautical mile (1852 m, exact) per hour (3600 s, exact).
inline constexpr double kMetersPerSecondPerKnot = 1852.0 / 3600.0;
// The SI radian per degree. std::numbers::pi is the correctly rounded pi, and this quotient is the
// correctly rounded pi/180, 0x1.1df46a2529d39p-6 (checked against a 50-digit pi in exact rational
// arithmetic; units_test.cpp pins it).
inline constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
// The international pound-force, exact by definition: 4.4482216152605 N. It is the denominator of
// Vol II Table 73's "kilogram to slug" (NASA/TM-2015-218675, p.93) and the NESC band generator's
// LBF_TO_N (tests/golden/nesc/generate_nesc_band.py).
inline constexpr double kNewtonsPerPoundForce = 4.4482216152605;
// One slug is one lbf s^2/ft, so this is the reciprocal of Table 73's "kilogram to slug",
// 0.3048/4.4482216152605 (the generator's KG_TO_SLUG), written in the named constants so no literal
// repeats. It is the correctly rounded 4.4482216152605/0.3048, 0x1.d30140bb8996fp+3 (checked in
// exact rational arithmetic; units_test.cpp pins it), and the generator's SLUG_TO_KG bit for bit.
// The plain quotient of the two doubles, kNewtonsPerPoundForce / kMetersPerFoot, lands one ulp
// below, because 0.3048 has no exact binary value.
inline constexpr double kKilogramsPerSlug = 1.0 / (kMetersPerFoot / kNewtonsPerPoundForce);

// Implementation pinned deliberately as multiplication/division by the SAME named constant in both
// directions (never a separately precomputed reciprocal, which would round once and break bit-exact
// round-tripping): feet_to_meters(meters_to_feet(x)) is exact whenever x/kMetersPerFoot is exact,
// and meters_to_feet(kMetersPerFoot) == 1.0 exactly for any finite nonzero double x (x / x == 1.0).
// The angle pair follows the same pin: degrees_to_radians multiplies by kRadiansPerDegree and
// radians_to_degrees divides by it, never by a separately rounded 180/pi. The pound-force and slug
// pairs follow the same pin. Which identities that makes exact, and which only near, is listed and
// tested in units_test.cpp.

// constexpr on a function already implies inline (readability-redundant-inline-specifier) -- unlike
// the variables above, which still need it explicitly to avoid an ODR violation across TUs.
[[nodiscard]] constexpr double feet_to_meters(double feet) noexcept {
  return feet * kMetersPerFoot;
}

[[nodiscard]] constexpr double meters_to_feet(double meters) noexcept {
  return meters / kMetersPerFoot;
}

[[nodiscard]] constexpr double knots_to_meters_per_second(double knots) noexcept {
  return knots * kMetersPerSecondPerKnot;
}

[[nodiscard]] constexpr double meters_per_second_to_knots(double meters_per_second) noexcept {
  return meters_per_second / kMetersPerSecondPerKnot;
}

[[nodiscard]] constexpr double degrees_to_radians(double degrees) noexcept {
  return degrees * kRadiansPerDegree;
}

[[nodiscard]] constexpr double radians_to_degrees(double radians) noexcept {
  return radians / kRadiansPerDegree;
}

[[nodiscard]] constexpr double pound_force_to_newtons(double pound_force) noexcept {
  return pound_force * kNewtonsPerPoundForce;
}

[[nodiscard]] constexpr double newtons_to_pound_force(double newtons) noexcept {
  return newtons / kNewtonsPerPoundForce;
}

[[nodiscard]] constexpr double slugs_to_kilograms(double slugs) noexcept {
  return slugs * kKilogramsPerSlug;
}

[[nodiscard]] constexpr double kilograms_to_slugs(double kilograms) noexcept {
  return kilograms / kKilogramsPerSlug;
}

// A moment or product of inertia. The parenthesized product of the named constants folds at
// compile time to 0x1.5b16e29369c65p+0 (the correctly rounded 4.4482216152605 * 0.3048), so each
// call is one rounding by one value and no new literal appears. No inverse: inertia only comes in.
[[nodiscard]] constexpr double slug_square_feet_to_kilogram_square_meters(
    double slug_square_feet) noexcept {
  return slug_square_feet * (kKilogramsPerSlug * kMetersPerFoot * kMetersPerFoot);
}

}  // namespace astra::units
