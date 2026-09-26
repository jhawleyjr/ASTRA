// M4 PR 5 (docs/plans/m4-pr5-autopilot.md R5): the autopilot's control law and command line on
// scripted frames. No HAL, no shm, no sim: astra_autopilot_lib (icd/ only) plus GoogleTest. The
// arithmetic is closed-form wherever the plan says so, written here from the gains.hpp constants,
// so a gain change keeps these tests true and a sign, clamp, or dt error breaks them.
#include "embedded/autopilot/autopilot.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "embedded/autopilot/cli.hpp"
#include "embedded/autopilot/gains.hpp"
#include "embedded/autopilot/limits.hpp"
#include "icd/astra_icd.h"
#include "tests/support/alloc_counter.hpp"

namespace {

using astra_autopilot::Autopilot;
using astra_autopilot::Setpoints;
using astra_autopilot::Trims;
namespace ap = astra_autopilot;

// The notional transport's still-air trim at 230 m/s, 10 km (cruise.yaml's initial controls).
constexpr double kAltitude = 10000.0;
constexpr double kTas = 230.0;
constexpr double kElevatorTrim = 0.026674474137989946;
constexpr double kThrottleTrim = 0.4985979490507746;
constexpr double kThetaTrim = 0.023586859573503434;
constexpr double kPeriod = 0.01;  // the exchange period at divisor 10
constexpr double kTolerance = 1e-14;
constexpr double kPi = std::numbers::pi;

Setpoints setpoints() { return {kAltitude, kTas, 0.0}; }
Trims trims() { return {kElevatorTrim, kThrottleTrim, kThetaTrim}; }

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

// Byte equality of two replies (the struct has no padding; compared as bytes, as the protocol
// copies it).
bool same_bytes(const astra_sw_to_sim_v1& a, const astra_sw_to_sim_v1& b) {
  return std::ranges::equal(std::as_bytes(std::span{&a, 1}), std::as_bytes(std::span{&b, 1}));
}

astra_sw_to_sim_v1 run_once(Autopilot& autopilot, const astra_sim_to_sw_v1& in) {
  astra_sw_to_sim_v1 out{};
  autopilot.update(in, in.t, out);
  return out;
}

// --- Frame 0 and the reply envelope ---------------------------------------------------------

TEST(Autopilot, FrameZeroReplyAtSetpointEqualsTrimExactly) {
  Autopilot autopilot(setpoints(), trims());
  const astra_sw_to_sim_v1 out = run_once(autopilot, nominal(7, 0.0));
  EXPECT_EQ(out.magic, ASTRA_ICD_MAGIC);
  EXPECT_EQ(out.version, ASTRA_ICD_VERSION);
  EXPECT_EQ(out.frame, 7U);
  EXPECT_EQ(out.elevator, kElevatorTrim);
  EXPECT_EQ(out.aileron, 0.0);
  EXPECT_EQ(out.rudder, 0.0);
  EXPECT_EQ(out.throttle[0], kThrottleTrim);
  EXPECT_EQ(out.throttle[1], kThrottleTrim);
  EXPECT_EQ(out.mode_flags, ap::kModeFlags);
  EXPECT_NE(out.mode_flags, 0U);
  EXPECT_EQ(out.fault_flags, 0U);
  EXPECT_EQ(autopilot.handled(), 1U);
  EXPECT_EQ(autopilot.held(), 0U);
}

TEST(Autopilot, EveryReplyCarriesTheConstantModeWordAndNoFault) {
  Autopilot autopilot(setpoints(), trims());
  for (std::uint64_t frame = 0; frame < 50; ++frame) {
    astra_sim_to_sw_v1 in = nominal(frame * 10, static_cast<double>(frame) * kPeriod);
    if (frame % 7 == 3) {
      in.altitude = std::numeric_limits<double>::quiet_NaN();  // a held frame carries them too
    }
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_EQ(out.mode_flags, ap::kModeFlags);
    EXPECT_EQ(out.fault_flags, 0U);
    EXPECT_EQ(out.throttle[0], out.throttle[1]);
  }
}

// --- Channel signs and closed-form arithmetic (dt = 0 on frame 0, so proportional terms only) ---

TEST(Autopilot, ElevatorAltitudeLoopSignAndValue) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = kAltitude - 10.0;  // below the setpoint: pitch up = negative elevator
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_LT(out.elevator, kElevatorTrim);
  EXPECT_NEAR(out.elevator, kElevatorTrim - ap::kAltP * 10.0, kTolerance);
}

