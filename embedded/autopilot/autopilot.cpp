#include "embedded/autopilot/autopilot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "embedded/autopilot/limits.hpp"

namespace astra_autopilot {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// The heading error, wrapped to [-pi, pi] so a +179 to -179 degree change commands the short way.
double wrap_pi(double angle) { return std::remainder(angle, kTwoPi); }

}  // namespace

Autopilot::Autopilot(const Setpoints& setpoints, const Trims& trims)
    : setpoints_(setpoints),
      trims_(trims),
      last_elevator_(trims.elevator_rad),
      last_throttle_(trims.throttle) {}

bool Autopilot::usable(const astra_sim_to_sw_v1& in) {
  // Every field a loop reads must be finite, and WIND_INVALID (the ICD zeroes tas, alpha, beta,
  // mach, cas then) holds the loop. ATMOSPHERE_INVALID and CAS_SUPERSONIC zero only mach/cas, which
  // no loop reads, so they are ignored on purpose, as are ACCEL_INVALID and LATLON_NOT_MODELED.
  if ((in.status_flags & ASTRA_ICD_STATUS_WIND_INVALID) != 0U) {
    return false;
  }
  const std::array<double, 10> read = {
      in.altitude, in.vertical_speed, in.theta, in.q, in.psi, in.phi, in.p, in.tas, in.r, in.beta};
  return std::all_of(read.begin(), read.end(), [](double value) { return std::isfinite(value); });
}

void Autopilot::update(const astra_sim_to_sw_v1& in, double now, astra_sw_to_sim_v1& out) {
  std::memset(&out, 0, sizeof out);
  out.magic = ASTRA_ICD_MAGIC;
  out.version = ASTRA_ICD_VERSION;
  out.frame = in.frame;
  out.mode_flags = kModeFlags;

  // Time bookkeeping is separate from loop state: previous_now_ advances on every frame, held or
  // not, so the first usable frame after held frames integrates over one frame interval, never the
  // whole gap.
  double dt = have_previous_ ? now - previous_now_ : 0.0;
  previous_now_ = now;
  have_previous_ = true;
  if (!std::isfinite(dt) || dt <= 0.0 || dt > kMaxIntegrationDt) {
    dt = 0.0;
  }

  ++handled_;
  if (usable(in)) {
    // Elevator: altitude PID plus fixed pitch attitude and rate feedback. The D term is on
    // measurement, vertical_speed, the measured rate of altitude, so the error rate is its
    // negative.
    const double elevator_offset =
        trims_.elevator_rad + kPitchTheta * (in.theta - trims_.theta_rad) + kPitchRate * in.q;
    last_elevator_ = altitude_.update(setpoints_.altitude_m - in.altitude, -in.vertical_speed, dt,
                                      elevator_offset, -1.0, kElevatorLimit);
    // Throttle: airspeed PI on tas.
    last_throttle_ = speed_.update(setpoints_.airspeed_mps - in.tas, 0.0, dt, trims_.throttle, 1.0,
                                   kThrottleLimit);
    // Aileron: heading PI to a bank command, then fixed bank and roll-rate feedback.
    const double bank_command = heading_.update(wrap_pi(setpoints_.heading_rad - in.psi), 0.0, dt,
                                                0.0, 1.0, kBankCommandLimit);
    last_aileron_ = Pid::clamp(kBankP * (bank_command - in.phi) - kRollRate * in.p, kAileronLimit);
    // Rudder: yaw damper, no integrator.
    last_rudder_ = Pid::clamp(kYawRate * in.r + kSideslip * in.beta, kRudderLimit);
  } else {
    ++held_;
  }

  out.elevator = last_elevator_;
  out.aileron = last_aileron_;
  out.rudder = last_rudder_;
  out.throttle[0] = last_throttle_;
  out.throttle[1] = last_throttle_;
}

}  // namespace astra_autopilot
