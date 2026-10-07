# M7 PR 8: the generator extension and the F-16 band headers

Spec: [M7 plan](m7-f16-nesc-oracle.md) (approved by Joe, 2026-10-04, at `3381693`): R8, the contract's
"oracle" block, and the PR table's row 8. Binding: ADR-0013 to ADR-0015, and
[ADR-0028](../decisions/0028-f16-nesc-oracle.md) (accepted 2026-10-05, #105).
Risk: Tier 3 (the oracle: `tests/golden/nesc/**`). Depends on PR 1a only; needs neither P1 nor P2.
Status: **approved by Joe, 2026-10-07, in plan mode,** with D1 to D3 accepted (D3 widened by his amendment
4) and his amendments 1 to 5, folded in below (marked A1 to A5). The amendments were folded in before any
guarded edit. Implemented in #106; Joe ran the generator at `76f7ee9`. The results are in the PR body.
Base: `origin/main` `37e8f4e` (#105), re-checked after `git fetch`. Worktree `m7-pr8`, branch
`worktree-m7-pr8`. Joe arms `.claude/touch-goldens`; Claude never arms it, and never runs the generator.

## Context

ADR-0028 defines the F-16 oracle: SIM 4 and SIM 5 only, SIM 2 reported and never gated, Vol II durations,
1e-5 s matching, a gated window of t ≥ 8 s (first gated sample 80), longitude wrapped like yaw, the
duplicate-column check, and a regeneration report of floor ratios, zero-accepting status and SIM 2's ratio.
PR 8 implements it in the generator and its header schema, and adds the eight band headers PR 9, 11a and
11b fly. Cases 1 to 10 must regenerate byte-identical, and `nesc_cases_test.cpp` must not change.

## Repository findings (read at `37e8f4e`)

- `generate_nesc_band.py`: `SAMPLE_COUNT = 301` (`:101`), `NEAREST_SAMPLE_TOLERANCE_S = 1e-6` (`:103`); the
  wrap is `Channel.is_yaw` only (`:140`, `:332`); `excluded()` (`:278`) holds Tables 77/78's rules by case
  number; the file glob is `Atmos_{num:02d}_sim_*.csv` (`:360`), which cannot name `Atmos_13p1_*`;
  `nearest_index` is a linear scan called per channel per sample (`:323`); `report()` prints `tol_abs` and the
  floor ratio only, and `check_non_discriminating` (`:386`) prints only a tautologically impossible case.
  Output is clang-formatted afterwards (docstring `:17`); `just format-check` covers `tests/golden/**`.
- `nesc_band_common.hpp`: `kSampleCount = 301`; `ChannelBand { double reference[kSampleCount]; double tol_abs; }`;
  `CaseBand` holds 21 `const ChannelBand&`. Every M2 header names only `ChannelBand` and `CaseBand`
  (`inline constexpr CaseBand kBand = {.latitude = kLatitude, ...}`); `nesc_cases_test.cpp` uses
  `nesc::kSampleCount` and `const nesc::CaseBand&` (`:132`, `:157`, `:235`).
- The CSVs (read-only survey, main checkout's `docs/refs`): row counts match ADR-0028's table (`sim_02`
  2001 rows in 11/12; `sim_05` 601 in 13.1/13.2 and 2400 to 239.9 s in 13.3/13.4); every file's times are
  strictly increasing and parse as floats; only `sim_05` duplicates a column (`feVelocity_ft_s_Z`). Every
  `CHANNELS` column is present in `sim_02`, `sim_04` and `sim_05`, so SIM 2's ratio covers all 21 channels.
- `astra_physics_tests` builds in all four gate presets (debug, release, asan, static-plugins);
  `just tidy` reads `build/asan`'s compile database with `HeaderFilterRegex` covering `tests/**.hpp`, so a
  TU there tidies every header it includes. ctest names carry `physics.`; `-R Nesc` selects `Nesc*` suites.
- The guard (`guard-oracle.sh:33`) covers `tests/golden/` and `tests/**tolerance*`; the new test's name
  avoids "tolerance".

## Q1. Per-case sample count and first gated sample, with M2 untouched

`nesc_band_common.hpp` (hand-edited with Edit under `touch-goldens`) becomes templates plus aliases that
keep the M2 names meaning exactly what they mean today:

```cpp
inline constexpr int kSampleCount = 301;  // cases 1 to 10 (ADR-0013), unchanged

template <int SampleCount>
struct ChannelBandOf { double reference[SampleCount]; double tol_abs; };  // same NOLINT as today

template <int SampleCount, int FirstGatedSample>
struct CaseBandOf {
  static_assert(0 <= FirstGatedSample && FirstGatedSample < SampleCount);
  static constexpr int kSampleCount = SampleCount;
  static constexpr int kFirstGatedSample = FirstGatedSample;
  using Channel = ChannelBandOf<SampleCount>;
  const Channel& latitude; ... (the same 21 members, same order)
};

using ChannelBand = ChannelBandOf<kSampleCount>;
using CaseBand = CaseBandOf<kSampleCount, 0>;  // ADR-0013: every sample gated
```

- The M2 headers compile unchanged: `ChannelBand`'s aggregate init and `CaseBand`'s designated init are the
  same, and static members do not enter aggregate initialization. `nesc_cases_test.cpp` compiles and runs
  unchanged (`nesc::kSampleCount`, `nesc::CaseBand`). I check this before Joe's run:
  `ctest --preset release -R Nesc --no-tests=error` and an asan build, with the common header edited and the
  M2 headers untouched.
- Each F-16 header carries its count and window in one place, its band type:
  `using Band = CaseBandOf<1801, 80>;`, `inline constexpr Band::Channel kLatitude = {...};`,
  `inline constexpr Band kBand = {...};`, with a comment citing ADR-0028 items 3 and 5.
- Byte identity: the generator's M2 path renders through today's `render_case_header`, untouched. Every new
  behavior is a per-case parameter whose M2 value reproduces today's computation exactly: sample count 301,
  first gated 0 (the max over `spreads[0:]`), 1e-6 s matching, wrapped set `{yaw}`, Tables 77/78's rules.
  Longitude's wrap applies only to the F-16 cases, because a wrapped mean is a different floating-point
  expression and would move M2 bytes.

### The generator changes (`tests/golden/nesc/generate_nesc_band.py`, Edit only)

- A per-case record: file tag (`01`, `13p1`), label (`13.1`), slug, sample count, first gated sample,
  matching tolerance, wrapped channels, and the rule family (Tables 77/78 for 1 to 10; ADR-0028 for 11 to 16).
  The eight F-16 entries cite Vol II Tables 35 to 43 and ADR-0028 items 3 to 6: 1801 (11, 12, 15, 16),
  201 (13.1, 13.2), 301 (13.3), 601 (13.4); first gated 80; 1e-5 s; `{yaw, longitude}`. A self-check
  raises unless each count equals 10 × duration + 1.
- Members: an F-16 case's folder must hold exactly `sim_02`, `sim_04`, `sim_05` (else `SystemExit`).
  `excluded()` gains one F-16 rule, SIM 2 for every channel (item 1), evaluated before and instead of the
  M2 rules; a check raises unless every F-16 sample's contributors are exactly {4, 5}.
- Matching: rows beyond the duration are never targeted; a missing grid instant raises (existing
  `nearest_index`, with the case's tolerance). Each participant's row per sample is computed once per case
  and reused across channels (same function, same row, so M2 bytes do not move; without it the F-16 cases
  cost about 1e8 Python comparisons each). The report gives each member's largest matching offset.
- The duplicate-column check stays in `load_participant` (every row read) and is reported per file.
- `tol_abs = max(K · max(spreads[first_gated:]), floor)`; the reference is still emitted for every sample.
- Report (stderr), per case: members, sample count, first gated sample, largest offset per member,
  duplicate columns checked. Per channel: `tol_abs`, `floor_ratio`, `zero_accepting` (ADR-0015's test over
  gated samples: every gated |reference| ≤ `tol_abs`), and for 11 to 16 `sim2_ratio` (the max over gated
  samples of |sim2 − reference| / `tol_abs`, wrapped for yaw and longitude). M2 lines gain
  `zero_accepting` too, since item 8 says every regeneration reports it. Headers are unaffected.
- (A1) `check_non_discriminating()` stays, unchanged in its test and message: ADR-0015 keeps it as a permanent
  structural-invariant check. The `zero_accepting` report is added beside it, not in its place. It runs
  on the gated samples, which for cases 1 to 10 are all of them.
- The F-16 renderer is a new function: header comment citing ADR-0028 (members, window, matching, wrap,
  SIM 2 reported), namespace `astra::test::nesc::case11` / `case13p1`, file
  `case11_trim_check_subsonic_f16_band.hpp`, `case12_trim_check_supersonic_f16_band.hpp`,
  `case13p1_subsonic_altitude_change_f16_band.hpp`, `case13p2_subsonic_airspeed_change_f16_band.hpp`,
  `case13p3_subsonic_heading_change_f16_band.hpp`, `case13p4_subsonic_lateral_side_step_f16_band.hpp`,
  `case15_circle_north_pole_band.hpp`, `case16_circle_equator_date_line_band.hpp`.
- The module docstring gains ADR-0028's rules; nothing in it states the preview's numbers.
- Before Joe's run I check the file without executing it: `python3 -m py_compile` and `just ruff`
  (`ruff format --diff`, never an in-place format of a guarded file).

## Q2. How the headers are exercised before PR 9

A new unguarded TU, `tests/physics/nesc/nesc_f16_bands_test.cpp`, added to `astra_physics_tests` (so it
builds in every preset and is tidied in asan's database), after Joe's run:
- `static_assert`s: each F-16 `kBand`'s `kSampleCount` equals ADR-0028 item 3's count and
  `kFirstGatedSample` equals 80 (item 5), restated in the test from the ADR, not read from the generator;
  `nesc::CaseBand` is `<301, 0>`.
- `NescF16Band.SampleCountsAndWindow`: the same values at run time, so ctest reports them.
- `NescF16Band.FloorsHold`: for every case and channel, `tol_abs` is finite and at least its kind's floor,
  restated from ADR-0014's table and ADR-0015's atmosphere conversion.
- `NescF16Band.ReferencesFinite`: every reference sample finite; yaw and longitude within [−π, π]; every angle
  band at most π (the generator's own sanity gate, checked independently).
- One function template over `CaseBandOf<N, G>` does the per-channel checks.
- (A3) The sweep cannot pass empty (lesson 2026-09-29): each test counts what it visited and asserts 8 cases
  and 21 channels per case.
- (A2) `NescF16Band.InitialConditionsMatchVolII`: an independent content check, described below.

### (A2) The sample-0 reference against Vol II's initial conditions

For each F-16 case, the sample-0 reference of latitude, longitude, altitude, v_north, v_east, v_down, roll and
yaw must equal Vol II's initial condition. The values are transcribed into the test in their published units
(deg, deg, ft MSL; ft/s; deg), each with its table cited beside it, and converted in the test with
`astra::units::degrees_to_radians` and `feet_to_meters` (`core/include/astra/util/units.hpp`). Pitch is
trimmed, so it is not checked. Velocity is the table's "local-relative" [north, east, down] and attitude its
[roll, pitch, yaw] (case 15's [0, 563.643, 0] with yaw 90 and case 16's [563.643, 0, 0] with yaw 0 fix the
order). Yaw and longitude compare on the wrapped difference.

**What it can and cannot catch (Joe, 2026-10-07).** It catches a wrong unit, column, sign or file in a
generated header. It cannot tell case 11 from 13.1 to 13.4, which share their initial conditions, and it says
nothing about the band's width or any sample after t = 0.

**The rule (Joe's decision, 2026-10-07, for every value in every case):** tol = max(half a unit in the last
digit Vol II prints, 1e-6 × |printed value|), in the value's published unit, converted to SI with the same
function. A printed integer, including 0, has a half unit of 0.5. It replaced the first rule (half a unit
alone) because that rule tested the precision of NASA's printed table rather than the check's purpose. It is
not a case 12 exception.

**Finding under the first rule (before the run, from the members' t = 0 rows, read-only).** Case 12's
v_north and v_east failed it: the members' mean is 1414.2135610 ft/s against the printed 1414.213562, a miss
of 1.18e-6 with a tolerance of 5e-7 (SIM 4 1414.21356237, SIM 5 1414.2135593). Joe changed the rule for every
value, for the reason above.

**Data, not a failure.** SIM 4 starts cases 11 and 13.1 to 13.4 at 400.010306 ft/s in v_north and v_east,
against the printed 400; SIM 5 starts at 400 (399.99999999).

Both rules, with the predicted margin (|members' mean − printed| / tolerance) from the CSVs' t = 0 rows. The PR
body reports each value's margin from the generated headers.

| Value as printed (tables) | First rule | Its margin | New rule | Its margin |
|---|---|---|---|---|
| latitude 36.01916667 deg (35 to 40) | 5e-9 | 0.63 | 3.602e-5 | 8.75e-5 |
| longitude −75.67444444 deg (35 to 40) | 5e-9 | 0.88 | 7.567e-5 | 5.81e-5 |
| altitude 10013, 30013 ft (35 to 40) | 0.5 | ≤ 4.7e-9 | 0.5 | ≤ 4.7e-9 |
| v_north, v_east 400 ft/s (35, 37 to 40) | 0.5 | 0.0103 | 0.5 | 0.0103 |
| v_north, v_east 1414.213562 ft/s (36) | 5e-7 | **2.35 (miss)** | 1.414e-3 | 8.31e-4 |
| v_down 0 ft/s (all) | 0.5 | ≤ 7.2e-9 | 0.5 | ≤ 7.2e-9 |
| roll 0 deg; yaw 45 deg (35 to 40) | 0.5 | ≤ 3.3e-13 | 0.5 | ≤ 3.3e-13 |
| latitude 89.95 deg (42) | 5e-3 | 1.39e-10 | 5e-3 | 1.39e-10 |
| longitude −45 deg (42); latitude 0 deg (43) | 0.5 | 0 | 0.5 | 0 |
| longitude −179.95 deg (43) | 5e-3 | 0 | 5e-3 | 0 |
| altitude 10000 ft (42, 43) | 0.5 | ≤ 5.8e-11 | 0.5 | ≤ 5.8e-11 |
| v_east 563.643 ft/s (42); v_north 563.643 ft/s (43) | 5e-4 | 0.703 | 5.636e-4 | 0.624 |
| v_north 0 (42); v_east 0 (43) ft/s | 0.5 | ≤ 6.6e-12 | 0.5 | ≤ 6.6e-12 |
| roll 0 deg; yaw 90, 0 deg (42, 43) | 0.5 | ≤ 1.8e-11 | 0.5 | ≤ 1.8e-11 |

Under the new rule every value passes; the closest is cases 15 and 16's 563.643 ft/s at 0.624. A miss is a
finding for Joe; no tolerance is loosened to clear it.

Acceptance: `ctest --preset release -R Nesc --no-tests=error` (M2's ten cases unchanged plus these), and the
full gate. The guard-free test means no tolerance or golden is touched by it.

## Q3. Header sizes and build cost (estimates; measured after Joe's run)

Bytes per value today: case01 107,651 B / (301 × 21) = 17.0 B; the M2 range is 17.0 to 23.2 B (case03).
F-16 values are nonzero full-precision doubles on nearly every channel, so the upper rate is the likelier one.

| Header | Samples | Values (× 21) | At 17.0 B | At 23.2 B |
|---|---|---|---|---|
| 11, 12, 15, 16 (each) | 1801 | 37,821 | 644 KB | 876 KB |
| 13.1, 13.2 (each) | 201 | 4,221 | 72 KB | 98 KB |
| 13.3 | 301 | 6,321 | 108 KB | 146 KB |
| 13.4 | 601 | 12,621 | 215 KB | 292 KB |
| **All eight** | | 178,668 | **3.04 MB** | **4.14 MB** |

The ten M2 headers total 1.31 MB. Build cost, from measurements this session (32 cores, idle):
- Compile: a TU including only the ten M2 headers compiles in 0.08 s at `-g`, `-O3` and ASan alike, so the
  F-16 data costs about 0.2 to 0.3 s per preset. The new TU (GoogleTest plus the eight headers) is
  estimated at 1 to 2 s per preset (today's `nesc_cases_test.cpp`, with Eigen and the core, takes 1.6 to
  1.9 s under ASan): about 4 to 8 s of CPU over the four presets, near zero wall time in a parallel build.
- clang-tidy: the ten M2 headers alone take 2.4 s, so the F-16 headers add about 6 to 8 s, plus the TU's
  GoogleTest overhead: about 10 to 20 s of CPU in one parallel tidy job (`nesc_cases_test.cpp` takes 25 s).
- Binary: about 1.4 MB of `.rodata` per test binary (178,668 doubles), in four presets.
After the run I measure: `wc -c` of each header; the TU's compile per preset (its `ninja -t commands`
line, three runs to `/dev/null`, median), headers-only compiles, and `clang-tidy-18 -p build/asan` on the TU.
Both go in the PR body against these estimates.

## Joe's run (after approval, the guarded edits and the pre-run check)

From the worktree root (`--refs` defaults to the main checkout's `docs/refs/Atmospheric_checkcases`):

```
git status --short tests/golden/nesc/          # (A5) expected: nothing
git rev-parse HEAD                             # (A5) the generator revision
python3 tests/golden/nesc/generate_nesc_band.py 2> /tmp/nesc-band-report.txt; echo "exit $?"
clang-format-18 -i tests/golden/nesc/case*_band.hpp
git diff --exit-code -- tests/golden/nesc/case0[1-9]_*_band.hpp tests/golden/nesc/case10_*_band.hpp \
  && echo "case01-case10 byte-identical"
git status --short tests/golden/nesc/
```

The report must show: exit 0; for 11 to 16, members sim 4 and sim 5, SIM 2 excluded by rule, the sample
counts and first gated sample 80, `sim_05`'s duplicate column checked, and largest offsets within 1e-5 s
(ADR-0028: 6.1e-6 s in 11, 12, 15, 16; 7.63e-7 s in 13.1 to 13.3; 1.53e-6 s in 13.4; 0 for SIM 4); every
channel's `tol_abs`, `floor_ratio`, `zero_accepting` and `sim2_ratio`; the M2 lines; the `git diff` prints
nothing and the echo appears; `git status` lists only the eight new `??` headers (the generator and common-header edits are already
committed). Joe pastes the output and `/tmp/nesc-band-report.txt`; the report stays out of git.

(A5) Provenance: the PR body names the `git rev-parse HEAD` commit as the generator revision the eight
headers came from, and records that `tests/golden/nesc/` was clean before the run.

## The PR body record (ADR-0028 item 8, R8)

One table per case: channel, `tol_abs` (SI), floor ratio, zero-accepting, SIM 2 ratio, and ADR-0028's
preview value where it states one.

(A4) The comparison covers every number ADR-0028 states that the report reproduces:
- the preview's selected bands (Measured preview table), each generated `tol_abs` rounded to the preview's
  printed digits;
- the preview's zero-accepting lists, as sets per case;
- the matching offsets: SIM 4's largest offset 0, and SIM 5's 6.1e-6 s (11, 12, 15, 16), 7.63e-7 s (13.1 to
  13.3) and 1.53e-6 s (13.4), each rounded to the printed digits;
- the sample counts 1801, 201, 301, 601 and the first gated sample 80;
- case 11's SIM 2 ratios: every position, velocity, attitude, rate and force channel (latitude, longitude,
  altitude, v_north, v_east, v_down, yaw, pitch, roll, rate_p, rate_q, rate_r, force_x, force_y, force_z)
  between 12 and 260 at the printed two digits (that is, in [11.5, 265)), except rate_p at 1.9.

A mismatch is any of these that differs. Each is reported to Joe as found. Nothing is adjusted to remove it.

## Validation (Tier 3)

- Before the run: `ctest --preset release -R Nesc --no-tests=error` and `./scripts/check-fast.sh` with the
  common header edited.
- After: the byte-identity check above (Joe's), `ctest --preset release -R Nesc --no-tests=error`, the
  measurements, `./scripts/check-full.sh` (exit 0 and `check: OK`, under `run_in_background`, waited on by
  pid), `python3 scripts/docs_check.py`, `git diff --check origin/main..HEAD`; trimmed gate summary at
  `docs/reviews/evidence/<head>-gate-summary.log`.
- Review: `/review-cycle`, one adversarial Codex review (fresh, read-only, committed-branch scope
  `origin/main..HEAD`) of the generator and the header diff against ADR-0013 to ADR-0015 and ADR-0028, with
  `/home/joe/repos/ASTRA/docs/refs` as read-only input so it can recompute members, offsets and bands. At most
  two correction rounds. A finding that would change generated values returns to Joe, who reruns.
- `/human-review` writes the PR body (results, Codex verdict and thread id, the report and comparison,
  sizes and build cost, any guard refusal); `/wrap` brings PLAN.md's PR 8 line and STATE.md current.

## Order of work

1. `git fetch`; worktree `m7-pr8` from `origin/main`; move this draft in as `docs/plans/m7-pr8-nesc-bands.md`;
   commit, push, open the draft PR.
2. Joe arms `touch-goldens`. Edit `nesc_band_common.hpp` and the generator; pre-run checks; commit and push.
3. Hand Joe the run commands; he runs and pastes the report.
4. Commit the eight headers (as Joe's run left them); add the test and its CMake line; measure; gate;
   Codex review; PR body; `/wrap`.

## Non-goals

No harness for flying the F-16 cases (PR 9, 11a, 11b), no change to `nesc_cases_test.cpp`, no per-channel
zero-accepting flag in the headers (R8's record is the PR body), no pytest that imports the generator (it is
never run by an agent or the gate, ADR-0013), no change to M2's exclusions, floors or k.

## Joe's decisions (2026-10-07)

- D1 accepted: the template-plus-alias schema above, rather than a separate F-16-only struct pair.
- D2 accepted: `zero_accepting` printed for cases 1 to 10 as well (headers unchanged).
- D3 accepted, widened by A4: the comparison above.
- A1 to A5 folded in above.
- A2's tolerance rule replaced (Joe, 2026-10-07), for every value in every case, by max(half a unit in the
  last printed digit, 1e-6 × |printed value|), after case 12's v_north and v_east missed the first rule (see
  A2). Both rules and their margins are kept above; the PR body records the finding and each value's margin.
- `touch-goldens` armed by Joe, 2026-10-07.
Rollback: revert the PR; the M2 headers are unchanged by construction.
