// Plugin-side "unit-tested with no simulator" pattern (docs/plans/pr4-notional-aero-turbofan.md,
// ADR-0020) -- see aircraft/notional_transport/physical/aero/aero_math.hpp's own header comment for
// the full rationale, not repeated here. Identical between aircraft/notional_transport and
// aircraft/_template; only each aircraft's own config supplies the Coefficients values.
//
// Linear thrust-lapse law (density-ratio only, no Mach/ram term, no spool dynamics -- docs/plans/
// pr4-notional-aero-turbofan.md's own "Worked numbers"/Open item 1): T = throttle * thrust_ref_n *
// (rho / density_ref_kgm3). Has no true_airspeed_mps dependence at all, so it is trivially finite
// at V=0 -- no algebraic V->0 fix is needed here the way aero_math.hpp's rate-damping terms need
// one.
#pragma once

#include <Eigen/Core>
#include <limits>

#include "astra/interfaces/wrench.hpp"
#include "astra/util/air_data.hpp"

namespace astra::aircraft_notional_transport_turbofan_math {

// Assembled by wrench_b() from EnvironmentSample plus this plugin's own bound throttle input.
struct Inputs {
  double throttle = 0.0;       // 0..1, dimensionless
  double density_kg_m3 = 0.0;  // raw atmosphere field this model actually reads
};

// Config-resolved constants (docs/plans/pr4-notional-aero-turbofan.md's own "Config keys" section).
struct Coefficients {
  Eigen::Vector3d r_engine_b =
      Eigen::Vector3d::Zero();  // engine station relative to CG, body axes, m
  double thrust_ref_n = 0.0;
  double density_ref_kgm3 = 0.0;
};

// R-001 (docs/reviews/pr3-aero-propulsion-review.md): the general lesson (validate the raw field
// your own math actually consumes, not only what you derived from it) applies to density_kg_m3, the
// one raw atmosphere field this model reads -- checked directly here, same convention aero_math.hpp
// uses, even though this model's own thrust law has no derived quantity that could mask a broken
// density_kg_m3 (T is linear in density_kg_m3, so corruption propagates visibly, never masked).
// Core-side HealthMonitor::check_environment (ADR-0020) already halts on this before wrench_b()
// runs; this check is this plugin's own second, redundant guarantee.
[[nodiscard]] inline bool finite(const Inputs& in) noexcept {
  return astra::air_data::all_finite({in.throttle, in.density_kg_m3});
}

[[nodiscard]] inline astra::Wrench non_finite_wrench() noexcept {
  astra::Wrench wrench;
  wrench.force_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  wrench.moment_b.setConstant(std::numeric_limits<double>::quiet_NaN());
  return wrench;
}

[[nodiscard]] inline astra::Wrench compute_wrench(const Inputs& in,
                                                  const Coefficients& c) noexcept {
  const double thrust_n = in.throttle * c.thrust_ref_n * (in.density_kg_m3 / c.density_ref_kgm3);
  const Eigen::Vector3d force_b(thrust_n, 0.0, 0.0);  // thrust line parallel to body X

  astra::Wrench wrench;
  wrench.force_b = force_b;
  wrench.moment_b = c.r_engine_b.cross(force_b);
  return wrench;
}

}  // namespace astra::aircraft_notional_transport_turbofan_math
