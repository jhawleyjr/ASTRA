// U.S. Standard Atmosphere, 1976 (NOAA-S/T 76-1562 / NASA-TM-X-74335), 0 to 86 km geometric
// altitude (docs/architecture.md §7, PLAN.md M2 PR 1b). Every defining constant and the
// seven-layer geopotential temperature-height table below are transcribed straight from that
// document, cited page by page in tests/golden/atmosphere/isa1976_table.hpp's provenance block;
// the eight "table points exact" golden rows there were read from the same document independently
// of this file. tests/golden/atmosphere/generate_isa1976_table.py re-implements the equations
// below from scratch in Python from the same transcribed constants and cross-checks the base
// pressures computed here against the document's own tabulated values -- read that file before
// touching this one's numbers.
//
// Geometric altitude Z -> geopotential altitude H (eq 18, doc p.8): H = r0*Z/(r0+Z) (the g0/g0'
// ratio is exactly 1 m'/m). Layer b's molecular-scale temperature is linear in H (eq 23, doc p.10):
//   T_M = T_{M,b} + L_{M,b} * (H - H_b)
// Pressure is closed-form per layer (eq 33a/33b, doc p.12):
//   L_{M,b} != 0:  P = P_b * (T_{M,b} / T_M) ^ (g0'*M0 / (R* * L_{M,b}))
//   L_{M,b}  = 0:  P = P_b * exp(-g0'*M0*(H - H_b) / (R* * T_{M,b}))
// Density (eq 42, doc p.15): rho = P*M0 / (R* * T_M). Speed of sound (eq 50, doc p.18):
//   a = sqrt(gamma * R* * T_M / M0)
// All four equations use T_M (molecular-scale), not kinetic T: below 80 km the document defines
// the two to be identical (M = M0 exactly, §1.2.4), and this plugin does not implement the M/M0
// correction table (Table 8) that separates them over the narrow 79.006-84.852 km' band -- so the
// "temperature_k" this returns is T_M throughout, including at the very top of valid_range(),
// where the true kinetic temperature differs from it by 0.08 K (the document's own number,
// §1.2.6). Out of scope: a further ~30-row interpolation for a 0.04%-at-most band nothing else in
// this plugin needs.
//
// std::pow/std::exp are not constexpr under clang-18 -std=c++23 (probed this session: both fail
// "must be initialized by a constant expression" at namespace scope), so the layer base pressures
// P_b are computed once in init(), integrating upward from the defining sea-level P_0 = 101325 Pa
// exactly as the document itself does (doc p.12: P_b for b=1..6 comes from applying eq 33a/33b for
// the *previous* layer at H = H_b). The T_{M,b} chain involves only +, -, * of compile-time
// constants, so it stays constexpr.
//
// Known gap, disclosed not fixed (Codex adversarial review, M2 PR 1b): at()'s altitude_m is
// whatever EarthModel::altitude() returns for the configured Earth model (AltitudeRange's own doc
// comment), and for earth/wgs84_rotating that is height above the WGS-84 ELLIPSOID (ADR-0003's
// p_n(2), returned verbatim -- see that plugin's altitude()), not height above a sea-level/geoid
// reference, which is what ISA-1976's "geometric altitude" implicitly assumes. The two differ by
// the geoid undulation, roughly -105 to +85 m depending on location -- earth/flat and earth/round
// have no such gap (NED altitude and height-above-sphere are both exact by construction). Via the
// barometric approximation (density ~ exp(-dh/H), scale height H ~ 8500 m near sea level), this
// bounds the resulting density error at about 1.3% for wgs84_rotating + isa1976. Fixing it needs a
// geoid model (e.g. EGM96/EGM2008) -- a new data dependency and its own plan-reviewed PR, not a
// bolt-on here. PLAN.md's M2 PR 3 line records that PR 3's NESC drag cases are the first thing this
// can actually move (this PR's own acceptance, ISA table points exact, never touches
// wgs84_rotating).
//
// Config keys:
//   none
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::AltitudeRange;
using astra::AtmoState;

// Category I/II defining constants (Table 2, doc p.2; g0'/T0/P0 restated in prose on doc p.3-4).
// NOLINTBEGIN(readability-magic-numbers): every value here is a cited defining constant, not an
// arbitrary literal.
constexpr double kG0Prime = 9.80665;              // m^2/(s^2.m'); numerically equal to g0
constexpr double kEffectiveRadius = 6356766.0;    // r0, m (doc p.8: "taken as 6,356,766 m")
constexpr double kGasConstant = 8314.32;          // R*, N.m/(kmol.K)
constexpr double kMeanMolecularWeight = 28.9644;  // M0, kg/kmol (doc p.9, eq 20/21)
constexpr double kSpecificHeatRatio = 1.40;       // gamma, dimensionless
constexpr double kSeaLevelTemperature = 288.15;   // T0, K
constexpr double kSeaLevelPressure = 101325.0;    // P0, Pa (doc: 1.013250e5 N/m^2 exactly)

constexpr std::size_t kLayerCount = 7;

// Table 4 (doc p.3): base geopotential altitude H_b (km' -> m) and the molecular-scale-temperature
// gradient L_{M,b} (K/km' -> K/m) for each of the seven layers, b = 0..6. The trailing 84852.0 is
// H_7 (84.8520 km', doc p.3): the upper edge of the last layer, not a layer of its own -- kept here
// only so this table matches the document's Table 4 read top to bottom.
constexpr std::array<double, kLayerCount + 1> kLayerBaseGeopotentialM = {
    0.0, 11000.0, 20000.0, 32000.0, 47000.0, 51000.0, 71000.0, 84852.0};
