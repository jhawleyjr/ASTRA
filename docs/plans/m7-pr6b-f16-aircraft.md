# M7 PR 6b: `aircraft/f16` from the template (Tier 3)

Status: **approved by Joe, 2026-10-10, in plan mode**, with his seven amendments folded in (below). Joe's seven
amendments of 2026-10-10 (the M7 plan amendment moved to PR 3c, descriptor names and signals, the ctest
split, the speed-floor premise, decision 4, the regeneration test's subprocess hygiene, and the rest
standing) are folded in.
Owner: Joe. Builder: Claude, worktree `m7-pr6b` from `origin/main` (`e4c9c63` or later).
Final name on approval: `docs/plans/m7-pr6b-f16-aircraft.md` (moved from the main checkout's
`docs/plans/` into the worktree, `docs/agents/claude-code.md`).
Binding: ADR-0001, 0002, 0016, 0017, 0020, 0021, 0028, 0030, 0031, 0032. Sources: the M7 plan
(`docs/plans/m7-f16-nesc-oracle.md`) row 6b, R1 to R3, Q14, Q15; the addendum
(`docs/plans/m7-f16-inventory.md`) §1.1, §2 to §4, §8, D1 to D8, §10.

## Context

The F-16 is M7's vehicle: the NESC atmospheric cases 11 to 16 fly it. PR 6b builds `aircraft/f16` with
`just new-aircraft f16` and replaces the template's three plugins with ports of P1 (NASA's DAVE-ML
`F16_inertia.dml`, `F16_aero.dml`, `F16_prop.dml`), each proven against the DML's own `checkData`.
Nothing flies yet: no configs for the NESC cases (PR 9), no trim, no envelope hook (PR 13), no peer (PR 11a).

## Decisions taken in planning (Joe, 2026-10-10)

1. **Engine altitude: PR 3c.** A WrenchSource sees only `(VehicleState, EnvironmentSample)`, which has no
   altitude, and reading `vs.p_n()` is the representation trap `wind_model.hpp:12-25` names. A small Tier 3
   core PR, **PR 3c**, adds `EnvironmentSample::altitude_m` (EarthModel::altitude per RK4 stage, set before
   the atmosphere range check, NaN on the latched sample; `kCoreApiVersion` 10 → 11). Its own worktree,
   plan and PR; this plan does not design it. Rejected for 6b, with reasons:
   - a config key naming the representation: no plugin can check it against the Earth model, so a
     mismatch reads the wrong altitude silently (the trap `wind_model.hpp` names);
   - reading `p_n(2)` as geodetic height: on a flat Earth it reads −altitude and clips to the 0 ft
     thrust row silently;
   - inverting `atmosphere.pressure_pa`: geopotential altitude on an ISA day only, wrong on M9's
     non-standard days.
2. **Slug and pound-force: PR 3c.** `core/util/units` gains `kNewtonsPerPoundForce = 4.4482216152605`,
   `kKilogramsPerSlug = 4.4482216152605 / 0.3048` (Vol II Table 73, p.93; the same as
   `generate_nesc_band.py:98-108`), and `pound_force_to_newtons`, `newtons_to_pound_force`,
   `slugs_to_kilograms`, `kilograms_to_slugs`, `slug_square_feet_to_kilogram_square_meters`, bound in
   `astra._core`. F-16-local constants were rejected (ADR-0002 has no exception); touching `units.hpp` in 6b
   was rejected (breaks this PR's blast radius).
3. **Commit order** follows from 1 and 2: generator and fixtures, then aero, then (after merging
   `origin/main` with PR 3c) mass and propulsion. The M7 plan's dated amendment (PR 3c's row, PR 6b's
   dependency on PR 3c, PR 7's obligation gaining `altitude_m`) travels with PR 3c, which merges first, as
   PR 6a carried PR 3b's row; this PR cites it and does not edit the M7 plan.
4. **PWR is not saturated.** `F16_prop.dml:44-49` gives PWR no `minValue`/`maxValue`, and
   `F16_prop_create.m` has no `Saturate` block, so addendum §8's rule (every `minValue`/`maxValue` is a
   saturation) read in reverse gives none: a throttle outside 0..1 extrapolates the piecewise build-up.
   Nothing refuses a config throttle outside 0..1 under `astra run`; the exchange's limits bound the ICD
   throttle (§15), and PR 9 decides whether its configs' throttle is checked.

