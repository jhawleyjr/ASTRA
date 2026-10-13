// The cases 11 and 12 family's negative controls (M7 PR 9, docs/plans/m7-pr9-open-loop.md, Q4; the
// M7 plan's R9 and Q24). Each defect is planted without editing aircraft/f16's aero, mass or
// engine, and each control's expected band was fixed in the plan before any run:
// - Izx's sign (case 11): the F-16 mass swapped for plugins/physical/mass/constant carrying P1's
//   values with ixz negated. A reported control (Joe's amendment 6): with trim rates near 5e-5
//   rad/s the plan's linear lateral estimate keeps it within 0.33 of every band, so no band is
//   asserted. It must complete, and the defect must be in effect. The positive control first shows
//   that the swap with ixz unchanged gives a bit-identical derivative, so the route plants only the
//   sign.
// - The elevator's sign (case 11): the committed trim's elevator written back negated through the
//   write seam before frame 0, the trim/plugin disagreement a twice-done ICD conversion would give
//   (a sign flipped in both the aero and the trim is invisible open loop). Expected to fail
//   force_z.
// - SIM 2's zero-inertial-rate trim (cases 11 and 12): tests/physics/nesc/case1{1,2}_f16_zero_
//   inertial_trim.yaml, the same bases trimmed by astra trim --rate-convention zero_inertial.
//   Expected to fail rate_q.
// A control that halts before Band::kFirstGatedSample fails (Joe's amendment 7): it is never a
// pass, and never read as the band being exceeded. A control that does not fail its band goes to
// Joe.
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/units.hpp"
#include "f16_flight.hpp"
#include "golden/nesc/case11_trim_check_subsonic_f16_band.hpp"
#include "golden/nesc/case12_trim_check_supersonic_f16_band.hpp"

