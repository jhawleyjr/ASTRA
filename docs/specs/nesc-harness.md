# Feature: NESC ten-case check harness (M2 PR 3b)

Status: **revised, re-approval pending** — Joe's second plan-review pass (2026-09-18) found two required
corrections (velocity is NED not body-axis; initial `omega_b` needs the Earth-relative→inertial correction)
and reversed the earlier no-critique decision. The required Codex plan critique has run (`docs/plans/
nesc-harness.md`'s "Plan critique and decisions") and found four further, independently-verified defects
(a 10× inertia-conversion error, an ADR-0014 Context overclaim about case 3, a yaw `tol_rel` gap, an unsound
single-participant R8 check) — no frame-sign/rotation-convention defect. All six corrections across both
rounds are incorporated below and in the plan.
Owner: Joe (requirement owner); Claude plans/builds/validates
Risk: 3 — `plugins/physical` consumers, `tests/golden/**`, tolerance derivation, a new ADR
(`docs/process/workflow.md`#risk-tiers)
Intent/approval: Joe's task instruction (this session), `PLAN.md`'s M2 PR 3b line. Spec/plan approved
2026-09-18, reopened same day by Joe's second review pass; the required Codex plan critique has since run
and its findings are folded in below and in the plan; re-approval pending Joe's read of this revision.
Architecture/ADRs: `docs/architecture.md` §16 (M2 row: "NASA check cases 1–10 ... within the tolerances the
NESC data provides"); ADR-0013 (oracle = ensemble band, generator human-owned); ADR-0012 (per-frame domain
validity, prerequisite already merged); ADR-0014 (this task's own new ADR — per-channel-kind resolution
floor, drafted in `docs/plans/nesc-harness.md`, not yet accepted).
System context: `tests/physics/nesc/nesc_cases_test.cpp` is a GoogleTest binary linked against
`astra::core` (`tests/physics/CMakeLists.txt`'s `astra_physics_tests` target), the same seam
`nesc_case1_test.cpp` already occupies. Each case builds a `Simulation` from a config under
`tests/physics/nesc/`, steps it, and compares `Vehicle::state()` / `Vehicle::environment()` /
`tests/plugins/nesc_aero`'s registered force/moment signals against a band read from a generated header
under `tests/golden/nesc/`. Producer: `tests/golden/nesc/generate_nesc_band.py`, run by Joe, reading
`docs/refs/Atmospheric_checkcases/` in the **main checkout** (gitignored, not in this worktree). Consumer:
`just check`'s unlabeled `ctest`, every preset — this is a `physics`-labeled target, not `unit`, so the Stop
hook's fast pass skips it.

## Outcome and boundaries

**Outcome**: `tests/physics/nesc/` runs all ten NASA/TM-2015-218675 atmospheric check cases (`Atmos_01`
through `Atmos_10` in `docs/refs/Atmospheric_checkcases/`) against per-channel, per-sample-time bands
generated offline from the report's own six-tool ensemble, at ADR-0013's `k = 3` with ADR-0014's resolution
floor. PR 1's hand-written `nesc_case1_test.cpp` (single final-state comparison against an independently
integrated reference) is deleted; case 1 becomes one row of the same ten-case harness, checked against the
ensemble band like every other case, not against that independent integration.

**Non-goals** (Joe's task instruction):
- No geoid model. `wgs84_rotating::altitude()` feeding `isa1976` stays as-is (ADR-0013's own "Settled: no
  geoid model" finding — `altitudeMsl_ft` already matches ellipsoidal height to ≤6.1e-5 m for the eight
  WGS-84 cases).
- No new `HealthMonitor` capability for `EnvironmentSample`. It stays disclosed-only
  (`environment_sample.hpp`'s existing comments); this harness does not inject a NaN atmosphere/wind to
  probe that gap, it only reads `EnvironmentSample` values a correctly-running case produces.
- No changes to existing plugins beyond what the harness needs. Repository findings below (plan document)
  show none are needed at all: `Vehicle::environment()` already exposes `EnvironmentSample` (wind +
  atmosphere) publicly, and `tests/plugins/nesc_aero` (PR 3a) already registers force/moment signals. If
  implementation finds a real gap, it is a plan revision, not a silent scope add.

**Invariants carried from already-accepted decisions** (binding, not reopened by this task):
- ADR-0013's oracle formula (`reference = mean`, `tol_abs = k * max_t(spread)`, `tol_rel` likewise, `k = 3`,
  pass iff `|astra - reference| <= tol_abs + tol_rel*|reference|`), 10 Hz common grid by nearest-sample
  selection within 1e-6 s, no interpolation, generator human-owned and never run by `just check`.
- ADR-0014 (elaborating ADR-0013): a per-channel-kind resolution floor —
  `tol_abs = max(k*max_t(spread), floor)`, floor = 1e-12 rad / 1e-9 m / 1e-9 m/s / 1e-12 rad/s /
  1e-12 N or N·m / 1e-12 relative (density, pressure, temperature, speed of sound) — so an analytically-zero
  channel for a given case does not fail on floating-point roundoff in the ensemble itself.
- The pre-step sampling fix: `wrench_b` is evaluated on the *pre-step* state, so a sample at `t = n*0.1 s`
  requires reading state, then `sim.run(1)` (using that just-read pre-step state), then reading the
  wrench/environment signals the single frame just computed, then `sim.run(99)` to the next sample (skipped
  after the last sample) — 30,001 frames per case, not 30,000.
- Case 8's `wind/table` nodes sit at 0 ft and 40,000 ft, not 0/30,000 — the flown trajectory must stay
  strictly interior to the table so the clamp-at-endpoints policy stays unexercised.
- A t=0-only exclusion, both wind cases (`sim_06`) and case 8 (`sim_03`), from every airspeed-derived
  channel: both publish a physically-wrong zero airspeed at their own first sample.
- Vol II Tables 77/78's own participant exclusions (SIM 2's Euler angles and aero moments in every case,
  SIM 2's X-force in cases 4–5, SIM 3's Y-force in cases 6–8) are honored by the generator, not
  re-litigated here.
- **Yaw wrap-around** (Joe's spec-review requirement, same shape as the settled t=0 airspeed exclusion):
  cases 2, 4, and 5 have 10 Hz samples where the six tools' yaw values straddle ±180° (case 2 at t=15.3 s;
  cases 4 and 5 at t=15.0 s and t=24.6 s), so a raw (non-circular) `spread` at that instant is ~359° and
  ADR-0013's `max_t(spread)` formula would make the whole yaw channel's band vacuous for those cases — not
  a real disagreement, an artifact of comparing e.g. 179.9° against -179.9° as if they were 359.8° apart.
  Fix, entirely inside the generator: compute yaw's per-sample mean and spread on the **unwrapped**
  difference between each tool's value and a reference (mod 2π, mapped to the shortest signed angular
  distance), never on the raw degree values directly. `nesc_cases_test.cpp` then compares ASTRA's own yaw
  the same way — the wrapped difference `((astra_yaw - band.reference + π) mod 2π) - π`, not a plain
  subtraction — against `tol_abs` **only** (see below for why `tol_rel` is never used for yaw). This applies
  to yaw only (the one angle channel this data set's own trajectories actually cross the wrap boundary on):
  roll, pitch, latitude, and longitude need no such handling (longitude's own spread stays ≤0.06° everywhere
  in this data, confirmed this session — nowhere near a wrap boundary).
  **Corrected, Codex plan critique**: yaw's `tol_rel` term is never computed or applied, in every case, not
  just near a wrap instant — ADR-0013's pass criterion is `tol_abs + tol_rel*|reference|`, and near a wrap
  instant the small residual mean of the unwrapped per-tool differences (e.g. ~0.1°) is not the same number
  as the reconstructed absolute yaw reference (~180°); computing `tol_rel` from the former as if it were the
  latter's denominator can blow it up arbitrarily (Codex's worked example: `tol_rel = 6`, accepting any
  wrapped value at the ~180° reference) while a `tol_abs`-only sanity check would not catch it. Dropping
  `tol_rel` for yaw entirely removes this failure mode rather than patching around it, and makes the
  generator's own sanity check — that yaw's `tol_abs` never exceeds π rad, since a band that wide would be
  non-discriminating (ADR-0013's own concern) and signals the wrap fix itself has a defect — sufficient by
  construction, since there is no relative term left to inflate the effective tolerance beyond it.
- **Initial body rate is Earth-relative in Vol II's tables; ASTRA's `omega_b` is inertial** (Joe's second
  plan-review correction; already settled by PR 1 for case 1 specifically —
  `tests/golden/nesc/case1_dragless_sphere.hpp` lines 10–15 — now generalized to all ten cases). Every
  case's Table 25–34 "Rate" value is relative to the rotating Earth, not inertial space, and every case
  sits at latitude 0 (equator/prime meridian), so the correction is
  `omega_b = omega_earth_relative + frames::rotate_n_to_b(q_nb_initial, [omega_ie, 0, 0])`, where
  `omega_ie = 7.292115e-5 rad/s` (Table 73) and `q_nb_initial` is that case's own initial attitude — **not**
  the printed table value transcribed directly into a config. Confirmed against every case's own t=0
  `bodyAngularRateWrtEi_deg_s_*` CSV row this session: case 1 → (0,0,0); cases 2, 3, 5 → (10,20,30); case 9
  → (0,−0.004178,0); case 10 → (+0.004178,0,0) — case 4 needs no correction (`round`, `rotating: false`, so
  `omega_ie` is identically zero for that case and the printed [10,20,30] is already inertial); cases 6, 7,
  8 share case 1's identity attitude, printed rate, and equatorial position, so by the same formula their
  `omega_b` is also (0,0,0), not the printed −0.004178073 deg/s value transcribed literally.
- **Every case's SI initial state must be checked before any test runs** (Joe's second plan-review
  correction, added to Acceptance as R8 and to the plan's Done-when list; refined by the Codex plan
  critique): primary authority is Vol II Tables 25–34's own printed initial conditions (exact values, not
  measurements), corroborated against **multiple** participants' t=0 CSV rows — `feVelocity_ft_s_*` (NED),
  `bodyAngularRateWrtEi_deg_s_*` (inertial `omega_b`), the Euler columns, and `altitudeMsl_ft` — each
  converted to SI, tolerant of small inter-tool initialization noise, before the case is trusted to run
  30,001 frames at all. Not "any one participant": NASA's own tools disagree even at t=0 (case 4/5's SIM 2
  publishes a spurious 0.01 ft/s north velocity where every other participant shows exact zero; case 9's
  SIM 3 omits the rate columns entirely) — a single-file check could wrongly reject a correct config or
  silently skip what it cannot find.
