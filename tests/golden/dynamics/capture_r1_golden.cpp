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
double from_bits(std::uint64_t value) { return std::bit_cast<double>(value); }

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
// negative-magnitude values throughout. Hardcoded bit patterns, not runtime-computed: an earlier
// version of this function built samples 1/2/4's quaternions via std::sin/cos and
// Eigen::Vector4d::normalize() at runtime, reasoning that IEEE-754 operations are exact and
// therefore reproducible across presets -- wrong. A first capture run found debug and release
// disagreeing by 1 ULP on exactly the two components sample 2's quaternion built via a
// multiply-add-shaped expression (`ca * cb * cc + sa * sb * sc`); the six underlying std::sin/cos
// results were confirmed bit-identical between presets (printed and diffed directly), so the
// divergence is `-ffp-contract`'s contraction of that expression shape differing by optimization
// level -- not a hardware FMA instruction (this build passes no -march/-mfma, so none is even
// available) and not the transcendental functions themselves. "No -ffast-math" only makes a
// *single* floating-point operation reproducible; it says nothing about whether the compiler
// contracts a multi-operation *expression* identically at every optimization level. Every sample
// below is therefore a literal `std::uint64_t` bit pattern, reconstructed via `from_bits()`
// (`std::bit_cast<double>`) -- values taken from a `release`-preset run (see
// rigid_body_6dof_r1_manifest.md's own note on this), computed once, never recomputed by this
// program again. See docs/lessons.md's matching entry for the general rule.
std::array<StateVector, 5> sample_states() {
  std::array<StateVector, 5> ys;

  ys[0] << from_bits(0x0ULL), from_bits(0x0ULL), from_bits(0x0ULL),            // p_n
      from_bits(0x0ULL), from_bits(0x0ULL), from_bits(0x0ULL),                 // v_b
      from_bits(0x3ff0000000000000ULL), from_bits(0x0ULL), from_bits(0x0ULL),  // q_nb (w,x)
      from_bits(0x0ULL),                                                       // q_nb (y)
      from_bits(0x0ULL), from_bits(0x0ULL), from_bits(0x0ULL);                 // omega_b

  ys[1] << from_bits(0x4059000000000000ULL), from_bits(0x4069000000000000ULL),
      from_bits(0x4072c00000000000ULL),  // p_n
      from_bits(0x4049000000000000ULL), from_bits(0x4024000000000000ULL),
      from_bits(0xc014000000000000ULL),  // v_b
      from_bits(0x3fec2ca7610cde1eULL), from_bits(0x3fc68a1f80d71819ULL),
      from_bits(0xbfd0e797a0a15212ULL), from_bits(0x3fd68a1f80d71819ULL),  // q_nb
      from_bits(0x3fd999999999999aULL), from_bits(0xbfc999999999999aULL),
      from_bits(0x3fe3333333333333ULL);  // omega_b

  ys[2] << from_bits(0x407f400000000000ULL), from_bits(0xc069000000000000ULL),
      from_bits(0x408f400000000000ULL),  // p_n
      from_bits(0x4034000000000000ULL), from_bits(0x0ULL),
      from_bits(0x4008000000000000ULL),  // v_b
      from_bits(0x3fe6d1a5853ab3f2ULL), from_bits(0xbfa0cd763f61321fULL),
      from_bits(0x3fe6607ca4bbbe70ULL), from_bits(0x3fa3625db240dac2ULL),  // q_nb, near gimbal lock
      from_bits(0x3fa999999999999aULL), from_bits(0x3f947ae147ae147bULL),
      from_bits(0xbf847ae147ae147bULL);  // omega_b

  ys[3] << from_bits(0xc049000000000000ULL), from_bits(0x4024000000000000ULL),
      from_bits(0x0ULL),  // p_n
      from_bits(0xc03e000000000000ULL), from_bits(0x4014000000000000ULL),
      from_bits(0x4000000000000000ULL),  // v_b
      from_bits(0x4000000000000000ULL), from_bits(0x3fe0000000000000ULL),
      from_bits(0xbfd3333333333333ULL), from_bits(0x3fb999999999999aULL),  // q_nb, norm != 1
      from_bits(0x3ff0000000000000ULL), from_bits(0xbfe0000000000000ULL),
      from_bits(0x3fc999999999999aULL);  // omega_b

  ys[4] << from_bits(0xc0c3880000000000ULL), from_bits(0x40b3880000000000ULL),
      from_bits(0xc0a3880000000000ULL),  // p_n
      from_bits(0xc072c00000000000ULL), from_bits(0x406f400000000000ULL),
      from_bits(0xc062c00000000000ULL),  // v_b
      from_bits(0xbfd0f43a45cdedadULL), from_bits(0x3fe96e5768b4e483ULL),
      from_bits(0xbfe0f43a45cdedadULL), from_bits(0x3fc0f43a45cdedadULL),  // q_nb
      from_bits(0xc008000000000000ULL), from_bits(0x4004000000000000ULL),
      from_bits(0xbff8000000000000ULL);  // omega_b

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

  // print_state_bits already emits its own {{...}} (the fully-braced form a plain, non-nested
  // std::array<uint64_t, 13> needs); wrapping it in a further brace pair here over-braces it --
  // -Wmany-braces-around-scalar-init under this repo's -Werror, caught by compiling the golden
  // header directly before trusting it (unlike run_r1's nested std::array<std::array<...>,5>
  // case just above, where this same helper's {{...}} is exactly one of five required rows).
  std::cout << "constexpr std::array<std::uint64_t, 13> kCruiseEndToEndR1Result = \n";
  print_state_bits(std::cout, state, 4);
  std::cout << ";\n";
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
