// M7b PR 4's oracle (c), the F-16's mirror and heading symmetry under F16_control
// (docs/plans/m7b-pr4-f16-symmetry.md): the one trim result and the oracle's config text, built
// here from committed text, never from a tracked F-16 config (PR 11a's tracked-config guard holds);
// the run helper, the record masks and their comparisons (section 7); and section 8's evaluators,
// with the stage rebuild, a replay precondition, and the five parts.
//
// The IC (section 5; Q9, C-01): docs/reviews/evidence/m7b-pr4-trim/trim_n_frame.out, astra trim's
// n_frame solution of base.yaml.in beside it (case 11's F-16 at case 11's latitude, longitude 0,
// 10,013 ft and heading 0, on a non-rotating sphere), recorded 2026-10-20. The config writes the
// trim's own text for the elevator, the throttle, theta, u, w and q, and a literal 0.0 for phi,
// psi, v, p, r and lon.
//
// None of this is a reference (section 3): the relation is. The masks and the parts compare the
// code under test with itself, mirrored, or with a variant that differs from it in one named place.
//
// Test-only code: it allocates and throws.
#pragma once

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../aircraft/f16/sim/transport/nesc_control/control_law.hpp"
#include "../../../aircraft/f16/sim/transport/nesc_control/nesc_control_peer.hpp"
#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/icd_pack.hpp"
#include "astra/util/units.hpp"
#include "icd/astra_icd.h"

