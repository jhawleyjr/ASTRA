// Flies an F-16 NESC case against its ADR-0028 band (M7 PR 9, docs/plans/m7-pr9-open-loop.md,
// "Harness design"). Shared by the open-loop cases (nesc_f16_open_loop_test.cpp) and the negative
// controls (nesc_f16_negative_controls_test.cpp).
//
// Per sample k (frame 100k, ADR-0028 item 4), in M2 PR 3c's R12 order:
// 1. the pre-step state gives the kinematic channels: latitude, longitude and altitude from p_n
// (the
//    altitude is the height above the WGS-84 ellipsoid, as both members' altitudeMsl_ft is: the
//    plan's Q2), v_n = rotate_b_to_n(q_nb, v_b), the Euler angles from q_nb, and omega_b (inertial,
//    as bodyAngularRateWrtEi);
// 2. run(1);
// 3. copy_inputs() on both evaluator instances, now that the latched controls are this frame's
//    (STATE.md's PR 9 thread);
// 4. the frame-start environment (PR 7's FrameStartEnvironment) and the aero instance's wrench give
//    the aero channels, aero only and about the CG, which is the CM (the M7 plan's Q5; addendum
//    D4); the latched environment gives the atmosphere channels;
// 5. Q5's route is guarded (the plan's D5): aero plus engine equals v0.wrench_total.* by PR 7's
//    matches_relative at every sample;
// 6. run(99), except after the last sample.
// So a case of N samples drives 100 (N - 1) + 1 frames: 180,001 for cases 11 and 12, one more than
// the configs' sim.frames, which bounds only the no-argument Simulation::run() (simulation.cpp).
//
// Each difference is taken against the band's reference, wrapped for yaw and longitude (ADR-0028
// item 6). Mode::kGate asserts |d| <= tol_abs at every sample from Band::kFirstGatedSample on and
// never before (ADR-0028 item 5); the samples before it are compared and logged only. No channel is
// excluded (the plan's D4: ADR-0028 item 2's footnotes exclude none in cases 11 and 12).
// Mode::kControl asserts nothing per sample: it records each channel's largest ratio and first
// gated exceedance, and may stop at a named channel's first gated exceedance.
//
// Test-only code: it allocates and throws.
#pragma once

#include <gtest/gtest.h>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/util/frames.hpp"
#include "independent_evaluator.hpp"

