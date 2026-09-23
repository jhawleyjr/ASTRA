// The one definition of M3 PR 8's frozen cruise regression run (D20, docs/plans/
// pr8-cruise-regression.md): aircraft/notional_transport/configs/cruise.yaml through
// Simulation::from_config, the 13-double rigid-body state sampled at frame 0 and then after each of
// 300 run(1000) calls -- 1 Hz over 300 s at dt = 1 ms, 301 samples -- as std::bit_cast bit
// patterns. Shared by tests/capture/capture_cruise_regression.cpp (which freezes it) and
// tests/unit/kernel/notional_transport_cruise_regression_test.cpp (which compares against it), so
// the two cannot drift. The config's own "sim.frames" is not used: run(N) bounds the run here.
//
// cruise.yaml keeps its Dryden wind (seed 42): the run starts from the still-air trim and flies
// into turbulence from frame 0 -- a turbulence response from trim, not a cruise hold.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"

namespace astra::test::cruise_regression {

inline constexpr std::size_t kSamples = 301;
inline constexpr std::uint64_t kFramesPerSample = 1000;
using Sample = std::array<std::uint64_t, kStateSize>;
using Trajectory = std::array<Sample, kSamples>;

struct RunResult {
  Trajectory trajectory{};
  // Samples actually taken; kSamples unless the run halted, in which case the remaining samples
  // stay zero. A halt is a finding (D20), never a golden.
  std::size_t samples_taken = 0;
  HealthCheck halt_check = HealthCheck::kNone;
  std::uint64_t halt_frame = 0;
};

inline Sample sample_state(const Simulation& sim) {
  Sample out{};
  const auto state = sim.vehicles().front().state();
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = std::bit_cast<std::uint64_t>(state(static_cast<Eigen::Index>(i)));
  }
  return out;
}

// `aircraft_dir` is the repository's aircraft/ directory; `plugin_dir` the build's plugin
// directory.
inline RunResult run_cruise_regression(const std::string& aircraft_dir,
                                       const std::string& plugin_dir) {
  const Config config = Config::from_file(aircraft_dir + "/notional_transport/configs/cruise.yaml");
  Simulation sim = Simulation::from_config(config, plugin_dir);

  RunResult result;
  result.trajectory[0] = sample_state(sim);
  result.samples_taken = 1;
  while (result.samples_taken < kSamples) {
    const HaltReason halt = sim.run(kFramesPerSample);
    if (halt) {
      result.halt_check = halt.check;
      result.halt_frame = halt.frame;
      break;
    }
    result.trajectory[result.samples_taken] = sample_state(sim);
    ++result.samples_taken;
  }
  return result;
}

}  // namespace astra::test::cruise_regression
