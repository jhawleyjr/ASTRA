// The notional transport's sim-side autopilot peer's plugin (M7b's oracle (b),
// docs/plans/m7b-pr3-autopilot-peer.md; ADR-0029 Decision 1, ADR-0034 Decision 8):
// aircraft/notional_transport/sim/transport/autopilot, a transport under the aircraft's sim/ kind
// (architecture §4's taxonomy rule). The peer itself is autopilot_peer.hpp, with the law in
// autopilot_law.hpp: a C++23 second implementation of embedded/autopilot's law, bitwise equal to it
// on ADR-0029 Decision 8's objects. This file only names the plugin, and instantiates only
// BasicAutopilotPeer<Law> with Gains{} (the plan's amendment 2).
//
// Its configs are
// aircraft/notional_transport/configs/cruise_{light,moderate}_turbulence_autopilot_peer.yaml. Every
// tool that refuses a transport block refuses them (the M7 plan's Q9 (b)); Simulation flies them,
// from C++ or astra._core.
//
// Values are SI (ADR-0002, ADR-0030), and each key is the autopilot's command-line argument of the
// same meaning (embedded/autopilot/cli.cpp), with the same rule.
//
// Config keys:
//   peer.setpoints.altitude | number | m   | required | The altitude to hold (astra_autopilot's
//          --altitude-m). Finite.
//   peer.setpoints.airspeed | number | m/s | required | The true airspeed (tas) to hold
//          (--airspeed-mps). Finite and above 0.
//   peer.setpoints.heading  | number | rad | required | The heading to hold (--heading-rad); the
//          error is wrapped to [-pi, pi] by remainder. Finite.
//   peer.trim.elevator      | number | rad | required | The trim's elevator (--elevator-trim), the
//          same text as vehicle.initial_controls.controls.elevator: the elevator offset's bias, and
//          the reply until the first usable frame. Within the elevator limit, +-0.3.
//   peer.trim.throttle      | number | 1   | required | The trim's throttle (--throttle-trim), the
//          same text as vehicle.initial_controls.controls.throttle. In [0, 1].
//   peer.trim.theta         | number | rad | required | The trim's pitch (--theta-trim), the same
//          text as initial.state.euler_nb[1] (theta = alpha at gamma = 0). Finite.
#include <memory>

#include "astra/core_api.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "autopilot_peer.hpp"

namespace {

std::unique_ptr<astra::Model> create() {
  return std::make_unique<astra::aircraft_notional_transport_autopilot::AutopilotPeer>();
}

constexpr astra::PluginDescriptor kDescriptor{"sim", "transport_autopilot", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
