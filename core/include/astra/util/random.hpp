// core/util/ owns the single seeded-RNG type every stochastic plugin uses (AGENTS.md Randomness:
// "std::mt19937_64 with in-house Box-Muller/uniform transforms ... the standard distributions are
// not portable across libstdc++/libc++"). std::uniform_real_distribution and
// std::normal_distribution are NOT used here: the standard leaves each distribution's exact
// algorithm (how many engine draws it consumes, in what order) implementation-defined, so the same
// seed can produce a different sequence under libstdc++ than under libc++ -- fatal to
// docs/architecture.md §2's "same configuration and inputs produce byte-identical logs" the moment
// a toolchain changes. Every transform below is written out so the sequence is pinned by this file
// alone.
//
// Per-frame path (§14): every member function is noexcept and allocates nothing --
// std::mt19937_64's state is a fixed-size in-object array, and none of the transforms below touch
// the heap.
#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

namespace astra::random {

// NOLINTBEGIN(readability-magic-numbers): 11/53/2.0/-2.0 below are the fixed constants of the
// 53-bit-uniform and Box-Muller constructions themselves, not arbitrary tunables.

class Rng {
 public:
  explicit Rng(std::uint64_t seed) noexcept : engine_(seed), seed_(seed) {}

  // reset()'s contract for every stochastic plugin: reseed(seed()) (or any seed) must reproduce
  // exactly the sequence a freshly constructed Rng(seed) would produce, including when a Box-Muller
  // second variate was cached and unconsumed -- so this clears the cache, not just the engine.
  void reseed(std::uint64_t seed) noexcept {
    engine_.seed(seed);
    seed_ = seed;
    has_cached_normal_ = false;
  }

  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

  // Uniform in [0, 1): the top 53 bits of a 64-bit draw, scaled by 2^-53 (exact in double). The
  // portable construction -- std::uniform_real_distribution's own bit consumption is
  // implementation-defined (see the file header). Never returns 1.0: the largest possible mantissa,
  // 2^53 - 1, times 2^-53 is strictly less than 1.0.
  [[nodiscard]] double uniform01() noexcept {
    constexpr double kTwoToMinus53 = 0x1.0p-53;
    constexpr int kDiscardBits = 64 - 53;
    return static_cast<double>(engine_() >> kDiscardBits) * kTwoToMinus53;
  }

  [[nodiscard]] double uniform(double low, double high) noexcept {
    return low + (high - low) * uniform01();
  }

  // Standard normal via Box-Muller, generating two independent variates per pair of uniform draws
  // and caching the second (sin) one for the following call -- so two consecutive normal() calls
  // consume exactly two uniform01() draws between them, deterministically. u1 is built as
  // 1.0 - uniform01() rather than uniform01() itself so it lies in (0, 1], never 0 -- uniform01()
  // can return exactly 0.0, which would make std::log(u1) -Inf.
  [[nodiscard]] double normal() noexcept {
    if (has_cached_normal_) {
      has_cached_normal_ = false;
      return cached_normal_;
    }
    const double u1 = 1.0 - uniform01();
    const double u2 = uniform01();
    const double r = std::sqrt(-2.0 * std::log(u1));
    const double theta = 2.0 * std::numbers::pi * u2;
    cached_normal_ = r * std::sin(theta);
    has_cached_normal_ = true;
    return r * std::cos(theta);
  }

 private:
  std::mt19937_64 engine_;
  std::uint64_t seed_;
  double cached_normal_ = 0.0;
  bool has_cached_normal_ = false;
};

// NOLINTEND(readability-magic-numbers)

}  // namespace astra::random
