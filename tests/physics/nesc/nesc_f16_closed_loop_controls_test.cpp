// The 13.x family's negative controls (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md, Q7, as
// changed by Joe's decisions after the measurement, 2026-10-13, item 6; the M7 plan's R9 and Q24).
// Each defect is planted without editing the peer's law or aircraft/f16's aero, mass or engine:
// - The elevator's sign at the law boundary (all four): tests/plugins/nesc_control_variant's
//   elevator_sign, the reply's elevator negated. Expected to fail pitch.
// - The aileron's sign at the law boundary (13.3 and 13.4): aileron_sign, the reply's aileron
//   negated after the interconnect, as an omitted +LWD-to-+RWD flip would be. Expected to fail
//   roll.
// - Izx's sign (all four, reported): the F-16 mass swapped for plugins/physical/mass/constant with
//   ixz negated (f16_flight.hpp's with_constant_mass, PR 9's route). Its only expected
//   exceedances, force_y and moment_n at the course step, are reported channels since Joe's item 3,
//   so no band is asserted, and R9's Izx clause is recorded as "not discriminated by the 13.x
//   kinematic bands". Each run must complete, and the defect must be in effect.
// - The mass route's positive control (13.3): the same swap with ixz unchanged gives a
//   bit-identical derivative, so the route plants only the sign.
// The wrong divisor is reported, not planted here: nesc_f16_closed_loop_variants_test.cpp's
// Divisor1 runs record "divisor 1 within every gated band", as R9 allows.
//
// A control stops at its first gated exceedance of the named channel and logs every channel's
// ratio. A control that halts before Band::kFirstGatedSample fails (PR 9's amendment 7): it is
// never a pass, and never read as the band being exceeded. A control that does not fail its band
// goes to Joe.
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "f16_flight.hpp"
#include "golden/nesc/case13p1_subsonic_altitude_change_f16_band.hpp"
#include "golden/nesc/case13p2_subsonic_airspeed_change_f16_band.hpp"
#include "golden/nesc/case13p3_subsonic_heading_change_f16_band.hpp"
#include "golden/nesc/case13p4_subsonic_lateral_side_step_f16_band.hpp"

namespace {

using astra::Config;
using astra::Simulation;
namespace nesc = astra::test::nesc;
namespace f16 = astra::test::nesc::f16;

constexpr std::string_view kCase13p1 = "case13p1_subsonic_altitude_change.yaml";
constexpr std::string_view kCase13p2 = "case13p2_subsonic_airspeed_change.yaml";
constexpr std::string_view kCase13p3 = "case13p3_subsonic_heading_change.yaml";
constexpr std::string_view kCase13p4 = "case13p4_subsonic_lateral_side_step.yaml";

std::string config_path(std::string_view file_name) {
  return std::string{ASTRA_AIRCRAFT_DIR} + "/f16/configs/" + std::string{file_name};
}

// The committed config with its transport plugin swapped for the test variant, and its
// test_variant line added to the peer block.
std::string planted(std::string_view file, std::string_view variant) {
  std::string text = f16::read_text(config_path(file));
  const auto replace_once = [&text](std::string_view from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
      throw astra::Error("the config must hold exactly one '" + std::string{from} + "'");
    }
    text.replace(at, from.size(), to);
  };
  replace_once("  plugin: aircraft/f16/sim/transport/nesc_control\n",
               "  plugin: tests/plugins/nesc_control_variant\n");
  replace_once("  peer:\n", "  peer:\n    test_variant: " + std::string{variant} + "\n");
  return text;
}

// Simulation::derivative at the initial state, and at it with omega_b + (0.1, 0.05, 0.1) rad/s,
// where the product of inertia couples roll and yaw (PR 9's).
struct Derivatives {
  std::vector<double> at_trim;
  std::vector<double> perturbed;
};

