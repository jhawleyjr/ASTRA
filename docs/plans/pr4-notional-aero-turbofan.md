# Plan: M3 PR 4 — `aircraft/notional_transport/physical/aero` + `hardware/propulsion/turbofan`

**Revision 2, 2026-09-20** — addresses Joe's review of Revision 1 (rebuilt against ADR-0020, merged
`origin/main` as `6590b7c`, PR #40): fixes a real side-force sign bug, replaces a self-referential
test oracle with hand-computed literals, adds an off-nominal test condition, adds a control sign
table, moves V→0 checks to the `wrench_b()` level, drops diagnostic signals and a dangling
dependency, specifies exact equality (not undefined relative tolerance) for every exact-zero expected
component, removes the empty placeholder row from "Mapping and sequence" and renumbers `R1`–`R10`,
and records the plan-critique decision (see "Plan critique"). Frame-conversion math and the AERORP
`r × F` moment transfer are still carried forward from the plan this superseded (Joe: reuse as-is) —
but see "Force/moment assembly" below: the *force* assembly's wind-axis vector is corrected, not
merely carried forward, per Joe's own catch.

Spec/brief: this file (combined spec+plan, Tier 3 — a bounded single-package addition, no new core
interface, no new ADR).
Risk: **3** (`aircraft/**/physical`, `aircraft/**/hardware`, per `docs/process/workflow.md`'s risk
table).
Status: **approved by Joe, 2026-09-20**, conditioned on four edits (all applied in this revision):
the ICD/ADR-0001 sign-convention follow-up written into `PLAN.md` (diff shown separately), the Mach
deviation noted explicitly (Open item 6, Plan critique), exact/absolute tolerance specified for every
zero-valued expected component (Open item 5), and the empty `R4` row removed with renumbering.
Architecture/ADRs: ADR-0001 (frames), ADR-0002 (units/SI), ADR-0016 (per-RK4-substage purity —
`wrench_b()` may be called more than once per stage, must stay `const noexcept`), ADR-0017 (write
seam), ADR-0020 (`AeroModel`/`PropulsionModel` as true interfaces — this PR's actual interface).
System context: two `WrenchSource` plugins loaded into `vehicle.models` (`docs/architecture.md` §7,
§13). `AeroModel`/`PropulsionModel` are minimal marker interfaces with exactly one non-final
`requires_environment()` override each (`{wind: false, atmosphere: true}`); a concrete plugin
overrides `WrenchSource::wrench_b(const VehicleState&, const EnvironmentSample&) const noexcept`
directly, composing whatever `core/include/astra/util/air_data.hpp` utilities it needs. This PR
supplies the only two concrete implementations `aircraft/notional_transport` needs for PLAN.md's M3
trim/linearize/regression milestone (PR 6/7/8).

Per Joe's redirect (unchanged from Revision 1): numeric fidelity does not matter for this notional
airframe — every open modeling choice is decided here, not deferred, and every derived/invented
number is labeled **notional**. No core interface, ADR, or the framework's ability to support a
higher-fidelity model later is touched (see "Escalations": none required).

## Outcome and boundaries

Two new plugins, mirrored into both `aircraft/_template/` (zeroed placeholder, throws `ConfigError`
until edited — exact pattern of PR 1's `physical/mass`) and `aircraft/notional_transport/` (real,
sourced-and-derived numbers in that aircraft's own `configs/cruise.yaml`; plugin `.cpp`/`.hpp`
unchanged between the two, only the class name differs — same separation PR 1 already established
for `mass.cpp`):

- `physical/aero/{aero.cpp, aero_math.hpp}` — a linear-coefficient `AeroModel`.
- `hardware/propulsion/turbofan/{turbofan.cpp, turbofan_math.hpp}` — a linear thrust-lapse
  `PropulsionModel`.

**The `_math.hpp` split is this PR's own answer to "unit-tested with no simulator"** now that core no
longer ships `AeroInputs`/`PropulsionInputs` (PR 3's own promise, which ADR-0020 explicitly asks a
concrete plugin to keep as its own convention, not a core guarantee). Each header declares a
plugin-local `Inputs` struct, a plugin-local `Coefficients` struct (the config-resolved constants),
and a pure, free `compute_wrench(const Inputs&, const Coefficients&) noexcept` function — no
`Config`, no `VehicleState`, no `EnvironmentSample`, no I/O. `aero.cpp`/`turbofan.cpp` `#include` it
and call it from `wrench_b()`; a new unit test `#include`s it directly and calls it with hand-built
`Inputs`/`Coefficients` values, exactly the way `tests/unit/wind/dryden_filter_test.cpp` already
exercises `plugins/physical/wind/dryden/dryden_filter.hpp` with "no Rng, no Simulation, no plugin
instance" (that file's own header comment) — the precedent this PR follows, not a new one it
invents. `aircraft/_template/` ships the identical `_math.hpp` pattern (generic, no notional numbers
baked in) so a new aircraft's own aero/propulsion model starts from something already structured
this way, not just a working `wrench_b()`. (This is the shape the "Plan critique" section below
addresses.)

**Non-goals** (unchanged): table-interpolated (non-linear) aerodynamics; engine spool/N1-N2 dynamics
or fuel flow; ground effect, flaps, gear, speedbrake/spoiler modeling (all zero at the clean-cruise
condition this airframe targets); envelope monitors (§15, still deferred, tracked as the same
PLAN.md follow-up); trim itself (PR 6).

**Assumptions:** `HealthMonitor::check_environment` (ADR-0020, built in PR 3d) already halts, naming
the wind/atmosphere plugin, before any configured `wrench_b()` runs that stage if `wind_n`/
`env.atmosphere` is non-finite — this PR's own plugin-side finiteness check (below) is deliberate
defense-in-depth on top of that guarantee, not a replacement for it, and is what this PR uses to
demonstrate the recommended `air_data::all_finite()` convention for future aircraft authors.
`HealthMonitor::check_input` already halts, naming the input itself, on an unwritten or non-finite
`InputRegistry` entry — this PR's plugins may assume every bound control they read is finite by the
time `wrench_b()` runs. `physical.mass`'s CG is the wrench reference point (§7, unchanged).
`InputRegistry`'s shared-pointer-by-same-name behavior for two plugin instances is already
implemented and tested (PR 2). `station: left|right` is ordinary plugin-defined config, not a
framework concept (confirmed: no framework code anywhere reads `station`).

## Open items — decided here, not escalated

### 1. Thrust source (no CFM56 data exists in `docs/refs/`)

Unchanged finding: `docs/refs/aircrafts/737.xml`'s `<propulsion><engine file="CFM56">` only
*references* an external JSBSim engine-model file; none exists under `docs/refs/`. **Decision**
(unchanged): rated thrust is derived from the cruise drag/thrust equilibrium implied by this same
source's own aerodynamics block, not from any real engine spec — labeled `notional` in both the
config comment and this file. `thrust_ref_n` is sized so **`throttle = 0.5` exactly balances Condition
N's own drag** (see "Worked numbers"), not `throttle = 1.0` (the superseded plan's zero-margin
choice, a defect PLAN.md's PR 4 line already records).

### 2. Tables vs. linearization

**Decision: linearize** (unchanged). PLAN.md's own PR 4 title says "linear-coefficient aero"; PR 7
later linearizes the whole vehicle about trim, so a plugin that is already constant stability
derivatives keeps that step self-consistent. Table-valued source coefficients (`CD0(alpha)`,
`CLalpha(alpha)`, `CDmach(mach)`, `Clda(mach)`, `Cmde(mach)`) are each reduced to one constant,
evaluated at or secant-sloped through the design point (worked below) and then held fixed —
`wrench_b()` never re-interpolates per call. Re-verified this session directly against
`docs/refs/aircrafts/737.xml` (main checkout; gitignored, not in this worktree).

### 3. Terms kept vs. dropped for a clean-cruise notional model

**Unchanged — reused as-is, per Joe's instruction:**

| Source term | Kept? | Why |
|---|---|---|
| `CD0(alpha)` | Kept, as one constant | Evaluated at `alpha=0` breakpoint (0.021) — cruise `alpha` is small (~0.025 rad) so the table's own `alpha=0` value is used directly. |
| `CDi` (induced, `K*CL^2`) | Kept | `K=0.043` is a plain scalar in the source. Primary source of drag-vs-alpha variation in this model. |
| `CDmach` | **Dropped** | Table is exactly 0 and flat from Mach 0.00–0.79; cruise Mach ≈0.77 is inside that flat-zero region — an exact drop, not an approximation. |
| `CDflap`, `CDgear`, `CDsb`, `CDsp` | **Dropped** | All proportional to a control/config position that is exactly 0 at clean cruise. |
| `kCDge`, `kCLge`, `kCLsb`, `kCLsp` | **Dropped (≡1)** | Ground-effect tables only defined to `h_b<=1.1`; 10,000 m altitude is far above ground — modeled as an unconditional multiplier of 1. |
| `dCLflap` | **Dropped** | Flap position 0 at clean cruise. |
| `CDbeta`, `CDde` | **Dropped** | Second-order coupling terms S&L §4.2's classical mode approximations (PR 7's oracle) don't use. |
| `CLalpha(alpha)` | Kept, linearized | Secant slope through `(0, 0.20)`→`(0.23, 1.20)` (4.347826 /rad); cruise trim `alpha` falls inside this bracket, not an extrapolation. |
| `CLde` | Kept, as given (0.2) | Already a plain scalar. |
| `CYb`, `Clb`, `Clp`, `Clr`, `Cldr`, `Cmalpha`, `Cmq`, `Cnb`, `Cnr`, `Cndr` | Kept, as given | Already plain scalars; every one is a named input to S&L §4.2's classical mode approximations, PR 7's oracle. |
| `Clda(mach)`, `Cmde(mach)` | Kept, evaluated at cruise Mach as constants | Linear interpolation at cruise Mach ≈0.77; needed for aileron/elevator effectiveness in PR 6's trim. |
| `Cmadot` (alpha-rate derivative) | **Dropped — disclosed, not silent** | No `alpha_dot` snapshot exists anywhere in `(VehicleState, EnvironmentSample)`; ADR-0020 itself says genuinely supporting it needs ADR-0021's model-owned continuous state (a lagged/filtered alpha state), not a plugin-level change. Stays dropped until PR 4a. |