- **Every transcribed mass/aero constant needs the same independent-recomputation discipline, not just
  initial state** (Codex plan critique, generalizing the lesson from a confirmed 10×
  slug-ft²-to-kg·m² arithmetic error this planning pass itself made and caught before implementation):
  citing a document page is not the same as having verified the arithmetic from it. A transcribed table in
  a planning document is provenance, never the final arithmetic authority — the same posture
  `generate_isa1976_table.py` already takes toward its own transcribed constants (independent cross-checks
  that `raise SystemExit` on their own detected inconsistency, not blind trust in a citation).
- `docs/decisions/` (the new ADR-0014), `tests/golden/nesc/**`, and any `tests/**/*tolerance*` file are
  human-owned (`guard-oracle.sh`); Joe arms `touch-goldens` before any of them can be written, and it stays
  armed through the whole window (generator, its output, the ISA generator's geopotential
  transcribed-vs-fresh consistency addition, ADR-0014, and the `nesc_case1_test.cpp`/`case1_dragless_sphere
  .hpp` deletions), not four separate lifts.

Assumptions:
- `docs/refs/Atmospheric_checkcases/` (82 MB, gitignored) exists in the **main checkout**
  (`/home/joe/repos/ASTRA`) with the expected 10 `Atmos_NN_*` folders and 4–6 `Atmos_NN_sim_MM.csv` files
  each (verified present this session: `Atmos_01`…`Atmos_10`, plus `Atmos_11`+ which are out of scope —
  M2 only claims check cases 1–10). The generator is run by hand from the main checkout or is pointed at it
  explicitly; it is never invoked by `just check` and this harness's own tests never read `docs/refs/` at
  all — only the generator's committed output under `tests/golden/nesc/`.
