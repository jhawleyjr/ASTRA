// A read-only, non-owning view over a StateVector with named accessors for ADR-0003's layout
// (docs/architecture.md §4 lists VehicleState among the kernel types). EarthModel and
// GravityModel's pull methods take one of these rather than reading a Vehicle's stored state
// directly, because Eom evaluates the derivative at every RK4 stage point, not just the frame's
// start: constructing a VehicleState over the stage's local StateVector is how those stage values
// reach the environment models with no extra plumbing.
#pragma once

#include <Eigen/Core>

#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

class VehicleState {
 public:
  explicit VehicleState(const StateVector& y) noexcept : y_(y) {}

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
  // The full 13-vector, for a model that wants more than the named slots above.
  [[nodiscard]] const StateVector& vector() const noexcept { return y_; }

 private:
  const StateVector& y_;
};

}  // namespace astra
