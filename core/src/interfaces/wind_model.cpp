#include "astra/interfaces/wind_model.hpp"

namespace astra {

// The key function; see the comment in wind_model.hpp and in model.cpp.
WindModel::~WindModel() = default;

// Default: nothing to evolve. wind/uniform does not override this.
void WindModel::begin_frame(const StepContext& /*ctx*/, const VehicleState& /*vs*/,
                            double /*altitude_m*/) noexcept {}

// Default: not shareable. A model with internal state its author has not declared safe to share
// must not silently run under more than one vehicle (wind_model.hpp). wind/uniform overrides this.
bool WindModel::shareable_across_vehicles() const noexcept { return false; }

}  // namespace astra
