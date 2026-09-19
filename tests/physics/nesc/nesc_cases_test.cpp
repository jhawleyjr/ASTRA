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
// Cases 4 and 5 (PLAN.md M2 PR 3c, docs/plans/pr3c-substage-wrench.md): were GTEST_SKIP()ed, not
// deleted and not tolerance-widened, from 2026-09-18 (Joe's explicit decision, accepting
// docs/plans/nesc-harness.md's "Known gap found during implementation" section as a disclosed gap)
// until PR 3c's own dry-run evidence showed the gap closed. A body spinning at Vol II Tables
// 28/29's 10-30 deg/s under real drag exposed the underlying EOM/architecture characteristic (the
// applied wrench was frozen across one frame's four RK4 sub-stages while gravity was recomputed
// fresh each sub-stage), producing a measurable lateral velocity/attitude drift NASA's own six-tool
// ensemble does not show. PR 3c evaluates the wrench (and the atmosphere sample it reads) fresh at
// each sub-stage instead, closing the gap `dt`-independently -- see the plan document for the full
// design, the rejected "just reduce dt" alternative (measured, not merely argued, to not actually
// close it even at several times the cost), and the ADR-0016 draft this PR's dry run is gated on.
//
// The `wrench_total.*` signals below (read via resolve_aero_signals) are core-owned (Vehicle,
// PLAN.md M2 PR 3c), not `nesc_aero`'s own: `nesc_aero` no longer self-publishes at all, since a
// per-call mutable publish inside a now-4x/frame-called wrench_b() would be exactly the "hidden
// per-call state" wrench_source.hpp's own purity contract forbids. The core publishes only the
// frame-start (k1) stage's total, matching the pre-step-sampling convention below exactly (see R12,
// WrenchTotalSignalReflectsFrameStartStageOnly, for the test proving this specifically).
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
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
  // ADR-0015: the pass criterion is |actual - reference| <= tol_abs alone -- there is no
  // relative term.
  const double tolerance = band.tol_abs;
  EXPECT_LE(std::abs(diff), tolerance)
      << label << " sample " << sample << " (t=" << (sample * kSampleDt) << " s): actual=" << actual
      << " reference=" << band.reference[sample] << " tol_abs=" << band.tol_abs
      << " tolerance=" << tolerance << " diff=" << diff;
}

struct AeroSignals {
  const double* force_x = nullptr;
  const double* force_y = nullptr;
  const double* force_z = nullptr;
  const double* moment_l = nullptr;
  const double* moment_m = nullptr;
  const double* moment_n = nullptr;
};

