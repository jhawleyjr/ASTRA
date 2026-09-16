// docs/architecture.md §7 pins at()'s signature; §14's per-frame rule (no alloc, no throw) is why
// it is noexcept, and why AtmoState carries no status field (an out-of-range altitude cannot report
// itself by throwing). Unlike EarthModel/GravityModel, at() takes an altitude, a plain double, not
// a VehicleState -- so no AtmosphereModel can read p_n's geometry, and there is deliberately no
// assumed_position_representation() here (see the comment on EarthModel::position_representation()
// in earth_model.hpp for what that would have meant, and why it never applies to this interface).
#pragma once

#include <limits>

#include "astra/interfaces/model.hpp"

namespace astra {

// NaN by default: a default-constructed AtmoState (or one from outside valid_range(), see at()
// below) must not read as a plausible zero-density atmosphere.
struct AtmoState {
  double temperature_k = std::numeric_limits<double>::quiet_NaN();
  double pressure_pa = std::numeric_limits<double>::quiet_NaN();
  double density_kg_m3 = std::numeric_limits<double>::quiet_NaN();
  double speed_of_sound_m_s = std::numeric_limits<double>::quiet_NaN();
};

// Closed interval of altitudes, metres above the Earth model's reference surface (ADR-0003; the
// same height EarthModel::altitude returns), over which at() returns meaningful values. Both bounds
// inclusive.
struct AltitudeRange {
  double min_m = 0.0;
  double max_m = 0.0;
};

class AtmosphereModel : public Model {
 public:
  // Defined out of line in libastra_core, exactly like Model's own destructor (core/src/interfaces/
  // model.cpp): plugins compile with -fvisibility=hidden and would otherwise each carry a private
  // copy of this vtable and typeinfo, breaking dynamic_cast<AtmosphereModel*> and UBSan's vptr
  // check across the .so boundary the moment Simulation::from_config casts a loaded Model to this.
  ~AtmosphereModel() override;

  // Per-frame path (§14): no allocation, no exceptions. Returns an all-NaN AtmoState outside
  // valid_range() rather than clamping -- a clamped value would be a plausible density for an
  // altitude the model does not model. As of M2 PR 1b nothing consumes an AtmoState yet, so this
  // NaN is not itself observed by anything; from PR 3 on, a WrenchSource that reads it into a
  // force/moment makes it visible to HealthMonitor's existing check_wrench (HealthMonitor checks
  // state vectors and wrench contributions directly, never AtmoState itself).
  //
  // altitude_m is exactly whatever the configured EarthModel::altitude() returns -- "height above
  // the Earth model's reference surface" (earth_model.hpp), not independently defined here. For
  // earth/flat and earth/round that reference surface is exact by construction (NED origin;
  // sphere), but for earth/wgs84_rotating it is the WGS-84 ELLIPSOID (ADR-0003's p_n(2)), not a
  // sea-level/geoid reference. An atmosphere model (plugins/physical/atmosphere/isa1976/isa1976.cpp
  // documents the resulting bound for that plugin) that treats altitude_m as geometric height above
  // sea level is therefore off by the geoid undulation when paired with wgs84_rotating -- a known,
  // disclosed gap (Codex adversarial review, M2 PR 1b), not something this interface corrects.
  [[nodiscard]] virtual AtmoState at(double altitude_m) const noexcept = 0;

  // The range at() is valid over. Data, not a validator: unlike EarthModel/GravityModel's
  // validate_p_n (which each check their own math's singularities), every AtmosphereModel's whole
  // domain is this one interval, so Simulation::from_config compares it directly against the
  // initial altitude instead of calling back into the model.
  //
  // Known gap, disclosed not fixed (Codex adversarial review, M2 PR 1b; PLAN.md's M2 PR 1b line):
  // that comparison is init-time only, the same shape as PR 1's EarthModel/GravityModel
  // validate_p_n gap -- a trajectory that leaves this range mid-run is not re-validated per frame.
  // Must be resolved, or consciously re-accepted, before a WrenchSource (PR 3 on) makes at()'s
  // output actually reach the EOM.
  [[nodiscard]] virtual AltitudeRange valid_range() const noexcept = 0;
};

}  // namespace astra
