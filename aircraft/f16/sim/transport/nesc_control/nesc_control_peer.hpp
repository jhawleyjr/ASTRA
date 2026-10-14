// The sim-side F16_control peer (ADR-0029 Decision 1; docs/plans/m7-pr11a-control-peer.md): an
// in-process Transport, after LoopbackTransport's precedent, that runs control_law.hpp's SI
// realization of NASA's F16_control.dml on each astra_sim_to_sw_v1 frame the exchange packs and
// replies with its astra_sw_to_sim_v1 commands. Its config keys, and the plugin's descriptor, are
// in nesc_control.cpp. The class lives in this header so that tests/plugins/nesc_control_variant
// can hold one by including it, never by linking the shipped .so (the plan's amendment 1).
//
// What it does at each exchange (publish()):
// 1. the refusal check (ADR-0029 Decision 2): a frame carrying CAS_SUPERSONIC, ATMOSPHERE_INVALID,
//    WIND_INVALID or LATLON_NOT_MODELED, or whose Mach is not above zero, is never computed from;
//    with 13.4's lateral offset configured, neither is a first exchange whose frame is not 0. The
//    reply's five commands are quiet NaN, so the exchange halts kTransport at its non-finite
//    check, naming astra_sw_to_sim_v1.elevator, and transport.peer.refusal holds the refused bits;
// 2. the schedule: each entry's commands apply from the first exchange at or after its frame
//    (ADR-0029 Decision 3), as the loopback's do;
// 3. 13.4's logic (ADR-0029 Decision 6): the base point lat0, lon0 and h0 is latched from the
//    frame-0 exchange, and from the lateral offset's frame on, lateralDeviationError = d - offset,
//    d the signed cross-track distance from the initial-plane line through the base point at the
//    initial course command, positive to the right; 0 before. It reads only lat and lon;
// 4. EAS from cas and mach, the body rate relative to the Earth, the law at the LQR references, and
//    the three boundary steps (control_law.hpp);
// 5. the reply, zeroed first (ADR-0022), echoing the request's frame.
// Its commands reach the inputs only through the exchange's command map (ADR-0017, ADR-0022),
// staged and latched the same frame. It reads no clock (ADR-0023) and holds no write handle.
//
// The LQR references (the M7 plan's Q11, as amended 2026-10-13 after the measurement): trimmedAlpha
// and trimmedTheta are the trim's own pitch, peer.trim.theta (alpha = theta at gamma = 0, level and
// still air), and trimmedKEAS is the initial equivalent-airspeed command, so the references and the
// trimmed stick describe one equilibrium. The DML's hard-coded references are a reported variant
// only (tests/plugins/nesc_control_variant's dml_references).
//
// The assumption it cannot check (the plan's Q4, Joe's decision): it subtracts a rotating WGS-84
// Earth's rate, geodesy::kOmegaIe, from ICD v1's inertial body rates. The peer sees only the ICD
// frame and its own config block, so it cannot see the configured Earth model. A flat Earth is
// refused at every frame (LATLON_NOT_MODELED); a geodetic non-rotating Earth is not detected.
//
// Signals (registered at init, held between exchanges; R13 excludes transport.* by construction):
//   transport.peer.refusal                     | 1   | the last exchange's refused bits; 0 if none
//   transport.peer.equivalent_airspeed         | m/s | the last exchange's EAS; NaN when refused
//   transport.peer.altitude_command            | m   | altitudeMslCommand in force
//   transport.peer.equivalent_airspeed_command | m/s | equivalentAirspeedCommand in force
//   transport.peer.course_command              | rad | trueBaseCourseCommand in force
//   transport.peer.lateral_deviation_error     | m   | lateralDeviationError fed to the law
//
// Per-frame methods allocate nothing and throw nothing; init() may allocate and throws a
// ConfigError naming the key.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"
#include "control_law.hpp"
#include "icd/astra_icd.h"