TEST(Autopilot, ElevatorClimbRateDampsNoseUpMotion) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.vertical_speed = 2.0;  // climbing: derivative on measurement adds nose-down
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_GT(out.elevator, kElevatorTrim);
  EXPECT_NEAR(out.elevator, kElevatorTrim + ap::kAltD * 2.0, kTolerance);
}

TEST(Autopilot, ElevatorPitchFeedbackActsOnDeviationFromTrimTheta) {
  {
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.theta = kThetaTrim + 0.01;  // nose high: more nose-down
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_GT(out.elevator, kElevatorTrim);
    EXPECT_NEAR(out.elevator, kElevatorTrim + ap::kPitchTheta * 0.01, kTolerance);
  }
  {
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.q = 0.02;  // pitching up: nose-down
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_GT(out.elevator, kElevatorTrim);
    EXPECT_NEAR(out.elevator, kElevatorTrim + ap::kPitchRate * 0.02, kTolerance);
  }
}

TEST(Autopilot, ThrottleAirspeedLoopSignAndValue) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.tas = kTas - 2.0;  // slow: more thrust
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_GT(out.throttle[0], kThrottleTrim);
  EXPECT_NEAR(out.throttle[0], kThrottleTrim + ap::kSpeedP * 2.0, kTolerance);
  EXPECT_EQ(out.throttle[0], out.throttle[1]);
}

TEST(Autopilot, AileronHeadingBankAndRollRateLoops) {
  {  // heading error +0.1 rad (target to the right): roll right = positive aileron
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.psi = -0.1;
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_GT(out.aileron, 0.0);
    EXPECT_NEAR(out.aileron, ap::kBankP * (ap::kHeadingP * 0.1), kTolerance);
  }
  {  // banked right already: the bank loop opposes it
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.phi = 0.05;
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_LT(out.aileron, 0.0);
    EXPECT_NEAR(out.aileron, -ap::kBankP * 0.05, kTolerance);
  }
  {  // rolling right: the rate loop opposes it
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.p = 0.1;
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    EXPECT_LT(out.aileron, 0.0);
    EXPECT_NEAR(out.aileron, -ap::kRollRate * 0.1, kTolerance);
  }
}

TEST(Autopilot, BankCommandIsClampedBeforeTheBankLoop) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.psi = -1.0;  // a 1 rad heading error would command 1.987 rad of bank unclamped
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_NEAR(out.aileron, ap::kBankP * ap::kBankCommandLimit.hi, kTolerance);
}

TEST(Autopilot, RudderYawDamperOpposesYawRateAndSideslip) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.r = 0.1;
  in.beta = 0.05;
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_NEAR(out.rudder, ap::kYawRate * 0.1 + ap::kSideslip * 0.05, kTolerance);
  EXPECT_GT(out.rudder, 0.0);
}

TEST(Autopilot, HeadingErrorWrapsTheShortWay) {
  Autopilot autopilot(Setpoints{kAltitude, kTas, -kPi + 0.01}, trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.psi = kPi - 0.01;  // 0.02 rad to the right through +/-pi, not 6.26 rad to the left
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_GT(out.aileron, 0.0);
  EXPECT_NEAR(out.aileron, ap::kBankP * (ap::kHeadingP * 0.02), 1e-12);
}

// --- dt from frame-derived time --------------------------------------------------------------

TEST(Autopilot, IntegralAdvancesByKiTimesErrorTimesTheTimestampDifference) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = kAltitude - 10.0;
  const astra_sw_to_sim_v1 first = run_once(autopilot, in);  // dt = 0: proportional only
  EXPECT_NEAR(first.elevator, kElevatorTrim - ap::kAltP * 10.0, kTolerance);
  in.frame = 10;
  in.t = kPeriod;
  const astra_sw_to_sim_v1 second = run_once(autopilot, in);
  EXPECT_NEAR(second.elevator, kElevatorTrim - (ap::kAltP * 10.0 + ap::kAltI * 10.0 * kPeriod),
              kTolerance);
}

