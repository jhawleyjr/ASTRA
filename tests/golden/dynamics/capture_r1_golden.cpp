// Pre-change-only capture harness for ADR-0021's R1 (bit-identity) golden and its end-to-end
// companion, R1-Sim (docs/plans/pr4a-dynamics-model.md, "R1 golden capture"). Never compiled again
// after the one authorized capture step that produces tests/golden/dynamics/{
// rigid_body_6dof_r1_golden.hpp, cruise_end_to_end_r1_golden.hpp} and their shared manifest.
// Prints, to stdout, the exact C++ data-array fragments capture_r1_golden.sh embeds into those
// golden headers. Not run directly by a human: the wrapper script builds and runs this program
// under all four presets and cross-checks its output bit-for-bit before trusting any of it (Fix 2
// of the plan).
//
// Usage:
//   capture_r1_golden r1    <plugin_dir>
//   capture_r1_golden r1sim <plugin_dir> <fixture_yaml_path>
#include <Eigen/Core>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/eom.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::Eom;
using astra::Integrator;
using astra::IntegratorKind;
using astra::PluginLoader;
using astra::SignalRegistry;
using astra::StateVector;
using astra::Wrench;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

void print_state_bits(std::ostream& out, const StateVector& y, int indent) {
  out << std::string(static_cast<std::size_t>(indent), ' ') << "{{";
  for (int i = 0; i < astra::kStateSize; ++i) {
    out << "0x" << std::hex << bits(y(i)) << "ULL" << std::dec;
    if (i + 1 < astra::kStateSize) {
      out << ", ";
    }
  }
  out << "}}";
}

// Five sampled initial states (ADR-0021 R1): rest; translation and rotation together (exercises the
// Coriolis-like and gyroscopic cross terms); near gimbal lock; a deliberately off-unit-norm
// quaternion (exercises post_step_renormalize's real branch, not only the derivative); large and
// negative-magnitude values throughout. Computed at runtime, not hand-transcribed decimal literals:
// std::sin/cos and Eigen::Vector4d::normalize() are exact IEEE-754 operations, identical across
// every preset this harness is cross-checked under (the whole point of that cross-check), so there
// is no transcription-truncation risk (docs/lessons.md, 2026-09-19) in writing them this way.
std::array<StateVector, 5> sample_states() {
  std::array<StateVector, 5> ys;

  ys[0] = StateVector::Zero();
  ys[0](astra::state::kQuaternion) = 1.0;  // rest at the origin, identity attitude

  ys[1] << 100.0, 200.0, 300.0,  // p_n
      50.0, 10.0, -5.0,          // v_b
      0.0, 0.0, 0.0, 0.0,        // q_nb (overwritten below with a normalized value)
      0.4, -0.2, 0.6;            // omega_b
  {
    Eigen::Vector4d q{1.0, 0.2, -0.3, 0.4};
    q.normalize();
    ys[1].segment<4>(astra::state::kQuaternion) = q;
  }

  {
    // Near gimbal lock (theta close to but not exactly +pi/2), via the exact Euler-to-quaternion
    // form tests/golden/frames/adr0001_cases.hpp already documents (Stevens & Lewis eq. 1.8-16).
    const double phi = 0.3;
    const double theta = 1.55;  // rad; +pi/2 is ~1.5708
    const double psi = 0.4;
    const double half_phi = phi / 2.0;
    const double half_theta = theta / 2.0;
    const double half_psi = psi / 2.0;
    const double ca = std::cos(half_phi);
    const double sa = std::sin(half_phi);
    const double cb = std::cos(half_theta);
    const double sb = std::sin(half_theta);
    const double cc = std::cos(half_psi);
    const double sc = std::sin(half_psi);
    ys[2] << 500.0, -200.0, 1000.0,   // p_n
        20.0, 0.0, 3.0,               // v_b
        ca * cb * cc + sa * sb * sc,  // w
        sa * cb * cc - ca * sb * sc,  // x
        ca * sb * cc + sa * cb * sc,  // y
        ca * cb * sc - sa * sb * cc,  // z
        0.05, 0.02, -0.01;            // omega_b
  }

  ys[3] << -50.0, 10.0, 0.0,  // p_n
      -30.0, 5.0, 2.0,        // v_b
      2.0, 0.5, -0.3, 0.1,    // q_nb, norm != 1 on purpose
      1.0, -0.5, 0.2;         // omega_b

  ys[4] << -10000.0, 5000.0, -2500.0,  // p_n
      -300.0, 250.0, -150.0,           // v_b
      0.0, 0.0, 0.0, 0.0,              // q_nb (overwritten below with a normalized value)
      -3.0, 2.5, -1.5;                 // omega_b
  {
    Eigen::Vector4d q{-1.0, 3.0, -2.0, 0.5};
    q.normalize();
    ys[4].segment<4>(astra::state::kQuaternion) = q;
  }

  return ys;
}

Wrench fixed_wrench() {
  Wrench wrench;
  wrench.force_b = Eigen::Vector3d{1500.0, -300.0, 800.0};
  wrench.moment_b = Eigen::Vector3d{200.0, -150.0, 75.0};
  return wrench;
}

