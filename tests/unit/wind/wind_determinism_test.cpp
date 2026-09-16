// M2 PR 2's determinism harness (PLAN.md, docs/architecture.md §2): two independently constructed
// Simulations from the same config, stepped the same number of frames, must agree bitwise on the
// per-frame sampled wind and on every registered signal -- compared as std::bit_cast<uint64_t>,
// never EXPECT_DOUBLE_EQ, which admits ULP slack that would hide a real nondeterminism bug.
//
// With only wind/uniform (a constant, stateless model) this result is near-trivial: see PLAN.md's
// PR 2 line for why. It is plumbing coverage for the latch and the registry snapshot, not evidence
// that seeded stochastic evolution is itself deterministic -- that claim belongs to PR 2b, which
// reuses this exact harness against plugins/physical/wind/dryden, where a bitwise mismatch would
// be diagnostic of a real RNG/filter bug rather than trivially passing.
//
// There is no logger until M5 (ADR-0005), and docs/architecture.md §16 puts "bit-identical log
// across two runs" under M4, not M2 -- this is the honest M2 substitute: the SignalRegistry is
// exactly what the M5 logger will sample (signal_registry.hpp's own header comment), so a bitwise
// registry snapshot is the closest analogue that compiles today, with no log and no golden file.
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::Simulation;

Config wind_config() {
  return Config::from_string(
      "sim: { dt: 0.001, frames: 0 }\n"
      "vehicle: { models: [] }\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: { plugin: plugins/physical/wind/uniform, north: 5.0, east: -2.0, down: 0.5 }\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 "
      "}\n"
      "initial: { state: { v_b: [50.0, 0.0, 0.0] } }\n",
      "determinism-fixture");
}

Simulation load() { return Simulation::from_config(wind_config(), ASTRA_TEST_PLUGIN_DIR); }

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

TEST(WindDeterminism, TwoRunsRecordTheSameWindNSequenceBitwise) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load();
  Simulation sim_b = load();

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

TEST(WindDeterminism, TwoRunsAreBitIdenticalOnEveryRegisteredSignal) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load();
  Simulation sim_b = load();
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

TEST(WindDeterminism, TwoRunsAreBitIdenticalOnTheFinalStateVector) {
  constexpr std::uint64_t kFrames = 50;
  Simulation sim_a = load();
  Simulation sim_b = load();
  EXPECT_FALSE(sim_a.run(kFrames));
  EXPECT_FALSE(sim_b.run(kFrames));

  const astra::StateVector& ya = sim_a.vehicles().front().state();
  const astra::StateVector& yb = sim_b.vehicles().front().state();
  for (int i = 0; i < astra::kStateSize; ++i) {
    EXPECT_EQ(bits(ya(i)), bits(yb(i))) << "state index " << i;
  }
}

}  // namespace
