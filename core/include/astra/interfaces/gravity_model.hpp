// docs/architecture.md §7 pins this signature. g_n is plumb-bob gravity (gravitation with
// centripetal folded in) -- the standard convention, and what M2's WGS-84 model will supply -- so
// the EOM carries no separate centripetal term.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class GravityModel : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp).
  ~GravityModel() override;

  [[nodiscard]] virtual Eigen::Vector3d g_n(const VehicleState& vs) const noexcept = 0;
};

}  // namespace astra
