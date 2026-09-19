// Shared schema for the per-case NESC oracle bands (ADR-0013, ADR-0014, PLAN.md M2 PR 3b): one
// ChannelBand per compared channel, aggregated into one CaseBand per case. The ten case*_band.hpp
// files hold the actual generated data; this header is pure structure, hand-written once rather
// than regenerated, so every one of those ten files shares exactly one definition of what
// "reference", "tol_abs", and "tol_rel" mean. guard-oracle.sh still protects it (guarded parent
// tests/golden/nesc/): a structural change here changes what every generated file means, the same
// reasoning that keeps a transcribed golden human-owned.
//
// Sample k corresponds to major frame k*100 (ADR-0013: dt = 1 ms, 10 Hz grid is exactly a scheduler
// divisor of 100), i.e. t = k*100*0.001 s -- computed by the test from the loop index, never stored
// here or in a generated file, so it compares bit-identically against Simulation::clock() the same
// way ADR-0013's own "100*0.001 == 0.1" argument relies on.
#pragma once

namespace astra::test::nesc {

inline constexpr int kSampleCount = 301;  // t = 0, 0.1, ..., 30.0 s

struct ChannelBand {
  // Per-sample mean across participating sims (ADR-0013: "reference = mean across participating
  // sims"). A data table; a C array keeps the generated literal braces one level deep, the same
  // reasoning tests/golden/atmosphere/isa1976_table.hpp already gives.
  // NOLINTBEGIN(modernize-avoid-c-arrays)
  double reference[kSampleCount];
  // NOLINTEND(modernize-avoid-c-arrays)
  // ADR-0013: "tol_abs = k * max over t of (max - min across sims)" -- a SINGLE scalar per channel
  // per case, the worst-case spread across every sample time, not a per-sample value; ADR-0014's
  // floor applies to this same scalar (`max(k*max_t(spread), floor)`).
  double tol_abs;
  // ADR-0013: "tol_rel = k * max over t of ((max-min)/|mean|), omitted where |mean| ~ 0" --
  // likewise a single scalar, 0.0 where omitted. Always 0.0 for yaw specifically (this session's
  // elaboration on ADR-0013's formula, Codex plan critique: yaw carries no relative term at all, at
  // any sample -- a relative term computed at one bad wrap-boundary instant would otherwise poison
  // this single case-wide scalar for every sample, not just that instant, since tol_rel is a max
  // over t. See generate_nesc_band.py's own header comment for the full reasoning).
  double tol_rel;
};

// One case's full compared-channel set (docs/specs/nesc-harness.md's Contract section). Aggregates
// references to the 21 named ChannelBand constants each case*_band.hpp defines, so
// tests/physics/nesc/nesc_cases_test.cpp can share one comparison routine across all ten cases
// instead of repeating it once per case.
struct CaseBand {
  const ChannelBand& latitude;
  const ChannelBand& longitude;
  const ChannelBand& altitude;
  const ChannelBand& v_north;
  const ChannelBand& v_east;
  const ChannelBand& v_down;
  const ChannelBand& yaw;
  const ChannelBand& pitch;
  const ChannelBand& roll;
  const ChannelBand& rate_p;
  const ChannelBand& rate_q;
  const ChannelBand& rate_r;
  const ChannelBand& force_x;
  const ChannelBand& force_y;
  const ChannelBand& force_z;
  const ChannelBand& moment_l;
  const ChannelBand& moment_m;
  const ChannelBand& moment_n;
  const ChannelBand& density;
  const ChannelBand& pressure;
  const ChannelBand& temperature;
};

}  // namespace astra::test::nesc
