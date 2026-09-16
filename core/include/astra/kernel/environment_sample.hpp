// One vehicle's environment for one major frame, sampled by Simulation::step in the environment
// stage (docs/architecture.md §5) and held constant through that vehicle's wrench and EOM stages --
// the same latching discipline Eom::begin_frame and Eom::set_applied_wrench already use.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/atmosphere_model.hpp"

namespace astra {

struct EnvironmentSample {
  // Velocity of the air mass w.r.t. the Earth, in n (NED), m/s (wind_model.hpp's "wind-to"
  // convention). Zero when no physical.wind is configured: still air is the correct default, and a
  // WrenchSource that cannot accept it declares EnvironmentRequirements::wind and is rejected at
  // init instead (wrench_source.hpp) -- never a silent zero read by a model that needed real wind.
  Eigen::Vector3d wind_n = Eigen::Vector3d::Zero();

  // All-NaN by default (AtmoState's own designed default, atmosphere_model.hpp). NEVER populated in
  // M2 PR 2 -- PR 3 is the first PR to write a real value here, in the same change that adds the
  // "atmosphere" field to Vehicle's EnvironmentAvailability (kernel/vehicle.hpp). Until then, any
  // WrenchSource declaring EnvironmentRequirements::atmosphere is rejected unconditionally at init
  // (see EnvironmentAvailability's own comment for why that is unconditional, not "if
  // physical.atmosphere is absent"), so this field being permanently NaN in M2 PR 2 is never
  // silently read by anything that needed a real value.
  AtmoState atmosphere{};
};

// Which optional environment inputs a WrenchSource cannot run without (docs/architecture.md §15's
// existing promise, core/src/kernel/simulation.cpp: "A wrench source that requires one and finds
// none is an init error naming physical.atmosphere, never a silent rho = 0" -- this struct plus
// WrenchSource::requires_environment() is what makes that true and testable, not just a comment).
// Both fields default to false: a WrenchSource that reads no environment input (M3's constant-
// coefficient aero, say) declares neither.
struct EnvironmentRequirements {
  bool wind = false;
  bool atmosphere = false;
};

}  // namespace astra
