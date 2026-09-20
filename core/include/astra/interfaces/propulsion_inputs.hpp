// Physics-derived inputs to a concrete PropulsionModel::compute() (docs/architecture.md §7,
// ADR-0018): airspeed and the full atmosphere sample, assembled by PropulsionModel::wrench_b()
// from a VehicleState/EnvironmentSample pair before compute() ever runs. Deliberately carries no
// throttle value -- ADR-0018's own Decision: a concrete plugin reads its own bound const double*
// (WrenchSource::bind_inputs(), ADR-0017), not through this struct.
#pragma once

#include <limits>

#include "astra/interfaces/atmosphere_model.hpp"

namespace astra {

// SI (ADR-0002). All-NaN by default (AtmoState's own default already is; true_airspeed_mps matches
// it here for the identical reason AeroInputs's own fields do).
struct PropulsionInputs {
  // Identical formula to AeroInputs::true_airspeed_mps, recomputed independently by
  // PropulsionModel's own wrench_b() (a separate WrenchSource, a separate call). m/s.
  double true_airspeed_mps = std::numeric_limits<double>::quiet_NaN();
  // Full AtmoState passthrough: density for thrust lapse; pressure/temperature reserved for a
  // later, more detailed engine model (not read by PLAN.md PR 4's simple lapse model, kept here so
  // that model would not need a breaking change).
  AtmoState atmosphere{};
};

}  // namespace astra
