// plugins/physical/gravity/inverse_square's own domain (ADR-0012, M2 PR 2c): in_domain/
// validate_p_n against its own, separately configured radius_ -- not earth/round's, the Codex
// finding tests/unit/kernel/simulation_physical_config_test.cpp's
// RejectsAnInverseSquareRadiusInconsistentWithTheInitialAltitude already exercises end to end
// through Simulation::from_config. This file is the unit-level counterpart, the same shape as
// tests/unit/earth/earth_kinematics_test.cpp and tests/unit/gravity/wgs84_j2_test.cpp: loads the
// real .so through PluginLoader, checks in_domain/validate_p_n as pure functions.
#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::GravityModel;

GravityModel& load(double radius) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("plugins/physical/gravity/inverse_square");
  auto* const gravity = dynamic_cast<GravityModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  gravity->init(
      Config::from_string("radius: " + std::to_string(radius) + "\n", "inverse-square-fixture"),
      registry);
  return *gravity;
}

// ADR-0012: no polar case here (g_n is purely radial), so in_domain has only the finite/altitude-
// floor pair -- no input-sanity check to interleave with, since latitude/longitude sanity is
// EarthModel::validate_p_n's job, checked first.
TEST(InverseSquareGravity, InDomainTrueAboveTheFloorAndFalseAtOrBelowIt) {
  GravityModel& gravity = load(1000.0);
  EXPECT_TRUE(gravity.in_domain({0.0, 0.0, -999.0}));
  EXPECT_FALSE(gravity.in_domain({0.0, 0.0, -1000.0})) << "at the floor";
  EXPECT_FALSE(gravity.in_domain({0.0, 0.0, -1001.0})) << "below the floor";
}

TEST(InverseSquareGravity, InDomainFalseForANonFiniteAltitude) {
  GravityModel& gravity = load(1000.0);
  EXPECT_FALSE(gravity.in_domain({0.0, 0.0, std::numeric_limits<double>::quiet_NaN()}));
}

// validate_p_n and in_domain share the same domain_fault definition (ADR-0012) -- this is the
// existing behavior test, now expressed against the shared helper rather than a standalone check.
TEST(InverseSquareGravity, ValidatePnRejectsExactlyWhatInDomainRejects) {
  GravityModel& gravity = load(1000.0);
  const Config node = Config::from_string("{}\n", "inverse-square-validate-fixture");
  EXPECT_NO_THROW(gravity.validate_p_n({0.0, 0.0, -999.0}, node));
  EXPECT_THROW(gravity.validate_p_n({0.0, 0.0, -1000.0}, node), ConfigError);
  try {
    gravity.validate_p_n({0.0, 0.0, std::numeric_limits<double>::quiet_NaN()}, node);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_NE(std::string_view{error.what()}.find("finite"), std::string_view::npos)
        << error.what();
  }
}

}  // namespace