TEST(Autopilot, IntegralScalesWithTheFrameSpacingNotAnAssumedPeriod) {
  const auto integral_after_two_frames = [](double spacing) {
    Autopilot autopilot(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.altitude = kAltitude - 10.0;
    (void)run_once(autopilot, in);
    in.frame = 1;
    in.t = spacing;
    const astra_sw_to_sim_v1 out = run_once(autopilot, in);
    return (kElevatorTrim - out.elevator) - ap::kAltP * 10.0;  // the integral term
  };
  const double ten = integral_after_two_frames(0.01);
  const double twenty = integral_after_two_frames(0.02);
  EXPECT_GT(ten, 0.0);
  EXPECT_NEAR(twenty / ten, 2.0, 1e-9);
}

TEST(Autopilot, FrameZeroRepeatedTimeAndBackwardsTimeDoNotIntegrate) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 5.0);
  in.altitude = kAltitude - 10.0;
  const double proportional_only = kElevatorTrim - ap::kAltP * 10.0;
  EXPECT_NEAR(run_once(autopilot, in).elevator, proportional_only, kTolerance);  // frame 0
  EXPECT_NEAR(run_once(autopilot, in).elevator, proportional_only, kTolerance);  // same t
  in.t = 4.0;                                                                    // backwards
  EXPECT_NEAR(run_once(autopilot, in).elevator, proportional_only, kTolerance);
  in.t = 4.0 + 2.0;  // a 2 s gap is a stall, not a sample period: over kMaxIntegrationDt
  EXPECT_NEAR(run_once(autopilot, in).elevator, proportional_only, kTolerance);
}

TEST(Autopilot, NonFiniteTimeDoesNotIntegrateOrCorruptLaterFrames) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = kAltitude - 10.0;
  (void)run_once(autopilot, in);
  astra_sw_to_sim_v1 out{};
  autopilot.update(in, std::numeric_limits<double>::quiet_NaN(), out);
  EXPECT_TRUE(std::isfinite(out.elevator));
  in.t = 2 * kPeriod;
  autopilot.update(in, in.t, out);  // previous_now_ was NaN: this frame has no usable dt either
  EXPECT_TRUE(std::isfinite(out.elevator));
  EXPECT_NEAR(out.elevator, kElevatorTrim - ap::kAltP * 10.0, kTolerance);
}

// --- Anti-windup and the output limits ------------------------------------------------------

TEST(Autopilot, SaturatedLoopDoesNotWindUp) {
  // Integrate a small in-range error first, then sit saturated at the top for 29 s.
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  for (int k = 0; k < 100; ++k) {
    in.tas = kTas - 1.0;
    in.frame = static_cast<std::uint64_t>(k);
    in.t = k * kPeriod;
    (void)run_once(autopilot, in);
  }
  for (int k = 100; k < 3000; ++k) {
    in.tas = kTas - 100.0;  // 100 m/s slow: proportional alone saturates the throttle at 1
    in.frame = static_cast<std::uint64_t>(k);
    in.t = k * kPeriod;
    EXPECT_EQ(run_once(autopilot, in).throttle[0], ap::kThrottleLimit.hi);
  }
  // Back at zero error: the integral is exactly what the small error accumulated before saturation
  // (99 steps of dt = 0.01), so the throttle leaves the top at once. A wound-up integral would not.
  const astra_sw_to_sim_v1 out = run_once(autopilot, nominal(3000, 3000 * kPeriod));
  EXPECT_NEAR(out.throttle[0], kThrottleTrim + ap::kSpeedI * 1.0 * (99 * kPeriod), 1e-9);
  EXPECT_LT(out.throttle[0], 0.6);
}

TEST(Autopilot, SaturationAtTheLowLimitAlsoHoldsTheIntegral) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  // Build integral inside the range first (tas 1 m/s slow), then overshoot by a lot the other way.
  for (int k = 0; k < 500; ++k) {
    in.tas = kTas - 1.0;
    in.frame = static_cast<std::uint64_t>(k);
    in.t = k * kPeriod;
    (void)run_once(autopilot, in);
  }
  in.tas = kTas + 100.0;  // far fast: throttle saturates at 0
  for (int k = 500; k < 700; ++k) {
    in.frame = static_cast<std::uint64_t>(k);
    in.t = k * kPeriod;
    EXPECT_EQ(run_once(autopilot, in).throttle[0], ap::kThrottleLimit.lo);
  }
  // The integral was not driven further down while saturated, so a small fast error leaves the
  // range at once instead of staying pinned at the low limit.
  in.tas = kTas + 0.5;
  in.frame = 700;
  in.t = 700 * kPeriod;
  const double throttle = run_once(autopilot, in).throttle[0];
  EXPECT_GT(throttle, ap::kThrottleLimit.lo);
}

