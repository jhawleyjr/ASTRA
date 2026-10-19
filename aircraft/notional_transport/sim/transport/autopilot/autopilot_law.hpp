// M4's straight-and-level autopilot law, written sim-side (M7b's oracle (b),
// docs/plans/m7b-pr3-autopilot-peer.md §2; ADR-0029 Decision 8, ADR-0034 Decision 8). A second
// implementation of embedded/autopilot's law (docs/plans/m4-pr5-autopilot.md D2 to D5), transcribed
// line for line from embedded/autopilot/autopilot.cpp:14-85 and pid.hpp:12-59 in that text's
// floating-point expression order (ADR-0029's Consequences: the same factors in the same order, the
// same libm calls, the same comparisons). tests/python/test_autopilot_peer.py shows the peer
// bitwise equal to the port over M4's acceptance scenario, and tests/autopilot_peer shows the two
// laws bitwise equal on scripted and seeded frame sequences.
//
// Nothing here includes embedded/ (ADR-0008, ADR-0029 Decision 8: "No include runs from aircraft/
// into embedded/"). The constants are this law's own, with gains.hpp's and limits.hpp's literal
// text; AutopilotPeerEquivalence.ConstantsArePinnedToTheAutopilots pins each one bitwise.
//
// The law, in SI (rad, m, m/s, s):
// - elevator: the altitude PID on e_h = altitude_cmd - altitude, derivative on measurement
//   (-vertical_speed), offset trim + kPitchTheta*(theta - theta_trim) + kPitchRate*q, sign -1
//   (positive elevator is nose-down);
// - throttle: the airspeed PI on tas, offset the throttle trim;
// - aileron: the heading PI on the wrapped heading error to a bank command, then kBankP*(bank -
//   phi) - kRollRate*p, clamped;
// - rudder: kYawRate*r, clamped (a pure yaw damper).
// Each PID integrates with conditional-integration anti-windup; dt is the frame-time difference,
// integrated as 0 when non-finite, not above 0 or above 1 s. A frame with WIND_INVALID or a
// non-finite field the loops read holds the previous commands and changes no loop state.
//
// The hooks, and only the variant uses them (the plan's decision 5): Gains (defaulted to the named
// constants), the law's PidType and the peer's LawType. The shipped plugin instantiates only
// BasicLaw<Pid> with Gains{}; tests/plugins/autopilot_peer_variant plants the porting controls.
//
// No Config, no allocation, no exceptions, no clock: time enters only as update()'s `now`.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "icd/astra_icd.h"

namespace astra::aircraft_notional_transport_autopilot {

// --- The gains (gains.hpp:35-46's values; units rad, m, m/s, s) ---
inline constexpr double kAltP = 1.54e-4;
inline constexpr double kAltI = 7.97e-6;
inline constexpr double kAltD = 2.24e-4;
inline constexpr double kPitchTheta = 0.389;
inline constexpr double kPitchRate = 0.184;
inline constexpr double kSpeedP = 0.0353;
inline constexpr double kSpeedI = 7.69e-4;
inline constexpr double kHeadingP = 2.5;
inline constexpr double kHeadingI = 0.0816;
inline constexpr double kBankP = 0.394;
inline constexpr double kRollRate = 0.360;
inline constexpr double kYawRate = 0.494;

struct Range {
  double lo;
  double hi;
};

// --- The output limits, the bank-command limit, the mode word and the dt rule (limits.hpp:23-38).
// rad; throttle 0..1. The M4 configs' transport.limits use the same values ---
inline constexpr Range kElevatorLimit{-0.3, 0.3};
inline constexpr Range kAileronLimit{-0.35, 0.35};
inline constexpr Range kRudderLimit{-0.35, 0.35};
inline constexpr Range kThrottleLimit{0.0, 1.0};
inline constexpr Range kBankCommandLimit{-0.35, 0.35};
inline constexpr std::uint32_t kModeFlags = 0x00000001U;
inline constexpr double kMaxIntegrationDt = 1.0;  // s

// The heading error's wrap (autopilot.cpp:14-18). kPi keeps autopilot.cpp:14's literal as written.
// NOLINTNEXTLINE(modernize-use-std-numbers)
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

// The heading error, wrapped to [-pi, pi] so a +179 to -179 degree change commands the short way.
inline double wrap_pi(double angle) { return std::remainder(angle, kTwoPi); }

struct Setpoints {
  double altitude_m;
  double airspeed_mps;  // true airspeed, tas
  double heading_rad;
};

// From astra.trim: the elevator, the throttle, and theta (= alpha at gamma = 0).
struct Trims {
  double elevator_rad;
  double throttle;
  double theta_rad;
};

// The gains a law runs with. The shipped peer uses only the defaults (the constants above);
// tests/plugins/autopilot_peer_variant's float_pitch_rate control is the only other user.
struct Gains {
  double alt_p = kAltP;
  double alt_i = kAltI;
  double alt_d = kAltD;
  double pitch_theta = kPitchTheta;
  double pitch_rate = kPitchRate;
  double speed_p = kSpeedP;
  double speed_i = kSpeedI;
  double heading_p = kHeadingP;
  double heading_i = kHeadingI;
  double bank_p = kBankP;
  double roll_rate = kRollRate;
  double yaw_rate = kYawRate;
};

struct PidGains {
  double kp;
  double ki;
  double kd;
};

// One PID term with conditional-integration anti-windup (pid.hpp:18-59).
class Pid {
 public:
  explicit constexpr Pid(const PidGains& gains) : gains_(gains) {}

