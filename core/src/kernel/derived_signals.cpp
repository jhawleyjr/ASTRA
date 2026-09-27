#include "astra/kernel/derived_signals.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "astra/kernel/vehicle_state.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/icd_pack.hpp"

namespace astra {

namespace {

struct Field {
  std::string_view name;
  std::string_view unit;
};

// Registration order: astra_sim_to_sw_v1's truth fields in struct order, then v_n. Units are the
// ICD header's comments in SI (ADR-0002), "1" for the dimensionless mach and status_flags.
// pack() fills `values` in exactly this order.
constexpr std::array<Field, DerivedSignals::kFieldCount> kFields = {{
    {"phi", "rad"},   {"theta", "rad"},      {"psi", "rad"},    {"p", "rad/s"},
    {"q", "rad/s"},   {"r", "rad/s"},        {"u", "m/s"},      {"v", "m/s"},
    {"w", "m/s"},     {"alpha", "rad"},      {"beta", "rad"},   {"tas", "m/s"},
    {"cas", "m/s"},   {"mach", "1"},         {"altitude", "m"}, {"vertical_speed", "m/s"},
    {"lat", "rad"},   {"lon", "rad"},        {"ax", "m/s^2"},   {"ay", "m/s^2"},
    {"az", "m/s^2"},  {"status_flags", "1"}, {"v_n_0", "m/s"},  {"v_n_1", "m/s"},
    {"v_n_2", "m/s"},
}};

}  // namespace

DerivedSignals::DerivedSignals(std::span<const Vehicle> vehicles, SignalRegistry& registry) {
  std::size_t slot_count = 0;
  for (const Vehicle& vehicle : vehicles) {
    slot_count = std::max<std::size_t>(slot_count, std::size_t{vehicle.index()} + 1);
  }
  slots_.resize(slot_count);  // before any add(): the pointers below must never move
  for (const Vehicle& vehicle : vehicles) {
    if (!vehicle.has_physics()) {
      continue;
    }
    Slot& slot = slots_[vehicle.index()];
    slot.active = true;
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      registry.add(vehicle.name() + ".derived." + std::string{kFields[i].name}, &slot.values[i],
                   kFields[i].unit);
    }
  }
}

void DerivedSignals::pack(const StepContext& ctx, const Vehicle& vehicle,
                          const EnvironmentSample& sample, const EarthModel& earth) noexcept {
  if (vehicle.index() >= slots_.size() || !slots_[vehicle.index()].active) {
    return;
  }
  Slot& slot = slots_[vehicle.index()];

  // The exchange stage's rule (lockstep_exchange.cpp, critique F-6), not a second one.
  const bool previous_valid =
      ctx.frame > 0 && completed_any_ && last_completed_frame_ + 1 == ctx.frame;
  const icd_pack::PreviousFrame previous{vehicle.published_wrench(), vehicle.latched_mass()};
  // .data(), not VehicleState{vehicle.state()}: see Simulation::sample_environment's comment on the
  // dangling temporary an Eigen::Map-to-StateVector conversion would create.
  const VehicleState vs{vehicle.state().data()};
  icd_pack::pack_sim_to_sw(slot.packed, ctx, vs, sample, earth,
                           previous_valid ? &previous : nullptr);

  // The same product the packer's vertical_speed negates: vertical_speed == -v_n_2 bitwise.
  const Eigen::Vector3d v_n = frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
  const astra_sim_to_sw_v1& s = slot.packed;
  slot.values = {s.phi,      s.theta,
                 s.psi,      s.p,
                 s.q,        s.r,
                 s.u,        s.v,
                 s.w,        s.alpha,
                 s.beta,     s.tas,
                 s.cas,      s.mach,
                 s.altitude, s.vertical_speed,
                 s.lat,      s.lon,
                 s.ax,       s.ay,
                 s.az,       static_cast<double>(s.status_flags),
                 v_n(0),     v_n(1),
                 v_n(2)};
}

}  // namespace astra