Derivatives derivatives_of(const Config& config) {
  Simulation sim = Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  astra::StateVector state = sim.vehicles().front().state();
  const auto size = static_cast<std::size_t>(state.size());
  Derivatives d;
  d.at_trim = sim.derivative(std::span<const double>(state.data(), size));
  state(astra::state::kAngularRate) += 0.1;
  state(astra::state::kAngularRate + 1) += 0.05;
  state(astra::state::kAngularRate + 2) += 0.1;
  d.perturbed = sim.derivative(std::span<const double>(state.data(), size));
  return d;
}

bool bit_identical(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) {
      return false;
    }
  }
  return true;
}

// PR 9's amendment 7: a halt before the window fails the control, whatever it would have shown.
template <typename Band>
void expect_no_halt_before_the_window(std::string_view name, const f16::FlightResult& result) {
  using B = std::remove_cvref_t<Band>;
  EXPECT_FALSE(result.halted && result.halt_sample < B::kFirstGatedSample)
      << name << ": halted before sample " << B::kFirstGatedSample << " (at sample "
      << result.halt_sample << ", frame " << result.halt.frame << ", " << result.halt.source
      << "), a control failure, never a pass";
}

// The control must exceed the named channel's band at some gated sample.
template <typename Band>
void expect_fails_band(std::string_view name, const Band& band, const std::string& text,
                       int channel, std::string_view channel_name) {
  using B = std::remove_cvref_t<Band>;
  f16::Flight flight{Config::from_string(text, std::string{name})};
  const f16::FlightResult result =
      flight.fly(name, band, {.mode = f16::Mode::kControl, .stop_channel = channel});
  f16::print_summary(name, band, result);
  const std::array<f16::ChannelRef, f16::kChannelCount> channels = f16::channels_of(band);
  std::cout << std::format(
      "{}: first gated exceedance of a discriminating channel: sample {}, {}\n", name,
      result.first_discriminating_exceedance,
      result.first_discriminating_channel >= 0
          ? channels.at(static_cast<std::size_t>(result.first_discriminating_channel)).name
          : "none");
  expect_no_halt_before_the_window<Band>(name, result);
  const f16::ChannelStats& stats = result.stats.at(static_cast<std::size_t>(channel));
  EXPECT_GE(stats.first_gated_exceedance, B::kFirstGatedSample)
      << name << ": the planted defect never left " << channel_name << "'s band while gated";
  EXPECT_FALSE(result.halted) << name << ": halted at sample " << result.halt_sample << " ("
                              << result.halt.source << ")";
}

// Izx's sign, reported: the defect is in effect, the run completes, and the log says whether it
// stayed inside every band the decided run asserts (13.3 and 13.4: the six aero channels
// reported).
template <typename Band>
void izx_sign_reported(std::string_view name, std::string_view file, const Band& band,
                       bool aero_reported) {
  using B = std::remove_cvref_t<Band>;
  const Config flipped_config =
      Config::from_string(f16::with_constant_mass(config_path(file), -1.0), std::string{name});
  const Derivatives f16_mass = derivatives_of(Config::from_file(config_path(file)));
  const Derivatives flipped = derivatives_of(flipped_config);
  const std::size_t p_dot = astra::state::kAngularRate;
  const std::size_t r_dot = astra::state::kAngularRate + 2;
  EXPECT_NE(flipped.perturbed.at(p_dot), f16_mass.perturbed.at(p_dot)) << name;
  EXPECT_NE(flipped.perturbed.at(r_dot), f16_mass.perturbed.at(r_dot)) << name;
  std::cout << std::format(
      "{}: with omega_b + (0.1, 0.05, 0.1), p_dot {:.6g} against {:.6g}, r_dot {:.6g} against "
      "{:.6g}\n",
      name, flipped.perturbed.at(p_dot), f16_mass.perturbed.at(p_dot), flipped.perturbed.at(r_dot),
      f16_mass.perturbed.at(r_dot));

  f16::Flight flight{flipped_config};
  const f16::FlightResult result = flight.fly(name, band, {.mode = f16::Mode::kControl});
  f16::print_summary(std::format("{} (reported, no band asserted)", name), band, result);
  const std::array<bool, f16::kChannelCount> reported =
      aero_reported ? f16::aero_reported() : std::array<bool, f16::kChannelCount>{};
  std::cout << std::format("{}: inside every band the decided run asserts ({}): {}\n", name,
                           aero_reported ? "15 channels, the six aero reported" : "all 21 channels",
                           f16::within_asserted_bands(result, reported) ? "yes" : "NO");
  expect_no_halt_before_the_window<Band>(name, result);
  EXPECT_FALSE(result.halted) << name << ": halted at sample " << result.halt_sample;
  EXPECT_EQ(result.samples_compared, B::kSampleCount) << name << " must complete";
}

}  // namespace

