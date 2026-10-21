// M7b PR 4's smokes and variant pins (docs/plans/m7b-pr4-f16-symmetry.md, section 12), in every
// preset. None of them is a flight of the oracle: each flies at most 1,000 frames and compares
// nothing under the mirror or the yardstick.
// - F16SymmetryTrim.TheIcIsLateralZero: at section 5's IC, f has v_dot = p_dot = r_dot = 0 by
// value,
//   with the shipped and with the |beta| aero, and the solved residuals within M3's D8.
// - The two pins: the default_options peer variant equals the shipped peer, and the signed_tables
//   aero variant equals the shipped aero, recorder for recorder, byte for byte; and recompose()
//   gives build_up()'s fields back bitwise (section 2).
// - Each configuration of sections 7, 9 and 10, with its step at frame 500, flies 1,000 frames with
//   no allocation, no halt and 100 exchanges.
// - Q10's rule: under kNone a flat Earth is refused only with a lateral offset; the shipped Earth
//   rate refuses it always.
// - An unknown test_variant is a ConfigError naming the key, in both variant plugins.
//
// Physics label (section 12): the stop hook's `just test-unit` never runs these.
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "../../plugins/f16_aero_variant/f16_aero_variant.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/random.hpp"
#include "f16_symmetry.hpp"
#include "icd/astra_icd.h"
#include "tests/support/alloc_counter.hpp"

namespace {

namespace sym = astra::test::nesc::f16_symmetry;
namespace aero = astra::aircraft_f16_aero_math;

constexpr std::uint64_t kSmokeFrames = 1000;  // section 12: 1,000 frames, the step at frame 500
constexpr std::uint64_t kSmokeStep = 500;
constexpr std::size_t kSmokeExchanges = 100;

double parsed(std::string_view text) {
  double value = 0.0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw astra::Error("f16_symmetry_smoke: not a number: " + std::string{text});
  }
  return value;
}

std::string replaced_once(std::string text, std::string_view from, std::string_view to) {
  const std::size_t at = text.find(from);
  if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
    throw astra::Error("the config must hold exactly one '" + std::string{from} + "'");
  }
  text.replace(at, from.size(), to);
  return text;
}

std::string tracked_case13p3() {
  const std::string path =
      std::string{ASTRA_AIRCRAFT_DIR} + "/f16/configs/case13p3_subsonic_heading_change.yaml";
  std::ifstream in{path};
  if (!in) {
    throw astra::Error("cannot read " + path);
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::vector<std::byte> recorder_bytes(const std::string& text, std::string_view name) {
  astra::Simulation sim = sym::build(text, name);
  const astra::HaltReason halt = sim.run();
  EXPECT_FALSE(halt) << name << " halted: " << halt.source << " at frame " << halt.frame;
  return sim.exchange_recorder()->serialize();
}

TEST(F16SymmetryTrim, TheIcIsLateralZero) {
  // Section 5's IC, with its controls latched through write_input (trim.py's route).
  for (const std::string& aero_entry :
       {std::string{sym::kShippedAero}, sym::variant_aero("abs_beta_tables")}) {
    const std::string text = sym::round_earth_text(aero_entry);
    astra::Simulation sim = sym::build(text, "f16-symmetry IC");
    sim.write_input("controls.elevator", parsed(sym::kElevatorText));
    sim.write_input("controls.aileron", 0.0);
    sim.write_input("controls.rudder", 0.0);
    sim.write_input("controls.throttle", parsed(sym::kThrottleText));
    const std::span<const double> y = sim.vehicles()[0].combined_state();
    const std::vector<double> f = sim.derivative(y);
    ASSERT_EQ(f.size(), static_cast<std::size_t>(astra::kStateSize));
    EXPECT_EQ(f[4], 0.0) << aero_entry << ": v_dot";
    EXPECT_EQ(f[10], 0.0) << aero_entry << ": p_dot";
    EXPECT_EQ(f[12], 0.0) << aero_entry << ": r_dot";
    // M3's D8: the solved residuals (u_dot, w_dot, q_dot) at most 1e-6.
    EXPECT_LE(std::abs(f[3]), 1e-6) << aero_entry << ": u_dot";
    EXPECT_LE(std::abs(f[5]), 1e-6) << aero_entry << ": w_dot";
    EXPECT_LE(std::abs(f[11]), 1e-6) << aero_entry << ": q_dot";
    std::cout << std::format("{}: u_dot {:+.3e}, w_dot {:+.3e}, q_dot {:+.3e}\n", aero_entry, f[3],
                             f[5], f[11]);
  }
}

TEST(F16SymmetrySmoke, DefaultOptionsEqualsTheShippedPeer) {
  // The tracked 13.3 config, 1,000 frames, with the shipped plugin and with default_options.
  const std::string shipped =
      replaced_once(tracked_case13p3(), "  frames: 30001\n", "  frames: 1000\n");
  const std::string variant =
      replaced_once(replaced_once(shipped, "  plugin: aircraft/f16/sim/transport/nesc_control\n",
                                  "  plugin: tests/plugins/nesc_control_variant\n"),
                    "  peer:\n", "  peer:\n    test_variant: default_options\n");
  const std::vector<std::byte> a = recorder_bytes(shipped, "13.3, the shipped peer");
  const std::vector<std::byte> b = recorder_bytes(variant, "13.3, default_options");
  ASSERT_EQ(a.size(), b.size());
  EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size()), 0)
      << "default_options must be the shipped peer, byte for byte";
}

