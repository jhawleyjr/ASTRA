// tests/physics/ (docs/architecture.md §16 row M2, PLAN.md M2 PR 2b): "Dryden PSD matches
// MIL-F-8785C within statistical bounds at a fixed seed." Labeled "physics", not "unit" -- the Stop
// hook's `ctest -L unit` (fast subset) skips this; `just check`'s unlabeled `ctest` still runs it
// under every preset.
//
// Drives plugins/physical/wind/dryden directly (PluginLoader, no Simulation, no EarthModel) with a
// SYNTHETIC, HELD-CONSTANT VehicleState across the whole run: PLAN.md settles this explicitly --
// MIL-F-8785C's PSD is a stationary-process spectrum, meaningful only at fixed V and h, so driving
// a real (changing) trajectory would test a nonstationary process against a stationary-process
// tolerance. q_nb is the identity, so wind_n() (NED) is numerically identical to the body-axis gust
// (u_g, v_g, w_g) dryden.cpp computes -- this test reads wind_n() directly as the per-axis gust
// series, with no rotation to undo.
//
// --- Welch periodogram estimator (see periodogram_average's own comment for the exact scaling) ---
//
// Estimator self-checked FIRST, against raw white noise (astra::random::Rng::normal(), unit
// variance, independent of Dryden's own Rng) whose one-sided PSD in this convention is EXACTLY
// dt/pi, flat across frequency -- if the self-check fails, the estimator itself is wrong and the
// Dryden rows below are not evidence of anything. Only once the self-check passes are the three
// gust axes checked against MIL-F-8785C's Phi_u/Phi_v/Phi_w, section 3.7.1.2 p.47 --
// tests/golden/wind/mil_f_8785c_dryden.hpp's phi_u()/phi_vw(), called below, not restated here
// (Codex review, PR #26: these two closed forms transcribe the same guarded section the constants
// in that header do, and had sat outside every guarded path when this file defined them itself).
//
// --- Grid bounds, and why (Joe's review) ---
//
// The exact discrete-time step reproduces the continuous filter's autocorrelation only AT THE
// SAMPLE INSTANTS, so the sampled process's spectrum is the continuous spectrum ALIASED to
// [-pi/dt, pi/dt], not the raw continuous Phi(omega) itself. Phi_u/Phi_w fall as omega^-2 at high
// frequency, so near Nyquist the aliased sum of the spectrum's images sits DETERMINISTICALLY above
// the un-aliased MIL formula -- not estimator noise, and no amount of averaging removes it. The
// grid top is capped at a stated fraction of Nyquist (omega_max = 0.1 * pi/dt) so the nearest alias
// image (at 2*pi/dt - omega, always beyond kNyquistFraction's own reach at this cap) contributes
// well below the statistical tolerance. The grid bottom stays at several multiples of the segment's
// own frequency resolution (2*pi/T_segment), since a bin near omega -> 0 is dominated by the
// segment window's own leakage, not signal. If a frequency near either edge fails, the fix is more
// segments or a tighter grid bound -- never a wider tolerance (AGENTS.md: narrowing/widening a
// check is a blocking review finding).
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/random.hpp"
#include "astra/util/units.hpp"
#include "golden/wind/mil_f_8785c_dryden.hpp"
#include "plugins/physical/wind/dryden/dryden_filter.hpp"

