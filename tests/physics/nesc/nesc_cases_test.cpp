// NASA/TM-2015-218675 atmospheric check-case harness (ADR-0013, ADR-0014, PLAN.md M2 PR 3b): all
// ten cases against per-channel oracle bands generated offline by
// tests/golden/nesc/generate_nesc_band.py. Absorbs PR 1's single-case nesc_case1_test.cpp
// (deleted): case 1 is now case01 below, checked against the six-tool ensemble band like every
// other case, not against an independent DOP853 integration.
//
// Sampling (ADR-0013's "sampling fix", PLAN.md's own settled bullet): wrench_b is evaluated on the
// PRE-step state, so comparing at t = n*0.1 s means reading state, then sim.run(1) (using the
// just-read pre-step state, which is what this frame's wrench/environment reflect), then reading
// the wrench/environment signals that single frame just computed, then sim.run(99) to the next
// sample (skipped after the last sample) -- 30,001 frames per case, not 30,000.
//
// Frame conventions (Joe's plan review; verified by an independent Codex plan critique, no defect
// found): NESC's feVelocity columns are NED, not body-axis, despite the "fe" prefix -- ASTRA's own
// v_b (body axes, ADR-0001) is rotated to NED via frames::rotate_b_to_n before comparison.
// bodyAngularRateWrtEi is already inertial-referenced and compares directly against ASTRA's
// omega_b. Yaw is compared on the wrapped difference (wrap_to_pi below), never a plain subtraction,
// and its band carries no relative term at all (generate_nesc_band.py's own header comment has the
// full reasoning).
//
// Cases 4 and 5 are GTEST_SKIP()ed, not deleted and not tolerance-widened (Joe's explicit decision,
// 2026-09-18, accepting docs/plans/nesc-harness.md's "Known gap found during implementation"
// section as a disclosed gap): a body spinning at Vol II Tables 28/29's 10-30 deg/s under real drag
// exposes a real EOM/architecture characteristic (the applied wrench is frozen across one frame's
// four RK4 sub-stages while gravity is recomputed fresh each sub-stage), producing a measurable
// lateral velocity/attitude drift NASA's own six-tool ensemble does not show. PLAN.md's M2 PR 3c
// (per-RK4- substage wrench evaluation, its own plan-reviewed ADR) is the closer for this gap; each
// skipped test's own run_nesc_case call is left in place, unreached, so the exact comparison this
// gap blocks stays visible in the source rather than silently vanishing.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "astra/config/config.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "golden/nesc/case01_dropped_sphere_band.hpp"
#include "golden/nesc/case02_tumbling_brick_no_damping_band.hpp"
#include "golden/nesc/case03_tumbling_brick_damping_band.hpp"
#include "golden/nesc/case04_dropped_sphere_round_non_rotation_band.hpp"
#include "golden/nesc/case05_dropped_sphere_round_rotation_band.hpp"
#include "golden/nesc/case06_dropped_sphere_ellipsoidal_no_wind_band.hpp"
#include "golden/nesc/case07_dropped_sphere_steady_wind_band.hpp"
#include "golden/nesc/case08_dropped_sphere_2d_wind_shear_band.hpp"
#include "golden/nesc/case09_eastward_cannonball_band.hpp"
#include "golden/nesc/case10_northward_cannonball_band.hpp"
#include "golden/nesc/nesc_band_common.hpp"

