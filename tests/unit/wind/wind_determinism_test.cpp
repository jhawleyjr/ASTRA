// M2 PR 2's determinism harness (PLAN.md, docs/architecture.md §2): two independently constructed
// Simulations from the same config, stepped the same number of frames, must agree bitwise on the
// per-frame sampled wind and on every registered signal -- compared as std::bit_cast<uint64_t>,
// never EXPECT_DOUBLE_EQ, which admits ULP slack that would hide a real nondeterminism bug.
//
// Parameterized over two WindModels (PLAN.md M2 PR 2b: "Reuses PR 2's bitwise determinism harness
// ... against dryden, where a mismatch is diagnostic of a real bug rather than trivially passing"):
//
//   Uniform -- a constant, stateless model. This result is near-trivial (see PLAN.md's PR 2 line):
//   it is plumbing coverage for the latch and the registry snapshot, not evidence that seeded
//   stochastic evolution is itself deterministic.
//
//   Dryden -- a stateful model whose gust sequence depends on an Rng seeded from config and a
//   shaping filter carried frame to frame. Here a bitwise mismatch is a real finding: it would mean
//   either the Rng itself, or the filter's own state, diverges between two runs built from
//   identical config -- exactly the claim PLAN.md's PR 2b line makes ("two same-seed runs bitwise
//   identical on the gust sequence and the registry snapshot").
//
// Written in M2, before the M5 logger existed (ADR-0005), and docs/architecture.md §16 puts
// "bit-identical log across two runs" under M4, not M2 -- this was the honest M2 substitute: the
// SignalRegistry is exactly what the M5 logger samples (signal_registry.hpp's own header comment),
// so a bitwise registry snapshot needs no log and no golden file.
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::Simulation;

struct WindFixture {
  std::string name;
  std::string yaml;
};

std::ostream& operator<<(std::ostream& os, const WindFixture& fixture) {
  return os << fixture.name;
}

Simulation load(const WindFixture& fixture) {
  return Simulation::from_config(Config::from_string(fixture.yaml, "determinism-fixture"),
                                 ASTRA_TEST_PLUGIN_DIR);
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

class WindDeterminism : public ::testing::TestWithParam<WindFixture> {};

TEST_P(WindDeterminism, TwoRunsRecordTheSameWindNSequenceBitwise) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load(GetParam());
  Simulation sim_b = load(GetParam());

  const double* north_a = sim_a.registry().find("wind.north");
  const double* east_a = sim_a.registry().find("wind.east");
  const double* down_a = sim_a.registry().find("wind.down");
  const double* north_b = sim_b.registry().find("wind.north");
  const double* east_b = sim_b.registry().find("wind.east");
  const double* down_b = sim_b.registry().find("wind.down");
  ASSERT_NE(north_a, nullptr);
  ASSERT_NE(north_b, nullptr);

  for (std::uint64_t frame = 0; frame < kFrames; ++frame) {
    EXPECT_FALSE(sim_a.step());
    EXPECT_FALSE(sim_b.step());
    EXPECT_EQ(bits(*north_a), bits(*north_b)) << "frame " << frame;
    EXPECT_EQ(bits(*east_a), bits(*east_b)) << "frame " << frame;
    EXPECT_EQ(bits(*down_a), bits(*down_b)) << "frame " << frame;
  }
}

TEST_P(WindDeterminism, TwoRunsAreBitIdenticalOnEveryRegisteredSignal) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load(GetParam());
  Simulation sim_b = load(GetParam());
  EXPECT_FALSE(sim_a.run(kFrames));
  EXPECT_FALSE(sim_b.run(kFrames));

  const std::span<const astra::Signal> signals_a = sim_a.registry().signals();
  const std::span<const astra::Signal> signals_b = sim_b.registry().signals();
  ASSERT_EQ(signals_a.size(), signals_b.size());
  for (std::size_t i = 0; i < signals_a.size(); ++i) {
    ASSERT_EQ(signals_a[i].name, signals_b[i].name);
    EXPECT_EQ(bits(*signals_a[i].value), bits(*signals_b[i].value)) << signals_a[i].name;
  }
}

TEST_P(WindDeterminism, TwoRunsAreBitIdenticalOnTheFinalStateVector) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load(GetParam());
  Simulation sim_b = load(GetParam());
  EXPECT_FALSE(sim_a.run(kFrames));
  EXPECT_FALSE(sim_b.run(kFrames));

  const astra::StateVector& ya = sim_a.vehicles().front().state();
  const astra::StateVector& yb = sim_b.vehicles().front().state();
  for (int i = 0; i < astra::kStateSize; ++i) {
    EXPECT_EQ(bits(ya(i)), bits(yb(i))) << "state index " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(
    WindModels, WindDeterminism,
    ::testing::Values(
        WindFixture{
            "Uniform",
            "sim: { dt: 0.001, frames: 0 }\n"
            "vehicle: { models: [] }\n"
            "physical:\n"
            "  earth: plugins/physical/earth/flat\n"
            "  gravity: plugins/physical/gravity/constant\n"
            "  wind: { plugin: plugins/physical/wind/uniform, north: 5.0, east: -2.0, down: 0.5 }\n"
            "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
            "izz: 1.0 }\n"
            "initial: { state: { v_b: [50.0, 0.0, 0.0] } }\n"},
        WindFixture{
            "Dryden",
            "sim: { dt: 0.001, frames: 0 }\n"
            "vehicle: { models: [] }\n"
            "physical:\n"
            "  earth: plugins/physical/earth/flat\n"
            "  gravity: plugins/physical/gravity/constant\n"
            "  wind: { plugin: plugins/physical/wind/dryden, seed: 7, w20_knots: 15.0, "
            "sigma_high: 3.0 }\n"
            "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
            "izz: 1.0 }\n"
            // p_n's down component is -100 -> earth/flat's altitude() (-p_n(2)) is 100 m; v_b
            // nonzero so dryden's rate = V/L is bounded away from the kMinRate floor, exercising
            // the plugin's ordinary (not edge-case) path.
            "initial: { state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] } }\n"}),
    [](const ::testing::TestParamInfo<WindFixture>& info) { return info.param.name; });

}  // namespace