### 4. Control names/units and sign table

`controls.elevator`, `controls.aileron`, `controls.rudder` — rad. `controls.throttle` —
dimensionless, 0..1, shared symmetrically by both turbofan instances (`InputRegistry`'s existing
same-name dedup — no differential-thrust use case exists yet).

**Sign table (new this revision, per Joe's instruction).** JSBSim's own coefficient tables imply a
definite positive-deflection sense for each surface (a conventionally stable airframe's derivative
signs reveal it). Neither ADR-0001 nor the ICD actually pins one: `icd/astra_icd.h:45`'s comment
("rad, positive per ADR-0001 sign conventions") is circular — ADR-0001 itself only says the
definition is "documented alongside them" (`docs/decisions/0001-frames-and-attitude.md`), and no
such documentation exists anywhere in the repository today. This is a real, pre-existing gap this PR
did not create; it is the first plugin that actually needs the definition, and pins it here by
adopting JSBSim's own sense directly, since there is nothing else to reconcile against:

| Control | Source property | JSBSim's own positive sense (inferred from its own coefficient signs) | ADR-0001/ICD's own definition | Sign applied here |
|---|---|---|---|---|
| Elevator | `fcs/elevator-pos-rad` | Trailing edge down — `Cmde` is negative (-1.20 to -0.30 across Mach): positive elevator gives a nose-down (negative `m`) moment, the classical convention | Not pinned (gap above) | Direct passthrough: `controls.elevator` *is* `fcs/elevator-pos-rad`, no flip |
| Aileron | `fcs/left-aileron-pos-rad` | Positive gives a positive (right-wing-down) rolling moment — `Clda` is positive (+0.100 to +0.033 across Mach) | Not pinned (gap above) | Direct passthrough |
| Rudder | `fcs/rudder-pos-rad` | Trailing edge left — `Cndr` is negative (nose-left/negative yaw for positive rudder, -0.20) and `Cldr` is small positive (+0.01, roll coupling) | Not pinned (gap above) | Direct passthrough |

**Flagged as a PLAN.md follow-up** (same class as PR 1's own `ixy`/`ixz`/`iyz` sign-convention gap
for `plugins/physical/mass/constant`): the ICD's elevator/aileron/rudder comment should eventually
state the physical sense instead of pointing at ADR-0001, which does not itself state it. Not fixed
here — pinning it for this one airframe's own controls does not resolve the framework-wide gap,
which will recur for any future aircraft's own control sign choices.

### 5. Test tolerances

Every numeric assertion below (`Wrench`/moment/thrust) is checked against a **literal** expected
value computed once, in this plan, by an independent re-implementation of the formulas below — never
by the test recomputing "the identical closed form" the plugin's own code computes (Joe's
correction: that description is a self-check, not an independent one; see "Worked numbers" for the
literals themselves). Two tolerances, not one — **relative tolerance is undefined against a zero
target** (Joe's correction: `1e-9 relative` to an expected value of `0.0` degenerates to requiring
exact equality anyway, or is vacuously satisfied depending on how a given `EXPECT_NEAR` treats it —
neither is what "1e-9 relative" was meant to communicate, so it must be stated explicitly, not left
implicit):

