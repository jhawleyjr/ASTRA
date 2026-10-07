// Shared schema for the per-case NESC oracle bands (ADR-0013, ADR-0014, ADR-0015, PLAN.md M2 PR
// 3b; ADR-0028, M7 PR 8): one channel band per compared channel, aggregated into one case band per
// case. The case*_band.hpp files hold the actual generated data; this header is pure structure,
// hand-written rather than regenerated, so every one of those files shares exactly one definition
// of what "reference" and "tol_abs" mean. guard-oracle.sh still protects it (guarded parent
// tests/golden/nesc/): a structural change here changes what every generated file means, the same
// reasoning that keeps a transcribed golden human-owned.
//
// Sample k corresponds to major frame k*100 (ADR-0013: dt = 1 ms, 10 Hz grid is exactly a scheduler
// divisor of 100), i.e. t = k*100*0.001 s -- computed by the test from the loop index, never stored
// here or in a generated file. ADR-0028 item 4: the sample is matched by frame index, never by
// equality of doubles.
//
// Each case carries its own sample count and first gated sample in its band's type,
// CaseBandOf<SampleCount, FirstGatedSample> (ADR-0028 items 3 and 5). Cases 1 to 10 keep ADR-0013's
// shape through the ChannelBand and CaseBand aliases below: 301 samples, every sample gated. Their
// generated headers name only those aliases, so they compile unchanged.
#pragma once

namespace astra::test::nesc {

inline constexpr int kSampleCount = 301;  // cases 1 to 10: t = 0, 0.1, ..., 30.0 s

template <int SampleCount>
struct ChannelBandOf {
  // Per-sample mean across participating sims (ADR-0013: "reference = mean across participating
  // sims"; ADR-0028 item 6: SIM 4 and SIM 5's midpoint for cases 11 to 16). A data table; a C array
  // keeps the generated literal braces one level deep, the same reasoning
  // tests/golden/atmosphere/isa1976_table.hpp already gives.
  // NOLINTBEGIN(modernize-avoid-c-arrays)
  double reference[SampleCount];
  // NOLINTEND(modernize-avoid-c-arrays)
  // ADR-0013: "tol_abs = k * max over t of (max - min across sims)" -- a SINGLE scalar per channel
  // per case, the worst-case spread, not a per-sample value; ADR-0014's floor applies to this same
  // scalar (`max(k*max_t(spread), floor)`). ADR-0015: the pass criterion is
  // `|astra - reference| <= tol_abs` alone -- there is no relative term. ADR-0028 item 5: for cases
  // 11 to 16 the max runs over the gated samples only.
  double tol_abs;
};

// One case's full compared-channel set (docs/specs/nesc-harness.md's Contract section). Aggregates
// references to the 21 named channel band constants each case*_band.hpp defines, so one comparison
// routine serves every case. kFirstGatedSample is the first sample the pass test applies to
// (ADR-0028 item 5); the samples before it are still compared and logged, never asserted.
template <int SampleCount, int FirstGatedSample>
struct CaseBandOf {
  static_assert(0 <= FirstGatedSample && FirstGatedSample < SampleCount);
  static constexpr int kSampleCount = SampleCount;
  static constexpr int kFirstGatedSample = FirstGatedSample;
  using Channel = ChannelBandOf<SampleCount>;

  const Channel& latitude;
  const Channel& longitude;
  const Channel& altitude;
  const Channel& v_north;
  const Channel& v_east;
  const Channel& v_down;
  const Channel& yaw;
  const Channel& pitch;
  const Channel& roll;
  const Channel& rate_p;
  const Channel& rate_q;
  const Channel& rate_r;
  const Channel& force_x;
  const Channel& force_y;
  const Channel& force_z;
  const Channel& moment_l;
  const Channel& moment_m;
  const Channel& moment_n;
  const Channel& density;
  const Channel& pressure;
  const Channel& temperature;
};

// Cases 1 to 10 (ADR-0013): 301 samples, every sample gated.
using ChannelBand = ChannelBandOf<kSampleCount>;
using CaseBand = CaseBandOf<kSampleCount, 0>;

}  // namespace astra::test::nesc
