// PropulsionModel: a WrenchSource specialization (docs/architecture.md §7, ADR-0018) that
// assembles PropulsionInputs from (VehicleState, EnvironmentSample) and forwards to a concrete
// plugin's own compute(). Mirrors AeroModel's own shape exactly -- see aero_model.hpp and
// ADR-0018's own Decision for the full reasoning, not duplicated here.
#pragma once

#include "astra/interfaces/propulsion_inputs.hpp"
#include "astra/interfaces/wrench_source.hpp"

namespace astra {

class PropulsionModel : public WrenchSource {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as AeroModel/WrenchSource.
  ~PropulsionModel() override;

  // Computes v_rel_b = vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n), assembles PropulsionInputs
  // from it (true_airspeed_mps, and env.atmosphere passed through unchanged), then: if every
  // assembled field is finite, forwards to compute(); if any field is non-finite, returns an
  // explicit all-NaN Wrench WITHOUT calling compute() at all -- the identical unconditional
  // no-silent-NaN guarantee AeroModel::wrench_b() gives (ADR-0018). Per-frame path (§14): no
  // allocation, no exceptions.
  [[nodiscard]] Wrench wrench_b(const VehicleState& vs,
                                const EnvironmentSample& env) const noexcept final;

  // Unconditionally {wind: false, atmosphere: true}, never overridable by a concrete plugin
  // (final) -- identical reasoning to AeroModel::requires_environment() (aero_model.hpp, ADR-0018):
  // this class's own wrench_b() above is what reads both env.wind_n and env.atmosphere.
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept final;

  // This model's contribution to the applied wrench, about the CG, body axes (Wrench's own
  // contract, unchanged) -- called by wrench_b() above with an already-assembled, already-verified-
  // finite PropulsionInputs. const noexcept, matching wrench_b()'s own per-RK4-substage purity
  // contract (ADR-0016, as ADR-0017 already amends it).
  //
  // A plugin whose thrust line does not pass through the CG applies its own r x F, using its own
  // config's station offset, before returning -- no core or base-class code performs this transfer
  // (ADR-0018 (d); PLAN.md's own PR 1 line: "engine-station offsets live in the turbofan plugin's
  // own config, not in mass properties").
  //
  // ADR-0018's own V->0 obligation, not enforced by any framework code: a conforming
  // implementation MUST return a finite Wrench at inputs.true_airspeed_mps == 0 -- the identical
  // hazard AeroModel::compute()'s own doc comment states, since a thrust-lapse or ram-effect model
  // can carry the same 1/V_T term in an intake-recovery or ram-drag calculation.
  [[nodiscard]] virtual Wrench compute(const PropulsionInputs& inputs) const noexcept = 0;
};

}  // namespace astra
