# M7 PR 3c: `EnvironmentSample::altitude_m` and the slug and pound-force units (Tier 3)

Status: **approved by Joe, 2026-10-10, in plan mode**, with his amendments 1 to 7 folded in (below).
Owner: Joe. Builder: Claude (one builder). Tier: **3** (`core/kernel/`, `core/util/`, the plugin ABI, a new ADR).
Base: `origin/main` = `c8f3128` (#113), confirmed by `git fetch`. Worktree `m7-pr3c` (branch `worktree-m7-pr3c`)
from `origin/main`. This draft was moved (`mv`) into the worktree and renamed (`docs/agents/claude-code.md`), and
nothing stays untracked in the main checkout.
Binding: ADR-0002, 0003, 0016, 0020, 0021, 0030, 0032. Sources: PR 6b's plan (`docs/plans/m7-pr6b-f16-aircraft.md`)
decisions 1 to 3; precedents `docs/plans/m7-pr3b-earth-relative-rate.md` (the field) and
`docs/plans/m6-pr4-units.md` (the units). No Codex plan critique: ADR-0032 and PR 3b pin the design (Joe).

## Joe's amendments at approval (2026-10-10)

1. **D2 is decided differently.** The draft's rationale did not hold: 1/`0x1.d30140bb8996fp+3` is also
   `0x1.18aa4776b497cp-4`, so the `kilograms_to_slugs(1.0)` pin cannot choose between the two values.
   - `kKilogramsPerSlug` is the correctly rounded value, `0x1.d30140bb8996fp+3`. That is also the generator's
     `SLUG_TO_KG`, bit for bit.
   - It is spelled `1.0 / (kMetersPerFoot / kNewtonsPerPoundForce)`, the reciprocal of Table 73's
     kilogram-to-slug in named constants, so no literal repeats.
   - Its comment states that it is the correctly rounded 4.4482216152605/0.3048 (`kRadiansPerDegree`'s
     precedent), and that the plain quotient of the two doubles lands one ulp below.
   - The tests, the guards, U4 and the Python constant are recomputed below.
   - `slugs_to_kilograms(637.1595)` = 9298.643898518938, which agrees with the generator's route. 6c's plan
     notes it.
2. **D1 accepted.** Commit 1 writes ADR-0033 with Status *proposed*. When Joe accepts it, in a turn he arms the
   guard for, I write Status *accepted* and the Approval line with his date. The architecture status line's
   "accepted" rides in that same Markdown-only commit. After the gate, that commit needs only `just docs-check`
   and `git diff --check <gated-head>..HEAD` (workflow, Verification).
3. **D3 accepted.** The recorder is the reader. Its header comment says so and says why. The PR body states
   that B1 to B6 and NoAlloc F are unchanged bit for bit.
4. **The field's comment** states the signed-zero fact (design 1), in place of "its signed zero is never
   reached".
5. **`_core.pyi`:** the build regenerates the stubs, and I check the five overload pairs. The PR body says so.
6. **The amendment's PR 7 row and 6c Depends cells** stand. The amendment cites Joe's Q1 decision of
   2026-10-10.
7. **Everything else stands.**

## Context

The F-16 engine (PR 6c, designed in PR 6b's plan) reads altitude, and a `WrenchSource` sees only `(VehicleState, EnvironmentSample)`. Reading
`vs.p_n()` is the representation trap `wind_model.hpp:12-25` names (PR 6b decision 1). Its mass and engine also
need slug and pound-force conversions, and ADR-0002 allows no F-16-local constants (decision 2). This PR supplies
both in the core, so PR 6c's mass and propulsion have something to read.

## Repository findings (read at `c8f3128`)

- **#113 is merged, not stopped.** `gh pr view 113`: MERGED at `c8f3128`, 2026-10-11 02:48 UTC.
  - `origin/main` and `m7-pr6b` (`23274d5`) have identical trees.
  - On main, `aircraft/f16` has the F-16 aero and the generated tables. `mass/` and `turbofan/` are still
    the template's verbatim copies, which is 6b's plan after commit 4.
  - PLAN.md has no 6b line, and STATE.md is unchanged.
  - **Joe's decision (2026-10-10), Q1:** row 6b stays as merged in #113. That covers the plan, the verbatim
    template copy, the generator and fixtures, the aero port and the regeneration test.
  - 6b's commits 5 and 6 (mass and propulsion) become **PR 6c**. It runs from a new worktree `m7-pr6c` off
    `origin/main`, under a short plan of its own that cites 6b's plan. Joe removes the `m7-pr6b` worktree.
  - This PR does not edit `docs/plans/m7-pr6b-f16-aircraft.md`.
  - Nothing on main reads the new field.
- **The stage path** (`core/src/kernel/vehicle.cpp`).
  - `prepare_stage_environment` (`:344`) sets `stage_env.omega_ie_b` first (`:357`).
  - It then computes `const double altitude_m = rigid_body_->earth().altitude(vs_stage)` (`:360`), but **only
    inside `if (atmosphere_ != nullptr)`**. A sim with no atmosphere computes no stage altitude today.
  - It returns early twice:
    - on the domain range check (`:366-374`);
    - on a non-finite wind or atmosphere (`:405`).
  - `evaluate_derivative_stage` (`:452`) copies the latched sample (`:460`), so the step path (`:528`) and the
    probe path (`Vehicle::derivative`, `:608`) both reach this one function.
- **The latched sample.** `Simulation::sample_environment` (`simulation.cpp:717`) computes `earth_->altitude(vs)`
  for the wind and the atmosphere, but stores neither. Its readers (`DerivedSignals::pack`, the exchange's
  `pack_sim_to_sw`) read `wind_n` and `atmosphere` only.
- **Each Earth model's `altitude`.**
  - `earth/flat`: `-p_n(2)` (`flat.cpp:36-39`).
  - `wgs84_rotating`: `p_n(2)`, the height above the ellipsoid (`wgs84_rotating.cpp:83-85`).
  - `earth/round`: `p_n(2)`, the height above the sphere (`round.cpp:116-118`).
  - The interface says "height above the Earth model's reference surface" (`earth_model.hpp:34-38`).
- **No positional or `sizeof` use of `EnvironmentSample`** in `core`, `plugins`, `aircraft`, `tests`, `python`
  or `embedded` (grep). Python binds no field of it (PR 3b finding, unchanged).
- **Stale comment.** `wrench_source.hpp:58` says atmosphere is per-stage "as of PR 3c". That means M2 PR 3c,
  which this PR's name makes ambiguous.
- **The recorder.** `tests/plugins/earth_rate_recorder` records `omega_ie_b`, `omega_eb_b`, `q_nb` and `p_n` into
  four slots per kind, and returns a zero wrench. Its readers:
  - `tests/unit/kernel/simulation_earth_rate_test.cpp` (suite `SimulationEarthRate`, B1 to B6);
  - `tests/physics/no_alloc_test.cpp:81-104`.
- **The API version** is 10 (`core_api.hpp.in:51-57`). Where it is used:
  - `tests/python/test_package.py:34` is its one hand-kept pin;
  - `stale_api_version.cpp:1-4` and `loader_test.cpp:58-60` name "version 9" and "omega_ie_b" as their example;
  - everything else reads `kCoreApiVersion` or `core_api_version()`.
- **Units.**
  - `core/include/astra/util/units.hpp` has three constants and six functions, each one multiply (to SI) or
    one divide (from SI) by the same named constant.
  - Bindings: `def_conversion<&f>` (`python/bindings/_core.cpp:366-379`), float and 1-D float64.
  - `python/astra/_core.pyi` is **generated by the build and untracked** (`python/CMakeLists.txt:45-54`), so it
    gains the stubs with no hand edit.
  - `tests/python/test_units.py`'s `CONVERSIONS` table drives every parametrized test.
  - `test_package.py`'s `UNIT_SUFFIXES` check passes the five new names (none ends in a listed suffix).
- **Constants, checked in exact rational arithmetic in plan mode.** Write `a` = 4.4482216152605 and
  `b` = 0.3048:
  - `a` as a double is `0x1.1cafa9b6b9dd7p+2`.
  - **The plain quotient `a / b` evaluates to `0x1.d30140bb8996ep+3`**, the once-rounded quotient of the two
    doubles. That is **one ulp below** the correctly rounded *decimal* quotient, `0x1.d30140bb8996fp+3`,
    because 0.3048 has no exact binary value.
  - **`1.0 / (b / a)` evaluates to `…96fp+3`**, the correctly rounded value. It is also the generator's
    `SLUG_TO_KG = 1 / KG_TO_SLUG` (`generate_nesc_band.py:106`), bit for bit. Joe's amendment 1 chose it.
  - **The 1.0 pin.** `kilograms_to_slugs(1.0)` is `0x1.18aa4776b497cp-4` under either value, so it equals the
    generator's `KG_TO_SLUG = 0.3048 / 4.4482216152605` either way.
  - **The F-16 mass.** `slugs_to_kilograms(637.1595)` = 9298.643898518938, which is the generator's route.
  - **The slug-ft² product.** `k * b * b` (either association) = `0x1.5b16e29369c65p+0` with the new k, equal
    to the correctly rounded decimal a·b.
  - **Round trips.** Over 10⁶ seeded values in [−1e5, 1e5], every round trip of both pairs is within one ulp.
    - lbf → N → lbf is off by one ulp for 5.8 % of values, and N → lbf → N for 8.6 %.
    - slug → kg → slug: 7.6 %; kg → slug → kg: 5.2 %.
    - Not exact even at integers: `(982·a)/a ≠ 982`.
  - **Reciprocal guards.**
    - With the new k, `k · (1/k)` = 1.0, and `3.0/k` equals `3.0·(1/k)`. So neither the x/x identity nor 3.0
      guards slugs any more.
    - The guard is `9.0 / k` = `0x1.3bbf90658b2abp-1`, where `9.0 · (1/k)` gives `…acp-1` (the first integer
      that tells them apart).
    - `a · (1/a)` = 1.0, so pound-force needs a pinned value too: `39.375 / a` = `0x1.1b425f685d97cp+3`, where
      the reciprocal gives `…97bp+3` (the first multiple of 1/8 that tells them apart).

## Design

1. **The field** (`environment_sample.hpp`), after `omega_ie_b`:
   `double altitude_m = std::numeric_limits<double>::quiet_NaN();`
   - The name carries a unit suffix, which ADR-0030 allows on a C++ identifier. It matches
     `AtmosphereModel::at(altitude_m)` and `WindModel::begin_frame(…, altitude_m)`.
   - **Its comment says:**
     - It is `EarthModel::altitude` at the stage's own state: the height above the configured Earth model's
       reference surface, in meters. That is the ellipsoid under `wgs84_rotating`, the sphere under
       `earth/round`, and −p_n[2] under `earth/flat`.
     - It is the value `AtmosphereModel::at` and `WindModel::begin_frame` already receive. It claims nothing
       about a geoid (PR 9's question, addendum D8).
     - It is set first in every RK4 stage's copy, with `omega_ie_b`, on the step and probe paths. A stage
       whose wrenches the environment checks skip still carries it.
     - It is NaN by default and in the latched sample. A sample built outside the stage path must set it from
       the sim's Earth model, and a forgotten value gives its reader a NaN wrench.
     - It is not canonicalized. At exactly zero altitude, `earth/flat` gives −0.0 (−p_n[2] with p_n[2] = +0.0),
       and the field carries it. Nothing subtracts it, so no reader's result changes.
     - `HealthMonitor` does not check it: it is the core's own value.
   - The file header's per-field list (L1-8) gains the line. The record is ADR-0033 (D1).
2. **The assignment** (`vehicle.cpp`, `prepare_stage_environment`), right after the `omega_ie_b` line:
   `stage_env.altitude_m = rigid_body_->earth().altitude(vs_stage);`
   - The atmosphere block then reads `stage_env.altitude_m` in place of its local. That is the same call on
     the same state, so the range check and `at()` see bit-identical altitudes.
   - The only new per-frame work is one `altitude()` virtual call per stage, and only in a sim with no
     atmosphere. One `double` is added to the stage copy. No allocation, no exception.
   - The function's comment ("What this function writes into stage_env") and `vehicle.hpp:393`'s comment
     gain the field.
3. **The latched sample keeps NaN.** `sample_environment` is not touched, and `vehicle.hpp:150`'s comment
   names `altitude_m` beside `omega_ie_b`.
4. **Other comments.**
   - `wrench_source.hpp:54-59` gains the field, and its "as of PR 3c" becomes "as of M2 PR 3c".
   - `model.hpp:58`: "per-stage atmosphere, omega_ie_b and altitude_m".
5. **API bump, 10 → 11** (`core_api.hpp.in`). Entry 11 (M7 PR 3c, ADR-0033) states:
   - `EnvironmentSample` gained `altitude_m`, and no vtable changed;
   - it crosses the loader boundary (ADR-0016's test), so a plugin built against 10 is rejected
     (`tests/plugins/stale_api_version`, unchanged code).
   - Comment-only updates: `stale_api_version.cpp:1-4` and `loader_test.cpp:58-60` (their example becomes
     "10, before `altitude_m`"), and `test_package.py:34` → 11, with its comment chain extended.
   - Every shipped plugin rebuilds against 11 with no source change.
6. **Units** (`units.hpp`), beside the existing pairs, with the header comment extended.
   - **The new constants:**
     ```cpp
     // The international pound-force, exact by definition (4.4482216152605 N; the denominator of Vol II
     // Table 73's "kilogram to slug", p.93, and generate_nesc_band.py's LBF_TO_N).
     inline constexpr double kNewtonsPerPoundForce = 4.4482216152605;
     // One slug is one lbf·s²/ft: the reciprocal of Table 73's "kilogram to slug" (0.3048/4.4482216152605,
     // generate_nesc_band.py's KG_TO_SLUG). This is the correctly rounded 4.4482216152605/0.3048,
     // 0x1.d30140bb8996fp+3 (checked in exact rational arithmetic; units_test.cpp pins it), and the
     // generator's SLUG_TO_KG bit for bit. The plain quotient of the two doubles lands one ulp below.
     inline constexpr double kKilogramsPerSlug = 1.0 / (kMetersPerFoot / kNewtonsPerPoundForce);
     ```
     No literal repeats (Joe's amendment 1).
   - **The functions:**
     - `pound_force_to_newtons` (× `kNewtonsPerPoundForce`) and `newtons_to_pound_force` (÷);
     - `slugs_to_kilograms` (× `kKilogramsPerSlug`) and `kilograms_to_slugs` (÷);
     - `slug_square_feet_to_kilogram_square_meters`, which returns
       `x * (kKilogramsPerSlug * kMetersPerFoot * kMetersPerFoot)`. The parenthesized product folds at compile
       time, so the call is one rounding by one derived value (`0x1.5b16e29369c65p+0`), and no new literal
       appears.
7. **Bindings.**
   - Five `def_conversion` lines in `_core.cpp`, each documented "… (core/util/units, ADR-0002)."
   - The constants stay unbound.
   - `_core.pyi` regenerates with the build (`just python`), and I check it for the five overload pairs.

## Tests (pre-registered names and values)

**A. The recorder, extended** (`tests/plugins/earth_rate_recorder`, its header comment updated):
- Each slot also records `env.altitude_m`, published as `earth_rate_recorder.<kind>.<k>.altitude_m` (unit "m").
- **It becomes the field's reader.** `wrench_b` returns `force_b.x() = env.altitude_m - env.altitude_m`. That is
  +0.0 for every finite altitude (IEEE round-to-nearest) and NaN for an unset one. Every other component is
  zero.
- Every existing use passes a finite stage altitude, so B1 to B6, `no_alloc_test.cpp`'s test F and their
  wrench totals are unchanged bit for bit.

**B. The new suite `SimulationStageAltitude`, which joins `tests/unit/kernel/simulation_earth_rate_test.cpp`.**
It is in `astra_unit_tests`, matches `Simulation`, and reuses that file's fixtures and `slot()` helpers.
- **The independent expected value** comes from each slot's recorded stage `p_n`:
  - `wgs84_rotating`: `p_n(2)`;
  - `earth/flat`: `-p_n(2)`.
- Every assertion is bitwise.
1. `EveryStepStageCarriesTheEarthModelsAltitudeOnWgs84Rotating`: one `step()` on `rotating_yaml()`, which has
   no atmosphere.
   - All four `wrench_b` and four `integrate_state` slots equal `p_n(2)`.
   - Both kinds of one stage carry the same bits.
   - Slot 1 ≠ slot 0, so the value is the stage's own.
   - This case is set only by the new placement: today's code computes no altitude without an atmosphere.
2. `TheDerivativeProbeCarriesTheProbedStatesAltitudeOnWgs84Rotating`: `derivative()` at `probe_position()`.
   Slot 0 of both kinds is 1500.0, not the sim's own 1000.
3. `IsMinusPnDownOnAFlatEarthOnBothPaths`: `earth/flat` with `atmosphere/isa1976` in range, at
   `negative_row_initial("[0.0, 0.0, -1000.0]")`.
   - Step: every slot equals `-p_n(2)`.
   - Probe at p_n = (0, 0, −1500): slot 0 = 1500.0.
4. `IntegrateStateSeesItOnStagesWhoseWrenchesAreSkipped`: B6's two fixtures, unchanged.
   - (i) `nan_atmosphere`: `wrench` count 0, all its slots NaN; every `integrate_state` slot equals `-p_n(2)`
     (1000.0 territory).
   - (ii) isa1976 crossing 86000 m: `wrench` count 1 (slot 0 = `-p_n(2)` ≤ 86000). All four `integrate_state`
     slots equal `-p_n(2)`, and slots 1 to 3 are > 86000.
5. `TheLatchedSampleKeepsNaN`: after B.1's step, and after B.3's step (with atmosphere),
   `vehicles().front().environment().altitude_m` is NaN.

**C. `VehicleEnvironment.AnUnsetAltitudeGivesItsReaderANaNWrench`, which joins
`tests/unit/kernel/vehicle_environment_test.cpp`** (matches `Vehicle`; ADR-0020's no-silent-NaN suite).
- It loads `tests/plugins/earth_rate_recorder` by path (`PluginLoader`, as `template_aero_migration_test.cpp:134`
  does) and calls `wrench_b` on a hand-built sample.
- `EnvironmentSample{}`: `force_b.x()` is NaN, never a number.
- The same sample with `altitude_m = 1000.0`: `force_b.x()` is bitwise +0.0.
- **The health half:** `HealthMonitor::check_wrench` on the NaN wrench halts `kNonFiniteWrench` and names
  `tests/plugins/earth_rate_recorder`. No new health test file is needed. Inside a sim the core always sets the
  field, which B shows.

**D. `units_test.cpp`** (new `TEST(Units, …)`; existing ones unchanged).
- **Constants:**
  - `static_assert(kNewtonsPerPoundForce == 4.4482216152605)`;
  - `static_assert(kKilogramsPerSlug == 0x1.d30140bb8996fp+3)`;
  - `static_assert(kKilogramsPerSlug == 1.0 / (0.3048 / 4.4482216152605))`: the generator's `SLUG_TO_KG`, in
    literals.
- **Known values:**
  - `pound_force_to_newtons(1.0) == 4.4482216152605`;
  - `slugs_to_kilograms(1.0) == kKilogramsPerSlug`;
  - `slug_square_feet_to_kilogram_square_meters(1.0) == 0x1.5b16e29369c65p+0`.
- **x/x identities:**
  - `newtons_to_pound_force(kNewtonsPerPoundForce) == 1.0`;
  - `kilograms_to_slugs(kKilogramsPerSlug) == 1.0`.
  Neither guards against a reciprocal here, since `a·(1/a)` and `k·(1/k)` both round to 1.0. The guards are
  the pins below.
- **The generator pin:** `kilograms_to_slugs(1.0) == 0.3048 / 4.4482216152605`, bitwise. That is the generator's
  `KG_TO_SLUG` expression, evaluated at the assertion site.
- **Reciprocal guards:**
  - `newtons_to_pound_force(39.375) == 0x1.1b425f685d97cp+3`;
  - `kilograms_to_slugs(9.0) == 0x1.3bbf90658b2abp-1`.
- **Round trips,** each direction of both pairs, over a fixed list: within one ulp, and `EXPECT_NEAR`, in
  `AnglesRoundTripWithinOneUlp`'s form. The comment cites the 10⁶-value measurement above.
- **The one-ulp disclosure:** `4.4482216152605 / 0.3048 == std::nextafter(kKilogramsPerSlug, 0.0)`. The plain
  quotient lands one ulp below, so a later "simplification" to it fails here, visibly.
- **Specials** (±0, ±inf, NaN) through all five.
- **Mutations,** applied one at a time with `-R Units`, then reverted. Failing names go in the PR body.
  - U1: `newtons_to_pound_force` multiplies by `1/kNewtonsPerPoundForce`. Fails the 39.375 pin.
  - U2: `kilograms_to_slugs` multiplies by `1/kKilogramsPerSlug`. Fails the 9.0 pin.
  - U3: the two pound-force operators are swapped. Fails the known values.
  - U4: `kKilogramsPerSlug = kNewtonsPerPoundForce / kMetersPerFoot` (the plain quotient, one ulp low). Fails:
    - both static_asserts;
    - the disclosure;
    - `slugs_to_kilograms(1.0)`'s known value.
    The 1.0 generator pin passes, as amendment 1 says.
  - U5: the slug-ft² function uses `kMetersPerFoot` once. Fails its known value.
  - U6: `kKilogramsPerSlug = kMetersPerFoot / kNewtonsPerPoundForce` (inverted). Fails the static_asserts and
    the known values.

**E. `tests/python/test_units.py`.**
- `CONVERSIONS` gains five rows. The constants are derived in the test:
  - `NEWTONS_PER_POUND_FORCE = float(Fraction(Decimal("4.4482216152605")))`.
  - `KILOGRAMS_PER_SLUG = float(Fraction(Decimal("4.4482216152605")) / Fraction(Decimal("0.3048")))`: the
    decimal definition, rounded once.
  - `KILOGRAM_SQUARE_METERS_PER_SLUG_SQUARE_FOOT = float(Fraction(Decimal("4.4482216152605")) * Fraction(3048, 10000))`:
    the decimal definition a·b, rounded once.
- The constants test asserts each against `units_test.cpp`'s hex pins. It also asserts the disclosure, inverted: the
  plain double quotient `4.4482216152605 / 0.3048` is `math.nextafter(KILOGRAMS_PER_SLUG, 0.0)`.
- The known-values test gains the C++ suite's new pins.
- The parametrized tests then cover all five names with no further edit:
  - bitwise one rounding over the sweep and specials;
  - the float protocol;
  - the array form element by element;
  - the refusals.
- `test_the_constants_are_not_bound` gains the three values.
- **Binding mutants** (`pytest -k units`, then reverted):
  - B1: the array path of `kilograms_to_slugs` multiplies by a reciprocal;
  - B2: two new names swapped.

**F. Mutations on the field** (`-R 'Simulation|Vehicle'`, then reverted; the failing names go in the PR body):
- M1: the assignment moved inside `if (atmosphere_ != nullptr)`. Fails B.1 and B.2 (NaN without atmosphere),
  and passes B.3.
- M2: the assignment moved after the range check's early return. Fails B.4 (ii)'s `integrate_state` slots 1
  to 3.
- M3: the assignment moved after the non-finite early return. Fails B.4 (i).
- M4: altitude taken from the latched frame-start state. Fails B.1's slot 1 ≠ slot 0, and B.2.
- M5: `sample_environment` sets the latched field. Fails B.5.

## ADR-0032's text: is the field list closed? (item 6)

**Closed.**
- ADR-0032 supersedes ADR-0020's Consequences clause "with zero further core interface change" *"for the body
  rate relative to the Earth only"* (Related/supersedes, L6-8). Its Decision 1 names one field, `omega_ie_b`.
- The surviving ADR-0020 clause therefore still says no further core interface change. ADR-0016 also says a
  field on `EnvironmentSample` is an interface change that crosses the loader boundary.
- The text Joe paraphrased ("the sample carries only `wind_n` and `atmosphere`") is ADR-0032's Context, not a
  rule. The rule is the scoped supersession.

**So this PR writes ADR-0033** (`docs/decisions/0033-stage-altitude.md`, status *proposed*). It is short and
additive.
- **Supersedes:** the same ADR-0020 clause, for the stage altitude only, as ADR-0032 did for the Earth rate.
  ADR-0020 and ADR-0032 are not edited.
- **Decision:**
  - the field, its meaning (`EarthModel::altitude`'s, with no geoid claim) and its unit;
  - per stage, set first with `omega_ie_b`, on both paths;
  - NaN by default and in the latched sample (ADR-0017's convention), with no canonicalization;
  - not checked by `HealthMonitor`, not published as a signal;
  - the `kCoreApiVersion` bump.
- **Rejected:**
  - PR 6b decision 1's three: a config key naming the representation, reading `p_n(2)`, and inverting
    `pressure_pa`;
  - a zero default;
  - a frame-latched value, which is wrong per stage under ADR-0016.
- **Consequences:** PR 6c's engine reads it, and PR 7's evaluator sets it beside `omega_ie_b`.
- **Approval:** Joe accepts it before 6c's mass commit. It is not a precondition for this PR's implementation
  commits.

Joe arms the guard (`ASTRA_TOUCH_GOLDENS=1`) for that one write at approval. I write it in the turn the guard is
armed (lesson 2026-10-09). The same commit brings the docs current:
- architecture §7's ADR-0032 paragraph gains a sibling paragraph for `altitude_m` (*ADR-0033*, built in M7 PR 3c);
- architecture's status line gains rev 13 (§7, ADR-0033, *proposed*), updated to accepted when you accept.

## Commits (each pushed; the draft PR opens at the first)

0. The plan, renamed, with its Status line approved by Joe (2026-10-10).
1. ADR-0033, architecture §7 and rev 13 (guard armed by Joe).
2. **The field**, with tests A, B and C:
   - the header and the comments (design 1 to 4);
   - the API bump (design 5).
3. **The units**, with tests D and E:
   - the bindings;
   - the M7 plan's dated amendment (2026-10-10, below).
4. Gate evidence: `docs/reviews/evidence/<head>-gate-summary.log`.

**The M7 plan amendment** (`docs/plans/m7-f16-nesc-oracle.md`), dated 2026-10-10. It cites PR 6b's plan
(decisions 1 to 3) and Joe's Q1 decision.

**Row 3c (new).**
- Tier 3.
- Scope: `EnvironmentSample::altitude_m` and the slug and pound-force units.
- Binding: ADR-0032's pattern, and ADR-0033.
- Depends on PR 3b.
- Acceptance: `ctest --preset asan -R 'Vehicle|Simulation|Units|PluginLoader' --no-tests=error` and
  `.venv/bin/pytest tests/python -k units`.

**Row 6b** reads as merged in #113: the generator, the tables, the fixtures and the aero.

**Row 6c (new).** Mass and propulsion from P1.
- Tier 3.
- Binding: ADR-0001, 0002, 0016, 0017, 0020, 0021, 0030, 0032.
- Depends on PR 3c and PR 6b.
- Approved design: 6b's plan's Mass and Propulsion sections, and its decisions 1, 2 and 4.
- Acceptance, 6b's plan's commands:
  - `ctest --preset asan -R F16 --no-tests=error`, with the `-N` count split by executable;
  - `-R NoAlloc`;
  - `-R F16Mass -V`;
  - `pytest -k new_aircraft` and `-k f16_generated`;
  - the full gate.

**The Depends cells** that need the F-16's mass or propulsion name 6c: PR 7, PR 9, PR 13 and PR 14. That
follows Joe's ordering: PR 7 and PR 14 wait on 6c, and PR 13 depends on 6c and PR 9.

**The obligations table.**
- The 6b row splits. The aero's MRC-to-CM transfer stays under 6b, done. The DML's mass (637.1595 slug, R1)
  moves to a 6c row.
- There is no PR 7 row today: PR 7's `omega_ie_b` duty lives in ADR-0032's Consequences and in STATE.md. The
  amendment **adds** a PR 7 row: the evaluator sets `omega_ie_b` (ADR-0032) and `altitude_m` (this PR) from the
  sim's Earth model at the frame-start state.

## Blast radius (pre-registered)

- **No plugin on main reads the field** (it does not exist yet), so every value is unchanged. That includes:
  - M2's NESC cases 1 to 10;
  - M3's frozen regression and the R1 golden;
  - M4's byte-identical logs;
  - M6's batch fixtures, and `-k new_aircraft`;
  - `aircraft/f16`'s aero and `F16NoAlloc`.
  The full gate runs all of them, and that is the identity evidence: no separate identity log.
- The atmosphere's stage altitude is the same call on the same state, so its samples are bit-identical.
- Nothing under `tests/golden/` or in any tolerance file. Under `docs/decisions/`, only the new ADR-0033.
- `aircraft/` (the template, the notional transport, the F-16) and `plugins/` are untouched in source.
- The NoAlloc suite passes unchanged.

## Checks (commands and results in the PR body)

- `ctest --preset asan -R 'Vehicle|Simulation|Units|PluginLoader' --no-tests=error`, with the
  `ctest --preset asan -N -R …` count before and after.
- `ctest --preset asan -R NoAlloc --no-tests=error`, with its `-N` count.
- `just python`, then `.venv/bin/pytest tests/python -k 'units or new_aircraft'`.
- The mutation runs (D, E, F), with the failing names.
- `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`, with the trimmed summary committed.
- `git diff --check origin/main..HEAD`.

Then the Codex adversarial review (at most two correction rounds). Then you read the stage-order change in
`vehicle.cpp` and `units_test.cpp`.

## `/wrap`

- **PLAN.md** gains two lines.
  - PR 3c's line.
  - PR 6b's line for #113, as merged, since #113 merged without its own `/wrap`. Plan:
    `docs/plans/m7-pr6b-f16-aircraft.md`. Scope: the generator, the tables, the fixtures, the aero and the
    regeneration test. Mass and propulsion go to PR 6c.
- **STATE.md**, as main reads it after 3c's merge:
  - PR 6c is eligible, from a new worktree `m7-pr6c` off `origin/main`, with a short plan that cites 6b's;
  - PR 7 and PR 14 wait on 6c;
  - PR 13 depends on 6c and PR 9;
  - PR 9 waits on PR 7;
  - PR 7's thread names `altitude_m` beside `omega_ie_b`;
  - ADR-0033's acceptance, if still pending;
  - PR 3 bumps `kCoreApiVersion` past 11.

## Decisions for Joe

- **Q1 (#113 merged): decided by Joe, 2026-10-10.** Mass and propulsion go to a new PR 6c; row 6b stays as
  merged. Folded into the amendment and `/wrap` above.
- **D1: accepted (Joe, 2026-10-10).** The acceptance commit follows amendment 2.
- **D2: decided by Joe, 2026-10-10 (amendment 1).** The correctly rounded `…96fp+3`, spelled
  `1.0 / (kMetersPerFoot / kNewtonsPerPoundForce)`.
- **D3: accepted (Joe, 2026-10-10).** The recorder becomes the field's reader (`x − x`), rather than a new test
  plugin.

## Risks

- **The recorder's wrench is no longer literally zero.** It is +0.0 for finite altitudes, and B1 to B6 and
  NoAlloc F show it unchanged. A NaN would surface as a halt, which is the point of C.
- **The `just python` rebuild** is needed before pytest. Lesson 2026-09-25 covers the hand-kept API pin.