namespace {

using astra::Config;
using astra::SignalRegistry;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::WindModel;
namespace golden = astra::test::mil_f_8785c;

// --- Fixed test scenario: low altitude, moderate airspeed -- keeps L/V short (T_u ~ 1 s,
// T_w ~ 0.15 s) so a run of practical length covers many correlation times per axis. ---
constexpr double kDt = 0.001;
constexpr double kAirspeedMS = 100.0;
constexpr double kAltitudeFt = 50.0;
constexpr double kAltitudeM = kAltitudeFt * 0.3048;
constexpr std::int64_t kSeed = 20260916;
constexpr double kW20Knots = 20.0;
constexpr double kSigmaHigh = 5.0;

constexpr int kSegmentLength = 20'000;  // 20 s/segment at kDt
constexpr int kSegmentCount = 50;       // 50 segments -> 1,000,000 samples total
constexpr int kTotalFrames = kSegmentLength * kSegmentCount;

// chi^2_{2K} sampling distribution of a K-segment Welch average: relative std ~ 1/sqrt(K). 3.5x
// that is a generous (not flake-prone at a fixed seed), not-fitted-to-the-observed-numbers bound.
// 7.0710678118654755 == sqrt(50) == sqrt(kSegmentCount).
constexpr double kFrequencyRelativeTolerance = 3.5 / 7.0710678118654755;

// Autocorrelated samples (a gust filter's own time constant spans many raw samples) make the
// EFFECTIVE sample count for a time-domain variance far smaller than kTotalFrames itself, so this
// bound is a deliberately loose, physically-reasoned constant, not a 1/sqrt(N) statistical formula
// over the raw (correlated) sample count.
constexpr double kVarianceRelativeTolerance = 0.20;

std::unique_ptr<WindModel> load_dryden() {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("plugins/physical/wind/dryden");
  auto* const wind = dynamic_cast<WindModel*>(loaded.model.get());
  SignalRegistry registry;
  const std::string yaml = "{ seed: " + std::to_string(kSeed) +
                           ", w20_knots: " + std::to_string(kW20Knots) +
                           ", sigma_high: " + std::to_string(kSigmaHigh) + " }\n";
  wind->init(Config::from_string(yaml, "dryden-psd-fixture"), registry);
  // Ownership transferred out only after init() succeeds -- see dryden_test.cpp's own load()
  // comment for why releasing the raw pointer BEFORE a call that can throw would leak it.
  return std::unique_ptr<WindModel>{static_cast<WindModel*>(loaded.model.release())};
}

struct GustSeries {
  std::vector<double> u;
  std::vector<double> v;
  std::vector<double> w;
};

GustSeries run_dryden(int frames) {
  std::unique_ptr<WindModel> wind = load_dryden();
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity: wind_n() == body-axis gust, no rotation to undo
  y(astra::state::kVelocity) = kAirspeedMS;
  const VehicleState vs{y};

  GustSeries series;
  series.u.reserve(static_cast<std::size_t>(frames));
  series.v.reserve(static_cast<std::size_t>(frames));
  series.w.reserve(static_cast<std::size_t>(frames));
  for (int frame = 0; frame < frames; ++frame) {
    const StepContext ctx{static_cast<double>(frame) * kDt, kDt, static_cast<std::uint64_t>(frame)};
    wind->step(ctx);
    wind->begin_frame(ctx, vs, kAltitudeM);
    const Eigen::Vector3d gust = wind->wind_n(ctx, vs);
    series.u.push_back(gust(0));
    series.v.push_back(gust(1));
    series.w.push_back(gust(2));
  }
  return series;
}

std::vector<double> white_noise_series(int frames, std::uint64_t seed) {
  astra::random::Rng rng{seed};
  std::vector<double> series;
  series.reserve(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    series.push_back(rng.normal());
  }
  return series;
}

// Welch-averaged one-sided periodogram at angular frequency omega (rad/s). Derivation: for a
// segment of N samples x_0..x_{N-1} at spacing dt,
//   I(omega) = (dt / (pi*N)) * |sum_k x_k * exp(-i*omega*k*dt)|^2
// estimates the same one-sided Phi(omega) convention MIL-F-8785C's spectra use (sigma^2 =
// integral_0^inf Phi domega): for unit-variance discrete white noise sampled at dt, E[I(omega)] ==
// dt/pi exactly, independent of omega -- the estimator self-check below confirms this numerically,
// not just algebraically. Averaged over kSegmentCount non-overlapping segments (Bartlett/Welch, no
// window: nothing here needs narrower-than-1/T_segment frequency resolution).
double periodogram_average(const std::vector<double>& series, double omega) {
  constexpr auto kSegmentLengthU = static_cast<std::size_t>(kSegmentLength);
  constexpr auto kSegmentCountU = static_cast<std::size_t>(kSegmentCount);
  double total = 0.0;
  for (std::size_t s = 0; s < kSegmentCountU; ++s) {
    double real = 0.0;
    double imag = 0.0;
    const std::size_t base = s * kSegmentLengthU;
    for (std::size_t k = 0; k < kSegmentLengthU; ++k) {
      const double x = series[base + k];
      const double phase = omega * static_cast<double>(k) * kDt;
      real += x * std::cos(phase);
      imag -= x * std::sin(phase);
    }
    const double magnitude_sq = (real * real) + (imag * imag);
    total += (kDt / (std::numbers::pi * kSegmentLength)) * magnitude_sq;
  }
  return total / kSegmentCount;
}

std::vector<double> log_spaced_grid(double omega_min, double omega_max, int count) {
  std::vector<double> grid;
  grid.reserve(static_cast<std::size_t>(count));
  const double log_min = std::log(omega_min);
  const double log_max = std::log(omega_max);
  for (int i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(count - 1);
    grid.push_back(std::exp(log_min + (t * (log_max - log_min))));
  }
  return grid;
}

double sample_variance(const std::vector<double>& series) {
  double sum = 0.0;
  for (const double x : series) {
    sum += x;
  }
  const double mean = sum / static_cast<double>(series.size());
  double sum_sq_dev = 0.0;
  for (const double x : series) {
    const double d = x - mean;
    sum_sq_dev += d * d;
  }
  return sum_sq_dev / static_cast<double>(series.size());
}

TEST(DrydenPsd, EstimatorSelfCheckMatchesFlatWhiteNoiseSpectrum) {
  const std::vector<double> noise = white_noise_series(kTotalFrames, 4242);
  const double omega_min = 5.0 * 2.0 * std::numbers::pi / (kSegmentLength * kDt);
  const double omega_max = 0.1 * std::numbers::pi / kDt;
  const std::vector<double> grid = log_spaced_grid(omega_min, omega_max, 10);
  const double expected = kDt / std::numbers::pi;
  for (const double omega : grid) {
    SCOPED_TRACE(omega);
    const double estimate = periodogram_average(noise, omega);
    EXPECT_NEAR(expected, estimate, expected * kFrequencyRelativeTolerance);
  }
}

TEST(DrydenPsd, GustSpectraMatchMilF8785cWithinStatisticalBounds) {
  const GustSeries series = run_dryden(kTotalFrames);

  // Same derivation dryden.cpp's own init() performs (section 3.7.3.4, p.53): sigma_w[ft/s] =
  // 0.1*W_20[kt], feet_to_meters applied to the ft/s RESULT, not to kW20Knots itself.
  const double sigma_w_low_m_s =
      astra::units::feet_to_meters(astra::dryden::kSigmaWToW20Ratio * kW20Knots);
  const astra::dryden::MilParameters mp =
      astra::dryden::mil_f_8785c_parameters(kAltitudeM, sigma_w_low_m_s, kSigmaHigh);

  const double omega_min = 5.0 * 2.0 * std::numbers::pi / (kSegmentLength * kDt);
  const double omega_max = 0.1 * std::numbers::pi / kDt;
  const std::vector<double> grid = log_spaced_grid(omega_min, omega_max, 10);

  // a = V/L is plumbing (dryden_filter.hpp's own gust_rate()), computed here at the call site, not
  // inside the golden spectral forms -- see mil_f_8785c_dryden.hpp's own comment on phi_u/phi_vw.
  const double rate_u = astra::dryden::gust_rate(kAirspeedMS, mp.length_u_m);
  const double rate_v = astra::dryden::gust_rate(kAirspeedMS, mp.length_v_m);
  const double rate_w = astra::dryden::gust_rate(kAirspeedMS, mp.length_w_m);

  for (const double omega : grid) {
    SCOPED_TRACE(omega);
    const double u_estimate = periodogram_average(series.u, omega);
    const double u_expected = golden::phi_u(omega, mp.sigma_u_m_s, rate_u);
    EXPECT_NEAR(u_expected, u_estimate, u_expected * kFrequencyRelativeTolerance);

    const double v_estimate = periodogram_average(series.v, omega);
    const double v_expected = golden::phi_vw(omega, mp.sigma_v_m_s, rate_v);
    EXPECT_NEAR(v_expected, v_estimate, v_expected * kFrequencyRelativeTolerance);

    const double w_estimate = periodogram_average(series.w, omega);
    const double w_expected = golden::phi_vw(omega, mp.sigma_w_m_s, rate_w);
    EXPECT_NEAR(w_expected, w_estimate, w_expected * kFrequencyRelativeTolerance);
  }

  EXPECT_NEAR(mp.sigma_u_m_s * mp.sigma_u_m_s, sample_variance(series.u),
              mp.sigma_u_m_s * mp.sigma_u_m_s * kVarianceRelativeTolerance);
  EXPECT_NEAR(mp.sigma_v_m_s * mp.sigma_v_m_s, sample_variance(series.v),
              mp.sigma_v_m_s * mp.sigma_v_m_s * kVarianceRelativeTolerance);
  EXPECT_NEAR(mp.sigma_w_m_s * mp.sigma_w_m_s, sample_variance(series.w),
              mp.sigma_w_m_s * mp.sigma_w_m_s * kVarianceRelativeTolerance);
}

}  // namespace
