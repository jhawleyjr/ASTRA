// The two-process echo peer (M4 PR 4, docs/plans/m4-pr4-shm-transport.md): a stand-in for flight
// software, linked ONLY against embedded/hal (astra_hal) and icd/ -- never astra_core (ADR-0008;
// EchoPeer_links_no_astra_core checks the binary). C++17, like embedded/. Test support, not
// product: it lives under tests/, so M4 PR 5's standalone embedded/ project does not inherit it.
//
//   astra_echo_peer --segment S --handshake-timeout-ms N --peer-timeout-ms N
//                   [--mode hold --commands e,a,r,t | --mode echo]
//                   [--notify-fd FD (--stop-after N | --abort-after N)]
//
//   hold  constant commands (throttle[0] == throttle[1] == t), for Simulation-level tests;
//   echo  the reply is derived from the request -- elevator = phi, aileron = theta, rudder = psi,
//         throttle = {p, q}, mode_flags = FNV-1a32 of the 200 request bytes, fault_flags =
//         (uint32_t)frame ^ status_flags -- so the stress test can build every expected reply
//         independently and catch a stale or torn payload in either direction (critique F-6).
//   --stop-after N   after the Nth reply: one byte to FD, then pause() until killed (R4 SIGKILL);
//   --abort-after N  after the Nth reply: one byte to FD, then abort() (R4's abort flag).
//
// Exit status: hal_exit_status() of the last HAL status (0 on CLOSED); 1 on bad arguments. On exit
// it prints "echo_peer: handled <n> status <s>" to stdout, an independent count of exchanges.
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "embedded/hal/astra_hal.h"
#include "icd/astra_icd.h"

namespace {

constexpr int kUsageError = 1;
constexpr std::uint32_t kFnvOffset = 2166136261U;
constexpr std::uint32_t kFnvPrime = 16777619U;

struct Options {
  std::string segment;
  std::uint32_t handshake_timeout_ms = 0;
  std::uint32_t peer_timeout_ms = 0;
  bool echo = false;
  double commands[4] = {0.0, 0.0, 0.0, 0.0};  // NOLINT(modernize-avoid-c-arrays): C++17 peer
  int notify_fd = -1;
  unsigned long long stop_after = 0;
  unsigned long long abort_after = 0;
};

bool parse_u32(const char* text, std::uint32_t& out) {
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (end == text || *end != '\0' || value > 0xFFFFFFFFULL) {
    return false;
  }
  out = static_cast<std::uint32_t>(value);
  return true;
}

bool parse_commands(const char* text, Options& options) {
  const char* cursor = text;
  for (int i = 0; i < 4; ++i) {
    char* end = nullptr;
    options.commands[i] = std::strtod(cursor, &end);
    if (end == cursor || (i < 3 && *end != ',') || (i == 3 && *end != '\0')) {
      return false;
    }
    cursor = end + 1;
  }
  return true;
}

bool parse(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (i + 1 >= argc) {
      return false;
    }
    const char* value = argv[++i];
    std::uint32_t number = 0;
    if (flag == "--segment") {
      options.segment = value;
    } else if (flag == "--handshake-timeout-ms" && parse_u32(value, number)) {
      options.handshake_timeout_ms = number;
    } else if (flag == "--peer-timeout-ms" && parse_u32(value, number)) {
      options.peer_timeout_ms = number;
    } else if (flag == "--mode" &&
               (std::strcmp(value, "echo") == 0 || std::strcmp(value, "hold") == 0)) {
      options.echo = std::strcmp(value, "echo") == 0;
    } else if (flag == "--commands" && parse_commands(value, options)) {
    } else if (flag == "--notify-fd" && parse_u32(value, number)) {
      options.notify_fd = static_cast<int>(number);
    } else if (flag == "--stop-after" && parse_u32(value, number)) {
      options.stop_after = number;
    } else if (flag == "--abort-after" && parse_u32(value, number)) {
      options.abort_after = number;
    } else {
      return false;
    }
  }
  return !options.segment.empty() && options.handshake_timeout_ms > 0 &&
         options.peer_timeout_ms > 0;
}

std::uint32_t fnv1a(const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::uint32_t hash = kFnvOffset;
  for (std::size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * kFnvPrime;
  }
  return hash;
}

void fill(const Options& options, const astra_sim_to_sw_v1& in, astra_sw_to_sim_v1& out) {
  std::memset(&out, 0, sizeof out);  // ADR-0022 Determinism: the peer zeroes its reply
  out.magic = ASTRA_ICD_MAGIC;
  out.version = ASTRA_ICD_VERSION;
  out.frame = in.frame;
  if (options.echo) {
    out.elevator = in.phi;
    out.aileron = in.theta;
    out.rudder = in.psi;
    out.throttle[0] = in.p;
    out.throttle[1] = in.q;
    out.mode_flags = fnv1a(&in, sizeof in);
    out.fault_flags = static_cast<std::uint32_t>(in.frame) ^ in.status_flags;
  } else {
    out.elevator = options.commands[0];
    out.aileron = options.commands[1];
    out.rudder = options.commands[2];
    out.throttle[0] = options.commands[3];
    out.throttle[1] = options.commands[3];
  }
}

void notify(int fd) {
  const char byte = 1;
  if (fd >= 0 && write(fd, &byte, 1) != 1) {
    std::abort();
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::fprintf(stderr,
                 "usage: astra_echo_peer --segment S --handshake-timeout-ms N "
                 "--peer-timeout-ms N [--mode hold|echo] [--commands e,a,r,t] "
                 "[--notify-fd FD] [--stop-after N] [--abort-after N]\n");
    return kUsageError;
  }
  const astra_hal_config config{options.segment.c_str(), options.handshake_timeout_ms,
                                options.peer_timeout_ms};
  astra_hal_status status = hal_init(&config);
  unsigned long long handled = 0;
  astra_sim_to_sw_v1 in{};
  astra_sw_to_sim_v1 out{};
  while (status == ASTRA_HAL_OK) {
    status = hal_wait_frame(&in);
    if (status != ASTRA_HAL_OK) {
      break;
    }
    fill(options, in, out);
    status = hal_send(&out);
    if (status != ASTRA_HAL_OK) {
      break;
    }
    ++handled;
    if (options.stop_after != 0 && handled == options.stop_after) {
      notify(options.notify_fd);
      for (;;) {
        pause();
      }
    }
    if (options.abort_after != 0 && handled == options.abort_after) {
      notify(options.notify_fd);
      std::abort();
    }
  }
  hal_close();
  std::printf("echo_peer: handled %llu status %d\n", handled, static_cast<int>(status));
  std::fflush(stdout);
  return hal_exit_status(status);
}
