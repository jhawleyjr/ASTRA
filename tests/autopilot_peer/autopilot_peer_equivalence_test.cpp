// R5 of M7b's oracle (b) (docs/plans/m7b-pr3-autopilot-peer.md §8): the notional transport's
// sim-side autopilot law (aircraft/notional_transport/sim/transport/autopilot/autopilot_law.hpp)
// against embedded/autopilot's (astra_autopilot_lib), fed identical frame sequences with identical
// `now` (the frame's t, as the HAL's hal_now() gives it). After every frame the two replies must be
// byte-equal and handled()/held() equal. Every preset runs it, so -O0 (debug, asan, static-plugins)
// is covered as well as -O3 (release). No astra_core: the law includes only the ICD.
//
// The hand-built sequences cover what M4's acceptance scenario never reaches (§4's envelope): held
// frames, WIND_INVALID, non-finite fields, saturation, anti-windup at and across each limit, the dt
// edges, the heading wrap and signed zeros. The seeded sequences (§8 item 3) mix all of them; their
// coverage counts are asserted. Every constant below that §8 pre-registers cites it.
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "aircraft/notional_transport/sim/transport/autopilot/autopilot_law.hpp"
#include "embedded/autopilot/autopilot.hpp"
#include "embedded/autopilot/gains.hpp"
#include "embedded/autopilot/limits.hpp"
#include "icd/astra_icd.h"
#include "tests/autopilot_peer/seeded_frames.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

namespace ap = astra_autopilot;
namespace peer = astra::aircraft_notional_transport_autopilot;
using autopilot_peer_test::Field;
using autopilot_peer_test::kNonFinite;
using autopilot_peer_test::kReadFields;
using autopilot_peer_test::kUnreadFields;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// The notional transport's still-air trim at 230 m/s, 10 km (cruise.yaml's initial controls; the
// same values as tests/embedded/autopilot_test.cpp:36-40).
constexpr double kAltitude = 10000.0;
constexpr double kTas = 230.0;
constexpr double kElevatorTrim = 0.026674474137989946;
constexpr double kThrottleTrim = 0.4985979490507746;
constexpr double kThetaTrim = 0.023586859573503434;
using autopilot_peer_test::kPeriod;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

struct Parameters {
  double altitude = kAltitude;
  double airspeed = kTas;
  double heading = 0.0;
  double elevator_trim = kElevatorTrim;
  double throttle_trim = kThrottleTrim;
  double theta_trim = kThetaTrim;
};

// The two laws side by side. step() feeds both the same frame at now = in.t and checks the replies
// byte for byte and the counts; it returns the reply.
class Pair {
 public:
  explicit Pair(const Parameters& p = Parameters{})
      : port_(ap::Setpoints{p.altitude, p.airspeed, p.heading},
              ap::Trims{p.elevator_trim, p.throttle_trim, p.theta_trim}),
        peer_(peer::Setpoints{p.altitude, p.airspeed, p.heading},
              peer::Trims{p.elevator_trim, p.throttle_trim, p.theta_trim}) {}

  astra_sw_to_sim_v1 step(const astra_sim_to_sw_v1& in) {
    astra_sw_to_sim_v1 port_out{};
    astra_sw_to_sim_v1 peer_out{};
    port_.update(in, in.t, port_out);
    peer_.update(in, in.t, peer_out);
    EXPECT_TRUE(autopilot_peer_test::same_bytes(port_out, peer_out))
        << "replies differ at step " << steps_ << " (frame " << in.frame << ", t " << in.t
        << "): elevator " << port_out.elevator << " / " << peer_out.elevator << ", aileron "
        << port_out.aileron << " / " << peer_out.aileron << ", rudder " << port_out.rudder << " / "
        << peer_out.rudder << ", throttle " << port_out.throttle[0] << " / " << peer_out.throttle[0]
        << ", mode " << port_out.mode_flags << " / " << peer_out.mode_flags;
    EXPECT_EQ(port_.handled(), peer_.handled()) << "step " << steps_;
    EXPECT_EQ(port_.held(), peer_.held()) << "step " << steps_;
    ++steps_;
    return peer_out;
  }

  [[nodiscard]] const peer::Law& law() const { return peer_; }
  [[nodiscard]] std::uint64_t held() const { return port_.held(); }