namespace astra::test::nesc::f16_symmetry {

namespace law = astra::aircraft_f16_control_law;
namespace peer = astra::aircraft_f16_nesc_control;

// Section 5's literals, each the text trim_n_frame.out prints (repr).
inline constexpr std::string_view kElevatorText = "-0.056511560909856456";
inline constexpr std::string_view kThrottleText = "0.13890766076656494";
inline constexpr std::string_view kThetaText = "0.04622449703844172";
inline constexpr std::string_view kUText = "172.23674407873062";
inline constexpr std::string_view kWText = "7.967232214379726";
inline constexpr std::string_view kQText = "-2.7050441154484522e-05";
// The same values as doubles, for the checks that read them.
inline constexpr double kTheta = 0.04622449703844172;

// The trim's own n_frame p and r before the config replaces them with 0.0, the inputs of C-01's
// bound (section 5; trim_check.log): kappa_2(J_c) and |omega| of the trim's own solve.
inline constexpr double kTrimP = 0.0;
inline constexpr double kTrimR = 0.0;
inline constexpr double kTrimKappa = 1.0;
inline constexpr double kTrimOmegaNorm = 2.7050441154484522e-05;

// The oracle's physical setup on the non-rotating sphere (section 7), with no transport block: the
// trim's base with section 5's IC. `aero` is the vehicle.models entry for the aero.
inline std::string round_earth_text(std::string_view aero = "- plugin: aircraft/f16/physical/aero",
                                    std::string_view frames = "30001") {
  return std::string{"sim:\n  dt: 0.001\n  frames: "} + std::string{frames} +
         "\nvehicle:\n  models:\n    " + std::string{aero} +
         "\n    - plugin: aircraft/f16/hardware/propulsion/engine\n"
         "  initial_controls:\n    controls:\n      elevator: " +
         std::string{kElevatorText} +
         "\n      aileron: 0.0\n      rudder: 0.0\n      throttle: " + std::string{kThrottleText} +
         "\nphysical:\n"
         "  earth: {plugin: plugins/physical/earth/round, rotating: false}\n"
         "  gravity: {plugin: plugins/physical/gravity/inverse_square, rotating: false}\n"
         "  atmosphere: plugins/physical/atmosphere/isa1976\n"
         "  mass:\n    plugin: aircraft/f16/physical/mass\n"
         "initial:\n  state:\n"
         "    p_n: [0.6286530522161018, 0.0, 3051.9624000000003]\n"
         "    v_b: [" +
         std::string{kUText} + ", 0.0, " + std::string{kWText} +
         "]\n"
         "    euler_nb: [0.0, " +
         std::string{kThetaText} +
         ", 0.0]\n"
         "    omega_b: [0.0, " +
         std::string{kQText} + ", 0.0]\n";
}

// --- The runs (sections 7, 9 and 10) ------------------------------------------------------------

// Section 7: 13.3's shape steps the course at frame 15,000 of 30,001, and 13.4's offsets the line
// at frame 20,000 of 60,001, both at the 13.x configs' exchange divisor of 10.
inline constexpr std::uint64_t kCourseStepFrame = 15000;
inline constexpr std::uint64_t kCourseStepFrames = 30001;
inline constexpr std::uint64_t kSideStepFrame = 20000;
inline constexpr std::uint64_t kSideStepFrames = 60001;
inline constexpr std::uint64_t kDivisor = 10;

// R flies to the right of the meridian (the course +15 deg, the offset +2,000 ft), L to the left.
enum class Side : std::uint8_t { kRight, kLeft };

// A run's shape: its frame count, its step's frame, and its lines in the peer block.
struct Shape {
  std::uint64_t frames = 0;
  std::uint64_t step = 0;
  std::string peer_lines;
};

inline std::string signed_text(Side side, std::string_view magnitude) {
  return side == Side::kRight ? std::string{magnitude} : "-" + std::string{magnitude};
}

// Written +-15 and +-2000, so the two runs' commands are mirrored by construction (section 4).
inline Shape course_step(Side side, std::uint64_t step = kCourseStepFrame,
                         std::uint64_t frames = kCourseStepFrames) {
  return {frames, step,
          std::format("    schedule:\n      - {{frame: {}, course_deg: {}}}\n", step,
                      signed_text(side, "15"))};
}

inline Shape side_step(Side side, std::uint64_t step = kSideStepFrame,
                       std::uint64_t frames = kSideStepFrames) {
  return {frames, step,
          std::format("    lateral_offset: {{frame: {}, offset_ft: {}}}\n", step,
                      signed_text(side, "2000"))};
}

// The 13.x configs' transport block (case13p3_subsonic_heading_change.yaml's commands and limits),
// flown by tests/plugins/nesc_control_variant at `variant`, with section 5's trim, the IC's
// altitude and EAS commands (the 13.x configs' literals, PR 11a's ic_eas.py), the course
// `course_deg`, and the shape's lines.
inline std::string transport_text(std::string_view variant, std::string_view course_deg,
                                  std::string_view shape_lines) {
  return std::format(
      "transport:\n"
      "  plugin: tests/plugins/nesc_control_variant\n"
      "  divisor: 10\n"
      "  commands:\n"
      "    elevator: controls.elevator\n"
      "    aileron: controls.aileron\n"
      "    rudder: controls.rudder\n"
      "    throttle: [controls.throttle, controls.throttle]\n"
      "  limits:\n"
      "    elevator: [-0.43633231299858238, 0.43633231299858238]\n"
      "    aileron: [-0.37524578917878088, 0.37524578917878088]\n"
      "    rudder: [-0.52660074191172901, 0.52660074191172901]\n"
      "    throttle: [0.0, 1.0]\n"
      "  peer:\n"
      "    test_variant: {}\n"
      "    trim: {{elevator: {}, throttle: {}, theta: {}}}\n"
      "    commands: {{altitude_ft: 10013, equivalent_airspeed_knots: 287.98158203347975, "
      "course_deg: {}}}\n"
      "{}",
      variant, kElevatorText, kThrottleText, kThetaText, course_deg, shape_lines);
}

// The vehicle.models entries for the aero: the shipped plugin, or tests/plugins/f16_aero_variant.
inline constexpr std::string_view kShippedAero = "- plugin: aircraft/f16/physical/aero";
inline std::string variant_aero(std::string_view mode) {
  return std::format("- plugin: tests/plugins/f16_aero_variant\n      test_variant: {}", mode);
}

// A meridian-mirror run (section 7): the non-rotating sphere with `aero`, the course 0 and the
// shape.
inline std::string mirror_text(std::string_view variant, std::string_view aero,
                               const Shape& shape) {
  return round_earth_text(aero, std::to_string(shape.frames)) +
         transport_text(variant, "0", shape.peer_lines);
}

// Section 9's flat Earth (decision 3): earth/flat, gravity/constant at its default g and ISA-1976
// in still air, with the trim's elevator, throttle, theta and v_b, omega_b = 0, and the heading
// psi0.
inline std::string flat_earth_text(std::string_view psi0, std::uint64_t frames = kCourseStepFrames,
                                   std::string_view aero = kShippedAero) {
  return std::format(
      "sim:\n  dt: 0.001\n  frames: {}\n"
      "vehicle:\n  models:\n    {}\n    - plugin: aircraft/f16/hardware/propulsion/engine\n"
      "  initial_controls:\n    controls:\n      elevator: {}\n      aileron: 0.0\n"
      "      rudder: 0.0\n      throttle: {}\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  atmosphere: plugins/physical/atmosphere/isa1976\n"
      "  mass:\n    plugin: aircraft/f16/physical/mass\n"
      "initial:\n  state:\n"
      "    p_n: [0.0, 0.0, -3051.9624000000003]\n"
      "    v_b: [{}, 0.0, {}]\n"
      "    euler_nb: [{}, {}, 0.0]\n"
      "    omega_b: [0.0, 0.0, 0.0]\n",
      frames, aero, kElevatorText, kThrottleText, kUText, kWText, psi0, kThetaText);
}

// A heading run (section 9): the flat Earth at psi0, and the peer at `variant` in 13.3's shape,
// with the course `course_deg`, then `step_course_deg` from frame `step`.
inline std::string heading_text(std::string_view variant, std::string_view psi0,
                                std::string_view course_deg, std::string_view step_course_deg,
                                std::uint64_t step = kCourseStepFrame,
                                std::uint64_t frames = kCourseStepFrames) {
  return flat_earth_text(psi0, frames) +
         transport_text(variant, course_deg,
                        std::format("    schedule:\n      - {{frame: {}, course_deg: {}}}\n", step,
                                    step_course_deg));
}

inline std::string g17(double x) { return std::format("{:.17g}", x); }

// Section 9's three headings, as the config writes them: euler_nb[0], the course, and the step's
// course 15 deg later. 0.6 rad is written in degrees through units::radians_to_degrees, and 172.5
// deg in radians through units::degrees_to_radians, each as %.17g, which round-trips.
struct Heading {
  std::string name;
  std::string psi0;
  std::string course_deg;
  std::string step_course_deg;
};

inline std::array<Heading, 3> headings() {
  const double rotated_deg = units::radians_to_degrees(0.6);
  return {{{"psi0 = 0", "0.0", "0", "15"},
           {"psi0 = 0.6 rad", "0.6", g17(rotated_deg), g17(rotated_deg + 15.0)},
           {"psi0 = 172.5 deg", g17(units::degrees_to_radians(172.5)), "172.5", "187.5"}}};
}

// --- The run helper ------------------------------------------------------------------------------

using State = std::array<double, kStateSize>;

// A run's three recorder streams and its header (ADR-0022), copied out of the sim, and how it
// ended. The flights keep their recorders in memory and write no log (section 7).
struct Records {
  ExchangeRecorder::Header header{};
  std::vector<astra_sim_to_sw_v1> requests;
  std::vector<astra_sw_to_sim_v1> replies;
  std::vector<RecordedState> states;
  HaltReason halt;          // its source is cleared: it named storage the sim owned
  std::string halt_source;  // that source, copied
};

inline Records records_of(const Simulation& sim, HaltReason halt) {
  const ExchangeRecorder* recorder = sim.exchange_recorder();
  if (recorder == nullptr) {
    throw Error("f16_symmetry: the run has no exchange recorder");
  }
  Records out;
  const std::vector<std::byte> bytes = recorder->serialize();
  std::memcpy(&out.header, bytes.data(), sizeof out.header);
  out.requests.assign(recorder->requests().begin(), recorder->requests().end());
  out.replies.assign(recorder->replies().begin(), recorder->replies().end());
  out.states.assign(recorder->states().begin(), recorder->states().end());
  out.halt_source = std::string{halt.source};
  halt.source = {};
  out.halt = halt;
  return out;
}

inline Simulation build(const std::string& text, std::string_view name) {
  return Simulation::from_config(Config::from_string(text, name), ASTRA_TEST_PLUGIN_DIR);
}

inline Records fly(const std::string& text, std::string_view name) {
  Simulation sim = build(text, name);
  const HaltReason halt = sim.run();
  return records_of(sim, halt);
}

// Section 9's route for the base run and P's eight: q0 written through Vehicle::state(), then
// sync_stage_buffer(), before frame 0 (vehicle.hpp:241-264). The recorder's state 0 was recorded
// at open, from the config's q_nb, so it does not show the write; the request at frame 0 does.
inline Records fly_with_quaternion(const std::string& text, std::string_view name,
                                   const frames::Quaternion& q0) {
  Simulation sim = build(text, name);
  Vehicle& vehicle = sim.vehicles()[0];
  vehicle.state().segment<state::kQuaternionSize>(state::kQuaternion) = q0;
  vehicle.sync_stage_buffer();
  const HaltReason halt = sim.run();
  return records_of(sim, halt);
}

// --- The record masks (section 7): sigma is -1 for an odd field, +1 for an even one -------------

// clang-format off
inline constexpr std::array<double, kStateSize> kStateSigma = {
    1.0, -1.0,  1.0,         // lat, lon, h
    1.0, -1.0,  1.0,         // u, v, w
    1.0, -1.0,  1.0, -1.0,   // q_w, q_x, q_y, q_z
   -1.0,  1.0, -1.0};        // p, q, r
inline constexpr std::array<std::string_view, kStateSize> kStateNames = {
    "lat", "lon", "h", "u", "v", "w", "q_w", "q_x", "q_y", "q_z", "p", "q", "r"};

inline constexpr std::size_t kRequestDoubles = 22;
inline constexpr std::array<std::string_view, kRequestDoubles> kRequestNames = {
    "t", "phi", "theta", "psi", "p", "q", "r", "u", "v", "w", "alpha", "beta", "tas", "cas",
    "mach", "altitude", "vertical_speed", "lat", "lon", "ax", "ay", "az"};
inline constexpr std::array<double, kRequestDoubles> kRequestSigma = {
    1.0,                    // t
   -1.0,  1.0, -1.0,        // phi, theta, psi
   -1.0,  1.0, -1.0,        // p, q, r
    1.0, -1.0,  1.0,        // u, v, w
    1.0, -1.0,              // alpha, beta
    1.0,  1.0,  1.0,        // tas, cas, mach
    1.0,  1.0,              // altitude, vertical_speed
    1.0, -1.0,              // lat, lon
    1.0, -1.0,  1.0};       // ax, ay, az
inline std::array<double, kRequestDoubles> request_values(const astra_sim_to_sw_v1& r) {
  return {r.t, r.phi, r.theta, r.psi, r.p, r.q, r.r, r.u, r.v, r.w, r.alpha, r.beta, r.tas, r.cas,
          r.mach, r.altitude, r.vertical_speed, r.lat, r.lon, r.ax, r.ay, r.az};
}
// clang-format on
inline constexpr std::array<std::string_view, 5> kRequestIntegerNames = {
    "magic", "version", "frame", "status_flags", "reserved"};
inline std::array<std::uint64_t, 5> request_integers(const astra_sim_to_sw_v1& r) {
  return {r.magic, r.version, r.frame, r.status_flags, r.reserved};
}

inline constexpr std::size_t kReplyDoubles = 5;
inline constexpr std::array<std::string_view, kReplyDoubles> kReplyNames = {
    "elevator", "aileron", "rudder", "throttle[0]", "throttle[1]"};
inline constexpr std::array<double, kReplyDoubles> kReplySigma = {1.0, -1.0, -1.0, 1.0, 1.0};
inline std::array<double, kReplyDoubles> reply_values(const astra_sw_to_sim_v1& r) {
  return {r.elevator, r.aileron, r.rudder, r.throttle[0], r.throttle[1]};
}
inline constexpr std::array<std::string_view, 5> kReplyIntegerNames = {"magic", "version", "frame",
                                                                       "mode_flags", "fault_flags"};
inline std::array<std::uint64_t, 5> reply_integers(const astra_sw_to_sim_v1& r) {
  return {r.magic, r.version, r.frame, r.mode_flags, r.fault_flags};
}

inline State mirrored(const State& y) {
  State m{};
  for (std::size_t i = 0; i < y.size(); ++i) {
    m[i] = kStateSigma[i] * y[i];
  }
  return m;
}

inline astra_sim_to_sw_v1 mirrored(const astra_sim_to_sw_v1& r) {
  astra_sim_to_sw_v1 m = r;
  m.phi = -r.phi;
  m.psi = -r.psi;
  m.p = -r.p;
  m.r = -r.r;
  m.v = -r.v;
  m.beta = -r.beta;
  m.lon = -r.lon;
  m.ay = -r.ay;
  return m;
}

inline std::string hex(double x) { return std::format("{:a}", x); }

inline std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }

