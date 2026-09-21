// Regression fixture (docs/plans/pr4-notional-aero-turbofan.md; Joe's review, real finding):
// simulates a second, independently-edited aircraft's own aero_math.hpp -- a plugin-local
// Inputs/Coefficients pair and a pure compute_wrench(), the same shape
// aircraft/notional_transport/physical/aero/ aero_math.hpp uses, but in its own distinct namespace
// with a deliberately different (fixed, input-independent) answer. Under ASTRA_STATIC_PLUGINS every
// plugin's object file links into one binary with no per-plugin visibility boundary (unlike the
// shared-object presets' own -fvisibility=hidden/-fvisibility-inlines-hidden), so two
// aircraft-specific math headers sharing one namespace would be a silent One Definition Rule
// violation the linker resolves by picking one definition for both --
// tests/unit/aero/aero_math_namespace_isolation_test.cpp loads this fixture alongside the real
// aircraft/notional_transport plugin in one process and checks each produces its own distinct,
// correct answer, proving that does not happen.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/wrench.hpp"

namespace astra::odr_check_aero_math {

struct Inputs {
  double unused = 0.0;
};

struct Coefficients {
  double unused = 0.0;
};

[[nodiscard]] inline astra::Wrench compute_wrench(const Inputs& /*in*/,
                                                  const Coefficients& /*c*/) noexcept {
  astra::Wrench wrench;
  wrench.force_b = Eigen::Vector3d(1.0, 2.0, 3.0);
  wrench.moment_b = Eigen::Vector3d(4.0, 5.0, 6.0);
  return wrench;
}

}  // namespace astra::odr_check_aero_math
