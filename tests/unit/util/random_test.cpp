// core/util/random.hpp (AGENTS.md Randomness: std::mt19937_64 + in-house Box-Muller/uniform
// transforms, not the standard distributions -- see the header's own file comment for why).
#include "astra/util/random.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace {

using astra::random::Rng;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

TEST(Random, SameSeedProducesBitwiseIdenticalSequences) {
  Rng a{42};
  Rng b{42};
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(bits(a.uniform01()), bits(b.uniform01())) << "draw " << i;
  }
}

TEST(Random, DifferentSeedsProduceDifferentSequences) {
  Rng a{1};
  Rng b{2};
  bool any_different = false;
  for (int i = 0; i < 16; ++i) {
    if (bits(a.uniform01()) != bits(b.uniform01())) {
      any_different = true;
    }
  }
  EXPECT_TRUE(any_different);
}

TEST(Random, ReseedAfterPartialConsumptionMatchesAFreshRngOfTheSameSeed) {
  Rng consumed{7};
  for (int i = 0; i < 37; ++i) {
    static_cast<void>(consumed.uniform01());
  }
  consumed.reseed(7);

  Rng fresh{7};
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(bits(consumed.uniform01()), bits(fresh.uniform01())) << "draw " << i;
  }
}

// reseed() must also clear a Box-Muller cached second variate -- otherwise a reseed right after an
// ODD number of normal() calls would return the OLD seed's cached value on its first post-reseed
// normal() call, breaking the "reseed reproduces a fresh Rng" contract every stochastic plugin's
// reset() depends on.
TEST(Random, ReseedClearsAnOutstandingCachedNormal) {
  Rng consumed{99};
  static_cast<void>(consumed.normal());  // caches the paired (sin) variate, consumes 2 uniform01()s
  consumed.reseed(99);

  Rng fresh{99};
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(bits(consumed.normal()), bits(fresh.normal())) << "draw " << i;
  }
}

TEST(Random, Uniform01StaysInHalfOpenUnitInterval) {
  Rng rng{123};
  constexpr int kDraws = 1'000'000;
  for (int i = 0; i < kDraws; ++i) {
    const double u = rng.uniform01();
    EXPECT_GE(u, 0.0);
    EXPECT_LT(u, 1.0);
  }
}

TEST(Random, NormalDrawsAreAlwaysFinite) {
  Rng rng{321};
  constexpr int kDraws = 1'000'000;
  for (int i = 0; i < kDraws; ++i) {
    EXPECT_TRUE(std::isfinite(rng.normal()));
  }
}

// Statistical bounds, not exact values: at 1e6 draws the sample mean/variance of a true N(0,1)
// source has standard error ~1/sqrt(1e6) = 1e-3 for the mean and a comparable order for the
// variance, so bounds several multiples of that (not tuned to the observed numbers) catch a real
// implementation bug (wrong scale, wrong sign inside the log/sqrt) without flaking on sampling
// noise. The 1-sigma fraction (theoretically 0.6827) gets the same treatment.
TEST(Random, NormalDrawsMatchStandardNormalStatistics) {
  Rng rng{555};
  constexpr int kDraws = 1'000'000;
  double sum = 0.0;
  double sum_sq = 0.0;
  int within_one_sigma = 0;
  for (int i = 0; i < kDraws; ++i) {
    const double x = rng.normal();
    sum += x;
    sum_sq += x * x;
    if (std::abs(x) <= 1.0) {
      ++within_one_sigma;
    }
  }
  const double mean = sum / kDraws;
  const double variance = (sum_sq / kDraws) - (mean * mean);
  const double fraction_within_one_sigma = static_cast<double>(within_one_sigma) / kDraws;

  EXPECT_NEAR(0.0, mean, 0.01);
  EXPECT_NEAR(1.0, variance, 0.02);
  EXPECT_NEAR(0.6827, fraction_within_one_sigma, 0.01);
}

TEST(Random, SeedAccessorReportsTheConstructedSeed) {
  Rng rng{123456789};
  EXPECT_EQ(123456789U, rng.seed());
  rng.reseed(42);
  EXPECT_EQ(42U, rng.seed());
}

}  // namespace