namespace astra::aircraft_f16_nesc_control {

namespace law = astra::aircraft_f16_control_law;

// What a variant of the peer may change (tests/plugins/nesc_control_variant). The defaults are the
// decided ones (the M7 plan's Q11 as amended 2026-10-13; ADR-0029 Decision 6), and the shipped
// plugin uses only those.
struct Options {
  // false: the LQR references from the trim (decided). true: the DML's hard-coded ones (Q11's
  // reported variant).
  bool dml_references = false;
  // false: the base point latches from the frame-0 exchange (decided). true: from the lateral
  // offset's frame (Q27's reported variant "the decided line through the position at 20 s").
  bool latch_at_offset_frame = false;
};

class NescControlPeer : public astra::Transport {
 public:
  explicit NescControlPeer(const Options& options = {}) : options_(options) {}

  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    const astra::Config peer = config.at("peer");
    read_trim(peer.at("trim"));
    read_commands(peer.at("commands"));
    references_ = options_.dml_references
                      ? law::References{}
                      : law::References{trim_theta_, trim_theta_, initial_commands_.eas};
    if (peer.has("schedule")) {
      read_schedule(peer.at("schedule"));
    }
    if (peer.has("lateral_offset")) {
      read_lateral_offset(peer.at("lateral_offset"));
    }
    reset();
    registry.add("transport.peer.refusal", &refusal_, "1");
    registry.add("transport.peer.equivalent_airspeed", &equivalent_airspeed_, "m/s");
    registry.add("transport.peer.altitude_command", &commands_.altitude, "m");
    registry.add("transport.peer.equivalent_airspeed_command", &commands_.eas, "m/s");
    registry.add("transport.peer.course_command", &commands_.course, "rad");
    registry.add("transport.peer.lateral_deviation_error", &commands_.lateral_deviation_error, "m");
  }
  void reset() override {
    commands_ = initial_commands_;
    next_change_ = 0;
    published_any_ = false;
    latched_ = false;
    refusal_ = 0.0;
    equivalent_airspeed_ = std::numeric_limits<double>::quiet_NaN();
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "nesc_control"; }

  [[nodiscard]] std::uint32_t icd_version() const noexcept override { return ASTRA_ICD_VERSION; }

  [[nodiscard]] std::uint32_t open(const astra::TransportSettings& /*settings*/) override {
    ack_ = kSentinel;
    return ack_;
  }

  // The peer computes as soon as it sees the request, as the loopback does.
  void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept override {
    std::memset(&reply_, 0, sizeof reply_);  // ADR-0022 Determinism: the peer zeroes its reply
    reply_.magic = ASTRA_ICD_MAGIC;
    reply_.version = ASTRA_ICD_VERSION;
    reply_.frame = request.frame;
    pending_ = seq;

    std::uint32_t refused = law::refusal_bits(request);
    // 13.4's base point latches from the frame-0 exchange only (ADR-0029 Decisions 2 and 6): a
    // first exchange whose frame is not 0 is refused, and so is every later one while no base
    // point has latched (a refused frame 0 latches nothing). The reported variant that latches
    // at the offset's frame still requires the first exchange at frame 0.
    if (offset_.configured) {
      const bool first = !published_any_;
      const bool unlatched = options_.latch_at_offset_frame
                                 ? first && request.frame != 0U
                                 : !latched_ && (!first || request.frame != 0U);
      if (unlatched) {
        refused |= law::kRefusalBasePointFrame;
      }
    }
    published_any_ = true;
    refusal_ = static_cast<double>(refused);
    apply_schedule(request.frame);
    if (refused != 0U) {
      refuse();
      return;
    }
    update_lateral_deviation(request);

    const law::Inputs in = law::inputs_from(request);
    equivalent_airspeed_ = in.eas;
    const law::IcdCommands icd =
        law::to_icd(law::evaluate(in, commands_, trim_, law::Modes{}, references_));
    reply_.elevator = icd.elevator;
    reply_.aileron = icd.aileron;
    reply_.rudder = icd.rudder;
    reply_.throttle[0] = icd.throttle;
    reply_.throttle[1] = icd.throttle;
  }

  [[nodiscard]] astra::AckObservation wait_ack(std::uint32_t /*unchanged*/) noexcept override {
    ack_ = pending_;
    return astra::AckObservation{astra::AckStatus::kObserved, ack_};
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    std::memcpy(&reply, &reply_, sizeof reply);
  }

