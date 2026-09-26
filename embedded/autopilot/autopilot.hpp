// The example straight-and-level autopilot's control law (docs/plans/m4-pr5-autopilot.md D2, D3).
// C++17, links only icd/. No HAL, no clock, no allocation, no exceptions: main.cpp feeds it frames
// from the HAL and the tests feed it scripted frames. Time enters only as the `now` argument (the
// HAL's frame-derived hal_now()).
#ifndef ASTRA_EMBEDDED_AUTOPILOT_AUTOPILOT_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_AUTOPILOT_HPP

#include <cstdint>

#include "embedded/autopilot/gains.hpp"
#include "embedded/autopilot/pid.hpp"
#include "icd/astra_icd.h"

namespace astra_autopilot {

struct Setpoints {
  double altitude_m;
  double airspeed_mps;  // true airspeed, tas
  double heading_rad;
};

// From astra.trim (plan D2): TrimResult.elevator_rad, .throttle, .alpha_rad (theta = alpha at gamma
// = 0).
struct Trims {
  double elevator_rad;
  double throttle;
  double theta_rad;
};

class Autopilot {
 public:
  Autopilot(const Setpoints& setpoints, const Trims& trims);

  // Computes the reply to `in` at frame time `now` (hal_now()). `out` is zeroed first, so padding
  // and fault_flags are deterministic. Never fails: an unusable frame (D3's hold mask) repeats the
  // previous commands (the trim biases before any reply) and changes no loop state.
  void update(const astra_sim_to_sw_v1& in, double now, astra_sw_to_sim_v1& out);

  [[nodiscard]] std::uint64_t handled() const { return handled_; }
  [[nodiscard]] std::uint64_t held() const { return held_; }

  // True when every field a loop reads is finite and WIND_INVALID is clear (the D3 hold mask,
  // inverted).
  [[nodiscard]] static bool usable(const astra_sim_to_sw_v1& in);

 private:
  Setpoints setpoints_;
  Trims trims_;
  Pid altitude_{PidGains{kAltP, kAltI, kAltD}};
  Pid speed_{PidGains{kSpeedP, kSpeedI, 0.0}};
  Pid heading_{PidGains{kHeadingP, kHeadingI, 0.0}};
  double last_elevator_;
  double last_aileron_ = 0.0;
  double last_rudder_ = 0.0;
  double last_throttle_;
  double previous_now_ = 0.0;
  bool have_previous_ = false;
  std::uint64_t handled_ = 0;
  std::uint64_t held_ = 0;
};

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_AUTOPILOT_HPP
