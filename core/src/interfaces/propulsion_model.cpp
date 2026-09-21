#include "astra/interfaces/propulsion_model.hpp"

namespace astra {

// The key function; see the comment in propulsion_model.hpp. Kept as this class's sole
// out-of-line member for the identical reason aero_model.cpp is (ADR-0020).
PropulsionModel::~PropulsionModel() = default;

}  // namespace astra