- ADR-0012 (per-frame domain validity) is already merged and covers `EarthModel`/`GravityModel::in_domain`
  plus the atmosphere `valid_range()` post-step check; this harness does not add new domain-validity
  coverage, it exercises the existing one (cases 9/10 fly in latitude).
- `Simulation::from_config` + `Vehicle::environment()` (public accessor, already returns the frame's latched
  `EnvironmentSample` by const reference) and `tests/plugins/nesc_aero`'s existing registered signals
  (`nesc_aero.force_b_{x,y,z}`, `nesc_aero.moment_b_{l,m,n}`) are sufficient to observe every channel this
  harness needs; no plugin gains new registered signals for this task (Repository findings, plan document).
- The generator's unit-conversion and column-keying logic (SI conversion, per-file header-name column
  keying across the 22/27/31/32/37/38-column variants, the `sim_05` duplicate-column assert, the
  transcribed-vs-fresh geopotential consistency check) is Joe's already-settled design (`PLAN.md`'s PR 3b
  line); this spec does not restate the derivation, it binds the harness to consume that generator's output.
- This feature does not itself validate that the generator's Python arithmetic is correct — that is the
  generator's own responsibility (mirroring `generate_isa1976_table.py`'s internal cross-checks:
  `check_transcribed_base_pressures`, `check_transcribed_row_tolerances`, and this task's added geopotential
  consistency check). The C++ harness trusts the committed header's numbers the same way
  `isa1976_table.hpp`'s C++ consumer already does.

