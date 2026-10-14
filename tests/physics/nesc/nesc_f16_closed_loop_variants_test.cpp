// The 13.x cases' reported variants (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md, Q5, Q6 and
// D11; the M7 plan's R6: "Reported, never selected: SIM 5's LQR references, divisor 1, and Q27's
// geometry variants"). Each flies a committed 13.x config with exactly one thing changed, through
// f16_flight.hpp in Mode::kControl: no band is asserted, every channel's largest ratio is logged,
// and the run must complete without a halt. None of them selects anything: the decided runs are
// nesc_f16_closed_loop_test.cpp's.
//
// Joe's decision of 2026-10-13, made after reading commit 6a's measurement, amended the M7 plan's
// Q11: the decided LQR references are now the trim's own (SIM 5's replacement, the plan's D11), so
// the configs fly them, and the DML's hard-coded references, the first decided choice, are the
// reported variant. Commit 6a's runs of the earlier configs are kept as evidence
// (docs/reviews/evidence/m7-pr11a-r6/variants.log).
// - NescF16Variant.Case13p{1-4}DmlReferences: the DML's hard-coded LQR references
//   (trimmedAlpha = trimmedTheta = 2.653813535191715 deg, trimmedKEAS = 287.8088596053291 kt)
//   in place of the trim's, through tests/plugins/nesc_control_variant.
// - NescF16Variant.Case13p{1-4}Divisor1: the law at exchange divisor 1 with the decided (trim)
//   references, so D11 at divisor 1: where the oracle would have landed with both changes (Joe's
//   item 5; Q6's measurement, reported only, since Q12 stands at divisor 10).
// - NescF16Variant.Case13p4{RhumbLine,GreatCircle,LineAtOffsetFrame}: Q27's three geometries,
//   through the variant plugin. Each logs its lateralDeviationError minus the decided line's, from
//   the same sample's position, against the plan's geometric estimate (Q5: -0.24, -0.72 and
//   -1.02 m at 20 s; -2.38, -6.68 and -2.06 m at 60 s along the band reference).
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <array>
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "astra/config/config.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/geodesy.hpp"
#include "astra/util/units.hpp"
#include "f16_flight.hpp"
#include "golden/nesc/case13p1_subsonic_altitude_change_f16_band.hpp"
#include "golden/nesc/case13p2_subsonic_airspeed_change_f16_band.hpp"
#include "golden/nesc/case13p3_subsonic_heading_change_f16_band.hpp"
#include "golden/nesc/case13p4_subsonic_lateral_side_step_f16_band.hpp"

namespace {

using astra::Config;
namespace nesc = astra::test::nesc;
namespace f16 = astra::test::nesc::f16;
namespace units = astra::units;
namespace geodesy = astra::geodesy;

std::string config_path(std::string_view file_name) {
  return std::string{ASTRA_AIRCRAFT_DIR} + "/f16/configs/" + std::string{file_name};
}

std::string replaced_once(std::string text, std::string_view from, std::string_view to) {
  const std::size_t at = text.find(from);
  if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
    throw astra::Error("the config must hold exactly one '" + std::string{from} + "'");
  }
  text.replace(at, from.size(), to);
  return text;
}

// The committed config with its transport plugin swapped for the test variant, and the variant's
// key lines added to its peer block.
std::string variant_config(std::string_view file, std::string_view peer_lines) {
  std::string text = f16::read_text(config_path(file));
  text = replaced_once(text, "  plugin: aircraft/f16/sim/transport/nesc_control\n",
                       "  plugin: tests/plugins/nesc_control_variant\n");
  return replaced_once(text, "  peer:\n", "  peer:\n" + std::string{peer_lines});
}

// The DML's hard-coded LQR references (Q11's reported variant since the 2026-10-13 amendment).
std::string dml_references(std::string_view file) {
  return variant_config(file, "    test_variant: dml_references\n");
}

std::string divisor_one(std::string_view file) {
  return replaced_once(f16::read_text(config_path(file)),
                       "  divisor: 10  # 100 Hz (ADR-0029 Decision 4)\n", "  divisor: 1\n");
}

// Each run also logs whether it stayed inside every band the decided run asserts (Joe's item 3:
// all 21 channels in 13.1 and 13.2, the six aero channels reported in 13.3 and 13.4). For the
// Divisor1 runs, that line is R9's wrong-divisor record.
template <typename Band>
void fly_reported(std::string_view name, const std::string& text, const Band& band,
                  bool aero_reported) {
  using B = std::remove_cvref_t<Band>;
  f16::Flight flight{Config::from_string(text, std::string{name})};
  f16::FlightOptions options;
  options.mode = f16::Mode::kControl;
  const f16::FlightResult result = flight.fly(name, band, options);
  f16::print_summary(std::format("{} (reported, no band asserted)", name), band, result);
  std::cout << std::format(
      "{}: first gated exceedance of a discriminating channel: sample {}, {}\n", name,
      result.first_discriminating_exceedance, result.first_discriminating_channel);
  const std::array<bool, f16::kChannelCount> reported =
      aero_reported ? f16::aero_reported() : std::array<bool, f16::kChannelCount>{};
  std::cout << std::format("{}: inside every band the decided run asserts ({}): {}\n", name,
                           aero_reported ? "15 channels, the six aero reported" : "all 21 channels",
                           f16::within_asserted_bands(result, reported) ? "yes" : "NO");
  EXPECT_FALSE(result.halted) << name << ": halted at sample " << result.halt_sample << " ("
                              << result.halt.source << ")";
  EXPECT_EQ(result.samples_compared, B::kSampleCount) << name << " must complete";
}