TEST(F16SymmetrySmoke, SignedTablesEqualTheShippedAero) {
  // Section 7's shipped-pair text, its step at frame 500, with the shipped aero and with
  // signed_tables: the variant's copy of the plugin boundary (aero.cpp:55-80) is the shipped one.
  const sym::Shape shape = sym::course_step(sym::Side::kRight, kSmokeStep, kSmokeFrames);
  const std::vector<std::byte> a =
      recorder_bytes(sym::mirror_text("default_options", sym::kShippedAero, shape), "shipped aero");
  const std::vector<std::byte> b =
      recorder_bytes(sym::mirror_text("default_options", sym::variant_aero("signed_tables"), shape),
                     "signed_tables");
  ASSERT_EQ(a.size(), b.size());
  EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size()), 0)
      << "signed_tables must be the shipped aero, byte for byte";
}

TEST(F16SymmetrySmoke, RecomposeIsBuildUpBitwise) {
  // Seeded inputs across the tables' ranges (astra::random::Rng, AGENTS.md): recompose() on an
  // unmodified BuildUp gives build_up()'s fields back, bit for bit (section 2).
  static_assert(sizeof(aero::BuildUp) % sizeof(double) == 0, "BuildUp holds only doubles");
  using Bits = std::array<std::uint64_t, sizeof(aero::BuildUp) / sizeof(double)>;
  constexpr int kDraws = 1 << 12;
  constexpr double kDeg = std::numbers::pi / 180.0;
  astra::random::Rng rng{4};
  const auto uniform = [&rng](double lo, double hi) { return lo + (hi - lo) * rng.uniform01(); };
  int differing = 0;
  for (int n = 0; n < kDraws; ++n) {
    aero::Inputs in;
    in.vt = uniform(100.0, 300.0);
    in.alpha = uniform(-10.0, 45.0) * kDeg;
    in.beta = uniform(-30.0, 30.0) * kDeg;
    in.p = uniform(-1.0, 1.0);
    in.q = uniform(-1.0, 1.0);
    in.r = uniform(-1.0, 1.0);
    in.el = uniform(-25.0, 25.0) * kDeg;
    in.ail = uniform(-21.5, 21.5) * kDeg;
    in.rdr = uniform(-30.0, 30.0) * kDeg;
    const aero::BuildUp shipped = aero::build_up(in);
    aero::BuildUp again = shipped;
    astra::test::f16_aero_variant::recompose(again);
    if (std::bit_cast<Bits>(shipped) != std::bit_cast<Bits>(again)) {
      ++differing;
    }
  }
  EXPECT_EQ(differing, 0) << "of " << kDraws << " seeded inputs";
}

