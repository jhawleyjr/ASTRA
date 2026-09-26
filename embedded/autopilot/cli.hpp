// The autopilot's command line (docs/plans/m4-pr5-autopilot.md D2), parsed without the HAL so a
// unit test can drive it. Init path only: it allocates a std::string for the segment name and the
// error text.
#ifndef ASTRA_EMBEDDED_AUTOPILOT_CLI_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_CLI_HPP

#include <cstdint>
#include <string>

#include "embedded/autopilot/autopilot.hpp"

namespace astra_autopilot {

struct Options {
  std::string segment;
  std::uint32_t handshake_timeout_ms = 0;
  std::uint32_t peer_timeout_ms = 0;
  Setpoints setpoints{0.0, 0.0, 0.0};
  Trims trims{0.0, 0.0, 0.0};
  // Test hooks with the echo peer's semantics (tests/lockstep/echo_peer.cpp): after the Nth
  // completed reply, one byte to notify_fd, then pause (stop_after) or abort() (abort_after). 0 =
  // off.
  int notify_fd = -1;
  std::uint64_t stop_after = 0;
  std::uint64_t abort_after = 0;
};

// The usage text (one line, ends in a newline).
[[nodiscard]] const char* usage();

// Parses argv[1..argc). On failure returns false and sets `error` to one sentence naming the
// argument; the caller prints it with usage() and exits 1 without touching the HAL. Rules: the nine
// named arguments are required and each appears once; timeouts are positive uint32; the altitude,
// heading and theta-trim values are finite and the airspeed setpoint is finite and positive;
// --elevator-trim lies inside the elevator limit and --throttle-trim inside [0, 1] (the trim is
// itself the frame-0 and held reply, so it must satisfy the output limits); --notify-fd goes with
// exactly one of --stop-after / --abort-after (positive), and those two never together.
[[nodiscard]] bool parse_arguments(int argc, const char* const* argv, Options& options,
                                   std::string& error);

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_CLI_HPP
