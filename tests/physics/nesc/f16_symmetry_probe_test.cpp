// M7b PR 4's parity probe (docs/plans/m7b-pr4-f16-symmetry.md, section 6; R10): glibc's libm,
// called as the flights call it, is exactly odd or even where oracle (c)'s bitwise mirror needs it,
// in every preset's build. Under the meridian mirror the only libm call on the bitwise path with an
// odd argument is atan2's y (psi, phi and beta: frames.hpp:82, air_data.hpp:43). asin, hypot, sin
// and cos are probed as the M7b plan's Assumptions list them, and remainder (double and long
// double) as the heading relation's wrap uses it (section 9).
//
// Each comparison is bitwise, through std::bit_cast (long double: equal value and equal sign bit,
// since its padding bytes are indeterminate), so the sign of zero counts. Arguments are seeded by
// astra::random::Rng, the in-house uniform over std::mt19937_64 (AGENTS.md), with seeds 1, 2 and 3,
// one per range, and 2^18 draws per range; each range ends with its edges. A failure is an item
// reclassified to roundoff, and it stops the PR before any flight (section 6).
//
// The trim checks (section 5; C-01, amendment 5) sit beside it, as facts about the model before any
// flight: the trim's own n_frame p and r within C-01's bound, and the config route's frame-0
// quaternion.
//
// Physics label (section 12): the stop hook's `just test-unit` never runs these; section 16, step 5
// does. The seeds, counts, ranges and edges below are section 6's pre-registered probe set.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/random.hpp"
#include "f16_symmetry.hpp"