// --- The record comparisons ----------------------------------------------------------------------

// kMirror: R = sigma * L by value, so +0.0 equals -0.0 (section 7). kBitwise: the same bits (the
// wrap control against its nominal run, section 10). Integer fields must be equal under both.
enum class Compare : std::uint8_t { kMirror, kBitwise };

inline bool same(double r, double l, double sigma, Compare how) {
  return how == Compare::kMirror ? r == sigma * l : bits(r) == bits(l);
}

// A record that fails a comparison: its stream, its index in that stream, its frame, and every
// field that fails, each with both values in hex.
struct Mismatch {
  std::string stream;
  std::size_t index = 0;
  std::uint64_t frame = 0;
  std::vector<std::string> fields;
  std::string values;

  [[nodiscard]] std::string describe() const {
    return std::format("{} {} (frame {}): {}", stream, index, frame, values);
  }
};

inline Mismatch mismatch_at(std::string_view stream, std::size_t index, std::uint64_t frame) {
  Mismatch m;
  m.stream = stream;
  m.index = index;
  m.frame = frame;
  return m;
}

template <std::size_t N>
void compare_doubles(const std::array<double, N>& r, const std::array<double, N>& l,
                     const std::array<double, N>& sigma,
                     const std::array<std::string_view, N>& names, Compare how, Mismatch& m) {
  for (std::size_t i = 0; i < N; ++i) {
    if (!same(r[i], l[i], sigma[i], how)) {
      m.fields.emplace_back(names[i]);
      m.values += std::format("{} R {} L {}; ", names[i], hex(r[i]), hex(l[i]));
    }
  }
}

