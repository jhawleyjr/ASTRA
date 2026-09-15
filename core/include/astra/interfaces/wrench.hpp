// The applied force and moment a vehicle's aero and propulsion models accumulate each frame
// (docs/architecture.md §5, §7). Exactly the shape §7 pins.
#pragma once

#include <Eigen/Core>

namespace astra {

// About the CG, body axes. Zero-initialized: a Wrench with nothing summed into it applies nothing,
// which is what a vehicle with no AeroModel or PropulsionModel (this PR; both arrive in M3) needs.
struct Wrench {
  Eigen::Vector3d force_b = Eigen::Vector3d::Zero();
  Eigen::Vector3d moment_b = Eigen::Vector3d::Zero();
};

}  // namespace astra
