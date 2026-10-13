// NASA/TM-2015-218675 atmospheric check-cases 11 and 12, the F-16 trimmed and flown open loop for
// 180 s (M7 PR 9, docs/plans/m7-pr9-open-loop.md; the M7 plan's R4 F-16 half, R5, Q3's and Q6's
// decided choices, Q7 with amendment 2). The configs are aircraft/f16/configs/case11_*.yaml and
// case12_*.yaml, trimmed by astra trim under n_frame (docs/reviews/evidence/m7-pr9-trim/).
//
// NescF16.Case1{1,2}OpenLoop (R5) fly each config against ADR-0028's band through f16_flight.hpp:
// every one of the 21 channels is asserted at every sample from Band::kFirstGatedSample on, and the
// samples before it are logged, never asserted. No channel is excluded: ADR-0028 item 2's footnotes
// exclude none in these cases (the plan's D4). A zero-accepting channel is asserted too, but the
// log marks it, and it is never cited as evidence.
//
// NescF16.Case1{1,2}TrimResiduals (R4; the plan's Q1 and Q3). Simulation::derivative at each
// config's initial state, with its controls latched:
// - the three solved residuals (u_dot, w_dot, q_dot) are at most 1e-6 (M3's D8);
// - the three lateral residuals (v_dot_y, p_dot, r_dot) equal Q7's full expected residual within
//   the plan's pre-registered absolute bounds. The expected values were derived from the DML alone,
//   by docs/reviews/evidence/m7-pr9-residual/derive_residual.py, at the config's own literals. Each
//   bound is 1e-12 times |g| for v_dot_y, and 1e-12 times the largest-magnitude part for p_dot and
//   r_dot (Joe's amendment 2);
// - the latched controls.throttle lies in [0, 1], since the engine extrapolates outside it (PR 6b's
//   decision 4; the plan's Q3).
// A miss in any of these goes to Joe; no bound here is widened to clear one.
#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "f16_flight.hpp"
#include "golden/nesc/case11_trim_check_subsonic_f16_band.hpp"
#include "golden/nesc/case12_trim_check_supersonic_f16_band.hpp"

namespace {

using astra::Config;
using astra::Simulation;
namespace nesc = astra::test::nesc;
namespace f16 = astra::test::nesc::f16;

std::string config_path(std::string_view file_name) {
  return std::string{ASTRA_AIRCRAFT_DIR} + "/f16/configs/" + std::string{file_name};
}

// One lateral residual's expected value and its pre-registered absolute bound (the plan's Q1).
struct Lateral {
  std::string_view name;
  int index;  // into Simulation::derivative's output
  double expected;
  double bound;
};

struct TrimExpectation {
  std::string_view name;
  std::string_view file;
  Lateral v_dot_y;
  Lateral p_dot;
  Lateral r_dot;
};

constexpr int kVDotX = astra::state::kVelocity;
constexpr int kVDotY = astra::state::kVelocity + 1;
constexpr int kVDotZ = astra::state::kVelocity + 2;
constexpr int kPDot = astra::state::kAngularRate;
constexpr int kQDot = astra::state::kAngularRate + 1;
constexpr int kRDot = astra::state::kAngularRate + 2;

// M3's D8 (the M7 plan's Q7): the solved residuals' bound, m/s^2 and rad/s^2.
constexpr double kSolvedBound = 1e-6;

// derive_residual.log's totals at the config's literals (%.17g) and the plan's bounds.
constexpr TrimExpectation kCase11{
    "case11",
    "case11_trim_check_subsonic.yaml",
    {"v_dot_y", kVDotY, 0.017171068467368312, 9.7888e-12},
    {"p_dot", kPDot, -9.8888285577672511e-06, 9.8858e-18},
    {"r_dot", kRDot, 5.9976661226860661e-06, 5.9955e-18},
};
constexpr TrimExpectation kCase12{
    "case12",
    "case12_trim_check_supersonic.yaml",
    {"v_dot_y", kVDotY, 0.082114102137769332, 9.7701e-12},
    {"p_dot", kPDot, -2.2396879325036955e-05, 2.2387e-17},
    {"r_dot", kRDot, 3.7706082846373026e-05, 3.7702e-17},
};

void expect_trim_residuals(const TrimExpectation& c) {
  const Config config = Config::from_file(config_path(c.file));
  Simulation sim = Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  const astra::StateVector state = sim.vehicles().front().state();
  const std::vector<double> derivative =
      sim.derivative(std::span<const double>(state.data(), static_cast<std::size_t>(state.size())));
  ASSERT_EQ(derivative.size(), static_cast<std::size_t>(state.size()));

  int visited = 0;
  for (const int index : {kVDotX, kVDotZ, kQDot}) {
    const double value = derivative.at(static_cast<std::size_t>(index));
    EXPECT_LE(std::abs(value), kSolvedBound) << c.name << ": solved residual at index " << index;
    std::cout << std::format("{} solved residual [{}] {:.6g}\n", c.name, index, value);
    ++visited;
  }
  for (const Lateral& lateral : {c.v_dot_y, c.p_dot, c.r_dot}) {
    const double value = derivative.at(static_cast<std::size_t>(lateral.index));
    const double miss = std::abs(value - lateral.expected);
    EXPECT_LE(miss, lateral.bound)
        << c.name << " " << lateral.name << ": " << std::format("{:.17g}", value)
        << " against the DML's " << std::format("{:.17g}", lateral.expected);
    std::cout << std::format("{} {} {:.17g}, expected {:.17g}, miss {:.3g}, bound {:.5g}\n", c.name,
                             lateral.name, value, lateral.expected, miss, lateral.bound);
    ++visited;
  }
  EXPECT_EQ(visited, 6) << c.name;

  const double* throttle = sim.registry().find("v0.controls.throttle");
  ASSERT_NE(throttle, nullptr) << c.name;
  EXPECT_GE(*throttle, 0.0) << c.name << ": the trim throttle leaves the engine's 0..1";
  EXPECT_LE(*throttle, 1.0) << c.name << ": the trim throttle leaves the engine's 0..1";
  std::cout << std::format("{} controls.throttle {:.17g}\n", c.name, *throttle);
}

template <typename Band>
void fly_open_loop(std::string_view name, std::string_view file, const Band& band) {
  using B = std::remove_cvref_t<Band>;
  f16::Flight flight{Config::from_file(config_path(file))};
  const f16::FlightResult result = flight.fly(name, band);
  f16::print_summary(name, band, result);
  EXPECT_EQ(result.samples_compared, B::kSampleCount) << name;
  // 100 (N - 1) + 1 frames: the configs' sim.frames does not bound run(n) (f16_flight.hpp).
  EXPECT_EQ(result.frames, (f16::kFramesPerSample * (B::kSampleCount - 1)) + 1) << name;
}

}  // namespace

TEST(NescF16, Case11TrimResiduals) { expect_trim_residuals(kCase11); }

TEST(NescF16, Case12TrimResiduals) { expect_trim_residuals(kCase12); }

TEST(NescF16, Case11OpenLoop) {
  fly_open_loop("case11", "case11_trim_check_subsonic.yaml", nesc::case11::kBand);
}

TEST(NescF16, Case12OpenLoop) {
  fly_open_loop("case12", "case12_trim_check_supersonic.yaml", nesc::case12::kBand);
}
