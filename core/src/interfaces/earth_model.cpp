#include "astra/interfaces/earth_model.hpp"

namespace astra {

// The key function; see the comment in earth_model.hpp and in model.cpp.
EarthModel::~EarthModel() = default;

// Default true: flat Earth's p_n_dot/omega_en_n have no singularity, so every p_n is in domain.
// Round and WGS-84 override this (plugins/physical/earth/{round,wgs84_rotating}).
bool EarthModel::in_domain(const Eigen::Vector3d& /*p_n*/) const noexcept { return true; }

// Default no-op: flat Earth accepts any finite NED triple (in_domain's default above is already
// true everywhere, and flat has no input-sanity check of its own). Round and WGS-84 override this
// (plugins/physical/earth/{round,wgs84_rotating}).
void EarthModel::validate_p_n(const Eigen::Vector3d& /*p_n*/, const Config& /*node*/) const {}

}  // namespace astra
