// A read-only, non-owning view over 13 doubles (the rigid-body block) with named accessors for
// ADR-0003's layout (docs/architecture.md §4 lists VehicleState among the kernel types). EarthModel
// and GravityModel's pull methods take one of these rather than reading a Vehicle's stored state
// directly, because the dynamics derivative is evaluated at every RK4 stage point, not just the
// frame's start: constructing a VehicleState over the stage's own trial state is how those stage
// values reach the environment models with no extra plumbing.
//
// ADR-0021: retyped from a reference over an owning StateVector to an Eigen::Map<const StateVector>
// over a raw pointer, so it can view any 13-double slice of a runtime-sized combined/stage buffer,
// not only a standalone stack StateVector. Every accessor's name and return value is unchanged, and
// no plugin call site changes (plugins only ever call these accessors; none constructs a
// VehicleState itself) -- only the kernel code that constructs one (Vehicle,
// RigidBody6DofDynamics) and any test that does the same.
#pragma once

#include <Eigen/Core>

#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

class VehicleState {
 public:
  explicit VehicleState(const double* y) noexcept : y_(y) {}
  // Convenience overload: most kernel call sites still hold an owning StateVector.
  explicit VehicleState(const StateVector& y) noexcept : y_(y.data()) {}

  [[nodiscard]] Eigen::Vector3d p_n() const noexcept {
    return y_.segment<state::kPositionSize>(state::kPosition);
  }
  [[nodiscard]] Eigen::Vector3d v_b() const noexcept {
    return y_.segment<state::kVelocitySize>(state::kVelocity);
  }
  [[nodiscard]] frames::Quaternion q_nb() const noexcept {
    return y_.segment<state::kQuaternionSize>(state::kQuaternion);
  }
  [[nodiscard]] Eigen::Vector3d omega_b() const noexcept {
    return y_.segment<state::kAngularRateSize>(state::kAngularRate);
  }
  // The full 13-vector, for a model that wants more than the named slots above. By value (a small
  // Eigen::Map, not an owning copy): ADR-0021's own retype, since y_ no longer has a stable address
  // a caller could hold a reference to across this view's own lifetime.
  [[nodiscard]] Eigen::Map<const StateVector> vector() const noexcept { return y_; }

 private:
  Eigen::Map<const StateVector> y_;
};

}  // namespace astra