constexpr std::array<double, kLayerCount> kLayerGradientKPerM = {-0.0065, 0.0,     0.001, 0.0028,
                                                                 0.0,     -0.0028, -0.002};
// NOLINTEND(readability-magic-numbers)

// T_{M,b} at each layer's base, applying eq 23 recursively from T_{M,0} = T0 (doc p.10's own
// construction of the profile). Only +, -, * of the compile-time constants above, so this stays
// constexpr even though the pressures below cannot.
constexpr std::array<double, kLayerCount> layer_base_temperatures() {
  std::array<double, kLayerCount> t{};
  t[0] = kSeaLevelTemperature;
  for (std::size_t b = 1; b < kLayerCount; ++b) {
    t[b] = t[b - 1] + kLayerGradientKPerM[b - 1] *
                          (kLayerBaseGeopotentialM[b] - kLayerBaseGeopotentialM[b - 1]);
  }
  return t;
}
constexpr std::array<double, kLayerCount> kLayerBaseTemperatureK = layer_base_temperatures();

// eq 18 (doc p.8), Gamma = g0/g0' = 1 m'/m exactly, so it drops out of the expression entirely.
double geopotential_from_geometric(double z_m) noexcept {
  return kEffectiveRadius * z_m / (kEffectiveRadius + z_m);
}

// The largest b in [0, kLayerCount) with kLayerBaseGeopotentialM[b] <= h_m, clamped to 0 below the
// first breakpoint: eq 33a is stated to apply "for H from 0 to -5 km'" too (doc p.12), i.e. layer
// 0's formula extends below its nominal base, which is exactly what clamping to 0 gives.
std::size_t layer_index(double h_m) noexcept {
  std::size_t layer = 0;
  for (std::size_t i = 1; i < kLayerCount; ++i) {
    if (h_m < kLayerBaseGeopotentialM[i]) {
      break;
    }
    layer = i;
  }
  return layer;
}

class Isa1976 final : public astra::AtmosphereModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {
    base_pressures_pa_[0] = kSeaLevelPressure;
    for (std::size_t b = 1; b < kLayerCount; ++b) {
      base_pressures_pa_[b] = pressure_within_layer(b - 1, kLayerBaseGeopotentialM[b]);
    }
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "atmosphere"; }

  [[nodiscard]] AtmoState at(double altitude_m) const noexcept override {
    // clang-tidy's "simplified" altitude_m < min || altitude_m > max is NOT equivalent for a NaN
    // input -- both comparisons are false for NaN, so that form would skip this branch instead of
    // catching it. The form below is true for NaN (both >= and <= are false, so the && is false,
    // so the ! is true), which is what makes a NaN altitude fall through to the all-NaN default.
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    if (!(altitude_m >= kValidRange.min_m && altitude_m <= kValidRange.max_m)) {
      return AtmoState{};  // all-NaN default (atmosphere_model.hpp)
    }
    const double h_m = geopotential_from_geometric(altitude_m);
    const std::size_t layer = layer_index(h_m);
    const double temperature_k =
        kLayerBaseTemperatureK[layer] +
        kLayerGradientKPerM[layer] * (h_m - kLayerBaseGeopotentialM[layer]);
    const double pressure_pa = pressure_within_layer(layer, h_m);
    const double density_kg_m3 =
        pressure_pa * kMeanMolecularWeight / (kGasConstant * temperature_k);
    const double speed_of_sound_m_s =
        std::sqrt(kSpecificHeatRatio * kGasConstant * temperature_k / kMeanMolecularWeight);
    return {temperature_k, pressure_pa, density_kg_m3, speed_of_sound_m_s};
  }

  [[nodiscard]] AltitudeRange valid_range() const noexcept override { return kValidRange; }

 private:
  // Geometric metres: the span the document's own Table I tabulates (doc p.51-68). 86000 m
  // geometric is exactly H = 84852 m geopotential, the mesopause boundary the layer table ends at
  // (doc p.10: "84.8520 km' (86.000 km)").
  static constexpr AltitudeRange kValidRange{-5000.0,
                                             86000.0};  // NOLINT(readability-magic-numbers)

  // eq 33a for L_{M,b} != 0, eq 33b for L_{M,b} == 0, evaluated at geopotential h_m using layer b's
  // own base pressure/temperature. Shared between init()'s base-pressure chain and at().
  [[nodiscard]] double pressure_within_layer(std::size_t layer, double h_m) const noexcept {
    const double lapse = kLayerGradientKPerM[layer];
    const double base_h = kLayerBaseGeopotentialM[layer];
    const double base_t = kLayerBaseTemperatureK[layer];
    const double base_p = base_pressures_pa_[layer];
    if (lapse == 0.0) {
      return base_p *
             std::exp(-kG0Prime * kMeanMolecularWeight * (h_m - base_h) / (kGasConstant * base_t));
    }
    const double t = base_t + lapse * (h_m - base_h);
    return base_p * std::pow(base_t / t, kG0Prime * kMeanMolecularWeight / (kGasConstant * lapse));
  }

  std::array<double, kLayerCount> base_pressures_pa_{};
};

std::unique_ptr<astra::Model> create() { return std::make_unique<Isa1976>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "atmosphere_isa1976",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
