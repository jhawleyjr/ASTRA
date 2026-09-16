// plugins/physical/wind/dryden against wind_model.hpp's call contract and this plugin's own
// physical.wind config (dryden.cpp's header comment). tests/physics/dryden_psd_test.cpp is the M2
// PSD acceptance; this file covers the plumbing: reset() reproducibility, seed sensitivity, the
// shareability/position-representation predicates, config rejections, and finiteness at the a -> 0
// and altitude-clamp edges.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::PositionRepresentation;
using astra::SignalRegistry;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::WindModel;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

// dryden's init() rebuilds every member from the config each call (including reseeding the Rng), so
// loading a fresh instance per call, via a static loader, is harmless -- the isa1976_test.cpp /
// uniform's own wind_test.cpp pattern. Each call gets its own SignalRegistry so tests loading
// different configs don't collide on registered names. Ownership stays in `loaded` (a unique_ptr<
// Model>) until AFTER init() succeeds: several tests below expect init() to throw on a bad config,
// and releasing the raw pointer before that call would leak it on that path (caught by this test
// binary's own asan/lsan run).
std::unique_ptr<WindModel> load(std::string_view yaml_fields, SignalRegistry& registry) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("plugins/physical/wind/dryden");
  auto* const wind = dynamic_cast<WindModel*>(loaded.model.get());
  wind->init(Config::from_string("{ " + std::string{yaml_fields} + " }\n", "dryden-fixture"),
             registry);
  return std::unique_ptr<WindModel>{static_cast<WindModel*>(loaded.model.release())};
}

constexpr std::string_view kValidFields = "seed: 1, w20_knots: 15.0, sigma_high: 3.0";

StateVector flying_state(double airspeed_m_s, double altitude_m) {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity: level, heading north
  y(astra::state::kVelocity) = airspeed_m_s;
  y(astra::state::kPosition + 2) = -altitude_m;  // NED down; not read by dryden (altitude_m is a
                                                 // separate begin_frame parameter), but kept
                                                 // self-consistent for readability
  return y;
}

// Steps ctx/vs through `frames` frames of begin_frame/wind_n, recording the NED wind at each frame.
std::vector<Eigen::Vector3d> run(WindModel& wind, int frames, double airspeed_m_s,
                                 double altitude_m) {
  std::vector<Eigen::Vector3d> out;
  out.reserve(static_cast<std::size_t>(frames));
  const StateVector y = flying_state(airspeed_m_s, altitude_m);
  const VehicleState vs{y};
  for (int frame = 0; frame < frames; ++frame) {
    const StepContext ctx{static_cast<double>(frame) * 0.001, 0.001,
                          static_cast<std::uint64_t>(frame)};
    wind.step(ctx);
    wind.begin_frame(ctx, vs, altitude_m);
    out.push_back(wind.wind_n(ctx, vs));
  }
  return out;
}

// --- reset() acceptance ---

TEST(Dryden, ResetReproducesTheSequence) {
  SignalRegistry registry;
  std::unique_ptr<WindModel> wind = load(kValidFields, registry);

  const std::vector<Eigen::Vector3d> first_run = run(*wind, 200, 60.0, 500.0);
  wind->reset();
  const std::vector<Eigen::Vector3d> second_run = run(*wind, 200, 60.0, 500.0);

  ASSERT_EQ(first_run.size(), second_run.size());
  for (std::size_t i = 0; i < first_run.size(); ++i) {
    SCOPED_TRACE(i);
    for (int axis = 0; axis < 3; ++axis) {
      EXPECT_EQ(bits(first_run[i](axis)), bits(second_run[i](axis)));
    }
  }
}

TEST(Dryden, ResetAlsoReproducesAFreshlyLoadedInstance) {
  SignalRegistry registry_a;
  SignalRegistry registry_b;
  std::unique_ptr<WindModel> warmed = load(kValidFields, registry_a);
  static_cast<void>(run(*warmed, 50, 60.0, 500.0));  // consume some draws before reset()
  warmed->reset();
  const std::vector<Eigen::Vector3d> after_reset = run(*warmed, 100, 60.0, 500.0);

  std::unique_ptr<WindModel> fresh = load(kValidFields, registry_b);
  const std::vector<Eigen::Vector3d> fresh_run = run(*fresh, 100, 60.0, 500.0);

  ASSERT_EQ(after_reset.size(), fresh_run.size());
  for (std::size_t i = 0; i < after_reset.size(); ++i) {
    SCOPED_TRACE(i);
    for (int axis = 0; axis < 3; ++axis) {
      EXPECT_EQ(bits(after_reset[i](axis)), bits(fresh_run[i](axis)));
    }
  }
}