namespace astra::test::nesc::f16 {

inline constexpr int kChannelCount = 21;
inline constexpr std::uint64_t kFramesPerSample = 100;  // ADR-0013: 10 Hz at dt = 1 ms

// The shortest signed angular distance, in (-pi, pi] (ADR-0028 item 6; nesc_cases_test.cpp's).
inline double wrap_to_pi(double x) noexcept {
  double wrapped = std::fmod(x + std::numbers::pi, 2.0 * std::numbers::pi);
  if (wrapped <= 0.0) {
    wrapped += 2.0 * std::numbers::pi;
  }
  return wrapped - std::numbers::pi;
}

struct ChannelRef {
  std::string_view name;
  bool wrapped;
  std::span<const double> reference;
  double tol_abs;
};

// CaseBandOf's members, in its order.
template <typename Band>
std::array<ChannelRef, kChannelCount> channels_of(const Band& band) {
  return {{
      {"latitude", false, band.latitude.reference, band.latitude.tol_abs},
      {"longitude", true, band.longitude.reference, band.longitude.tol_abs},
      {"altitude", false, band.altitude.reference, band.altitude.tol_abs},
      {"v_north", false, band.v_north.reference, band.v_north.tol_abs},
      {"v_east", false, band.v_east.reference, band.v_east.tol_abs},
      {"v_down", false, band.v_down.reference, band.v_down.tol_abs},
      {"yaw", true, band.yaw.reference, band.yaw.tol_abs},
      {"pitch", false, band.pitch.reference, band.pitch.tol_abs},
      {"roll", false, band.roll.reference, band.roll.tol_abs},
      {"rate_p", false, band.rate_p.reference, band.rate_p.tol_abs},
      {"rate_q", false, band.rate_q.reference, band.rate_q.tol_abs},
      {"rate_r", false, band.rate_r.reference, band.rate_r.tol_abs},
      {"force_x", false, band.force_x.reference, band.force_x.tol_abs},
      {"force_y", false, band.force_y.reference, band.force_y.tol_abs},
      {"force_z", false, band.force_z.reference, band.force_z.tol_abs},
      {"moment_l", false, band.moment_l.reference, band.moment_l.tol_abs},
      {"moment_m", false, band.moment_m.reference, band.moment_m.tol_abs},
      {"moment_n", false, band.moment_n.reference, band.moment_n.tol_abs},
      {"density", false, band.density.reference, band.density.tol_abs},
      {"pressure", false, band.pressure.reference, band.pressure.tol_abs},
      {"temperature", false, band.temperature.reference, band.temperature.tol_abs},
  }};
}

// The channel indices the negative controls name.
inline constexpr int kPitch = 7;
inline constexpr int kRateQ = 10;
inline constexpr int kForceZ = 14;

// ADR-0015's accepts_zero over the gated samples: the band is at least the reference's magnitude at
// every gated sample, so a trajectory of zeros would pass. Such a channel is asserted, but never
// cited as evidence (ADR-0028 item 8).
inline bool zero_accepting(const ChannelRef& channel, int first_gated) {
  double peak = 0.0;
  for (const double reference : channel.reference.subspan(static_cast<std::size_t>(first_gated))) {
    peak = std::max(peak, std::abs(reference));
  }
  return channel.tol_abs >= peak;
}

struct ChannelStats {
  double sample0_ratio = 0.0;
  double max_ungated_ratio = 0.0;
  int max_ungated_at = -1;
  double max_gated_ratio = 0.0;
  int max_gated_at = -1;
  int first_gated_exceedance = -1;
};

enum class Mode : std::uint8_t { kGate, kControl };

struct FlightOptions {
  Mode mode = Mode::kGate;
  // kControl only: stop after the sample at which this channel first exceeds its band while gated.
  int stop_channel = -1;
};

struct FlightResult {
  std::array<ChannelStats, kChannelCount> stats{};
  int samples_compared = 0;
  bool halted = false;
  // The first sample whose channels could not be compared because of the halt.
  int halt_sample = -1;
  astra::HaltReason halt;
  bool stopped_at_exceedance = false;
  std::uint64_t frames = 0;
  double guard_force = 0.0;  // the largest relative miss of D5's guard, force and moment
  double guard_moment = 0.0;
};

// The sim, built from a config, with PR 7's evaluator around it: the frame-start environment and
// independent instances of vehicle.models' entry 0 (the aero) and entry 1 (the engine).
class Flight {
 public:
  explicit Flight(Config config)
      : config_(std::move(config)),
        sim_(Simulation::from_config(config_, ASTRA_TEST_PLUGIN_DIR)),
        environment_(config_, loader_),
        aero_(config_.at("vehicle.models").at(0), loader_, sim_),
        engine_(config_.at("vehicle.models").at(1), loader_, sim_) {
    const std::string base = "v0.wrench_total.";
    for (const std::string_view field :
         {"force_b_x", "force_b_y", "force_b_z", "moment_b_l", "moment_b_m", "moment_b_n"}) {
      const double* signal = sim_.registry().find(base + std::string{field});
      if (signal == nullptr) {
        throw astra::Error("Flight: the sim has no " + base + std::string{field});
      }
      total_.push_back(signal);
    }
  }
  Flight(const Flight&) = delete;
  Flight& operator=(const Flight&) = delete;
  Flight(Flight&&) = delete;
  Flight& operator=(Flight&&) = delete;
  ~Flight() = default;

