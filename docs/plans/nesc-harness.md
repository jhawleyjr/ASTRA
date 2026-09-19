# Implementation plan: NESC ten-case check harness (M2 PR 3b)

Spec/brief: `docs/specs/nesc-harness.md` (reopened 2026-09-18, second review pass)
Risk: 3 — `plugins/physical` consumers, `tests/golden/**`, tolerance derivation, new ADR
Status: **approved 2026-09-18; cases 4/5 accepted as a disclosed gap (Joe, option (a))** — spec/plan
approved, ADR-0014 accepted and written, generator/configs/harness built. 8/10 cases pass under asan;
cases 4/5 are `GTEST_SKIP()`ed per Joe's decision (see "Known gap found during implementation" below),
`PLAN.md`'s new PR 3c is the closer. Resuming: remaining three presets, full gate, `/review-cycle`,
human-review packet.
Plan approval: **approved**, 2026-09-18

## Repository findings

**Observed, this session** (file/symbol-cited):

- `tests/physics/nesc_case1_test.cpp` + `tests/configs/nesc_case1_dragless_sphere.yaml` +
  `tests/golden/nesc/case1_dragless_sphere.hpp` are the PR-1-era single-case harness to be absorbed/deleted.
  `tests/physics/CMakeLists.txt` lists `nesc_case1_test.cpp` directly in `astra_physics_tests`'s `SOURCES`
  (no subdirectory globbing) and defines `ASTRA_TEST_CONFIG_DIR="${ASTRA_SOURCE_DIR}/tests/configs"` /
  `ASTRA_TEST_PLUGIN_DIR` as preprocessor macros; `target_include_directories(... PRIVATE
  "${ASTRA_SOURCE_DIR}/tests")` is what lets `nesc_case1_test.cpp` `#include
  "golden/nesc/case1_dragless_sphere.hpp"`.
- `tests/golden/nesc/` and `tests/golden/wind/` are header-only, guard-protected, **not** built as CMake
  targets — no `tests/golden/CMakeLists.txt` exists. Golden headers are `#include`d directly by the tests
  that use them.
- `tests/golden/atmosphere/generate_isa1976_table.py` is the load-bearing precedent for
  `generate_nesc_band.py`'s shape: pure stdlib Python (`math`, `dataclasses`), no ASTRA imports, every
  constant cited to a document page, an internal cross-check that `raise SystemExit`s on its own detected
  inconsistency before ever emitting output, and a rendered `.hpp` with a long provenance comment followed
  by `constexpr` array data — this task's generator should follow the same shape, not invent a new one.
- ADR-0013 (`docs/decisions/0013-nesc-check-case-oracle.md`) is accepted and already pins: the band formula,
  `k = 3`, no interpolation, the generator's human-owned/never-run-by-`just-check` status, and the ten-case
  scope. It leaves the resolution-floor question explicitly open (implied by its own "may be tightened...
  never widened" language having no floor mechanism) — that gap is what `PLAN.md`'s M2 PR 3b line calls
  "ADR-0014".
- `docs/refs/Atmospheric_checkcases/` (main checkout, gitignored, verified present this session: 81 files
  across 21 `Atmos_NN_*` folders, `Atmos_01` through `Atmos_10` matching M2's ten check cases plus
  `Atmos_11` onward which are out of scope). Folder names identify each case's scenario precisely:
  `Atmos_01_DroppedSphere`, `_02_TumblingBrickNoDamping`, `_03_TumblingBrickDamping`,
  `_04_DroppedSphereRoundNonRotation`, `_05_DroppedSphereRoundRotation`,
  `_06_DroppedSphereEllipsoidalNoWind`, `_07_DroppedSphereSteadyWind`, `_08_DroppedSphere2DWindShear`,
  `_09_EastwardCannonball`, `_10_NorthwardCannonball`. Each `Atmos_NN_sim_MM.csv` file's header row was
  read this session (`Atmos_01_sim_01.csv` through `_04.csv`): confirms `airDensity_slug_ft3` /
  `ambientPressure_lbf_ft2` / `ambientTemperature_dgR` / `speedOfSound_ft_s` are published even for case 1
  (the dragless case), and confirms per-file column-count variation (27–32 columns across the four files
  read) matching ADR-0013's own "22/27/31/32/37/38 across the ten cases' 54 files" claim. **Corrected, Joe's
  second review**: `feVelocity_ft_s_*` is NED (Earth-relative, local-level), not body-axis — this plan's
  earlier draft misread the "fe" prefix and ADR-0003's "velocity relative to the Earth-fixed frame" framing
  (about the Earth-relative-vs-inertial axis, not the NED-vs-body one) as meaning body axes. Joe's evidence:
  case 9's `feVelocity` Y = +1000 ft/s at t=0 with yaw 90° (east-pointing) matches NED east directly, not
  body y/right/south as a body-axis reading would require; case 2's `feVelocity` stays ≈(0, 0.23, 320) ft/s
  through a large attitude swing (Euler ≈ (−4.3°, 3.7°, −66°) at t≈10 s) without rotating with the body,
  which only a NED-frame vector would do. The comparison therefore needs `frames::rotate_b_to_n(q_nb, v_b)`
  (`core/util/frames.hpp`, already present) on ASTRA's side, not a bare unit conversion.
- **`core/include/astra/kernel/vehicle.hpp`'s `Vehicle::environment()`** (`const EnvironmentSample&`,
  public, already exists) exposes the frame's latched `wind_n` and `atmosphere` (`AtmoState`: temperature_k,
  pressure_pa, density_kg_m3, speed_of_sound_m_s) with **no plugin changes needed** — this is the key finding
  that keeps this task's non-goal ("no changes to existing plugins beyond what the harness needs") trivially
  satisfied: the harness needs no plugin changes at all, atmosphere/wind are already test-visible through a
  public accessor `nesc_case1_test.cpp`'s own precedent (`sim.vehicles().front().state()`) already shows how
  to reach.
- `tests/plugins/nesc_aero/nesc_aero.cpp` (PR 3a, already merged) registers `nesc_aero.force_b_{x,y,z}` /
  `nesc_aero.moment_b_{l,m,n}` (N, N·m) whenever it is loaded; its `requires_environment()` unconditionally
  asks for atmosphere and config-conditionally for wind. A case with zero aero reaction (e.g. case 1, per
  its own PR-1-era golden comment "the sphere has no reaction with the atmosphere at all") is not expected
  to load this plugin at all — its trajectory not reflecting drag already proves the zero force, so no force/
  moment channel is compared for that case, not a zero compared against a trivial band.
- `plugins/physical/wind/table/table.cpp` already registers `wind.{north,east,down,altitude}` (m/s, m) —
  confirmed unnecessary to touch for wind visibility either.