 private:
  ap::Autopilot port_;
  peer::Law peer_;
  std::uint64_t steps_ = 0;
};

// A frame with every measurement at its setpoint and trim value.
astra_sim_to_sw_v1 nominal(std::uint64_t frame, double t) {
  astra_sim_to_sw_v1 in{};
  in.magic = ASTRA_ICD_MAGIC;
  in.version = ASTRA_ICD_VERSION;
  in.frame = frame;
  in.t = t;
  in.altitude = kAltitude;
  in.tas = kTas;
  in.theta = kThetaTrim;
  return in;
}

astra_sim_to_sw_v1 at_step(std::uint64_t k) {
  return nominal(k * 10, static_cast<double>(k) * kPeriod);
}

// A short disturbed lead-in, so every loop carries a nonzero integral into what follows.
void lead_in(Pair& pair, std::uint64_t& k, std::uint64_t count) {
  for (std::uint64_t i = 0; i < count; ++i, ++k) {
    astra_sim_to_sw_v1 in = at_step(k);
    in.altitude = kAltitude - 3.0;
    in.tas = kTas - 0.5;
    in.psi = 0.01;
    in.phi = -0.002;
    in.q = 0.001;
    in.r = 0.0005;
    in.vertical_speed = 0.2;
    pair.step(in);
  }
}

}  // namespace

// --- 1. The constants, pinned bitwise (§3, §8 item 1) --------------------------------------------

TEST(AutopilotPeerEquivalence, ConstantsArePinnedToTheAutopilots) {
  const std::array<std::array<double, 2>, 12> gains = {{{peer::kAltP, ap::kAltP},
                                                        {peer::kAltI, ap::kAltI},
                                                        {peer::kAltD, ap::kAltD},
                                                        {peer::kPitchTheta, ap::kPitchTheta},
                                                        {peer::kPitchRate, ap::kPitchRate},
                                                        {peer::kSpeedP, ap::kSpeedP},
                                                        {peer::kSpeedI, ap::kSpeedI},
                                                        {peer::kHeadingP, ap::kHeadingP},
                                                        {peer::kHeadingI, ap::kHeadingI},
                                                        {peer::kBankP, ap::kBankP},
                                                        {peer::kRollRate, ap::kRollRate},
                                                        {peer::kYawRate, ap::kYawRate}}};
  for (std::size_t i = 0; i < gains.size(); ++i) {
    EXPECT_EQ(bits(gains[i][0]), bits(gains[i][1])) << "gain " << i;
  }
  const auto pin_range = [](const peer::Range& mine, const ap::Range& theirs, const char* name) {
    EXPECT_EQ(bits(mine.lo), bits(theirs.lo)) << name;
    EXPECT_EQ(bits(mine.hi), bits(theirs.hi)) << name;
  };
  pin_range(peer::kElevatorLimit, ap::kElevatorLimit, "elevator");
  pin_range(peer::kAileronLimit, ap::kAileronLimit, "aileron");
  pin_range(peer::kRudderLimit, ap::kRudderLimit, "rudder");
  pin_range(peer::kThrottleLimit, ap::kThrottleLimit, "throttle");
  pin_range(peer::kBankCommandLimit, ap::kBankCommandLimit, "bank command");
  EXPECT_EQ(peer::kModeFlags, ap::kModeFlags);
  EXPECT_EQ(bits(peer::kMaxIntegrationDt), bits(ap::kMaxIntegrationDt));
  // autopilot.cpp:14's kPi is internal to that file; its literal rounds to the correctly rounded
  // pi, which std::numbers::pi is. HeadingWrap exercises the wrap itself.
  EXPECT_EQ(bits(peer::kPi), bits(std::numbers::pi));
  EXPECT_EQ(bits(peer::kTwoPi), bits(2.0 * std::numbers::pi));
  // Gains{} is the named constants (the plan's amendment 2).
  const peer::Gains defaults{};
  const std::array<std::array<double, 2>, 12> fields = {{{defaults.alt_p, peer::kAltP},
                                                         {defaults.alt_i, peer::kAltI},
                                                         {defaults.alt_d, peer::kAltD},
                                                         {defaults.pitch_theta, peer::kPitchTheta},
                                                         {defaults.pitch_rate, peer::kPitchRate},
                                                         {defaults.speed_p, peer::kSpeedP},
                                                         {defaults.speed_i, peer::kSpeedI},
                                                         {defaults.heading_p, peer::kHeadingP},
                                                         {defaults.heading_i, peer::kHeadingI},
                                                         {defaults.bank_p, peer::kBankP},
                                                         {defaults.roll_rate, peer::kRollRate},
                                                         {defaults.yaw_rate, peer::kYawRate}}};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    EXPECT_EQ(bits(fields[i][0]), bits(fields[i][1])) << "Gains{} field " << i;
  }
}