  Simulation& sim() noexcept { return sim_; }

  template <typename Band>
  FlightResult fly(std::string_view name, const Band& band, FlightOptions options = {}) {
    using B = std::remove_cvref_t<Band>;
    const std::array<ChannelRef, kChannelCount> channels = channels_of(band);
    FlightResult result;
    for (int k = 0; k < B::kSampleCount; ++k) {
      // An explicit copy, taken before run(1) moves the live state on (R12's own copy).
      const StateVector pre_step = sim_.vehicles().front().state();
      const VehicleState vs{pre_step};
      const Eigen::Vector3d p_n = pre_step.segment<state::kPositionSize>(state::kPosition);
      const Eigen::Vector3d v_b = pre_step.segment<state::kVelocitySize>(state::kVelocity);
      const frames::Quaternion q_nb = pre_step.segment<state::kQuaternionSize>(state::kQuaternion);
      const Eigen::Vector3d omega_b =
          pre_step.segment<state::kAngularRateSize>(state::kAngularRate);
      const Eigen::Vector3d v_n = frames::rotate_b_to_n(q_nb, v_b);
      const frames::EulerAngles euler = frames::euler_from_quaternion(q_nb);  // yaw, pitch, roll

      if (const astra::HaltReason halt = sim_.run(1); halt) {
        record_halt(result, halt, k);
        break;
      }
      aero_.copy_inputs();
      engine_.copy_inputs();
      const EnvironmentSample& latched = sim_.vehicles().front().environment();
      const EnvironmentSample env = environment_.at(latched, vs);
      const Wrench aero = aero_.at(vs, env);
      const Wrench engine = engine_.at(vs, env);
      guard(name, k, aero, engine, result);

      const std::array<double, kChannelCount> actual{
          p_n(0),
          p_n(1),
          p_n(2),
          v_n(0),
          v_n(1),
          v_n(2),
          euler(0),
          euler(1),
          euler(2),
          omega_b(0),
          omega_b(1),
          omega_b(2),
          aero.force_b(0),
          aero.force_b(1),
          aero.force_b(2),
          aero.moment_b(0),
          aero.moment_b(1),
          aero.moment_b(2),
          latched.atmosphere.density_kg_m3,
          latched.atmosphere.pressure_pa,
          latched.atmosphere.temperature_k,
      };
      compare(name, channels, actual, k, B::kFirstGatedSample, options.mode, result);
      result.samples_compared = k + 1;

      if (options.mode == Mode::kControl && options.stop_channel >= 0 &&
          result.stats.at(static_cast<std::size_t>(options.stop_channel)).first_gated_exceedance >=
              0) {
        result.stopped_at_exceedance = true;
        break;
      }
      if (k + 1 < B::kSampleCount) {
        if (const astra::HaltReason halt = sim_.run(kFramesPerSample - 1); halt) {
          record_halt(result, halt, k + 1);
          break;
        }
      }
    }
    result.frames = sim_.clock().frame();
    if (options.mode == Mode::kGate) {
      EXPECT_FALSE(result.halted) << name << ": halted on check "
                                  << static_cast<int>(result.halt.check) << " at frame "
                                  << result.halt.frame << " (" << result.halt.source
                                  << "), before sample " << result.halt_sample;
    }
    return result;
  }

 private:
  static void record_halt(FlightResult& result, const astra::HaltReason& halt, int sample) {
    result.halted = true;
    result.halt = halt;
    result.halt_sample = sample;
  }

  void guard(std::string_view name, int k, const Wrench& aero, const Wrench& engine,
             FlightResult& result) const {
    Wrench sum;
    sum.force_b = aero.force_b + engine.force_b;
    sum.moment_b = aero.moment_b + engine.moment_b;
    Wrench total;
    total.force_b = {*total_[0], *total_[1], *total_[2]};
    total.moment_b = {*total_[3], *total_[4], *total_[5]};
    EXPECT_TRUE(matches_relative(sum, total)) << name << " sample " << k << ": Q5's route";
    result.guard_force =
        std::max(result.guard_force, vector_miss(sum.force_b, total.force_b).relative());
    result.guard_moment =
        std::max(result.guard_moment, vector_miss(sum.moment_b, total.moment_b).relative());
  }

