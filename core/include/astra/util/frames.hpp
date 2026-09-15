// core/util/ owns the single quaternion/DCM/Euler conversion set (ADR-0001); no model writes its
// own. Every formula here is typed from Stevens & Lewis, Aircraft Control and Simulation, 3rd ed.,
// against the same equations tests/golden/frames/adr0001_cases.hpp was computed from independently
// (numpy, no ASTRA code involved), so the test judges this file against the convention, not against
// itself.
//
// Conventions (ADR-0001): body FRD, nav NED. q_nb is Hamilton, scalar-first (w, x, y, z), rotates n
// -> b (v_b = C_nb v_n), renormalized after every integrator step. Euler angles are 3-2-1 (psi yaw,
// theta pitch, phi roll), derived outputs only, stored (psi, theta, phi) to match the oracle's
// euler_nb layout.
#pragma once

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>

namespace astra::frames {

// A unit quaternion (w, x, y, z); callers own normalization except where noted.
using Quaternion = Eigen::Vector4d;
// (psi, theta, phi) in rad, 3-2-1 sequence; derived-only per ADR-0001, never state.
using EulerAngles = Eigen::Vector3d;

// NOLINTBEGIN(readability-magic-numbers): every literal below is a coefficient from Stevens &
// Lewis' closed-form equations (eq. 1.3-11, 1.8-16, 1.8-18), not an arbitrary magic number; naming
// each one (e.g. kHalf = 0.5) would make the formulas harder to check against the reference, not
// easier.

// --- Euler <-> quaternion <-> DCM (Stevens & Lewis eq. 1.3-11, 1.8-16, 1.8-18) ---

// eq. 1.8-16, half angles a = phi/2, b = theta/2, c = psi/2.
[[nodiscard]] inline Quaternion quaternion_from_euler(const EulerAngles& euler_nb) noexcept {
  const double ca = std::cos(euler_nb(2) / 2.0);
  const double sa = std::sin(euler_nb(2) / 2.0);
  const double cb = std::cos(euler_nb(1) / 2.0);
  const double sb = std::sin(euler_nb(1) / 2.0);
  const double cc = std::cos(euler_nb(0) / 2.0);
  const double sc = std::sin(euler_nb(0) / 2.0);
  return Quaternion{ca * cb * cc + sa * sb * sc, sa * cb * cc - ca * sb * sc,
                    ca * sb * cc + sa * cb * sc, ca * cb * sc - sa * sb * cc};
}

// eq. 1.3-11.
[[nodiscard]] inline Eigen::Matrix3d dcm_from_euler(const EulerAngles& euler_nb) noexcept {
  const double cp = std::cos(euler_nb(2));
  const double sp = std::sin(euler_nb(2));
  const double ct = std::cos(euler_nb(1));
  const double st = std::sin(euler_nb(1));
  const double cps = std::cos(euler_nb(0));
  const double sps = std::sin(euler_nb(0));
  Eigen::Matrix3d c_nb;
  // clang-format off
  c_nb << ct * cps,                 ct * sps,                 -st,
          sp * st * cps - cp * sps, sp * st * sps + cp * cps, sp * ct,
          cp * st * cps + sp * sps, cp * st * sps - sp * cps, cp * ct;
  // clang-format on
  return c_nb;
}

// eq. 1.8-18, checked equal to the Euler form for every oracle case.
[[nodiscard]] inline Eigen::Matrix3d dcm_from_quaternion(const Quaternion& q_nb) noexcept {
  const double w = q_nb(0);
  const double x = q_nb(1);
  const double y = q_nb(2);
  const double z = q_nb(3);
  Eigen::Matrix3d c_nb;
  // clang-format off
  c_nb << w*w + x*x - y*y - z*z, 2.0 * (x*y + w*z),     2.0 * (x*z - w*y),
          2.0 * (x*y - w*z),     w*w - x*x + y*y - z*z, 2.0 * (y*z + w*x),
          2.0 * (x*z + w*y),     2.0 * (y*z - w*x),     w*w - x*x - y*y + z*z;
  // clang-format on
  return c_nb;
}

// psi = atan2(C12, C11), theta = -asin(C13), phi = atan2(C23, C33) (1-based indices in the oracle's
// comment; C13 is clamped for the theta = +-90 deg gimbal-lock cases where roundoff can push |C13|
// past 1 by an epsilon).
[[nodiscard]] inline EulerAngles euler_from_dcm(const Eigen::Matrix3d& c_nb) noexcept {
  const double c13 = std::clamp(c_nb(0, 2), -1.0, 1.0);
  return EulerAngles{std::atan2(c_nb(0, 1), c_nb(0, 0)), -std::asin(c13),
                     std::atan2(c_nb(1, 2), c_nb(2, 2))};
}

[[nodiscard]] inline EulerAngles euler_from_quaternion(const Quaternion& q_nb) noexcept {
  return euler_from_dcm(dcm_from_quaternion(q_nb));
}

// --- Quaternion algebra (Hamilton product, scalar-first) ---

[[nodiscard]] inline Quaternion quaternion_conjugate(const Quaternion& q) noexcept {
  return Quaternion{q(0), -q(1), -q(2), -q(3)};
}

[[nodiscard]] inline Quaternion quaternion_product(const Quaternion& a,
                                                   const Quaternion& b) noexcept {
  const double aw = a(0);
  const double ax = a(1);
  const double ay = a(2);
  const double az = a(3);
  const double bw = b(0);
  const double bx = b(1);
  const double by = b(2);
  const double bz = b(3);
  return Quaternion{aw * bw - ax * bx - ay * by - az * bz, aw * bx + ax * bw + ay * bz - az * by,
                    aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw};
}

// eq. 1.8-15: qdot_nb = 0.5 * q_nb (x) (0, omega_b). The other product order gives different
// numbers for every oracle case except pure yaw, so the order here is exactly what the oracle pins.
[[nodiscard]] inline Quaternion quaternion_derivative(const Quaternion& q_nb,
                                                      const Eigen::Vector3d& omega_b) noexcept {
  const Quaternion omega_quat{0.0, omega_b(0), omega_b(1), omega_b(2)};
  return 0.5 * quaternion_product(q_nb, omega_quat);
}

// In-place renormalize, e.g. on a StateVector's quaternion segment (ADR-0001: after every
// integrator step). Returns false, leaving q unmodified, if its norm is zero or non-finite, instead
// of dividing by zero or propagating NaN into every later rotation (docs/architecture.md §14: model
// errors are status flags, not silent corruption); the caller routes that into a halt once the
// health monitor exists (PLAN.md M1 PR 4).
[[nodiscard]] inline bool normalize_quaternion(Eigen::Ref<Quaternion> q) noexcept {
  const double norm = q.norm();
  if (!std::isfinite(norm) || norm == 0.0) {
    return false;
  }
  q /= norm;
  return true;
}

// --- Vector rotation by the quaternion sandwich, independent of the DCM path above ---

[[nodiscard]] inline Eigen::Vector3d rotate_n_to_b(const Quaternion& q_nb,
                                                   const Eigen::Vector3d& v_n) noexcept {
  const Quaternion v_quat{0.0, v_n(0), v_n(1), v_n(2)};
  const Quaternion rotated =
      quaternion_product(quaternion_product(quaternion_conjugate(q_nb), v_quat), q_nb);
  return rotated.tail<3>();
}

[[nodiscard]] inline Eigen::Vector3d rotate_b_to_n(const Quaternion& q_nb,
                                                   const Eigen::Vector3d& v_b) noexcept {
  const Quaternion v_quat{0.0, v_b(0), v_b(1), v_b(2)};
  const Quaternion rotated =
      quaternion_product(quaternion_product(q_nb, v_quat), quaternion_conjugate(q_nb));
  return rotated.tail<3>();
}

// --- The one place ADR-0001 allows an Eigen::Quaterniond conversion ---
//
// Eigen stores quaternions scalar-last internally and its toRotationMatrix() uses the opposite
// rotation convention from C_nb: to_eigen_quaternion(q_nb).toRotationMatrix() equals
// dcm_from_quaternion(q_nb) transposed (C_bn, not C_nb). Anyone rotating a vector with the
// Eigen::Quaterniond this returns instead of rotate_n_to_b/rotate_b_to_n above gets the inverse
// rotation; that trap is why this conversion lives in exactly one place.

[[nodiscard]] inline Eigen::Quaterniond to_eigen_quaternion(const Quaternion& q_nb) noexcept {
  return {q_nb(0), q_nb(1), q_nb(2), q_nb(3)};
}

[[nodiscard]] inline Quaternion from_eigen_quaternion(const Eigen::Quaterniond& q) noexcept {
  return Quaternion{q.w(), q.x(), q.y(), q.z()};
}

// NOLINTEND(readability-magic-numbers)

}  // namespace astra::frames
