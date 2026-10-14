// NASA/TM-2015-218675 atmospheric check-cases 13.1 to 13.4, the F-16 flown closed loop by the
// sim-side F16_control peer (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md; the M7 plan's R4 13.x
// half and R6, Q11's and Q12's decided choices, Q27's 13.4 logic). The configs are
// aircraft/f16/configs/case13p*.yaml: case 11's trim, with the peer at divisor 10
// (docs/reviews/evidence/m7-pr11a-trim/).
//
// NescF16.Case13p{1,2,3,4}ClosedLoop (R6) fly each config against ADR-0028's band through
// f16_flight.hpp: every one of the 21 channels is asserted at every sample from
// Band::kFirstGatedSample on, and the samples before it are logged, never asserted. No channel is
// excluded (the plan's D7: ADR-0028 item 2's footnotes exclude none). A zero-accepting channel is
// asserted too, but the log marks it, and it is never cited as evidence. At every sample, the
// plan's per-sample guards (amendment 8), each its own expectation:
// - G1, Q5's route: aero plus engine equals v0.wrench_total.* (f16_flight.hpp, 1e-12 normwise);
// - G2, EAS in flight (Q3): transport.peer.equivalent_airspeed against |v_b| sqrt(rho / rho0), rho
//   the latched environment's density, rho0 = p0 M0 / (R* T0) from icd_pack, to 1e-12 relative;
// - G3, 13.4's cross-track: transport.peer.lateral_deviation_error against this file's own
//   evaluation of ADR-0029 Decision 6 from the sample's pre-step p_n (the ICD's lat and lon are
//   that p_n, copied), the base point from the frame-0 state, to 1e-12 max(|expected|, 609.6 m),
//   and exactly 0 before the step (and in 13.1 to 13.3, at every sample);
// - G4, zero compute delay: the latched controls.* equal the recorder's reply to the sample's own
//   exchange, bit for bit;
// - G5, D1's literal: at frame 0, the peer's EAS against the config's
//   equivalent_airspeed_knots, to 1e-9 relative (Joe's amendment 6).
// A miss in any of them goes to Joe.
//
// NescF16.Case13p{1,2,3,4}TrimResiduals (R4; the plan's Q1): Simulation::derivative at each
// config's initial state, case 11's trim. The expected lateral residuals are
// docs/reviews/evidence/m7-pr11a-residual/derive_residual.log's totals at each config's literals,
// which are case 11's bit for bit, with PR 9's pre-registered bounds.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "astra/config/config.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/geodesy.hpp"
#include "astra/util/icd_pack.hpp"
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

// derive_residual.log's totals at the configs' literals (%.17g), case 11's, and PR 9's bounds.
constexpr std::array<f16::LateralResidual, 3> kResiduals = {{
    {"v_dot_y", astra::state::kVelocity + 1, 0.017171068467368312, 9.7888e-12},
    {"p_dot", astra::state::kAngularRate, -9.8888285577672511e-06, 9.8858e-18},
    {"r_dot", astra::state::kAngularRate + 2, 5.9976661226860661e-06, 5.9955e-18},
}};