- **1e-9 relative** for every nonzero expected component: generous headroom over the
  non-bit-identical rounding a different implementation of the same formula (different operation
  order, no shared code path) can produce, not a fidelity claim.
- **Exact equality (`0.0`, `EXPECT_EQ`)** for every expected component that is precisely zero
  (Condition N's `F_aero_b`'s `y` and `M_cg`'s `x`/`z`; every component of Condition Z's `F_aero_b`/
  `M_cg`; the turbofan symmetric-yaw sum). Justified, not asserted on faith: every one of these zeros
  is produced by multiplying a finite quantity by an exact `0.0` factor (`beta=0`, `aileron=0`,
  `rudder=0`, `p=0`, `r=0` in Condition N; `qbar=0` and the rate term's own explicit `V=0` factor in
  Condition Z; `T + (-T)` for two engines' literally-identical-magnitude, opposite-sign moments in
  the symmetric-yaw case) or by summing two such exact zeros — never a cancellation of two large,
  independently-rounded quantities, which is the one shape floating-point arithmetic cannot promise
  exactly. `x * 0.0 == 0.0` and `x - x == 0.0` are guaranteed by IEEE-754 for any finite, non-NaN `x`
  (this project bans `-ffast-math`, so the compiler cannot reassociate this away), so bit-exact
  equality is a provable property here, not an optimistic tolerance choice.

The V→0 case (Condition Z, below) is therefore asserted against a literal expected value of exactly
zero via `EXPECT_EQ`, not a bare `std::isfinite` check — a strictly stronger assertion the plan's own
literal-input approach makes free to state.

### 6. Where the plugin-side finiteness check sits, and what it checks

ADR-0020 removed the core-mandated, `final`, unconditional `isfinite` guard PR 3's own
`AeroModel::wrench_b()`/`PropulsionModel::wrench_b()` used to run, replacing its no-silent-NaN
guarantee with a core-side `HealthMonitor::check_environment` that runs *before* any `wrench_b()`
call of a stage, checking the raw `EnvironmentSample` for NaN/Inf. What that core check does **not**
cover: a plugin's own arithmetic producing a non-finite result from otherwise-finite inputs, and any
future aircraft's plugin that reads something `check_environment` doesn't validate. Per Joe's
instruction, both plugins keep their own guard, as the demonstrated convention `air_data.hpp`'s own
header comment already recommends.

**Revised per the Codex plan critique (see "Plan critique"): `Inputs` carries only the fields this
plugin's own math actually reads — no `mach`.** **This is a deviation from Joe's original
instruction this session** ("Use `air_data::` for α, β, q̄, and Mach"), accepted explicitly by Joe
after review, not a silent substitution: the original R-001 defect was specific to a design
that used `Mach = true_airspeed_mps / speed_of_sound_m_s` as a math input; this plugin's
linear-coefficient math never uses Mach at runtime (`Clda`/`Cmde` are pre-baked constants, evaluated
once at plan time, not re-interpolated per call — see Open item 2), so computing it at all would be
dead weight with its own unnecessary division. `aero_math.hpp`'s `finite(...)` therefore validates
only the one raw atmosphere field this plugin actually consumes, `density_kg_m3` (via `qbar`), not
`speed_of_sound_m_s`:

```cpp
// R-001 (docs/reviews/pr3-aero-propulsion-review.md) is cited here for the GENERAL lesson, not a
// literal replay of its exact mechanism: checking only a struct's DERIVED fields can let a broken
// RAW field pass silently if some derived quantity happens to mask it algebraically (there, an
// infinite speed_of_sound_m_s divided a finite airspeed down to a finite mach == 0.0). This plugin
// never computes Mach at all (its coefficients are constants, not Mach-scheduled), so that specific
// masking shape cannot recur here -- but the underlying habit (validate the raw field your own math
// actually consumes, not only what you derived from it) still applies to density_kg_m3, the one raw
// atmosphere field this model reads. Core-side HealthMonitor::check_environment (ADR-0020) already
// halts on a non-finite density_kg_m3 before wrench_b() runs; this check is this plugin's own
// second, redundant guarantee.
```

`turbofan_math.hpp`'s own `finite(...)` checks `density_kg_m3` for the identical reason (it never
read `speed_of_sound_m_s` in the first place, so this finding does not change it). On failure: the
identical explicit NaN sentinel `Wrench` ADR-0018's own removed core guard used (never `Wrench{}`'s
zero default, which would read as "this model applies no force").

## Frame-conversion math (worked, reused verbatim — `docs/refs/` is gitignored, re-verified this
session against the source file directly, main checkout)

Source (`docs/refs/aircrafts/737.xml`) locations are in JSBSim's **structural frame**: X aft, Y
right, Z up, inches. ASTRA body frame (ADR-0001) is FRD: X forward, Y right, Z down. Converting a
**delta** between two structural-frame points to body axes: `dx_body = -dx_struct`,
`dy_body = dy_struct`, `dz_body = -dz_struct`; then inches → meters (`x 0.0254`).

