// The sim-side F16_control peer's plugin (ADR-0029 Decision 1;
// docs/plans/m7-pr11a-control-peer.md): aircraft/f16/sim/transport/nesc_control, a transport under
// the aircraft's sim/ kind (architecture §4's taxonomy rule). The peer itself, NASA's
// F16_control.dml in SI at the exchange stage, is nesc_control_peer.hpp, with the law in
// control_law.hpp. This file only names the plugin.
//
// It assumes a rotating WGS-84 Earth (geodesy::kOmegaIe), which it cannot check (the plan's Q4): a
// config pairs it with plugins/physical/earth/wgs84_rotating. Every tool that refuses a transport
// block refuses its configs (the M7 plan's Q9 (b)); Simulation flies it, from C++ or astra._core.
//
// Values follow ADR-0002 and ADR-0030: a key's suffix names the unit as written, converted once at
// init with core/util/units.
//
// Config keys:
//   peer.trim.elevator                      | number | rad | required | The trim's elevator, the
//          same text as vehicle.initial_controls.controls.elevator. Inverted at init through the
//          law's limit-and-scale block to trimmedPilotControl_long = elevator / (-25 deg), which
//          must lie in [-1, 1].
//   peer.trim.throttle                      | number | 1   | required | The trim's throttle, the
//          same text as vehicle.initial_controls.controls.throttle: trimmedPilotControl_throttle,
//          in [0, 1].
//   peer.trim.theta                         | number | rad | required | The trim's pitch, the same
//          text as initial.state.euler_nb[1]: the LQR's trimmedAlpha and trimmedTheta (alpha =
//          theta at gamma = 0, level and still air; the M7 plan's Q11 as amended 2026-10-13).
//          trimmedKEAS is commands.equivalent_airspeed_knots. Within +-90 deg.
//   peer.commands.altitude_ft              | number | ft  | required | altitudeMslCommand from
//          frame 0, in feet, converted at init.
//   peer.commands.equivalent_airspeed_knots | number | kt  | required | equivalentAirspeedCommand
//          from frame 0, KEAS, converted with the exact knot; above 0.
//   peer.commands.course_deg                | number | deg | required | trueBaseCourseCommand from
//          frame 0, degrees true, converted at init. 13.4's base course.
//   peer.schedule[].frame                   | integer | 1  | required | The frame from which this
//          entry's commands apply, at the first exchange at or after it; at least 0, strictly
//          ascending. The schedule itself is optional.
//   peer.schedule[].altitude_ft             | number | ft  | -        | altitudeMslCommand from
//          this entry's frame on; left out, it is unchanged.
//   peer.schedule[].equivalent_airspeed_knots | number | kt | -       | equivalentAirspeedCommand
//          from this entry's frame on, KEAS; above 0; left out, it is unchanged.
//   peer.schedule[].course_deg              | number | deg | -        | trueBaseCourseCommand from
//          this entry's frame on; left out, it is unchanged. Each entry changes at least one.
//   peer.lateral_offset.frame               | integer | 1  | required | 13.4's side step: from this
//          frame on, lateralDeviationError is the cross-track distance from the initial-plane line
//          through the frame-0 position at course_deg, minus offset_ft. Without lateral_offset the
//          law holds the course (lateralDeviationError 0).
//   peer.lateral_offset.offset_ft           | number | ft  | required | The offset to the right of
//          that line, in feet, converted at init.
#include <memory>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "nesc_control_peer.hpp"

namespace {

std::unique_ptr<astra::Model> create() {
  return std::make_unique<astra::aircraft_f16_nesc_control::NescControlPeer>();
}

constexpr astra::PluginDescriptor kDescriptor{"sim", "transport_nesc_control",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