## Scope and non-goals

In: `aircraft/f16/**` (three plugins, generated tables, generator, fixture reader, fixtures, tests),
`tests/python/test_f16_generated.py`, this plan, and at `/wrap` PLAN.md's PR 6b line and STATE.md. The M7
plan is not edited here (decision 3).

Out: `core/`, `plugins/`, `python/astra/`, the template, the notional transport, `tests/golden/**`,
tolerance files, `docs/decisions/**`; NESC configs, trim, the envelope declaration (PR 13), the peer, a
CG input (the CM is fixed at 25 % MAC, README `:1234`), a Mach or altitude dependence in the aero.

## The tree after this PR

```
aircraft/f16/
  common/f16_interp.hpp                       clip-at-edge linear lookup, 1-D and 2-D (no CMakeLists:
                                              not a plugin); namespace astra::aircraft_f16_interp
  physical/mass/{CMakeLists.txt, mass.cpp}
  physical/aero/{CMakeLists.txt, aero.cpp, aero_math.hpp, aero_tables.hpp (generated)}
  hardware/propulsion/engine/{CMakeLists.txt, engine.cpp, engine_math.hpp, engine_tables.hpp (generated)}
  tools/generate_f16_tables.py                the emitter (Q14)
  tests/{CMakeLists.txt, mass_test.cpp, aero_test.cpp, engine_test.cpp, no_alloc_test.cpp,
         dml_reference.hpp (the DML-units reference), extract_f16_checkdata.py,
         f16_checkdata.hpp (generated by the reader), .clang-tidy}
  plots/                                      the template's starter packages, unchanged
```

- `hardware/propulsion/turbofan/` and `tests/turbofan_test.cpp` are deleted. `configs/cruise.yaml` is
  deleted in the aero commit (it names the template's aero keys and the turbofan; PR 9 writes the
  F-16's configs).
- Plugin classes and helpers sit in anonymous namespaces. The headers shared with tests use
  aircraft-specific namespaces (`astra::aircraft_f16_aero_math`, `astra::aircraft_f16_engine_math`,
  `astra::aircraft_f16_interp`), so the static-plugins preset links them beside the template's and the
  notional transport's with no ODR collision. Descriptor names are `f16_mass`, `f16_aero`, `f16_engine`,
  as Joe's brief asks. Their uniqueness is a convention, not an ODR need: the template and the notional
  transport already share `mass`, `aero` and `turbofan`, the static registration table keys by path
  (`cmake/AstraPlugin.cmake:182`), and the name is only recorded in `/plugins` (`run_log.cpp:121`).
  `name()` stays `mass`, `aero`, `engine`.
- Published signals keep the template's names, so the starter plots (`plots/*.yaml`) and the tools'
  consumers keep working: mass registers `mass.mass` (kg); aero binds the write-seam inputs
  `controls.elevator`, `controls.aileron`, `controls.rudder` (rad); the engine binds `controls.throttle`
  ("1"). No plugin registers any other signal.
- No F-16 plugin reads a config key: every number is P1's. A config block is `- plugin: <path>` only,
  and an extra key fails as an unconsumed key.
- Test suites are named `F16Mass`, `F16Aero`, `F16Engine`, `F16Tables`, `F16NoAlloc`, so
  `ctest -R F16` (case-sensitive; the prefix is `aircraft_f16_tests.`) selects them, and `-R NoAlloc`
  selects `F16NoAlloc`.

## The generator (Q14) and the fixture reader (lesson 2026-09-25)

**`aircraft/f16/tools/generate_f16_tables.py`** (stdlib only, `xml.etree`), committed before its first run.
- `--refs` defaults to the main checkout's `docs/refs/Atmospheric_models/F16_package`, resolved with
  `git rev-parse --git-common-dir` exactly as `generate_nesc_band.py:348-361`; `--out DIR` redirects the
  outputs (default: their committed paths).
- Before parsing, it checks each file's SHA-256 against addendum §1.1 (`F16_aero.dml` `82796577…`,
  `F16_prop.dml` `e5fd7772…`) and refuses a mismatch.