`<metrics><location name="AERORP">` = (625, 0, 24) in. `<mass_balance><location name="CG">` = (639,
0, -40) in (identical CG numbers PR 1 already transcribed).

```
delta_struct (AERORP - CG) = (-14, 0, 64) in
r_aero_b (AERORP relative to CG, body axes) = (+14, 0, -64) in = (0.3556, 0.0, -1.6256) m
```

Two `<engine file="CFM56">` blocks, `<thruster file="direct">` at (540, -193, -40) in (feed 0,
"left" — right is `+y` in FRD, negative-y is left) and (540, 193, -40) in (feed 1, "right"):

```
delta_struct, left  (engine - CG) = (-99, -193, 0) in
delta_struct, right (engine - CG) = (-99,  193, 0) in
r_engine_left_b  = (+99, -193, 0) in = (2.5146, -4.9022, 0.0) m
r_engine_right_b = (+99,  193, 0) in = (2.5146,  4.9022, 0.0) m
```

Both thrusters' `<orient>` is `(0, 0, 0)` deg — thrust line parallel to body X:
`F_engine_b = (T, 0, 0)` per engine, `moment_engine_b = r_engine_b × F_engine_b = (0, 0, -r_y * T)`.
This cancels exactly for equal `T` on both engines regardless of `T`'s value (`r_y` is exactly
`∓4.9022` on the two engines) — an algebraic identity, not a numeric coincidence; instantiated with a
literal `T` in "Worked numbers" (`R4`) purely for a concrete regression value.

## Force/moment assembly (S&L citation, resolves "AERORP ≠ CG") — structure reused, **side-force
sign corrected** (Joe's catch)

Verified directly against the PDF this session (`docs/refs/textbooks/...Stevens_Lewis...pdf`,
printed page 78, Table 2.3-1):

- Eq. (2.3-3a), literally: `F_A^w = (-D, -C, -L)`, where `C = q̄ S C_C` (Eq. 2.3-8b) is S&L's own
  **crosswind-force** quantity — not necessarily the same signed quantity as JSBSim's `SIDE` axis.
- **This plugin does NOT substitute `CYb` for S&L's `C_C` and then negate it.** JSBSim's `SIDE` axis
  (`737.xml`'s `<axis name="SIDE"><function name="aero/coefficient/CYb">`) is the force's signed
  **body/wind +Y (right)** component *directly*: `CYb = -1` is negative because a real vertical tail
  generates a force to the LEFT (−Y) under a positive (nose-left, wind-from-the-right) sideslip, and
  JSBSim applies that coefficient with no further sign flip. Using S&L's literal `-C` slot with
  `C_C := CYb` would flip that sense: for `beta = +0.1`, the physically correct (JSBSim-native) side
  force is negative (leftward, restoring); `-C = -(q̄ S CYb beta)` computes a *positive* (rightward)
  force instead — the exact bug Joe caught.
- **This plugin's own wind-axis force vector, stated explicitly, replacing S&L's literal
  `(-D,-C,-L)`:**

  ```
  F_aero_w = (-D, Y, -L),   Y = CYb * beta_rad * qbar * S     (JSBSim's own +right convention)
  D = qbar * S * CD,   L = qbar * S * CL   (unchanged — no equivalent bookkeeping trap: S&L's own
                                             -D/-L slots already match "drag opposes velocity"/"lift
                                             is up," and 737.xml's CD/CL tables are positive exactly
                                             where physically expected)
  ```

- Eq. (2.3-2) (p.77): the wind-from-body-fixed rotation `C_{w/bf}(alpha, beta)` (unchanged), applied
  with the *instantaneous* `alpha_rad`/`beta_rad` from `air_data::angle_of_attack`/`sideslip`.
  `F_aero_b = C_{w/bf}^T * F_aero_w`, using this plugin's own vector above.
- Eq. (2.3-4a)/(2.3-4b) (p.78, re-verified): the roll/pitch/yaw moment coefficients carry **no**
  equivalent sign-bookkeeping trick — `M_A^{bf} = (l, m, n)` directly, `Cl`/`Cm`/`Cn` with no minus
  sign anywhere in the table, unlike the force row. No correction needed for moments.
- Moments computed this way are about **AERORP**, not CG. Transfer:
  `moment_b_cg = moment_native_b + r_aero_b × F_aero_b`.

## Nondimensional-rate V→0 obligation — reused verbatim, attached directly to `wrench_b()`
(ADR-0020, since `compute()` no longer exists as a separate step)

Source terms `Clp`, `Clr`, `Cmq`, `Cnr` each multiply their coefficient by
`qbar * S * L * (L/(2*V_T)) * rate` (`L` = `b` for roll/yaw, `c̄` for pitch). A naive port computes
`(L/(2*V_T))` as its own subexpression: at `V_T=0` this is `+Inf`, and `Inf * 0` (`qbar=0` at
`V_T=0`) yields `NaN`.

**Fix, algebraic:** substitute `qbar = 0.5*rho*V_T^2` and simplify before implementing:

```
term = qbar * S * L * Coeff * (L/(2*V_T)) * rate = 0.25 * rho * V_T * S * L^2 * Coeff * rate
```

Linear in `V_T`, no division anywhere — finite (exactly 0) at `V_T=0` by construction.
`aero_math.hpp`'s `compute_wrench` implements this reduced form directly (never computes
`L/(2*V_T)` as a standalone quantity). `turbofan_math.hpp`'s thrust law (density-ratio only, no
Mach/ram term) has no `V_T` dependence at all — see "Worked numbers, Turbofan Condition Z" for why
this PR checks it once, at the `wrench_b()` level only, not at the pure-math level (Joe: the
pure-math version would be vacuous).

## Worked numbers — literal, hand-computed conditions (Joe's correction: not "illustrative,
re-derived from `isa1976` at implementation time" — these numbers **are** the test oracle)