namespace {

using astra::Config;
using astra::Simulation;
namespace nesc = astra::test::nesc;
namespace f16 = astra::test::nesc::f16;
namespace units = astra::units;

std::string read_text(const std::string& path) {
  std::ifstream in{path};
  if (!in) {
    throw astra::Error("cannot read " + path);
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string case11_path() {
  return std::string{ASTRA_AIRCRAFT_DIR} + "/f16/configs/case11_trim_check_subsonic.yaml";
}

std::string zero_inertial_path(std::string_view file_name) {
  return std::string{ASTRA_TEST_NESC_CASE_DIR} + "/" + std::string{file_name};
}

// Case 11's config text with physical.mass swapped for plugins/physical/mass/constant, carrying
// F16_inertia.dml's values through the same core/util/units functions the F-16 mass plugin uses,
// written %.17g so the core reads back each double exactly. ixz is the S&L product of inertia
// (mass/constant's convention, M7 PR 4), times izx_sign.
std::string case11_with_constant_mass(double izx_sign) {
  const std::string block = "  mass:\n    plugin: aircraft/f16/physical/mass\n";
  std::string text = read_text(case11_path());
  const std::size_t at = text.find(block);
  if (at == std::string::npos || text.find(block, at + 1) != std::string::npos) {
    throw astra::Error("case 11's config does not hold exactly one F-16 mass block");
  }
  const std::string swapped = std::format(
      "  mass:\n"
      "    plugin: plugins/physical/mass/constant\n"
      "    mass: {:.17g}\n"
      "    ixx: {:.17g}\n"
      "    iyy: {:.17g}\n"
      "    izz: {:.17g}\n"
      "    ixz: {:.17g}\n",
      units::slugs_to_kilograms(637.1595),
      units::slug_square_feet_to_kilogram_square_meters(9496.0),
      units::slug_square_feet_to_kilogram_square_meters(55814.0),
      units::slug_square_feet_to_kilogram_square_meters(63100.0),
      izx_sign * units::slug_square_feet_to_kilogram_square_meters(982.0));
  text.replace(at, block.size(), swapped);
  return text;
}

// Simulation::derivative at the initial state, and at it with omega_b + (0.1, 0.05, 0.1) rad/s,
// where the product of inertia couples roll and yaw.
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

// Amendment 7: a halt before the window fails the control, whatever it would have shown.
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
void expect_fails_band(std::string_view name, const Band& band, Config config, int channel,
                       std::string_view channel_name,
                       const std::function<void(Simulation&)>& plant = {}) {
  using B = std::remove_cvref_t<Band>;
  f16::Flight flight{std::move(config)};
  if (plant) {
    plant(flight.sim());
  }
  const f16::FlightResult result =
      flight.fly(name, band, {.mode = f16::Mode::kControl, .stop_channel = channel});
  f16::print_summary(name, band, result);
  expect_no_halt_before_the_window<Band>(name, result);
  const f16::ChannelStats& stats = result.stats.at(static_cast<std::size_t>(channel));
  EXPECT_GE(stats.first_gated_exceedance, B::kFirstGatedSample)
      << name << ": the planted defect never left " << channel_name << "'s band while gated";
  EXPECT_FALSE(result.halted) << name << ": halted at sample " << result.halt_sample;
}

}  // namespace

TEST(NescF16Control, Case11MassRoutePositiveControl) {
  const Derivatives f16_mass = derivatives_of(Config::from_file(case11_path()));
  const Derivatives constant =
      derivatives_of(Config::from_string(case11_with_constant_mass(1.0), "case11-constant-mass"));
  EXPECT_TRUE(bit_identical(f16_mass.at_trim, constant.at_trim))
      << "mass/constant with P1's values must reproduce the F-16 mass at the trim state";
  EXPECT_TRUE(bit_identical(f16_mass.perturbed, constant.perturbed))
      << "mass/constant with P1's values must reproduce the F-16 mass with roll and yaw rates";
}

TEST(NescF16Control, Case11IzxSignReported) {
  const Config planted =
      Config::from_string(case11_with_constant_mass(-1.0), "case11-izx-sign-flipped");
  // The defect is in effect: with roll and yaw rates, p_dot and r_dot differ from the F-16's.
  const Derivatives f16_mass = derivatives_of(Config::from_file(case11_path()));
  const Derivatives flipped = derivatives_of(planted);
  const std::size_t p_dot = astra::state::kAngularRate;
  const std::size_t r_dot = astra::state::kAngularRate + 2;
  EXPECT_NE(flipped.perturbed.at(p_dot), f16_mass.perturbed.at(p_dot));
  EXPECT_NE(flipped.perturbed.at(r_dot), f16_mass.perturbed.at(r_dot));
  std::cout << std::format(
      "case11 Izx sign: with omega_b + (0.1, 0.05, 0.1), p_dot {:.6g} against {:.6g}, r_dot {:.6g} "
      "against {:.6g}\n",
      flipped.perturbed.at(p_dot), f16_mass.perturbed.at(p_dot), flipped.perturbed.at(r_dot),
      f16_mass.perturbed.at(r_dot));

  using Band = std::remove_cvref_t<decltype(nesc::case11::kBand)>;
  f16::Flight flight{planted};
  const f16::FlightResult result =
      flight.fly("case11 Izx sign", nesc::case11::kBand, {.mode = f16::Mode::kControl});
  f16::print_summary("case11 Izx sign (reported, no band asserted)", nesc::case11::kBand, result);
  expect_no_halt_before_the_window<Band>("case11 Izx sign", result);
  EXPECT_FALSE(result.halted) << "case11 Izx sign: halted at sample " << result.halt_sample;
  EXPECT_EQ(result.samples_compared, Band::kSampleCount) << "case11 Izx sign must complete";
}

TEST(NescF16Control, Case11ElevatorSignFailsForceZ) {
  expect_fails_band("case11 elevator sign", nesc::case11::kBand, Config::from_file(case11_path()),
                    f16::kForceZ, "force_z", [](Simulation& sim) {
                      const double* elevator = sim.registry().find("v0.controls.elevator");
                      ASSERT_NE(elevator, nullptr);
                      const double trimmed = *elevator;
                      sim.write_input("controls.elevator", -trimmed);
                      std::cout << std::format(
                          "case11 elevator sign: {:.17g} rad written as {:.17g}\n", trimmed,
                          *elevator);
                    });
}

TEST(NescF16Control, Case11ZeroInertialTrimFailsRateQ) {
  expect_fails_band("case11 zero-inertial trim", nesc::case11::kBand,
                    Config::from_file(zero_inertial_path("case11_f16_zero_inertial_trim.yaml")),
                    f16::kRateQ, "rate_q");
}

TEST(NescF16Control, Case12ZeroInertialTrimFailsRateQ) {
  expect_fails_band("case12 zero-inertial trim", nesc::case12::kBand,
                    Config::from_file(zero_inertial_path("case12_f16_zero_inertial_trim.yaml")),
                    f16::kRateQ, "rate_q");
}