namespace {

using astra::Config;
using astra::Simulation;
using astra::StateVector;
namespace nesc = astra::test::nesc;

constexpr double kSampleDt = 0.1;

// Shortest signed angular distance representation of x, in (-pi, pi] -- the same wrap the generator
// applies on its own side (generate_nesc_band.py's wrap_to_pi), so a yaw difference straddling the
// +/-180 deg boundary compares correctly instead of reading as a ~360 deg outlier.
double wrap_to_pi(double x) noexcept {
  double wrapped = std::fmod(x + std::numbers::pi, 2.0 * std::numbers::pi);
  if (wrapped <= 0.0) {
    wrapped += 2.0 * std::numbers::pi;
  }
  return wrapped - std::numbers::pi;
}

void expect_in_band(double actual, const nesc::ChannelBand& band, int sample, bool is_yaw,
                    const std::string& label) {
  double diff = actual - band.reference[sample];
  if (is_yaw) {
    diff = wrap_to_pi(diff);
  }
  const double tolerance = band.tol_abs + band.tol_rel * std::abs(band.reference[sample]);
  EXPECT_LE(std::abs(diff), tolerance)
      << label << " sample " << sample << " (t=" << (sample * kSampleDt) << " s): actual=" << actual
      << " reference=" << band.reference[sample] << " tol_abs=" << band.tol_abs
      << " tol_rel=" << band.tol_rel << " tolerance=" << tolerance << " diff=" << diff;
}

struct AeroSignals {
  const double* force_x = nullptr;
  const double* force_y = nullptr;
  const double* force_z = nullptr;
  const double* moment_l = nullptr;
  const double* moment_m = nullptr;
  const double* moment_n = nullptr;
};

// Resolves nesc_aero's registered signals once (SignalRegistry::find is an init-time/test-only
// call, signal_registry.hpp's own comment -- the per-frame path never does this), not once per
// sample.
AeroSignals resolve_aero_signals(const Simulation& sim, const std::string& case_name) {
  AeroSignals aero;
  aero.force_x = sim.registry().find("v0.nesc_aero.force_b_x");
  aero.force_y = sim.registry().find("v0.nesc_aero.force_b_y");
  aero.force_z = sim.registry().find("v0.nesc_aero.force_b_z");
  aero.moment_l = sim.registry().find("v0.nesc_aero.moment_b_l");
  aero.moment_m = sim.registry().find("v0.nesc_aero.moment_b_m");
  aero.moment_n = sim.registry().find("v0.nesc_aero.moment_b_n");
  for (const double* signal :
       {aero.force_x, aero.force_y, aero.force_z, aero.moment_l, aero.moment_m, aero.moment_n}) {
    EXPECT_NE(signal, nullptr) << case_name << ": nesc_aero signal not found";
  }
  return aero;
}

void run_nesc_case(const std::string& case_name, const std::string& config_path,
                   const nesc::CaseBand& band, bool has_aero) {
  const Config config = Config::from_file(config_path);
  Simulation sim = Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);

  const AeroSignals aero = has_aero ? resolve_aero_signals(sim, case_name) : AeroSignals{};

  for (int sample = 0; sample < nesc::kSampleCount; ++sample) {
    const std::string label = case_name + " ";

    // State as of this sample's own frame, read BEFORE the step that computes this frame's wrench
    // -- the pre-step sampling fix (this file's own header comment).
    const StateVector& y = sim.vehicles().front().state();
    const Eigen::Vector3d p_n = y.segment<astra::state::kPositionSize>(astra::state::kPosition);
    const Eigen::Vector3d v_b = y.segment<astra::state::kVelocitySize>(astra::state::kVelocity);
    const astra::frames::Quaternion q_nb =
        y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion);
    const Eigen::Vector3d omega_b =
        y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate);

    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(q_nb, v_b);
    const astra::frames::EulerAngles euler = astra::frames::euler_from_quaternion(q_nb);

    expect_in_band(p_n(0), band.latitude, sample, false, label + "latitude");
    expect_in_band(p_n(1), band.longitude, sample, false, label + "longitude");
    expect_in_band(p_n(2), band.altitude, sample, false, label + "altitude");
    expect_in_band(v_n(0), band.v_north, sample, false, label + "v_north");
    expect_in_band(v_n(1), band.v_east, sample, false, label + "v_east");
    expect_in_band(v_n(2), band.v_down, sample, false, label + "v_down");
    expect_in_band(euler(0), band.yaw, sample, true, label + "yaw");
    expect_in_band(euler(1), band.pitch, sample, false, label + "pitch");
    expect_in_band(euler(2), band.roll, sample, false, label + "roll");
    expect_in_band(omega_b(0), band.rate_p, sample, false, label + "rate_p");
    expect_in_band(omega_b(1), band.rate_q, sample, false, label + "rate_q");
    expect_in_band(omega_b(2), band.rate_r, sample, false, label + "rate_r");

    const astra::HaltReason halt = sim.run(1);
    ASSERT_FALSE(halt) << label << "halted computing sample " << sample << "'s wrench/environment";

    if (has_aero) {
      expect_in_band(*aero.force_x, band.force_x, sample, false, label + "force_x");
      expect_in_band(*aero.force_y, band.force_y, sample, false, label + "force_y");
      expect_in_band(*aero.force_z, band.force_z, sample, false, label + "force_z");
      expect_in_band(*aero.moment_l, band.moment_l, sample, false, label + "moment_l");
      expect_in_band(*aero.moment_m, band.moment_m, sample, false, label + "moment_m");
      expect_in_band(*aero.moment_n, band.moment_n, sample, false, label + "moment_n");
    }

    const astra::EnvironmentSample& env = sim.vehicles().front().environment();
    expect_in_band(env.atmosphere.density_kg_m3, band.density, sample, false, label + "density");
    expect_in_band(env.atmosphere.pressure_pa, band.pressure, sample, false, label + "pressure");
    expect_in_band(env.atmosphere.temperature_k, band.temperature, sample, false,
                   label + "temperature");

    if (sample < nesc::kSampleCount - 1) {
      const astra::HaltReason step_halt = sim.run(99);
      ASSERT_FALSE(step_halt) << label << "halted advancing past sample " << sample;
    }
  }
}