TEST(Autopilot, EveryOutputStaysInsideItsLimitUnderViolentInput) {
  Autopilot autopilot(setpoints(), trims());
  const std::array<double, 4> magnitudes = {1e6, -1e6, 1e3, -1e3};
  std::uint64_t frame = 0;
  for (const double big : magnitudes) {
    for (int k = 0; k < 20; ++k) {
      astra_sim_to_sw_v1 in = nominal(frame, static_cast<double>(frame) * kPeriod);
      in.altitude = kAltitude + big;
      in.vertical_speed = big;
      in.theta = big;
      in.q = big;
      in.psi = big;
      in.phi = big;
      in.p = big;
      in.tas = kTas + big;
      in.r = big;
      in.beta = big;
      const astra_sw_to_sim_v1 out = run_once(autopilot, in);
      ++frame;
      EXPECT_GE(out.elevator, ap::kElevatorLimit.lo);
      EXPECT_LE(out.elevator, ap::kElevatorLimit.hi);
      EXPECT_GE(out.aileron, ap::kAileronLimit.lo);
      EXPECT_LE(out.aileron, ap::kAileronLimit.hi);
      EXPECT_GE(out.rudder, ap::kRudderLimit.lo);
      EXPECT_LE(out.rudder, ap::kRudderLimit.hi);
      EXPECT_GE(out.throttle[0], ap::kThrottleLimit.lo);
      EXPECT_LE(out.throttle[0], ap::kThrottleLimit.hi);
      EXPECT_EQ(out.throttle[0], out.throttle[1]);
    }
  }
}

// --- Hold on an unusable frame ----------------------------------------------------------------

using Field = double astra_sim_to_sw_v1::*;

TEST(Autopilot, EachReadFieldHoldsThePreviousCommandWhenNonFinite) {
  const std::array<Field, 10> read = {
      &astra_sim_to_sw_v1::altitude, &astra_sim_to_sw_v1::vertical_speed,
      &astra_sim_to_sw_v1::theta,    &astra_sim_to_sw_v1::q,
      &astra_sim_to_sw_v1::psi,      &astra_sim_to_sw_v1::phi,
      &astra_sim_to_sw_v1::p,        &astra_sim_to_sw_v1::tas,
      &astra_sim_to_sw_v1::r,        &astra_sim_to_sw_v1::beta};
  const std::array<double, 3> bad = {std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::infinity(),
                                     -std::numeric_limits<double>::infinity()};
  for (const Field field : read) {
    for (const double value : bad) {
      Autopilot autopilot(setpoints(), trims());
      astra_sim_to_sw_v1 in = nominal(0, 0.0);
      in.altitude = kAltitude - 10.0;  // a reply that is visibly not the trim
      in.psi = -0.05;
      in.tas = kTas - 1.0;
      in.r = 0.02;
      const astra_sw_to_sim_v1 before = run_once(autopilot, in);
      in.frame = 10;
      in.t = kPeriod;
      in.*field = value;
      const astra_sw_to_sim_v1 held = run_once(autopilot, in);
      EXPECT_EQ(held.elevator, before.elevator);
      EXPECT_EQ(held.aileron, before.aileron);
      EXPECT_EQ(held.rudder, before.rudder);
      EXPECT_EQ(held.throttle[0], before.throttle[0]);
      EXPECT_EQ(held.throttle[1], before.throttle[1]);
      EXPECT_EQ(held.frame, 10U);  // the reply still answers this frame
      EXPECT_EQ(autopilot.held(), 1U);
      EXPECT_EQ(autopilot.handled(), 2U);
    }
  }
}

TEST(Autopilot, WindInvalidHoldsEvenWithFiniteFields) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = kAltitude - 10.0;
  const astra_sw_to_sim_v1 before = run_once(autopilot, in);
  in.frame = 10;
  in.t = kPeriod;
  in.tas = 0.0;  // what the ICD puts there when wind_n is non-finite
  in.status_flags = ASTRA_ICD_STATUS_WIND_INVALID;
  const astra_sw_to_sim_v1 held = run_once(autopilot, in);
  EXPECT_EQ(held.throttle[0], before.throttle[0]);  // not full throttle for a zero tas
  EXPECT_EQ(held.elevator, before.elevator);
  EXPECT_EQ(autopilot.held(), 1U);
}

