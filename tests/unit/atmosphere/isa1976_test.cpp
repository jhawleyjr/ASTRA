// plugins/physical/atmosphere/isa1976 against tests/golden/atmosphere/isa1976_table.hpp
// (PLAN.md M2 PR 1b, docs/architecture.md §16: "ISA table points exact"). That header's two
// arrays are judged differently here: kTranscribedRows (read verbatim off the document) at
// printed precision -- the one comparison against the document itself, not against another
// implementation of the same formula; kComputedRows (an independent Python re-implementation,
// tests/golden/atmosphere/generate_isa1976_table.py) at a looser, libm-disagreement tolerance,
// dense enough to also exercise continuity across every layer boundary.
#include <gtest/gtest.h>

#include <cmath>

#include "astra/config/config.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "golden/atmosphere/isa1976_table.hpp"

namespace {

using astra::AltitudeRange;
using astra::AtmosphereModel;
using astra::AtmoState;
using astra::Config;

// isa1976's init() reads no config and registers no signal (an empty override), so calling it
// again on every load() is harmless -- the wgs84_j2_test.cpp / earth_kinematics_test.cpp pattern.
AtmosphereModel& load() {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded = loader.load("plugins/physical/atmosphere/isa1976");
  auto* const atmosphere = dynamic_cast<AtmosphereModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  atmosphere->init(Config::from_string("{}\n", "isa1976-fixture"), registry);
  return *atmosphere;
}

// eq 18 (doc p.8) and its inverse eq 19, restated here rather than exposed from isa1976.cpp: an
// independent way to turn a layer's geopotential boundary into the geometric altitude at() takes,
// so the continuity test below drives at() the same way a caller would, not through a back door.
constexpr double kEffectiveRadiusM = 6'356'766.0;  // r0; see isa1976.cpp's own citation
double geometric_from_geopotential(double h_m) noexcept {
  return kEffectiveRadiusM * h_m / (kEffectiveRadiusM - h_m);
}

TEST(Isa1976, MatchesTranscribedTablePointsAtPrintedPrecision) {
  AtmosphereModel& atmosphere = load();
  for (const auto& row : astra::test::isa1976::kTranscribedRows) {
    SCOPED_TRACE(row.altitude_m);
    const AtmoState state = atmosphere.at(row.altitude_m);
    const double rel = astra::test::isa1976::kTranscribedRelativeTolerance;
    EXPECT_NEAR(row.temperature_k, state.temperature_k,
                astra::test::isa1976::kTranscribedTemperatureToleranceK);
    EXPECT_NEAR(row.pressure_pa, state.pressure_pa, row.pressure_pa * rel);
    EXPECT_NEAR(row.density_kg_m3, state.density_kg_m3, row.density_kg_m3 * rel);
  }
}

TEST(Isa1976, MatchesComputedRowsAcrossEveryLayer) {
  AtmosphereModel& atmosphere = load();
  for (const auto& row : astra::test::isa1976::kComputedRows) {
    SCOPED_TRACE(row.altitude_m);
    const AtmoState state = atmosphere.at(row.altitude_m);
    const double rel = astra::test::isa1976::kComputedRelativeTolerance;
    EXPECT_NEAR(row.temperature_k, state.temperature_k, row.temperature_k * rel);
    EXPECT_NEAR(row.pressure_pa, state.pressure_pa, row.pressure_pa * rel);
    EXPECT_NEAR(row.density_kg_m3, state.density_kg_m3, row.density_kg_m3 * rel);
    EXPECT_NEAR(row.speed_of_sound_m_s, state.speed_of_sound_m_s, row.speed_of_sound_m_s * rel);
  }
}

TEST(Isa1976, MatchesTheDefiningSeaLevelValues) {
  const AtmoState state = load().at(0.0);
  EXPECT_DOUBLE_EQ(288.15, state.temperature_k);
  EXPECT_DOUBLE_EQ(101325.0, state.pressure_pa);
  EXPECT_NEAR(1.2250, state.density_kg_m3, 1e-4);
}

// docs/lessons.md 2026-09-15 (the tan(pi/2) entry): never assert finite/non-finite behavior right
// at a boundary reached only through floating-point rounding. min_m/max_m are exact literals here
// (not derived from an irrational quantity), so testing exactly at them is safe; "outside" uses
// values clearly past the bound, not a hair beyond it.
TEST(Isa1976, ValidRangeBoundsAreInclusive) {
  AtmosphereModel& atmosphere = load();
  const AltitudeRange range = atmosphere.valid_range();
  EXPECT_DOUBLE_EQ(-5000.0, range.min_m);
  EXPECT_DOUBLE_EQ(86000.0, range.max_m);
  EXPECT_TRUE(std::isfinite(atmosphere.at(range.min_m).temperature_k));
  EXPECT_TRUE(std::isfinite(atmosphere.at(range.max_m).temperature_k));
}

TEST(Isa1976, AtReturnsAllNanClearlyOutsideValidRange) {
  AtmosphereModel& atmosphere = load();
  for (const double altitude_m : {-6000.0, 90000.0}) {
    SCOPED_TRACE(altitude_m);
    const AtmoState state = atmosphere.at(altitude_m);
    EXPECT_TRUE(std::isnan(state.temperature_k));
    EXPECT_TRUE(std::isnan(state.pressure_pa));
    EXPECT_TRUE(std::isnan(state.density_kg_m3));
    EXPECT_TRUE(std::isnan(state.speed_of_sound_m_s));
  }
}

TEST(Isa1976, DefaultConstructedAtmoStateIsAllNan) {
  const AtmoState state;
  EXPECT_TRUE(std::isnan(state.temperature_k));
  EXPECT_TRUE(std::isnan(state.pressure_pa));
  EXPECT_TRUE(std::isnan(state.density_kg_m3));
  EXPECT_TRUE(std::isnan(state.speed_of_sound_m_s));
}

// rho = P*M0/(R*T) and a = sqrt(gamma*R*T/M0) (eq 42/50, doc p.15/18): a check independent of the
// golden, over the same constants isa1976.cpp cites in its own header comment.
TEST(Isa1976, DensityAndSpeedOfSoundAreSelfConsistentWithPressureAndTemperature) {
  constexpr double kGasConstant = 8314.32;
  constexpr double kMeanMolecularWeight = 28.9644;
  constexpr double kSpecificHeatRatio = 1.40;
  AtmosphereModel& atmosphere = load();
  for (const double altitude_m : {-5000.0, 0.0, 11000.0, 20000.0, 47000.0, 71000.0, 86000.0}) {
    SCOPED_TRACE(altitude_m);
    const AtmoState state = atmosphere.at(altitude_m);
    const double expected_density =
        state.pressure_pa * kMeanMolecularWeight / (kGasConstant * state.temperature_k);
    const double expected_speed_of_sound =
        std::sqrt(kSpecificHeatRatio * kGasConstant * state.temperature_k / kMeanMolecularWeight);
    EXPECT_NEAR(expected_density, state.density_kg_m3, expected_density * 1e-9);
    EXPECT_NEAR(expected_speed_of_sound, state.speed_of_sound_m_s, expected_speed_of_sound * 1e-9);
  }
}

// Each geopotential layer boundary, approached from both sides in the GEOMETRIC altitude at()
// actually takes: T is continuous by eq 23's own construction (both pieces evaluate to the same
// value at H_b), P by the base-pressure chain's construction -- so a jump here means the wrong
// layer is being picked on one side of the boundary, not a property of the physics.
TEST(Isa1976, IsContinuousAcrossEveryLayerBoundary) {
  AtmosphereModel& atmosphere = load();
  constexpr double kEpsilonM = 0.01;
  for (const double h_boundary_m : {11000.0, 20000.0, 32000.0, 47000.0, 51000.0, 71000.0}) {
    SCOPED_TRACE(h_boundary_m);
    const double z_below = geometric_from_geopotential(h_boundary_m - kEpsilonM);
    const double z_above = geometric_from_geopotential(h_boundary_m + kEpsilonM);
    const AtmoState below = atmosphere.at(z_below);
    const AtmoState above = atmosphere.at(z_above);
    // A one-sided formula error is at least an order of magnitude larger than what 0.02 m of
    // geopotential altitude can change T/P/rho by at any of these boundaries.
    EXPECT_NEAR(below.temperature_k, above.temperature_k, 1e-3);
    EXPECT_NEAR(below.pressure_pa, above.pressure_pa, below.pressure_pa * 1e-5);
    EXPECT_NEAR(below.density_kg_m3, above.density_kg_m3, below.density_kg_m3 * 1e-5);
  }
}

}  // namespace