// --- 2. Hand-built sequences (§8 item 2) ---------------------------------------------------------

TEST(AutopilotPeerEquivalence, FirstFrameAndTheTrimBiases) {
  {
    Pair pair;
    const astra_sw_to_sim_v1 out = pair.step(nominal(0, 0.0));
    EXPECT_EQ(bits(out.elevator), bits(kElevatorTrim));
    EXPECT_EQ(bits(out.throttle[0]), bits(kThrottleTrim));
  }
  {
    Pair pair;
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.status_flags = ASTRA_ICD_STATUS_WIND_INVALID;
    const astra_sw_to_sim_v1 out = pair.step(in);  // the trim biases, 0, 0
    EXPECT_EQ(bits(out.elevator), bits(kElevatorTrim));
    EXPECT_EQ(bits(out.aileron), bits(0.0));
    EXPECT_EQ(pair.held(), 1U);
    for (std::uint64_t k = 1; k < 50; ++k) {
      astra_sim_to_sw_v1 next = at_step(k);
      next.altitude = kAltitude + 1.0;
      pair.step(next);
    }
  }
}

TEST(AutopilotPeerEquivalence, HeldFramesAndRecovery) {
  Pair pair;
  std::uint64_t k = 0;
  lead_in(pair, k, 20);
  for (int i = 0; i < 10; ++i, ++k) {  // held: WIND_INVALID, then a non-finite read field
    astra_sim_to_sw_v1 in = at_step(k);
    in.altitude = kAltitude - 50.0;
    if (i < 5) {
      in.status_flags = ASTRA_ICD_STATUS_WIND_INVALID;
    } else {
      in.tas = kNan;
    }
    pair.step(in);
  }
  lead_in(pair, k, 20);  // recovery integrates over one period, never the gap
  EXPECT_EQ(pair.held(), 10U);
}

TEST(AutopilotPeerEquivalence, WindInvalidHoldsWithFiniteFields) {
  Pair pair;
  std::uint64_t k = 0;
  lead_in(pair, k, 10);
  for (int i = 0; i < 30; ++i, ++k) {
    astra_sim_to_sw_v1 in = at_step(k);
    in.altitude = kAltitude + static_cast<double>(i);
    in.status_flags = (i % 3 == 0) ? ASTRA_ICD_STATUS_WIND_INVALID : 0U;
    pair.step(in);
  }
}

TEST(AutopilotPeerEquivalence, EachReadFieldNonFinite) {
  for (const Field field : kReadFields) {
    for (const double bad : kNonFinite) {
      Pair pair;
      std::uint64_t k = 0;
      lead_in(pair, k, 5);
      astra_sim_to_sw_v1 in = at_step(k++);
      in.*field = bad;
      pair.step(in);
      lead_in(pair, k, 5);
      EXPECT_EQ(pair.held(), 1U);
    }
  }
  for (const Field field : kUnreadFields) {  // not read, so never held
    for (const double bad : kNonFinite) {
      Pair pair;
      std::uint64_t k = 0;
      lead_in(pair, k, 5);
      astra_sim_to_sw_v1 in = at_step(k++);
      in.*field = bad;
      pair.step(in);
      lead_in(pair, k, 5);
      EXPECT_EQ(pair.held(), 0U);
    }
  }
}

TEST(AutopilotPeerEquivalence, StatusBitsThatTouchNoReadField) {
  for (const std::uint32_t bit :
       {ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, ASTRA_ICD_STATUS_CAS_SUPERSONIC,
        ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_LATLON_NOT_MODELED}) {
    Pair pair;
    std::uint64_t k = 0;
    lead_in(pair, k, 5);
    for (int i = 0; i < 5; ++i, ++k) {
      astra_sim_to_sw_v1 in = at_step(k);
      in.status_flags = bit;
      in.altitude = kAltitude - 10.0;
      pair.step(in);
    }
    EXPECT_EQ(pair.held(), 0U);
  }
}

