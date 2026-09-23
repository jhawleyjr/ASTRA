// M3 PR 8 capture harness (D20, docs/plans/pr8-cruise-regression.md): prints the frozen cruise
// regression trajectory as C++ source for tests/golden/notional_transport/
// cruise_regression_golden.hpp. Built and run only by capture_cruise_regression.sh, under all four
// presets, whose outputs must agree byte for byte before anything is written. Never run by
// `just check`. The run itself is tests/support/cruise_regression_run.hpp -- the same function the
// regression test calls -- so this file only formats.
//
// Usage:
//   capture_cruise_regression trajectory <aircraft_dir> <plugin_dir>
//   capture_cruise_regression meta
// `trajectory` exits 2 without printing an array if the run halted (a halt is a finding, D20).
#include <Eigen/Core>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#include "cruise_regression_run.hpp"

namespace {

namespace cr = astra::test::cruise_regression;

int run_trajectory(const std::string& aircraft_dir, const std::string& plugin_dir) {
  const cr::RunResult result = cr::run_cruise_regression(aircraft_dir, plugin_dir);
  if (result.samples_taken != cr::kSamples) {
    std::cerr << "capture_cruise_regression: run halted (HealthCheck "
              << static_cast<int>(result.halt_check) << ") at frame " << result.halt_frame
              << " after " << result.samples_taken << " samples -- nothing to freeze\n";
    return 2;
  }
  std::cout << "inline constexpr std::size_t kCruiseRegressionSamples = " << cr::kSamples << ";\n";
  std::cout << "inline constexpr std::uint64_t kCruiseRegressionFramesPerSample = "
            << cr::kFramesPerSample << ";\n";
  std::cout << "inline constexpr std::array<std::array<std::uint64_t, " << astra::kStateSize
            << ">, " << cr::kSamples << "> kCruiseRegressionTrajectory = {{\n";
  for (std::size_t k = 0; k < cr::kSamples; ++k) {
    std::cout << "    {{";
    for (std::size_t i = 0; i < result.trajectory[k].size(); ++i) {
      std::cout << "0x" << std::hex << result.trajectory[k][i] << "ULL" << std::dec
                << (i + 1 < result.trajectory[k].size() ? ", " : "");
    }
    std::cout << "}}" << (k + 1 < cr::kSamples ? "," : "") << "  // t = " << k << " s\n";
  }
  std::cout << "}};\n";
  return 0;
}

// This preset's Eigen version and libstdc++ ABI, for the manifest (the capture_r1_golden meta
// pattern). Compiler version and target triple are read by the wrapper script directly.
void run_meta() {
  std::cout << "EIGEN_VERSION=" << EIGEN_WORLD_VERSION << "." << EIGEN_MAJOR_VERSION << "."
            << EIGEN_MINOR_VERSION << "\n";
#ifdef __GLIBCXX__
  std::cout << "GLIBCXX=" << __GLIBCXX__ << "\n";
#else
  std::cout << "GLIBCXX=none\n";
#endif
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view mode = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
  if (mode == "trajectory" && argc == 4) {
    return run_trajectory(argv[2], argv[3]);
  }
  if (mode == "meta" && argc == 2) {
    run_meta();
    return 0;
  }
  std::cerr << "usage: capture_cruise_regression trajectory <aircraft_dir> <plugin_dir> | meta\n";
  return 1;
}