TEST(Autopilot, StatusBitsThatTouchNoReadFieldAreIgnored) {
  for (const std::uint32_t bit :
       {ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_LATLON_NOT_MODELED,
        ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, ASTRA_ICD_STATUS_CAS_SUPERSONIC}) {
    Autopilot with_bit(setpoints(), trims());
    Autopilot without(setpoints(), trims());
    astra_sim_to_sw_v1 in = nominal(0, 0.0);
    in.altitude = kAltitude - 10.0;
    in.status_flags = bit;
    in.mach = 0.0;  // ATMOSPHERE_INVALID and CAS_SUPERSONIC zero these; no loop reads them
    in.cas = 0.0;
    astra_sim_to_sw_v1 clean = in;
    clean.status_flags = 0;
    const astra_sw_to_sim_v1 a = run_once(with_bit, in);
    const astra_sw_to_sim_v1 b = run_once(without, clean);
    EXPECT_TRUE(same_bytes(a, b)) << "status bit " << bit;
    EXPECT_EQ(with_bit.held(), 0U) << "status bit " << bit;
  }
}

TEST(Autopilot, FirstFrameUnusableRepliesTheTrimBiasesWithinTheOutputLimits) {
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = std::numeric_limits<double>::quiet_NaN();
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_EQ(out.elevator, kElevatorTrim);
  EXPECT_EQ(out.aileron, 0.0);
  EXPECT_EQ(out.rudder, 0.0);
  EXPECT_EQ(out.throttle[0], kThrottleTrim);
  EXPECT_GE(out.elevator, ap::kElevatorLimit.lo);
  EXPECT_LE(out.elevator, ap::kElevatorLimit.hi);
  EXPECT_EQ(autopilot.held(), 1U);
}

TEST(Autopilot, HeldFramesLeaveLoopStateAloneAndRecoveryIntegratesOnePeriod) {
  // valid (t = 0), held (t = 0.01), valid (t = 0.02): the integral must gain exactly one period's
  // worth (the valid frame's own dt = 0.01), not two (a frozen previous time) and not zero.
  Autopilot autopilot(setpoints(), trims());
  astra_sim_to_sw_v1 in = nominal(0, 0.0);
  in.altitude = kAltitude - 10.0;
  (void)run_once(autopilot, in);
  astra_sim_to_sw_v1 held = in;
  held.frame = 10;
  held.t = kPeriod;
  held.q = std::numeric_limits<double>::quiet_NaN();
  (void)run_once(autopilot, held);
  in.frame = 20;
  in.t = 2 * kPeriod;
  const astra_sw_to_sim_v1 out = run_once(autopilot, in);
  EXPECT_NEAR(out.elevator, kElevatorTrim - (ap::kAltP * 10.0 + ap::kAltI * 10.0 * kPeriod),
              kTolerance);

  // Several held frames in a row: the same single period on recovery.
  Autopilot several(setpoints(), trims());
  astra_sim_to_sw_v1 first = nominal(0, 0.0);
  first.altitude = kAltitude - 10.0;
  (void)run_once(several, first);
  for (int k = 1; k <= 5; ++k) {
    astra_sim_to_sw_v1 bad = first;
    bad.frame = 10ULL * static_cast<std::uint64_t>(k);
    bad.t = k * kPeriod;
    bad.altitude = std::numeric_limits<double>::infinity();
    (void)run_once(several, bad);
  }
  astra_sim_to_sw_v1 recovered = first;
  recovered.frame = 60;
  recovered.t = 6 * kPeriod;
  const astra_sw_to_sim_v1 result = run_once(several, recovered);
  EXPECT_NEAR(result.elevator, kElevatorTrim - (ap::kAltP * 10.0 + ap::kAltI * 10.0 * kPeriod),
              kTolerance);
  EXPECT_EQ(several.held(), 5U);
}

// --- Determinism and the per-frame path ---------------------------------------------------------