std::string case_config_path(std::string_view file_name) {
  return std::string{ASTRA_TEST_NESC_CASE_DIR} + "/" + std::string{file_name};
}

}  // namespace

TEST(Nesc, Case01DroppedSphere) {
  run_nesc_case("case01", case_config_path("case01_dropped_sphere.yaml"), nesc::case01::kBand,
                /*has_aero=*/false);
}

TEST(Nesc, Case02TumblingBrickNoDamping) {
  run_nesc_case("case02", case_config_path("case02_tumbling_brick_no_damping.yaml"),
                nesc::case02::kBand,
                /*has_aero=*/true);
}

TEST(Nesc, Case03TumblingBrickDamping) {
  run_nesc_case("case03", case_config_path("case03_tumbling_brick_damping.yaml"),
                nesc::case03::kBand,
                /*has_aero=*/true);
}

TEST(Nesc, Case04DroppedSphereRoundNonRotation) {
  GTEST_SKIP()
      << "Known gap, disclosed not fixed (docs/plans/nesc-harness.md's \"Known gap found "
         "during implementation\" section, accepted by Joe 2026-09-18): the aerodynamic "
         "wrench is frozen across one frame's four RK4 sub-stages while gravity is "
         "recomputed fresh each sub-stage; this case's real C_D combined with Vol II Table "
         "28's 10-30 deg/s initial spin produces a growing v_east/longitude/attitude drift "
         "NASA's own six-tool ensemble does not show. Closed by PLAN.md's M2 PR 3c "
         "(per-RK4-substage wrench evaluation), not this PR.";
  run_nesc_case("case04", case_config_path("case04_dropped_sphere_round_non_rotation.yaml"),
                nesc::case04::kBand, /*has_aero=*/true);
}

TEST(Nesc, Case05DroppedSphereRoundRotation) {
  GTEST_SKIP() << "Known gap, disclosed not fixed (docs/plans/nesc-harness.md's \"Known gap found "
                  "during implementation\" section, accepted by Joe 2026-09-18): the same "
                  "frozen-wrench-across-RK4-sub-stages characteristic as case 4 (Vol II Table 29's "
                  "10-30 deg/s spin plus real C_D). Closed by PLAN.md's M2 PR 3c "
                  "(per-RK4-substage wrench evaluation), not this PR.";
  run_nesc_case("case05", case_config_path("case05_dropped_sphere_round_rotation.yaml"),
                nesc::case05::kBand, /*has_aero=*/true);
}

TEST(Nesc, Case06DroppedSphereEllipsoidalNoWind) {
  run_nesc_case("case06", case_config_path("case06_dropped_sphere_ellipsoidal_no_wind.yaml"),
                nesc::case06::kBand, /*has_aero=*/true);
}

TEST(Nesc, Case07DroppedSphereSteadyWind) {
  run_nesc_case("case07", case_config_path("case07_dropped_sphere_steady_wind.yaml"),
                nesc::case07::kBand,
                /*has_aero=*/true);
}

TEST(Nesc, Case08DroppedSphere2DWindShear) {
  run_nesc_case("case08", case_config_path("case08_dropped_sphere_2d_wind_shear.yaml"),
                nesc::case08::kBand,
                /*has_aero=*/true);
}

TEST(Nesc, Case09EastwardCannonball) {
  run_nesc_case("case09", case_config_path("case09_eastward_cannonball.yaml"), nesc::case09::kBand,
                /*has_aero=*/true);
}

TEST(Nesc, Case10NorthwardCannonball) {
  run_nesc_case("case10", case_config_path("case10_northward_cannonball.yaml"), nesc::case10::kBand,
                /*has_aero=*/true);
}
