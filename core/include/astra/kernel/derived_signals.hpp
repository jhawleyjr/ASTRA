// The derived flight-condition signals (ADR-0026; M5 PR 3, docs/plans/m5-tooling.md Q13). Owned
// by Simulation, which calls pack() once per physics vehicle per frame at docs/architecture.md §5's
// frame-start point: right after that vehicle's environment sample, before the transport exchange
// and before the vehicle's step(). pack() makes one separate icd_pack::pack_sim_to_sw call into a
// stage-owned astra_sim_to_sw_v1 and copies its 22 truth fields, plus the ground-relative NED
// velocity v_n = frames::rotate_b_to_n(q_nb, v_b), into the published v<i>.derived.<field>
// doubles. The exchange stage keeps its own call (lockstep_exchange.cpp is untouched), so the
// request bytes it publishes cannot change; nothing here is a second derivation.
//
// A field made invalid by a status bit keeps the ICD's convention: the bit is set in status_flags
// (published as a double that holds the integer bitmask exactly) and the value is the defined 0.
//
// Timing exception (ADR-0026): ax ay az are the ICD's previous-frame specific force (frame k-1's
// k1-stage wrench_total force over frame k-1's mass), so at frame k they describe t_{k-1}.
// PreviousFrame is supplied only when this Simulation completed frame k-1 itself, the exchange
// stage's own rule (Vehicle::derivative() latches a mass without integrating, so the mass alone
// proves nothing); otherwise the packer sets ASTRA_ICD_STATUS_ACCEL_INVALID, as at every frame 0.
// Its wrench and mass are a snapshot taken when frame k-1 completed, not the vehicle's live
// published_wrench(): a halted attempt at frame k has already overwritten that with frame k's k1
// wrench (Vehicle::integrate publishes at k1, before a later stage or vehicle can halt), and a
// non-transport retry of frame k would otherwise pack it as frame k-1's (Codex review F1). On every
// completed frame the two are equal, so the exchange's request is unaffected.
//
// Per-frame methods are noexcept and allocation-free; construction may allocate/throw.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/icd_pack.hpp"
#include "icd/astra_icd.h"

namespace astra {

class DerivedSignals {
 public:
  // The 22 astra_sim_to_sw_v1 truth fields plus v_n_0..2.
  static constexpr std::size_t kFieldCount = 25;

  // Init-time: registers v<i>.derived.* for every vehicle with physics, in the ICD's field order
  // then v_n_0..2. Throws what SignalRegistry::add throws (a duplicate name, a frozen registry).
  DerivedSignals(std::span<const Vehicle> vehicles, SignalRegistry& registry);

  // Packs `vehicle`'s frame-start truth and publishes it; a no-op for a vehicle without physics.
  // `sample` is this frame's environment sample for `vehicle`, `earth` the Simulation's Earth
  // model.
  void pack(const StepContext& ctx, const Vehicle& vehicle, const EnvironmentSample& sample,
            const EarthModel& earth) noexcept;

  // Records that this Simulation completed `frame` (every vehicle integrated, clock about to
  // advance), the only thing that makes the next frame's PreviousFrame valid, and snapshots each
  // physics vehicle's k1 wrench_total and mass of that frame for it.
  void note_completed(std::span<const Vehicle> vehicles, std::uint64_t frame) noexcept;

 private:
  struct Slot {
    bool active = false;  // the vehicle has physics, so its signals are registered
    astra_sim_to_sw_v1 packed{};
    icd_pack::PreviousFrame previous{};  // as of the last completed frame
    std::array<double, kFieldCount> values{};
  };

  // One per vehicle, indexed by Vehicle::index(); sized once before any signal registers and never
  // resized, so the registered pointers into `values` stay valid (Simulation holds this stage by
  // unique_ptr for the same reason across its own moves).
  std::vector<Slot> slots_;
  std::uint64_t last_completed_frame_ = 0;
  bool completed_any_ = false;
};

}  // namespace astra
