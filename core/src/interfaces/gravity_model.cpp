#include "astra/interfaces/gravity_model.hpp"

namespace astra {

// The key function; see the comment in gravity_model.hpp and in model.cpp.
GravityModel::~GravityModel() = default;

// Default: does not read p_n. gravity/inverse_square and gravity/wgs84_j2 override this to
// kGeodetic (plugins/physical/gravity/{inverse_square,wgs84_j2}).
PositionRepresentation GravityModel::assumed_position_representation() const noexcept {
  return PositionRepresentation::kAny;
}

}  // namespace astra
