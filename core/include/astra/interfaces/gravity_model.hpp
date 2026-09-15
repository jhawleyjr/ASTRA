// docs/architecture.md §7 pins this signature. g_n is plumb-bob gravity (gravitation with
// centripetal folded in) -- the standard convention, and what M2's WGS-84 model will supply -- so
// the EOM carries no separate centripetal term.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class GravityModel : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp).
  ~GravityModel() override;

  [[nodiscard]] virtual Eigen::Vector3d g_n(const VehicleState& vs) const noexcept = 0;

  // What this gravity model needs p_n to mean, when it reads p_n at all (M2): gravity/constant
  // leaves the kAny default (it never reads p_n); gravity/inverse_square and gravity/wgs84_j2
  // override it to kGeodetic, since they read latitude and altitude straight from vs.p_n() under
  // ADR-0003's round/WGS-84 layout. Simulation::from_config compares this against the configured
  // Earth model's EarthModel::position_representation() and rejects a mismatch at init. Non-pure
  // and defined out of line in gravity_model.cpp, so the destructor above stays the key function.
  [[nodiscard]] virtual PositionRepresentation assumed_position_representation() const noexcept;
};

}  // namespace astra