template <std::size_t N>
void compare_integers(const std::array<std::uint64_t, N>& r, const std::array<std::uint64_t, N>& l,
                      const std::array<std::string_view, N>& names, Mismatch& m) {
  for (std::size_t i = 0; i < N; ++i) {
    if (r[i] != l[i]) {
      m.fields.emplace_back(names[i]);
      m.values += std::format("{} R {} L {}; ", names[i], r[i], l[i]);
    }
  }
}

// The first record, in record order, that fails the comparison: for each frame f, state f, then the
// exchange at frame f if any, its request before its reply (PR 3's notation: request k is packed
// from state 10k, and reply k first acts on state 10k + 1). Unequal headers, and streams of unequal
// length past the shorter, are mismatches too.
inline std::optional<Mismatch> first_mismatch(const Records& r, const Records& l, Compare how) {
  if (std::memcmp(&r.header, &l.header, sizeof r.header) != 0) {
    Mismatch header = mismatch_at("header", 0, 0);
    header.fields.emplace_back("header");
    header.values = std::format("counts R {}/{}/{} L {}/{}/{}", r.header.request_count,
                                r.header.reply_count, r.header.state_count, l.header.request_count,
                                l.header.reply_count, l.header.state_count);
    return header;
  }
  const std::size_t states = std::min(r.states.size(), l.states.size());
  const std::size_t exchanges =
      std::min({r.requests.size(), l.requests.size(), r.replies.size(), l.replies.size()});
  std::size_t k = 0;
  for (std::size_t s = 0; s < states; ++s) {
    const std::uint64_t frame = r.states[s].frame;
    Mismatch state = mismatch_at("state", s, frame);
    if (frame != l.states[s].frame) {
      state.fields.emplace_back("frame");
      state.values += std::format("frame R {} L {}; ", frame, l.states[s].frame);
    }
    compare_doubles(r.states[s].y, l.states[s].y, kStateSigma, kStateNames, how, state);
    if (!state.fields.empty()) {
      return state;
    }
    for (; k < exchanges && r.requests[k].frame == frame; ++k) {
      Mismatch request = mismatch_at("request", k, frame);
      compare_integers(request_integers(r.requests[k]), request_integers(l.requests[k]),
                       kRequestIntegerNames, request);
      compare_doubles(request_values(r.requests[k]), request_values(l.requests[k]), kRequestSigma,
                      kRequestNames, how, request);
      if (!request.fields.empty()) {
        return request;
      }
      Mismatch reply = mismatch_at("reply", k, frame);
      compare_integers(reply_integers(r.replies[k]), reply_integers(l.replies[k]),
                       kReplyIntegerNames, reply);
      compare_doubles(reply_values(r.replies[k]), reply_values(l.replies[k]), kReplySigma,
                      kReplyNames, how, reply);
      if (!reply.fields.empty()) {
        return reply;
      }
    }
  }
  if (r.states.size() != l.states.size() || r.requests.size() != l.requests.size() ||
      r.replies.size() != l.replies.size()) {
    Mismatch count = mismatch_at("count", 0, 0);
    count.fields.emplace_back("count");
    count.values = "stream lengths differ";
    return count;
  }
  return std::nullopt;
}

// The registered recorder header (section 7, P-02): ceil(frames / 10) exchanges and frames + 1
// states, the terminal state included.
inline std::optional<std::string> header_violation(const Records& run, std::uint64_t frames) {
  const std::uint64_t exchanges = (frames + kDivisor - 1) / kDivisor;
  const ExchangeRecorder::Header& h = run.header;
  if (h.magic != ExchangeRecorder::kMagic || h.version != ExchangeRecorder::kVersion ||
      h.request_count != exchanges || h.reply_count != exchanges || h.state_count != frames + 1 ||
      run.requests.size() != exchanges || run.replies.size() != exchanges ||
      run.states.size() != frames + 1) {
    return std::format("header counts {}/{}/{} against {}/{}/{}", h.request_count, h.reply_count,
                       h.state_count, exchanges, exchanges, frames + 1);
  }
  for (std::size_t s = 0; s < run.states.size(); ++s) {
    if (run.states[s].frame != s) {
      return std::format("state {} has frame {}", s, run.states[s].frame);
    }
  }
  for (std::size_t k = 0; k < run.requests.size(); ++k) {
    if (run.requests[k].frame != k * kDivisor || run.replies[k].frame != k * kDivisor) {
      return std::format("exchange {} has frames {} and {}", k, run.requests[k].frame,
                         run.replies[k].frame);
    }
  }
  return std::nullopt;
}

