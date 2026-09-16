// A vehicle model that contributes to the applied wrench at the CG (docs/architecture.md §5, §7):
// AeroModel and PropulsionModel (M3) derive from this so Vehicle can find every such model at init
// and sum its contribution each frame. PLAN.md M1 PR 4 needs this now: §15's "NaN/Inf in ... any
// wrench contribution, with the offending plugin named" has no per-contribution data to check
// without a seam like this one -- Eom::set_applied_wrench existed with no caller before this PR.
#pragma once

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class WrenchSource : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp): without it
  // each plugin's -fvisibility=hidden translation unit would carry its own private vtable/typeinfo,
  // breaking dynamic_cast<WrenchSource*> across the .so boundary.
  ~WrenchSource() override;

  // This model's contribution to the vehicle's applied wrench (about the CG, body axes), evaluated
  // at the frame's state and this frame's environment sample (M2 PR 2: wind; PR 3 adds atmosphere).
  // env is the same EnvironmentSample Simulation::step latched for this vehicle before the wrench
  // stage (docs/architecture.md §5) -- a WrenchSource never samples wind or atmosphere itself. Per-
  // frame path (§14): no allocation, no exceptions.
  [[nodiscard]] virtual Wrench wrench_b(const VehicleState& vs,
                                        const EnvironmentSample& env) const noexcept = 0;

  // Which optional environment inputs this source cannot run without (environment_sample.hpp).
  // Simulation::from_config rejects a config that omits a required slot with a ConfigError naming
  // physical.wind / physical.atmosphere -- never a silent zero wind or a NaN density read by a
  // model that needed a real value. Default: none (a source that reads no environment input, e.g.
  // a constant-coefficient contribution, declares nothing). Defined out of line in
  // wrench_source.cpp, so the destructor stays the key function.
  [[nodiscard]] virtual EnvironmentRequirements requires_environment() const noexcept;
};

}  // namespace astra
