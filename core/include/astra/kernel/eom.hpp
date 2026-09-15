// The rigid-body derivative (docs/architecture.md §6, §8; ADR-0003, ADR-0004): the standard
// body-axis equations (Stevens & Lewis form), parameterized by the Earth interface so selecting
// flat/round/WGS-84 at runtime costs nothing here. There is exactly one Eom (ADR-0004: "the
// derivative functor is the same one exposed to Python as Simulation.derivative").
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/state.hpp"

namespace astra {

class Eom {
 public:
  Eom(EarthModel& earth, GravityModel& gravity, MassPropertiesModel& mass) noexcept
      : earth_(earth), gravity_(gravity), mass_model_(mass) {}

  // Latches this frame's mass properties and the inertia inverse the four RK4 stages within the
  // frame share, instead of inverting a 3x3 matrix at every stage. Mass is constant for a run
  // (ADR-0003), but reading it once per major frame rather than assuming it constant in code is
  // what keeps the deferred fuel-burn case (docs/architecture.md §19) from needing an Eom change.
  // Called once per major frame, before Integrator::step; never allocates, never throws.
  void begin_frame() noexcept;

  // The wrench (about the CG, body axes) the frame's vehicle models accumulated (§5's "vehicle
  // models accumulate forces and moments" stage). Zero by default: no AeroModel or PropulsionModel
  // exists yet (M3 adds the seam that calls this).
  void set_applied_wrench(const Wrench& wrench) noexcept { applied_wrench_ = wrench; }

  // The single derivative ADR-0004 requires: f(t, y) -> ydot, satisfying Integrator's
  // StateDerivative concept (noexcept, returns StateVector by value).
  [[nodiscard]] StateVector operator()(double t, const StateVector& y) const noexcept;

 private:
  EarthModel& earth_;
  GravityModel& gravity_;
  MassPropertiesModel& mass_model_;
  Wrench applied_wrench_;
  MassProps mass_props_;
  Eigen::Matrix3d inertia_inverse_ = Eigen::Matrix3d::Identity();
};

}  // namespace astra