// Resolves the vehicle's core-owned wrench_total signals once (SignalRegistry::find is an
// init-time/test-only call, signal_registry.hpp's own comment -- the per-frame path never does
// this), not once per sample. PLAN.md M2 PR 3c: these replace nesc_aero's own former self-publish
// (v0.nesc_aero.force_b_*/moment_b_*, removed) -- see this file's own header comment.
AeroSignals resolve_aero_signals(const Simulation& sim, const std::string& case_name) {
  AeroSignals aero;
  aero.force_x = sim.registry().find("v0.wrench_total.force_b_x");
  aero.force_y = sim.registry().find("v0.wrench_total.force_b_y");
  aero.force_z = sim.registry().find("v0.wrench_total.force_b_z");
  aero.moment_l = sim.registry().find("v0.wrench_total.moment_b_l");
  aero.moment_m = sim.registry().find("v0.wrench_total.moment_b_m");
  aero.moment_n = sim.registry().find("v0.wrench_total.moment_b_n");
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

  // R12 (PLAN.md M2 PR 3c, docs/plans/pr3c-substage-wrench.md): an independently-loaded nesc_aero
  // instance, called directly against each sample's own preserved pre-step state, proves the
  // core-published wrench_total signal genuinely reflects the frame-start (k1) stage and not
  // k2/k3/k4 -- not merely reviewed as such. Same config the case's own vehicle.models entry uses
  // (config.at("vehicle.models").at(0), the exact node Vehicle::init itself would resolve), loaded
  // once, outside the sample loop, since the config never changes across samples.
  astra::SignalRegistry independent_registry;
  astra::PluginLoader independent_loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin independent_plugin;
  astra::WrenchSource* independent_aero = nullptr;
  if (has_aero) {
    independent_plugin = independent_loader.load("tests/plugins/nesc_aero");
    independent_aero = dynamic_cast<astra::WrenchSource*>(independent_plugin.model.get());
    ASSERT_NE(independent_aero, nullptr) << case_name
                                         << ": tests/plugins/nesc_aero does not "
                                            "implement WrenchSource";
    independent_aero->init(config.at("vehicle.models").at(0), independent_registry);
  }

  for (int sample = 0; sample < nesc::kSampleCount; ++sample) {
    const std::string label = case_name + " ";

    // State as of this sample's own frame, read BEFORE the step that computes this frame's wrench
    // -- the pre-step sampling fix (this file's own header comment).
    const StateVector& y = sim.vehicles().front().state();
    // R12's own explicit copy, taken before sim.run(1) below mutates y (a reference into the live
    // Vehicle::state()) in place -- reusing y itself after that call would compare against the
    // post-step state, not the pre-step one wrench_b() actually saw (PR 3c plan critique).
    const StateVector pre_step_state = y;
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

    // R12: env here is still this same frame's frame-start EnvironmentSample -- Simulation::step()
    // latches it once, before the wrench stage, and does not touch it again until the *next*
    // Simulation::step() call, so it is exactly the (pre_step_state, env) pair wrench_b()'s own k1
    // evaluation saw. Comparing an independent, direct wrench_b() call at that same pair against
    // the sim's own published wrench_total signal is R12's whole point: if the core ever started
    // publishing k2/k3/k4 instead, this would catch it as a real, non-negligible mismatch, not a
    // floating-point rounding difference.
    if (has_aero) {
      const astra::Wrench independent =
          independent_aero->wrench_b(astra::VehicleState{pre_step_state}, env);
      constexpr double kPublishTolerance = 1e-12;
      EXPECT_NEAR(independent.force_b(0), *aero.force_x, kPublishTolerance)
          << label << "sample " << sample
          << ": v0.wrench_total.force_b_x must be the frame-start "
             "(k1) stage's own wrench_b(), not a later stage's";
      EXPECT_NEAR(independent.force_b(1), *aero.force_y, kPublishTolerance)
          << label << "sample " << sample << ": wrench_total.force_b_y";
      EXPECT_NEAR(independent.force_b(2), *aero.force_z, kPublishTolerance)
          << label << "sample " << sample << ": wrench_total.force_b_z";
      EXPECT_NEAR(independent.moment_b(0), *aero.moment_l, kPublishTolerance)
          << label << "sample " << sample << ": wrench_total.moment_b_l";
      EXPECT_NEAR(independent.moment_b(1), *aero.moment_m, kPublishTolerance)
          << label << "sample " << sample << ": wrench_total.moment_b_m";
      EXPECT_NEAR(independent.moment_b(2), *aero.moment_n, kPublishTolerance)
          << label << "sample " << sample << ": wrench_total.moment_b_n";
    }

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

// PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md) closes the frozen-wrench-across-RK4-
// sub-stages gap the two tests below used to GTEST_SKIP() for: the aerodynamic wrench (and the
// atmosphere sample it reads) is now evaluated fresh at each of the four RK4 sub-stages, from that
// stage's own state, instead of once at frame-start and reused across all four.
TEST(Nesc, Case04DroppedSphereRoundNonRotation) {
  run_nesc_case("case04", case_config_path("case04_dropped_sphere_round_non_rotation.yaml"),
                nesc::case04::kBand, /*has_aero=*/true);
}

TEST(Nesc, Case05DroppedSphereRoundRotation) {
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
