// The Earth model owns the meaning of p_n and supplies the EOM three things (docs/architecture.md
// §6, ADR-0003): the Earth rotation rate expressed in n, the transport rate of n from motion over a
// curved surface, and position kinematics. All three are identically zero for flat Earth (this PR);
// M2's round and WGS-84 models give them their real values with no change to core/kernel/eom.cpp
// ("selecting an Earth model at runtime costs nothing in the EOM", ADR-0003). §7 leaves this
// interface's method shape open -- unpinned, and the one the plan's Codex round was run against.
#pragma once

#include <Eigen/Core>

#include "astra/interfaces/model.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace astra {

class EarthModel : public Model {
 public:
  // Defined out of line in libastra_core, exactly like Model's own destructor (see
  // core/src/interfaces/model.cpp): EarthModel has no other non-pure virtual member function, so
  // without a key function each plugin's -fvisibility=hidden translation unit would emit its own
  // private copy of its vtable and typeinfo, breaking dynamic_cast<EarthModel*> and UBSan's vptr
  // check across the .so boundary the moment Simulation::from_config casts a loaded Model to this.
  ~EarthModel() override;

  // d(p_n)/dt: position kinematics in the Earth model's own representation of p_n.
  [[nodiscard]] virtual Eigen::Vector3d p_n_dot(const VehicleState& vs) const noexcept = 0;
  // Earth rotation rate expressed in n (omega_ie_n): zero for flat and round-non-rotating.
  [[nodiscard]] virtual Eigen::Vector3d omega_ie_n(const VehicleState& vs) const noexcept = 0;
  // Transport rate of n from motion over a curved surface (omega_en_n): zero for flat.
  [[nodiscard]] virtual Eigen::Vector3d omega_en_n(const VehicleState& vs) const noexcept = 0;
  // Height above the Earth model's reference surface, metres: what a later atmosphere model and
  // inverse-square/WGS-84 gravity need, and what makes "the Earth model owns the meaning of p_n"
  // concrete rather than a comment.
  [[nodiscard]] virtual double altitude(const VehicleState& vs) const noexcept = 0;
};

}  // namespace astra