## Contract and failure behavior

**Inputs** (per case, all under version control):
- One YAML config under `tests/physics/nesc/` per case (`ASTRA_TEST_PLUGIN_DIR`-resolved plugin paths,
  `sim.dt = 0.001`, `sim.frames = 30000`, `physical.{earth,gravity,mass}` required, `physical.{atmosphere,
  wind}` as the case needs, `vehicle.models` naming `tests/plugins/nesc_aero` only for cases with a real
  aero reaction, `initial.state` from Vol II Tables 25–34's initial conditions converted to SI).
- One generated band header under `tests/golden/nesc/` per case (`tests/golden/nesc/generate_nesc_band.py`'s
  output; human-owned, guarded).

**Outputs / behavior**: `tests/physics/nesc/nesc_cases_test.cpp` registers one GoogleTest case per NESC
case (10 total, replacing the single `Nesc.Case1DraglessSphereMatchesTheIndependentReference`). Each test:
1. Builds `Simulation::from_config` against that case's config.
2. Drives the pre-step sampling sequence above (§ Invariants) to collect ASTRA's own value for every
   compared channel at every sample time 0, 0.1, …, 30.0 s (301 samples).
3. For every `(channel, sample_time)` pair the case's generated band actually carries (a channel/case the
   generator excluded per Tables 77/78 or the t=0 airspeed exclusion is not compared at all, not compared
   against a vacuous band), asserts `|astra_value - band.reference| <= band.tol_abs + band.tol_rel *
   |band.reference|`.
4. Fails with the channel name, sample time, case name, actual value, reference, and computed tolerance on
   the first (or, if cheap, every) violation — matching `EXPECT_NEAR`/`SCOPED_TRACE`'s existing diagnostic
   style in `nesc_case1_test.cpp`.

**Compared channels** (settled per Q1, Joe's spec review — no `trueAirspeed`/`mach`/`dynamicPressure`):
- Position: `latitude` (rad), `longitude` (rad), `altitude` (m) — from `StateVector`'s `p_n` under
  round/WGS-84 Earth models (never flat: no NESC case uses `earth/flat`).
- Velocity: **corrected, Joe's second plan review** — `feVelocity_ft_s_*` is NED (local-level,
  Earth-relative) velocity, not body-axis, despite the "fe" prefix inviting the opposite reading. Evidence
  (Joe): case 9 (yaw 90°, launched east) has `feVelocity` Y = +1000 ft/s at t=0, matching NED east directly
  — a body-axis reading at yaw 90° would put that component on the body y (right/south) axis, contradicting
  "launched eastward"; case 2 (tumbling brick) holds `feVelocity ≈ (0, 0.23, 320)` ft/s from t≈9–10 s while
  Euler swings through (−4.3°, 3.7°, −66°) — a body-axis vector would visibly rotate with that attitude
  change and a NED one would not, and it does not. ASTRA's own `v_b` (body axes, ADR-0001) is therefore
  rotated to NED before comparison: `v_n = frames::rotate_b_to_n(q_nb, v_b)` (`core/util/frames.hpp`,
  already used in the opposite direction by `rotate_n_to_b`), compared in m/s against the band's NED
  reference. The generator emits the band already in NED SI units; no per-tool Euler-angle rotation is
  needed on the generator side, since the CSV column is already in a common frame across every tool.
