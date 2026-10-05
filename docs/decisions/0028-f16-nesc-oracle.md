# ADR-0028: The F-16 NESC oracle: two members, a gated window, ADR-0013's scalar band

Status: **accepted**, 2026-10-05
Date: 2026-10-04
Owner: Joe
Related/supersedes: supersedes ADR-0013 **in part, for NESC atmospheric cases 11, 12, 13.1 to 13.4, 15 and 16
only**, in three places:
- its matching tolerance, 1e-6 s, becomes 1e-5 s;
- `tol_abs`'s "max over t" becomes the max over the gated window;
- the pass test applies at every gated sample, not every sample.

ADR-0013 is not edited, and cases 1 to 10 keep its text exactly. This record elaborates ADR-0014 (the floors
are unchanged) and ADR-0015: the absolute-only test, and a participant excluded for a whole case by one rule,
not by a per-channel list. Drafted with `docs/plans/m7-f16-nesc-oracle.md` (Q2 to Q4, R8) and
`docs/plans/m7-pr1a-adrs.md` (Joe's D1, D2 and amendments A1 to A3). Governs PLAN.md's M7 PR 8 (the generator
and the band headers) and the harness in PRs 9, 11a and 11b.

## Context

The F-16 cases of NASA/TM-2015-218675 are the first whole-aircraft check cases ASTRA flies. Their data is
human-placed in `docs/refs/Atmospheric_checkcases/Atmos_11_*` to `Atmos_16_*`, gitignored as ADR-0013
describes.

**The members.** Each case has exactly three trajectories: `sim_02`, `sim_04` and `sim_05`. All three sample
at 10 Hz. Vol II's own tables set each case's duration, and some files run longer:

| Case | Vol II duration | `sim_02` | `sim_04` | `sim_05` |
|---|---|---|---|---|
| 11, 12 | 180 s (Tables 35, 36) | 0 to 200 s | 0 to 180 s | 0 to 180 s |
| 13.1, 13.2 | 20 s (Tables 37, 38) | 0 to 20 s | 0 to 20 s | 0 to 60 s |
| 13.3 | 30 s (Table 39) | 0 to 30 s | 0 to 30 s | 0 to 239.9 s |
| 13.4 | 60 s (Table 40) | 0 to 60 s | 0 to 60 s | 0 to 239.9 s |
| 15, 16 | 180 s (Tables 42, 43) | 0 to 180 s | 0 to 180 s | 0 to 180 s |

`sim_05`'s times are float32.
- **Offsets.** Within each Vol II duration, its largest offset from the instants k/10 is 6.1e-6 s in cases 11,
  12, 15 and 16, 7.63e-7 s in 13.1 to 13.3, and 1.53e-6 s in 13.4. `sim_02` and `sim_04` are exact. ADR-0013's
  1e-6 s therefore fails in five cases.
- **A duplicate column.** `sim_05` carries `feVelocity_ft_s_Z` twice. The two copies are equal in every row of
  all eight cases.

**NASA's treatment.** Vol II's Table 79 (p.593) and Table 80 (p.594) carry the same six footnotes:
- (a) "The first 8 sec for all F-16 test cases due to initial transients (all simulation tools) were not
  included."
- (b) "SIM 2 results are ignored for all F-16 check-cases (11 through 16, 18 and 19) due to large trim
  differences."
- (c) "Tiny differences in vertical velocity for supersonic trim fly-out case 12 that led to large percentage
  errors (all simulation tools)."
- (d) "Mismatched timing of pitching and rolling moment and X-body force transients during altitude change
  test (check-case 13.1) for SIM 4 and 5."
- (e) "Mismatched timing of pitching moment transients during heading change and course side-step maneuvers
  (check-cases 13.3 and 13.4) for SIM 4 and 5."
- (f) "Mismatched timing of rolling moment and X-body force transients during North Pole circumnavigation test
  (check-case 15) for SIM 4 and 5."

**Unfootnoted cells.** Both tables' keys also mark cells "0.000 – data provided but not included in
calculation". In Table 80 some such cells carry no footnote. Within the generator's `CHANNELS`, they are SIM 5's
longitude, yaw, pitch and roll, and both members' latitude and temperature. Table 78 (p.592) marks SIM 5's Euler
angles the same way, without a footnote, for cases 1 to 10. `generate_nesc_band.py`'s `excluded()` never
excluded them: it applies only footnoted rules.

**The ground rule.** NASA's F-16 comparison has two members. Vol I §6.2.7 (p.13) set a ground rule: "a minimum
of three data sets (i.e., parameter trajectories) were necessary to warrant inclusion in this assessment". The
F-16 cases meet it only by counting SIM 2.

**Why the members differ** (Vol II D.1.11 to D.1.18):
- *Trim angular rate.* SIM 2 holds zero inertial rate. SIM 4 holds the pitch rate that keeps pitch constant
  over the curved Earth. SIM 5 holds the rate that keeps all three Euler angles constant relative to the local
  vertical.
- *SIM 2.* It uses a geocentric-vertical gravity simplification and reports moments at the MRC.
- *SIM 4 in case 15.* It starts untrimmed.
- *The LQR.* Only SIM 5 replaced the LQR's hard-coded reference states with its trim values.
- *Execution rate.* Differences in the control law's execution rate are named as a possible cause.

**The time grid.** ADR-0013 says `t = frame * dt` "compares equal to the sample instants as doubles". Compared
with the decimal instants k/10 read as doubles, `(100k) * 0.001` differs at 325 of k = 0 to 1800. Against
`k * 0.1` computed in double, the count is 296. Nothing relies on the claim:
- the harness advances by frame index (`tests/physics/nesc/nesc_cases_test.cpp`);
- `nesc_band_common.hpp` defines sample k as frame 100k.

**Measured preview** (a read-only scratch computation, not the generator; PR 8 recomputes it). The band is
`3 · max |sim4 − sim5|` at t ≥ 8 s, in SI, with yaw and longitude on the wrapped difference. "Zero-accepting" is
ADR-0015's `accepts_zero`: the band is at least the reference's magnitude at every gated sample, so a trajectory
of zeros would pass.

| Case | Selected bands (SI) | Zero-accepting channels |
|---|---|---|
| 11 | altitude 0.140 m; v_north 0.015, v_east 0.024 m/s; pitch 1.8e-5 rad; rate_q 3.2e-7 rad/s; force_x 1.84 N, force_z 0.48 N | v_down, force_y, moment_l, moment_m, moment_n |
| 12 | altitude 0.93 m; v_north 0.057 m/s; pitch 5.4e-5 rad; force_x 6.9 N, force_z 4.7 N | v_down, moment_l, moment_m, moment_n |
| 13.1 | altitude 0.23 m; v_down 0.021 m/s; pitch 2.2e-4 rad; rate_q 2.5e-4 rad/s; force_x 482 N, force_z 2,411 N | force_y, moment_l, moment_m, moment_n |
| 13.2 | altitude 0.15 m; v_north 0.073 m/s; pitch 1.0e-4 rad; force_x 27.9 N, force_z 73 N | moment_l, moment_m, moment_n |
| 13.3 | altitude 0.58 m; yaw 0.041 rad; roll 0.042 rad; rate_p 1.25, rate_q 0.122 rad/s; force_y 2.1e4 N, force_z 6.26e4 N; moment_l 1.3e5 N·m | moment_m |
| 13.4 | altitude 0.60 m; yaw 0.045 rad; rate_q 0.139 rad/s; force_z 6.24e4 N; moment_l 2.1e5 N·m | moment_m |
| 15 | altitude 0.46 m; longitude 3.3e-3 rad; roll 0.020 rad; rate_q 3.3e-3 rad/s; force_z 1,361 N | rate_p, force_y, moment_l, moment_m, moment_n |
| 16 | altitude 0.45 m; latitude 1.5e-6 rad; roll 0.027 rad; rate_q 4.3e-3 rad/s; force_z 1,741 N | rate_p, force_y, moment_l, moment_m, moment_n |

What the preview shows:
- **The trim cases discriminate sharply.** Case 11's force_z band is 0.48 N against a lift of about 9.1e4 N.
- **The maneuver cases' force and moment bands are loose.** In 13.3 force_z's band is 6.26e4 N, 69 % of
  Table 7's weight (9.12e4 N), set by footnote (e)'s transient timing at the command step. The kinematic
  channels still discriminate.
- **The window matters.**
  - In case 15 the full window raises force_z's band to 3.69e4 N, because SIM 4 starts untrimmed.
  - In 13.1 a window from t ≥ 2 s takes in the 5 s step's transient, and force_z's band becomes 1.36e5 N.

## Decision

1. **Members.** The band for cases 11 to 16 is computed from SIM 4 and SIM 5 only, as Table 79 and 80's
   footnote (b) does.
   - SIM 2 is excluded from each of those cases by one participant rule, for every channel (ADR-0015's
     shape).
   - SIM 2's distance from the band is reported, never gated: the max over gated samples of
     |sim2 − reference| / `tol_abs`, for each channel it publishes.
2. **Footnotes govern exclusion.**
   - Footnote (a) sets the window (item 5), and footnote (b) excludes SIM 2.
   - Footnotes (c) to (f) explain transients and exclude nothing. Their disagreement shows as band width.
   - An unfootnoted "0.000 – data provided but not included in calculation" cell is not an exclusion,
     following the Table 78 precedent that cases 1 to 10 already apply.
3. **Durations.** Each case runs for its Vol II duration: 180 s for 11, 12, 15 and 16, 20 s for 13.1 and 13.2,
   30 s for 13.3, and 60 s for 13.4. That is 1801, 201, 301 and 601 samples.
   - A member's rows beyond the duration are ignored.
   - A member missing any grid instant within it is a generator error.
4. **Matching and alignment.**
   - A row matches instant k/10 when its time lies within 1e-5 s of it. That is the float32 half-ulp bound
     below 256 s (7.6e-6 s), and it is well inside half the 0.1 s spacing.
   - Sample i is frame 100i, compared by frame index, never by equality of doubles.
   - The generator checks that `sim_05`'s duplicate `feVelocity_ft_s_Z` columns agree in every row it reads,
     and fails if they do not.
5. **The gated window.** The window is t ≥ 8 s in every case, so the first gated sample is 80.
   - Both `tol_abs`'s max and the pass test run over gated samples only.
   - The 0 to 8 s samples are still compared, and their distances are logged by the harness, never asserted.
   - Each case's header carries its sample count and first gated sample.
6. **The band** is ADR-0013's scalar per channel and case.
   - The reference at each sample is the members' mean, their midpoint.
   - `tol_abs = max(3 · max_gated |sim4 − sim5|, floor)`, with ADR-0014's floors.
   - The pass test is ADR-0015's absolute-only one, `|astra − reference| ≤ tol_abs`. k = 3 is unchanged.
   - Yaw and longitude take the circular mean and the shortest signed difference that the generator already
     uses for yaw. Cases 15 and 16 cross ±180° of longitude.
7. **The aero channels.** Each `aero_bodyForce_*` and `aero_bodyMoment_*` channel is about the CM and aero
   only. It is compared with ASTRA's aero-only, frame-start-stage wrench about the CG, obtained by the M7
   plan's Q5 route, never with `v0.wrench_total`, which includes thrust. This item points to that route; it
   does not record it.
8. **Reports.** Every regeneration reports two things per case and channel: `tol_abs` as a ratio to its
   floor, and whether the channel is zero-accepting (ADR-0015's test over gated samples). It also reports item
   1's SIM 2 ratio.
   - A zero-accepting channel is never cited as evidence of correctness.
   - This record holds the rule and the measured preview above. The generator's report, in PR 8's PR body,
     is the record of the generated values.

## Rejected alternatives

- **All three members.** This takes in SIM 2's documented gravity and trim differences, and makes every band
  wider than NASA's own comparison. In the preview's case 11, SIM 2 lies 12 to 260 times `tol_abs` from the
  reference on every position, velocity, attitude, rate and force channel except rate_p (1.9 times).
- **A per-channel SIM 2 list.** ADR-0015 rejected this shape: the references within one case would come from
  different ensembles.
- **Excluding the unfootnoted cells.** This departs from the Table 78 precedent that cases 1 to 10 apply.
  Excluding SIM 5's Euler angles would leave one member and no band, and excluding both members' latitude and
  temperature would leave no member at all.
- **A window from t ≥ 2 s** (the end of Vol II D.1.11's first-second oscillation). It gates 13.1's and 13.2's
  onset, but widens 13.1's force_z band from 2,411 N to 1.36e5 N, above the aircraft's weight.
- **A per-sample band** `tol(t) = max(k · spread(t), floor_c)` for the maneuver cases. It needs a new judgment
  number, the floor, and it is tightest exactly where the members happen to cross.
- **Interpolating onto the grid.** Every member already samples at 10 Hz within 1e-5 s, so interpolation would
  add a judgment that changes the band for no gain.

## Consequences and validation

- **Below the ground rule.** The oracle has two members, below Vol I's three-data-set rule. The band is k times
  one pairwise difference, so a channel where SIM 4 and SIM 5 happen to agree is tighter than three tools
  might give. This is disclosed, not hidden.
- **The maneuver cases.** Their force and moment bands test little, while their kinematic channels
  discriminate. 13.1's and 13.2's command onset at t = 5 s is ungated. The M7 plan's R22 fixtures check it,
  not this band.
- **The generator and the header** (PR 8, run by Joe under `touch-goldens`, never by an agent or the gate):
  - per-case durations, the window, the 1e-5 s match, the longitude wrap, the duplicate-column check and
    SIM 2's rule;
  - `nesc_band_common.hpp` gains a per-case sample count and first gated sample;
  - `case01` to `case10` regenerate byte-identical.
- **Negative controls.** The M7 plan's R9 planted defects must each fail at least one discriminating band of
  the family they touch.
- **Acceptance.** PR 8's regeneration with `case01` to `case10` byte-identical, and
  `ctest --preset release -R Nesc --no-tests=error`. PRs 9, 11a and 11b gate each case at every gated sample.

## Approval

Accepted by Joe, 2026-10-05.
