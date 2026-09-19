// plugins/physical/gravity/inverse_square's own domain (ADR-0012, M2 PR 2c): in_domain/
// validate_p_n against its own, separately configured radius_ -- not earth/round's, the Codex
// finding tests/unit/kernel/simulation_physical_config_test.cpp's
// RejectsAnInverseSquareRadiusInconsistentWithTheInitialAltitude already exercises end to end
// through Simulation::from_config. This file is the unit-level counterpart, the same shape as
// tests/unit/earth/earth_kinematics_test.cpp and tests/unit/gravity/wgs84_j2_test.cpp: loads the
// real .so through PluginLoader, checks in_domain/validate_p_n as pure functions.
//
// The `rotating` centripetal correction (docs/plans/nesc-case05-gravity-centripetal.md, D1) is
// checked against oracles independent of this plugin's own implementation, not by re-deriving the
// same formula a second time: an ECEF-plus-rotation construction using the already-existing
// astra::geodesy::ecef_to_ned_dcm (RotatingCentripetalMatchesAnIndependentEcefConstruction), a
// fully hand-computed literal at the equator (MatchesAHandComputedEquatorialValue), and a
// radius-scaling check (CorrectionScalesWithRadiusPlusAltitude). The ECEF oracle still shares
// astra::geodesy::kOmegaIe and the omega^2*r factor with the plugin under test (same author, same
// session), so the latter two checks are the ones genuinely independent of it.
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/geodesy.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::GravityModel;
using astra::StateVector;
using astra::VehicleState;

// std::to_string(double) formats with a fixed 6 digits after the decimal point, silently
// truncating a value like g0 = 9.820228349842926 to "9.820228" -- caught by
// MatchesAHandComputedEquatorialValue below failing by exactly that truncated amount before this
// helper existed. std::ostringstream with max_digits10 round-trips a double exactly.
std::string precise(double value) {
  std::ostringstream out;
  out.precision(std::numeric_limits<double>::max_digits10);
  out << value;
  return out.str();
}

GravityModel& load(double radius) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("plugins/physical/gravity/inverse_square");
  auto* const gravity = dynamic_cast<GravityModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  gravity->init(Config::from_string("radius: " + precise(radius) + "\n", "inverse-square-fixture"),
                registry);
  return *gravity;
}

// A separate loader/loaded static pair from load() above (own instance, own lifetime), for the
// D1 rotating tests below, which need g0/rotating overrides load() does not take.
GravityModel& load_rotating(double radius, double g0, bool rotating) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("plugins/physical/gravity/inverse_square");
  auto* const gravity = dynamic_cast<GravityModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  gravity->init(Config::from_string("radius: " + precise(radius) + "\n" + "g0: " + precise(g0) +
                                        "\n" + "rotating: " + (rotating ? "true" : "false") + "\n",
                                    "inverse-square-rotating-fixture"),
                registry);
  return *gravity;
}

StateVector state_at(double lat, double lon, double alt) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kPositionSize>(astra::state::kPosition) = Eigen::Vector3d{lat, lon, alt};
  y(astra::state::kQuaternion) = 1.0;
  return y;
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

// R2 (docs/plans/nesc-case05-gravity-centripetal.md): the plugin's own g_n() output, not a
// test-local re-derivation of its formula, checked against an ECEF-plus-rotation oracle built
// from astra::geodesy::ecef_to_ned_dcm (reusable for a plain sphere: it depends only on lat/lon,
// not on the WGS-84 ellipsoid). Both hemispheres and a nonzero longitude are exercised: case05
// alone, at the equator, would pass even a sign error in the north component. Tolerance is
// absolute, scaled to the correction's own characteristic magnitude (omega^2 * r) rather than
// relative to each component's own value -- a relative tolerance breaks down at a
// near-equatorial latitude, where the north component itself is tiny even though the overall
// correction is not.
TEST(InverseSquareGravity, RotatingCentripetalMatchesAnIndependentEcefConstruction) {
  constexpr double kRadius = 6371007.1809;
  constexpr double kAltitude = 9144.0;
  constexpr double kG0 = 9.80665;
  constexpr double kLon = 0.647;  // nonzero, arbitrary (mirrors wgs84_j2_test.cpp's own choice)
  GravityModel& gravity = load_rotating(kRadius, kG0, true);
  const double omega = astra::geodesy::kOmegaIe;
  const double r = kRadius + kAltitude;
  const double ratio = kRadius / r;
  const double pure_radial_down = kG0 * ratio * ratio;

  for (const double lat_deg : {-60.0, -30.0, -1e-3, 30.0, 60.0}) {
    SCOPED_TRACE(lat_deg);
    const double lat = lat_deg * std::numbers::pi / 180.0;
    const Eigen::Vector3d g = gravity.g_n(VehicleState{state_at(lat, kLon, kAltitude)});
    const Eigen::Vector3d residual{g(0), g(1), g(2) - pure_radial_down};

    const Eigen::Vector3d r_ecef{r * std::cos(lat) * std::cos(kLon),
                                 r * std::cos(lat) * std::sin(kLon), r * std::sin(lat)};
    const Eigen::Vector3d centrifugal_ecef{omega * omega * r_ecef(0), omega * omega * r_ecef(1),
                                           0.0};
    const Eigen::Vector3d oracle = astra::geodesy::ecef_to_ned_dcm(lat, kLon) * centrifugal_ecef;

    const double tolerance = omega * omega * r * 1e-9;  // absolute, scaled to the whole term
    EXPECT_NEAR(oracle(0), residual(0), tolerance) << "north";
    EXPECT_NEAR(oracle(1), residual(1), tolerance) << "east";
    EXPECT_NEAR(oracle(2), residual(2), tolerance) << "down";
  }
}

