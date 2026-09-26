// Output limits and the constant mode word of the example autopilot (docs/plans/m4-pr5-autopilot.md
// D4, D5).
//
// The autopilot clamps every command at these ranges itself. PR 6's sim-side transport.limits use
// the same values: a clamped value equals lo/hi and the sim halts only on v < lo or v > hi
// (ADR-0022), so equal limits raise no false halt while a broken clamp still does. C++17; nothing
// here includes anything but <cstdint>.
#ifndef ASTRA_EMBEDDED_AUTOPILOT_LIMITS_HPP
#define ASTRA_EMBEDDED_AUTOPILOT_LIMITS_HPP

#include <cstdint>

namespace astra_autopilot {

struct Range {
  double lo;
  double hi;
};

// rad. Elevator: docs/refs/aircrafts/737.xml lines 204-205 (+/-0.3); aileron, rudder: +/-0.35 (the
// same file and docs/plans/pr6-trim.md D10). Throttle 0..1 is the ICD's stated range
// (icd/astra_icd.h).
inline constexpr Range kElevatorLimit{-0.3, 0.3};
inline constexpr Range kAileronLimit{-0.35, 0.35};
inline constexpr Range kRudderLimit{-0.35, 0.35};
inline constexpr Range kThrottleLimit{0.0, 1.0};

// The bank command the heading loop may issue, rad (20 degrees). Keeps a heading correction inside
// the small-angle regime the linear design assumes (plan D3).
inline constexpr Range kBankCommandLimit{-0.35, 0.35};

// The ICD defines no mode bits in v1 and the sim records but never acts on mode_flags (M4 Q16).
// Nonzero on purpose: a zeroed or sim-fabricated reply is distinguishable from a real autopilot
// reply in the recorder.
inline constexpr std::uint32_t kModeFlags = 0x00000001U;

// A frame interval above this is not integrated (a stalled or resumed peer, not a sample period).
inline constexpr double kMaxIntegrationDt = 1.0;  // s

}  // namespace astra_autopilot

#endif  // ASTRA_EMBEDDED_AUTOPILOT_LIMITS_HPP