std::vector<astra_sim_to_sw_v1> script() {
  std::vector<astra_sim_to_sw_v1> frames;
  for (std::uint64_t k = 0; k < 400; ++k) {
    astra_sim_to_sw_v1 in = nominal(k * 10, static_cast<double>(k) * kPeriod);
    const double phase = static_cast<double>(k) * 0.05;
    in.altitude = kAltitude + 20.0 * std::sin(phase);
    in.vertical_speed = 1.0 * std::cos(phase);
    in.tas = kTas + 2.0 * std::sin(1.3 * phase);
    in.psi = 0.05 * std::sin(0.7 * phase);
    in.phi = 0.02 * std::cos(0.9 * phase);
    in.theta = kThetaTrim + 0.01 * std::sin(phase);
    in.q = 0.01 * std::cos(phase);
    in.p = 0.005 * std::sin(phase);
    in.r = 0.004 * std::cos(phase);
    in.beta = 0.003 * std::sin(phase);
    if (k % 97 == 50) {
      in.status_flags = ASTRA_ICD_STATUS_WIND_INVALID;
    }
    frames.push_back(in);
  }
  return frames;
}

TEST(Autopilot, ReplayingTheSameScriptIsBitIdentical) {
  Autopilot a(setpoints(), trims());
  Autopilot b(setpoints(), trims());
  for (const astra_sim_to_sw_v1& in : script()) {
    const astra_sw_to_sim_v1 out_a = run_once(a, in);
    const astra_sw_to_sim_v1 out_b = run_once(b, in);
    ASSERT_TRUE(same_bytes(out_a, out_b)) << "frame " << in.frame;
  }
  EXPECT_EQ(a.held(), b.held());
  EXPECT_GT(a.held(), 0U);  // the script does include held frames
}

TEST(Autopilot, UpdateAllocatesNothing) {
  Autopilot autopilot(setpoints(), trims());
  const std::vector<astra_sim_to_sw_v1> frames = script();
  astra_sw_to_sim_v1 out{};
  const std::size_t before = astra_test::allocation_count();
  for (const astra_sim_to_sw_v1& in : frames) {
    autopilot.update(in, in.t, out);
  }
  EXPECT_EQ(astra_test::allocation_count(), before);
}

// --- The command line (D2)
// ------------------------------------------------------------------------

std::vector<std::string> good_arguments() {
  return {"--segment",         "astra_1_abc", "--handshake-timeout-ms", "5000",
          "--peer-timeout-ms", "1000",        "--altitude-m",           "10000",
          "--airspeed-mps",    "230",         "--heading-rad",          "0.5",
          "--elevator-trim",   "0.0266744",   "--throttle-trim",        "0.4986",
          "--theta-trim",      "0.0235868"};
}

bool parse(const std::vector<std::string>& arguments, ap::Options& options, std::string& error) {
  std::vector<const char*> argv{"astra_autopilot"};
  for (const std::string& argument : arguments) {
    argv.push_back(argument.c_str());
  }
  return ap::parse_arguments(static_cast<int>(argv.size()), argv.data(), options, error);
}

TEST(AutopilotCli, AcceptsTheNineNamedArguments) {
  ap::Options options;
  std::string error;
  ASSERT_TRUE(parse(good_arguments(), options, error)) << error;
  EXPECT_EQ(options.segment, "astra_1_abc");
  EXPECT_EQ(options.handshake_timeout_ms, 5000U);
  EXPECT_EQ(options.peer_timeout_ms, 1000U);
  EXPECT_EQ(options.setpoints.altitude_m, 10000.0);
  EXPECT_EQ(options.setpoints.airspeed_mps, 230.0);
  EXPECT_EQ(options.setpoints.heading_rad, 0.5);
  EXPECT_EQ(options.trims.elevator_rad, 0.0266744);
  EXPECT_EQ(options.trims.throttle, 0.4986);
  EXPECT_EQ(options.trims.theta_rad, 0.0235868);
  EXPECT_EQ(options.notify_fd, -1);
  EXPECT_EQ(options.stop_after, 0U);
  EXPECT_EQ(options.abort_after, 0U);
}

TEST(AutopilotCli, EachNamedArgumentIsRequired) {
  const std::vector<std::string> all = good_arguments();
  for (std::size_t skip = 0; skip < all.size(); skip += 2) {
    std::vector<std::string> arguments;
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (i != skip && i != skip + 1) {
        arguments.push_back(all[i]);
      }
    }
    ap::Options options;
    std::string error;
    EXPECT_FALSE(parse(arguments, options, error)) << "without " << all[skip];
    EXPECT_NE(error.find(all[skip]), std::string::npos) << error;
  }
}

