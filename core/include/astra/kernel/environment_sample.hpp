// One vehicle's environment. Simulation::step samples it once per major frame at the frame-start
// state, in the environment stage (docs/architecture.md §5), and latches it on the Vehicle
// (Vehicle::environment()). Each RK4 stage then works on its own copy
// (Vehicle::evaluate_derivative_stage):
// - wind_n stays frame-latched;
// - atmosphere is refreshed at the stage's own altitude (ADR-0016);
// - omega_ie_b is set from the stage's own state (ADR-0032).
// The latched sample keeps omega_ie_b at its NaN default.
#pragma once

#include <Eigen/Core>
#include <limits>

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

  // All-NaN by default (AtmoState's own designed default, atmosphere_model.hpp).
  // - Simulation::step (M2 PR 3) fills it from physical.atmosphere's at(altitude_m) at the
  //   frame-start state.
  // - Each RK4 stage then refreshes its own copy at the stage's own altitude (M2 PR 3c, ADR-0016,
  //   Vehicle::prepare_stage_environment), so the latched value is only the stage copy's starting
  //   point.
  // - A stage skipped for an out-of-range altitude keeps that latched value. Such a stage calls no
  //   wrench_b(), but integrate_state() still receives it.
  // See Vehicle::EnvironmentAvailability::atmosphere (kernel/vehicle.hpp) for the matching
  // init-time requirement check. Stays all-NaN when
  // physical.atmosphere is absent (rejected at init for a WrenchSource that requires it, so never
  // silently read by something that assumed a real value and got none configured), or when the
  // vehicle's altitude is outside the configured AtmosphereModel's valid_range() -- never clamped,
  // per that interface's own contract.
  //
  // Known gap, disclosed not fixed (Codex adversarial review, M2 PR 3), the identical shape as
  // wind_n's own disclosed gap above, and not narrower than it: whatever the reason -- out of
  // range by design, or a genuinely broken AtmosphereModel returning NaN at an altitude squarely
  // inside its own declared valid_range() -- this NaN is not itself checked. HealthMonitor has no
  // environment check, only check_wrench and check_state. A WrenchSource that reads it into a
  // force/moment does make it visible, via check_wrench, but check_wrench names the WRENCH's
  // source (the consuming WrenchSource's plugin path), never the atmosphere plugin that actually
  // produced the NaN -- contrary to §15's "with the offending plugin named," and, for a
  // non-wrench or NaN-tolerant consumer, not surfaced at all. A correct fix is the same new
  // per-frame domain-validity HealthMonitor capability wind_n's own comment already defers, not a
  // second, atmosphere-specific bolt-on: it needs to validate EnvironmentSample as a whole,
  // reporting a halt naming the atmosphere or wind plugin path Simulation::step already retains
  // (atmosphere_plugin_path_), before the two disclosed gaps above are fixed twice.
  AtmoState atmosphere{};

  // ADR-0032: the Earth's rotation rate relative to inertial, in body axes, rad/s. Read it through
  // air_data::omega_eb_b, which gives the body rate relative to the Earth.
  //
  // Vehicle::prepare_stage_environment sets it first in every RK4 stage's copy, from that stage's
  // own state, on the step path and on Simulation::derivative's probe path alike. It is therefore
  // set before any wrench_b() or integrate_state() call of that stage, including a stage whose
  // wrenches the environment checks skip.
  //
  // Its value is c_nb * earth.omega_ie_n(vs_stage), the EOM's own expression
  // (rigid_body_6dof_dynamics.cpp), and it is exactly +0.0 wherever that product is a zero.
  // - Why: on a flat or non-rotating Earth, c_nb * 0 is -0.0 in any component whose c_nb row is all
  //   negative, and omega_b - (-0.0) would turn a -0.0 rate into +0.0.
  // - With +0.0 there, omega_b - omega_ie_b is omega_b bit for bit, so an aero that damps the
  //   Earth-relative rate on such an Earth computes exactly what it computed from the inertial
  //   rate.
  //
  // All-NaN by default (ADR-0017's convention, as atmosphere above):
  // - The latched sample (Vehicle::environment()) keeps the default, and nothing reads the field
  //   there.
  // - A sample built outside the stage path (an independent evaluator, a test) must set the field
  //   from the sim's Earth model at the evaluated state. A forgotten value gives its reader a NaN
  //   wrench, never a silent non-rotating Earth.
  // - HealthMonitor does not check it: it is the core's own value, not an environment plugin's
  //   output.
  Eigen::Vector3d omega_ie_b = Eigen::Vector3d::Constant(std::numeric_limits<double>::quiet_NaN());
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
