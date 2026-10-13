// F16Evaluator (M7 PR 7, docs/plans/m7-pr7-aero-evaluator.md): the self-check that guards Q5's
// aero-only channel route (the M7 plan's Q5 under (c), with its 2026-10-08 amendment and amendment
// 6), pre-registered in the plan with Joe's amendments of 2026-10-11.
//
// On the F-16 (aero, engine, mass), with all four controls changing every frame, the independent
// aero wrench plus an independently evaluated engine wrench must equal v0.wrench_total.* at every
// sample, to 1e-12 normwise relative (independent_evaluator.hpp's matches_relative). The engine
// owns no state (the inventory addendum, section 10), so its frame-start evaluation needs none. The
// self-check runs on earth/flat and on wgs84_rotating. A miss goes to Joe; it never becomes a wider
// number.
//
// Every frame is a sample, in R12's order (nesc_cases_test.cpp): write the frame's controls, copy
// the pre-step state, run(1), then read the frame's latched environment and published wrench_total.
//
// Each test prints one report line: the largest relative miss of its correct evaluations, which is
// expected to be exactly 0 (the same functions on the same inputs, summed in vehicle.models' order
// as Vehicle::accumulate_wrench sums them), and the first sample whose miss is not exactly 0, if
// any. The negative controls also print their smallest miss, the check's weakest discrimination.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "physics/nesc/independent_evaluator.hpp"

namespace {

namespace nesc = astra::test::nesc;
using astra::Config;
using astra::Simulation;
using astra::Wrench;

constexpr double kDt = 0.001;
constexpr std::uint64_t kFrames = 5000;

// The controls, ICD v1's convention (rad, throttle 0..1), each a sinusoid of its own frequency, so
// no two are proportional and every frame's differ from the last.
struct Controls {
  double elevator;
  double aileron;
  double rudder;
  double throttle;
};

Controls schedule(std::uint64_t frame) {
  constexpr double kTwoPi = 2.0 * std::numbers::pi;
  const double t = static_cast<double>(frame) * kDt;  // ADR-0010: derived, never accumulated
  return {.elevator = -0.05 + (0.03 * std::sin(kTwoPi * 0.7 * t)),
          .aileron = 0.04 * std::sin(kTwoPi * 1.1 * t),
          .rudder = 0.03 * std::sin(kTwoPi * 1.3 * t),
          .throttle = 0.6 + (0.2 * std::sin(kTwoPi * 0.5 * t))};
}

// The F-16 fixture, F16NoAlloc's pattern (aircraft/f16/tests/no_alloc_test.cpp): the aero, then the
// engine, then the mass and isa1976. Both Earths start at 3,000 m with a nonzero heading, pitch,
// roll and body rate, so on wgs84_rotating every body component of the Earth rate is nonzero.
// initial_controls equals schedule(0).
std::string f16_yaml(std::string_view physical, std::string_view p_n) {
  std::string yaml = std::format("sim: {{ dt: {}, frames: {} }}\n", kDt, kFrames);
  yaml += "vehicle:\n";
  yaml += "  models:\n";
  yaml += "    - { plugin: aircraft/f16/physical/aero }\n";
  yaml += "    - { plugin: aircraft/f16/hardware/propulsion/engine }\n";
  yaml += "  initial_controls:\n";
  yaml += "    controls: { elevator: -0.05, aileron: 0.0, rudder: 0.0, throttle: 0.6 }\n";
  yaml += "physical:\n";
  yaml += physical;
  yaml += "  atmosphere: plugins/physical/atmosphere/isa1976\n";
  yaml += "  mass: { plugin: aircraft/f16/physical/mass }\n";
  yaml += "initial:\n";
  yaml += std::format(
      "  state: {{ p_n: {}, v_b: [150.0, 3.0, 8.0], euler_nb: [0.5, 0.05, 0.1], "
      "omega_b: [0.02, -0.01, 0.03] }}\n",
      p_n);
  return yaml;
}

std::string flat_yaml() {
  return f16_yaml(
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n",
      "[0.0, 0.0, -3000.0]");
}

// wgs84_rotating with wgs84_j2, the consistent pair PR 9's configs use; p_n is latitude and
// longitude in rad and the altitude in m. The uniform wind (case 07's form) exercises the latched
// wind_n.
std::string rotating_yaml() {
  return f16_yaml(
      "  earth: plugins/physical/earth/wgs84_rotating\n"
      "  gravity: plugins/physical/gravity/wgs84_j2\n"
      "  wind: { plugin: plugins/physical/wind/uniform, north: 3.0, east: 6.0 }\n",
      "[0.6, -2.0, 3000.0]");
}

// The sim, the evaluator's environment, and the two independent instances: vehicle.models' entry 0
// (the aero) and entry 1 (the engine), each from the node the sim loaded it from.
class Rig {
 public:
  Rig(const std::string& yaml, std::string_view origin)
      : config_(Config::from_string(yaml, origin)),
        sim_(Simulation::from_config(config_, ASTRA_TEST_PLUGIN_DIR)),
        environment_(config_, loader_),
        aero_(config_.at("vehicle.models").at(0), loader_, sim_),
        engine_(config_.at("vehicle.models").at(1), loader_, sim_) {
    const std::string base = "v0.wrench_total.";
    for (const std::string_view field :
         {"force_b_x", "force_b_y", "force_b_z", "moment_b_l", "moment_b_m", "moment_b_n"}) {
      const double* signal = sim_.registry().find(base + std::string{field});
      if (signal == nullptr) {
        throw astra::Error("F16Evaluator: the sim has no " + base + std::string{field});
      }
      total_.push_back(signal);
    }
  }