- `plugins/physical/earth/round/round.cpp` has `rotating: <bool>` (default false, `config.boolean(
  "rotating", false)`, line 84) — already merged (PR 3a), needed for cases 4/5's pairing.
- `plugins/physical/gravity/inverse_square/inverse_square.cpp` reads `g0`/`radius` with defaults
  `kStandardGravity`/`kDefaultRadius` (6371000 m, not Table 73's 6371007.1809 m) — confirms `PLAN.md`'s own
  "Config gotchas" note that cases 4/5 need explicit `g0`/`radius` overrides, not the plugin's defaults.
- `core/util/frames.hpp::euler_from_quaternion` exists and is the conversion the harness needs for Euler
  comparison (no state-vector Euler angle exists — ADR-0001: Euler is a derived output, never state).
- `core/include/astra/kernel/state.hpp` gives the exact `StateVector` layout/indices `nesc_case1_test.cpp`
  already uses (`state::kPosition/kVelocity/kQuaternion/kAngularRate`); this harness reuses those, not new
  ones.

**Assumption-vs-repository mismatches found**: none material. The spec's assumptions (generator
sufficiency, no plugin changes needed, ADR-0012 prerequisite already merged) all check out against the
current tree.

### Vol II Tables 25–34 / 6.3-1 cross-check (Joe's spec-review requirement, done this session)

Read directly from the main checkout's `docs/refs/NASA-TM-2015-218675-Vol-{I,II}.pdf` this session — Vol I
Table 6.3-1 (doc p.15–16) for the scenario/model matrix, Vol II §C.1.1–C.1.10 and Tables 25–34 (doc
p.56–62) for per-case initial conditions, §B.1.1/B.1.3 and Tables 1/2/4/5 (doc p.12–13) for vehicle mass/
aero constants, §B.4.2 and Table 73 (doc p.44, p.93) for gravitation constants. This **corrects** one
assumption this plan's earlier draft carried from folder names/`PLAN.md` prose alone: cases 9 and 10 were
assumed dragless (like case 1) by analogy to their "ballistic"/"cannonball" naming; Table 6.3-1 and Tables
33/34 both say "Vehicle: Sphere with constant `C_D`" for cases 9 and 10, the same nonzero `C_D` as cases
4–8 — only cases 1 (dragless sphere) and 2 (dragless, un-damped brick) have zero aero reaction. This is
exactly the kind of error a folder-name-only inference could have carried silently into a case 9/10 config
that could never match its band; it is why Joe's instruction to do this cross-check at plan time, not
implementation time, was correct.

**Vehicle constants** (Vol II Tables 1, 2, 4, 5 — doc p.12–13; unit conversions below use exact factors
from Vol II Table 73, doc p.93: 1 slug = 1/0.06852176585678 kg, 1 slug-ft² = that × 0.3048² kg·m², 1 ft² =
0.3048² m²):

| Vehicle | `m` | Inertia (`Ixx,Iyy,Izz`; off-diag 0) | `S` | `b` | `cbar` | `C_D` | `Cl_p,Cm_q,Cn_r` |
|---|---|---|---|---|---|---|---|
| Sphere (Table 1/2) | 1.0 slug (14.5939 kg) | 3.6, 3.6, 3.6 slug-ft² (**4.880945 kg·m² each**) | 0.1963495 ft² (0.018241 m²) | — | — | 0.1 | 0 (sphere has no damping terms) |
| Brick (Table 4/5) | 0.155404754 slug (2.26800 kg) | 0.001894220, 0.006211019, 0.007194665 slug-ft² (**2.568217e-3, 8.421011e-3, 9.754656e-3 kg·m²**) | 0.22222 ft² (0.020645 m²) | 0.33333 ft (0.10160 m) | 0.66667 ft (0.20320 m) | 0.01 (zeroed for case 2) | −1.0, −1.0, −1.0 (zeroed for case 2; used as-is for case 3) |

**Corrected, Codex plan critique**: the inertia conversions above were originally computed 10× too small in
every one of the six values (sphere ×3, brick ×3) — a genuine arithmetic slip, not a formula error (the
formula stated, `1 slug-ft² = (1/0.06852176585678) × 0.3048²` kg·m² ≈ 1.355818 kg·m² per slug-ft², is
correct; it was applied wrong). Recomputed by hand this session and cross-checked against Codex's
independent recomputation: sphere `3.6 × 1.355818 = 4.880945` kg·m²; brick `0.001894220 × 1.355818 =
0.002568217`, `0.006211019 × 1.355818 = 0.008421011`, `0.007194665 × 1.355818 = 0.009754656` kg·m² — both
recomputations agree to 6+ significant figures. Mass and area/length conversions in this same table were
independently re-checked and were already correct (only inertia was wrong); Codex's critique confirmed this
by not flagging them. This error would have gone straight into a config and materially changed every
inertia-dependent case's dynamics (2, 3 by ~10×; any sphere case's rotational response) had it not been
caught before implementation — exactly the failure mode R8's per-case CSV/table cross-check exists to catch
for *initial state*, but inertia is a **mass-property config value**, not part of `initial.state`, so R8 as
originally scoped would not have caught this specific error. Generalizing R8's intent: the plan's own
Done-when now also requires every transcribed mass/aero constant (not just initial state) to be
independently recomputed or cross-checked at config-authoring time, not copied from this table on trust —
this table itself is provenance/citation, never the final arithmetic authority (same posture
`generate_isa1976_table.py` already takes toward its own transcribed constants).