  // The command is offset + sign * (kp*e + integral + kd*e_rate), clamped to `range`. `e_rate` is
  // the rate of the error taken from a measured rate, never a difference of samples. The integral
  // advances by ki*e*dt when dt > 0, except that a step which would leave the command outside
  // `range` and farther from it than before is not taken.
  [[nodiscard]] double update(double error, double error_rate, double dt, double offset,
                              double sign, const Range& range) {
    const double proportional = gains_.kp * error + gains_.kd * error_rate;
    const double before = offset + sign * (proportional + integral_);
    if (dt > 0.0) {
      const double candidate = integral_ + gains_.ki * error * dt;
      const double after = offset + sign * (proportional + candidate);
      if (std::isfinite(candidate) &&
          (inside(after, range) || distance(after, range) < distance(before, range))) {
        integral_ = candidate;
      }
    }
    return clamp(offset + sign * (proportional + integral_), range);
  }

  [[nodiscard]] constexpr double integral() const { return integral_; }

  // The nested conditionals are pid.hpp:45-55's own expressions, kept as the port writes them.
  [[nodiscard]] static double clamp(double value, const Range& range) {
    // NOLINTNEXTLINE(readability-avoid-nested-conditional-operator)
    return value < range.lo ? range.lo : (value > range.hi ? range.hi : value);
  }

 private:
  [[nodiscard]] static bool inside(double value, const Range& range) {
    return value >= range.lo && value <= range.hi;
  }
  [[nodiscard]] static double distance(double value, const Range& range) {
    // NOLINTNEXTLINE(readability-avoid-nested-conditional-operator)
    return value < range.lo ? range.lo - value : (value > range.hi ? value - range.hi : 0.0);
  }

  PidGains gains_;
  double integral_ = 0.0;
};

// The law (autopilot.cpp:22-85). PidType is the three loops' PID: Pid in the shipped peer.
template <class PidType = Pid>
class BasicLaw {
 public:
  BasicLaw(const Setpoints& setpoints, const Trims& trims, const Gains& gains = Gains{})
      : setpoints_(setpoints),
        trims_(trims),
        gains_(gains),
        altitude_(PidGains{gains.alt_p, gains.alt_i, gains.alt_d}),
        speed_(PidGains{gains.speed_p, gains.speed_i, 0.0}),
        heading_(PidGains{gains.heading_p, gains.heading_i, 0.0}),
        last_elevator_(trims.elevator_rad),
        last_throttle_(trims.throttle) {}

