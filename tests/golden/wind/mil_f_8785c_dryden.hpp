// MIL-F-8785C Dryden turbulence model constants (PLAN.md M2 PR 2b, docs/architecture.md §16:
// "Dryden PSD matches MIL-F-8785C"). Human-owned: lives under tests/golden/ so guard-oracle.sh
// blocks agent edits. Transcribed directly from docs/refs/MIL-F-8785C.pdf (read at high DPI, not
// through the scan's own OCR layer, which mis-renders several of these exponents), cross-checked
// against MathWorks' "Dryden Wind Turbulence Model (Continuous)" documentation
// (mathworks.com/help/aeroblks/drydenwindturbulencemodelcontinuous.html), which states the same
// MIL-F-8785C constants independently and, for kLowAltitudeVerticalScaleIsAltitude below, in words
// where the primary document only implies it graphically -- both sources agree on every value here.
//
// Two different roles, by design, not an inconsistency:
//
// The scale-length/intensity CONSTANTS below (kLowAltitudeBracketConst etc.) are data only, no
// formula: plugins/physical/wind/dryden/dryden_filter.hpp independently transcribes the same
// numbers and implements the formulas that combine them, and tests/unit/wind/dryden_filter_test.cpp
// re-derives its own expected values from THIS file's raw constants, sharing only the numbers, not
// the arithmetic -- so a bug in how the implementation combines them (a flipped exponent's
// placement, a multiply where the spec divides) is still caught, the same reasoning
// isa1976_table.hpp's own header gives for keeping the golden table generator independent of the
// plugin under test.
//
// The SPECTRAL FORMS below (phi_u, phi_vw) are different: nothing in plugins/ computes a PSD --
// dryden.cpp only runs the time-domain shaping filter -- so there is no second, independently-typed
// implementation to cross-check against, and duplicating these two closed forms into
// tests/physics/dryden_psd_test.cpp as well would be the same formula transcribed twice with no
// guard on either copy drifting from MIL-F-8785C (Codex review, PR #26: dryden_psd_test.cpp's own
// phi_u/phi_vw sat outside every guarded path even though they transcribe the same section
// 3.7.1.2, p.47 the constants above are cited to). One copy, here, guarded; the test calls it.
#pragma once

#include <numbers>

namespace astra::test::mil_f_8785c {

// Figure 10, "Low-altitude turbulence integral scales" (MIL-F-8785C p.55): printed formula box,
//   L_u = L_v = h / (0.177 + 0.000823*h)^1.2,  for 10 < h < 1000 ft
// h in feet, L in feet. Confirmed by direct high-DPI read of the page (a horizontal fraction bar,
// "h" over the bracket to the 1.2 power) and by MathWorks' rendering of the same MIL-F-8785C
// relation.
inline constexpr double kLowAltitudeBracketConst = 0.177;
inline constexpr double kLowAltitudeBracketSlopePerFoot = 0.000823;
inline constexpr double kLowAltitudeLengthExponent = 1.2;
// Figure 10's own stated domain for the formula above.
inline constexpr double kLowAltitudeMinFt = 10.0;
inline constexpr double kLowAltitudeMaxFt = 1000.0;

// L_w = h below 1000 ft: NOT printed as a formula on MIL-F-8785C's Figure 10 page (the page's
// second curve implies it graphically but the page carries no accompanying text formula for it,
// unlike L_u = L_v above). Confirmed instead from MathWorks' page, which states explicitly, citing
// MIL-F-8785C: "Lw = h". No separate numeric constant needed -- this is a boolean marker that the
// relation is the identity, kept here so a reader auditing this file for "did we transcribe L_w"
// finds an explicit answer rather than an omission.
inline constexpr bool kLowAltitudeVerticalScaleIsAltitude = true;

// Figure 11, "Horizontal turbulence RMS intensities" (MIL-F-8785C p.56): printed formula box,
//   sigma_u/sigma_w = sigma_v/sigma_w = 1 / (0.177 + 0.000823*h)^0.4,  below 1000 ft
// Same bracket as Figure 10 (kLowAltitudeBracketConst/kLowAltitudeBracketSlopePerFoot above), a
// different exponent.
inline constexpr double kLowAltitudeIntensityExponent = 0.4;

// Section 3.7.3.4, p.53: "The turbulence intensities to be used are sigma_w = 0.1*u20" -- a
// mixed-unit relation (sigma_w in ft/s, u20 in knots, per section 3.7.3.1's figure 9, p.51-52,
// whose x-axis is explicitly "Mean Wind Speed, Knots"), transcribed here as the bare ratio; the
// unit mismatch is the caller's boundary conversion to perform (ADR-0002), not something folded
// into this constant.
inline constexpr double kSigmaWToW20Ratio = 0.1;

// Section 3.7.2.1, p.48: "The scales to be used are ... L_u = L_v = L_w = 1,750 feet using the
// Dryden form" for the medium/high-altitude model (isotropic, above 2000 ft per section 3.7.2's own
// isotropy assumption stated at p.48's top). The von Karman form's 2,500 ft on the same page is not
// transcribed: this plugin implements the Dryden form only (PLAN.md).
inline constexpr double kMediumHighAltitudeScaleLengthFt = 1750.0;
// Section 3.7.2, p.48: "above 2,000 feet."
inline constexpr double kMediumHighAltitudeMinFt = 2000.0;

// Section 3.7.1.2, p.47, "Turbulence model (Dryden form)": the spatial-frequency spectra
//   Phi_u(Omega) = sigma_u^2 * (2*L_u/pi) / (1 + (L_u*Omega)^2)
//   Phi_v(Omega) = Phi_w(Omega) = sigma^2 * (L/pi) * (1 + 3*(L*Omega)^2) / (1 + (L*Omega)^2)^2
// converted to the temporal form these functions take via Taylor's frozen-turbulence hypothesis,
// s = j*V*Omega (equivalently omega = V*Omega, a = V/L): dryden_filter.hpp's own header derives and
// cites this conversion (Joe: verified there, not re-derived here) and states the resulting transfer
// functions G_u(s) = sigma_u*sqrt(2a)/(s+a), G_vw(s) = sigma*sqrt(a)*(sqrt(3)*s+a)/(s+a)^2, whose
// squared magnitude over pi is exactly the two closed forms below. a (= V/L, dryden_filter.hpp's
// gust_rate()) is the caller's to compute and pass in -- that division is V/L plumbing, not part of
// the document's spectral shape, and stays out of this file the same way it stays out of the
// constants above.
//
// Phi_u(omega) = sigma_u^2 * (2*a) / (pi * (a^2 + omega^2))
[[nodiscard]] inline double phi_u(double omega, double sigma_u, double a) noexcept {
  return sigma_u * sigma_u * (2.0 * a) / (std::numbers::pi * ((a * a) + (omega * omega)));
}

// Phi_v(omega) = Phi_w(omega) = sigma^2 * a * (a^2 + 3*omega^2) / (pi * (a^2 + omega^2)^2) -- one
// function for both axes, exactly as the document gives one shared closed form for v and w (they
// differ only in the L/sigma the caller supplies through a and sigma, never in this formula).
[[nodiscard]] inline double phi_vw(double omega, double sigma, double a) noexcept {
  const double a_sq = a * a;
  const double omega_sq = omega * omega;
  const double denom = (a_sq + omega_sq) * (a_sq + omega_sq);
  return sigma * sigma * a * (a_sq + (3.0 * omega_sq)) / (std::numbers::pi * denom);
}

}  // namespace astra::test::mil_f_8785c
