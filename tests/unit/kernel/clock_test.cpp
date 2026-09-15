// VirtualClock (core/kernel/virtual_clock.hpp, docs/architecture.md §5, ADR-0010): t() is exactly
// frame * dt, computed on every call and never accumulated, across 1000 frames (PLAN.md M1 PR 2's
// acceptance in miniature).
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "astra/errors.hpp"
#include "astra/kernel/virtual_clock.hpp"

namespace {

using astra::Error;
using astra::VirtualClock;

TEST(VirtualClock, StartsAtFrameZeroAndTimeZero) {
  const VirtualClock clock(1e-3);
  EXPECT_EQ(clock.frame(), 0U);
  EXPECT_EQ(clock.t(), 0.0);
  EXPECT_EQ(clock.dt(), 1e-3);
}

TEST(VirtualClock, TIsExactlyFrameTimesDtOverOneThousandFrames) {
  VirtualClock clock(1e-3);
  for (std::uint64_t frame = 0; frame < 1000; ++frame) {
    EXPECT_EQ(clock.frame(), frame);
    EXPECT_EQ(clock.t(), static_cast<double>(frame) * 1e-3) << "frame " << frame;
    clock.advance();
  }
  EXPECT_EQ(clock.frame(), 1000U);
}

TEST(VirtualClock, ContextCarriesTDtAndFrameTogether) {
  VirtualClock clock(2e-3);
  clock.advance();
  clock.advance();
  const astra::StepContext ctx = clock.context();
  EXPECT_EQ(ctx.frame, 2U);
  EXPECT_EQ(ctx.dt, 2e-3);
  EXPECT_EQ(ctx.t, 4e-3);
}

TEST(VirtualClock, ResetReturnsToFrameZeroWithoutChangingDt) {
  VirtualClock clock(1e-3);
  clock.advance();
  clock.advance();
  clock.reset();
  EXPECT_EQ(clock.frame(), 0U);
  EXPECT_EQ(clock.dt(), 1e-3);
}

TEST(VirtualClock, RejectsNonFiniteOrNonPositiveDt) {
  EXPECT_THROW(VirtualClock{0.0}, Error);
  EXPECT_THROW(VirtualClock{-1e-3}, Error);
  EXPECT_THROW(VirtualClock{std::numeric_limits<double>::quiet_NaN()}, Error);
  EXPECT_THROW(VirtualClock{std::numeric_limits<double>::infinity()}, Error);
}

TEST(VirtualClock, DefaultsToTheOneKilohertzMajorFrame) {
  const VirtualClock clock;
  EXPECT_EQ(clock.dt(), astra::kDefaultDt);
}

}  // namespace