// Q27's variants: the variant's lateralDeviationError against the decided line's d from the same
// sample's position (ADR-0029 Decision 6, written here from geodesy's radii), at 20, 40 and 60 s.
template <typename Band>
void fly_geometry(std::string_view name, std::string_view variant, const Band& band) {
  const double offset = units::feet_to_meters(2000.0);
  const double chi = units::degrees_to_radians(45.0);
  Eigen::Vector3d base = Eigen::Vector3d::Zero();
  f16::FlightOptions options;
  const std::string text = variant_config("case13p4_subsonic_lateral_side_step.yaml",
                                          std::format("    test_variant: {}\n", variant));
  f16::Flight flight{Config::from_string(text, std::string{name})};
  const double* deviation = flight.sim().registry().find("transport.peer.lateral_deviation_error");
  ASSERT_NE(deviation, nullptr);
  options.mode = f16::Mode::kControl;
  options.on_sample = [&](int k, const astra::StateVector& pre_step,
                          const astra::EnvironmentSample& /*env*/) {
    const Eigen::Vector3d p_n =
        pre_step.segment<astra::state::kPositionSize>(astra::state::kPosition);
    if (k == 0) {
      base = p_n;
    }
    if (k == 200 || k == 400 || k == 600) {
      const double dn = (p_n(0) - base(0)) * (geodesy::meridian_radius(base(0)) + base(2));
      const double de = (p_n(1) - base(1)) * (geodesy::prime_vertical_radius(base(0)) + base(2)) *
                        std::cos(base(0));
      const double decided = (-dn * std::sin(chi)) + (de * std::cos(chi));
      std::cout << std::format(
          "{} t = {} s: variant lateralDeviationError + offset - decided d = {:+.3f} m (decided "
          "d {:.3f} m)\n",
          name, k / 10, *deviation + offset - decided, decided);
    }
  };
  using B = std::remove_cvref_t<Band>;
  const f16::FlightResult result = flight.fly(name, band, options);
  f16::print_summary(std::format("{} (reported, no band asserted)", name), band, result);
  EXPECT_FALSE(result.halted) << name;
  EXPECT_EQ(result.samples_compared, B::kSampleCount) << name;
}

}  // namespace

TEST(NescF16Variant, Case13p1DmlReferences) {
  fly_reported("case13p1 DML references", dml_references("case13p1_subsonic_altitude_change.yaml"),
               nesc::case13p1::kBand, false);
}

TEST(NescF16Variant, Case13p2DmlReferences) {
  fly_reported("case13p2 DML references", dml_references("case13p2_subsonic_airspeed_change.yaml"),
               nesc::case13p2::kBand, false);
}

TEST(NescF16Variant, Case13p3DmlReferences) {
  fly_reported("case13p3 DML references", dml_references("case13p3_subsonic_heading_change.yaml"),
               nesc::case13p3::kBand, true);
}

TEST(NescF16Variant, Case13p4DmlReferences) {
  fly_reported("case13p4 DML references",
               dml_references("case13p4_subsonic_lateral_side_step.yaml"), nesc::case13p4::kBand,
               true);
}

TEST(NescF16Variant, Case13p1Divisor1) {
  fly_reported("case13p1 divisor 1 (trim references)",
               divisor_one("case13p1_subsonic_altitude_change.yaml"), nesc::case13p1::kBand, false);
}

TEST(NescF16Variant, Case13p2Divisor1) {
  fly_reported("case13p2 divisor 1 (trim references)",
               divisor_one("case13p2_subsonic_airspeed_change.yaml"), nesc::case13p2::kBand, false);
}

TEST(NescF16Variant, Case13p3Divisor1) {
  fly_reported("case13p3 divisor 1 (trim references)",
               divisor_one("case13p3_subsonic_heading_change.yaml"), nesc::case13p3::kBand, true);
}

TEST(NescF16Variant, Case13p4Divisor1) {
  fly_reported("case13p4 divisor 1 (trim references)",
               divisor_one("case13p4_subsonic_lateral_side_step.yaml"), nesc::case13p4::kBand,
               true);
}

TEST(NescF16Variant, Case13p4RhumbLine) {
  fly_geometry("case13p4 rhumb line", "rhumb_line", nesc::case13p4::kBand);
}

TEST(NescF16Variant, Case13p4GreatCircle) {
  fly_geometry("case13p4 great circle", "great_circle", nesc::case13p4::kBand);
}

TEST(NescF16Variant, Case13p4LineAtOffsetFrame) {
  fly_geometry("case13p4 line at the offset frame", "line_at_offset_frame", nesc::case13p4::kBand);
}