TEST(AutopilotPeerEquivalence, SaturationOfEachOutput) {
  struct Drive {
    Field field;
    double value;
  };
  const std::array<Drive, 10> drives = {{{&astra_sim_to_sw_v1::altitude, kAltitude + 5000.0},
                                         {&astra_sim_to_sw_v1::altitude, kAltitude - 5000.0},
                                         {&astra_sim_to_sw_v1::tas, kTas + 100.0},
                                         {&astra_sim_to_sw_v1::tas, kTas - 100.0},
                                         {&astra_sim_to_sw_v1::phi, 1.5},
                                         {&astra_sim_to_sw_v1::phi, -1.5},
                                         {&astra_sim_to_sw_v1::r, 2.0},
                                         {&astra_sim_to_sw_v1::r, -2.0},
                                         {&astra_sim_to_sw_v1::psi, 1.0},
                                         {&astra_sim_to_sw_v1::psi, -1.0}}};
  for (const Drive& drive : drives) {
    Pair pair;
    std::uint64_t k = 0;
    lead_in(pair, k, 3);
    for (int i = 0; i < 200; ++i, ++k) {
      astra_sim_to_sw_v1 in = at_step(k);
      in.*drive.field = drive.value;
      pair.step(in);
    }
    lead_in(pair, k, 100);
  }
}

TEST(AutopilotPeerEquivalence, AntiWindupAtAndAcrossEachLimit) {
  // Each PID's channel and the measurement that drives its error: altitude (elevator), tas
  // (throttle), psi (the bank command).
  struct Loop {
    Field field;
    double setpoint;
    double scale;  // an error of +-scale saturates the channel by about a factor of four
  };
  const std::array<Loop, 3> loops = {{{&astra_sim_to_sw_v1::altitude, kAltitude, 8000.0},
                                      {&astra_sim_to_sw_v1::tas, kTas, 60.0},
                                      {&astra_sim_to_sw_v1::psi, 0.0, 0.6}}};
  for (const Loop& loop : loops) {
    for (const double side : {1.0, -1.0}) {
      Pair pair;
      std::uint64_t k = 0;
      // Beyond the limit and winding further (held), a step that lands outside but nearer (taken),
      // then the error's sign change (it unwinds at once), and back across the limit.
      const std::array<double, 7> ramp = {1.0, 1.0, 0.6, 0.3, -0.05, -0.3, 0.2};
      for (const double level : ramp) {
        for (int i = 0; i < 150; ++i, ++k) {
          astra_sim_to_sw_v1 in = at_step(k);
          in.*loop.field = loop.setpoint - side * level * loop.scale;
          pair.step(in);
        }
      }
    }
  }
  // Exactly at a limit: each trim on its limit, with errors that push outward and inward.
  for (const Parameters& p : {Parameters{.elevator_trim = 0.3}, Parameters{.elevator_trim = -0.3},
                              Parameters{.throttle_trim = 0.0}, Parameters{.throttle_trim = 1.0}}) {
    Pair pair(p);
    std::uint64_t k = 0;
    for (const double error : {0.0, 1.0, -1.0, 0.0, 5.0, -5.0}) {
      for (int i = 0; i < 100; ++i, ++k) {
        astra_sim_to_sw_v1 in = at_step(k);
        in.altitude = kAltitude - error;
        in.tas = kTas - error * 0.01;
        pair.step(in);
      }
    }
  }
}

TEST(AutopilotPeerEquivalence, DtEdges) {
  // t, in order: the first frame (dt 0), a period, a repeated t, a backward t, a 1.5 s gap,
  // exactly 1.0 s (integrates), NaN and the frame after it, Inf and the frame after it, then -0.0
  // and a subnormal positive dt on a fresh pair.
  const std::array<double, 14> times = {0.0,  0.01, 0.01, 0.0,  1.5,  2.0,   3.0,
                                        kNan, 3.01, kInf, 3.02, 3.03, -kInf, 3.04};
  Pair pair;
  for (std::size_t i = 0; i < times.size(); ++i) {
    astra_sim_to_sw_v1 in = nominal(i * 10, times[i]);
    in.altitude = kAltitude - 7.0;
    in.tas = kTas - 1.0;
    in.psi = 0.02;
    pair.step(in);
  }
  Pair tiny;
  const std::array<double, 5> small = {-0.0, std::numeric_limits<double>::denorm_min(), 1e-310,
                                       1e-300, 0.01};
  for (std::size_t i = 0; i < small.size(); ++i) {
    astra_sim_to_sw_v1 in = nominal(i * 10, small[i]);
    in.altitude = kAltitude - 1e6;
    in.tas = kTas - 1e3;
    in.psi = 1.0;
    tiny.step(in);
  }
}