// Section 5's pre-step prefix (C-01, N-01), for the runs flown with Omega = 0: every odd field of
// all three streams exactly 0 by value at every state and request with frame <= S and every reply
// with frame < S.
inline std::optional<std::string> prefix_violation(const Records& run, std::uint64_t step) {
  for (const RecordedState& s : run.states) {
    if (s.frame > step) {
      break;
    }
    for (std::size_t i = 0; i < kStateSize; ++i) {
      if (kStateSigma[i] < 0.0 && s.y[i] != 0.0) {
        return std::format("state {} {} = {}", s.frame, kStateNames[i], hex(s.y[i]));
      }
    }
  }
  for (const astra_sim_to_sw_v1& q : run.requests) {
    if (q.frame > step) {
      break;
    }
    const std::array<double, kRequestDoubles> values = request_values(q);
    for (std::size_t i = 0; i < kRequestDoubles; ++i) {
      if (kRequestSigma[i] < 0.0 && values[i] != 0.0) {
        return std::format("request at {} {} = {}", q.frame, kRequestNames[i], hex(values[i]));
      }
    }
  }
  for (const astra_sw_to_sim_v1& p : run.replies) {
    if (p.frame >= step) {
      break;
    }
    if (p.aileron != 0.0 || p.rudder != 0.0) {
      return std::format("reply at {}: aileron {}, rudder {}", p.frame, hex(p.aileron),
                         hex(p.rudder));
    }
  }
  return std::nullopt;
}

inline constexpr double kQuarterTurn = std::numbers::pi / 2.0;
inline constexpr double kHeadingPitchLimit = units::degrees_to_radians(80.0);  // section 9 (P-04)

// Section 7's signed-zero guard, scoped by P-04 to the meridian-mirror runs: |psi| < 90 deg and
// |phi| < 90 deg on every request, and u > 0 on every state and request. Outside it, section 6's
// signed-zero argument does not hold, so a departure stops the PR; it is not a miss.
inline std::optional<std::string> mirror_envelope_violation(const Records& run) {
  for (const RecordedState& s : run.states) {
    if (!(s.y[state::kVelocity] > 0.0)) {
      return std::format("state {}: u = {}", s.frame, s.y[state::kVelocity]);
    }
  }
  for (const astra_sim_to_sw_v1& q : run.requests) {
    const bool inside =
        std::abs(q.psi) < kQuarterTurn && std::abs(q.phi) < kQuarterTurn && q.u > 0.0;
    if (!inside) {  // NaN-safe: a NaN field is outside
      return std::format("request at {}: psi {}, phi {}, u {}", q.frame, q.psi, q.phi, q.u);
    }
  }
  return std::nullopt;
}

// Section 9's heading guards (P-04), for the base, P's eight, the rotated runs and the wrap
// control: u > 0 on every state and request, and |theta| < 80 deg on every request, away from the
// 3-2-1 singularity. The caller checks the halt.
inline std::optional<std::string> heading_guard_violation(const Records& run) {
  for (const RecordedState& s : run.states) {
    if (!(s.y[state::kVelocity] > 0.0)) {
      return std::format("state {}: u = {}", s.frame, s.y[state::kVelocity]);
    }
  }
  for (const astra_sim_to_sw_v1& q : run.requests) {
    const bool inside = q.u > 0.0 && std::abs(q.theta) < kHeadingPitchLimit;
    if (!inside) {  // NaN-safe: a NaN field is outside
      return std::format("request at {}: u {}, theta {}", q.frame, q.u, q.theta);
    }
  }
  return std::nullopt;
}

// --- Section 8's report, not a bound ------------------------------------------------------------

// The sign of the reply aileron by value: -1, 0 or +1, so +-0 both count as 0 (amendment 11).
inline int aileron_sign(const astra_sw_to_sim_v1& reply) {
  if (reply.aileron > 0.0) {
    return 1;
  }
  if (reply.aileron < 0.0) {
    return -1;
  }
  return 0;
}

// A reversal is an exchange whose reply aileron's sign differs from the previous exchange's.
inline std::vector<std::size_t> reversals(const Records& run) {
  std::vector<std::size_t> out;
  for (std::size_t k = 1; k < run.replies.size(); ++k) {
    if (aileron_sign(run.replies[k]) != aileron_sign(run.replies[k - 1])) {
      out.push_back(k);
    }
  }
  return out;
}

template <std::size_t N, typename Record, typename Values>
void print_largest(std::string_view name, std::string_view stream, const std::vector<Record>& r,
                   const std::vector<Record>& l, const std::array<double, N>& sigma,
                   const std::array<std::string_view, N>& names, const Values& values) {
  for (std::size_t i = 0; i < N; ++i) {
    double worst = 0.0;
    std::size_t at = 0;
    for (std::size_t k = 0; k < std::min(r.size(), l.size()); ++k) {
      const double e = std::abs(values(r[k])[i] - sigma[i] * values(l[k])[i]);
      if (e > worst) {
        worst = e;
        at = k;
      }
    }
    std::cout << std::format("{}: {} {}: max |e| {:.3e} at record {} (R {:+.6e}, L {:+.6e})\n",
                             name, stream, names[i], worst, at, values(r[at])[i], values(l[at])[i]);
  }
}

// Section 8's trajectory report: per field of all three streams, max |e_f| with e_f = R_f -
// sigma_f L_f and the record where it falls, then R's and L's aileron reversals as pairs in order,
// each with its difference in exchanges, the unpaired ones after, and the count on each side.
inline void print_trajectory_report(std::string_view name, const Records& r, const Records& l) {
  print_largest(name, "state", r.states, l.states, kStateSigma, kStateNames,
                [](const RecordedState& s) { return s.y; });
  print_largest(name, "request", r.requests, l.requests, kRequestSigma, kRequestNames,
                [](const astra_sim_to_sw_v1& q) { return request_values(q); });
  print_largest(name, "reply", r.replies, l.replies, kReplySigma, kReplyNames,
                [](const astra_sw_to_sim_v1& p) { return reply_values(p); });
  const std::vector<std::size_t> right = reversals(r);
  const std::vector<std::size_t> left = reversals(l);
  std::cout << std::format("{}: aileron reversals: R {}, L {}\n", name, right.size(), left.size());
  std::string line;
  const std::size_t paired = std::min(right.size(), left.size());
  for (std::size_t i = 0; i < paired; ++i) {
    const auto difference =
        static_cast<std::int64_t>(left[i]) - static_cast<std::int64_t>(right[i]);
    line += std::format("({}, {}, {:+}) ", right[i], left[i], difference);
    if ((i + 1) % 8 == 0 || i + 1 == paired) {
      std::cout << std::format("{}:   pairs (R, L, L - R): {}\n", name, line);
      line.clear();
    }
  }
  const std::vector<std::size_t>& longer = right.size() > left.size() ? right : left;
  for (std::size_t i = paired; i < longer.size(); ++i) {
    line += std::format("{} ", longer[i]);
  }
  if (!line.empty()) {
    std::cout << std::format("{}:   unpaired ({}): {}\n", name,
                             right.size() > left.size() ? "R" : "L", line);
  }
}