  static void compare(std::string_view name, const std::array<ChannelRef, kChannelCount>& channels,
                      const std::array<double, kChannelCount>& actual, int k, int first_gated,
                      Mode mode, FlightResult& result) {
    for (std::size_t c = 0; c < channels.size(); ++c) {
      const ChannelRef& channel = channels.at(c);
      const double reference = channel.reference[static_cast<std::size_t>(k)];
      double diff = actual.at(c) - reference;
      if (channel.wrapped) {
        diff = wrap_to_pi(diff);
      }
      const double ratio = std::abs(diff) / channel.tol_abs;
      ChannelStats& stats = result.stats.at(c);
      if (k == 0) {
        stats.sample0_ratio = ratio;
      }
      if (k < first_gated) {
        // ADR-0028 item 5: compared and logged, never asserted.
        if (!(ratio <= stats.max_ungated_ratio)) {
          stats.max_ungated_ratio = ratio;
          stats.max_ungated_at = k;
        }
        continue;
      }
      if (!(ratio <= stats.max_gated_ratio)) {
        stats.max_gated_ratio = ratio;
        stats.max_gated_at = k;
      }
      const bool inside = std::abs(diff) <= channel.tol_abs;
      if (!inside && stats.first_gated_exceedance < 0) {
        stats.first_gated_exceedance = k;
      }
      if (mode == Mode::kGate) {
        EXPECT_TRUE(inside) << name << " " << channel.name << " sample " << k
                            << " (t = " << std::format("{:.1f}", 0.1 * k) << " s): actual "
                            << std::format("{:.17g}", actual.at(c)) << ", reference "
                            << std::format("{:.17g}", reference) << ", |d| "
                            << std::format("{:.6g}", std::abs(diff)) << " > tol_abs "
                            << std::format("{:.6g}", channel.tol_abs);
      }
    }
  }

  Config config_;
  PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  Simulation sim_;
  FrameStartEnvironment environment_;
  IndependentWrenchSource aero_;
  IndependentWrenchSource engine_;
  std::vector<const double*> total_;
};

// The plan's D3 log: per channel, sample 0's ratio |d|/tol_abs, the largest ratio before the window
// and where, the largest gated ratio and where, and whether the channel is zero-accepting.
template <typename Band>
void print_summary(std::string_view name, const Band& band, const FlightResult& result) {
  using B = std::remove_cvref_t<Band>;
  const std::array<ChannelRef, kChannelCount> channels = channels_of(band);
  std::cout << std::format(
      "{}: {} samples compared, {} frames, halted {}, stopped at an exceedance {}; Q5 guard "
      "largest "
      "relative miss force {:.3g}, moment {:.3g}\n",
      name, result.samples_compared, result.frames, result.halted, result.stopped_at_exceedance,
      result.guard_force, result.guard_moment);
  for (std::size_t c = 0; c < channels.size(); ++c) {
    const ChannelStats& s = result.stats.at(c);
    std::cout << std::format(
        "  {:12s} tol_abs {:<12.6g} sample 0 {:<10.4g} ungated max {:<10.4g} at {:>4}  gated max "
        "{:<10.4g} at {:>4}  first gated exceedance {:>5}  {}\n",
        channels.at(c).name, channels.at(c).tol_abs, s.sample0_ratio, s.max_ungated_ratio,
        s.max_ungated_at, s.max_gated_ratio, s.max_gated_at, s.first_gated_exceedance,
        zero_accepting(channels.at(c), B::kFirstGatedSample) ? "zero-accepting: not evidence" : "");
  }
}

}  // namespace astra::test::nesc::f16