  Simulation& sim() noexcept { return sim_; }
  const nesc::FrameStartEnvironment& environment() const noexcept { return environment_; }
  nesc::IndependentWrenchSource& aero() noexcept { return aero_; }
  nesc::IndependentWrenchSource& engine() noexcept { return engine_; }

  // The frame's published k1 wrench_total (Vehicle::evaluate_derivative_stage).
  Wrench total() const noexcept {
    Wrench w;
    w.force_b = {*total_[0], *total_[1], *total_[2]};
    w.moment_b = {*total_[3], *total_[4], *total_[5]};
    return w;
  }

 private:
  Config config_;
  Simulation sim_;
  astra::PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  nesc::FrameStartEnvironment environment_;
  nesc::IndependentWrenchSource aero_;
  nesc::IndependentWrenchSource engine_;
  std::vector<const double*> total_;
};

struct Sample {
  std::uint64_t frame = 0;
  astra::StateVector pre_step_state;
  astra::EnvironmentSample latched;
  Wrench total;
};

// Flies all kFrames frames, handing each to on_sample. Stops at the first halt or fatal failure;
// the caller asserts the run completed.
template <typename OnSample>
void fly(Rig& rig, OnSample&& on_sample) {
  Simulation& sim = rig.sim();
  for (std::uint64_t frame = 0; frame < kFrames; ++frame) {
    const Controls controls = schedule(frame);
    sim.write_input("controls.elevator", controls.elevator);
    sim.write_input("controls.aileron", controls.aileron);
    sim.write_input("controls.rudder", controls.rudder);
    sim.write_input("controls.throttle", controls.throttle);

    Sample sample;
    sample.frame = frame;
    // An explicit copy, taken before run(1) moves the live state on (R12's own copy).
    sample.pre_step_state = sim.vehicles().front().state();
    const astra::HaltReason halt = sim.run(1);
    ASSERT_FALSE(halt) << "frame " << frame << " halted on check " << static_cast<int>(halt.check)
                       << " (" << halt.source << ")";
    sample.latched = sim.vehicles().front().environment();
    sample.total = rig.total();
    on_sample(sample);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
}

Wrench sum(const Wrench& a, const Wrench& b) {
  Wrench w;
  w.force_b = a.force_b + b.force_b;
  w.moment_b = a.moment_b + b.moment_b;
  return w;
}

// The largest relative miss over a test's correct evaluations, and the first sample whose miss is
// not exactly 0 (amendment 3).
struct MissReport {
  double force = 0.0;
  double moment = 0.0;
  std::uint64_t nonzero_samples = 0;
  std::uint64_t first_nonzero_frame = 0;

  void add(std::uint64_t frame, const Wrench& evaluated, const Wrench& total) {
    const nesc::VectorMiss f = nesc::vector_miss(evaluated.force_b, total.force_b);
    const nesc::VectorMiss m = nesc::vector_miss(evaluated.moment_b, total.moment_b);
    force = std::max(force, f.relative());
    moment = std::max(moment, m.relative());
    if (f.difference != 0.0 || m.difference != 0.0) {
      if (nonzero_samples == 0) {
        first_nonzero_frame = frame;
      }
      ++nonzero_samples;
    }
  }

  void print(std::string_view test, std::string_view fixture, std::uint64_t samples) const {
    std::cout << std::format(
        "{} ({}): over {} samples, the largest relative miss is force {} and moment {}; {} samples "
        "miss by more than exactly 0{}\n",
        test, fixture, samples, force, moment, nonzero_samples,
        nonzero_samples == 0 ? std::string{}
                             : std::format(", the first at frame {}", first_nonzero_frame));
  }
};

// The smallest relative miss of a negative control: max(force, moment) per sample, minimized over
// the samples.
struct WeakestMiss {
  double relative = std::numeric_limits<double>::infinity();

  void add(const Wrench& evaluated, const Wrench& total) {
    const double f = nesc::vector_miss(evaluated.force_b, total.force_b).relative();
    const double m = nesc::vector_miss(evaluated.moment_b, total.moment_b).relative();
    relative = std::min(relative, std::max(f, m));
  }
};

// Tests 1 and 2: the evaluator's aero plus the engine equals wrench_total at every sample.
void expect_sum_equals_wrench_total(const std::string& yaml, std::string_view origin,
                                    std::string_view test, std::string_view fixture) {
  Rig rig{yaml, origin};
  MissReport report;
  std::uint64_t samples = 0;
  fly(rig, [&](const Sample& s) {
    const astra::VehicleState vs{s.pre_step_state};
    const astra::EnvironmentSample env = rig.environment().at(s.latched, vs);
    rig.aero().copy_inputs();
    rig.engine().copy_inputs();
    const Wrench evaluated = sum(rig.aero().at(vs, env), rig.engine().at(vs, env));
    EXPECT_TRUE(nesc::matches_relative(evaluated, s.total)) << fixture << " frame " << s.frame;
    report.add(s.frame, evaluated, s.total);
    ++samples;
  });
  EXPECT_EQ(rig.sim().clock().frame(), kFrames) << fixture << ": the run must complete";
  report.print(test, fixture, samples);
}

}  // namespace

TEST(F16Evaluator, AeroPlusEngineEqualsWrenchTotalOnAFlatEarth) {
  expect_sum_equals_wrench_total(flat_yaml(), "f16-evaluator-flat",
                                 "F16Evaluator.AeroPlusEngineEqualsWrenchTotalOnAFlatEarth",
                                 "earth/flat");
}

TEST(F16Evaluator, AeroPlusEngineEqualsWrenchTotalOnWgs84Rotating) {
  expect_sum_equals_wrench_total(rotating_yaml(), "f16-evaluator-rotating",
                                 "F16Evaluator.AeroPlusEngineEqualsWrenchTotalOnWgs84Rotating",
                                 "earth/wgs84_rotating");
}

// Test 3, the negative control (the brief, with M1's split, and Joe's accepted additions). The
// latched sample's omega_ie_b and altitude_m are NaN. Left there, the check fails:
// - a NaN omega_ie_b gives a non-finite aero wrench (the engine does not read it);
// - a NaN altitude_m gives a finite aero wrench (the F-16 aero does not read it) and a non-finite
//   engine wrench.
// A zero omega_ie_b (a silent non-rotating Earth) and a zero altitude_m (silent sea level) fail
// too, at every sample. The correct environment passes at the same samples, so each failure is the
// field's.
TEST(F16Evaluator, AnUnsetEarthRateOrAltitudeFailsTheCheck) {
  Rig rig{rotating_yaml(), "f16-evaluator-negative"};
  MissReport report;
  WeakestMiss zero_rate;
  WeakestMiss zero_altitude;
  std::uint64_t samples = 0;
  fly(rig, [&](const Sample& s) {
    const astra::VehicleState vs{s.pre_step_state};
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(std::isnan(s.latched.omega_ie_b(i))) << "latched omega_ie_b component " << i;
    }
    ASSERT_TRUE(std::isnan(s.latched.altitude_m)) << "latched altitude_m";
    rig.aero().copy_inputs();
    rig.engine().copy_inputs();

    const astra::EnvironmentSample env = rig.environment().at(s.latched, vs);
    const Wrench correct = sum(rig.aero().at(vs, env), rig.engine().at(vs, env));
    ASSERT_TRUE(nesc::matches_relative(correct, s.total)) << "frame " << s.frame;
    report.add(s.frame, correct, s.total);

    astra::EnvironmentSample nan_rate = env;
    nan_rate.omega_ie_b = s.latched.omega_ie_b;
    const Wrench nan_rate_aero = rig.aero().at(vs, nan_rate);
    const Wrench nan_rate_engine = rig.engine().at(vs, nan_rate);
    EXPECT_FALSE(nesc::all_finite(nan_rate_aero)) << "frame " << s.frame;
    EXPECT_TRUE(nesc::all_finite(nan_rate_engine)) << "frame " << s.frame;
    EXPECT_FALSE(nesc::matches_relative(sum(nan_rate_aero, nan_rate_engine), s.total))
        << "frame " << s.frame;

    astra::EnvironmentSample nan_altitude = env;
    nan_altitude.altitude_m = s.latched.altitude_m;
    const Wrench nan_altitude_aero = rig.aero().at(vs, nan_altitude);
    const Wrench nan_altitude_engine = rig.engine().at(vs, nan_altitude);
    EXPECT_TRUE(nesc::all_finite(nan_altitude_aero)) << "frame " << s.frame;
    EXPECT_FALSE(nesc::all_finite(nan_altitude_engine)) << "frame " << s.frame;
    EXPECT_FALSE(nesc::matches_relative(sum(nan_altitude_aero, nan_altitude_engine), s.total))
        << "frame " << s.frame;

    astra::EnvironmentSample no_rate = env;
    no_rate.omega_ie_b = Eigen::Vector3d::Zero();
    const Wrench no_rate_sum = sum(rig.aero().at(vs, no_rate), rig.engine().at(vs, no_rate));
    EXPECT_FALSE(nesc::matches_relative(no_rate_sum, s.total)) << "frame " << s.frame;
    zero_rate.add(no_rate_sum, s.total);

    astra::EnvironmentSample sea_level = env;
    sea_level.altitude_m = 0.0;
    const Wrench sea_level_sum = sum(rig.aero().at(vs, sea_level), rig.engine().at(vs, sea_level));
    EXPECT_FALSE(nesc::matches_relative(sea_level_sum, s.total)) << "frame " << s.frame;
    zero_altitude.add(sea_level_sum, s.total);
    ++samples;
  });
  EXPECT_EQ(rig.sim().clock().frame(), kFrames) << "the run must complete";
  report.print("F16Evaluator.AnUnsetEarthRateOrAltitudeFailsTheCheck", "earth/wgs84_rotating",
               samples);
  std::cout << std::format(
      "F16Evaluator.AnUnsetEarthRateOrAltitudeFailsTheCheck (earth/wgs84_rotating): the smallest "
      "relative miss is {} with a zero omega_ie_b and {} with a zero altitude_m\n",
      zero_rate.relative, zero_altitude.relative);
}