bool same_bits(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// The guards' largest misses, printed with the D3 summary.
struct GuardStats {
  double eas_relative = 0.0;
  double cross_track_ratio = 0.0;
  int g4_mismatches = 0;
  double frame0_eas_relative = 0.0;
};

// Flies one 13.x config with the guards (the file's header comment).
template <typename Band>
void fly_closed_loop(std::string_view name, std::string_view file, const Band& band,
                     bool side_step) {
  using B = std::remove_cvref_t<Band>;
  const Config config = Config::from_file(config_path(file));
  f16::Flight flight{config};
  astra::Simulation& sim = flight.sim();
  const double* eas = sim.registry().find("transport.peer.equivalent_airspeed");
  const double* deviation = sim.registry().find("transport.peer.lateral_deviation_error");
  ASSERT_NE(eas, nullptr);
  ASSERT_NE(deviation, nullptr);
  const std::array<const double*, 4> latched = {
      sim.registry().find("v0.controls.elevator"), sim.registry().find("v0.controls.aileron"),
      sim.registry().find("v0.controls.rudder"), sim.registry().find("v0.controls.throttle")};
  for (const double* p : latched) {
    ASSERT_NE(p, nullptr);
  }
  const double rho0 = astra::icd_pack::kSeaLevelPressure * astra::icd_pack::kMeanMolecularWeight /
                      (astra::icd_pack::kGasConstant * astra::icd_pack::kSeaLevelTemperature);
  const double eas_literal = units::knots_to_meters_per_second(
      config.number("transport.peer.commands.equivalent_airspeed_knots"));
  const double offset = units::feet_to_meters(2000.0);
  const double chi = units::degrees_to_radians(45.0);
  constexpr std::uint64_t kStepFrame = 20000;

  GuardStats stats;
  Eigen::Vector3d base = Eigen::Vector3d::Zero();
  f16::FlightOptions options;
  options.on_sample = [&](int k, const astra::StateVector& pre_step,
                          const astra::EnvironmentSample& env) {
    const std::uint64_t frame = f16::kFramesPerSample * static_cast<std::uint64_t>(k);
    const Eigen::Vector3d p_n =
        pre_step.segment<astra::state::kPositionSize>(astra::state::kPosition);
    const Eigen::Vector3d v_b =
        pre_step.segment<astra::state::kVelocitySize>(astra::state::kVelocity);
    if (k == 0) {
      base = p_n;
      // G5.
      const double rel = std::abs(*eas - eas_literal) / eas_literal;
      EXPECT_LE(rel, 1e-9) << name << ": the peer's frame-0 EAS against the config's literal";
      stats.frame0_eas_relative = rel;
    }
    // G2.
    const double expected_eas = v_b.norm() * std::sqrt(env.atmosphere.density_kg_m3 / rho0);
    const double eas_rel = std::abs(*eas - expected_eas) / expected_eas;
    EXPECT_LE(eas_rel, 1e-12) << name << " sample " << k
                              << std::format(": EAS {:.17g} against {:.17g}", *eas, expected_eas);
    stats.eas_relative = std::max(stats.eas_relative, eas_rel);
    // G3.
    double expected_deviation = 0.0;
    if (side_step && frame >= kStepFrame) {
      const double north_scale = geodesy::meridian_radius(base(0)) + base(2);
      const double east_scale =
          (geodesy::prime_vertical_radius(base(0)) + base(2)) * std::cos(base(0));
      const double dn = (p_n(0) - base(0)) * north_scale;
      const double de = (p_n(1) - base(1)) * east_scale;
      expected_deviation = (-dn * std::sin(chi)) + (de * std::cos(chi)) - offset;
      const double bound = 1e-12 * std::max(std::abs(expected_deviation), offset);
      const double miss = std::abs(*deviation - expected_deviation);
      EXPECT_LE(miss, bound) << name << " sample " << k
                             << std::format(": cross-track {:.17g} against {:.17g}", *deviation,
                                            expected_deviation);
      stats.cross_track_ratio = std::max(stats.cross_track_ratio, miss / bound);
    } else {
      EXPECT_EQ(*deviation, 0.0) << name << " sample " << k;
    }
    // G4.
    const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
    const astra_sw_to_sim_v1& reply = recorder->replies().back();
    EXPECT_EQ(reply.frame, frame) << name << " sample " << k;
    const bool bits =
        same_bits(*latched[0], reply.elevator) && same_bits(*latched[1], reply.aileron) &&
        same_bits(*latched[2], reply.rudder) && same_bits(*latched[3], reply.throttle[0]) &&
        same_bits(reply.throttle[0], reply.throttle[1]);
    EXPECT_TRUE(bits) << name << " sample " << k << ": the latched controls against the reply";
    stats.g4_mismatches += bits ? 0 : 1;
  };

  const f16::FlightResult result = flight.fly(name, band, options);
  f16::print_summary(name, band, result);
  std::cout << std::format(
      "{} guards: G2 EAS largest relative miss {:.3g} (1e-12); G3 cross-track largest |d| / bound "
      "{:.3g}; G4 latched-against-reply mismatches {}; G5 frame-0 EAS against the literal {:.3g} "
      "(1e-9)\n",
      name, stats.eas_relative, stats.cross_track_ratio, stats.g4_mismatches,
      stats.frame0_eas_relative);
  EXPECT_EQ(result.samples_compared, B::kSampleCount) << name;
  // 100 (N - 1) + 1 frames, which is the config's sim.frames (the plan's D6).
  const auto frames = (f16::kFramesPerSample * (B::kSampleCount - 1)) + 1;
  EXPECT_EQ(result.frames, frames) << name;
  EXPECT_EQ(config.integer("sim.frames"), static_cast<std::int64_t>(frames)) << name;
}

}  // namespace

TEST(NescF16, Case13p1TrimResiduals) {
  f16::check_trim_residuals(config_path("case13p1_subsonic_altitude_change.yaml"), "case13p1",
                            kResiduals);
}

TEST(NescF16, Case13p2TrimResiduals) {
  f16::check_trim_residuals(config_path("case13p2_subsonic_airspeed_change.yaml"), "case13p2",
                            kResiduals);
}

TEST(NescF16, Case13p3TrimResiduals) {
  f16::check_trim_residuals(config_path("case13p3_subsonic_heading_change.yaml"), "case13p3",
                            kResiduals);
}

TEST(NescF16, Case13p4TrimResiduals) {
  f16::check_trim_residuals(config_path("case13p4_subsonic_lateral_side_step.yaml"), "case13p4",
                            kResiduals);
}

TEST(NescF16, Case13p1ClosedLoop) {
  fly_closed_loop("case13p1", "case13p1_subsonic_altitude_change.yaml", nesc::case13p1::kBand,
                  false);
}

TEST(NescF16, Case13p2ClosedLoop) {
  fly_closed_loop("case13p2", "case13p2_subsonic_airspeed_change.yaml", nesc::case13p2::kBand,
                  false);
}

TEST(NescF16, Case13p3ClosedLoop) {
  fly_closed_loop("case13p3", "case13p3_subsonic_heading_change.yaml", nesc::case13p3::kBand,
                  false);
}

TEST(NescF16, Case13p4ClosedLoop) {
  fly_closed_loop("case13p4", "case13p4_subsonic_lateral_side_step.yaml", nesc::case13p4::kBand,
                  true);
}
