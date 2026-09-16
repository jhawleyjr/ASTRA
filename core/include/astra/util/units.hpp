// core/util/ owns boundary unit conversion the way frames.hpp owns frame/attitude conversions and
// geodesy.hpp owns Earth geometry (§4's "frames, quaternions, geodesy"). ADR-0002: SI doubles
// everywhere inside core/, plugins/, aircraft/; "every boundary conversion is a named function in
// core/util/units with a unit test; nothing multiplies by 0.3048 inline." A config read (init-time,
// may allocate/throw) is exactly such a boundary; the per-frame math on the other side of it stays
// SI throughout and never calls anything in this header.
#pragma once

namespace astra::units {

// The international foot, exact by definition (1959 international yard/pound agreement).
inline constexpr double kMetersPerFoot = 0.3048;
// 1 knot = 1 international nautical mile (1852 m, exact) per hour (3600 s, exact).
inline constexpr double kMetersPerSecondPerKnot = 1852.0 / 3600.0;

// Implementation pinned deliberately as multiplication/division by the SAME named constant in both
// directions (never a separately precomputed reciprocal, which would round once and break bit-exact
// round-tripping): feet_to_meters(meters_to_feet(x)) is exact whenever x/kMetersPerFoot is exact,
// and meters_to_feet(kMetersPerFoot) == 1.0 exactly for any finite nonzero double x (x / x == 1.0).

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

}  // namespace astra::units
