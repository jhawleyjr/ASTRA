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
//   - begin_frame(ctx, vs, altitude_m) -- below. The ONLY place a WindModel may use a VehicleState
//                             to evolve internal state (a shaping filter's coefficients, which
//                             MIL-F-8785C ties to true airspeed and altitude -- state step() cannot
//                             see). Called once per vehicle, per frame, by Simulation::step,
//                             immediately before wind_n for that same vehicle. altitude_m is Earth-
//                             derived (EarthModel::altitude(vs)), not read from vs.p_n() directly:
//                             an earlier draft passed only VehicleState, and Codex's adversarial
//                             review caught that vs.p_n()(2) means opposite things under earth/flat
//                             (NED down, metres) and earth/round or wgs84_rotating (geodetic
//                             altitude, metres, opposite sign) -- exactly the representation trap
//                             ADR-0003/PositionRepresentation already exists to prevent for gravity
//                             models, and the same trap AtmosphereModel::at() avoids by taking a
//                             plain altitude_m instead of a VehicleState at all. A WindModel
//                             needing altitude reads this parameter, never vs.p_n().
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
#include "astra/interfaces/position_representation.hpp"
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
  //
  // altitude_m is exactly whatever the configured EarthModel::altitude() returns for vs -- height
  // above that Earth model's own reference surface, the same value AtmosphereModel::at() takes
  // (atmosphere_model.hpp). A WindModel needing altitude reads this, never vs.p_n() (see the file
  // header above). vs itself still carries v_b (true airspeed) and q_nb (for wind_n's body-to-nav
  // rotation), both well-defined under every Earth model with no representation ambiguity.
  //
  // Default: empty (wind/uniform has no state to evolve). Per-frame path (§14): no allocation, no
  // exceptions. Defined out of line in wind_model.cpp, so the destructor stays the key function.
  virtual void begin_frame(const StepContext& ctx, const VehicleState& vs,
                           double altitude_m) noexcept;

  // What this wind model needs p_n to mean, WHEN IT READS vs.p_n() AT ALL -- the same contract
  // GravityModel::assumed_position_representation() makes (gravity_model.hpp), applied here for
  // the identical reason. altitude_m above already gives a representation-safe altitude, so a
  // conforming WindModel never needs to read vs.p_n() -- but nothing stops one from doing so anyway
  // (Codex adversarial review, M2 PR 2 round 2: altitude_m alone does not enforce that choice), and
  // if one does, this is the same guard gravity already relies on: earth/flat's p_n and
  // earth/round's/wgs84_rotating's p_n mean different things (NED metres vs geodetic lat/lon/alt),
  // so a plugin assuming one representation and paired with the other Earth model would silently
  // misread it without this check. Simulation::from_config compares this against the configured
  // EarthModel::position_representation() and rejects a mismatch at init, exactly like it already
  // does for physical.gravity. Default: kAny (wind/uniform reads no position at all). Non-pure and
  // defined out of line in wind_model.cpp, so the destructor stays the key function.
  [[nodiscard]] virtual PositionRepresentation assumed_position_representation() const noexcept;

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