**Gravitation constants for cases 4/5's `inverse_square`** (Vol II Table 73, "Atmospheric check-cases"
block, doc p.93): sphere-Earth radius 6371007.1809 m, `GM` = 3.986004418×10¹⁴ m³/s². `inverse_square.cpp`
reads `g0`/`radius` (surface acceleration + radius), not `GM` directly, so the config must set
`radius: 6371007.1809` and `g0: 9.820228349842926` (= `GM`/`radius²`, matching `PLAN.md`'s own already-
recorded value — confirmed against Table 73's numbers this session, not re-derived independently). Earth
rotation rate (used by `earth/round`'s `rotating: true` for case 5, and implicitly correct already for
`wgs84_rotating`): 7.292115×10⁻⁵ rad/s.

**Rate column, corrected (Joe's second plan review)**: Vol II's printed "Rate" is relative to the rotating
Earth, not inertial — already settled for case 1 specifically by PR 1
(`tests/golden/nesc/case1_dragless_sphere.hpp` lines 10–15: the printed −0.004178073 deg/s roll rate is
"not this scenario's `omega_b`," which is exactly zero). Generalized here to all ten cases: every case sits
at latitude 0, so `omega_b = omega_earth_relative_printed + frames::rotate_n_to_b(q_nb_initial, [omega_ie, 0,
0])` where `omega_ie = 7.292115e-5` rad/s (Table 73) — **not** the printed table value transcribed directly
except for case 4, where `round`'s `rotating: false` makes `omega_ie` identically zero for that case alone
(Table 28 prints `[10,20,30]` with no Earth-relative bias baked in, consistent with this). The table below
lists both the printed value (for provenance/transcription-checking against the cited table) and the actual
`omega_b` to configure. Confirmed against every case's own t=0 `bodyAngularRateWrtEi_deg_s_*` CSV row this
session (Joe): case 1 → (0,0,0); cases 2, 3, 5 → (10,20,30); case 9 → (0,−0.004178,0); case 10 →
(+0.004178,0,0) — all four match this formula's output exactly (worked by hand this session using
`rotate_n_to_b`'s own quaternion-sandwich formula, not asserted); cases 6, 7, 8 share case 1's identity
attitude, printed rate, and position, so by the same formula (not yet independently CSV-confirmed this
session — R8/the plan's own Done-when item covers finishing this at implementation time) their `omega_b` is
also (0,0,0).

**Per-case table** (Vol I Table 6.3-1 for the model matrix; Vol II Tables 25–34 for initial conditions, all
originals in the document's own units — deg/deg/ft MSL position, ft/s NED velocity (not body-axis — see the
Repository findings correction above), deg roll-pitch-yaw attitude *in that printed order* — ASTRA's
`euler_nb` is yaw-pitch-roll, so the generator/config reverse the printed order, not relabel it — and deg/s
roll-pitch-yaw rate, printed Earth-relative, corrected to inertial `omega_b` per the paragraph above; every
case runs 30 s):

| # | Table | Vehicle / `nesc_aero`? | Earth (`physical.earth`) | Gravity (`physical.gravity`) | Wind | Position (deg,deg,ft) | Velocity NED (ft/s) | Attitude r,p,y (deg) | Rate printed (deg/s) | **`omega_b` to configure (deg/s)** |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 25 | Sphere, `C_D=0` — **no** `nesc_aero` (matches the existing `nesc_case1_dragless_sphere.yaml`, absorbed) | `wgs84_rotating` | `wgs84_j2` | none | [0,0,30000] | [0,0,0] | [0,0,0] | [−0.004178073,0,0] | **[0,0,0]** |
| 2 | 26 | Brick, `C_D=0`, `Cl_p=Cm_q=Cn_r=0` — `nesc_aero` loaded (checkable zero) | `wgs84_rotating` | `wgs84_j2` | none | [0,0,0] | [0,0,0] | [0,0,0] | [9.995821927,20,30] | **[10,20,30]** |
| 3 | 27 | Brick, `C_D=0`, damping nonzero — `nesc_aero` loaded | `wgs84_rotating` | `wgs84_j2` | none | [0,0,0] | [0,0,0] | [0,0,0] | [9.995821927,20,30] | **[10,20,30]** |
| 4 | 28 | Sphere, `C_D=0.1` — `nesc_aero` loaded | `round` (`rotating: false`) | `inverse_square` (`radius: 6371007.1809`, `g0: 9.820228349842926`) | none | [0,0,30000] | [0,0,0] | [0,0,0] | [10,20,30] | **[10,20,30]** (no correction — `omega_ie=0` for this Earth model) |
| 5 | 29 | Sphere, `C_D=0.1` — `nesc_aero` loaded | `round` (`rotating: true`) | `inverse_square` (same params as case 4) | none | [0,0,30000] | [0,0,0] | [0,0,0] | [9.995821927,20,30] | **[10,20,30]** |
| 6 | 30 | Sphere, `C_D=0.1` — `nesc_aero` loaded | `wgs84_rotating` | `wgs84_j2` | none | [0,0,30000] | [0,0,0] | [0,0,0] | [−0.004178073,0,0] | **[0,0,0]** |
| 7 | 31 | Sphere, `C_D=0.1` — `nesc_aero` loaded | `wgs84_rotating` | `wgs84_j2` | `wind/uniform`, 20 ft/s (6.096 m/s) from due west → `wind_n = [0, +6.096, 0]` (blowing east) | [0,0,30000] | [0,0,0] | [0,0,0] | [−0.004178073,0,0] | **[0,0,0]** |
| 8 | 32 | Sphere, `C_D=0.1` — `nesc_aero` loaded | `wgs84_rotating` | `wgs84_j2` | `wind/table`, `V_wind(h) = (0.003·h[ft] − 20)` ft/s from west; nodes at `h=0` (−20 ft/s, i.e. 6.096 m/s from east) and `h=40000` ft (per `PLAN.md`'s settled choice — formula value there is 100 ft/s from west, 30.48 m/s, even though the case never flies that high; keeps the whole ~16,293–30,000 ft flown range strictly interior) | [0,0,30000] | [0,0,0] | [0,0,0] | [−0.004178073,0,0] | **[0,0,0]** |
| 9 | 33 | Sphere, `C_D=0.1` — `nesc_aero` loaded (**not** dragless — corrects this plan's earlier assumption) | `wgs84_rotating` | `wgs84_j2` | none | [0,0,0] | [0,1000,−1000] NED (east+up, √2000 ft/s at 45° from vertical) | [0,0,90] | [0,0,0] | **[0,−0.004178073,0]** |
| 10 | 34 | Sphere, `C_D=0.1` — `nesc_aero` loaded (**not** dragless — same correction) | `wgs84_rotating` | `wgs84_j2` | none | [0,0,0] | [1000,0,−1000] NED (north+up, √2000 ft/s at 45° from vertical) | [0,0,0] | [0,0,0] | **[0.004178073,0,0]** |

`physical.atmosphere: isa1976` is configured in every case (confirmed this session: `airDensity_slug_ft3`/
`ambientPressure_lbf_ft2`/`ambientTemperature_dgR` are published in every `Atmos_NN_sim_MM.csv` read,
including case 1's). `physical.mass` uses the sphere or brick constants table above per case. This table is
Claude's own transcription checked against the cited pages this session, not a second independent
verification — the generator/config-writing step re-reads the same pages when it transcribes exact
floating-point values (mirroring `generate_isa1976_table.py`'s own "read directly off the document, not
from memory" discipline), so a transcription slip here is caught again at implementation time. **This is
exactly why R8/the Done-when CSV cross-check exists**: this table already had two material errors (velocity
frame, rate frame) caught only because Joe re-derived them independently from raw CSV rows, not from this
document's own citations — a standing per-case, per-config CSV check at implementation time is the real
guard, not a one-time table like this one.

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance command |
|---|---|---|---|
| First step, before any code (task instruction's own precondition) | **Done, this session**: `ctest -R physics.Nesc.Case1` timed once per preset — asan 5.56 s, debug 1.60 s, static-plugins 3.89 s, release 0.02 s (sum 11.07 s/case). ×10 cases ≈ 110.7 s added across `just check`'s four presets combined (the new harness's extra per-sample state-read/`run(1)`/`run(99)`/registry-`find()` overhead is negligible next to 30,000 raw RK4 steps, so this is not an under-count by more than a few percent) | None | Recorded measurement above, not a code change |
| R6 (ADR-0014 accepted) | New `docs/decisions/0014-nesc-per-channel-tolerance-floor.md` (draft below) | Joe's acceptance; guarded by `guard-oracle.sh`, needs `touch-goldens` | Joe's explicit decision, recorded the way ADR-0012/0013 were |
| R1, R2 (ten-case harness, case 1 absorbed) | New `tests/physics/nesc/case{01..10}_<slug>.yaml` (10 files); new `tests/physics/nesc/nesc_cases_test.cpp`; delete `tests/physics/nesc_case1_test.cpp`, `tests/configs/nesc_case1_dragless_sphere.yaml`, `tests/golden/nesc/case1_dragless_sphere.hpp`; edit `tests/physics/CMakeLists.txt` (source list, a new `ASTRA_TEST_NESC_CASE_DIR` macro alongside the existing `ASTRA_TEST_CONFIG_DIR`/`ASTRA_TEST_PLUGIN_DIR`, per spec Q2's proposed answer) | `Simulation::from_config`, `Vehicle::state()`/`environment()`, `tests/plugins/nesc_aero` — all existing, no interface change | `ctest --preset <preset> -R 'physics.Nesc'` |
| R1 (band data) | New `tests/golden/nesc/generate_nesc_band.py` (human-owned); new `tests/golden/nesc/case{01..10}_<slug>_band.hpp` (10 generated headers, guarded) | Reads `docs/refs/Atmospheric_checkcases/` from the main checkout; writes only under `tests/golden/nesc/`; follows `generate_isa1976_table.py`'s shape | Run by Joe by hand; `clang-format-18 -i` on the output, matching the ISA generator's own documented invocation |
| R1 (geopotential consistency, PLAN.md's own addition) | Edit `tests/golden/atmosphere/generate_isa1976_table.py`: add `abs(geopotential_from_geometric(Z) - transcribed_H) <= half_ulp(0)` cross-check (worst residual already measured at 0.281 m per `PLAN.md`) | None — additive check inside an existing, already-guarded generator | Re-run the ISA generator; existing `astra_atmosphere_tests`-equivalent (whatever consumes `isa1976_table.hpp`) still passes |
| R3 (resolution floor) | Encoded inside `generate_nesc_band.py`'s per-channel tolerance computation (ADR-0014's formula), not a separate mechanism | ADR-0014 accepted first | Generator's own stderr report (mirroring `check_transcribed_row_tolerances`'s pattern) showing every degenerate channel's floor margin |
| R4 (sampling-fix regression coverage) | `nesc_cases_test.cpp`'s own read-state/`run(1)`/read-wrench/`run(99)` loop (spec's Contract section) | `Simulation::run(std::uint64_t)`, existing | Case 3 (damping) and case 7 (drag+wind) passing is the regression signal — no separate unit test proposed |
| R5 (wall-time budget) | No code; a measurement recorded in the human-review packet | — | The "first step" row above |
| R7 (yaw wrap-around, cases 2/4/5) | `generate_nesc_band.py`: yaw's per-sample mean/spread computed on the unwrapped (shortest-signed-angle) difference between each tool and a reference, not raw degrees; **`tol_rel` is never computed/applied for yaw (corrected, Codex plan critique — see below), only `tol_abs` (from the unwrapped spread) plus ADR-0014's floor**; a generator-side assertion that the full effective tolerance (which, with `tol_rel = 0` for yaw, is `tol_abs` alone) never exceeds π rad. `nesc_cases_test.cpp`: yaw comparison wraps `(astra_yaw - band.reference)` into `(-π, π]` before comparing against `tol_abs` (no relative term) | ADR-0013's band formula (elaborated, not reopened) | Generator's own stderr report shows the unwrapped spread at case 2's t=15.3 s and cases 4/5's t=15.0 s/24.6 s, not the raw ~359° one; case 2/4/5 yaw passes at those instants |
| R8 (per-case initial-state cross-check, corrected — Codex plan critique) | Before writing each `tests/physics/nesc/case{NN}_*.yaml`: the **primary authority is Vol II Tables 25–34's own printed initial conditions** (already transcribed into this plan's per-case table above, converted to SI with the corrected constants) — these are exact, analytically-specified values (0, 1000, −1000 ft/s, etc.), not measurements, so the config must match them exactly after SI conversion. The CSV t=0 rows are a **secondary corroboration, across multiple participants, not "any one"**: check `feVelocity_ft_s_*`→m/s (NED) and `bodyAngularRateWrtEi_deg_s_*`→rad/s (inertial `omega_b`) against at least two or three `sim_MM` files per case, allowing small inter-tool floating-point/initialization noise (not exact equality — corrected below) rather than trusting a single file, and skipping a participant missing a required column for that case rather than failing on it | Vol II Tables 25–34 (primary), the CSV files (corroboration) | Ten short per-case checks against the table, plus a multi-participant CSV spot-check, done once at config-authoring time, before any `ctest` run on that case is treated as meaningful evidence |

**R7 corrected, Codex plan critique**: the original design computed `tol_abs` from the unwrapped spread but
said nothing about `tol_rel` for yaw, and ADR-0013's actual pass criterion is `tol_abs + tol_rel*|reference|`,
not `tol_abs` alone. Codex's derivation: near a wrap instant, the small residual mean of the unwrapped
per-tool differences (e.g. ~0.1°) is not the same number as the reconstructed absolute yaw reference
(~180°) — if the generator's relative-spread formula were applied using that small residual as its
denominator, `tol_rel` could come out enormous (Codex's worked example: `tol_rel = 6`, i.e. 600%), passing
a `tol_abs ≤ π` check alone while the *effective* tolerance (`tol_abs + tol_rel*|reference|`) at the
reconstructed ~180° reference exceeds 1,080° and accepts any wrapped value. Fix: yaw carries **no relative
term at all** — always `tol_rel = 0` for the yaw channel, in every case, not just near a wrap instant. This
is simpler and more robust than the alternative (reconstruct an absolute reference correctly and validate
the *combined* tolerance), since it removes the failure mode's entire mechanism rather than patching around
it, and costs nothing: yaw's own generator-side π-rad sanity check on `tol_abs` alone is then sufficient by
construction, exactly as originally intended, because there is no relative term left to blow it up. No
other channel needs this (roll/pitch/latitude never cross a wrap boundary in this data; longitude's spread
stays ≤0.06° everywhere, confirmed by Joe, so it is nowhere near one either).

**R8 corrected, Codex plan critique**: the original R8 said "any one participant" is an authoritative t=0
source, reasoning that every tool starts from the same shared initial condition. Codex found this is not
reliably true in the data itself: in cases 4 and 5, participant SIM 2 publishes a nonzero t=0 north velocity
(0.01 ft/s) where every other participant shows exact zero (almost certainly that one tool's own
initialization artifact, not the intended condition); in case 9, participant SIM 3 does not publish the
`bodyAngularRateWrtEi_*` columns at all — both independently confirmed against the raw CSVs this session
(Repository findings above already covered feVelocity's own frame; this is the same files' initial rows).
An "any one participant" check could therefore either wrongly reject a correct config (if the one file
picked happens to carry a small per-tool initialization perturbation) or silently skip a case it cannot
actually check (if the one file picked is missing the needed column) — neither is the intended safeguard.
Fixed above: Tables 25–34 are the real authority (they are exact, not an ensemble average with its own
noise), and the CSV cross-check is corroboration across several participants with an explicit noise
allowance, not a single file trusted at face value.

One Claude builder by default (no genuinely independent, stable-interface sub-packages here — the
generator, the configs, and the C++ harness are tightly coupled through the band header's own schema, so
splitting them across builders would just relocate a shared-contract negotiation into inter-agent
coordination for no benefit).

## Validation and risks

- **Fast loop**: `./scripts/check-fast.sh` (ASan unit tests) does not touch `physics`-labeled tests; it
  stays green trivially and is not this task's evidence.
- **Full gate**: `./scripts/check-full.sh` (`just check`) is the actual acceptance surface for R1 — must run
  under every preset, per the existing `gtest_discover_tests(... PROPERTIES LABELS physics)` wiring.
- **Independent oracle**: ADR-0013's ensemble band is independent of ASTRA by construction (six other tools'
  trajectories, never fitted to ASTRA's own output — ADR-0013's own "never fitted" clause, reaffirmed by
  this plan, not reopened).
- **Numerical risk**: case 1 is *expected* to be the hardest to match per ADR-0013's own "Consequences"
  section (fixed-step RK4 at 1 kHz vs. variable-step integrators, inter-tool spread at the same order as the
  10 Hz grid's own timing noise) — a case-1 failure at implementation time is not automatically evidence of
  an ASTRA defect; it needs the same diagnosis ADR-0013 already anticipated, not a tolerance widening
  (blocked outright by `guard-oracle.sh`/`AGENTS.md` regardless).
- **Allocation/determinism**: no new requirement; the existing no-alloc/bitwise-determinism tests are
  unaffected (this harness adds no per-frame allocation — `sim.run(1)`/`run(99)` and a registry `find()` are
  the only new per-test-binary calls, all outside the per-frame path's own no-alloc contract, which governs
  `core`/`plugins`, not test binaries).
- **Compatibility/migration**: deleting `nesc_case1_test.cpp` and its golden removes the one place ASTRA was
  judged against an independently-integrated DOP853 reference rather than the NESC ensemble. That
  independent-integration check is not preserved elsewhere by this plan — flagged as a real (if narrow) loss
  of coverage: the DOP853 reference was tighter (1e-8 rad / 1e-9 m / 1e-4 m/s) than case 1's own ensemble
  band is likely to be (ADR-0013's own numbers: case 1's inter-tool spread is ~2e-3 ft ≈ 6e-4 m at t=30,
  i.e. `tol_abs` at k=3 is roughly 1.8e-3 m before the floor — looser than the deleted test's 1e-3 m
  altitude tolerance, tighter than its 1e-4 m/s velocity tolerance is not directly comparable across
  different quantities). Not blocking — this is the design ADR-0013 already chose and the task instruction
  explicitly confirms ("case 1's hand-written test is absorbed") — but the human-review packet should say
  this plainly rather than imply strictly more coverage.
- **Rollback**: this task adds new files and deletes three specific old ones; reverting is a plain `git
  revert` of the merge commit, no data migration.
- **Scope exclusions**: `Atmos_11` onward (trim/maneuver/orbit cases) are out of scope — M2's own acceptance
  is "check cases 1–10" (`docs/architecture.md` §16), matching the ten folders this plan targets.
- **New dependency**: none. The generator is stdlib Python, same as `generate_isa1976_table.py`.
- **Human-owned files this task writes**: `docs/decisions/0014-*.md` (new ADR), everything under
  `tests/golden/nesc/` (generator + 10 band headers), and the three deletions under `tests/golden/nesc/`/
  `tests/configs/`. All require `touch-goldens` armed per the spec's own Invariants section.

## Plan critique and decisions

**Joe reversed the earlier no-critique decision** (this round): one read-only Codex plan critique was
required and run, via `/codex:rescue --fresh --wait`, focused on frame/reference conventions between the
NESC columns and ASTRA's state (ADR-0001/ADR-0003) and the yaw-wrap generator logic, per
`docs/agents/review-dispatch.md`'s "Plan critique" procedure. First dispatch attempt failed before Codex
ran at all — `guard-oracle.sh`'s PreToolUse hook blocked the underlying Bash call, most likely (per
`docs/lessons.md`'s 2026-09-17 entry on backtick expansion in a double-quoted Codex focus argument) because
the first prompt's heavy use of Markdown backticks around code identifiers was not safely quoted somewhere
in the dispatch path; retried with a backtick-free prompt describing the same content in prose, which
completed cleanly. Both attempts and their outcomes are honestly the record here, not just the one that
worked — the first was a real tooling failure, not evidence the critique itself is unavailable.

**Findings, all verified independently before being folded in (never applied on the reviewer's word alone)**:

1. **Confirmed, major — six inertia conversions were 10× too small.** The stated conversion formula
   (`1 slug-ft² ≈ 1.355818 kg·m²`) was correct; its application to all six sphere/brick inertia values was
   not. Verified by hand-recomputing all six independently before accepting Codex's numbers; both
   recomputations agree to 6+ significant figures. Fixed in the Vehicle constants table above.
2. **Confirmed, major — ADR-0014's draft Context section overclaimed which case-3 channels are degenerate.**
   Case 3 has zero drag *force* but deliberately nonzero damping *moments* (the case's whole point);
   verified against Table 27's own "`C_D` set to zero" note (force only) and `Atmos_03_sim_04.csv`'s t=10 s
   moments (~3.9e-6, 6.0e-6, −2.7e-4 ft·lbf — clearly nonzero, matching `nesc_aero.cpp`'s own already-cited
   cross-check at t=10/30 s). The floor *formula* (Decision section) is unaffected; only the illustrative
   Context prose was wrong. Fixed, flagged explicitly given Joe's prior "accepted as written."
3. **Confirmed, major — yaw's `tol_rel` term, if computed from the wrong "mean," could silently make the
   whole band vacuous even with a `tol_abs ≤ π` sanity check in place.** Verified the derivation
   independently (residual-mean-as-denominator blowing up `tol_rel`, confirmed the arithmetic in Codex's
   worked example). Fixed by removing `tol_rel` from yaw's comparison entirely, not by trying to reconstruct
   a correct absolute reference for it — the simpler fix that removes the failure mode's mechanism.
4. **Confirmed, moderate — R8's "any one participant" CSV cross-check was unsound.** Independently spot-read
   `Atmos_04_sim_02.csv`/`Atmos_09_sim_03.csv` this session to confirm Codex's cited disagreement is real
   (SIM 2's nonzero t=0 north velocity in cases 4/5; SIM 3's missing rate columns in case 9), not a
   fabricated example. Fixed: Vol II Tables 25–34 are now R8's primary authority (exact values), CSV rows
   are multi-participant corroboration with a noise allowance, not a single trusted file.

Codex reported no frame-sign, rotation-convention, or Euler-order defect in the velocity-frame and
initial-rate corrections from the prior review round — independently re-derived the equatorial
Earth-rotation-in-NED assumption, the case 9/case 10 attitude-dependent projections, and the printed-vs-
inertial rate arithmetic and confirmed all of it. No changes needed there.

This is now the required plan critique for this task; step 5's earlier "architecture already pins the
design" reasoning is superseded by Joe's explicit reversal, not reinstated.

**Draft ADR-0014** (kept with this plan per `docs/templates/README.md`; not written to `docs/decisions/`
until Joe authorizes it and arms `touch-goldens`):

> # ADR-0014: Per-channel-kind resolution floor on the NESC oracle band
>
> Status: proposed
> Date: 2026-09-18
> Owner: Joe
> Related/supersedes: elaborates ADR-0013 (does not supersede it)
>
> ## Context
>
> ADR-0013's band formula (`tol_abs = k * max_t(spread)`, `k = 3`) is exact where the ensemble genuinely
> disagrees, but several channels are analytically zero for a given case: case 1's latitude; every aero
> **force** in cases 1–3 (case 1's sphere and case 2's brick both have `C_D = 0`; case 3's brick also has
> `C_D = 0`, zero drag specifically, though its damping **moments** are deliberately nonzero — that case
> exercises real rotational damping, not a degenerate channel, and is excluded from this floor's
> illustrative examples below **[corrected, Codex plan critique: the original text of this ADR draft wrongly
> included case 3's moments here]**). For those genuinely-zero channels, six independent tools' own
> floating-point roundoff on a value that should be exactly zero
> produces a nonzero `spread`, but one with no physical meaning — `tol_abs` computed purely from ADR-0013's
> formula can be vacuously tight (rejecting ASTRA's own equally-legitimate roundoff) or, if the roundoff
> happens to be larger, vacuously loose. ADR-0013 anticipated needing "the second judgment number its
> formula doesn't have" without specifying it.
>
> ## Decision
>
> Add a per-channel-kind absolute floor: `tol_abs = max(k * max_t(spread), floor)`, where `floor` depends
> only on the channel's physical kind, not on the case:
>
> | Channel kind | Floor |
> |---|---|
> | Angle (latitude, longitude, Euler) | 1e-12 rad |
> | Length (altitude) | 1e-9 m |
> | Linear velocity | 1e-9 m/s |
> | Angular rate | 1e-12 rad/s |
> | Force or moment | 1e-12 (N or N·m) |
> | Density, pressure, temperature, speed of sound | 1e-12 relative (folded into `tol_rel`, not `tol_abs`,
> since these are always-positive quantities with no natural zero to floor an absolute term against) |
>
> These are round numbers well inside double precision's own roundoff floor for the magnitudes this data
> spans (verified, this session's cited measurement: the tightest genuinely non-zero band found, case 9's
> yaw, sits 20× above its own floor — the floor does not mask a real disagreement at the scale this data
> actually exhibits).
>
> ## Rejected alternatives
>
> - **A single flat floor for every channel kind.** Rejected for the same reason ADR-0013 rejected one
>   global tolerance: an angle in radians and a force in newtons do not share a meaningful common floor.
> - **No floor; accept that a degenerate channel occasionally produces a vacuous band.** Rejected: a band
>   wide enough to admit an obviously wrong answer is explicitly not a test (ADR-0013's own
>   non-discriminating-channel language) — the failure mode this floor exists to prevent is a *false pass*
>   on a broken zero, not (only) a false failure.
> - **Drop degenerate channels from comparison entirely.** Rejected: cases 1–3's near-zero force/moment
>   channels are exactly where a sign error or an unintended nonzero constant is easiest to miss if nothing
>   checks them at all.
>
> ## Consequences and validation
>
> `tests/golden/nesc/generate_nesc_band.py` implements this formula for every channel it emits; the
> generator's own stderr report (mirroring `generate_isa1976_table.py`'s
> `check_transcribed_row_tolerances`) prints each channel's floor-vs-computed-`tol_abs` ratio so a future
> regeneration that moves a channel toward its own floor is a visible signal, not silently absorbed.
>
> ## Approval
>
> Accepted, 2026-09-18, as part of Joe's approval of this plan (`docs/plans/nesc-harness.md`) in full,
> including this embedded draft. **The Context section above was corrected after that acceptance** (same
> day, Codex plan critique caught a factual overclaim about case 3's aero moments — see the inline
> correction marker). The Decision (the floor formula and its table) is byte-for-byte unchanged; only the
> illustrative Context prose changed. Flagged explicitly here rather than silently reworded, since Joe's
> acceptance was of the ADR "as written" before this fix — his call whether this needs a fresh explicit
> re-acceptance or stands covered by the original one, given the Decision itself did not move. Written to
> `docs/decisions/0014-nesc-per-channel-tolerance-floor.md` verbatim (with this correction) once
> `touch-goldens` is armed and this task reaches that implementation step (guard-oracle.sh: `docs/decisions/`
> stays human-owned regardless of this acceptance record).

## Known gap found during implementation, disclosed not fixed: cases 4/5 fail on a real EOM/architecture
## characteristic, not a harness bug

**Cases 4 and 5 fail** (`ctest --preset asan -R physics.Nesc.Case0[45]`); all other eight cases pass
cleanly. Both failures are on `v_east`/`longitude`/`yaw`/`pitch`/`roll` (small, growing over the run) and
`force_x` (growing to ~0.7–0.8% relative error by t=30 s) — not `latitude`/`altitude`/`v_down`/the angular
rates, and **not `v_north` in this test run, but that "not" is unverified, not a clean negative result —
see "`v_north`'s band is contaminated, not clean" below.** This is real: NASA's own six-tool ensemble reference for `v_east` at every sample is
~1e-18 (floating-point zero — every participant agrees the sphere has zero lateral velocity, as expected
for a spherically symmetric body under central gravity with isotropic drag and no wind), while ASTRA's own
`v_east` grows from ~0 to several mm/s by t=30 s — a genuine simulated lateral drift, not a comparison
artifact (verified: `|v_b|` exactly equals the reconstructed `|v_n|` at every sample, and `frames::
rotate_b_to_n` is the exact algebraic inverse of the already-trusted `rotate_n_to_b` by construction, so the
NED reconstruction itself is not in question).

**Root cause, isolated this session**: `Integrator::step()`'s RK4 (`core/include/astra/kernel/
integrator.hpp`) calls the EOM functor four times (k1–k4) at four different intermediate states within one
1 ms frame. `Eom::operator()` (`core/src/kernel/eom.cpp`) recomputes gravity fresh at each of those four
calls (`gravity_.g_n(vs)` from that call's own intermediate `q_nb`/`p_n`), but reads `applied_wrench_` — the
aerodynamic force from `tests/plugins/nesc_aero`, computed **once**, before the integrator runs, from the
frame's pre-step state (`docs/architecture.md` §5: "vehicle models ... accumulate forces and moments → EOM
derivative → integrator step" is one ordered stage sequence, wrenches computed once per frame, not per RK4
sub-stage). For a body spinning fast enough (case 4/5's 10–30 deg/s, i.e. 0.03 deg per 1 ms frame) while
subject to a velocity-coupled force (drag, which is fixed in body-frame direction for the whole frame even
as the body's true instantaneous velocity direction rotates slightly under the frame's own Coriolis term),
the frozen-direction force is very slightly misaligned with the body's actual instantaneous velocity for
the later part of each frame. That misalignment is tiny per frame but systematic (not randomly
canceling, since the spin has a consistent sense) and compounds over 30,000 frames into a measurable
lateral velocity — confirmed by directly zeroing case 4's `C_D` (no rebuild needed, config is read at
runtime): with no aerodynamic force, `v_east`/`v_north` collapse to ~1e-12–1e-18 (pure floating-point
noise, matching the passing cases exactly); with `C_D` restored, the drift reappears identically.

**Follow-up check, Joe's request this session**: does `yaw`/`pitch`/`roll` collapse the same way
`v_east` did with `C_D = 0`, and does `force_x`? Ran it (temporarily un-skipping case 4, `C_D = 0`,
full channel comparison, not just the earlier `v_b`/`v_n` debug dump): **yes for `yaw`/`pitch`/
`roll`** — every sample passes, zero occurrences in the failure list, the same collapse-to-noise
result `v_east`/`v_north`/`longitude` already showed. This directly confirms the coupling is real
and specific to the applied *force*, not a broader attitude-integration defect: remove the force
entirely and the attitude (not just the translational lateral velocity) tracks the reference
correctly too. **`force_x` does not "collapse" in the same sense** — with `C_D = 0` it is
*identically* zero at every sample (`nesc_aero` computes no force at all), while the band's own
reference still reflects the real, published `C_D = 0.1` NASA data (a nonzero, growing drag force,
reaching ~14.4 N by t=30 s) — the channel now fails for an entirely different, expected reason (a
changed physical scenario, not the coupling bug: comparing a dragless run against a with-drag
reference). That mismatch cascades into new failures on `v_down`/`altitude`/`density`/`pressure`/
`temperature` too (the sphere free-falls faster without drag, reaching a higher `v_down` and a
lower altitude than the `C_D = 0.1` reference by t=30 s) — all expected side effects of removing the
force outright, not additional evidence about the mechanism. Net: the falsification test's original
conclusion stands and is now confirmed across every affected channel, not just the two it was first
checked against. No other
case combines both a fast body spin (10–30 deg/s; only cases 2–5 have this, from Vol II Tables 26–29) and a
nonzero net aerodynamic force (`C_D ≠ 0`; only cases 3–10) at once — cases 2/3 have spin but `C_D = 0`
(zero force, so nothing to misalign); cases 6–10 have real drag but negligible spin (~0 or ~`omega_ie`,
several orders of magnitude below 10–30 deg/s). Case 4/5 are the only two NESC cases, and very likely the
first scenario anywhere in this codebase's test history, to combine both — M1's own rigid-body tests
(torque-free, ballistic arc, symmetric-top precession) each exercise spin or translation, never fast spin
together with an extended-duration velocity-coupled force.

**`v_north`'s band is contaminated, not clean — the "never `v_north`" claim above is unverified, not a
negative result** (Joe, second review round this session). `case04`/`case05`'s `kVNorth.tol_abs` is
`0.009144000000000001` m/s — exactly `3 × 0.01 ft/s × 0.3048`, i.e. `k` times SIM 2's own known t=0
north-velocity artifact (0.01 ft/s where every other participant reports exact zero; already
identified as a real per-tool initialization anomaly by the Codex plan critique's Finding 4, which is
why SIM 2 is already excluded from `force_x` in these two cases per Vol II Table 78 footnote b — but
Table 78 says nothing about velocity, so this session's generator never excluded SIM 2 from `v_north`
too). The exact match (no residual beyond that one artifact) implies the genuine inter-tool spread for
`v_north` is at or below ADR-0014's own 1e-9 m/s floor, the same as `v_east`'s own tight `1e-9`
`tol_abs` in `case04` (not contaminated — case 4's Earth model does not rotate, so `v_east` has no
real physical spread to speak of either; case 5's own `v_east.tol_abs` is `0.0025` m/s, a real,
non-floor value from genuine rotation-Earth spread, still nothing like `v_north`'s artifact). Checked
directly against this session's own earlier debug capture (the un-skipped, `C_D = 0.1`, un-modified
run): `case04` sample 300 (t=30 s) had `v_b = [-80.5616, 77.4774, 239.497]`, reconstructed
`v_n = [-0.0048504, 0.00242424, 264.295]` — the actual `v_north` value is `-0.0048504` m/s, **larger
in magnitude than the `v_east` value (`0.00242424`) that did fail at the same sample**, but it never
appeared in the failure list because `|-0.0048504 - ~0| ≈ 0.00485 < 0.009144`, the inflated tolerance.
This is not a clean pass; it is a real coupling-scale drift sitting comfortably inside a tolerance
that is ~9144x wider than the floor a genuinely clean channel gets. **Action for PR 3c**: before
judging whether a per-RK4-substage fix actually resolves cases 4/5, first exclude SIM 2 from
`v_north` in the generator (the same way it is already excluded from `force_x` in these two cases)
and regenerate the band — only then does a `v_north` pass mean anything.

**`tol_rel` is loose (not vacuous, but a real weak spot) on channels that cross zero, not only on
yaw** (Joe, same review round; a docs-only disclosure, no golden/generator change this round).
ADR-0013's `tol_rel` is one scalar per channel per case — `k` times the *worst* per-sample relative
spread over the whole run — and `omit_relative_term`'s guard (`generate_nesc_band.py`) only drops a
*sample's* contribution when that sample's own `|mean|` is at or below `tol_abs`; it does not prevent
a sample just outside that band, where the mean is small but not smaller than `tol_abs`, from
contributing a large relative spread that then applies as a *constant* allowance to every other
sample of that channel, including ones where the reference is large. Measured, this session: `case02`
`rate_p` `tol_rel = 0.947` (95%), `case03` `roll` `tol_rel = 0.476` (48%), `case09` `v_down`
`tol_rel = 0.596` (60%), `case04` `force_y`/`force_z` `tol_rel = 0.761`/`0.719` (72–76%) — all
channels whose reference crosses (or passes very close to) zero at some point in the 30 s run. None
of these are unbounded the way yaw's own pre-fix `tol_rel` could have been (Codex's plan-critique
finding, `tol_rel = 6` in its worked example) — `tol_abs` alone already provides a real, non-vacuous
bound for every one of these channels, so this is not currently masking a comparison failure the way
`v_north`'s contamination above does — but a `tol_rel` this large is doing essentially no
discriminating work of its own and, added to `tol_abs`, is a real (if lesser) version of the same
mechanism the yaw fix removed entirely. **Follow-up recommended, not actioned this round**: a new
ADR elaborating ADR-0013 to drop `tol_rel` globally (not just for yaw), relying on `tol_abs` +
ADR-0014's floor alone for every channel — needs Joe's own decision, since it touches the accepted
oracle formula, not something to fold into this PR's own scope.

**Band width varies a lot by which participants happen to contribute, not just by physics** (Joe,
same review round, docs-only note): `case01`'s `temperature` `tol_abs` (`0.571` K) is ~250x
`case02`'s (`0.00229` K) — not a generator bug. `case01` (six participants, including `sim_03`) and
`case02` (five participants — `sim_03` does not exist for this case at all, confirmed earlier in this
plan's own Repository findings) genuinely have different contributing ensembles; `sim_03`'s own
temperature trajectory diverges from the other five participants' by up to ~0.19 K over the run
(verified directly against the raw CSVs this session), which alone accounts for the ~250x gap once
multiplied by `k = 3`. `density`/`pressure` show the same direction of effect far more mildly (~3–4x,
not ~250x) for the same reason. Noted here so a future reader does not mistake this ordinary
per-case ensemble-membership effect for a bug the way the two findings above actually are.

**Why this is not fixed in this PR**: it is a real characteristic of the existing, already-shipped,
documented per-frame architecture (M1 PR 4 through M2 PR 3a), not a defect this harness's own code
introduced. A fix means recomputing `wrench_b()` at every RK4 sub-stage instead of once per frame — a
genuine architecture change to the `Integrator`/`Eom`/`WrenchSource` contract (how often a `WrenchSource` is
called, what state it is called with, and its performance/allocation implications for every existing
plugin), squarely the kind of "unpinned, expensive-to-unwind design decision" `AGENTS.md`/`docs/process/
workflow.md`'s plan-review trigger exists for, not something to bolt on under a check-case harness's own
review pressure. Loosening ADR-0013/0014's tolerance for these two cases is explicitly not an option either
(`AGENTS.md`: "Never widen tolerances, narrow comparisons... to hide failure"; the reference itself shows
NASA's own six tools agree this channel should be ~zero, so any tolerance wide enough to pass ASTRA's actual
drift would not be discriminating).

**Decision: Joe, 2026-09-18, option (a)** — accepted as a disclosed gap, with these conditions, all applied:

- Cases 4/5 are `GTEST_SKIP()`ed in `tests/physics/nesc/nesc_cases_test.cpp`, each with a reason citing this
  section and the follow-up below — not deleted, and `ADR-0013`/`ADR-0014`'s tolerance is unchanged (no
  `WILL_FAIL`, no widened band). Each skipped test's own `RunNescCase` call is left in the source, unreached,
  so the exact comparison this gap blocks stays visible.
- `PLAN.md`'s M2 stays unchecked; **PR 3c — per-RK4-substage wrench evaluation** is added as the M2 closer:
  needs a superseding/elaborating ADR to ADR-0004 and `docs/architecture.md` §5, one independent Codex plan
  critique, and full plan review before any code. Not started in this worktree — a separate task.
- The risk (first-order force/spin coupling this frame architecture does not track between wrench samples;
  matters more once M3 introduces a real aircraft with sustained coupled spin+force dynamics) is recorded in
  the human-review packet's Decision record, not only here.

**Not yet done at the time of that decision**: the other three CMake presets (debug/release/static-plugins),
the full gate (`just check`), `/review-cycle` (general + adversarial, tier 3), and the human-review packet —
resumed immediately after the decision above, per Joe's own instruction.

## Done when

- Spec (`docs/specs/nesc-harness.md`) and this plan carry Joe's explicit approval (not inferred from
  elapsed time or model review).
- ADR-0014 is accepted.
- `touch-goldens` is armed for the implementation session; `tests/golden/nesc/generate_nesc_band.py` is run
  by Joe (or a session he explicitly arms) and its output committed.
- Eight of ten cases pass under every CMake preset, cases 4/5 `GTEST_SKIP()`ed with the disclosed-gap reason
  above (R1, as revised by Joe's 2026-09-18 decision — not the original "all ten," closed instead by
  `PLAN.md`'s new PR 3c); `nesc_case1_test.cpp` and its two dedicated files are deleted (R2); ADR-0014's
  floor is demonstrably load-bearing, not vacuous (R3); wall-time is measured and acceptable within
  `just check`'s budget (R5); case 2's yaw channel is discriminating through its own ±180° wrap instant with
  no `tol_rel` term applied to it (R7 — cases 4/5's own yaw wrap instants are in the generated band, and the
  generator's π-rad sanity check passed for them too, but the C++ test does not currently exercise them,
  being skipped); every case's SI `initial.state` matches Vol II Tables 25–34's own printed values,
  corroborated against multiple participants' t=0 CSV rows, not one (R8).
- Every transcribed mass/aero constant in this plan's Vehicle constants table (sphere/brick mass, inertia,
  `S`/`b`/`cbar`, `C_D`, damping coefficients) is independently recomputed from Vol II Tables 1/2/4/5 at
  config-authoring time, not copied from this document — this table's own inertia values were themselves
  wrong by 10× until the Codex plan critique caught it, so this is a standing implementation discipline, not
  a one-off fix.
- Independent Codex review (numerical/adversarial, per tier 3) completes with no unresolved critical/major
  findings; Joe's human-review packet is prepared (`/human-review`) before his merge decision.
- Links to this plan/spec recorded in `STATE.md`'s next handoff.
