// docs/architecture.md §7 pins this signature. g_n is plumb-bob gravity (gravitation with
// centripetal folded in) -- the standard convention, and what M2's WGS-84 model will supply -- so
// the EOM carries no separate centripetal term.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class Config;  // core/include/astra/config/config.hpp

class GravityModel : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp).
  ~GravityModel() override;

  [[nodiscard]] virtual Eigen::Vector3d g_n(const VehicleState& vs) const noexcept = 0;

  // What this gravity model needs p_n to mean, when it reads p_n at all (M2): gravity/constant
  // leaves the kAny default (it never reads p_n); gravity/inverse_square and gravity/wgs84_j2
  // override it to kGeodetic, since they read latitude and altitude straight from vs.p_n() under
  // ADR-0003's round/WGS-84 layout. Simulation::from_config compares this against the configured
  // Earth model's EarthModel::position_representation() and rejects a mismatch at init. Non-pure
  // and defined out of line in gravity_model.cpp, so the destructor above stays the key function.
  [[nodiscard]] virtual PositionRepresentation assumed_position_representation() const noexcept;

  // Init-path (may throw ConfigError): validates a proposed initial p_n against this gravity
  // model's *own* configured geometry, the same shape as EarthModel::validate_p_n. Needed because
  // the Earth and gravity models each carry their own, independently configurable notion of
  // "radius" (Codex adversarial review, M2 PR 1): earth/round's altitude floor uses *its* radius,
  // which says nothing about whether gravity/inverse_square's own, separately configured radius
  // still makes (radius + altitude) positive at that same altitude. Default no-op (gravity/constant
  // and gravity/wgs84_j2, whose fixed WGS-84 geometry is already bounded by the paired Earth
  // model's own floor). Defined out of line, so the destructor stays the key function.
  virtual void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const;
};

}  // namespace astra
