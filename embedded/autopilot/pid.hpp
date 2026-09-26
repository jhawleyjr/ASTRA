// One PID term with conditional-integration anti-windup (docs/plans/m4-pr5-autopilot.md D3). C++17,
// header only, no allocation, no exceptions, no clock: dt is an argument.
#ifndef ASTRA_EMBEDDED_AUTOPILOT_PID_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_PID_HPP

#include <cmath>

#include "embedded/autopilot/limits.hpp"

namespace astra_autopilot {

struct PidGains {
  double kp;
  double ki;
  double kd;
};

class Pid {
 public:
  explicit constexpr Pid(const PidGains& gains) : gains_(gains) {}

  // The command is offset + sign * (kp*e + integral + kd*e_rate), clamped to `range`. `e_rate` is
  // the rate of the error taken from a measured rate (derivative on measurement: -measured_rate for
  // a constant setpoint), never a difference of samples. The integral advances by ki*e*dt when dt >
  // 0, except that a step which would leave the command outside `range` and farther from it than
  // before is not taken (so a saturated loop does not wind up, and it unwinds as soon as the error
  // changes sign).
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

  [[nodiscard]] static double clamp(double value, const Range& range) {
    return value < range.lo ? range.lo : (value > range.hi ? range.hi : value);
  }

 private:
  [[nodiscard]] static bool inside(double value, const Range& range) {
    return value >= range.lo && value <= range.hi;
  }
  [[nodiscard]] static double distance(double value, const Range& range) {
    return value < range.lo ? range.lo - value : (value > range.hi ? value - range.hi : 0.0);
  }

  PidGains gains_;
  double integral_ = 0.0;
};

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_PID_HPP
