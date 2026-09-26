// astra_autopilot: the example flight software (docs/architecture.md §11; ADR-0008; plan
// docs/plans/m4-pr5-autopilot.md). A plain HAL client: it includes only the HAL's public header and
// the ICD, links only icd/ and embedded/hal/, and reads no clock (hal_now() is frame-derived and
// goes to the control law as an argument).
//
//   astra_autopilot --segment S --handshake-timeout-ms N --peer-timeout-ms N
//                   --altitude-m H --airspeed-mps V --heading-rad PSI
//                   --elevator-trim R --throttle-trim T --theta-trim R
//                   [--notify-fd FD (--stop-after N | --abort-after N)]
//
// Exit status: hal_exit_status() of the last HAL status, unchanged (0 for CLOSED; 2..6 as the HAL
// defines); 1 only for a bad command line, before the HAL is touched. Every exit after the
// arguments parse prints "autopilot: handled <n> held <m> status <s>" to stdout, the echo peer's
// format. --stop-after and
// --abort-after end the process without a summary (they exist to be killed or to abort mid-run).
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "embedded/autopilot/autopilot.hpp"
#include "embedded/autopilot/cli.hpp"
#include "embedded/hal/astra_hal.h"
#include "icd/astra_icd.h"

namespace {

constexpr int kUsageError = 1;

void notify(int fd) {
  const char byte = 1;
  if (fd >= 0 && write(fd, &byte, 1) != 1) {
    std::abort();
  }
}

}  // namespace

int main(int argc, char** argv) {
  astra_autopilot::Options options;
  std::string error;
  if (!astra_autopilot::parse_arguments(argc, argv, options, error)) {
    std::fprintf(stderr, "astra_autopilot: %s\n%s", error.c_str(), astra_autopilot::usage());
    return kUsageError;
  }

  astra_autopilot::Autopilot autopilot(options.setpoints, options.trims);
  const astra_hal_config config{options.segment.c_str(), options.handshake_timeout_ms,
                                options.peer_timeout_ms};
  astra_hal_status status = hal_init(&config);
  astra_sim_to_sw_v1 in{};
  astra_sw_to_sim_v1 out{};
  while (status == ASTRA_HAL_OK) {
    status = hal_wait_frame(&in);
    if (status != ASTRA_HAL_OK) {
      break;
    }
    autopilot.update(in, hal_now(), out);
    status = hal_send(&out);
    if (status != ASTRA_HAL_OK) {
      break;
    }
    if (options.stop_after != 0 && autopilot.handled() == options.stop_after) {
      notify(options.notify_fd);
      for (;;) {
        pause();
      }
    }
    if (options.abort_after != 0 && autopilot.handled() == options.abort_after) {
      notify(options.notify_fd);
      std::abort();
    }
  }
  hal_close();
  std::printf("autopilot: handled %llu held %llu status %d\n",
              static_cast<unsigned long long>(autopilot.handled()),
              static_cast<unsigned long long>(autopilot.held()), static_cast<int>(status));
  std::fflush(stdout);
  return hal_exit_status(status);
}
