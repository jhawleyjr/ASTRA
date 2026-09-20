// AeroModel: a WrenchSource specialization (docs/architecture.md §7, ADR-0018) that assembles
// AeroInputs from (VehicleState, EnvironmentSample) and forwards to a concrete plugin's own
// compute(). wrench_b() and requires_environment() are both final -- see ADR-0018's own Decision
// for why atmosphere is unconditionally mandatory and wind unconditionally optional, and why a
// concrete plugin has no say in either.
#pragma once

#include "astra/interfaces/aero_inputs.hpp"
#include "astra/interfaces/wrench_source.hpp"

namespace astra {

class AeroModel : public WrenchSource {
 public:
  // Defined out of line in libastra_core, the same key-function reasoning as WrenchSource itself
  // (wrench_source.hpp): without it, each plugin's own -fvisibility=hidden translation unit would
  // carry a private vtable/typeinfo for AeroModel, breaking dynamic_cast<AeroModel*> across the
  // .so boundary.
  ~AeroModel() override;

  // Computes v_rel_b = vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n), assembles AeroInputs from
  // it (atan2-based alpha/beta, finite -- never 0/0 -- at true_airspeed_mps == 0, ADR-0018), then:
  // if every assembled field is finite, forwards to compute(); if any field is non-finite (a
  // genuinely broken AtmosphereModel is the only realistic cause, since "no atmosphere" is already
  // an init-time rejection via requires_environment() below), returns an explicit all-NaN Wrench
  // WITHOUT calling compute() at all. HealthMonitor::check_wrench (ADR-0016, §15, unmodified) then
  // unconditionally halts, naming this plugin's own path, regardless of which fields a concrete
  // compute() would itself have read -- an unconditional no-silent-NaN guarantee, not one
  // contingent on the concrete plugin's own arithmetic (ADR-0018). Per-frame path (§14): no
  // allocation, no exceptions.
  [[nodiscard]] Wrench wrench_b(const VehicleState& vs,
                                const EnvironmentSample& env) const noexcept final;

  // Unconditionally {wind: false, atmosphere: true}, never overridable by a concrete plugin
  // (final): this class's own wrench_b() above, not the plugin, is what reads env.wind_n (optional
  // -- EnvironmentSample::wind_n's own contract makes "absent" and "zero" identical by design) and
  // env.atmosphere (mandatory -- every AeroInputs field above depends on a real AtmoState, with no
  // meaningful default). Sealing this prevents a future concrete plugin from overriding atmosphere
  // back to false, which would silently reopen the fault wrench_b()'s own isfinite guard exists to
  // prevent, by skipping Vehicle::init()'s rejection of a config missing physical.atmosphere
  // (ADR-0018).
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept final;

  // This model's contribution to the applied wrench, about the CG, body axes (Wrench's own
  // contract, unchanged) -- called by wrench_b() above with an already-assembled, already-verified-
  // finite AeroInputs. const noexcept, matching wrench_b()'s own per-RK4-substage purity contract
  // (ADR-0016, as ADR-0017 already amends it for a bound control member read outside this call's
  // own arguments): must return the identical value across all four of one frame's RK4 sub-stages
  // for identical inputs.
  //
  // ADR-0018's own V->0 obligation, not enforced by any framework code (this class computes no
  // nondimensional rate itself): a conforming implementation MUST return a finite Wrench at
  // inputs.true_airspeed_mps == 0. Stevens & Lewis's nondimensional stability/damping derivatives
  // (3rd ed., §2.3, Eq. (2.3-9a): p_hat = P*b/(2*V_T)) are scaled by 1/(2*V_T) -- a naive port of
  // one carries a real division by zero at that state, unlike the *dimensional* force equations
  // built from the same coefficients (Eq. (2.3-8b)), which have no such singularity
  // (dynamic_pressure_pa = 0.5*rho*V_T^2 is finite and -> 0 at V_T == 0 by construction). PLAN.md
  // PR 4's own linear-coefficient aero carries this obligation's own acceptance test.
  [[nodiscard]] virtual Wrench compute(const AeroInputs& inputs) const noexcept = 0;
};

}  // namespace astra