Every input below is a literal the tests hard-code directly; none is sourced from a real
`AtmosphereModel` call, so there is no "does this match whatever `isa1976` actually returns"
ambiguity. Each set of outputs was computed once here by an independent re-implementation of the
formulas above (not the plugin's own code) — the literal numbers the tests assert against.

Geometry and coefficients (transcribed from `docs/refs/aircrafts/737.xml`, re-verified this session
against the source file directly): `S = 108.78945984 m²` (1171 ft² × 0.09290304),
`b = 28.864560 m` (94.70 ft × 0.3048), `c̄ = 3.752088 m` (12.31 ft × 0.3048),
`r_aero_b = (0.3556, 0.0, -1.6256) m`, `r_engine_left_b = (2.5146, -4.9022, 0.0) m`,
`r_engine_right_b = (2.5146, 4.9022, 0.0) m` (all reused verbatim from "Frame-conversion math").
`CD0=0.021, K=0.043, CL0=0.20, CLalpha=4.347826 /rad (=1.0/0.23), CLde=0.2, CYb=-1.0, Clb=-0.09,
Clp=-0.4, Clr=0.09, Clda=0.0743, Cldr=0.01, Cmalpha=-0.6, Cmq=-27.0, Cmde=-0.8544, Cnb=0.26,
Cnr=-0.35, Cndr=-0.20`.

**Condition N (nominal, wings-level, straight)**: `V=230 m/s, alpha=0.025484 rad, beta=0, p=q=r=0,
elevator=aileron=rudder=0, rho=0.4128 kg/m³` (`alpha`/`rho` are the same illustrative design-point
values Revision 1 used, no longer claimed to equal whatever the real `isa1976` returns at 10,000 m —
they are literal test inputs, full stop).

```
qbar = 0.5*0.4128*230^2 = 10,918.56 Pa
CL = 0.20 + 4.347826*0.025484 = 0.3108 (matches the design CL by construction)
CD = 0.021 + 0.043*CL^2 = 0.0251536555...
D = qbar*S*CD = 29,878.121867... N;  L = qbar*S*CL = 369,175.775231... N;  Y = 0 (beta=0)
```

Literal expected values (what the test hard-codes; tolerance per component per Open item 5 — **bold**
marks the exact-equality components, plain the 1e-9-relative ones):
`F_aero_b = (-20461.3632868957 [rel], **0.0 [exact]**, -369817.2356669478 [rel])` N
`M_cg     = (**0.0 [exact]**, 96622.4239007194 [rel], **0.0 [exact]**)` N·m

**Condition O (off-nominal — Joe's instruction: nonzero β, p/q/r, elevator/aileron/rudder, all at
once)**: `V=100 m/s, alpha=0, beta=0.1 rad, p=0.05, q=-0.02, r=0.03 rad/s, elevator=0.1,
aileron=-0.05, rudder=0.02 rad, rho=1.0 kg/m³` — round numbers for a clean bench condition, not tied
to any altitude or trim; exists only to exercise every term of the general formula at once (`alpha=0`
deliberately, to keep `CL`/`CD` simple while every *other* term is nonzero).

```
qbar = 0.5*1.0*100^2 = 5000 Pa
CL = 0.20 + 4.347826*0 + 0.2*0.1 = 0.22;  CD = 0.021 + 0.043*0.22^2 = 0.0230812
D = 12,554.956402... N;  L = 119,668.405824 N;  Y = qbar*S*CYb*beta = -54,394.729920 N
```

Literal expected values:
`F_aero_b = (-7061.8221796725, -55376.3870330663, -119668.405824)` N
`M_cg     = (-325716.8717090227, -99667.9663201383, 301932.9197071403)` N·m

**Condition Z (V=0 — moved here from a separate pure-math test per Joe's instruction; this is now
`R2`'s own case, not `R1`'s)**: `V=0` (so `alpha=beta=0` too: `v_rel_b=0` gives `atan2(0,0)=0` for
both, never NaN), `p=0.1, q=-0.2, r=0.15` and `elevator=0.3, aileron=-0.2, rudder=0.1` — all
deliberately **nonzero**, since `V=0` alone would make every term trivially zero for the wrong
reason. Every term in the general formula multiplies either `qbar` (zero at `V=0` regardless of
`beta`/controls) or the V-linear rate form `0.25*rho*V*S*L²*Coeff*rate` (zero at `V=0` by its own
explicit `V` factor, regardless of `p/q/r`) — so the expected wrench is **exactly** `(0,0,0)`/
`(0,0,0)`, not merely "some finite value." Asserting the literal zero is strictly stronger than
`std::isfinite` alone (which would also pass a wrong-but-finite answer) and is free once the plan
states inputs and outputs as literals rather than "prove it doesn't NaN."

Literal expected values, **every component exact equality** (Open item 5 — every one is a
zero-times-finite product or a sum of two such products, no cancellation): `F_aero_b = (0.0, 0.0,
0.0)` N, `M_cg = (0.0, 0.0, 0.0)` N·m.

**Notional thrust reference** (Open item 1): `thrust_ref_n := D` from Condition N above =
**29,878.121867743088 N** per engine — chosen so `throttle=0.5` at `rho=density_ref_kgm3=0.4128`
reproduces Condition N's own drag exactly, by construction (`T = 0.5*thrust_ref_n = D/2`, the
per-engine share of total drag at equal throttle). `density_ref_kgm3 = 0.4128`. Thrust law:
`T = throttle * thrust_ref_n * (rho / density_ref_kgm3)`.