- Attitude: 3-2-1 Euler angles (yaw, pitch, roll; `core/util/frames.hpp::euler_from_quaternion(q_nb)`),
  compared in rad against the CSV's `eulerAngle_deg_{Yaw,Pitch,Roll}` (deg), excluded case-wide for SIM 2
  per Tables 77/78 (that exclusion lives in the *generator's* band, not the C++ test — the test simply has
  nothing to compare against for an excluded channel/case). Yaw is compared on the wrapped difference
  (§ Invariants' "Yaw wrap-around" bullet), not a plain subtraction; roll and pitch need no such handling.
- Angular rate: `omega_b` (rad/s, inertial-referenced) against `bodyAngularRateWrtEi_deg_s_*` — no
  Earth-relative conversion needed (`ADR-0003`; the "Ei" in the column name is ECI/inertial already).
- Aero force/moment: `nesc_aero.force_b_{x,y,z}` (N) / `nesc_aero.moment_b_{l,m,n}` (N·m) against
  `aero_bodyForce_lbf_*` / `aero_bodyMoment_ftlbf_*`, only for cases whose config loads `nesc_aero`. Per the
  plan document's Vol II cross-check, that is nine of the ten cases — only case 1 (`C_D = 0`, no
  `WrenchSource` at all) is not checked on these channels; its trajectory not reflecting drag already
  proves the zero force, the same reasoning the existing `case1_dragless_sphere.hpp` comment gives for why
  `omega_b` stays exactly zero there.
- Atmosphere: `Vehicle::environment().atmosphere.{density_kg_m3, pressure_pa, temperature_k}` against
  `airDensity_slug_ft3`, `ambientPressure_lbf_ft2`, `ambientTemperature_dgR`, for every case that configures
  `physical.atmosphere` (all ten, per this session's read of `Atmos_01_sim_01.csv`'s header — even the
  dragless case 1 publishes these columns).