// --- Section 8's evaluators ----------------------------------------------------------------------

// The controls latched for a frame: the reply of the last exchange at or before it, as the exchange
// writes them, unclamped (lockstep_exchange.cpp:284-296).
struct Controls {
  double elevator = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;
  double throttle = 0.0;
};

inline Controls controls_of(const astra_sw_to_sim_v1& reply) {
  return {reply.elevator, reply.aileron, reply.rudder, reply.throttle[0]};
}

// M acts on the controls as u -> (elevator, -aileron, -rudder, throttle).
inline Controls mirrored(const Controls& u) {
  return {u.elevator, -u.aileron, -u.rudder, u.throttle};
}

inline bool bitwise_equal(const Controls& a, const Controls& b) {
  return bits(a.elevator) == bits(b.elevator) && bits(a.aileron) == bits(b.aileron) &&
         bits(a.rudder) == bits(b.rudder) && bits(a.throttle) == bits(b.throttle);
}

// Section 8's f(x, u): Simulation::derivative() (simulation.hpp:262), the same
// Vehicle::evaluate_derivative_stage each RK4 stage calls (vehicle.cpp:440-481), on a sim built
// from a config text with no transport. Its controls are written through write_input
// (simulation.hpp:249), as trim.py's residual3 writes them. The sim never steps, so its clock stays
// at frame 0, and no model reads a stage's t: still air, ISA-1976 (Joe's third decision, point 1).
class Evaluator {
 public:
  Evaluator(const std::string& text, std::string_view name) : sim_(build(text, name)) {}

  void set(const Controls& u) {
    if (written_ && bitwise_equal(u, u_)) {
      return;
    }
    sim_.write_input("controls.elevator", u.elevator);
    sim_.write_input("controls.aileron", u.aileron);
    sim_.write_input("controls.rudder", u.rudder);
    sim_.write_input("controls.throttle", u.throttle);
    u_ = u;
    written_ = true;
  }

  [[nodiscard]] State f(const State& x) {
    const std::vector<double> d = sim_.derivative(x);
    State out{};
    std::copy(d.begin(), d.end(), out.begin());
    return out;
  }

  [[nodiscard]] double dt() const { return sim_.clock().dt(); }
  // The mass derivative() latched (Vehicle::derivative calls begin_frame).
  [[nodiscard]] double mass() const { return sim_.vehicles()[0].latched_mass(); }

 private:
  Simulation sim_;
  Controls u_;
  bool written_ = false;
};

// A plugin model loaded and initialized alone, outside any sim (control_test.cpp's LoadedModel):
// part (e)'s Earth model and atmosphere.
template <typename Model>
class LoadedModel {
 public:
  LoadedModel(std::string_view path, std::string_view config_text) : loaded_(loader_.load(path)) {
    model_ = dynamic_cast<Model*>(loaded_.model.get());
    if (model_ == nullptr) {
      throw Error("f16_symmetry: unexpected plugin type: " + std::string{path});
    }
    SignalRegistry registry;
    model_->init(Config::from_string(config_text, "f16-symmetry part (e)"), registry);
  }

  [[nodiscard]] Model& get() const { return *model_; }

 private:
  PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  LoadedPlugin loaded_;
  Model* model_ = nullptr;
};

// The components of f that the four tables can reach (section 8): p_dot and r_dot. And v, whose
// sign picks part (c)'s x'.
inline constexpr std::size_t kPDot = 10;
inline constexpr std::size_t kRDot = 12;
inline constexpr std::size_t kV = 4;

// One part failing at one operand, state or exchange: where, which part, the failing fields, and
// each one's two compared values.
struct PartFailure {
  std::string where;
  char part = ' ';
  std::vector<std::string> fields;
  std::string values;
};

// A part's failing fields at one evaluation, gathered before PartsReport keeps them.
struct Failing {
  std::vector<std::string> fields;
  std::string values;

  void add(std::string field, double lhs, double rhs) {
    values += std::format("{} {:+.6e} ({}) vs {:+.6e} ({}); ", field, lhs, hex(lhs), rhs, hex(rhs));
    fields.push_back(std::move(field));
  }
};

inline constexpr std::size_t kKeptFailures = 32;

struct PartsReport {
  // The stage rebuild's first miss: its frame and field, both values in hex. When it is set, the
  // pass stopped there, and no part's result is reported (Joe's third decision, point 1).
  std::optional<std::string> rebuild_miss;
  std::vector<PartFailure> failures;  // the first kKeptFailures, in section 8's read order
  std::size_t failure_count = 0;
  std::size_t operands = 0;
  std::size_t exchanges = 0;
};

// What the parts read besides the run's records.
struct PartsSetup {
  std::string own_text;     // the run's own aero, no transport: f_shipped, or the doubled model
  std::string abs_text;     // the same text with f16_aero_variant abs_beta_tables: f_abs
  std::string flight_text;  // the flown config: the test's peer reads its transport block
};