**Turbofan Condition N**: `throttle=0.5, rho=0.4128` → `T = 14,939.0609338715 N` [rel, 1e-9].
Symmetric-yaw check (`R4`): `moment_left = r_engine_left_b × (T,0,0) = (0 [exact], 0 [exact],
73234.2645100251 [rel])`, `moment_right = (0 [exact], 0 [exact], -73234.2645100251 [rel])`, sum
`= (0, 0, 0)` **exact equality, all three components** (Open item 5): the `x`/`y` components of each
engine's own moment are individually zero-times-finite products (`r_z=0` for both engines); the `z`
component's sum is `T' + (-T')` for one identical floating-point value `T' = 4.9022*T` computed the
same way on both sides (`r_engine_left_b`/`r_engine_right_b`'s `y` components are exact negatives of
each other as literals) — `x - x == 0.0` exactly in IEEE-754 regardless of `T'`'s own rounding, true
for any equal `T`, instantiated here with Condition N's own `T` for a concrete regression value.

**Turbofan Condition O** (off-nominal throttle/density — a distinct data point exercising the
density-ratio scaling; propulsion has no `beta`/rate/control-surface dependence, so there is no
turbofan analogue of aero's per-term off-nominal sweep): `throttle=0.8, rho=0.5` →
`T = 28,951.6684764952 N`. **Checked at both `R4` (pure math) and `R5` (`wrench_b()`, per the Codex
plan critique)**: Condition N alone uses `rho=0.4128`, identical to `density_ref_kgm3`, so a
`wrench_b()` bug that wired the config's `density_ref_kgm3` into the thrust law instead of the
sampled `env.atmosphere.density_kg_m3` would reproduce Condition N's own answer by coincidence and
pass undetected; Condition O's `rho=0.5 != density_ref_kgm3=0.4128` forces the two to diverge, so
`R5` needs it too, not only `R4`.

**Turbofan Condition Z** (V=0, at the `wrench_b()` level only — `R5`; deliberately **not** duplicated
at the pure-math level, `R4`, because the thrust law has no `V` term to break in the first place, so
a pure-math V=0 case would prove nothing Condition N doesn't already prove — Joe: drop the vacuous
one): `throttle=0.6, rho=0.4128` → `T = 17,926.8731206459 N` (unaffected by `V`, by construction).

## Config keys (both plugins; `_template`'s copy ships all-zero/TODO, throwing `ConfigError` until
edited, exact pattern of PR 1's `physical/mass`)

`physical/aero` — prefixes resolve the `CL` (lift) vs. `Cl` (rolling moment) collision that would
otherwise both lowercase to `cl_`: `wing_area_m2`, `wingspan_m`, `mean_chord_m`, `r_aero_b_x_m`,
`r_aero_b_y_m`, `r_aero_b_z_m` (three scalar keys, not a sequence — `Config` has no vector-of-double
getter outside `Simulation::from_config`'s own private `read_vector3`, and `hardware/propulsion/
turbofan` already needs the same per-axis-scalar shape for its own offsets) — `cl_0`,
`cl_alpha_per_rad`, `cl_elevator_per_rad` (LIFT) — `cd_0`, `cd_k` (DRAG) — `cy_beta_per_rad` (SIDE)
— `croll_beta_per_rad`, `croll_p_per_rad`, `croll_r_per_rad`, `croll_aileron_per_rad`,
`croll_rudder_per_rad` (ROLL, `Cl`) — `cm_alpha_per_rad`, `cm_q_per_rad`, `cm_elevator_per_rad`
(PITCH) — `cn_beta_per_rad`, `cn_r_per_rad`, `cn_rudder_per_rad` (YAW). Validated positive at init:
`wing_area_m2`, `wingspan_m`, `mean_chord_m` (a zero/negative geometry is never physical — the same
"fails loudly until edited" story `physical/mass`'s `mass > 0` check gives an un-edited template).

`hardware/propulsion/turbofan` — `station` (string, label only, not read for geometry),
`x_offset_from_cg_m`, `y_offset_from_cg_m`, `z_offset_from_cg_m` (explicit per-instance numbers, no
"left implies negative y" logic in the plugin), `thrust_ref_n`, `density_ref_kgm3`. Validated
positive at init: `thrust_ref_n`, `density_ref_kgm3` (the latter also guards against a
divide-by-zero in the thrust law, not only "un-edited template" hygiene).

No diagnostic signals are published by either plugin this PR (Joe: drop them). Neither plugin
registers anything beyond what `WrenchSource`/`AeroModel`/`PropulsionModel` already require.

## Repository findings

- `AeroModel`/`PropulsionModel` (ADR-0020, `core/include/astra/interfaces/{aero,propulsion}_model.hpp`)
  are minimal marker interfaces, unchanged by this PR. `air_data.hpp`
  (`core/include/astra/util/air_data.hpp`) ships `relative_velocity_b`, `true_airspeed`,
  `angle_of_attack`, `sideslip`, `dynamic_pressure`, `mach`, `all_finite` — read directly this
  session. Aero's `wrench_b()` uses `relative_velocity_b`/`true_airspeed`/`angle_of_attack`/
  `sideslip`/`dynamic_pressure`, **not** `mach` (Codex plan-critique finding, resolved — Open item 6:
  this plugin's coefficients are Mach-evaluated constants, never re-interpolated per call, so
  computing Mach at all was dead weight with its own unnecessary division). Turbofan's own math
  needs none of `air_data::`'s functions (density-ratio-only thrust law), so `turbofan_math.hpp` has
  no `air_data.hpp` dependency at all.
- `InputRegistry`/`bind_inputs()` (PR 2) already gives two same-named registrations the same shared
  pointer — no code change needed for symmetric throttle.
- `tests/plugins/aero_stub/aero_stub.cpp`, `tests/plugins/propulsion_stub/propulsion_stub.cpp` (both
  rebuilt under ADR-0020 in PR 3d, read directly this session) are the exact structural pattern
  (`bind_inputs()`, `wrench_b()` overriding directly, anonymous namespace, `PluginDescriptor`) this
  PR's two real plugins mirror — neither stub demonstrates the `_math.hpp` pure-helper split (they
  are deliberately minimal test fixtures), so this PR is the first to build that pattern out.
- `plugins/physical/wind/dryden/dryden_filter.hpp` + `tests/unit/wind/dryden_filter_test.cpp` (read
  directly this session) is the exact precedent for a plugin-adjacent header included by both its
  own `.cpp` and an independent unit test with no plugin instance — confirmed the include resolves
  because `astra::icd` (linked transitively through `astra_core_headers`) exposes the whole repo
  root as an include directory (`icd/CMakeLists.txt:5`; verified against
  `compile_commands.json`'s actual `-I/home/joe/repos/ASTRA` for that exact test file).
- `aircraft/notional_transport/physical/mass/mass.cpp` + `aircraft/_template/physical/mass/mass.cpp`
  (read directly this session) are the exact `_template`-vs-real-aircraft separation pattern
  (byte-identical `.cpp`, differing only in each aircraft's own `configs/cruise.yaml` numbers, plus
  an init-time positive-value check that makes an un-edited template throw) this PR's two plugin
  pairs follow.
- `docs/architecture.md` §13's `cruise.yaml` excerpt already shows the two commented-out
  `vehicle.models` entries this PR uncomments, in both the doc and the real file — kept
  byte-identical per §13's own stated invariant.
- `tests/unit/kernel/notional_transport_cruise_config_test.cpp` (read directly this session)
  currently asserts the shipped cruise config loads and steps with `vehicle.models: []` (free-fall,
  its own docstring says so explicitly). Once this PR uncomments the aero/turbofan entries, that
  config needs a new `vehicle.initial_controls` block (elevator/aileron/rudder default to 0.0,
  throttle to 0.5 matching the half-throttle trim design point — not a claim this is trimmed, PR 6
  still solves that) or the very first frame halts on `HealthMonitor::check_input` naming an
  unwritten control. This test file's own docstring and body need updating to match (no longer
  "free-falls" — it now flies, un-trimmed, under real aero+thrust).
- `cmake/AstraPlugin.cmake`'s `astra_finalize_plugins()` hardcodes which test executables get a
  plugin-build-order dependency (PLAN.md's own already-tracked follow-up item, not fixed here, same
  as PR 1 before it): this PR's four plugin-loading executables need adding to that list (see `R10`
  below for names), exactly as PR 1 already had to add its two mass-test executables. The two
  pure-math executables (`R1`/`R4`) do not load a plugin dynamically and need no such dependency.
- PLAN.md's own PR 4 acceptance line names `tests/physics/notional_transport/` for the
  hand-picked-condition tests that "exercise the rebuilt aero/propulsion `wrench_b()` overrides
  directly" — that is exactly `R2`/`R5` below (the `wrench_b()`-level tests), so they are placed
  there, matching PLAN.md's existing wording with **no PLAN.md edit needed**. The pure-math tests
  (`R1`/`R4`, which exercise `compute_wrench` directly, not `wrench_b()`) and the `_template`
  ConfigError tests (`R9`) are a different concern PLAN.md's line doesn't name; they follow the
  `tests/unit/mass/` precedent instead (new `tests/unit/aero/`, `tests/unit/turbofan/`
  directories). Test *count* also reconciles without an edit: PLAN.md says "2-3 hand-picked
  conditions" — Conditions N and O above are 2, within that range; Condition Z (V→0) and the R-001
  NaN-sentinel case are separate acceptance clauses in PLAN.md's own sentence structure ("**and**
  ... returns a finite `Wrench` at `true_airspeed_mps == 0`, proven by its own test"), not counted
  among the "2-3."

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance command |
|---|---|---|---|
| R1 Pure aero math (`compute_wrench`) matches Conditions N and O (tol: 1e-9 rel except the exact-zero components, Open item 5) | `aircraft/notional_transport/physical/aero/aero_math.hpp`, `tests/unit/aero/notional_transport_aero_math_test.cpp` | none | `ctest --preset asan -R notional_transport_aero_math` |
| R2 Aero plugin `wrench_b()` end-to-end (real `Config`, hand-built `VehicleState`/`EnvironmentSample`) reproduces Conditions N, O, and Z (V=0, exact equality) | `aircraft/notional_transport/physical/aero/aero.cpp`, `tests/physics/notional_transport/aero_wrench_test.cpp` | R1 | `ctest --preset asan -R notional_transport_aero_wrench` |
| R3 Aero plugin `wrench_b()` returns the NaN sentinel on an infinite `density_kg_m3` with an otherwise-finite sample (raw-not-derived check, revised per Codex — see Open item 6) | Same file as R2 | R2 | same test binary |
| R4 Pure turbofan math matches Conditions N and O; symmetric equal-throttle net yaw moment = 0 exactly (tol: 1e-9 rel except the exact-zero components, Open item 5; no V=0 case — vacuous, see "Worked numbers") | `aircraft/notional_transport/hardware/propulsion/turbofan/turbofan_math.hpp`, `tests/unit/turbofan/notional_transport_turbofan_math_test.cpp` | none | `ctest --preset asan -R notional_transport_turbofan_math` |
| R5 Turbofan plugin `wrench_b()` end-to-end reproduces Condition N **and** Condition O (Codex finding: R4 alone cannot catch `wrench_b()` wiring the config's `density_ref_kgm3` in place of the sampled `env.atmosphere.density_kg_m3`, since Condition N's own density equals the reference density and would pass either way); finite (exactly Condition Z's value) at `true_airspeed_mps=0` | `aircraft/notional_transport/hardware/propulsion/turbofan/turbofan.cpp`, `tests/physics/notional_transport/turbofan_wrench_test.cpp` | R4 | `ctest --preset asan -R notional_transport_turbofan_wrench` |
| R6 `Simulation::from_config` loads the real `cruise.yaml` (both plugins uncommented, `initial_controls` added) and steps 1000 frames with no halt | `aircraft/notional_transport/configs/cruise.yaml`, `tests/unit/kernel/notional_transport_cruise_config_test.cpp` (updated) | R2, R5, PR 2 seam | `ctest --preset asan -R notional_transport_cruise_config` |
| R7 Zero heap allocation, 1000 frames, the real cruise config | `tests/physics/no_alloc_test.cpp` (new case, loads `cruise.yaml` via `ASTRA_AIRCRAFT_DIR`) | R6 | `ctest --preset asan -R no_alloc` |
| R8 `docs/architecture.md` §13 matches shipped `cruise.yaml` exactly | Doc edit only | R6 | manual diff / `git diff --stat` |
| R9 `_template` copies build and throw `ConfigError` until edited (`wing_area_m2`, `thrust_ref_n`/`density_ref_kgm3`) | `aircraft/_template/physical/aero/`, `.../hardware/propulsion/turbofan/`, `tests/unit/{aero,turbofan}/template_*_test.cpp` | R1–R5's plugin code | existing `template_mass_test.cpp` pattern, extended |
| R10 `cmake/AstraPlugin.cmake`'s `astra_finalize_plugins()` test-executable list includes the four new plugin-loading executables (`astra_notional_transport_aero_wrench_test`, `astra_template_aero_test`, `astra_notional_transport_turbofan_wrench_test`, `astra_template_turbofan_test` — the two `_math_test` executables load no plugin and need no entry) | `cmake/AstraPlugin.cmake` | R2, R5, R9 | `just check` from a clean `build/` |

One Claude builder; no package split (two plugins, both small, no interface boundary between them
worth a separate writer).

## Validation and risks

Fast: `./scripts/check-fast.sh` during iteration. Full: `./scripts/check-full.sh` before human
review, per Tier 3. Independent oracle: the literal Condition N/O/Z values in "Worked numbers" above
(traceable to `docs/refs/aircrafts/737.xml`, cited by tag name since the file is gitignored, plus the
S&L citations) — no separate golden file. Boundary/failure cases: Condition Z (`true_airspeed_mps=0`,
exact zero expected, R2/R5), symmetric-thrust yaw cancellation (R4/R5) as an independent sanity check
on the r×F sign convention, an infinite `density_kg_m3` with an otherwise-finite sample (R3, the
raw-not-derived check Open item 6 describes) as the concrete regression this plugin's own redundant
guard exists for, and Condition O checked at both `R4` and `R5` for turbofan (Codex plan-critique
finding, resolved) so a `wrench_b()` wiring bug that swaps the sampled density for the config's own
reference density cannot hide behind a pure-math-only check. No new dependency, no `tests/golden/**`
touch, no ADR change — no separate human-owned-oracle authorization needed beyond the normal Tier 3
human-review packet.

**Disclosed gaps (labeled, not silently dropped):** no `Cmadot` term (no `alpha_dot` anywhere in
`(VehicleState, EnvironmentSample)` — decision 3; needs ADR-0021); no envelope monitors (already
deferred, still deferred); no engine-out/asymmetric-thrust modeling; notional thrust magnitude is
self-derived from this airframe's own aero coefficients, not a real CFM56 spec; ADR-0001/ICD's own
elevator/aileron/rudder positive-sense definition does not exist yet, pinned here via JSBSim's own
convention (Open item 4) — every config comment and this plan say so explicitly.

## Plan critique

**Run, not waived.** Per the skill's own trigger ("an expensive design not settled by
architecture/ADRs") and Joe's instruction this round: the plugin-side `_math.hpp`
(`Inputs`/`Coefficients` struct + pure `compute_wrench`) is the *first* concrete instance of a
convention ADR-0020 only sketched, and every future aircraft's aero/propulsion plugin will likely
copy whatever shape this PR ships. Dispatched via `/codex:rescue --fresh --wait`, scoped to the
`_math.hpp` shape specifically (not the numeric choices, which Joe reviewed directly). Verdict:
"Retain the pattern," with two findings, both accepted and resolved in this revision:

- **Finding 1 (structural)**: `Inputs` included `mach`, a diagnostic dependency this plugin's own
  math never actually reads (its coefficients are Mach-evaluated constants, not runtime-scheduled) —
  an unnecessary division and failure mode a future, copied plugin would inherit for no reason.
  Resolved: `mach` removed from `Inputs`; the finiteness check narrowed to the raw field this plugin
  actually consumes, `density_kg_m3` (Open item 6, Repository findings). **Accepted by Joe as a
  deviation from his own original instruction** to use `air_data::` for Mach — recorded here and in
  Open item 6, not applied silently.
- **Finding 2 (test-coverage)**: the acceptance pattern left the turbofan `wrench_b()`-level test
  (`R5`) exercising only Condition N, whose density happens to equal the config's own
  `density_ref_kgm3` — a wrapper bug that wired the reference density into the thrust law instead of
  the sampled one would pass undetected. Resolved: Condition O (a differing density) is now checked
  at both `R4` (pure math) and `R5` (`wrench_b()`) — "Worked numbers," `R5`'s own row.

Every other question the critique checked (seam/ownership between `Inputs`/`Coefficients`/
`wrench_b()`, naming/namespace isolation, finiteness-check placement, `all_finite()` usage, the NaN
sentinel, the Dryden precedent, and the tolerance scheme) came back clean, with the note that the
plan text doesn't literally show `finite(...)`'s body or a namespace declaration — both are
implementation detail this plan intentionally leaves to the builder, consistent with every other
plugin's own `.cpp` in this repository never being fully transcribed into its plan. This is a
plan-level critique, not an implementation review — the standard `/review-cycle` (general +
adversarial) still runs against the actual code at implementation time, per "Done when."

## Escalations (per Joe's standing instruction this session)

**None required.** Checked against the three named triggers:
- *Core interfaces*: no change to `AeroModel`/`PropulsionModel`/`WrenchSource`/`InputRegistry`/
  `air_data.hpp`. `Cmadot` stays dropped rather than adding an `alpha_dot` snapshot anywhere core.
- *ADRs*: none superseded, amended, or newly needed. `kCoreApiVersion` does not bump (no vtable
  change — `AeroModel`/`PropulsionModel` gain no new override).
- *Framework's ability to support higher-fidelity models later*: every simplification here (linear
  coefficients, density-only thrust lapse, dropped ground-effect/flap/gear terms, the specific
  `_math.hpp` shape) is confined to this one aircraft's own plugin files (and `_template`'s generic
  copy). A future higher-fidelity variant implements full table interpolation inside its own
  `wrench_b()`/its own differently-shaped `_math.hpp` with no framework change.

## Done when

R1–R10 pass under all four `just check` presets; `./scripts/check-full.sh` exits 0; `docs/
architecture.md` §13 and `aircraft/notional_transport/configs/cruise.yaml` agree byte-for-byte on
the shared excerpt; independent Codex review (general + adversarial, per the standard `/review-cycle`
this PR still goes through at implementation time) finds no unresolved major/critical finding;
human-review packet prepared; PLAN.md's PR 4 line checked off with the merge SHA, mirroring how PR
0/1/3/3d are recorded now.
