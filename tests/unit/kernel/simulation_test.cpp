// Simulation (core/kernel/simulation.hpp, docs/architecture.md §4/§5/§13): PLAN.md M1 PR 2's
// acceptance -- a config naming the dummy plugin runs 1000 frames with t == frame * dt exact and
// the plugin's signal visible as "v0.dummy.*" -- plus the negative cases (an unknown config key,
// an unknown plugin path) that §15 requires to be errors.
#include "astra/kernel/simulation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::PluginError;
using astra::Simulation;

bool mentions(const std::exception& error, std::string_view text) {
  return std::string_view{error.what()}.find(text) != std::string_view::npos;
}

Simulation load(const Config& config) {
  return Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
}

TEST(Simulation, RunsOneThousandFramesWithTExactlyFrameTimesDt) {
  const Config config = Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/dummy_1000.yaml");
  Simulation sim = load(config);

  ASSERT_EQ(sim.frames(), 1000U);
  for (std::uint64_t frame = 0; frame < sim.frames(); ++frame) {
    ASSERT_EQ(sim.clock().frame(), frame);
    ASSERT_EQ(sim.clock().t(), static_cast<double>(frame) * sim.clock().dt()) << "frame " << frame;
    ASSERT_FALSE(sim.step());
  }
  EXPECT_EQ(sim.clock().frame(), 1000U);
}

TEST(Simulation, RunAdvancesTheConfiguredFrameCountAndPublishesTheDummySignals) {
  const Config config = Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/dummy_1000.yaml");
  Simulation sim = load(config);

  ASSERT_FALSE(sim.run());

  EXPECT_EQ(sim.clock().frame(), 1000U);
  const double* steps = sim.registry().find("v0.dummy.steps");
  const double* t = sim.registry().find("v0.dummy.t");
  ASSERT_NE(steps, nullptr);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*steps, 1000.0);
  EXPECT_DOUBLE_EQ(*t, 999.0 * sim.clock().dt());
}

TEST(Simulation, VehiclesExposesTheOneVehicleAsASpanOfLengthOne) {
  const Config config = Config::from_file(std::string{ASTRA_TEST_CONFIG_DIR} + "/dummy_1000.yaml");
  const Simulation sim = load(config);
  ASSERT_EQ(sim.vehicles().size(), 1U);
  EXPECT_EQ(sim.vehicles().front().name(), "v0");
}

TEST(Simulation, RejectsAnUnknownConfigKeyNamingIt) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle: { models: [ { plugin: tests/plugins/dummy, divisor: 1 } ] }
bogus: 1
)",
                                            "unknown-key-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_TRUE(mentions(error, "bogus")) << error.what();
  }
}

TEST(Simulation, RejectsAnUnknownPluginPathNamingIt) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle: { models: [ { plugin: plugins/physical/does_not_exist, divisor: 1 } ] }
)",
                                            "bad-plugin-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected PluginError";
  } catch (const PluginError& error) {
    EXPECT_TRUE(mentions(error, "plugins/physical/does_not_exist")) << error.what();
  }
}

// Codex review, PR M1-2: a negative frame count used to wrap to UINT64_MAX on the unsigned cast,
// turning a typo'd config into an effectively-infinite run instead of a rejected one.
TEST(Simulation, RejectsANegativeFrameCountNamingSimFrames) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: -1 }
vehicle: { models: [ { plugin: tests/plugins/dummy, divisor: 1 } ] }
)",
                                            "negative-frames-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sim.frames");
  }
}

// Codex review, PR M1-2: divisor: -1 used to wrap to UINT32_MAX on the unsigned cast, silently
// turning a typo into "run once every 4.29 billion frames" instead of a rejected config.
TEST(Simulation, RejectsANegativeDivisorNamingItsPath) {
  const Config config = Config::from_string(R"(
sim: { dt: 0.001, frames: 1 }
vehicle: { models: [ { plugin: tests/plugins/dummy, divisor: -1 } ] }
)",
                                            "negative-divisor-fixture");
  try {
    static_cast<void>(load(config));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "vehicle.models.0.divisor");
  }
}

}  // namespace