TEST(F16SymmetrySmoke, EachHookAndDefectFliesWithoutAllocating) {
  struct Case {
    std::string name;
    std::string text;
  };
  const auto right = [](bool side_step) {
    return side_step ? sym::side_step(sym::Side::kRight, kSmokeStep, kSmokeFrames)
                     : sym::course_step(sym::Side::kRight, kSmokeStep, kSmokeFrames);
  };
  const std::string abs_tables = sym::variant_aero("abs_beta_tables");
  const std::string doubled = sym::variant_aero("signed_tables_doubled_odd");
  const std::string sign_drop = sym::variant_aero("abs_beta_tables_cl0_unsigned");
  const auto heading = [](std::string_view variant, const sym::Heading& h) {
    return sym::heading_text(variant, h.psi0, h.course_deg, h.step_course_deg, kSmokeStep,
                             kSmokeFrames);
  };
  const std::array<sym::Heading, 3> headings = sym::headings();
  std::vector<Case> cases = {
      {"the variant pair, 13.3's shape",
       sym::mirror_text("earth_rate_none", abs_tables, right(false))},
      {"the variant pair, 13.4's shape",
       sym::mirror_text("earth_rate_none", abs_tables, right(true))},
      {"the shipped pair, 13.3's shape",
       sym::mirror_text("default_options", sym::kShippedAero, right(false))},
      {"the shipped pair, 13.4's shape",
       sym::mirror_text("default_options", sym::kShippedAero, right(true))},
      {"the Omega-only control", sym::mirror_text("default_options", abs_tables, right(false))},
      {"the tables-only control",
       sym::mirror_text("earth_rate_none", sym::kShippedAero, right(false))},
      {"the sign-drop control", sym::mirror_text("earth_rate_none", sign_drop, right(false))},
      {"the doubled control, 13.3's shape",
       sym::mirror_text("default_options", doubled, right(false))},
      {"the doubled control, 13.4's shape",
       sym::mirror_text("default_options", doubled, right(true))},
      {"the unwrapped track error", heading("earth_rate_none_unwrapped_track_error", headings[2])},
  };
  for (const sym::Heading& h : headings) {
    cases.push_back({h.name, heading("earth_rate_none", h)});
  }
  for (const Case& c : cases) {
    astra::Simulation sim = sym::build(c.text, c.name);
    const std::size_t before = astra_test::allocation_count();
    const astra::HaltReason halt = sim.run();
    const std::size_t after = astra_test::allocation_count();
    EXPECT_FALSE(halt) << c.name << ": halted, " << halt.source << " at frame " << halt.frame;
    EXPECT_EQ(after, before) << c.name << ": allocations over the steps";
    EXPECT_EQ(sim.exchange_recorder()->requests().size(), kSmokeExchanges) << c.name;
  }
}

TEST(F16SymmetrySmoke, EarthRateNoneRefusesAFlatEarthOnlyWithALateralOffset) {
  // Q10's rule (section 2): under kNone the Earth rate reads neither lat nor lon, so only the
  // lateral offset's path refuses a flat Earth (ADR-0029 Decision 2). The shipped Earth rate reads
  // lat, so default_options refuses a flat Earth always.
  const std::string flat = sym::flat_earth_text("0.0", kSmokeFrames);
  const std::string offset = sym::side_step(sym::Side::kRight, kSmokeStep, kSmokeFrames).peer_lines;
  const auto refusal = [](const astra::Simulation& sim) {
    const double* value = sim.registry().find("transport.peer.refusal");
    return value == nullptr ? 0U : static_cast<std::uint32_t>(*value);
  };
  {
    astra::Simulation sim =
        sym::build(flat + sym::transport_text("earth_rate_none", "0", offset), "kNone, offset");
    const astra::HaltReason halt = sim.run();
    EXPECT_EQ(halt.check, astra::HealthCheck::kTransport);
    EXPECT_EQ(halt.frame, 0U);
    EXPECT_NE(refusal(sim) & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  }
  {
    astra::Simulation sim =
        sym::build(flat + sym::transport_text("earth_rate_none", "0", ""), "kNone, no offset");
    const astra::HaltReason halt = sim.run();
    EXPECT_FALSE(halt) << halt.source << " at frame " << halt.frame;
    EXPECT_EQ(refusal(sim), 0U);
  }
  {
    astra::Simulation sim =
        sym::build(flat + sym::transport_text("default_options", "0", ""), "default_options");
    const astra::HaltReason halt = sim.run();
    EXPECT_EQ(halt.check, astra::HealthCheck::kTransport);
    EXPECT_EQ(halt.frame, 0U);
    EXPECT_NE(refusal(sim) & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  }
}

TEST(F16SymmetrySmoke, UnknownVariantsAreConfigErrorsNamingTheKey) {
  const sym::Shape shape = sym::course_step(sym::Side::kRight, kSmokeStep, kSmokeFrames);
  const std::array<std::string, 2> texts = {
      sym::mirror_text("no_such_variant", sym::kShippedAero, shape),
      sym::mirror_text("default_options", sym::variant_aero("no_such_mode"), shape)};
  for (const std::string& text : texts) {
    try {
      static_cast<void>(sym::build(text, "an unknown variant"));
      ADD_FAILURE() << "an unknown test_variant must be refused";
    } catch (const astra::ConfigError& error) {
      EXPECT_TRUE(error.config_path().ends_with("test_variant")) << error.config_path();
      EXPECT_NE(std::string{error.what()}.find("unknown test variant"), std::string::npos)
          << error.what();
    }
  }
}

}  // namespace