// R1: Eom-level, in isolation. flat earth + constant gravity + constant mass -- the simplest
// fixture that still exercises every real Earth/gravity/mass term in Eom::operator(), per the
// plan's own "Config/fixture" section.
void run_r1(const std::string& plugin_dir) {
  const PluginLoader loader(plugin_dir);
  auto earth_loaded = loader.load("plugins/physical/earth/flat");
  auto gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto mass_loaded = loader.load("plugins/physical/mass/constant");

  SignalRegistry registry;
  earth_loaded.model->init(Config::from_string("", "capture-r1-earth"), registry);
  gravity_loaded.model->init(Config::from_string("g: 9.80665\n", "capture-r1-gravity"), registry);
  mass_loaded.model->init(
      Config::from_string("mass: 1000.0\nixx: 1000.0\niyy: 1500.0\nizz: 2000.0\n",
                          "capture-r1-mass"),
      registry);

  auto* earth = dynamic_cast<astra::EarthModel*>(earth_loaded.model.get());
  auto* gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  auto* mass = dynamic_cast<astra::MassPropertiesModel*>(mass_loaded.model.get());
  if (earth == nullptr || gravity == nullptr || mass == nullptr) {
    std::cerr << "capture_r1_golden: a loaded plugin did not implement the expected interface\n";
    std::exit(1);
  }

  Eom eom(*earth, *gravity, *mass);
  const Integrator integrator(IntegratorKind::kRk4, astra::kDefaultDt);
  const Wrench wrench = fixed_wrench();

  const std::array<StateVector, 5> initial = sample_states();
  std::array<StateVector, 5> result = initial;

  const auto derivative = [&](double t, const StateVector& y) noexcept -> StateVector {
    eom.set_applied_wrench(wrench);
    return eom(t, y);
  };

  for (StateVector& y : result) {
    eom.begin_frame();
    integrator.step(derivative, 0.0, y);
    static_cast<void>(astra::post_step_renormalize(y));
  }

  std::cout << "constexpr std::array<std::array<std::uint64_t, 13>, 5> kRigidBody6DofR1Initial = "
               "{{\n";
  for (std::size_t i = 0; i < initial.size(); ++i) {
    print_state_bits(std::cout, initial[i], 4);
    std::cout << (i + 1 < initial.size() ? ",\n" : "\n");
  }
  std::cout << "}};\n";

  std::cout << "constexpr std::array<double, 6> kRigidBody6DofR1Wrench = {\n"
            << "    " << wrench.force_b(0) << ", " << wrench.force_b(1) << ", " << wrench.force_b(2)
            << ",\n"
            << "    " << wrench.moment_b(0) << ", " << wrench.moment_b(1) << ", "
            << wrench.moment_b(2) << "};\n";

  std::cout << "constexpr std::array<std::array<std::uint64_t, 13>, 5> kRigidBody6DofR1Result = "
               "{{\n";
  for (std::size_t i = 0; i < result.size(); ++i) {
    print_state_bits(std::cout, result[i], 4);
    std::cout << (i + 1 < result.size() ? ",\n" : "\n");
  }
  std::cout << "}};\n";
}

// R1-Sim: a full Simulation::from_config over the frozen cruise.yaml fixture, run to its own
// configured frame count, recording the final rigid-body state and whether the run halted.
void run_r1sim(const std::string& plugin_dir, const std::string& fixture_yaml) {
  const Config config = Config::from_file(fixture_yaml);
  astra::Simulation sim = astra::Simulation::from_config(config, plugin_dir);
  const astra::HaltReason halt = sim.run();
  const StateVector state = sim.vehicles().front().state();

  std::cout << "constexpr std::array<std::uint64_t, 13> kCruiseEndToEndR1Result = {{\n";
  print_state_bits(std::cout, state, 4);
  std::cout << "\n}};\n";
  std::cout << "constexpr std::uint64_t kCruiseEndToEndR1Frames = " << sim.frames() << ";\n";
  std::cout << "constexpr bool kCruiseEndToEndR1Halted = " << (halt ? "true" : "false") << ";\n";
}

// meta: this preset's Eigen version and libstdc++ ABI, read from the same compile-time macros the
// build itself sees -- for the manifest (Fix 4 of the plan), not hand-typed. Compiler version and
// target triple are read by the wrapper script directly (`clang++-18 --version`/`-dumpmachine`),
// since those are the same across every preset here and need no build to discover.
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
  if (argc < 2) {
    std::cerr << "usage: capture_r1_golden r1 <plugin_dir> | r1sim <plugin_dir> <fixture_yaml> "
                 "| meta\n";
    return 2;
  }
  const std::string mode = argv[1];
  try {
    if (mode == "meta") {
      run_meta();
    } else if (mode == "r1") {
      if (argc < 3) {
        std::cerr << "usage: capture_r1_golden r1 <plugin_dir>\n";
        return 2;
      }
      run_r1(argv[2]);
    } else if (mode == "r1sim") {
      if (argc < 4) {
        std::cerr << "usage: capture_r1_golden r1sim <plugin_dir> <fixture_yaml>\n";
        return 2;
      }
      run_r1sim(argv[2], argv[3]);
    } else {
      std::cerr << "unknown mode: " << mode << "\n";
      return 2;
    }
  } catch (const std::exception& e) {
    std::cerr << "capture_r1_golden: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
