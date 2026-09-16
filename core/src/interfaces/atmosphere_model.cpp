#include "astra/interfaces/atmosphere_model.hpp"

namespace astra {

// The key function; see the comment in atmosphere_model.hpp and in model.cpp. No other non-pure
// virtuals exist on this interface (unlike EarthModel/GravityModel), so this destructor is the only
// definition here.
AtmosphereModel::~AtmosphereModel() = default;

}  // namespace astra