TEST(AutopilotPeerEquivalence, HeadingWrap) {
  for (const double heading : {0.0, 3.0, -std::numbers::pi, std::numbers::pi}) {
    Pair pair(Parameters{.heading = heading});
    std::uint64_t k = 0;
    const std::array<double, 12> psis = {std::numbers::pi,
                                         -std::numbers::pi,
                                         3.0 * std::numbers::pi,
                                         -3.0 * std::numbers::pi,
                                         std::nextafter(std::numbers::pi, 4.0),
                                         std::nextafter(-std::numbers::pi, -4.0),
                                         3.1,
                                         -3.1,
                                         0.5,
                                         -0.5,
                                         6.0,
                                         -6.0};
    for (const double psi : psis) {
      for (int i = 0; i < 20; ++i, ++k) {
        astra_sim_to_sw_v1 in = at_step(k);
        in.psi = psi;
        pair.step(in);
      }
    }
  }
}

TEST(AutopilotPeerEquivalence, SignedZeros) {
  Pair pair(Parameters{.altitude = -0.0, .heading = -0.0, .theta_trim = -0.0});
  for (std::uint64_t k = 0; k < 20; ++k) {
    astra_sim_to_sw_v1 in{};
    in.magic = ASTRA_ICD_MAGIC;
    in.version = ASTRA_ICD_VERSION;
    in.frame = k * 10;
    const double zero = (k % 2 == 0) ? -0.0 : 0.0;
    in.t = k == 0 ? -0.0 : static_cast<double>(k) * kPeriod;
    in.altitude = zero;
    in.vertical_speed = zero;
    in.theta = zero;
    in.q = zero;
    in.psi = zero;
    in.phi = zero;
    in.p = zero;
    in.tas = kTas;
    in.r = zero;
    pair.step(in);
  }
}

// --- 3. Seeded sequences (§8 item 3; the generator is seeded_frames.hpp) ----------------------