// Section 8 over one run, through state `last` (the terminal state by default), in its read order
// (Joe's third decision, point 2): state 0 and the exchange at frame 0, then, for each frame j,
// frame j's stage operands 1 to 4, state j + 1, and the exchange at j + 1 if any. Operand 1 of
// frame j is state j itself (k1 = f(t, y)), so it is read once, as state j. Each operand and state
// is read for (a), (b) and (c), and each exchange for (d) and (e).
//
// The stage rebuild runs alongside, a replay precondition in PR 3's N-03 form, never an oracle
// (point 1). For each frame it rebuilds operands 2 to 4 and the next state from the recorded state
// and f_shipped, in integrator.hpp:109-121's operation order, renormalized as
// RigidBody6DofDynamics::post_step does (rigid_body_6dof_dynamics.cpp:63-67, frames.hpp:123-130).
// The rebuilt next state must equal the recorded one bit for bit. On the first miss the pass stops,
// and the report carries the miss alone: it is a test-side question for Joe, never a finding about
// the path under test.
inline PartsReport read_parts(const Records& run, const PartsSetup& setup,
                              std::uint64_t last = std::numeric_limits<std::uint64_t>::max()) {
  if (run.states.empty() || run.requests.empty() || run.requests.size() != run.replies.size() ||
      run.requests.front().frame != 0) {
    throw Error("f16_symmetry: read_parts needs states and an exchange at frame 0");
  }
  for (std::size_t s = 0; s < run.states.size(); ++s) {
    if (run.states[s].frame != s) {
      throw Error(std::format("f16_symmetry: state {} has frame {}", s, run.states[s].frame));
    }
  }

  // f_shipped and f_abs, each at (x, u) and at (M x, M u): four evaluators, so each holds its
  // controls for a frame.
  Evaluator own{setup.own_text, "f16-symmetry f_shipped(x, u)"};
  Evaluator own_mirrored{setup.own_text, "f16-symmetry f_shipped(Mx, Mu)"};
  Evaluator symmetric{setup.abs_text, "f16-symmetry f_abs(x, u)"};
  Evaluator symmetric_mirrored{setup.abs_text, "f16-symmetry f_abs(Mx, Mu)"};

  // Part (d)'s route (section 8): a second instance of the shipped peer class under kNone, from the
  // run's own transport text, published each recorded request in turn, so its commands() apply the
  // schedule and the offset as the flight's peer did. Its trim() and references() are the peer's
  // own (amendment 10's accessors).
  peer::Options options;
  options.earth_rate = law::EarthRate::kNone;
  peer::NescControlPeer replay{options};
  SignalRegistry replay_registry;
  replay.init(Config::from_string(setup.flight_text, "f16-symmetry part (d)").at("transport"),
              replay_registry);

  // Part (e)'s rebuilt environment: separate earth/round (rotating: false, as flown) and ISA-1976
  // instances, in still air.
  const LoadedModel<EarthModel> earth{"plugins/physical/earth/round", "rotating: false\n"};
  const LoadedModel<AtmosphereModel> atmosphere{"plugins/physical/atmosphere/isa1976", "{}\n"};

  PartsReport report;
  const auto note = [&report](const auto& where, char part, Failing failing) {
    if (failing.fields.empty()) {
      return;
    }
    if (report.failures.size() < kKeptFailures) {
      report.failures.push_back(
          {std::string{where}, part, std::move(failing.fields), std::move(failing.values)});
    }
    ++report.failure_count;
  };

  // (a), (b) and (c) at one operand, with the evaluators' latched controls. Returns f_shipped(x,
  // u), the rebuild's k for that operand. `where` is formatted only when a part fails.
  const auto read_operand = [&](const State& x, const auto& where) {
    const State mx = mirrored(x);
    const State fs = own.f(x);
    const State fs_m = own_mirrored.f(mx);
    const State fa = symmetric.f(x);
    const State fa_m = symmetric_mirrored.f(mx);
    const bool x_prime_is_x = x[kV] >= 0.0;  // beta >= 0 there, +-0 included (section 8 (c))
    Failing a;
    Failing b;
    Failing c;
    for (std::size_t i = 0; i < kStateSize; ++i) {
      const auto name = [i] { return std::format("{}_dot", kStateNames[i]); };
      if (i != kPDot && i != kRDot && fs[i] != kStateSigma[i] * fs_m[i]) {
        a.add(name(), fs[i], kStateSigma[i] * fs_m[i]);
      }
      if (fa[i] != kStateSigma[i] * fa_m[i]) {
        b.add(name(), fa[i], kStateSigma[i] * fa_m[i]);
      }
      if (x_prime_is_x ? fs[i] != fa[i] : fs_m[i] != fa_m[i]) {
        c.add(name(), x_prime_is_x ? fs[i] : fs_m[i], x_prime_is_x ? fa[i] : fa_m[i]);
      }
    }
    if (!a.fields.empty() || !b.fields.empty() || !c.fields.empty()) {
      const std::string at = where();
      note(at, 'a', std::move(a));
      note(at, 'b', std::move(b));
      note(at, 'c', std::move(c));
    }
    ++report.operands;
    return fs;
  };

  const double dt = own.dt();
  // (d) and (e) at one exchange.
  const auto read_exchange = [&](std::size_t k) {
    const astra_sim_to_sw_v1& request = run.requests[k];
    const std::string where = std::format("exchange {}", k);

    // (d): law_kNone(req, cmd) == M law_kNone(M req, M cmd), with course and lateral deviation
    // negated in M cmd.
    replay.publish(request, static_cast<std::uint32_t>(request.frame));
    const law::Commands cmd = replay.commands();
    law::Commands cmd_m = cmd;
    cmd_m.course = -cmd.course;
    cmd_m.lateral_deviation_error = -cmd.lateral_deviation_error;
    const law::IcdCommands out =
        law::to_icd(law::evaluate(law::inputs_from(request, law::EarthRate::kNone), cmd,
                                  replay.trim(), law::Modes{}, replay.references()));
    const law::IcdCommands out_m =
        law::to_icd(law::evaluate(law::inputs_from(mirrored(request), law::EarthRate::kNone), cmd_m,
                                  replay.trim(), law::Modes{}, replay.references()));
    Failing d;
    if (out.elevator != out_m.elevator) {
      d.add("elevator", out.elevator, out_m.elevator);
    }
    if (out.aileron != -out_m.aileron) {
      d.add("aileron", out.aileron, -out_m.aileron);
    }
    if (out.rudder != -out_m.rudder) {
      d.add("rudder", out.rudder, -out_m.rudder);
    }
    if (out.throttle != out_m.throttle) {
      d.add("throttle", out.throttle, out_m.throttle);
    }
    note(where, 'd', std::move(d));

    // (e): pack_sim_to_sw at (M x, M F) == M pack_sim_to_sw at (x, F), under section 7's request
    // mask (Joe's third decision, point 3). x is the state the flight packed from; F is the
    // recorded request's specific force times the mass, so (e) sees only the packer's own parity on
    // given inputs. The rebuilt environment gives the altitude (and the atmosphere's argument) and,
    // being geodetic, lat and lon from p_n; ISA-1976 gives the pressure and the speed of sound;
    // still air gives v_rel = v_b.
    const State& x = run.states[request.frame].y;
    const State x_m = mirrored(x);
    const StepContext ctx{
        .t = static_cast<double>(request.frame) * dt, .dt = dt, .frame = request.frame};
    const double mass = own.mass();
    const bool accel_valid = (request.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID) == 0U;
    icd_pack::PreviousFrame previous;
    previous.wrench_total.force_b = mass * Eigen::Vector3d{request.ax, request.ay, request.az};
    previous.mass = mass;
    icd_pack::PreviousFrame previous_m = previous;
    previous_m.wrench_total.force_b.y() = -previous.wrench_total.force_b.y();
    const auto pack = [&](const State& at, const icd_pack::PreviousFrame& prev) {
      const VehicleState vs{at.data()};
      EnvironmentSample env;
      env.wind_n = Eigen::Vector3d::Zero();
      env.atmosphere = atmosphere.get().at(earth.get().altitude(vs));
      astra_sim_to_sw_v1 frame_out{};
      icd_pack::pack_sim_to_sw(frame_out, ctx, vs, env, earth.get(), accel_valid ? &prev : nullptr);
      return frame_out;
    };
    const astra_sim_to_sw_v1 packed = pack(x, previous);
    const astra_sim_to_sw_v1 packed_m = pack(x_m, previous_m);
    Mismatch e = mismatch_at("packed request", k, request.frame);
    compare_integers(request_integers(packed), request_integers(packed_m), kRequestIntegerNames, e);
    compare_doubles(request_values(packed), request_values(packed_m), kRequestSigma, kRequestNames,
                    Compare::kMirror, e);
    note(where, 'e', Failing{std::move(e.fields), std::move(e.values)});
    ++report.exchanges;
  };

  std::size_t next = 0;  // the next exchange to read
  // At an exchange's frame, latches its reply's controls into the four evaluators and returns its
  // index: the controls of that frame and of every frame up to the next exchange.
  const auto exchange_at = [&](std::uint64_t frame) -> std::optional<std::size_t> {
    if (next >= run.requests.size() || run.requests[next].frame != frame) {
      return std::nullopt;
    }
    const Controls u = controls_of(run.replies[next]);
    own.set(u);
    own_mirrored.set(mirrored(u));
    symmetric.set(u);
    symmetric_mirrored.set(mirrored(u));
    return next++;
  };

  const std::uint64_t final_state = std::min<std::uint64_t>(last, run.states.size() - 1);
  const double half_dt = dt / 2.0;
  const std::optional<std::size_t> first = exchange_at(0);
  if (!first) {
    throw Error("f16_symmetry: no exchange at frame 0");
  }
  State k1 = read_operand(run.states[0].y, [] { return std::string{"state 0"}; });
  read_exchange(*first);
  for (std::uint64_t j = 0; j < final_state; ++j) {
    const State& y = run.states[j].y;
    State operand{};
    for (std::size_t i = 0; i < kStateSize; ++i) {
      operand[i] = y[i] + half_dt * k1[i];
    }
    const State k2 = read_operand(operand, [j] { return std::format("frame {}'s operand 2", j); });
    for (std::size_t i = 0; i < kStateSize; ++i) {
      operand[i] = y[i] + half_dt * k2[i];
    }
    const State k3 = read_operand(operand, [j] { return std::format("frame {}'s operand 3", j); });
    for (std::size_t i = 0; i < kStateSize; ++i) {
      operand[i] = y[i] + dt * k3[i];
    }
    const State k4 = read_operand(operand, [j] { return std::format("frame {}'s operand 4", j); });
    State rebuilt{};
    for (std::size_t i = 0; i < kStateSize; ++i) {
      rebuilt[i] = y[i] + (dt / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    }
    static_cast<void>(
        frames::normalize_quaternion(Eigen::Map<StateVector>(rebuilt.data())
                                         .segment<state::kQuaternionSize>(state::kQuaternion)));
    const State& recorded = run.states[j + 1].y;
    for (std::size_t i = 0; i < kStateSize; ++i) {
      if (bits(rebuilt[i]) != bits(recorded[i])) {
        report.rebuild_miss = std::format("frame {} to state {}: {} rebuilt {} recorded {}", j,
                                          j + 1, kStateNames[i], hex(rebuilt[i]), hex(recorded[i]));
        report.failures.clear();
        report.failure_count = 0;
        return report;
      }
    }
    const std::optional<std::size_t> exchange = exchange_at(j + 1);
    k1 = read_operand(recorded, [j] { return std::format("state {}", j + 1); });
    if (exchange) {
      read_exchange(*exchange);
    }
  }
  return report;
}

// One line per kept failure, and the totals.
inline std::string describe(const PartsReport& report) {
  if (report.rebuild_miss) {
    return "the stage rebuild missed (a test-side question for Joe): " + *report.rebuild_miss;
  }
  std::string out = std::format("{} operands and {} exchanges read; {} part failures",
                                report.operands, report.exchanges, report.failure_count);
  for (const PartFailure& f : report.failures) {
    out += std::format("\n  {}: part ({}) in", f.where, f.part);
    for (const std::string& field : f.fields) {
      out += " " + field;
    }
    out += "\n    " + f.values;
  }
  return out;
}

}  // namespace astra::test::nesc::f16_symmetry