- Not compared: `speed_of_sound_m_s` (published in `AtmoState` but no NESC column drives a distinct check
  beyond what temperature already implies through the same formula), `mach`, `dynamicPressure_lbf_ft2`,
  `trueAirspeed_nmi_h`, `altitudeRateWrtMsl_ft_min`, `localGravity_ft_s2` (PLAN.md's own "never a compared
  channel" note — it is pure Newtonian gravitation, not `wgs84_j2::g_n()`'s apparent gravity), and ECEF
  `gePosition_ft_*`/`eiPosition_ft_*`/`eiVelocity_ft_s_*` (redundant with the geodetic/body-frame channels
  already compared, and ADR-0013's own geoid check already used ECEF only as a one-time offline
  verification, not a per-frame comparison).

**Failure/reset cases**: a case whose config is malformed fails at `Simulation::from_config` with the
existing `ConfigError` diagnostics (no new behavior needed). A case that halts before 30 s (any
`HealthMonitor` halt, including ADR-0012's domain check) fails the GoogleTest case with the halt reason —
this harness adds no new tolerance for "ran short," a halt before 30,001 frames is a hard failure needing
Joe's decision (config bug vs. real defect), not a partial-credit comparison.

**Determinism**: no new requirement beyond what `Simulation::run`/`step` already guarantee (M1/M2's existing
bitwise-determinism coverage); this harness runs each case once, not twice, since it compares against a
fixed external band, not against itself.

**Units/frames**: every ASTRA-side quantity stays SI/native (no unit conversion inside `nesc_cases_test
.cpp` or any production/test code other than the generator, per ADR-0013's "unit conversion happens in the
generator, not in `core/util/units.hpp`"). The generated header's `reference`/`tol_abs`/`tol_rel` values are
already SI.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | All ten cases pass within their generated bands under every CMake preset | `ctest --preset <preset> -R 'physics.Nesc'` (or `just check`'s unlabeled full `ctest`) exits 0; `tests/golden/nesc/*.hpp` is the independent oracle (ADR-0013's ensemble, not ASTRA's own output) |
| R2 | Case 1's old hand-written final-state test is fully absorbed, not duplicated | `nesc_case1_test.cpp`, `tests/configs/nesc_case1_dragless_sphere.yaml`, and `tests/golden/nesc/case1_dragless_sphere.hpp` are deleted; `git grep -n Nesc.Case1` returns nothing outside history |
| R3 | A degenerate (analytically-zero-for-its-case) channel does not fail on ensemble floating-point roundoff | ADR-0014's floor applied per channel kind; generator-side check (already scoped in `PLAN.md`'s settled bullet) confirms the tightest genuinely-non-zero band (case 9 yaw) sits well above the floor |
| R4 | The sampling-fix regression the old PR-3a-era code had (one-frame-stale wrench) does not recur | The pre-step-then-`sim.run(1)`-then-`sim.run(99)` sequence is exercised by every one of the ten cases, not asserted by a separate unit test — a case with real aero (e.g. case 3) failing to reproduce Vol II's `sim_04` cross-check at t=10/30 s (already true today per `nesc_aero.cpp`'s own header) is the regression signal |
| R5 | `just check`'s wall-time budget absorbs ten cases, not one | Measured before writing any new code: current `nesc_case1_test.cpp` wall time × 10 (see plan's first implementation step) |
| R6 | `docs/decisions/0014-*.md` is accepted before any golden file cites it | Joe's explicit ADR acceptance, recorded the same way ADR-0012/0013 were |
| R7 | Cases 2, 4, 5's yaw channel is discriminating across the ±180° wrap instants, not vacuous | Generator's own stderr report (mirroring `check_transcribed_row_tolerances`'s pattern) shows yaw's `tol_abs` at t=15.3 s (case 2) and t=15.0 s/24.6 s (cases 4/5) computed from the unwrapped spread, not the raw ~359° one, with no `tol_rel` term applied to yaw anywhere; the generator's own assertion that yaw's `tol_abs` never exceeds π rad passes |
| R8 | Every case's SI `initial.state` matches Vol II Tables 25–34's own printed values (primary authority), corroborated against several participants' t=0 CSV rows (not one), before the case is trusted to run | For each case: config `v_b`/`omega_b`/`euler_nb`/altitude checked exactly against the plan's transcribed table values (converted to SI with the corrected constants), then spot-checked for gross disagreement against ≥2 participants' `feVelocity_ft_s_*` (NED, via `rotate_b_to_n`) and `bodyAngularRateWrtEi_deg_s_*` at t=0, tolerant of small inter-tool initialization noise and skipping any participant missing a needed column |

Human-owned goldens/tolerances stay unchanged in *mechanism* (ADR-0013's formula, `k = 3`) — this task adds
ADR-0014's floor as an elaboration, not a widening, and only Joe's arming of `touch-goldens` permits writing
any file under `tests/golden/nesc/` or `docs/decisions/`.

## Questions and decision

| # | Question | Answer/reference |
|---|---|---|
| Q1 | Compared-channel set — should `trueAirspeed`/`mach`/`dynamicPressure` also be checked? | **Answered (Joe, spec review)**: no. Scope stays position, velocity, Euler, rate, aero force/moment (where a case loads `nesc_aero`), atmosphere density/pressure/temperature, as listed above. |
| Q2 | Where do case configs live? | **Answered (Joe, spec review)**: `tests/physics/nesc/`, per the task instruction's own wording. |
| Q3 | Per-case plugin selection (Earth/gravity model, `rotating:` flag, wind, whether `nesc_aero` is loaded) | **Moved to the plan, not resolved here** (Joe, spec review: "Not an implementation-time item at tier 3"). `docs/plans/nesc-harness.md`'s Repository findings now carries the full cross-check against Vol II Table 6.3-1 and Tables 25–34, verified this session against the actual document (not inferred from folder names), with a case-by-case table Joe reads before plan approval. |
| Q4 | One `.hpp` per case or a combined header? | **Answered**: ten files, one per case, per ADR-0013's own explicit wording. Not open. |

**Required change from spec review, incorporated above**: yaw wrap-around handling for cases 2, 4, 5 (§
Invariants, § Contract's Attitude bullet, R7). Longitude needs no equivalent change (≤0.06° spread
everywhere in this data).

**Note for the human-review packet** (Joe, spec review, no spec change needed): R2 replaces case 1's
independently-integrated-DOP853 oracle with the ensemble band — after this PR, no NESC case is checked
against anything but the six-tool ensemble. This is already stated in this spec's Validation-relevant
"Compatibility/migration" discussion (plan document) and should be stated plainly in the packet, not
implied as strictly more coverage.

Joe's spec approval: reopened 2026-09-18 by the velocity-frame and initial-rate corrections; both
incorporated. Plan approval: reopened 2026-09-18; the required Codex plan critique on frame/reference
conventions (ADR-0001/0003) and the yaw-wrap generator logic has run (two dispatch attempts, second
succeeded) and found four further defects (10× inertia error, ADR-0014 Context overclaim, yaw `tol_rel`
gap, unsound R8 single-participant check), all independently verified and fixed — see
`docs/plans/nesc-harness.md`'s "Plan critique and decisions". Both spec and plan now pending Joe's re-read
of this full revision.
