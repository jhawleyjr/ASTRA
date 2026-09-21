# Plan: M3 PR 4 — `aircraft/notional_transport/physical/aero` + `hardware/propulsion/turbofan`

Spec/brief: this file (combined spec+plan, Tier 3 but a bounded single-package addition — no new
interface, no new ADR).
Risk: **3** (`aircraft/**/physical`, `aircraft/**/hardware`, per `docs/process/workflow.md`'s risk
table).
Status: draft — awaiting Joe's plan approval (spec/plan combined per template's "small change" allowance).
Plan approval: pending.
Architecture/ADRs: ADR-0001 (frames), ADR-0002 (units/SI), ADR-0017 (write seam), ADR-0018
(`AeroModel`/`PropulsionModel`). No new ADR — PLAN.md's own PR 4 line already says "mechanism
already pinned by PR 2/3."
System context: two `WrenchSource` plugins loaded into `vehicle.models` (docs/architecture.md §7,
§13). `AeroModel`/`PropulsionModel` (PR 3, merged `da18909`) already assemble `AeroInputs`/
`PropulsionInputs` from `(VehicleState, EnvironmentSample)` and call this PR's `compute()`; this PR
supplies the only two concrete implementations `aircraft/notional_transport` needs for PLAN.md's
M3 trim/linearize/regression milestone (PR 6/7/8).

Per Joe's redirect this session: numeric fidelity does not matter for this notional airframe: every
open modeling choice below is decided here, not deferred to Joe, and every derived/invented number
is labeled **notional**. Nothing in this plan touches a core interface, an ADR, or the framework's
ability to support a higher-fidelity model later — see "Escalations" at the end (none required),
and one disclosed scope drop that brushes against a core-interface boundary without crossing it.

## Outcome and boundaries

Two new plugins, mirrored into both `aircraft/_template/` (zeroed placeholder, throws `ConfigError`
until edited — exact pattern of PR 1's `physical/mass`) and `aircraft/notional_transport/` (real,
sourced-and-derived numbers in that aircraft's own `configs/cruise.yaml`, plugin `.cpp` unchanged
between the two — same separation PR 1 already established for `mass.cpp`):

- `physical/aero/aero.cpp` — a linear-coefficient `AeroModel`. Reads `controls.elevator`,
  `controls.aileron`, `controls.rudder` (rad) via `bind_inputs()`.
- `hardware/propulsion/turbofan/turbofan.cpp` — a linear thrust-lapse `PropulsionModel`. Reads
  `controls.throttle` (dimensionless, 0..1) via `bind_inputs()`, shared symmetrically across both
  engine instances by `InputRegistry`'s existing same-name dedup (`pr2-write-seam.md` decision (h):
  PR 4 picks shared/symmetric, not per-station — no differential-thrust use case exists yet).

**Non-goals:** table-interpolated (non-linear) aerodynamics; engine spool/N1-N2 dynamics or fuel
flow; ground effect, flaps, gear, speedbrake/spoiler modeling (all zero at the clean-cruise
condition this airframe targets — dropped, not modeled); envelope monitors (§15, already deferred
by ADR-0018 to "PR 4 or later" — still deferred here, tracked as the same PLAN.md follow-up, no
declared α/β/Mach/q̄ limits shipped this PR); trim itself (PR 6) — this PR only proves `compute()`
against hand-picked, sourced/derived conditions, not a converged equilibrium.

**Assumptions:** `AeroModel`/`PropulsionModel`'s `wrench_b()` already computes
`v_rel_b`/`alpha_rad`/`beta_rad`/`dynamic_pressure_pa`/`mach` correctly and guards non-finite
inputs (PR 3, not re-checked here). `physical.mass`'s CG is the wrench reference point (§7,
unchanged). `InputRegistry`'s shared-pointer-by-same-name behavior for two plugin instances is
already implemented and tested (PR 2). Config `station: left|right` keys are read as ordinary,
plugin-defined config, not a framework concept (confirmed: no framework code anywhere reads
`station`; it is this plugin's own key, same status as `mass`'s `ixz`).

## Open items — decided here, not escalated

### 1. Thrust source (no CFM56 data exists in `docs/refs/`)