TEST(AutopilotCli, RejectsUnknownDuplicateAndValuelessArguments) {
  ap::Options options;
  std::string error;
  std::vector<std::string> arguments = good_arguments();
  arguments.emplace_back("--bogus");
  arguments.emplace_back("1");
  EXPECT_FALSE(parse(arguments, options, error));
  arguments = good_arguments();
  arguments.emplace_back("--segment");
  arguments.emplace_back("again");
  EXPECT_FALSE(parse(arguments, options, error));
  arguments = good_arguments();
  arguments.emplace_back("--notify-fd");  // no value
  EXPECT_FALSE(parse(arguments, options, error));
  EXPECT_FALSE(parse({}, options, error));
}

TEST(AutopilotCli, RejectsBadNumbers) {
  const std::vector<std::pair<std::string, std::string>> bad = {
      {"--handshake-timeout-ms", "0"},
      {"--handshake-timeout-ms", "-5"},
      {"--handshake-timeout-ms", "4294967296"},
      {"--handshake-timeout-ms", "abc"},
      {"--handshake-timeout-ms", "12x"},
      {"--peer-timeout-ms", "0"},
      {"--peer-timeout-ms", " 7"},
      {"--altitude-m", "nan"},
      {"--altitude-m", "inf"},
      {"--altitude-m", "1e999"},
      {"--altitude-m", ""},
      {"--airspeed-mps", "abc"},
      {"--heading-rad", "-inf"},
      {"--theta-trim", "nan"},
      {"--segment", ""}};
  for (const auto& [flag, value] : bad) {
    std::vector<std::string> arguments = good_arguments();
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
      if (arguments[i] == flag) {
        arguments[i + 1] = value;
      }
    }
    ap::Options options;
    std::string error;
    EXPECT_FALSE(parse(arguments, options, error)) << flag << " " << value;
  }
}

TEST(AutopilotCli, TrimsMustSatisfyTheOutputLimits) {
  const std::vector<std::pair<std::string, std::string>> bad = {
      {"--elevator-trim", "0.31"}, {"--elevator-trim", "-0.31"}, {"--elevator-trim", "1"},
      {"--throttle-trim", "1.01"}, {"--throttle-trim", "-0.01"}, {"--throttle-trim", "nan"}};
  for (const auto& [flag, value] : bad) {
    std::vector<std::string> arguments = good_arguments();
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
      if (arguments[i] == flag) {
        arguments[i + 1] = value;
      }
    }
    ap::Options options;
    std::string error;
    EXPECT_FALSE(parse(arguments, options, error)) << flag << " " << value;
  }
  // The limits themselves are accepted: a clamped or trimmed value equal to lo/hi is in range.
  for (const auto& [flag, value] :
       std::vector<std::pair<std::string, std::string>>{{"--elevator-trim", "0.3"},
                                                        {"--elevator-trim", "-0.3"},
                                                        {"--throttle-trim", "0"},
                                                        {"--throttle-trim", "1"}}) {
    std::vector<std::string> arguments = good_arguments();
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
      if (arguments[i] == flag) {
        arguments[i + 1] = value;
      }
    }
    ap::Options options;
    std::string error;
    EXPECT_TRUE(parse(arguments, options, error)) << flag << " " << value << ": " << error;
  }
}

TEST(AutopilotCli, TestHooksNeedNotifyFdAndExactlyOneCount) {
  const auto with = [](std::initializer_list<const char*> extra) {
    std::vector<std::string> arguments = good_arguments();
    for (const char* item : extra) {
      arguments.emplace_back(item);
    }
    return arguments;
  };
  ap::Options options;
  std::string error;
  EXPECT_TRUE(parse(with({"--notify-fd", "3", "--stop-after", "5"}), options, error)) << error;
  EXPECT_EQ(options.notify_fd, 3);
  EXPECT_EQ(options.stop_after, 5U);
  ap::Options abort_options;
  EXPECT_TRUE(parse(with({"--notify-fd", "3", "--abort-after", "9"}), abort_options, error))
      << error;
  EXPECT_EQ(abort_options.abort_after, 9U);
  EXPECT_FALSE(parse(with({"--stop-after", "5"}), options, error));  // no notify-fd
  EXPECT_FALSE(parse(with({"--notify-fd", "3"}), options, error));   // no count
  EXPECT_FALSE(
      parse(with({"--notify-fd", "3", "--stop-after", "5", "--abort-after", "6"}), options, error));
  EXPECT_FALSE(parse(with({"--notify-fd", "3", "--stop-after", "0"}), options, error));
  EXPECT_FALSE(parse(with({"--notify-fd", "-1", "--stop-after", "1"}), options, error));
}

}  // namespace