- It reads the four aero breakpoint sets (`:952-970`) and 18 `griddedTableDef`s, and the prop's two
  breakpoint sets and three tables (`:228-327`). It checks each table's count equals the product of its
  breakpoints and the totals are 744 and 108, and that every `independentVarRef` is
  `extrapolate="neither"` with min and max at its breakpoints' ends (else it refuses).
- It emits each value's source text verbatim (validated as a decimal literal), never a reformatted float,
  as nested `inline constexpr std::array` in the slowest-first order the file states ("last breakpoint
  changes most rapidly"). The header carries both files' full SHA-256, the source line ranges and each
  gtID, inside `NOLINTBEGIN/END(readability-magic-numbers)` with a reason (the `geodesy.hpp:13` precedent).
  Output is deterministic (no time stamp).

**`aircraft/f16/tests/extract_f16_checkdata.py`**, a separate reader. It imports nothing from the
generator, checks the SHA-256s on its own, and writes `aircraft/f16/tests/f16_checkdata.hpp`:
- the 16 aero `staticShot`s: name, 9 inputs, 9 outputs with each output's `tol`, and every
  `internalValues` entry;
- the 9 prop shots: 3 inputs, 6 outputs with their own `tol` (1e-5; FEX 0.001 at `:822`, 0.0006 at `:894`),
  and their `internalValues` where present;
- `F16_inertia.dml`'s seven mass constants (`:56-135`, SHA-256 `e11d4eba…`), so the mass test's expected
  values do not come from the plugin's own literals.

**`tests/python/test_f16_generated.py`**: runs both scripts as subprocesses with `python3 -I -B` into
`tmp_path` with the default `--refs`, and compares every output byte for byte with the committed file. It
snapshots `aircraft/f16/tools` and `aircraft/f16/tests` (every file, ignored ones included) before and
after, and asserts nothing appeared, so no `__pycache__` lands in a tree that `just new-aircraft` or the
copy tests might carry (lessons 2026-10-01, 2026-10-07). It skips, naming the missing path, only when
the main checkout's `docs/refs` lacks P1. A second case checks that the generator refuses a copy of
`F16_aero.dml` with one byte changed (the SHA gate).

## Mass (R1, D1): `physical/mass`

- `mass = slugs_to_kilograms(637.1595)` (`F16_inertia.dml:111-117`); Ixx 9496, Iyy 55814, Izz 63100,
  Izx 982, Ixy = Iyz = 0 (`:56-135`), each through `slug_square_feet_to_kilogram_square_meters`.
- Products of inertia in the S&L convention PR 4 made uniform: the tensor is
  `[[Ixx, 0, −Izx], [0, Iyy, 0], [−Izx, 0, Izz]]`, so its xz entry is negative. The LLT positive-definite
  check stays. Signal `mass.mass` (kg), as the template.
- `F16Mass` tests:
  - each SI value equals the fixture's DML constant through the same named function, bitwise;
  - each also equals an independently written SI literal (exact rational arithmetic, offline, 17
    digits) to 1e-12 relative;
  - `inertia_b(0,2) < 0` and equals `−slug_square_feet_to_kilogram_square_meters(982)`; the tensor is
    symmetric; the xy and yz entries are +0.0;
  - Table 7's 637.26 slug (Vol II p.15) is computed through the same function and reported, not
    asserted (D1): `F16Mass.ReportsTableSevenBesideTheDml` prints one line (both masses in slug and kg,
    their difference in kg, and its weight in N at 9.80665 m/s²). `ctest --preset asan -R F16Mass -V`
    shows it, and the PR body quotes it (critique F-5; `RecordProperty` reaches only XML output, which
    the template's registration does not request).

## Aero (R2, Q14, Q15, D2 to D6): `physical/aero`

**The port, an SI realization** (`aero_math.hpp`, pure, `noexcept`, no allocation; the Q26 pattern,
critique F-1). Every quantity inside is SI: angles in rad, speeds in m/s, lengths in m. Each DML constant
with a unit is written as the DML's value times a named `core/util/units` conversion, never as a
pre-multiplied number. The generated header keeps the source text verbatim (deg, lbf, ft); the math header
derives its SI breakpoint arrays from it at compile time (`degrees_to_radians`, `feet_to_meters` are
`constexpr`). Table values are coefficients and need no conversion.
`Inputs{V, alpha, beta, p, q, r, el, ail, rdr}` (m/s, rad, rad/s; `ail` already in the DML's +LWD sense)
→ `BuildUp`, a struct with one field per `internalValues` varID (SI where dimensional), computed as
addendum §2:
- `rtd = 180/3.14159265` stays the DML's own named constant (with `kDmlPi = 3.14159265`), applied as the
  DML applies it: cz1's `(beta/rtd)` is `radians_to_degrees(beta)/rtd`, so the truncated π's effect is
  kept (addendum §8's Q26 rule);
- `del = el/(25°)`, `dail = ail/(20°)`, `drdr = rdr/(30°)`, each divisor `degrees_to_radians(25.0)` etc.;
- `cy0 = −0.02/° · beta + 0.021·dail + 0.086·drdr`, the per-degree slope written
  `-0.02 / degrees_to_radians(1.0)`; `cz1 = czt·(1 − (beta/rtd)²) − 0.19·del`;
- `tvt = 2·vt`, `b2v = b/tvt`, `cq2v = c̄·q/tvt`, with b, c̄ in m and vt in m/s (b2v in s, cq2v
  dimensionless, the same numbers as the DML's);
- the Cl0 and Cn0 tables read at `absbeta = |beta|`; `clt = −absCl0` when `beta < 0`, else `absCl0`,
  and `cnt` likewise with `absCn0` (`:607-663`; β = −0.0 takes the `otherwise` branch, as `lt` does);
- `cl1 = clt + dclda·dail + dcldr·drdr`, `cn1 = cnt + dcnda·dail + dcndr·drdr`;
- the six outputs as §2, with the nine damping derivatives on `cq2v` (cxq, czq, cmq) and `b2v` (cyp,
  cyr, clp, clr, cnp, cnr).
Every numeric constant is a named `constexpr` with its DML line. Only table lookups clip: `del`, `dail`,
`drdr` and β enter their linear terms unclipped, as the DML says of `del` (`:392`).

**Interpolation** (`common/f16_interp.hpp`): linear between breakpoints; outside, the input is held at
the nearest end, i.e. "neither" read as the Simulink realization's clip (`F16_aero_create.m:94-123`:
`PreLookup` 'Linear' feeding `Interpolation_n-D` 'clip'). 2-D lookups interpolate on both axes after the
clip. The breakpoints are the SI arrays (rad, m); BETA1 is read at |β|, BETA2 at the signed β. No
allocation; breakpoints searched linearly over `std::array`.

**The plugin boundary** (`aero.cpp`, `wrench_b`), conversions once, here:
- `v_rel_b = air_data::relative_velocity_b(vs, env)`; `V = true_airspeed`; α, β from `air_data` (rad);
- `vt = max(V, feet_to_meters(0.1))`: the DML's `minValue="0.1"` ft/s (`:292`), which the Simulink
  realizes as a `Saturate` (`F16_aero_create.m:92`). The floor feeds only `tvt`; q̄ below uses the true V,
  so at V = 0 the wrench is exactly zero and finite;
- `(p, q, r) = air_data::omega_eb_b(vs, env)` (ADR-0032), never `vs.omega_b()`; an unset
  `env.omega_ie_b` gives a NaN wrench;
- controls from the write seam in ICD v1's convention (`icd/astra_icd.h:81-87`), all rad: `el = elevator`
  (+TED both), `ail = −aileron` (ICD +RWD to DML +LWD, D2, the one sign flip), `rdr = rudder` (+TEL both);
- dimensionalized: `q̄ = air_data::dynamic_pressure(ρ, V)`, `S = 300 ft²`, `b = 30 ft`, `c̄ = 11.32 ft`
  through `feet_to_meters`; force_b = q̄S·(cx, cy, cz) (body axes: +fwd, +right, +down); moment about the
  MRC = q̄S·(b·cl, c̄·cm, b·cn);
- **the MRC-to-CM transfer (D4)**: `d = 0.01·cbar·(35 − 25)` ft = 1.132 ft (`F16_inertia.dml:141-162`, CM at
  25 % MAC per README `:1234`), `feet_to_meters(d)`; with the MRC at `(−d, 0, 0)` from the CM,
  `M_cm = M_mrc + r × F`, i.e. `M_cm = M_mrc + d·Fz`, `N_cm = N_mrc − d·Fy`, L unchanged;
- a non-finite input gives the ADR-0018 NaN sentinel, as the template;
- `kMachLimit = 1.0` (D6) is a named constant in `aero_math.hpp` with a comment pointing to PR 13; nothing
  here declares or reads it.

**The checkData comparison, pre-registered.** For each of the 16 shots, `F16Aero.CheckData` builds
`v_b = V·(cos α cos β, sin β, sin α cos β)` with `V = feet_to_meters(300)`, identity attitude, no wind,
`ω_b = (p, q, r)`, `env.omega_ie_b = 0`, ρ = 1.225 kg/m³, and writes the controls through the write seam
in ICD convention. Then:
- **where the transfer sits:** the test undoes it with its own `d` (the literal 1.132 ft, never the
  plugin's constant), using the plugin's returned forces: `M_mrc = M_cm − d·Fz`, `N_mrc = N_cm + d·Fy`;
- **coefficient space is the criterion:** `cx = Fx/(q̄S)`, …, `cm = M_mrc/(q̄Sc̄)`, `cn = N_mrc/(q̄Sb)`,
  with q̄, S, b, c̄ from the test's own expressions, each passing `|y − y_good| < tol` (strict, absolute,
  1e-6, the DML's criterion, `F16_aero_verify.m:686`); the three reference lengths too;
- **the equivalent wrench tolerance** at vt = 300 ft/s and ρ = 1.225: q̄ = 5121.28 Pa, S = 27.870912 m²,
  so forces 1e-6·q̄S = 0.1427 N; L and N about the MRC 1.305 N·m; M about the MRC 0.4925 N·m;
- a wrong transfer distance shows: an error δd moves the recovered cm by δd·cz/c̄, above 1e-6 for
  δd ≥ 1e-4 ft at these shots' |cz| ≈ 0.4 to 1.

**Intermediates.** `F16Aero.InternalValues` evaluates `build_up` on each shot's inputs (converted to SI
by the test through `core/util/units`) and compares every `internalValues` varID with its `BuildUp` field
to `1e-12·max(1, |v|)`: dimensionless ones directly, dimensional ones (vt, alpha, beta, el, ail, rdr,
tvt, absbeta) after the test converts the fixture's value to SI. If any intermediate misses that but the
outputs pass, the builder stops and brings it to Joe (no loosening). `rtd` is pinned separately: equal to
`180.0/3.14159265` bitwise, within 1e-12 of the shots' `57.29577957855229`, and the negative control
`180/π` misses it by 6.5e-8.

**A DML-units reference (critique F-1).** `aircraft/f16/tests/dml_reference.hpp` transcribes §2 again in
the DML's own units (deg, ft/s), with its own few-line clip interpolation over the generated tables'
verbatim degree breakpoints, sharing no code with `aero_math.hpp` or `f16_interp.hpp`.
`F16Aero.SiRealizationMatchesTheDmlUnitsReference` draws 10,000 seeded inputs (`std::mt19937_64` with the
in-house uniform transform; seed in the test) over α −30° to 70°, β −45° to 45°, el, ail, rdr −40° to 40°,
p, q, r −2 to 2 rad/s, and V from 0 to 300 m/s with a block of speeds below 1 ft/s, so every clip edge,
every unclipped linear term and the floor are reached. The six coefficients agree to `1e-12·max(1, |c|)`;
a miss goes to Joe.

**The other aero tests.**
- `ControlSigns` (R2, C-05): through the write seam at α = 5°, positive ICD aileron gives L > 0; positive
  elevator gives M < 0 relative to zero elevator; positive rudder gives N < 0 relative to zero rudder.
  The "Positive aileron" anchor (`:3291`): ICD aileron = −24.1° (DML +24.1°) reproduces cl = −0.06266,
  and +24.1° ICD gives a positive rolling moment.
- `TableEdgesClip` (Q15): for every one of the 18 tables, each input past each edge (α −20° and 60°,
  |β| 40°, β ±40°, el ±30°) gives the table's value at the edge, bitwise; plus hand-transcribed corner
  entries (four per 2-D table, two per 1-D table, each with its DML line) pin the layout independently of
  the emitter.
- `OnlyTheLookupsClip` (critique F-2), through `wrench_b`, β = 0 and rates zero unless stated, expected
  values written by hand in the test:
  - el +30° against +24°: cx and cm equal (the DE1 tables hold), and cz differs by exactly
    `−0.19·(30 − 24)/25 = −0.0456` (del is unclipped), to 1e-12;
  - β 40° against 30°: cl and cn's table parts hold (BETA1 on |β| and BETA2), cy differs by
    `−0.02·10 = −0.2` (cy0 is unclipped), and cz by `czt·((30/rtd)² − (40/rtd)²)` with czt from the
    α-shot's internal value;
  - rudder 45° (drdr = 1.5, past the normalization's ±30°): cy, cl, cn move by `0.086·1.5`,
    `dcldr·1.5`, `dcndr·1.5` against rudder 0, with the derivatives from a shot's internal values at
    the same α and β.
  A plugin that clips el, β or rudder before the build-up fails each by more than 1e-2.
- `TheSpeedFloorIsPointOneFeetPerSecond` (critique F-3): at α = 5°, β = 0, zero controls (clp = −0.42
  from the "Positive aileron" shot's internal values), the test asserts the difference between p = 1 and
  p = 0 rad/s, so it rests on no premise about cl1 (which is 0 there only because Cl0's β = 0 row is all
  zeros, `F16_aero.dml:1101`; lesson 2026-10-09). That difference in rolling moment at
  V = feet_to_meters(0.05) equals `q̄(V)·S·b·clp·b/(2·feet_to_meters(0.1))`, and at
  V = feet_to_meters(0.2) equals `q̄(V)·S·b·clp·b/(2V)`, each to 1e-12 relative, q̄ from the true V. A floor of 0.1 m/s (0.328 ft/s)
  fails the 0.2 ft/s case by a factor 1.64; a floored V in q̄ fails the 0.05 ft/s case by a factor 4.
- `DampsTheEarthRelativeRate` and `AnUnsetEarthRateGivesANonFiniteWrench`: the template's two ADR-0032
  tests, on F-16 data. `ZeroAirspeedIsFiniteAndZero`: V = 0 gives an exactly zero, finite wrench.

## Propulsion (R3, D5, D8): `hardware/propulsion/engine`

- `engine_math.hpp`, SI inside: T_IDLE, T_MIL, T_MAX (MACH_PTS × ALT_PTS, 36 each) are generated verbatim
  in lbf and ft, and the math header derives constexpr SI arrays (`pound_force_to_newtons`,
  `feet_to_meters`, from PR 3c and the existing header). The thrust per the PWR build-up with
  `MIL_PWR = 50` (`F16_prop.dml:72-76,107-177`); lookups clip at Mach 0 to 1.0 and 0 to 50,000 ft (in m).
  No state, no moments, no gyroscopic term (D5). PWR is not saturated (decision 4).
- `engine.cpp`, the one place the throttle maps: `PWR = 100·throttle` (write seam `controls.throttle`, "1").
  Altitude is `env.altitude_m` (PR 3c), compared with the SI breakpoints directly;
  `Mach = air_data::mach(V, env.atmosphere.speed_of_sound_m_s)`; the engine declares the atmosphere
  requirement. Thrust along +x through the reference point; moment zero (D4). A NaN altitude or speed of
  sound gives the NaN sentinel, never sea-level thrust.
- The altitude is `EarthModel::altitude`'s height (the ellipsoid under `wgs84_rotating`); the DML's
  "geometric altitude above reference geoid" (README `:1161`) is PR 9's question (D8).
- `F16Engine` tests: the 9 shots through `wrench_b` (`throttle = PWR/100`, `env.altitude_m =
  feet_to_meters(ALT)`, `V = RMACH·a`), `newtons_to_pound_force(Fx)` against FEX at each shot's own tol,
  and the other five outputs zero; the map pinned at throttle 0, 0.5 and 1 at (Mach 0, 0 ft) against
  hand-transcribed T_IDLE, T_MIL and T_MAX entries with their DML lines; an unset `altitude_m` gives a
  NaN wrench.
- `F16Engine.EveryEdgeClips` (critique F-4): for each of the three tables, through the math seam at
  PWR 0, 50 and 100, each axis past both ends (Mach −0.5 and 2.0; −1,000 ft and 60,000 ft) equals the
  edge value bitwise; through `wrench_b`, −1,000 ft equals 0 ft and Mach 2.0 at 30,000 ft equals
  Mach 1.0 there (case 12's clip), bitwise.
- `F16Engine.ThrottleOutsideZeroToOneExtrapolates` (critique F-4): throttle −0.1 and 1.2 at (Mach 0,
  0 ft) give `T_IDLE + (−10)·(T_MIL − T_IDLE)/50` and `T_MIL + 70·(T_MAX − T_MIL)/50` from the
  hand-transcribed entries, to 1e-12 relative; a clamp to 0..1 fails both.

## Per-frame rules and NoAlloc

`wrench_b` allocates nothing, throws nothing, uses no `std::string`; the tables are constexpr. SI inside
(ADR-0002, ADR-0030): the DML's units appear only as the generated headers' verbatim source text and in
the test-side reference, and every conversion is a named `core/util/units` function. No new signal
carries a unit suffix.

`aircraft/f16/tests/no_alloc_test.cpp` is its own executable, linked with
`tests/support/alloc_counter.cpp` (the `tests/embedded` precedent) and registered with
`astra_add_plugin_test`:
- `F16NoAlloc.WrenchPathAllocatesNothing` (aero commit; engine added in the propulsion commit): loaded
  plugins, `wrench_b` over a sweep that includes every clip edge and V = 0, zero allocations;
- `F16NoAlloc.OneThousandFramesAllocateNothing` (propulsion commit): a `Simulation` from an inline test
  config (earth/flat, gravity/constant, atmosphere/isa1976, the three F-16 plugins, 3,000 m, 150 m/s),
  1000 frames, no halt, frame 1000, zero allocations (the `no_alloc_test.cpp:63-80` pattern).

## Blast radius (pre-registered)

Everything new is under `aircraft/f16` and its tests, plus `tests/python/test_f16_generated.py`. The
template, the notional transport, `plugins/`, `core/` and the Python package are untouched by this PR;
no `kCoreApiVersion` bump here (PR 3c's). Nothing under `tests/golden/`, in any tolerance file or under
`docs/decisions/`. M2's NESC cases 1 to 10, M3's frozen regression, the R1 golden and `-k new_aircraft`
pass unchanged (the full gate runs all of them). `test_new_aircraft_build.py` copies `aircraft/` whole,
so its static case also compiles the F-16 plugins under `debug`.

## Commits (each pushed; the draft PR opens at the first)

0. After approval: `git fetch`, `git worktree add` `m7-pr6b` from `origin/main` (planning ran in plan mode
   in the main checkout, which allows no worktree), and this draft moved there and renamed. Commit 0 is
   the renamed plan only; it cites PR 3c's M7 plan amendment (decision 3).
1. `just new-aircraft f16`, verbatim (its tests pass as the template's do), so the later diffs read as
   replacements.
2. The generator and the reader, before their first run.
3. Their outputs (`aero_tables.hpp`, `engine_tables.hpp`, `f16_checkdata.hpp`) and
   `test_f16_generated.py`.
4. Aero: `common/f16_interp.hpp`, the plugin, `aero_test.cpp`, `F16NoAlloc`'s aero half;
   `configs/cruise.yaml` removed.
   — merge `origin/main` once PR 3c has merged —
5. Mass and `mass_test.cpp`.
6. Propulsion: `engine/` and `engine_test.cpp`; `turbofan/` and `turbofan_test.cpp` removed; the
   1000-frame NoAlloc case.
7. Gate evidence: `docs/reviews/evidence/<head>-gate-summary.log`.

If PR 3c has not merged when commit 4 is done, the branch stops there and `/wrap` records it as blocked
on PR 3c.

## Acceptance and checks

| Req | Check |
|---|---|
| R1 | `F16Mass.*` |
| R2 | `F16Aero.*`, `F16Tables.*` (if split out of `F16Aero`) |
| R3 | `F16Engine.*` |
| Tables and fixtures | `test_f16_generated.py` (skips only without P1) |
| Per-frame | `F16NoAlloc.*` |

Commands, results recorded in the PR body:
- `ctest --preset asan -R F16 --no-tests=error`, with `ctest --preset asan -N -R F16` counts split by
  executable: the selection also takes `NescF16Band`'s tests (four `TEST` macros,
  `tests/physics/nesc/nesc_f16_bands_test.cpp:200,221,236,360`, `physics.` prefix), so the F-16's own count
  (`aircraft_f16_tests.` prefix) is read on its own;
- `ctest --preset asan -R NoAlloc --no-tests=error`;
- `ctest --preset asan -R F16Mass -V` for the D1 report line;
- `.venv/bin/pytest tests/python -k new_aircraft`; `.venv/bin/pytest tests/python -k f16_generated`;
- `./scripts/check-full.sh` on the final candidate (exit 0 and `check: OK`), trimmed summary committed;
- `git diff --check origin/main..HEAD`.

Then the Codex adversarial review (at most two correction rounds), then Joe reads the build-up port, the
control conversions, the transfer and the mass test.

## `/wrap`

PLAN.md gains PR 6b's line. STATE.md, as main reads it after the merge: PR 7 and PR 14 become eligible;
PR 9 waits on PR 7; PR 13 depends on PR 6b and PR 9; PR 7's thread names `altitude_m` beside
`omega_ie_b`. PR 2 merged first (#112, `856fff0`) and was merged into this branch before commit 1
(`233b506`); `origin/main` is merged again before `/wrap`, and STATE.md is written for both PRs merged.

## Plan critique (one fresh, read-only Codex pass, 2026-10-10)

Run: companion 1.0.6, `task --fresh --effort high --cwd <main checkout>` (read-only, no `--write`), job
`task-mv2udsm5-fd6yrr`, thread `01a1277c-6a1e-77b2-a991-d8d45d6889d1`, exit 0, 6 m 42 s, against
`origin/main` = `e4c9c63`. Verdict: **rework**, 5 findings. Each was checked against its cited lines, and
all five are accepted and folded in above:

| ID | Severity | Finding | Disposition |
|---|---|---|---|
| F-1 | high | The draft ran the aero build-up in ft/s and degrees inside `aircraft/`, against ADR-0002 (`0002:13`), Joe's item 8 ("SI inside"), and the M7 plan's Q26 (`m7-f16-nesc-oracle.md:749`), which calls a customary-unit interior an ADR exception | Accepted. The port is an SI realization (Q26's pattern): SI breakpoints derived at compile time from the verbatim generated text, DML constants as value × named conversion, `rtd` kept with its truncated π. A test-side DML-units reference checks it on 10,000 seeded inputs |
| F-2 | medium | Table-edge tests alone cannot see a plugin that clips el, β or rudder before the build-up (`F16_aero.dml:390,479`): +30° el would shift cz by 0.0456 and pass | Accepted: `OnlyTheLookupsClip`, through `wrench_b`, with hand-computed deltas |
| F-3 | medium | A zero-speed test cannot tell a 0.1 ft/s floor from 0.1 m/s, or a floored V in q̄ | Accepted: `TheSpeedFloorIsPointOneFeetPerSecond` at 0.05 and 0.2 ft/s with a nonzero roll rate |
| F-4 | medium | Propulsion tests covered only upper edges and PWR inside 0..100; a throttle clamp or extrapolation below 0 ft would pass (`F16_prop.dml:44,107`) | Accepted: `EveryEdgeClips` (both ends, both axes, all three tables) and `ThrottleOutsideZeroToOneExtrapolates` |
| F-5 | low | `RecordProperty` reaches only gtest's XML output, which nothing requests, so the D1 report would not appear | Accepted: a printed report line, shown by `ctest -R F16Mass -V` and quoted in the PR body |

No finding was rejected. An interim message in the run says its own arithmetic on the sources supports the
build-up, the transfer's signs and the wrench tolerances; its final report raises nothing against them.

## Risks

- PR 3c slips: commits 5 and 6 wait; nothing in 0 to 4 depends on it.
- The 1e-12 tolerances (intermediates, SI against the DML-units reference) are predictions: radian
  breakpoints change interpolation rounding at about 1e-16. A miss goes to Joe, not into a wider number.
- Hand-transcribed corner anchors are a second transcription; each carries its DML line so Joe can read it.
