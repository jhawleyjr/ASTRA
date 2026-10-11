// Linear table lookup with DAVE-ML's extrapolate="neither" read as clip, shared by the F-16's aero
// and engine plugins (docs/plans/m7-pr6b-f16-aircraft.md, Q15). Every F-16 table is
// extrapolate="neither" in P1, and the package's own Simulink realization implements that as a
// 'Linear' PreLookup feeding an Interpolation_n-D block with 'ExtrapMethod','clip'
// (F16_aero_create.m:94-123; F16_prop_create.m:49-55): past an end, the lookup holds the table's
// value at that end. docs/plans/m7-f16-inventory.md sections 2 and 3 record the reading.
//
// This directory has no CMakeLists.txt, so it is not a plugin; both plugins and the tests include
// this header by relative path. Its namespace is aircraft-specific, as the template's math headers'
// are, so the static-plugins build links it beside any other aircraft's helpers (ODR).
//
// Per-frame path: no allocation, no exceptions. A NaN input gives a NaN value, never an edge value.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace astra::aircraft_f16_interp {

// The segment [breakpoints[index], breakpoints[index + 1]] and the fraction along it.
struct Bracket {
  std::size_t index = 0;
  double fraction = 0.0;
};

// Breakpoints strictly increasing (the generator refuses any other set). Below the first, the
// fraction is 0 at segment 0; at or above the last, it is 1 at the last segment: the clip.
template <std::size_t N>
[[nodiscard]] Bracket bracket(const std::array<double, N>& breakpoints, double x) noexcept {
  static_assert(N >= 2, "a table axis needs at least two breakpoints");
  if (std::isnan(x)) {
    return {.index = 0, .fraction = x};
  }
  if (x <= breakpoints[0]) {
    return {.index = 0, .fraction = 0.0};
  }
  if (x >= breakpoints[N - 1]) {
    return {.index = N - 2, .fraction = 1.0};
  }
  std::size_t index = 0;
  while (x >= breakpoints[index + 1]) {
    ++index;
  }
  return {.index = index,
          .fraction = (x - breakpoints[index]) / (breakpoints[index + 1] - breakpoints[index])};
}

// (1 - f) * a + f * b, which is exactly a at f = 0 and exactly b at f = 1, so a clipped lookup
// returns the edge entry bit for bit.
[[nodiscard]] inline double blend(double a, double b, double fraction) noexcept {
  return (1.0 - fraction) * a + fraction * b;
}

template <std::size_t N>
[[nodiscard]] double lookup(const std::array<double, N>& breakpoints,
                            const std::array<double, N>& table, double x) noexcept {
  const Bracket at = bracket(breakpoints, x);
  return blend(table[at.index], table[at.index + 1], at.fraction);
}

// table[row][column], rows on the first breakpoint set: the file's "last breakpoint changes most
// rapidly" order, which the generator keeps.
template <std::size_t Rows, std::size_t Columns>
[[nodiscard]] double lookup(const std::array<double, Rows>& row_breakpoints,
                            const std::array<double, Columns>& column_breakpoints,
                            const std::array<std::array<double, Columns>, Rows>& table,
                            double row_x, double column_x) noexcept {
  const Bracket row = bracket(row_breakpoints, row_x);
  const Bracket column = bracket(column_breakpoints, column_x);
  const double lower =
      blend(table[row.index][column.index], table[row.index][column.index + 1], column.fraction);
  const double upper = blend(table[row.index + 1][column.index],
                             table[row.index + 1][column.index + 1], column.fraction);
  return blend(lower, upper, row.fraction);
}

}  // namespace astra::aircraft_f16_interp