TEST(NescF16Control, Case13p3MassRoutePositiveControl) {
  const Derivatives f16_mass = derivatives_of(Config::from_file(config_path(kCase13p3)));
  const Derivatives constant = derivatives_of(Config::from_string(
      f16::with_constant_mass(config_path(kCase13p3), 1.0), "case13p3-constant-mass"));
  EXPECT_TRUE(bit_identical(f16_mass.at_trim, constant.at_trim))
      << "mass/constant with P1's values must reproduce the F-16 mass at the trim state";
  EXPECT_TRUE(bit_identical(f16_mass.perturbed, constant.perturbed))
      << "mass/constant with P1's values must reproduce the F-16 mass with roll and yaw rates";
}

TEST(NescF16Control, Case13p1IzxSignReported) {
  izx_sign_reported("case13p1 Izx sign", kCase13p1, nesc::case13p1::kBand, false);
}

TEST(NescF16Control, Case13p2IzxSignReported) {
  izx_sign_reported("case13p2 Izx sign", kCase13p2, nesc::case13p2::kBand, false);
}

TEST(NescF16Control, Case13p3IzxSignReported) {
  izx_sign_reported("case13p3 Izx sign", kCase13p3, nesc::case13p3::kBand, true);
}

TEST(NescF16Control, Case13p4IzxSignReported) {
  izx_sign_reported("case13p4 Izx sign", kCase13p4, nesc::case13p4::kBand, true);
}

TEST(NescF16Control, Case13p1ElevatorSignFailsPitch) {
  expect_fails_band("case13p1 elevator sign", nesc::case13p1::kBand,
                    planted(kCase13p1, "elevator_sign"), f16::kPitch, "pitch");
}

TEST(NescF16Control, Case13p2ElevatorSignFailsPitch) {
  expect_fails_band("case13p2 elevator sign", nesc::case13p2::kBand,
                    planted(kCase13p2, "elevator_sign"), f16::kPitch, "pitch");
}

TEST(NescF16Control, Case13p3ElevatorSignFailsPitch) {
  expect_fails_band("case13p3 elevator sign", nesc::case13p3::kBand,
                    planted(kCase13p3, "elevator_sign"), f16::kPitch, "pitch");
}

TEST(NescF16Control, Case13p4ElevatorSignFailsPitch) {
  expect_fails_band("case13p4 elevator sign", nesc::case13p4::kBand,
                    planted(kCase13p4, "elevator_sign"), f16::kPitch, "pitch");
}

TEST(NescF16Control, Case13p3AileronSignFailsRoll) {
  expect_fails_band("case13p3 aileron sign", nesc::case13p3::kBand,
                    planted(kCase13p3, "aileron_sign"), f16::kRoll, "roll");
}

TEST(NescF16Control, Case13p4AileronSignFailsRoll) {
  expect_fails_band("case13p4 aileron sign", nesc::case13p4::kBand,
                    planted(kCase13p4, "aileron_sign"), f16::kRoll, "roll");
}