  void close(astra::CloseReason /*reason*/) noexcept override {}

  // The trimmed pilot controls the peer derived at init (R23's route), the LQR references it set
  // at init (Q11), and the commands in force.
  [[nodiscard]] const law::Trim& trim() const noexcept { return trim_; }
  [[nodiscard]] const law::References& references() const noexcept { return references_; }
  [[nodiscard]] const law::Commands& commands() const noexcept { return commands_; }

 protected:
  // For tests/plugins/nesc_control_variant, before init(): the shipped plugin never calls it.
  void set_options(const Options& options) noexcept { options_ = options; }

  // The signed cross-track distance from the base course (ADR-0029 Decision 6). Q27's reported
  // geometry variants override it in tests/plugins/nesc_control_variant; the shipped plugin never
  // does.
  [[nodiscard]] virtual double cross_track_at(const law::BasePoint& base, double chi, double lat,
                                              double lon) const noexcept {
    return law::cross_track(base, chi, lat, lon);
  }
  // Called once, when the base point latches.
  virtual void on_base_latched(const law::BasePoint& /*base*/, double /*chi*/) noexcept {}

 private:
  static constexpr std::uint32_t kSentinel = 0xFFFFFFFFU;  // ADR-0024
  // peer.trim.theta's range: a 3-2-1 pitch lies within +-90 deg.
  static constexpr double kPitchLimit = astra::units::degrees_to_radians(90.0);

  // One schedule entry: from `frame` on, each command it names takes its value (SI).
  struct Change {
    std::uint64_t frame = 0;
    bool has_altitude = false;
    double altitude = 0.0;
    bool has_eas = false;
    double eas = 0.0;
    bool has_course = false;
    double course = 0.0;
  };

  // 13.4's lateral offset (ADR-0029 Decision 6).
  struct Offset {
    bool configured = false;
    std::uint64_t frame = 0;
    double offset = 0.0;  // m, to the right of the base course
  };

  static std::string keyed(const astra::Config& config, std::string_view key) {
    return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
  }

  static double finite_number(const astra::Config& block, std::string_view key) {
    const double value = block.number(key);
    if (!std::isfinite(value)) {
      throw astra::ConfigError(block.origin(), keyed(block, key), "must be finite");
    }
    return value;
  }

  static std::uint64_t frame_of(const astra::Config& block) {
    const std::int64_t frame = block.integer("frame");
    if (frame < 0) {
      throw astra::ConfigError(block.origin(), keyed(block, "frame"), "must not be negative");
    }
    return static_cast<std::uint64_t>(frame);
  }

  static double knots(const astra::Config& block, std::string_view key) {
    const double value = finite_number(block, key);
    if (value <= 0.0) {
      throw astra::ConfigError(block.origin(), keyed(block, key), "must be above 0");
    }
    return astra::units::knots_to_meters_per_second(value);
  }

  // The trim's physical controls, inverted through the law's own limit-and-scale block (R23), and
  // the trim's pitch, the LQR's reference angle of attack and pitch (Q11).
  void read_trim(const astra::Config& trim) {
    const double elevator = finite_number(trim, "elevator");
    const double throttle = finite_number(trim, "throttle");
    trim_theta_ = finite_number(trim, "theta");
    if (std::abs(trim_theta_) > kPitchLimit) {
      throw astra::ConfigError(trim.origin(), keyed(trim, "theta"),
                               "must lie within +-90 deg (a 3-2-1 pitch)");
    }
    trim_ = law::trim_from_controls(elevator, throttle);
    if (std::abs(trim_.long_stick) > 1.0) {
      throw astra::ConfigError(
          trim.origin(), keyed(trim, "elevator"),
          "gives a trimmed stick outside [-1, 1]: the elevator must lie within "
          "the law's +-25 deg");
    }
    if (throttle < 0.0 || throttle > 1.0) {
      throw astra::ConfigError(trim.origin(), keyed(trim, "throttle"), "must lie in [0, 1]");
    }
  }

