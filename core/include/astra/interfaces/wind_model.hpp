// docs/architecture.md §7 pins wind_n's signature; this interface adds two non-pure virtuals §7's
// sketch does not carry (begin_frame, shareable_across_vehicles), each defined out of line in
// wind_model.cpp so ~WindModel stays the key function -- the same shape as GravityModel's
// assumed_position_representation()/validate_p_n additions over its own §7 sketch.
//
// THREE-WAY SPLIT (docs/architecture.md §2: "Random processes ... are seeded and stepped by the
// scheduler, never by a system RNG"; M2 PR 2 plan review):
//   - step(ctx)            -- Model's own per-frame method, scheduler-driven, no VehicleState. The
//                             one place a stochastic model draws this frame's RNG samples:
//                             evolution here depends only on the scheduler, never on how many
//                             vehicles or consumers exist.
//   - begin_frame(ctx, vs) -- below. The ONLY place a WindModel may use a VehicleState to evolve
//                             internal state (a shaping filter's coefficients, which MIL-F-8785C
//                             ties to true airspeed and altitude -- state step() cannot see).
//                             Called once per vehicle, per frame, by Simulation::step, immediately
//                             before wind_n for that same vehicle.
//   - wind_n(ctx, vs)      -- const noexcept. A pure read of whatever begin_frame already computed;
//                             it cannot itself advance anything, because a const method cannot
//                             write to a non-mutable member (compiler-enforced). An earlier draft
//                             of this design instead left wind_n non-const, reasoning that
//                             Eom::operator() being const noexcept would then be unable to call it
//                             -- that reasoning does not hold: Eom holds EarthModel&/GravityModel&
//                             as reference members, and a reference member does not propagate the
//                             enclosing object's constness to the referent, so a const Eom could
//                             still call a non-const method through one (verified directly against
//                             a minimal reproduction). wind_n's own constness, not its caller's, is
//                             the real guarantee, and it is why the split above puts all mutation
//                             in step() and begin_frame() instead.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class WindModel : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as AtmosphereModel/
  // GravityModel/WrenchSource (core/src/interfaces/model.cpp): plugins compile with
  // -fvisibility=hidden and would otherwise each carry a private copy of this vtable and typeinfo,
  // breaking dynamic_cast<WindModel*> and UBSan's vptr check across the .so boundary the moment
  // Simulation::from_config casts a loaded Model to this.
  ~WindModel() override;

  // Per-frame, per-vehicle evolution hook, called once per vehicle by Simulation::step immediately
  // before wind_n for that same vehicle -- mirroring Eom::begin_frame's "latch what this frame's
  // evaluations need" role (core/include/astra/kernel/eom.hpp), but per-vehicle rather than
  // per-Eom. This is the ONLY place a WindModel may use a VehicleState to evolve internal state:
  // Model::step(const StepContext&) has no VehicleState to offer, and wind_n below is const and
  // cannot evolve anything. Non-const and kernel-driven only -- nothing outside Simulation::step
  // calls it, so evolution still does not depend on how many WrenchSources later read wind_n.
  // Default: empty (wind/uniform has no state to evolve). Per-frame path (§14): no allocation, no
  // exceptions. Defined out of line in wind_model.cpp, so the destructor stays the key function.
  virtual void begin_frame(const StepContext& ctx, const VehicleState& vs) noexcept;

  // The velocity of the AIR MASS with respect to the Earth, expressed in the local navigation frame
  // (NED, ADR-0001), metres per second (ADR-0002). This is "wind-to", not "wind-from": a 10 m/s
  // wind FROM the north -- a northerly, which blows air toward the south -- is wind_n = (-10, 0,
  // 0). The meteorological "from" convention is a reporting convention and is deliberately NOT used
  // anywhere inside core/ or plugins/; a config that wants to express it converts at its own
  // boundary (ADR-0002: conversions happen only at boundaries).
  //
  // A consumer forms air-relative velocity in body axes as
  //     v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), wind_n(ctx, vs));
  // which is the only correct combination: v_b is Earth-relative velocity in body axes (ADR-0003)
  // and wind_n is Earth-relative air velocity in n, so the subtraction happens after the rotation,
  // in one frame. ADR-0001: "Any later frame (wind, stability, sensor mount) is defined relative to
  // b or n here, never freshly."
  //
  // const noexcept, unlike §7's plain sketch: a pure read of whatever begin_frame already latched
  // this frame (see the file header above for why constness, not non-constness, is what actually
  // keeps evolution out of this method). Per-frame path (§14): no allocation, no exceptions.
  [[nodiscard]] virtual Eigen::Vector3d wind_n(const StepContext& ctx,
                                               const VehicleState& vs) const noexcept = 0;

  // Whether this model's per-frame output is correct when shared by more than one vehicle
  // (ADR-0011: environment models are shared across vehicles, and nothing in v1 may assume there is
  // exactly one). Defaults to FALSE: refusing to run beats a plausible-looking wrong answer (§15),
  // and a model with internal state its author has not thought about sharing must not silently
  // produce one realization for every vehicle -- concretely, begin_frame above would be called once
  // per vehicle with a DIFFERENT VehicleState each time, and a model whose evolution is keyed to
  // that vehicle's own airspeed/altitude (a future stateful wind model) cannot honor two at once
  // with one set of internal state. wind/uniform overrides to true: it is stateless and reads no
  // VehicleState, so sharing it is exact, not approximate. Simulation::from_config rejects more
  // than one vehicle paired with a non-shareable wind model (currently unreachable: v1 builds
  // exactly one vehicle; written now because this is the cheapest moment to add the guard).
  // Defined out of line in wind_model.cpp, so the destructor stays the key function.
  [[nodiscard]] virtual bool shareable_across_vehicles() const noexcept;
};

}  // namespace astra
