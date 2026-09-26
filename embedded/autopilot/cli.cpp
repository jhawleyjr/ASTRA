#include "embedded/autopilot/cli.hpp"

#include <array>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "embedded/autopilot/limits.hpp"

namespace astra_autopilot {

namespace {

// Named arguments; the enumerator is the bit index in the `seen` mask.
enum Key : std::uint8_t {
  kSegment,
  kHandshake,
  kPeer,
  kAltitude,
  kAirspeed,
  kHeading,
  kElevatorTrim,
  kThrottleTrim,
  kThetaTrim,
  kNotifyFd,
  kStopAfter,
  kAbortAfter,
  kKeyCount
};

struct KeyName {
  const char* flag;
  Key key;
};

constexpr std::array<KeyName, kKeyCount> kKeys = {{
    {"--segment", kSegment},
    {"--handshake-timeout-ms", kHandshake},
    {"--peer-timeout-ms", kPeer},
    {"--altitude-m", kAltitude},
    {"--airspeed-mps", kAirspeed},
    {"--heading-rad", kHeading},
    {"--elevator-trim", kElevatorTrim},
    {"--throttle-trim", kThrottleTrim},
    {"--theta-trim", kThetaTrim},
    {"--notify-fd", kNotifyFd},
    {"--stop-after", kStopAfter},
    {"--abort-after", kAbortAfter},
}};

constexpr unsigned bit(Key key) { return 1U << key; }

constexpr unsigned kRequired = bit(kSegment) | bit(kHandshake) | bit(kPeer) | bit(kAltitude) |
                               bit(kAirspeed) | bit(kHeading) | bit(kElevatorTrim) |
                               bit(kThrottleTrim) | bit(kThetaTrim);

bool parse_unsigned(const char* text, unsigned long long limit, unsigned long long& out) {
  // Digits only: strtoull would accept a sign or leading space, and "-1" would wrap.
  if (text[0] < '0' || text[0] > '9') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (errno != 0 || *end != '\0' || value > limit) {
    return false;
  }
  out = value;
  return true;
}

bool parse_finite(const char* text, double& out) {
  if (text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(text, &end);
  if (errno != 0 || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

bool parse_positive_u32(const char* text, std::uint32_t& out) {
  unsigned long long number = 0;
  if (!parse_unsigned(text, UINT32_MAX, number) || number == 0) {
    return false;
  }
  out = static_cast<std::uint32_t>(number);
  return true;
}

bool parse_positive_u64(const char* text, std::uint64_t& out) {
  unsigned long long number = 0;
  if (!parse_unsigned(text, UINT64_MAX, number) || number == 0) {
    return false;
  }
  out = number;
  return true;
}

bool parse_in_range(const char* text, const Range& range, double& out) {
  return parse_finite(text, out) && out >= range.lo && out <= range.hi;
}

// Stores `value` for `key`; false if the text is not acceptable for that key.
bool assign(Key key, const char* value, Options& options) {
  unsigned long long number = 0;
  switch (key) {
    case kSegment:
      options.segment = value;
      return value[0] != '\0';
    case kHandshake:
      return parse_positive_u32(value, options.handshake_timeout_ms);
    case kPeer:
      return parse_positive_u32(value, options.peer_timeout_ms);
    case kAltitude:
      return parse_finite(value, options.setpoints.altitude_m);
    case kAirspeed:
      return parse_finite(value, options.setpoints.airspeed_mps) &&
             options.setpoints.airspeed_mps > 0.0;
    case kHeading:
      return parse_finite(value, options.setpoints.heading_rad);
    case kThetaTrim:
      return parse_finite(value, options.trims.theta_rad);
    case kElevatorTrim:
      return parse_in_range(value, kElevatorLimit, options.trims.elevator_rad);
    case kThrottleTrim:
      return parse_in_range(value, kThrottleLimit, options.trims.throttle);
    case kNotifyFd:
      if (!parse_unsigned(value, INT_MAX, number)) {
        return false;
      }
      options.notify_fd = static_cast<int>(number);
      return true;
    case kStopAfter:
      return parse_positive_u64(value, options.stop_after);
    case kAbortAfter:
      return parse_positive_u64(value, options.abort_after);
    case kKeyCount:
      break;
  }
  return false;
}

const KeyName* find_key(const char* flag) {
  for (const KeyName& candidate : kKeys) {
    if (std::strcmp(flag, candidate.flag) == 0) {
      return &candidate;
    }
  }
  return nullptr;
}

// The cross-argument rules, after every argument has been read.
bool check_complete(unsigned seen, std::string& error) {
  for (const KeyName& candidate : kKeys) {
    if ((bit(candidate.key) & kRequired) != 0U && (seen & bit(candidate.key)) == 0U) {
      error = std::string("missing ") + candidate.flag;
      return false;
    }
  }
  const bool stop = (seen & bit(kStopAfter)) != 0U;
  const bool abort_flag = (seen & bit(kAbortAfter)) != 0U;
  if (stop && abort_flag) {
    error = "--stop-after and --abort-after are mutually exclusive";
    return false;
  }
  if (((seen & bit(kNotifyFd)) != 0U) != (stop || abort_flag)) {
    error = "--notify-fd goes with exactly one of --stop-after / --abort-after";
    return false;
  }
  return true;
}

}  // namespace

const char* usage() {
  return "usage: astra_autopilot --segment S --handshake-timeout-ms N --peer-timeout-ms N "
         "--altitude-m H --airspeed-mps V --heading-rad PSI --elevator-trim R --throttle-trim T "
         "--theta-trim R [--notify-fd FD (--stop-after N | --abort-after N)]\n";
}

bool parse_arguments(int argc, const char* const* argv, Options& options, std::string& error) {
  unsigned seen = 0;
  for (int i = 1; i < argc; ++i) {
    const char* flag = argv[i];
    const KeyName* match = find_key(flag);
    if (match == nullptr) {
      error = std::string("unknown argument ") + flag;
      return false;
    }
    if ((seen & bit(match->key)) != 0U) {
      error = std::string(flag) + " given more than once";
      return false;
    }
    if (i + 1 >= argc) {
      error = std::string(flag) + " needs a value";
      return false;
    }
    const char* value = argv[++i];
    seen |= bit(match->key);
    if (!assign(match->key, value, options)) {
      error = std::string("bad value for ") + flag + ": " + value;
      return false;
    }
  }
  return check_complete(seen, error);
}

}  // namespace astra_autopilot