namespace {

using autopilot_peer_test::kDtKinds;

struct Coverage {
  std::uint64_t held_wind = 0;
  std::uint64_t held_nonfinite = 0;
  std::array<std::uint64_t, 8> at_limit{};  // elevator, aileron, rudder, throttle: lo and hi
  std::array<std::uint64_t, 2> bank_at_limit{};
  std::array<std::uint64_t, 3> refused{};  // altitude, speed, heading
  std::array<std::uint64_t, 3> taken_at_limit{};
  std::array<std::uint64_t, kDtKinds> dt_kinds{};
};

void run_seed(std::uint64_t seed, Coverage& coverage) {
  autopilot_peer_test::SeededFrames frames(seed);
  const autopilot_peer_test::SeedParameters& sp = frames.parameters();
  Pair pair(Parameters{sp.altitude, sp.airspeed, sp.heading, sp.elevator_trim, sp.throttle_trim,
                       sp.theta_trim});
  double previous_now = 0.0;
  bool have_previous = false;
  for (std::size_t i = 0; i < autopilot_peer_test::kFramesPerSeed; ++i) {
    const astra_sim_to_sw_v1 in = frames.next();
    if (frames.has_dt_kind()) {
      ++coverage.dt_kinds[static_cast<std::size_t>(frames.last_dt_kind())];
    }

    // The coverage counts, from the frame and the peer law's own state (its test accessors).
    const peer::Law& law = pair.law();
    const std::array<double, 3> integral_before = {law.altitude_integral(), law.speed_integral(),
                                                   law.heading_integral()};
    double dt = have_previous ? in.t - previous_now : 0.0;
    previous_now = in.t;
    have_previous = true;
    if (!std::isfinite(dt) || dt <= 0.0 || dt > peer::kMaxIntegrationDt) {
      dt = 0.0;
    }
    const bool usable = peer::Law::usable(in);
    if ((in.status_flags & ASTRA_ICD_STATUS_WIND_INVALID) != 0U) {
      ++coverage.held_wind;
    } else if (!usable) {
      ++coverage.held_nonfinite;
    }

    const astra_sw_to_sim_v1 out = pair.step(in);

    const std::array<double, 8> outputs = {out.elevator,    out.elevator,   out.aileron,
                                           out.aileron,     out.rudder,     out.rudder,
                                           out.throttle[0], out.throttle[0]};
    const std::array<double, 8> limits = {-0.3, 0.3, -0.35, 0.35, -0.35, 0.35, 0.0, 1.0};
    for (std::size_t c = 0; c < outputs.size(); ++c) {
      coverage.at_limit[c] += outputs[c] == limits[c] ? 1U : 0U;
    }
    coverage.bank_at_limit[0] += law.bank_command() == -0.35 ? 1U : 0U;
    coverage.bank_at_limit[1] += law.bank_command() == 0.35 ? 1U : 0U;
    if (usable && dt > 0.0) {
      const std::array<double, 3> increments = {
          peer::kAltI * (sp.altitude - in.altitude) * dt,
          peer::kSpeedI * (sp.airspeed - in.tas) * dt,
          peer::kHeadingI * peer::wrap_pi(sp.heading - in.psi) * dt};
      const std::array<double, 3> integral_after = {law.altitude_integral(), law.speed_integral(),
                                                    law.heading_integral()};
      const std::array<bool, 3> saturated = {std::abs(out.elevator) == 0.3,
                                             out.throttle[0] == 0.0 || out.throttle[0] == 1.0,
                                             std::abs(law.bank_command()) == 0.35};
      for (std::size_t l = 0; l < 3; ++l) {
        if (increments[l] == 0.0) {
          continue;
        }
        if (integral_after[l] == integral_before[l]) {
          ++coverage.refused[l];
        } else if (saturated[l]) {
          ++coverage.taken_at_limit[l];
        }
      }
    }
  }
}

}  // namespace

TEST(AutopilotPeerEquivalence, SeededSequences) {
  Coverage coverage;
  for (const std::uint64_t seed : autopilot_peer_test::kSeeds) {
    run_seed(seed, coverage);
    if (::testing::Test::HasFailure()) {
      FAIL() << "seed " << seed << " diverged; see the first failure above";
    }
  }
  // §8 item 3's coverage, over the eight seeds.
  EXPECT_GT(coverage.held_wind, 0U);
  EXPECT_GT(coverage.held_nonfinite, 0U);
  for (std::size_t c = 0; c < coverage.at_limit.size(); ++c) {
    EXPECT_GT(coverage.at_limit[c], 0U) << "reply channel limit " << c;
  }
  EXPECT_GT(coverage.bank_at_limit[0], 0U);
  EXPECT_GT(coverage.bank_at_limit[1], 0U);
  for (std::size_t l = 0; l < 3; ++l) {
    EXPECT_GT(coverage.refused[l], 0U) << "loop " << l << ": no refused integral step";
    EXPECT_GT(coverage.taken_at_limit[l], 0U) << "loop " << l << ": no step taken at its limit";
  }
  for (std::size_t d = 0; d < kDtKinds; ++d) {
    EXPECT_GT(coverage.dt_kinds[d], 0U) << "dt kind " << d;
  }
}

// --- 4. The law's per-frame path allocates nothing (§8 item 4) -----------------------------------

TEST(AutopilotPeerEquivalence, LawUpdateAllocatesNothing) {
  peer::Law law(peer::Setpoints{kAltitude, kTas, 0.0},
                peer::Trims{kElevatorTrim, kThrottleTrim, kThetaTrim});
  astra_sw_to_sim_v1 out{};
  const std::size_t before = astra_test::allocation_count();
  for (std::uint64_t k = 0; k < 10000; ++k) {
    astra_sim_to_sw_v1 in = at_step(k);
    in.altitude = kAltitude - static_cast<double>(k % 100);
    law.update(in, in.t, out);
  }
  EXPECT_EQ(astra_test::allocation_count(), before);
}