// Test 4 (Joe's accepted addition): evaluating a sample before copying its controls, that is with
// the previous frame's, fails the check at every sample after the first, so the changing controls
// give the check teeth. The same sample then passes once its controls are copied.
TEST(F16Evaluator, StaleControlsFailTheCheck) {
  Rig rig{flat_yaml(), "f16-evaluator-stale"};
  MissReport report;
  WeakestMiss stale;
  std::uint64_t samples = 0;
  Controls previous = schedule(0);
  fly(rig, [&](const Sample& s) {
    const astra::VehicleState vs{s.pre_step_state};
    const astra::EnvironmentSample env = rig.environment().at(s.latched, vs);
    if (s.frame > 0) {
      const Controls current = schedule(s.frame);
      ASSERT_TRUE(current.elevator != previous.elevator && current.aileron != previous.aileron &&
                  current.rudder != previous.rudder && current.throttle != previous.throttle)
          << "frame " << s.frame << ": every control must change";
      previous = current;
      const Wrench stale_sum = sum(rig.aero().at(vs, env), rig.engine().at(vs, env));
      EXPECT_FALSE(nesc::matches_relative(stale_sum, s.total)) << "frame " << s.frame;
      stale.add(stale_sum, s.total);
    }
    rig.aero().copy_inputs();
    rig.engine().copy_inputs();
    const Wrench current_sum = sum(rig.aero().at(vs, env), rig.engine().at(vs, env));
    EXPECT_TRUE(nesc::matches_relative(current_sum, s.total)) << "frame " << s.frame;
    report.add(s.frame, current_sum, s.total);
    ++samples;
  });
  EXPECT_EQ(rig.sim().clock().frame(), kFrames) << "the run must complete";
  report.print("F16Evaluator.StaleControlsFailTheCheck", "earth/flat", samples);
  std::cout << std::format(
      "F16Evaluator.StaleControlsFailTheCheck (earth/flat): the smallest relative miss with the "
      "previous frame's controls is {}\n",
      stale.relative);
}
