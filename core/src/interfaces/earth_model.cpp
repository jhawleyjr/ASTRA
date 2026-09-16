#include "astra/interfaces/earth_model.hpp"

namespace astra {

// The key function; see the comment in earth_model.hpp and in model.cpp.
EarthModel::~EarthModel() = default;

// Default no-op: flat Earth's p_n_dot/omega_en_n have no singularity, so any finite NED triple is
// valid. Round and WGS-84 override this (plugins/physical/earth/{round,wgs84_rotating}).
void EarthModel::validate_p_n(const Eigen::Vector3d& /*p_n*/, const Config& /*node*/) const {}

}  // namespace astra
