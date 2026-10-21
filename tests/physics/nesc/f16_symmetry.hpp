// M7b PR 4's oracle (c), the F-16's mirror and heading symmetry under F16_control
// (docs/plans/m7b-pr4-f16-symmetry.md): the one trim result and the oracle's config text, built
// here from committed text, never from a tracked F-16 config (PR 11a's tracked-config guard holds).
//
// The IC (section 5; Q9, C-01): docs/reviews/evidence/m7b-pr4-trim/trim_n_frame.out, astra trim's
// n_frame solution of base.yaml.in beside it (case 11's F-16 at case 11's latitude, longitude 0,
// 10,013 ft and heading 0, on a non-rotating sphere), recorded 2026-10-20. The config writes the
// trim's own text for the elevator, the throttle, theta, u, w and q, and a literal 0.0 for phi,
// psi, v, p, r and lon.
//
// Test-only code: it allocates and throws.
#pragma once

#include <string>
#include <string_view>

namespace astra::test::nesc::f16_symmetry {

// Section 5's literals, each the text trim_n_frame.out prints (repr).
inline constexpr std::string_view kElevatorText = "-0.056511560909856456";
inline constexpr std::string_view kThrottleText = "0.13890766076656494";
inline constexpr std::string_view kThetaText = "0.04622449703844172";
inline constexpr std::string_view kUText = "172.23674407873062";
inline constexpr std::string_view kWText = "7.967232214379726";
inline constexpr std::string_view kQText = "-2.7050441154484522e-05";
// The same values as doubles, for the checks that read them.
inline constexpr double kTheta = 0.04622449703844172;

// The trim's own n_frame p and r before the config replaces them with 0.0, the inputs of C-01's
// bound (section 5; trim_check.log): kappa_2(J_c) and |omega| of the trim's own solve.
inline constexpr double kTrimP = 0.0;
inline constexpr double kTrimR = 0.0;
inline constexpr double kTrimKappa = 1.0;
inline constexpr double kTrimOmegaNorm = 2.7050441154484522e-05;

// The oracle's physical setup on the non-rotating sphere (section 7), with no transport block: the
// trim's base with section 5's IC. `aero` is the vehicle.models entry for the aero.
inline std::string round_earth_text(std::string_view aero = "- plugin: aircraft/f16/physical/aero",
                                    std::string_view frames = "30001") {
  return std::string{"sim:\n  dt: 0.001\n  frames: "} + std::string{frames} +
         "\nvehicle:\n  models:\n    " + std::string{aero} +
         "\n    - plugin: aircraft/f16/hardware/propulsion/engine\n"
         "  initial_controls:\n    controls:\n      elevator: " +
         std::string{kElevatorText} +
         "\n      aileron: 0.0\n      rudder: 0.0\n      throttle: " + std::string{kThrottleText} +
         "\nphysical:\n"
         "  earth: {plugin: plugins/physical/earth/round, rotating: false}\n"
         "  gravity: {plugin: plugins/physical/gravity/inverse_square, rotating: false}\n"
         "  atmosphere: plugins/physical/atmosphere/isa1976\n"
         "  mass:\n    plugin: aircraft/f16/physical/mass\n"
         "initial:\n  state:\n"
         "    p_n: [0.6286530522161018, 0.0, 3051.9624000000003]\n"
         "    v_b: [" +
         std::string{kUText} + ", 0.0, " + std::string{kWText} +
         "]\n"
         "    euler_nb: [0.0, " +
         std::string{kThetaText} +
         ", 0.0]\n"
         "    omega_b: [0.0, " +
         std::string{kQText} + ", 0.0]\n";
}

}  // namespace astra::test::nesc::f16_symmetry
