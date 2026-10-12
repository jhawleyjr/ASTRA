# M7 PR 6c: the F-16's mass and propulsion from P1 (Tier 3)

Status: **approved by Joe, 2026-10-11, in plan mode**, with his amendments 1 and 2 folded in (below).
Owner: Joe. Builder: Claude, one builder. Tier: **3** (`aircraft/**/physical`, `aircraft/**/hardware`).
Base: `origin/main` = `c87f202` (#114). It was read with `git ls-remote origin main` in plan mode and confirmed by
`git fetch` after approval. The worktree `m7-pr6c` (branch `worktree-m7-pr6c`) is at `c87f202`.
Name: the plan-mode draft was moved (`mv`, before its first add) to `docs/plans/m7-pr6c-f16-mass-propulsion.md`.
Binding: ADR-0001, 0002, 0016, 0017, 0020, 0021, 0030, 0032, 0033 (all accepted; 0033 on 2026-10-11, which
meets the M7 plan's precondition for this PR's mass commit, `m7-f16-nesc-oracle.md:878`).

**The design is PR 6b's approved plan**, `docs/plans/m7-pr6b-f16-aircraft.md`:
- its Mass and Propulsion sections;
- decisions 1 (the engine reads `altitude_m`), 2 (the slug and pound-force units) and 4 (PWR is not saturated);
- the Per-frame rules and NoAlloc section;
- its commits 5 and 6.

This plan does not redesign any of it. It records what changed (Deviations), what reading the code turned up, and
the sequence. Sources: the M7 plan's 2026-10-10 amendment (row 6c, `m7-f16-nesc-oracle.md:825,850`) and Joe's Q1
decision (`m7-pr3c-altitude-and-units.md`, "Decisions for Joe"). No Codex plan critique: the design is pinned (Joe).

## Joe's amendments at approval (2026-10-11)

Joe recomputed the plan's numbers independently. He used exact rational arithmetic for the five mass constants,
and Python doubles over the committed `engine_tables.hpp` for the three margins and the five map points. All of
them stand:
- the margins, 5.67e-4, 3.10e-5 and 1e-5 lbf;
- the map points, 1060, 12680, 20000, −1264 and 22928 lbf;
- every SI constant, equal to its correctly rounded value.

1. **Deviation 3 stands.** `F16Mass.DescriptorAndSignal` and `F16Engine.DescriptorAndWriteSeamName` are kept.
   They check sentences 6b's plan pins, and nothing else does: the descriptor names `f16_mass` and `f16_engine`,
   `name()` `mass` and `engine`, `mass.mass` in kg, `controls.throttle` as "1", and the atmosphere requirement.
   The predicted count stays 30 (24, 2 and 4).
2. **The infinite-Mach rule is a deviation, not only a finding** (Deviation 4 below), and it is tested: the third
   sample of `AnUnsetAltitudeGivesANonFiniteWrench`.
3. Everything else stands as written: the commit order, the blast radius, the checks and the `/wrap` text.

## Context

The F-16 aero is on main (#113). The mass and the engine are still the template's verbatim copies:
- `physical/mass/mass.cpp` reads config keys;
- `hardware/propulsion/turbofan/` is a linear thrust lapse;
- `tests/mass_test.cpp` and `turbofan_test.cpp` are the template's "zeroed config throws" tests.

PR 3c (#114) supplied `EnvironmentSample::altitude_m` and the slug and pound-force units, so 6b's commits 5 and
6 can now land. Nothing flies the F-16 yet: there are no NESC configs (PR 9) and no evaluator (PR 7).

## Deviations (recorded before any code)

1. **#113 merged PR 6b after its commit 4**, so 6b's commits 5 (mass) and 6 (propulsion) moved to this PR (Joe's
   Q1 decision, 2026-10-10). 6b's plan on main is not edited. Its "merge `origin/main` once PR 3c has merged" step
   is satisfied by this branch's base, which already contains #114.
2. **One addition to 6b's mass tests (Joe's brief, item 2):** `slugs_to_kilograms(637.1595) ==
   9298.643898518938`, bitwise.
3. **Two test additions, kept by Joe (amendment 1).** `F16Mass.DescriptorAndSignal` and
   `F16Engine.DescriptorAndWriteSeamName` mirror `F16Aero.DescriptorAndWriteSeamNames` (`aero_test.cpp:212`).
   Without them, nothing checks 6b's descriptor names (`f16_mass`, `f16_engine`), `name()` (`mass`, `engine`),
   the signal `mass.mass` (kg), or `controls.throttle` ("1").
4. **A zero speed of sound gives the NaN sentinel (Joe's amendment 2).** 6b's Propulsion section says "a NaN
   altitude or speed of sound gives the NaN sentinel". This extends it to a speed of sound of zero:
   - with a nonzero airspeed, Mach is infinite;
   - at zero airspeed, it is 0/0.
   Either way the engine returns the NaN sentinel. The alternative, an infinite Mach reaching the lookup, clips
   silently to Mach 1. The engine's finiteness check runs on Mach before any lookup.
   `AnUnsetAltitudeGivesANonFiniteWrench`'s third sample tests it: `altitude_m` set, `speed_of_sound_m_s = 0` and
   `v_b` nonzero, with all six components NaN. A port that clips Mach before the finiteness check fails it.

## Repository findings (read at `c87f202`)

- **The zero products' sign.** The copied `mass.cpp:72-74` negates with `-p`. With Ixy = Iyz =
  `slug_square_feet_to_kilogram_square_meters(0.0)` = +0.0, that gives −0.0 off the diagonal. 6b's plan pins
  "the xy and yz entries are +0.0".
  - So the F-16 mass negates with `0.0 - p`, the form `plugins/physical/mass/constant/constant.cpp:18-25,74-76`
    adopted in M7 PR 4 (D1).
  - The template and the notional transport keep `-p` for their frozen bits. The F-16 has no frozen bits: nothing
    flies it yet.
  - xz is `0.0 - slug_square_feet_to_kilogram_square_meters(982)`, the same bits as `-(…)`.
- **The atmosphere requirement.** `PropulsionModel::requires_environment()` already returns
  `{.wind = false, .atmosphere = true}` (`propulsion_model.hpp:16-18`, non-final, ADR-0020's safe default).
  - The engine keeps it, as `F16Aero` keeps `AeroModel`'s; no plugin in the repository overrides it.
  - The plugin's header comment says it relies on the default.
  - `F16Engine.DescriptorAndWriteSeamName` asserts `.atmosphere` is true.
- **The tables' axes.** `F16_prop.dml`'s three `griddedTableDef`s list `MACH_PTS` then `ALT_PTS` (`:267-268`,
  `:291-292`, `:315-316`), so `engine_tables.hpp`'s `kTIdle[mach][alt]` reads as `interp::lookup(kMachPts,
  kAltPts, table, mach, altitude)`. Every `independentVarRef` is `extrapolate="neither"` with min/max at the
  breakpoint ends, so every lookup clips.
- **The build-up, as the DML writes it** (`F16_prop.dml:107-177`):
  - PWR < MIL_PWR (strict `<lt/>`): `FEX = T_IDLE + PWR·(T_MIL − T_IDLE)/MIL_PWR`;
  - otherwise: `FEX = T_MIL + (PWR − MIL_PWR)·(T_MAX − T_MIL)/(100.0 − MIL_PWR)`.
  - PWR = 50 takes the second branch. The port keeps this operation order, with `100.0` as a named constant
    (`kMaxPwr`, the `<cn>` at `:165`).
- **The checkData margins, computed in plan mode** (Python doubles, both routes, nothing written):
  - The SI route reproduces the DML's own internal FEX to ≤ 1e-12 lbf on shots 6 to 8. It converts the tables to
    N and the breakpoints to m, then converts the thrust back with `newtons_to_pound_force`.
  - The shots' published FEX outputs are rounded, so the margins are:
    | Shot | FEX output | Tolerance | Margin |
    |---|---|---|---|
    | 7 (`:784`) | 5319.3491 | 1e-3 | 5.67e-4 lbf |
    | 8 (`:856`) | 9298.8926 | 6e-4 | 3.1e-5 lbf |
    | 6 | exactly 5057 | 1e-5 | 1e-5 lbf |
  - Shot 8 passes on the port matching the DML's arithmetic, not on luck. A port that misses by more than 3e-5
    lbf fails it.
- **The R3 map points**, also computed in plan mode, at (Mach 0, 0 ft):
  - throttle 0, 0.5 and 1 give `pound_force_to_newtons` of 1060, 12680 and 20000 bit for bit;
  - throttle −0.1 gives −1264 lbf to 1.8e-16 relative, and 1.2 gives 22928 lbf exactly.
- **The mass constants.** Computed in plan mode in exact rational arithmetic. The units route equals the
  correctly rounded value for every constant:
  | Constant | Value (kg or kg·m²) |
  |---|---|
  | mass | 9298.6438985189388 |
  | Ixx | 12874.847237354978 |
  | Iyy | 75673.622968168782 |
  | Izz | 85552.112539711365 |
  | Izx | 1331.4132252614352 |

  D1's report line will read close to: 637.26 slug = 9300.110585764129 kg, which is 1.466687245 kg (14.383 N at
  9.80665 m/s²) above the DML's mass.
- **Nothing outside `aircraft/f16` names these plugins.** `git grep` finds no reference to
  `aircraft/f16/{physical/mass,hardware}` or the F-16's turbofan outside the tree. The F-16's plots use
  `controls.throttle`, which the engine binds. `test_new_aircraft*.py` copies the template, and its build case
  copies `aircraft/` whole, so it compiles `engine/` under `debug`.

## Mapping and sequence

**Commit 1: this plan**, renamed, with the Status line written as approved by Joe on his word. Push it and open
the draft PR (`M7 PR 6c: the F-16's mass and propulsion from P1 (Tier 3)`).

**Commit 2: mass and `F16Mass`** (6b's "Mass" section).
- `physical/mass/mass.cpp` is rewritten as `F16Mass`, in an anonymous namespace, with descriptor `{"physical",
  "f16_mass"}` and `name()` `mass`.
  - It reads no config key, and `init` ignores the config.
  - `mass = slugs_to_kilograms(637.1595)`. Ixx 9496, Iyy 55814, Izz 63100, Izx 982 and Ixy = Iyz = 0 each go
    through `slug_square_feet_to_kilogram_square_meters`. Each constant is named, with its `F16_inertia.dml` line
    (`:63-111`; the file calls XIZX `bodyProductOfInertia_ZX`, so 982 is already in the product convention).
  - The tensor is `[[Ixx, 0.0−Ixy, 0.0−Izx], [·, Iyy, 0.0−Iyz], [·, ·, Izz]]`, symmetric.
  - The LLT check stays, and `mass.mass` is registered in kg.
- `tests/mass_test.cpp` is replaced. The template's `AircraftMass` test goes, since no key is read. The new
  `F16Mass` tests:
  - `MatchesTheInertiaDml`:
    - each SI value equals the fixture's constant (`checkdata::kInertiaConstants`, looked up by varID) through
      the same named function, bitwise;
    - each also equals the 17-digit literals above to 1e-12 relative.
  - `MassIsTheBandGeneratorsSlugRoute` (Joe's addition): `slugs_to_kilograms(637.1595)` and the plugin's mass
    both equal `9298.643898518938`, bitwise.
    - The comment says this is `generate_nesc_band.py`'s `637.1595 · SLUG_TO_KG` route (PR 3c's D2).
    - It also says the two routes agree because `kKilogramsPerSlug` is the correctly rounded
      4.4482216152605/0.3048, the same double as the generator's `SLUG_TO_KG = 1/KG_TO_SLUG`.
  - `ProductsOfInertiaSignAndSymmetry`:
    - `inertia_b(0,2) < 0` and equals `0.0 − slug_square_feet_to_kilogram_square_meters(982)`;
    - the tensor equals its transpose;
    - the xy and yz entries are +0.0 (`== 0.0`, and `std::signbit` false).
  - `ReportsTableSevenBesideTheDml` (D1): one printed line with both masses in slug and kg, their difference in
    kg, and its weight in N at 9.80665 m/s². It asserts nothing about 637.26.
  - `DescriptorAndSignal` (Deviation 3).
- `tests/CMakeLists.txt` is unchanged in this commit.

**Commit 3: propulsion, `F16Engine`, the turbofan removal and the NoAlloc 1000-frame case** (6b's "Propulsion"
section, plus its "Per-frame rules and NoAlloc" section).
- **`hardware/propulsion/engine/engine_math.hpp`**, namespace `astra::aircraft_f16_engine_math`. It is pure,
  `noexcept` and allocation-free, and includes `../../../common/f16_interp.hpp`.
  - It derives constexpr SI arrays from `engine_tables.hpp`'s verbatim text: altitude breakpoints through
    `feet_to_meters`, and the three tables through `pound_force_to_newtons`, as `aero_math.hpp`'s `to_radians`.
    `MACH_PTS` is used as is.
  - `kMilPwr = 50.0` (`:72-76`) and `kMaxPwr = 100.0`.
  - It provides `Inputs{pwr, altitude, mach}` (percent, m, nd), `thrust(Inputs)` (the build-up above, in N),
    `finite(Inputs)`, `non_finite_wrench()` and `wrench(thrust)`. `wrench(thrust)` gives force (T, 0, 0) with
    moment zero (D4, D5), and there is no state (ADR-0021: none to own).
- **`engine.cpp`**: `F16Engine : PropulsionModel`, with descriptor `{"hardware", "f16_engine"}` and `name()`
  `engine`.
  - It binds `controls.throttle` ("1") and reads no config key.
  - `wrench_b`:
    - `pwr = kPwrPerThrottle · throttle`, with `kPwrPerThrottle = 100.0`. This is the one place the throttle maps
      (R3), and nothing saturates it (decision 4).
    - `altitude = env.altitude_m` (ADR-0033).
    - `mach = air_data::mach(air_data::true_airspeed(air_data::relative_velocity_b(vs, env)),
      env.atmosphere.speed_of_sound_m_s)`.
    - Any non-finite input gives the NaN sentinel, checked before any lookup. That covers a NaN altitude or
      speed of sound, and a zero speed of sound (Deviation 4): an infinite Mach would otherwise clip to Mach 1.
    - Otherwise it returns `wrench(thrust(in))`.
  - The header comment states D8: `altitude_m` is `EarthModel::altitude`'s height, and the geoid is PR 9's
    question.
- **`engine/CMakeLists.txt`** is `astra_add_plugin(SOURCES engine.cpp)`. `git rm` removes `turbofan/` and
  `tests/turbofan_test.cpp`. `tests/CMakeLists.txt` swaps `turbofan_test.cpp` for `engine_test.cpp`.
- **`tests/engine_test.cpp`**: an `EngineUnderTest` harness like `AeroUnderTest`. Its state has `v_b = (RMACH·a,
  0, 0)` with identity attitude, no wind, and a test-chosen speed of sound `a`. It sets `env.altitude_m`
  directly. The tests are 6b's list:
  - `CheckData`:
    - each of the 9 shots runs through `wrench_b` with `throttle = PWR/100` and `altitude_m =
      feet_to_meters(ALT)`;
    - `|newtons_to_pound_force(Fx) − FEX| < tol`, strict, at each shot's own tol;
    - the other five outputs are exactly 0.0, which also meets their 1e-5.
  - `MapPinnedAtIdleMilAndMax` (R3): throttle 0, 0.5 and 1 at (Mach 0, 0 ft) give `pound_force_to_newtons` of
    T_IDLE 1060.0 (`:272`), T_MIL 12680.0 (`:296`) and T_MAX 20000.0 (`:320`), hand-transcribed, each to 1e-12
    relative.
  - `EveryEdgeClips`:
    - through the math seam, at PWR 0, 50 and 100: Mach −0.5 and 2.0, and −1,000 ft and 60,000 ft, each equal
      the matching edge bitwise;
    - through `wrench_b`: −1,000 ft equals 0 ft, and Mach 2.0 at 30,000 ft equals Mach 1.0 there, bitwise.
  - `ThrottleOutsideZeroToOneExtrapolates`: throttle −0.1 and 1.2 at (Mach 0, 0 ft) give −1264 and 22928 lbf
    from the hand-transcribed entries, to 1e-12 relative. A clamp gives 1060 and 20000.
  - `AnUnsetAltitudeGivesANonFiniteWrench`:
    - the default NaN `altitude_m` gives all six components NaN (`std::isnan`, so never sea-level thrust);
    - a second sample, with `altitude_m` set and a NaN speed of sound, does the same (Joe's item 3);
    - a third, with `altitude_m` set, `speed_of_sound_m_s = 0` and `v_b` nonzero, does the same (Deviation 4).
  - `DescriptorAndWriteSeamName` (Deviation 3).
- **`tests/no_alloc_test.cpp`** (its header comment updated):
  - the engine joins `WrenchPathAllocatesNothing`. The sweep adds throttles {−0.1, 0, 0.5, 1, 1.2} and altitudes
    {−1,000, 0, 23,507, 50,000, 60,000} ft, with a set speed of sound. The existing V sweep (0 to 900 ft/s)
    already reaches Mach 0 and past Mach 0.8;
  - `OneThousandFramesAllocateNothing` is new. It runs a `Simulation` from an inline config:
    - `earth/flat`, `gravity/constant` and `atmosphere/isa1976`, with the F-16 aero and engine as models and
      `physical.mass: { plugin: <aircraft>/physical/mass }`;
    - the paths built from `ASTRA_THIS_AIRCRAFT`;
    - `initial_controls` throttle 0.6, `p_n` (0, 0, −3000) m and `v_b` (150, 0, 0) m/s.
  - It asserts no halt, frame 1000 and zero allocations, in the `tests/physics/no_alloc_test.cpp:56-80` pattern.

**Commit 4: gate evidence**, `docs/reviews/evidence/<head>-gate-summary.log`.

Then the Codex adversarial review (`/review-cycle`, at most two correction rounds), the PR body (`/human-review`),
and Joe's reading: the mass tensor, the engine's throttle and altitude mapping, and both tests.

## Validation and risks

Checks (commands, exit codes and counts go in the PR body):
- `ctest --preset asan -R F16 --no-tests=error`, with `ctest --preset asan -N -R F16` split by executable.
  Predicted, with Deviation 3's two tests (Joe confirmed the count at approval):
  | Executable | Suites | Tests |
  |---|---|---|
  | `astra_aircraft_f16_tests` | `F16Aero` 13, `F16Mass` 5, `F16Engine` 6 | 24 |
  | `…_no_alloc` | `F16NoAlloc` | 2 |
  | `physics.` | `NescF16Band` (`nesc_f16_bands_test.cpp:200,221,236,360`) | 4 |
  | | | 30 |

  Both F-16 executables share the `aircraft_f16_tests.` prefix, so the split is read per suite.
- `ctest --preset asan -R NoAlloc --no-tests=error`.
- `ctest --preset asan -R F16Mass -V`: the D1 line, quoted in the PR body.
- `.venv/bin/pytest tests/python -k new_aircraft` and `-k f16_generated`.
- `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`, with the trimmed summary committed.
- `git diff --check origin/main..HEAD`.

Blast radius (pre-registered):
- No config outside `aircraft/f16` loads these plugins. M2's cases 1 to 10, M3's regression, the R1 golden, M4's
  logs and M6's fixtures are therefore unchanged under the full gate.
- `F16Aero` and `test_f16_generated` are unchanged, and `-k new_aircraft` passes.
- Nothing under `core/`, `plugins/`, `python/`, the template, the notional transport, `tests/golden/`, tolerance
  files or `docs/decisions/` changes. `kCoreApiVersion` stays 11.

Risks:
- `CheckData` shot 8's 3.1e-5 lbf margin (above) is the tightest number in the PR. A miss goes to Joe; the
  tolerance is the DML's own.
- The 1000-frame run is untrimmed. At 150 m/s and 3,000 m it stays far inside every domain for 1 s, and the test
  asserts that it completes.
- Rollback: the PR touches only `aircraft/f16`, so a revert restores the template copies.

## Done when

- R1 and R3 pass on `F16Mass` and `F16Engine`, and `F16NoAlloc` is green.
- The full gate exits 0 with `check: OK`, and Codex's adversarial review is resolved within two rounds.
- Joe has read the four items above.
- `/wrap`, before Joe's merge:
  - PLAN.md gains PR 6c's line;
  - STATE.md is written as main reads it after the merge: PR 7 and PR 14 eligible, PR 9 waits on PR 7, PR 13 on
    PR 9, and PR 3 on Joe's go (bumping `kCoreApiVersion` past 11).
