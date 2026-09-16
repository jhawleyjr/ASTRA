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
  //
  // Known gap, disclosed not fixed (Cowork review, M2 PR 2, post-merge-ready; PLAN.md's M2 PR 2 and
  // PR 3 lines), the same shape as AtmoState's own disclosed gap (atmosphere_model.hpp): a
  // WindModel that returns a NaN wind_n (e.g. an out-of-domain internal state PR 2b's dryden could
  // reach) is not itself checked -- HealthMonitor has no environment check, only check_wrench and
  // check_state. A WrenchSource that reads this NaN into a force/moment does make it visible, via
  // check_wrench, but check_wrench names the WRENCH's source (the consuming WrenchSource's plugin
  // path), never the wind plugin that actually produced the NaN -- contrary to §15's "with the
  // offending plugin named." PR 3's drag WrenchSource is the first thing that can carry such a NaN
  // into the EOM. A correct fix is the same new per-frame domain-validity HealthMonitor capability
  // PR 1/1b already deferred for AtmoState, not a second bolt-on here.
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
