#include "astra/interfaces/gravity_model.hpp"

namespace astra {

// The key function; see the comment in gravity_model.hpp and in model.cpp.
GravityModel::~GravityModel() = default;

// Default: does not read p_n. gravity/inverse_square and gravity/wgs84_j2 override this to
// kGeodetic (plugins/physical/gravity/{inverse_square,wgs84_j2}).
PositionRepresentation GravityModel::assumed_position_representation() const noexcept {
  return PositionRepresentation::kAny;
}

// Default true: gravity/constant reads no geometry, so every p_n is in domain.
// gravity/inverse_square and gravity/wgs84_j2 each override this against their own geometry --
// WGS-84's fixed ellipsoid geometry is NOT already bounded by the paired Earth model's own altitude
// floor: earth/wgs84_ rotating's floor is the meridian radius M(lat), which does not protect
// gravity/wgs84_j2's separate ECEF-radius singularity near the pole (Codex adversarial review; see
// that plugin's in_domain comment).
bool GravityModel::in_domain(const Eigen::Vector3d& /*p_n*/) const noexcept { return true; }

// Default no-op: gravity/constant's in_domain default above is already true everywhere, and it has
// no input-sanity check of its own (EarthModel::validate_p_n runs first and covers latitude/
// longitude sanity). gravity/inverse_square and gravity/wgs84_j2 override this.
void GravityModel::validate_p_n(const Eigen::Vector3d& /*p_n*/, const Config& /*node*/) const {}

}  // namespace astra
