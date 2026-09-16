// MIL-F-8785C Dryden turbulence model constants (PLAN.md M2 PR 2b, docs/architecture.md §16:
// "Dryden PSD matches MIL-F-8785C"). Human-owned: lives under tests/golden/ so guard-oracle.sh
// blocks agent edits. Transcribed directly from docs/refs/MIL-F-8785C.pdf (read at high DPI, not
// through the scan's own OCR layer, which mis-renders several of these exponents), cross-checked
// against MathWorks' "Dryden Wind Turbulence Model (Continuous)" documentation
// (mathworks.com/help/aeroblks/drydenwindturbulencemodelcontinuous.html), which states the same
// MIL-F-8785C constants independently and, for kLowAltitudeVerticalScaleIsAltitude below, in words
// where the primary document only implies it graphically -- both sources agree on every value here.
//
// Deliberately data only, no formula: plugins/physical/wind/dryden/dryden_filter.hpp implements the
// formulas that combine these constants, and tests/unit/wind/dryden_filter_test.cpp re-derives its
// own expected values from these raw constants independently of dryden_filter.hpp's own code --
// sharing only the numbers transcribed here, not the arithmetic, so a bug in how the implementation
// combines them (a flipped exponent's placement, a multiply where the spec divides) is still
// caught, the same reasoning isa1976_table.hpp's own header gives for keeping the golden table
// generator independent of the plugin under test.
#pragma once

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

}  // namespace astra::test::mil_f_8785c