namespace {

namespace sym = astra::test::nesc::f16_symmetry;

constexpr int kDraws = 1 << 18;  // section 6: 2^18 draws per range
constexpr double kMax = std::numeric_limits<double>::max();
constexpr double kMinSubnormal = std::numeric_limits<double>::denorm_min();  // 2^-1074
constexpr double kMinNormal = std::numeric_limits<double>::min();            // 2^-1022
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::uint64_t bits(double x) { return std::bit_cast<std::uint64_t>(x); }

std::string hex(double x) { return std::format("{:a}", x); }

// A magnitude log-uniform over [2^lo, 2^hi), with a random sign when signed is true.
double log_uniform(astra::random::Rng& rng, int lo, int hi, bool with_sign) {
  const int exponent = lo + static_cast<int>(rng.uniform01() * (hi - lo));
  const double magnitude = std::ldexp(1.0 + rng.uniform01(), exponent);
  return with_sign && rng.uniform01() < 0.5 ? -magnitude : magnitude;
}

struct Tally {
  long checked = 0;
  long failed = 0;
  std::string first;
};

void odd(Tally& t, const std::function<double(double)>& f, double x, std::string_view name) {
  const double fx = f(x);
  const double fmx = f(-x);
  if (std::isnan(fx) || std::isnan(fmx)) {
    return;  // section 6: non-finite results are left out
  }
  ++t.checked;
  if (bits(fmx) != bits(-fx)) {
    if (t.failed++ == 0) {
      t.first = std::format("{}({}) = {} but {}(-x) = {}", name, hex(x), hex(fx), name, hex(fmx));
    }
  }
}

void even(Tally& t, const std::function<double(double)>& f, double x, std::string_view name) {
  const double fx = f(x);
  const double fmx = f(-x);
  if (std::isnan(fx) || std::isnan(fmx)) {
    return;
  }
  ++t.checked;
  if (bits(fmx) != bits(fx)) {
    if (t.failed++ == 0) {
      t.first = std::format("{}({}) = {} but {}(-x) = {}", name, hex(x), hex(fx), name, hex(fmx));
    }
  }
}

void report(std::string_view name, const Tally& t) {
  std::cout << std::format("F16SymmetryParity {}: {} comparisons, {} failed{}{}\n", name, t.checked,
                           t.failed, t.failed > 0 ? "; first: " : "", t.first);
  EXPECT_EQ(t.failed, 0) << name << ": " << t.first;
  EXPECT_GT(t.checked, 0) << name << ": no comparison ran";
}

const std::array<double, 12> kEdges = {0.0,        -0.0,        kMinSubnormal, -kMinSubnormal,
                                       kMinNormal, -kMinNormal, 1.0,           -1.0,
                                       kMax,       -kMax,       kInf,          -kInf};

TEST(F16SymmetryParity, Atan2IsOddInY) {
  Tally t;
  const auto in_y = [](double x) { return [x](double y) { return std::atan2(y, x); }; };
  astra::random::Rng psi_phi(1);  // C12 over C11, C23 over C33: (U[-1, 1], U(0, 1])
  for (int i = 0; i < kDraws; ++i) {
    const double y = psi_phi.uniform(-1.0, 1.0);
    const double x = 1.0 - psi_phi.uniform01();
    odd(t, in_y(x), y, "atan2");
  }
  astra::random::Rng beta(2);  // v over hypot(u, w): (U[-60, 60], U[100, 300]) m/s
  for (int i = 0; i < kDraws; ++i) {
    const double y = beta.uniform(-60.0, 60.0);
    const double x = beta.uniform(100.0, 300.0);
    odd(t, in_y(x), y, "atan2");
  }
  astra::random::Rng wide(3);  // both signs, log-uniform over [2^-1074, 2^10) for each argument
  for (int i = 0; i < kDraws; ++i) {
    const double y = log_uniform(wide, -1074, 10, true);
    const double x = log_uniform(wide, -1074, 10, true);
    odd(t, in_y(x), y, "atan2");
  }
  for (const double y : kEdges) {
    for (const double x : kEdges) {
      odd(t, in_y(x), y, "atan2");
    }
  }
  report("atan2 odd in y", t);
}

TEST(F16SymmetryParity, AsinIsOdd) {
  Tally t;
  const auto f = [](double x) { return std::asin(x); };
  astra::random::Rng flights(1);
  for (int i = 0; i < kDraws; ++i) {
    odd(t, f, flights.uniform(-1.0, 1.0), "asin");
  }
  astra::random::Rng wide(2);
  for (int i = 0; i < kDraws; ++i) {
    odd(t, f, log_uniform(wide, -1074, 0, true), "asin");
  }
  for (const double x : {0.0, -0.0, kMinSubnormal, -kMinSubnormal, 1.0, -1.0}) {
    odd(t, f, x, "asin");
  }
  report("asin odd", t);
}

TEST(F16SymmetryParity, SinIsOddAndCosIsEven) {
  Tally s;
  Tally c;
  const auto sine = [](double x) { return std::sin(x); };
  const auto cosine = [](double x) { return std::cos(x); };
  astra::random::Rng flights(1);
  for (int i = 0; i < kDraws; ++i) {
    const double x = flights.uniform(-kTwoPi, kTwoPi);
    odd(s, sine, x, "sin");
    even(c, cosine, x, "cos");
  }
  astra::random::Rng wide(2);
  for (int i = 0; i < kDraws; ++i) {
    const double x = log_uniform(wide, -1074, 20, true);
    odd(s, sine, x, "sin");
    even(c, cosine, x, "cos");
  }
  for (const double x : {0.0, -0.0, kMinSubnormal, -kMinSubnormal, kMax, -kMax}) {
    odd(s, sine, x, "sin");
    even(c, cosine, x, "cos");
  }
  report("sin odd", s);
  report("cos even", c);
}

TEST(F16SymmetryParity, HypotIsEvenInEachArgument) {
  Tally t;
  const auto first = [](double y) { return [y](double x) { return std::hypot(x, y); }; };
  const auto second = [](double x) { return [x](double y) { return std::hypot(x, y); }; };
  astra::random::Rng flights(1);  // hypot(u, w): (U[100, 300], U[-60, 60]) m/s
  for (int i = 0; i < kDraws; ++i) {
    const double x = flights.uniform(100.0, 300.0);
    const double y = flights.uniform(-60.0, 60.0);
    even(t, first(y), x, "hypot");
    even(t, second(x), y, "hypot");
  }
  astra::random::Rng wide(2);
  for (int i = 0; i < kDraws; ++i) {
    const double x = log_uniform(wide, -1074, 10, true);
    const double y = log_uniform(wide, -1074, 10, true);
    even(t, first(y), x, "hypot");
    even(t, second(x), y, "hypot");
  }
  for (const double x : kEdges) {
    for (const double y : kEdges) {
      even(t, first(y), x, "hypot");
      even(t, second(x), y, "hypot");
    }
  }
  report("hypot even in each argument", t);
}

TEST(F16SymmetryParity, RemainderIsOddInXAndEvenInY) {
  Tally t;
  const auto in_x = [](double y) { return [y](double x) { return std::remainder(x, y); }; };
  const auto in_y = [](double x) { return [x](double y) { return std::remainder(x, y); }; };
  astra::random::Rng flights(1);
  for (int i = 0; i < kDraws; ++i) {
    const double x = flights.uniform(-20.0, 20.0);
    odd(t, in_x(kTwoPi), x, "remainder");
    even(t, in_y(x), kTwoPi, "remainder");
  }
  astra::random::Rng wide(2);
  for (int i = 0; i < kDraws; ++i) {
    const double x = log_uniform(wide, -1074, 10, true);
    const double y = log_uniform(wide, -1074, 10, true);
    odd(t, in_x(y), x, "remainder");
    even(t, in_y(x), y, "remainder");
  }
  for (const double x :
       {0.0, -0.0, kMinSubnormal, -kMinSubnormal, std::numbers::pi, kTwoPi, 20.0}) {
    odd(t, in_x(kTwoPi), x, "remainder");
  }
  report("remainder odd in x, even in y", t);

  // remainderl, as the heading relation calls it: equal value and equal sign bit.
  static_assert(std::numeric_limits<long double>::digits == 64,
                "section 9: x87 extended precision");
  long checked = 0;
  long failed = 0;
  const long double two_pi = 2.0L * std::numbers::pi_v<long double>;
  astra::random::Rng flights_l(3);
  for (int i = 0; i < kDraws; ++i) {
    const auto x = static_cast<long double>(flights_l.uniform(-20.0, 20.0));
    const long double a = std::remainder(x, two_pi);
    const long double b = std::remainder(-x, two_pi);
    const long double c = std::remainder(x, -two_pi);
    const bool odd_in_x = b == -a && std::signbit(b) != std::signbit(a);
    const bool even_in_y = c == a && std::signbit(c) == std::signbit(a);
    ++checked;
    if (!odd_in_x || !even_in_y) {
      ++failed;
    }
  }
  std::cout << std::format(
      "F16SymmetryParity remainderl odd in x, even in y: {} comparisons, {} failed\n", checked,
      failed);
  EXPECT_EQ(failed, 0);
}

// Section 5, C-01: the trim's own n_frame p and r lie within B = c u kappa |omega| of 0 before the
// config writes 0.0 for them; c = 64, u = 2^-53, and kappa_2(J_c) and |omega| from the trim's own
// solve (docs/reviews/evidence/m7b-pr4-trim/trim_check.log).
TEST(F16SymmetryTrim, TheTrimsLateralRatesAreRoundoff) {
  constexpr double kC = 64.0;
  constexpr double kU = 0x1.0p-53;
  const double bound = kC * kU * sym::kTrimKappa * sym::kTrimOmegaNorm;
  std::cout << std::format("F16SymmetryTrim: p {:.17g}, r {:.17g}, B {:.17g} rad/s\n", sym::kTrimP,
                           sym::kTrimR, bound);
  EXPECT_LE(std::abs(sym::kTrimP), bound);
  EXPECT_LE(std::abs(sym::kTrimR), bound);
}

// Section 9, amendment 5: the config route's frame-0 q_nb, from euler_nb [0, theta, 0], is
// (cos(theta/2), +0, sin(theta/2), +0) bit for bit, so the heading base run can write it through
// Vehicle::state() with no route difference entering the yardstick.
TEST(F16SymmetryTrim, TheConfigRoutesQuaternionIsTheTrims) {
  const astra::Config config =
      astra::Config::from_string(sym::round_earth_text(), "f16_symmetry round_earth_text");
  const astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  const astra::StateVector state = sim.vehicles().front().state();
  const Eigen::Vector4d q = state.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
  const std::array<double, 4> expected = {std::cos(sym::kTheta / 2.0), 0.0,
                                          std::sin(sym::kTheta / 2.0), 0.0};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(bits(q(i)), bits(expected.at(static_cast<std::size_t>(i))))
        << "q_nb[" << i << "] " << hex(q(i)) << " against "
        << hex(expected.at(static_cast<std::size_t>(i)));
  }
}

}  // namespace