// A fully hand-computed literal, sharing no formula or helper with the plugin or with the ECEF
// oracle above (Cowork: that oracle still shares astra::geodesy::kOmegaIe and the omega^2*r
// factor with the implementation, same author/session).
TEST(InverseSquareGravity, MatchesAHandComputedEquatorialValue) {
  // Table 73 (NASA/TM-2015-218675 Vol II): radius = 6371007.1809 m,
  // g0 = GM/radius^2 = 9.820228349842926 m/s^2; omega = 7.292115e-5 rad/s (same table).
  // At the equator (lat = h = 0): g_down = g0 - omega^2 * radius.
  constexpr double kRadius = 6371007.1809;
  constexpr double kG0 = 9.820228349842926;
  constexpr double kOmega = 7.292115e-5;
  const double expected_down = kG0 - kOmega * kOmega * kRadius;

  GravityModel& gravity = load_rotating(kRadius, kG0, true);
  const Eigen::Vector3d g = gravity.g_n(VehicleState{state_at(0.0, 0.0, 0.0)});
  EXPECT_NEAR(expected_down, g(2), 1e-9);
  EXPECT_NEAR(0.0, g(0), 1e-9) << "north is exactly zero at the equator";
  EXPECT_NEAR(0.0, g(1), 1e-9) << "east is always zero";
}

// The correction must scale with (radius_ + h), not with radius_ or h alone -- catches a bug
// that used the wrong one of the two.
TEST(InverseSquareGravity, CorrectionScalesWithRadiusPlusAltitude) {
  constexpr double kRadius = 1.0e7;
  constexpr double kG0 = 9.80665;
  const double lat = 37.0 * std::numbers::pi / 180.0;
  GravityModel& gravity = load_rotating(kRadius, kG0, true);

  const double ratio1 = kRadius / (kRadius + 1000.0);
  const Eigen::Vector3d g1 = gravity.g_n(VehicleState{state_at(lat, 0.0, 1000.0)});
  const double correction1 = g1(2) - kG0 * ratio1 * ratio1;

  const double ratio2 = kRadius / (kRadius + 5000.0);
  const Eigen::Vector3d g2 = gravity.g_n(VehicleState{state_at(lat, 0.0, 5000.0)});
  const double correction2 = g2(2) - kG0 * ratio2 * ratio2;

  const double expected_ratio = (kRadius + 5000.0) / (kRadius + 1000.0);
  EXPECT_NEAR(expected_ratio, correction2 / correction1, 1e-9);
}

TEST(InverseSquareGravity, IsIndependentOfLongitudeWhenRotating) {
  constexpr double kRadius = 6371007.1809;
  GravityModel& gravity = load_rotating(kRadius, 9.80665, true);
  const double lat = 37.0 * std::numbers::pi / 180.0;
  const Eigen::Vector3d at_0 = gravity.g_n(VehicleState{state_at(lat, 0.0, 1000.0)});
  const Eigen::Vector3d at_180 =
      gravity.g_n(VehicleState{state_at(lat, std::numbers::pi - 0.01, 1000.0)});
  EXPECT_NEAR(at_0(2), at_180(2), 1e-9) << "down";
  EXPECT_NEAR(at_0(0), at_180(0), 1e-9) << "north";
}

// R3: rotating: false (the default) reproduces the exact pre-fix g_n bit-for-bit, at a nonzero,
// nonequatorial latitude too -- not only the equator, where the correction happens to be
// down-only regardless. EXPECT_DOUBLE_EQ cannot distinguish +0.0 from -0.0 (they compare
// arithmetically equal), so an implementation that constructed the correction standalone instead
// of adding it to the existing literal {0,0,g} vector could silently return -0.0 for the north
// component in the northern hemisphere and this test would not catch it; std::bit_cast makes the
// two bit patterns distinguishable, the same discipline the determinism tests already use.
TEST(InverseSquareGravity, NonRotatingLeavesGCorrectionExactlyZeroAwayFromTheEquator) {
  constexpr double kRadius = 6371000.0;
  constexpr double kG0 = 9.80665;
  GravityModel& gravity = load(kRadius);
  const double lat = 37.0 * std::numbers::pi / 180.0;
  const Eigen::Vector3d g = gravity.g_n(VehicleState{state_at(lat, 0.0, 5000.0)});

  const double ratio = kRadius / (kRadius + 5000.0);
  const Eigen::Vector3d expected{0.0, 0.0, kG0 * ratio * ratio};

  EXPECT_EQ(std::bit_cast<std::uint64_t>(expected(0)), std::bit_cast<std::uint64_t>(g(0)))
      << "north";
  EXPECT_EQ(std::bit_cast<std::uint64_t>(expected(1)), std::bit_cast<std::uint64_t>(g(1)))
      << "east";
  EXPECT_EQ(std::bit_cast<std::uint64_t>(expected(2)), std::bit_cast<std::uint64_t>(g(2)))
      << "down";
}

}  // namespace