Confirmed this session: `docs/refs/aircrafts/737.xml`'s `<propulsion><engine file="CFM56">` only
*references* an external JSBSim engine-model file; no such file, and no other engine/thrust
reference, exists anywhere under `docs/refs/`. Per Joe's redirect: do not stop, do not ask — derive
a self-consistent notional value from data already on disk and label it plainly.

**Decision:** rated thrust is **derived from the cruise drag/thrust equilibrium implied by this
same source's own, already-sourced aerodynamics block** (§"Worked numbers" below), not from any
real engine spec. `docs/refs/aircrafts/737.xml` is Aeromatic/Roskam-estimated to begin with (its
own `<note>`, already quoted in `cruise.yaml`) — a derived thrust number carries no less fidelity
than the aero coefficients it is derived from, and every number this PR ships is labeled `notional
-- not real CFM56 data, derived from 737.xml's own <aerodynamics> block, see docs/plans/
pr4-notional-aero-turbofan.md` in both the config comment and this file.

### 2. Tables vs. linearization

**Decision: linearize.** PLAN.md's own PR 4 title says "linear-coefficient aero"; PR 7 later
linearizes the *whole vehicle* about trim, so a plugin that is already a set of constant
stability-derivative coefficients keeps that step self-consistent (no separate "is my table's local
slope the same as what PR 7 computed" question). Table-valued source coefficients
(`CD0(alpha)`, `CLalpha(alpha)`, `CDmach(mach)`, `Clda(mach)`, `Cmde(mach)`) are each reduced to one
constant, evaluated at or secant-sloped through the cruise design point — worked below.

### 3. Terms kept vs. dropped for a clean-cruise notional model

| Source term | Kept? | Why |
|---|---|---|
| `CD0(alpha)` | Kept, as one constant | Evaluated at `alpha=0` breakpoint (0.021) — cruise `alpha` is small (~0.0255 rad, worked below) so the table's own `alpha=0` value is used directly rather than interpolating a barely-different neighbor; simplest option. |
| `CDi` (induced, `K*CL^2`) | Kept | `K=0.043` is a plain scalar in the source, not a table. Primary source of drag-vs-alpha variation in this model (subsumes most of what `CD0`'s own alpha-slope would have added). |
| `CDmach` | **Dropped** | Table is exactly 0 and flat from Mach 0.00–0.79; cruise Mach ≈0.77 (worked below) is inside that flat-zero region, so the term is identically zero here — not an approximation, an exact drop. |
| `CDflap`, `CDgear`, `CDsb`, `CDsp` | **Dropped** | All proportional to a control/config position that is exactly 0 at clean cruise (flaps 0, gear up, speedbrake/spoiler stowed). |
| `kCDge`, `kCLge`, `kCLsb`, `kCLsp` | **Dropped (≡1)** | Ground-effect tables only defined to `h_b<=1.1`; 10,000 m altitude is many wingspans above ground, table value clamps to ~1.0 at its last entry — modeled as an unconditional multiplier of 1 (i.e., not modeled at all) rather than carrying dead table-lookup code for a regime this airframe never flies in. |
| `dCLflap` | **Dropped** | Flap position 0 at clean cruise. |
| `CDbeta`, `CDde` | **Dropped** | Second-order longitudinal/lateral coupling terms; S&L §4.2's classical mode approximations (this project's own PR 7 oracle, per ADR-0018/PLAN.md's O4 supersession) do not use drag-due-to-beta or drag-due-to-elevator at all — carrying them here adds no value PR 6/7/8 need. |
| `CLalpha(alpha)` | Kept, linearized | Secant slope through the source table's `(0, 0.20)`→`(0.23, 1.20)` bracket (4.3478 /rad); cruise trim `alpha` falls inside this exact bracket (worked below), so the secant is not an extrapolation. |
| `CLde` | Kept, as given (0.2) | Already a plain scalar. |
| `CYb`, `Clb`, `Clp`, `Clr`, `Cldr`, `Cmalpha`, `Cmq`, `Cnb`, `Cnr`, `Cndr` | Kept, as given | Already plain scalars in the source (no table); every one of these is a named input to S&L §4.2's classical short-period/phugoid/Dutch-roll/roll/spiral approximations, PR 7's own oracle — dropping any would remove a term PR 7 needs. |
| `Clda(mach)`, `Cmde(mach)` | Kept, evaluated at cruise Mach as constants | Linear interpolation of each table at cruise Mach ≈0.77 (worked below); needed for aileron/elevator control effectiveness in PR 6's trim. |
| `Cmadot` (alpha-rate derivative) | **Dropped — disclosed, not a silent gap** | `AeroInputs` (ADR-0018, pinned) carries no `alpha_dot` field; this coefficient cannot be evaluated without one. Adding that field would be a core-interface change to a struct ADR-0018 already settled — out of scope for a plugin-level PR, and exactly the kind of change Joe asked to have escalated rather than made silently. Resolved here by **not** adding the field and dropping the term instead, so no escalation is actually needed, but flagged here and in the PLAN.md follow-up list since it is a real, non-obvious modeling limitation (no alpha-rate damping in short-period response) a future session or Codex review should not have to rediscover. |

### 4. Control names/units

`controls.elevator`, `controls.aileron`, `controls.rudder` — rad, matching ADR-0001 sign
conventions and the ICD's `astra_sw_to_sim_v1.elevator/aileron/rudder` fields (§10, already `rad`).
`controls.throttle` — dimensionless, 0..1, matching the ICD's `throttle[2]` comment ("0..1 per
engine") and shared symmetrically by both turbofan instances (decision 4 above). All four follow
`tests/plugins/aero_stub`'s own precedent (`inputs.add(registry, "controls.elevator", "rad")`).

### 5. Test tolerances

Decided here (not a golden file or an ADR; AGENTS.md's human-owned-oracle rule is about
`tests/golden/**`/tolerance files/ADRs specifically, and this PR's tests are new, not existing
oracles being loosened):
- **1e-9 relative** for pure arithmetic identities with no modeling judgment involved: the
  structural→body frame conversion (sign flip + unit conversion) for `AERORP` and each engine
  station, and the `r × F` moment-transfer arithmetic itself.
- **1e-6 relative** for force/moment values that depend on this plan's own secant-slope or
  cruise-Mach-evaluated constants (the worked numbers below): both the test oracle and the plugin
  implementation compute the identical closed form, so this is generous headroom over
  double-precision rounding, not a fidelity claim.
- **V→0 tests** (`R2`/`R4` below): no numeric tolerance — `std::isfinite` on every `Wrench`
  component, full stop.

## Frame-conversion math (worked, since `docs/refs/` is gitignored)

Source (`docs/refs/aircrafts/737.xml`) locations are in JSBSim's **structural frame**: X aft, Y
right, Z up, inches, origin unspecified but irrelevant (only deltas are used). ASTRA body frame
(ADR-0001) is FRD: X forward, Y right, Z down. Converting a **delta** between two structural-frame
points to body axes: `dx_body = -dx_struct`, `dy_body = dy_struct` (Y unchanged, right is right in
both), `dz_body = -dz_struct`; then inches → meters (`x 0.0254`). This is the same sign-convention
discipline PR 1's `mass.cpp` already documents for the inertia tensor's product-of-inertia
convention, applied here to locations instead.

`<metrics><location name="AERORP">` = (625, 0, 24) in. `<mass_balance><location name="CG">` = (639,
0, -40) in (identical CG numbers PR 1 already transcribed for the inertia/mass config).

```
delta_struct (AERORP - CG) = (-14, 0, 64) in
r_aero_b (AERORP relative to CG, body axes) = (+14, 0, -64) in = (0.3556, 0.0, -1.6256) m
```

(AERORP is forward of and above CG — matches the raw numbers' sign: smaller structural-X is more
forward in FRD; larger structural-Z is more "up," i.e. more negative in FRD-down Z.)

Two `<engine file="CFM56">` blocks, `<thruster file="direct">` locations (539/-193/-40 → wait,
source reads 540) = (540, -193, -40) in (feed 0, `y<0` ⇒ this project's "left" — right is
`+y`/positive in FRD, so negative-y is left) and (540, 193, -40) in (feed 1, "right"):

```
delta_struct, left  (engine - CG) = (-99, -193, 0) in
delta_struct, right (engine - CG) = (-99,  193, 0) in
r_engine_left_b  = (+99, -193, 0) in = (2.5146, -4.9022, 0.0) m
r_engine_right_b = (+99,  193, 0) in = (2.5146,  4.9022, 0.0) m
```

Both thrusters' `<orient>` is `(roll, pitch, yaw) = (0, 0, 0)` deg — thrust line parallel to body
X, so `F_engine_b = (T, 0, 0)` per engine and `moment_engine_b = r_engine_b × F_engine_b = (0, 0,
-r_y * T)`. For equal `T` on both engines this is `(0, 0, +4.9022*T) + (0, 0, -4.9022*T) = 0` — the
two engines' yaw moments exactly cancel under symmetric thrust, the expected sanity check for a
symmetric clean-cruise trim with no rudder needed for engine-out asymmetry (there is no engine-out
condition modeled in this PR).

## Force/moment assembly (S&L citation, resolves "AERORP ≠ CG")

Verified directly against `docs/refs/textbooks/...Stevens_Lewis...pdf` this session (same book
ADR-0018 already cites for α/β and the V→0 hazard):

- Table 2.3-1 (p.78), Eq. (2.3-3a): aerodynamic force in wind axes is `F_A^w = (-D, -C, -L)` (drag,
  crosswind/side force, lift — matches 737.xml's own `<axis name="DRAG"/"SIDE"/"LIFT">` naming
  exactly).
- Eq. (2.3-2) (p.77): the wind-from-body-fixed rotation `C_{w/bf}(alpha, beta)`; used, per the
  text's own note directly under it, "without the subscript e when converting instantaneous
  wind-axes components into body axes" — i.e. with the instantaneous `alpha_rad`/`beta_rad` from
  `AeroInputs`, not a fixed trim value.
- `F_aero_b = C_{w/bf}^T * (-D, -C, -L)` (rotation matrices are orthogonal; transpose is inverse) —
  this plugin's force assembly.
- Eq. (2.3-4a)/(2.3-4b) (p.78): the roll/pitch/yaw moment coefficients (`Clb`, `Cmalpha`, `Cnb`,
  etc. — 737.xml's `<axis name="ROLL"/"PITCH"/"YAW">`) are **already body-fixed-axis moments**
  (`M_A^{bf} = (l, m, n)`) in this convention, needing no rotation — only the D/C/L forces above go
  through `C_{w/bf}`.
- Moments computed this way are about **AERORP** (the point the source's own force/moment
  coefficients are referenced to), not CG. `Wrench::moment_b` is defined about CG (§7, unchanged).
  Transfer: `moment_b_cg = moment_native_b + r_aero_b × F_aero_b` — the standard rigid-body
  moment-transfer theorem (force is unchanged by choice of reference point; moment picks up the
  lever-arm cross product). This is exactly PLAN.md's own PR 4 line ("AERORP ≠ CG ... aero needs
  its own r×F") and the concrete implementation of ADR-0018's compute()-owns-its-own-transfer rule.

## Nondimensional-rate V→0 obligation (ADR-0018) — the concrete fix for this plugin

Source terms `Clp`, `Clr`, `Cmq`, `Cnr` each multiply their coefficient by `qbar * S * L *
(L/(2*V_T)) * rate` (`L` = `b` for roll/yaw, `c̄` for pitch — JSBSim's `bi2vel`/`ci2vel`
properties). A **naive port** computes `(L/(2*V_T))` as its own subexpression: at `V_T=0` this is
`L/0 = +Inf` (not NaN by itself), and `Inf` multiplied by any other zero factor later (`qbar=0` at
`V_T=0`) yields `NaN` — the exact hazard ADR-0018's compute()-level V→0 obligation warns about.

**Fix, algebraic, not a branch:** substitute `qbar = 0.5*rho*V_T^2` and simplify before
implementing:

```
term = qbar * S * L * Coeff * (L/(2*V_T)) * rate
     = (0.5*rho*V_T^2) * S * L^2 * Coeff * rate / (2*V_T)
     = 0.25 * rho * V_T * S * L^2 * Coeff * rate
```

This is **linear in `V_T`**, with no division by `V_T` anywhere — finite (exactly 0) at `V_T=0` by
construction, no `V_T==0` branch needed. The plugin must compute it in this reduced form (never
compute `L/(2*V_T)` as a standalone quantity); this is the one piece of implementation-level
guidance this plan gives beyond "kept vs. dropped," because a builder translating the source
formula literally will reproduce the exact hazard ADR-0018 already named.

The propulsion side has no such hazard: the thrust-lapse form chosen below (density-ratio only, no
Mach/ram term) has no `V_T` dependence at all, so `compute()` is trivially finite at `V_T=0` — the
simplest option available also happens to satisfy the V→0 obligation for free. Still gets its own
test (`R4`) since the obligation is stated as a `compute()`-level contract regardless of how trivial
a given implementation makes it.

## Worked numbers (illustrative derivation — builder re-derives exact test-point values from the
real `isa1976` output at implementation time; these are for sourcing/self-consistency, not a
bit-exact oracle)

Design point: 10,000 m, 230 m/s TAS (PLAN.md's own M3 acceptance line). ISA at 10,000 m: `T ≈
223.15 K`, `rho ≈ 0.4128 kg/m^3`, `a ≈ 299.5 m/s` ⇒ `Mach ≈ 0.768`. `S = 1171 ft^2 = 108.809 m^2`,
`b = 94.70 ft = 28.865 m`, `c̄ = 12.31 ft = 3.7521 m`.

`q̄ = 0.5 * 0.4128 * 230^2 ≈ 10,918.5 Pa`. Weight (mass 37,648 kg from PR 1's own config, `g =
9.80665`) `≈ 369,177 N`. Required `CL = W / (q̄ S) ≈ 0.3108`. Solving the `CLalpha` table's
`(0,0.20)→(0.23,1.20)` linear bracket for this `CL` gives trim `alpha ≈ 0.0255 rad` — inside the
bracket used for the secant slope above, not an extrapolation (self-consistency check passes).
`CD0(0) = 0.021` (source table, `alpha=0` breakpoint, used directly per decision 3). `CD = CD0 + K
CL^2 = 0.021 + 0.043 * 0.3108^2 ≈ 0.02721`. `D = q̄ S CD ≈ 32.3 kN` total (≈16.16 kN/engine at equal
throttle) — this is this notional airframe's own L/D ≈ 11.4, well below a real 737's ≈17; expected
and irrelevant, since the source aero coefficients themselves are Aeromatic-estimated, not a real
737's, and Joe has said fidelity is not a goal here.

`Clda(0.768) ≈ 0.0743`, `Cmde(0.768) ≈ -0.8544` (linear interpolation of each table's two given
Mach breakpoints).

**Notional thrust reference** (decision 1): `thrust_ref_n = 16,163` (N, per engine — half of
`D` above, i.e. this value is *defined* so that `throttle=1.0` at the reference density exactly
balances this notional airframe's own derived cruise drag — a self-consistency choice, not a
trim solve; PR 6 still does the real trim), `density_ref_kgm3 = 0.4128` (the ISA density this
reference thrust was derived at). Thrust law: `T = throttle * thrust_ref_n * (rho / density_ref_kgm3)`
— linear in density, no Mach/ram term, no spool dynamics (non-goals, above).

## Config keys (both plugins; `_template`'s copy ships all-zero/TODO, throwing `ConfigError` until
edited, exact pattern of PR 1's `physical/mass`)

`physical/aero` — prefixes resolve the `CL` (capital-L lift) vs. `Cl` (rolling moment) collision
that would otherwise both lowercase to `cl_` in snake_case (a real ambiguity in the source's own
notation, not present in this repo's naming until this PR — flagged so a reviewer sees it was
deliberate): `wing_area_m2`, `wingspan_m`, `mean_chord_m`, `r_aero_b_m` (3 numbers, AERORP-CG offset
worked above) — `cl_0`, `cl_alpha_per_rad`, `cl_elevator_per_rad` (LIFT family) — `cd_0`,
`cd_k` (DRAG family) — `cy_beta_per_rad` (SIDE) — `croll_beta_per_rad`, `croll_p_per_rad`,
`croll_r_per_rad`, `croll_aileron_per_rad`, `croll_rudder_per_rad` (ROLL, `Cl`) — `cm_alpha_per_rad`,
`cm_q_per_rad`, `cm_elevator_per_rad` (PITCH) — `cn_beta_per_rad`, `cn_r_per_rad`,
`cn_rudder_per_rad` (YAW).

`hardware/propulsion/turbofan` — `station` (string, label only, not read for geometry),
`x_offset_from_cg_m`, `y_offset_from_cg_m`, `z_offset_from_cg_m` (explicit per-instance numbers,
worked above — no "left implies negative y" logic in the plugin itself, keeps config maximally
explicit per instance), `thrust_ref_n`, `density_ref_kgm3`.

## Repository findings

- `AeroModel`/`PropulsionModel`/`AeroInputs`/`PropulsionInputs`/`Wrench`/`WrenchSource` (PR 3,
  `core/include/astra/interfaces/{aero,propulsion}_{model,inputs}.hpp`, `wrench.hpp`,
  `wrench_source.hpp`) are final, unchanged by this PR.
- `InputRegistry`/`bind_inputs()` (PR 2, `core/include/astra/signals/input_registry.hpp`) already
  gives two same-named registrations the same shared pointer — no code change needed for symmetric
  throttle.
- `tests/plugins/aero_stub/aero_stub.cpp`, `tests/plugins/propulsion_stub/propulsion_stub.cpp` are
  the exact structural pattern (`bind_inputs()`, `compute()` signature, anonymous namespace,
  `PluginDescriptor`) this PR's two real plugins mirror, minus the "test"-category descriptor.
- `aircraft/notional_transport/physical/mass/mass.cpp` + `aircraft/_template/physical/mass/mass.cpp`
  are the exact `_template`-vs-real-aircraft separation pattern (identical `.cpp`, differing only in
  each aircraft's own `configs/cruise.yaml` numbers) this PR's two plugins follow.
- `docs/architecture.md` §13's `cruise.yaml` excerpt already shows the two commented-out
  `vehicle.models` entries (`physical/aero`, `hardware/propulsion/turbofan` x2 with `station:
  left`/`right`) this PR uncomments, in both the doc and the real file — kept byte-identical per
  §13's own stated invariant ("every key and value identical to the shipped file").
- `cmake/AstraPlugin.cmake`'s `astra_add_plugin()` requires only "at least two levels deep" under
  the repo root and derives target/library names from the full path — the three-level
  `hardware/propulsion/turbofan` path is fine as-is, no cmake change needed.
- No existing test directory `tests/physics/notional_transport/` yet — new, per PLAN.md's PR 4
  acceptance line.

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance command |
|---|---|---|---|
| R1 Aero compute() matches sourced/derived values at cruise trim (tol: 1e-6 rel) | `aircraft/notional_transport/physical/aero/aero.cpp`, hand-built `AeroInputs` test | PR 3 interfaces | `ctest --preset asan -R notional_transport_aero` |
| R2 Aero compute() finite at `true_airspeed_mps=0` | Same file, V_T-linear rate-term form above | R1 | same test binary, dedicated case |
| R3 Propulsion compute() matches derived thrust/moment at cruise (tol: 1e-6 rel) | `aircraft/notional_transport/hardware/propulsion/turbofan/turbofan.cpp` | PR 3 | `ctest --preset asan -R notional_transport_propulsion` |
| R4 Propulsion compute() finite at `true_airspeed_mps=0` | Same file | R3 | same test binary |
| R5 Symmetric equal-throttle net yaw moment ≈ 0 (tol: 1e-9 rel, pure r×F arithmetic) | Same file, two-instance test | R3/R4 | same test binary |
| R6 End-to-end `Simulation::from_config` loads both plugins + shared `controls.throttle` | `aircraft/notional_transport/configs/cruise.yaml` uncommented, integration test mirroring `tests/unit/kernel/aero_propulsion_seam_test.cpp` | R1–R5, PR 2 seam | `ctest --preset asan -R notional_transport_seam` |
| R7 Zero heap allocation, 1000 frames, both new plugins | `tests/physics/no_alloc_test.cpp` extension | R6 | `ctest --preset asan -R no_alloc` |
| R8 `docs/architecture.md` §13 matches shipped `cruise.yaml` exactly | Doc edit only | R6 | `git diff --stat` / manual diff |
| R9 `_template` copies build and throw `ConfigError` until edited | `aircraft/_template/physical/aero/`, `.../hardware/propulsion/turbofan/` | R1–R4's plugin code | existing template-mass test pattern, extended |

One Claude builder; no package split (two plugins, both small, no interface boundary between them
worth a separate writer).

## Validation and risks

Fast: `./scripts/check-fast.sh` during iteration. Full: `./scripts/check-full.sh` before human
review, per Tier 3. Independent oracle: this plan's own worked numbers (traceable to
`docs/refs/aircrafts/737.xml`, cited by tag name since the file is gitignored) plus the S&L
citations above — no separate golden file. Boundary/failure cases: `true_airspeed_mps=0` (R2/R4),
symmetric-thrust yaw cancellation (R5) as an independent sanity check on the r×F sign convention
(catches a left/right sign-flip bug that a single-engine test would miss). No new dependency, no
`tests/golden/**` touch, no ADR change — no separate human-owned-oracle authorization needed beyond
the normal Tier 3 human-review packet.

**Disclosed gaps (labeled, not silently dropped):** no `Cmadot` term (no `alpha_dot` in `AeroInputs`
— see decision 3); no envelope monitors (ADR-0018's own already-deferred gap, still deferred); no
engine-out / asymmetric-thrust modeling; notional thrust magnitude is self-derived from this
airframe's own aero coefficients, not a real CFM56 spec — every config comment and this plan say so
explicitly, matching `cruise.yaml`'s existing "not a validated real Boeing 737" caveat.

## Plan critique

Not requested. Per the skill's own step 5, a critique is for "expensive design not settled by
architecture/ADRs" — this PR's mechanism (how a concrete `AeroModel`/`PropulsionModel` is built) was
already critiqued and pinned by ADR-0018/PR 3; what's new here is airframe-specific numeric modeling
choices Joe has now said not to gate on additional review. If Joe wants one anyway (e.g. on the V→0
algebraic reduction or the force/moment-transfer citation), say so and it can be dispatched before a
branch opens.

## Escalations (per Joe's standing instruction this session)

**None required.** Checked explicitly against the three named triggers:
- *Core interfaces*: no change to `AeroModel`/`PropulsionModel`/`AeroInputs`/`PropulsionInputs`/
  `Wrench`/`WrenchSource`/`InputRegistry`. The one place a core-interface change was considered
  (adding `alpha_dot` to `AeroInputs` for `Cmadot`) was resolved by *not* making that change and
  dropping the term instead — flagged above, not escalated, since the actual decision taken never
  touches core.
- *ADRs*: none superseded, amended, or newly needed. `kCoreApiVersion` does not bump (no vtable
  change).
- *Framework's ability to support higher-fidelity models later*: every simplification here (linear
  coefficients, density-only thrust lapse, dropped ground-effect/flap/gear terms) is confined to
  this one aircraft's own plugin files. `PropulsionInputs` already reserves
  `atmosphere.pressure_pa`/`temperature_k` "for a later, more detailed engine model" (its own header
  comment) — unused by this PR's simple lapse law, not foreclosed by it. A future higher-fidelity
  `notional_transport` variant (or a different aircraft) can implement full table interpolation
  inside its own `compute()` with no framework change, exactly as ADR-0018's rejected-alternatives
  section already establishes for control layouts.

## Done when

R1–R9 pass under all four `just check` presets; `./scripts/check-full.sh` exits 0; `docs/
architecture.md` §13 and `aircraft/notional_transport/configs/cruise.yaml` agree byte-for-byte on
the shared excerpt; independent Codex review (general + adversarial, per the standard `/review-cycle`
this PR still goes through at implementation time) finds no unresolved major/critical finding;
human-review packet prepared; PLAN.md's PR 4 line checked off with the merge SHA, mirroring how PR
0/1/3 are recorded now.