  // The commands from frame 0, each converted once, here (ADR-0029 Decision 3, ADR-0030 rule 3).
  void read_commands(const astra::Config& commands) {
    initial_commands_.altitude =
        astra::units::feet_to_meters(finite_number(commands, "altitude_ft"));
    initial_commands_.eas = knots(commands, "equivalent_airspeed_knots");
    initial_commands_.course =
        astra::units::degrees_to_radians(finite_number(commands, "course_deg"));
    initial_commands_.lateral_deviation_error = 0.0;
  }

  void read_schedule(const astra::Config& schedule) {
    // A scalar, null or map schedule has size 0, and at() has already marked the key consumed, so
    // without this check it would fly with no command step at all (Codex review R-001).
    if (!schedule.is_sequence()) {
      throw astra::ConfigError(schedule.origin(), schedule.path(),
                               "must be a sequence of schedule entries");
    }
    for (std::size_t i = 0; i < schedule.size(); ++i) {
      const astra::Config entry = schedule.at(i);
      Change change;
      change.frame = frame_of(entry);
      if (!schedule_.empty() && change.frame <= schedule_.back().frame) {
        throw astra::ConfigError(entry.origin(), keyed(entry, "frame"),
                                 "schedule frames must be strictly ascending");
      }
      if (entry.has("altitude_ft")) {
        change.has_altitude = true;
        change.altitude = astra::units::feet_to_meters(finite_number(entry, "altitude_ft"));
      }
      if (entry.has("equivalent_airspeed_knots")) {
        change.has_eas = true;
        change.eas = knots(entry, "equivalent_airspeed_knots");
      }
      if (entry.has("course_deg")) {
        change.has_course = true;
        change.course = astra::units::degrees_to_radians(finite_number(entry, "course_deg"));
      }
      if (!change.has_altitude && !change.has_eas && !change.has_course) {
        throw astra::ConfigError(entry.origin(), entry.path(),
                                 "a schedule entry must change at least one command");
      }
      schedule_.push_back(change);
    }
  }

  void read_lateral_offset(const astra::Config& offset) {
    offset_.configured = true;
    offset_.frame = frame_of(offset);
    offset_.offset = astra::units::feet_to_meters(finite_number(offset, "offset_ft"));
  }

  void apply_schedule(std::uint64_t frame) noexcept {
    while (next_change_ < schedule_.size() && schedule_[next_change_].frame <= frame) {
      const Change& change = schedule_[next_change_];
      if (change.has_altitude) {
        commands_.altitude = change.altitude;
      }
      if (change.has_eas) {
        commands_.eas = change.eas;
      }
      if (change.has_course) {
        commands_.course = change.course;
      }
      ++next_change_;
    }
  }

  void update_lateral_deviation(const astra_sim_to_sw_v1& request) noexcept {
    commands_.lateral_deviation_error = 0.0;
    if (!offset_.configured) {
      return;
    }
    const bool latch_now =
        options_.latch_at_offset_frame ? request.frame >= offset_.frame : request.frame == 0U;
    if (!latched_ && latch_now) {
      base_ = law::base_point(request.lat, request.lon, request.altitude);
      latched_ = true;
      on_base_latched(base_, initial_commands_.course);
    }
    if (latched_ && request.frame >= offset_.frame) {
      commands_.lateral_deviation_error =
          cross_track_at(base_, initial_commands_.course, request.lat, request.lon) -
          offset_.offset;
    }
  }

  void refuse() noexcept {
    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
    equivalent_airspeed_ = kNan;
    reply_.elevator = kNan;
    reply_.aileron = kNan;
    reply_.rudder = kNan;
    reply_.throttle[0] = kNan;
    reply_.throttle[1] = kNan;
  }

  Options options_;
  law::Trim trim_{};
  double trim_theta_ = 0.0;
  law::References references_{};
  law::Commands initial_commands_{};
  std::vector<Change> schedule_;
  Offset offset_{};

  law::Commands commands_{};
  std::size_t next_change_ = 0;
  bool published_any_ = false;
  bool latched_ = false;
  law::BasePoint base_{};

  astra_sw_to_sim_v1 reply_{};
  std::uint32_t ack_ = kSentinel;
  std::uint32_t pending_ = kSentinel;

  double refusal_ = 0.0;
  double equivalent_airspeed_ = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace astra::aircraft_f16_nesc_control