// --- Seed actually used ---

TEST(Dryden, DifferentSeedsProduceDifferentSequences) {
  SignalRegistry registry_a;
  SignalRegistry registry_b;
  std::unique_ptr<WindModel> a = load("seed: 1, w20_knots: 15.0, sigma_high: 3.0", registry_a);
  std::unique_ptr<WindModel> b = load("seed: 2, w20_knots: 15.0, sigma_high: 3.0", registry_b);
  const std::vector<Eigen::Vector3d> run_a = run(*a, 20, 60.0, 500.0);
  const std::vector<Eigen::Vector3d> run_b = run(*b, 20, 60.0, 500.0);
  bool any_different = false;
  for (std::size_t i = 0; i < run_a.size(); ++i) {
    if (!run_a[i].isApprox(run_b[i])) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different);
}

// --- Predicates ---

TEST(Dryden, IsNotShareableAcrossVehicles) {
  SignalRegistry registry;
  EXPECT_FALSE(load(kValidFields, registry)->shareable_across_vehicles());
}

TEST(Dryden, AssumesNoPositionRepresentation) {
  SignalRegistry registry;
  EXPECT_EQ(PositionRepresentation::kAny,
            load(kValidFields, registry)->assumed_position_representation());
}

// --- Config rejections ---

TEST(Dryden, MissingSeedIsAConfigErrorNamingPhysicalWindSeed) {
  SignalRegistry registry;
  try {
    static_cast<void>(load("w20_knots: 15.0, sigma_high: 3.0", registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "seed");
  }
}

TEST(Dryden, NegativeSeedIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(static_cast<void>(load("seed: -1, w20_knots: 15.0, sigma_high: 3.0", registry)),
               ConfigError);
}

TEST(Dryden, SeedAboveTwoToTheFiftyThreeIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(
      static_cast<void>(load("seed: 9007199254740993, w20_knots: 15.0, sigma_high: 3.0", registry)),
      ConfigError);
}

TEST(Dryden, MissingW20KnotsIsAConfigErrorNamingIt) {
  SignalRegistry registry;
  try {
    static_cast<void>(load("seed: 1, sigma_high: 3.0", registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "w20_knots");
  }
}

TEST(Dryden, NonFiniteW20KnotsIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(static_cast<void>(load("seed: 1, w20_knots: .nan, sigma_high: 3.0", registry)),
               ConfigError);
}

TEST(Dryden, NegativeW20KnotsIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(static_cast<void>(load("seed: 1, w20_knots: -1.0, sigma_high: 3.0", registry)),
               ConfigError);
}

TEST(Dryden, MissingSigmaHighIsAConfigErrorNamingIt) {
  SignalRegistry registry;
  try {
    static_cast<void>(load("seed: 1, w20_knots: 15.0", registry));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sigma_high");
  }
}

TEST(Dryden, NonFiniteSigmaHighIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(static_cast<void>(load("seed: 1, w20_knots: 15.0, sigma_high: .nan", registry)),
               ConfigError);
}

TEST(Dryden, NegativeSigmaHighIsRejected) {
  SignalRegistry registry;
  EXPECT_THROW(static_cast<void>(load("seed: 1, w20_knots: 15.0, sigma_high: -1.0", registry)),
               ConfigError);
}

// --- Finiteness at the edges (a -> 0 via V == 0; the 10 ft altitude clamp) ---

TEST(Dryden, GustsStayFiniteAtZeroAirspeed) {
  SignalRegistry registry;
  std::unique_ptr<WindModel> wind = load(kValidFields, registry);
  const std::vector<Eigen::Vector3d> gusts = run(*wind, 10'000, 0.0, 500.0);
  for (const Eigen::Vector3d& g : gusts) {
    EXPECT_TRUE(g.allFinite());
  }
}

TEST(Dryden, GustsStayFiniteAtTheAltitudeClampIncludingNegative) {
  SignalRegistry registry_a;
  SignalRegistry registry_b;
  std::unique_ptr<WindModel> at_floor = load(kValidFields, registry_a);
  const std::vector<Eigen::Vector3d> gusts_floor = run(*at_floor, 1000, 60.0, 0.0);
  for (const Eigen::Vector3d& g : gusts_floor) {
    EXPECT_TRUE(g.allFinite());
  }

  std::unique_ptr<WindModel> below_floor = load(kValidFields, registry_b);
  const std::vector<Eigen::Vector3d> gusts_below = run(*below_floor, 1000, 60.0, -50.0);
  for (const Eigen::Vector3d& g : gusts_below) {
    EXPECT_TRUE(g.allFinite());
  }
}

}  // namespace
