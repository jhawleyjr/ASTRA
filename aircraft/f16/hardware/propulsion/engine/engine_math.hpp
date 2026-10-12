// The F-16 engine's thrust, hand-ported from F16_prop.dml (NASA's DAVE-ML F-16 propulsion model;
// docs/plans/m7-f16-inventory.md section 3) as an SI realization (docs/plans/
// m7-pr6c-f16-mass-propulsion.md, with the design in docs/plans/m7-pr6b-f16-aircraft.md,
// "Propulsion"; ADR-0002).
//
// The file's three tables, T_IDLE, T_MIL and T_MAX, give thrust in lbf over Mach (MACH_PTS, rows)
// and altitude (ALT_PTS, ft, columns; :257-327). The generated engine_tables.hpp keeps their text
// verbatim, and this header derives SI arrays from it at compile time: the altitude breakpoints
// through feet_to_meters, the thrust values through pound_force_to_newtons. Mach needs no
// conversion.
//
// The power-lever build-up (:107-177) keeps the file's operation order:
// - PWR < MIL_PWR: FEX = T_IDLE + PWR * (T_MIL - T_IDLE) / MIL_PWR;
// - otherwise:     FEX = T_MIL + (PWR - MIL_PWR) * (T_MAX - T_MIL) / (100 - MIL_PWR).
// Only the table lookups clip (f16_interp.hpp), at Mach 0 and 1 and at 0 and 50,000 ft. PWR has no
// minValue or maxValue in the file (:44-49), so it is not saturated: a PWR outside 0 to 100
// extrapolates the build-up (PR 6b's decision 4).
//
// The file's thrust lies along the body x axis, and its other five outputs (FEY, FEZ, TEL, TEM,
// TEN) are zero: no moment (D4). The model has no state (D5), so ADR-0021 has nothing to own.
//
// Pure functions of their arguments: no Config, no VehicleState, no allocation, no exceptions.
#pragma once

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <limits>

#include "../../../common/f16_interp.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/util/air_data.hpp"
#include "astra/util/units.hpp"
#include "engine_tables.hpp"

namespace astra::aircraft_f16_engine_math {

namespace tables = astra::aircraft_f16_engine_tables;
namespace interp = astra::aircraft_f16_interp;
namespace units = astra::units;

// NOLINTBEGIN(readability-magic-numbers): each literal below is a constant of F16_prop.dml, cited
// on its line.

// MIL_PWR (:72-76): the power lever angle of military (maximum dry) thrust, percent.
inline constexpr double kMilPwr = 50.0;
// The power lever angle of maximum (afterburning) thrust, percent: the <cn>100.0</cn> in FEX's
// second branch (:165).
inline constexpr double kMaxPwr = 100.0;

// NOLINTEND(readability-magic-numbers)

namespace detail {

template <std::size_t N>
constexpr std::array<double, N> to_meters(const std::array<double, N>& feet) {
  std::array<double, N> meters{};
  for (std::size_t i = 0; i < N; ++i) {
    meters[i] = units::feet_to_meters(feet[i]);
  }
  return meters;
}

template <std::size_t Rows, std::size_t Columns>
constexpr std::array<std::array<double, Columns>, Rows> to_newtons(
    const std::array<std::array<double, Columns>, Rows>& pound_force) {
  std::array<std::array<double, Columns>, Rows> newtons{};
  for (std::size_t row = 0; row < Rows; ++row) {
    for (std::size_t column = 0; column < Columns; ++column) {
      newtons[row][column] = units::pound_force_to_newtons(pound_force[row][column]);
    }
  }
  return newtons;
}

}  // namespace detail

// The breakpoints: Mach as the file states it (:237), altitude in m (:228).
inline constexpr std::array<double, 6> kMachPts = tables::kMachPts;
inline constexpr std::array<double, 6> kAltPts = detail::to_meters(tables::kAltPts);

// The thrust tables in N, rows on Mach and columns on altitude (:257, :281, :305).
inline constexpr std::array<std::array<double, 6>, 6> kTIdle = detail::to_newtons(tables::kTIdle);
inline constexpr std::array<std::array<double, 6>, 6> kTMil = detail::to_newtons(tables::kTMil);
inline constexpr std::array<std::array<double, 6>, 6> kTMax = detail::to_newtons(tables::kTMax);

// The file's three inputs (:44-66), in SI. The plugin boundary (engine.cpp) forms each one.
struct Inputs {
  double pwr = 0.0;       // power lever angle, percent: 50 is MIL, 100 is MAX
  double altitude = 0.0;  // m, EnvironmentSample::altitude_m (ADR-0033)
  double mach = 0.0;      // nd
};

[[nodiscard]] inline bool finite(const Inputs& in) noexcept {
  return astra::air_data::all_finite({in.pwr, in.altitude, in.mach});
}

// FEX in N, from the three table lookups and the build-up.
[[nodiscard]] inline double thrust(const Inputs& in) noexcept {
  const double t_idle = interp::lookup(kMachPts, kAltPts, kTIdle, in.mach, in.altitude);
  const double t_mil = interp::lookup(kMachPts, kAltPts, kTMil, in.mach, in.altitude);
  const double t_max = interp::lookup(kMachPts, kAltPts, kTMax, in.mach, in.altitude);
  if (in.pwr < kMilPwr) {
    return t_idle + in.pwr * (t_mil - t_idle) / kMilPwr;
  }
  return t_mil + (in.pwr - kMilPwr) * (t_max - t_mil) / (kMaxPwr - kMilPwr);
}

// The thrust along the body x axis, with no moment (D4).
[[nodiscard]] inline astra::Wrench wrench(double thrust_n) noexcept {
  astra::Wrench out;
  out.force_b = Eigen::Vector3d(thrust_n, 0.0, 0.0);
  return out;
}

// ADR-0018's sentinel, as the aero's: every component NaN.
[[nodiscard]] inline astra::Wrench non_finite_wrench() noexcept {
  astra::Wrench out;
  out.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  out.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return out;
}

}  // namespace astra::aircraft_f16_engine_math
