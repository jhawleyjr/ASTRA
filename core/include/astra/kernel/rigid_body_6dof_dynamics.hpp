// The rigid-body derivative (docs/architecture.md §6, §8; ADR-0003, ADR-0004, ADR-0021): the
// standard body-axis equations (Stevens & Lewis form), parameterized by the Earth interface so
// selecting flat/round/WGS-84 at runtime costs nothing here. ADR-0021's one DynamicsModel
// implementation for the 13-double rigid-body block: a byte-for-byte port of the arithmetic that
// was `Eom::operator()` before this ADR (R1's own frozen golden pins this). Held directly by
// `Vehicle` (`std::optional<RigidBody6DofDynamics>`), never loaded through `PluginLoader` and never
// a `Model` -- the same status `Eom` had.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/dynamics_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/state.hpp"

namespace astra {

class RigidBody6DofDynamics : public DynamicsModel {
 public:
  RigidBody6DofDynamics(EarthModel& earth, GravityModel& gravity,
                        MassPropertiesModel& mass) noexcept
      : earth_(earth), gravity_(gravity), mass_model_(mass) {}

  // Latches this frame's mass properties and the inertia inverse the four RK4 stages within the
  // frame share, instead of inverting a 3x3 matrix at every stage. Mass is constant for a run
  // (ADR-0003), but reading it once per major frame rather than assuming it constant in code is
  // what keeps the deferred fuel-burn case (docs/architecture.md §19) from needing a change here.
  // Called once per major frame, before Integrator::step; never allocates, never throws.
  void begin_frame() noexcept;

  // The wrench (about the CG, body axes) the frame's vehicle models accumulated (§5's "vehicle
  // models accumulate forces and moments" stage), re-injected fresh before each RK4 sub-stage's own
  // derivative() call by Vehicle::integrate()'s own derivative functor.
  void set_applied_wrench(const Wrench& wrench) noexcept { applied_wrench_ = wrench; }

  [[nodiscard]] int state_count() const noexcept override { return kStateSize; }
  // Zero, matching the pre-ADR-0021 default (StateVector::Zero()); Vehicle overwrites this slice
  // directly from initial.state/for_testing's span immediately after, exactly as it overwrote its
  // own zero-initialized state_ member before this ADR -- this hook is not the seam that config-
  // driven initial state travels through.
  void initial_state(double* q) const noexcept override;
  void derivative(double t, const double* q, double* dq) const noexcept override;
  // ADR-0001: renormalizes q_nb in place -- this class's own override of what
  // post_step_renormalize did before this ADR. Returns are ignored by the DynamicsModel interface
  // (a zero/non-finite quaternion leaves q_nb untouched; see frames::normalize_quaternion).
  void post_step(double* q) const noexcept override;

  // ADR-0012: Vehicle::check_domain's own read of this frame's Earth/gravity models, so per-frame
  // domain validity needs no second, retained copy of the pointers handed to this constructor.
  // const-qualified: the domain check only reads in_domain()/p_n_dot(), never mutates either model.
  [[nodiscard]] const EarthModel& earth() const noexcept { return earth_; }
  [[nodiscard]] const GravityModel& gravity() const noexcept { return gravity_; }

 private:
  EarthModel& earth_;
  GravityModel& gravity_;
  MassPropertiesModel& mass_model_;
  Wrench applied_wrench_;
  MassProps mass_props_;
  Eigen::Matrix3d inertia_inverse_ = Eigen::Matrix3d::Identity();
};

}  // namespace astra
