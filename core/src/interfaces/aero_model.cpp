#include "astra/interfaces/aero_model.hpp"

namespace astra {

// The key function; see the comment in aero_model.hpp. Kept as this class's sole out-of-line
// member (ADR-0020 moved wrench_b()'s own former arithmetic to core/include/astra/util/
// air_data.hpp; requires_environment() is now inline in the header) specifically so this
// definition keeps existing -- deleting this file entirely would delete the only definition of
// this destructor.
AeroModel::~AeroModel() = default;

}  // namespace astra
