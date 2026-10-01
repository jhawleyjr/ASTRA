// Mass and inertia are not state in v1 (ADR-0003): constant for a run, supplied by whichever
// MassPropertiesModel a vehicle's config names. docs/architecture.md §7 pins get()'s signature.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"

namespace astra {

// About the CG, body axes. No CG-offset field: the state's p_n is the CG's own position and §7's
// Wrench is about the CG, so an offset is identically zero in v1 -- adding an unused field now
// would be speculative.
struct MassProps {
  double mass = 0.0;
  Eigen::Matrix3d inertia_b = Eigen::Matrix3d::Zero();
};

// noexcept strengthens §7's sketch: get() is called from inside
// RigidBody6DofDynamics::begin_frame(), which is noexcept, on the per-frame path (ADR-0021), so a
// throwing override would terminate the process regardless.
class MassPropertiesModel : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp).
  ~MassPropertiesModel() override;

  [[nodiscard]] virtual MassProps get() const noexcept = 0;
};

}  // namespace astra