  // Computes the reply to `in` at frame time `now` (the request's t, as the HAL's hal_now()
  // returns it). `out` is zeroed first. Never fails: an unusable frame repeats the previous
  // commands (the trim biases before any reply) and changes no loop state.
  void update(const astra_sim_to_sw_v1& in, double now, astra_sw_to_sim_v1& out) noexcept {
    std::memset(&out, 0, sizeof out);
    out.magic = ASTRA_ICD_MAGIC;
    out.version = ASTRA_ICD_VERSION;
    out.frame = in.frame;
    out.mode_flags = kModeFlags;

    // Time bookkeeping is separate from loop state: previous_now_ advances on every frame, held or
    // not, so the first usable frame after held frames integrates over one frame interval.
    double dt = have_previous_ ? now - previous_now_ : 0.0;
    previous_now_ = now;
    have_previous_ = true;
    if (!std::isfinite(dt) || dt <= 0.0 || dt > kMaxIntegrationDt) {
      dt = 0.0;
    }

    ++handled_;
    if (usable(in)) {
      const double elevator_offset = trims_.elevator_rad +
                                     gains_.pitch_theta * (in.theta - trims_.theta_rad) +
                                     gains_.pitch_rate * in.q;
      last_elevator_ = altitude_.update(setpoints_.altitude_m - in.altitude, -in.vertical_speed, dt,
                                        elevator_offset, -1.0, kElevatorLimit);
      last_throttle_ = speed_.update(setpoints_.airspeed_mps - in.tas, 0.0, dt, trims_.throttle,
                                     1.0, kThrottleLimit);
      bank_command_ = heading_.update(wrap_pi(setpoints_.heading_rad - in.psi), 0.0, dt, 0.0, 1.0,
                                      kBankCommandLimit);
      last_aileron_ = Pid::clamp(gains_.bank_p * (bank_command_ - in.phi) - gains_.roll_rate * in.p,
                                 kAileronLimit);
      last_rudder_ = Pid::clamp(gains_.yaw_rate * in.r, kRudderLimit);
    } else {
      ++held_;
    }

    out.elevator = last_elevator_;
    out.aileron = last_aileron_;
    out.rudder = last_rudder_;
    out.throttle[0] = last_throttle_;
    out.throttle[1] = last_throttle_;
  }

  [[nodiscard]] std::uint64_t handled() const noexcept { return handled_; }
  [[nodiscard]] std::uint64_t held() const noexcept { return held_; }

  // True when every field a loop reads is finite and WIND_INVALID is clear (autopilot.cpp:28-38).
  // ATMOSPHERE_INVALID and CAS_SUPERSONIC zero only mach and cas, which no loop reads, so they are
  // ignored on purpose, as are ACCEL_INVALID and LATLON_NOT_MODELED.
  [[nodiscard]] static bool usable(const astra_sim_to_sw_v1& in) noexcept {
    if ((in.status_flags & ASTRA_ICD_STATUS_WIND_INVALID) != 0U) {
      return false;
    }
    const std::array<double, 9> read = {
        in.altitude, in.vertical_speed, in.theta, in.q, in.psi, in.phi, in.p, in.tas, in.r};
    return std::all_of(read.begin(), read.end(), [](double value) { return std::isfinite(value); });
  }

  // For the tests' coverage counts (tests/autopilot_peer): the loops' integrals and the last bank
  // command the heading loop issued.
  [[nodiscard]] double altitude_integral() const noexcept { return altitude_.integral(); }
  [[nodiscard]] double speed_integral() const noexcept { return speed_.integral(); }
  [[nodiscard]] double heading_integral() const noexcept { return heading_.integral(); }
  [[nodiscard]] double bank_command() const noexcept { return bank_command_; }

 private:
  Setpoints setpoints_;
  Trims trims_;
  Gains gains_;
  PidType altitude_;
  PidType speed_;
  PidType heading_;
  double last_elevator_;
  double last_aileron_ = 0.0;
  double last_rudder_ = 0.0;
  double last_throttle_;
  double bank_command_ = 0.0;
  double previous_now_ = 0.0;
  bool have_previous_ = false;
  std::uint64_t handled_ = 0;
  std::uint64_t held_ = 0;
};

using Law = BasicLaw<>;

}  // namespace astra::aircraft_notional_transport_autopilot
