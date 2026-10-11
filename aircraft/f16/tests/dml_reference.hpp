// A second transcription of F16_aero.dml's build-up (docs/plans/m7-f16-inventory.md section 2), in
// the DML's own units: degrees, ft/s, rad/s for the rates (docs/plans/m7-pr6b-f16-aircraft.md,
// critique F-1). F16Aero.SiRealizationMatchesTheDmlUnitsReference checks the plugin's SI
// realization (physical/aero/aero_math.hpp) against it on seeded random inputs, so a unit
// conversion error in the SI port shows even where no checkData shot reaches.
//
// It shares no code with aero_math.hpp or common/f16_interp.hpp: its interpolation clamps the input
// first and searches with std::upper_bound, over the generated header's verbatim degree
// breakpoints. It shares the generated table values, which tests/python/test_f16_generated.py and
// the checkData shots check separately. Test code only; not on any per-frame path.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "../physical/aero/aero_tables.hpp"

namespace f16_dml_reference {

namespace tables = astra::aircraft_f16_aero_tables;

struct Inputs {
  double vt = 0.0;     // ft/s
  double alpha = 0.0;  // deg
  double beta = 0.0;   // deg
  double p = 0.0;      // rad/s
  double q = 0.0;
  double r = 0.0;
  double el = 0.0;   // deg, +TED
  double ail = 0.0;  // deg, +LWD
  double rdr = 0.0;  // deg, +TEL
};

struct Coefficients {
  double cx = 0.0;
  double cy = 0.0;
  double cz = 0.0;
  double cl = 0.0;
  double cm = 0.0;
  double cn = 0.0;
};

// Clamp to the breakpoints' range, then interpolate linearly: DAVE-ML "neither" as the Simulink
// realization's clip.
template <std::size_t N>
void locate(const std::array<double, N>& breakpoints, double x, std::size_t& lower,
            double& fraction) {
  const double clamped = std::clamp(x, breakpoints.front(), breakpoints.back());
  const auto above = std::upper_bound(breakpoints.begin(), breakpoints.end(), clamped);
  const auto position = static_cast<std::size_t>(above - breakpoints.begin());
  lower = std::min(std::max<std::size_t>(position, 1), N - 1) - 1;
  fraction = (clamped - breakpoints[lower]) / (breakpoints[lower + 1] - breakpoints[lower]);
}

template <std::size_t N>
double table1(const std::array<double, N>& breakpoints, const std::array<double, N>& values,
              double x) {
  std::size_t i = 0;
  double f = 0.0;
  locate(breakpoints, x, i, f);
  return values[i] + f * (values[i + 1] - values[i]);
}

template <std::size_t Rows, std::size_t Columns>
double table2(const std::array<double, Rows>& row_breakpoints,
              const std::array<double, Columns>& column_breakpoints,
              const std::array<std::array<double, Columns>, Rows>& values, double row_x,
              double column_x) {
  std::size_t i = 0;
  double fi = 0.0;
  std::size_t j = 0;
  double fj = 0.0;
  locate(row_breakpoints, row_x, i, fi);
  locate(column_breakpoints, column_x, j, fj);
  const double at_i = values[i][j] + fj * (values[i][j + 1] - values[i][j]);
  const double at_next = values[i + 1][j] + fj * (values[i + 1][j + 1] - values[i + 1][j]);
  return at_i + fi * (at_next - at_i);
}

inline Coefficients evaluate(const Inputs& in) {
  // NOLINTNEXTLINE(modernize-use-std-numbers): the DML's truncated pi (F16_aero.dml:355-366).
  const double rtd = 180.0 / 3.14159265;
  const double cbar = 11.32;
  const double bspan = 30.0;
  const double vt = std::max(in.vt, 0.1);

  const double del = in.el / 25.0;
  const double dail = in.ail / 20.0;
  const double drdr = in.rdr / 30.0;

  const double cxt = table2(tables::kDe1, tables::kAlpha1, tables::kCxt, in.el, in.alpha);
  const double cy0 = -0.02 * in.beta + 0.021 * dail + 0.086 * drdr;
  const double czt = table1(tables::kAlpha1, tables::kCzt, in.alpha);
  const double cz1 = czt * (1.0 - std::pow(in.beta / rtd, 2)) - 0.19 * del;

  const double tvt = 2.0 * vt;
  const double b2v = bspan / tvt;
  const double cq2v = cbar * in.q / tvt;

  const double absbeta = std::abs(in.beta);
  const double abs_cl0 =
      table2(tables::kBeta1, tables::kAlpha1, tables::kAbsCl0, absbeta, in.alpha);
  const double abs_cn0 =
      table2(tables::kBeta1, tables::kAlpha1, tables::kAbsCn0, absbeta, in.alpha);
  const double clt = in.beta < 0.0 ? -abs_cl0 : abs_cl0;
  const double cnt = in.beta < 0.0 ? -abs_cn0 : abs_cn0;
  const double cmt = table2(tables::kDe1, tables::kAlpha1, tables::kCmt, in.el, in.alpha);

  const double dclda = table2(tables::kBeta2, tables::kAlpha1, tables::kDclda, in.beta, in.alpha);
  const double dcldr = table2(tables::kBeta2, tables::kAlpha1, tables::kDcldr, in.beta, in.alpha);
  const double dcnda = table2(tables::kBeta2, tables::kAlpha1, tables::kDcnda, in.beta, in.alpha);
  const double dcndr = table2(tables::kBeta2, tables::kAlpha1, tables::kDcndr, in.beta, in.alpha);

  Coefficients c;
  c.cx = cxt + cq2v * table1(tables::kAlpha1, tables::kCxq, in.alpha);
  c.cy = cy0 + b2v * (table1(tables::kAlpha1, tables::kCyp, in.alpha) * in.p +
                      table1(tables::kAlpha1, tables::kCyr, in.alpha) * in.r);
  c.cz = cz1 + cq2v * table1(tables::kAlpha1, tables::kCzq, in.alpha);
  c.cl = clt + dclda * dail + dcldr * drdr +
         b2v * (table1(tables::kAlpha1, tables::kClp, in.alpha) * in.p +
                table1(tables::kAlpha1, tables::kClr, in.alpha) * in.r);
  c.cm = cmt + cq2v * table1(tables::kAlpha1, tables::kCmq, in.alpha);
  c.cn = cnt + dcnda * dail + dcndr * drdr +
         b2v * (table1(tables::kAlpha1, tables::kCnp, in.alpha) * in.p +
                table1(tables::kAlpha1, tables::kCnr, in.alpha) * in.r);
  return c;
}

}  // namespace f16_dml_reference
