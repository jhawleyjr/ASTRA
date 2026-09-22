#include "astra/kernel/rigid_body_6dof_dynamics.hpp"

#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

void RigidBody6DofDynamics::begin_frame() noexcept {
  mass_props_ = mass_model_.get();
  inertia_inverse_ = mass_props_.inertia_b.inverse();
}

void RigidBody6DofDynamics::initial_state(double* q) const noexcept {
  Eigen::Map<StateVector>(q).setZero();
}

// Stevens & Lewis body-axis rigid-body equations, general form (docs/architecture.md §6, ADR-0003):
// omega_b is the body rate relative to inertial (state's own definition), so the attitude
// kinematics subtract the Earth and transport rates to get the rate relative to n that q_nb
// measures, while the gyroscopic term in the angular-rate equation uses omega_b directly. Byte-
// for-byte port of Eom::operator() (R1's own frozen golden pins this): the same arithmetic, over a
// VehicleState constructed on a raw pointer and written into dq instead of returned by value.
void RigidBody6DofDynamics::derivative(double /*t*/, const double* q, double* dq) const noexcept {
  const VehicleState vs{q};
  const frames::Quaternion q_nb = vs.q_nb();
  const Eigen::Matrix3d c_nb = frames::dcm_from_quaternion(q_nb);
  const Eigen::Vector3d v_b = vs.v_b();
  const Eigen::Vector3d omega_b = vs.omega_b();

  // Earth rate and transport rate, rotated from n into b; the rate q_nb's kinematics actually see
  // is omega_b relative to inertial minus both (ADR-0003: "Earth rotation rate expressed in n" and
  // "transport rate of the n frame", both zero for flat Earth).
  const Eigen::Vector3d omega_ie_b = c_nb * earth_.omega_ie_n(vs);
  const Eigen::Vector3d omega_en_b = c_nb * earth_.omega_en_n(vs);
  const Eigen::Vector3d omega_nb_b = omega_b - (omega_ie_b + omega_en_b);

  const Eigen::Vector3d p_n_dot = earth_.p_n_dot(vs);

  // v_b_dot: applied force over mass, plus gravity rotated into body axes, minus the Coriolis-like
  // term from differentiating Earth-relative velocity (expressed in body axes) in a frame rotating
  // at omega_b relative to inertial while the Earth frame itself rotates at omega_ie_b: the
  // combined term is -(omega_b + omega_ie_b) x v_b. For flat Earth omega_ie_b is zero and this is
  // exactly the Stevens & Lewis flat-Earth form v_b_dot = F_b/m + g_b - omega_b x v_b.
  const Eigen::Vector3d g_b = frames::rotate_n_to_b(q_nb, gravity_.g_n(vs));
  const Eigen::Vector3d v_b_dot =
      applied_wrench_.force_b / mass_props_.mass + g_b - (omega_b + omega_ie_b).cross(v_b);

  const frames::Quaternion q_nb_dot = frames::quaternion_derivative(q_nb, omega_nb_b);

  const Eigen::Vector3d omega_b_dot =
      inertia_inverse_ *
      (applied_wrench_.moment_b - omega_b.cross(mass_props_.inertia_b * omega_b));

  Eigen::Map<StateVector> ydot(dq);
  ydot.segment<state::kPositionSize>(state::kPosition) = p_n_dot;
  ydot.segment<state::kVelocitySize>(state::kVelocity) = v_b_dot;
  ydot.segment<state::kQuaternionSize>(state::kQuaternion) = q_nb_dot;
  ydot.segment<state::kAngularRateSize>(state::kAngularRate) = omega_b_dot;
}

// ADR-0001: renormalized after every integrator step. Returns false (q_nb left unmodified) on a
// zero or non-finite quaternion; see frames::normalize_quaternion.
void RigidBody6DofDynamics::post_step(double* q) const noexcept {
  Eigen::Map<StateVector> y(q);
  static_cast<void>(
      frames::normalize_quaternion(y.segment<state::kQuaternionSize>(state::kQuaternion)));
}

}  // namespace astra
