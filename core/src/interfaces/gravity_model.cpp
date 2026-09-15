#include "astra/interfaces/gravity_model.hpp"

namespace astra {

// The key function; see the comment in gravity_model.hpp and in model.cpp.
GravityModel::~GravityModel() = default;

// Default: does not read p_n. gravity/inverse_square and gravity/wgs84_j2 override this to
// kGeodetic (plugins/physical/gravity/{inverse_square,wgs84_j2}).
PositionRepresentation GravityModel::assumed_position_representation() const noexcept {
  return PositionRepresentation::kAny;
}

// Default no-op: gravity/constant reads no geometry, and gravity/wgs84_j2's fixed WGS-84 geometry
// is already bounded well away from any singularity by the paired Earth model's own altitude
// floor. gravity/inverse_square overrides this (its own, separately configured radius).
void GravityModel::validate_p_n(const Eigen::Vector3d& /*p_n*/, const Config& /*node*/) const {}

}  // namespace astra
