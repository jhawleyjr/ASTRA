# Feature: M3 PR 6 — `astra.trim`

Status: approved (Revision 2)
Owner: Joe
Risk: Tier 2 — feature + one new dev dependency, no `core/`, `plugins/physical`, `aircraft/**/physical`,
`aircraft/**/hardware`, or ICD change (workflow.md's Tier 3 row does not apply; this is pure
`python/astra` + `tests/python` + `pyproject.toml`).
Intent/approval: Joe's session instruction (2026-09-21), D8 residual settled, unknowns/fixed
variables settled, four acceptance tests named. PLAN.md's own PR 6 line: "Plan critique: no" — the
design is pinned by PR 5's ADR-0021 surface (`derivative(state)`, `write_input`), no new core/ADR
surface is introduced here.
Architecture/ADRs: ADR-0021 (state layout, R4 named slices), ADR-0017 (write seam), ADR-0001
(frames/quaternion), ADR-0002 (units, no suffixes). No new ADR.
System context: `astra.trim` is a pure-Python consumer of PR 5's bindings only
(`Simulation.derivative`, `Simulation.write_input`, `Config.from_dict`, the `astra.VELOCITY`/
`astra.ANGULAR_RATE` slice constants) — it adds no core/plugin surface and calls nothing PR 5 does
not already expose. It is the first of three M3 tools in this shape (`docs/plans/pr5-nanobind-
bindings.md`'s own module list: trim, linearize, montecarlo, log, scenario, fgbridge); PR 7
(`astra.linearize`) will call `astra.trim.trim_straight_and_level` to get its own operating point,
so this PR's return type is shaped for that reuse without adding anything PR 6 itself doesn't need.

## Outcome and boundaries

Solve for the four numbers (elevator, throttle, alpha, theta) that zero PR 5's `derivative(state)`
at `aircraft/notional_transport/configs/cruise.yaml`'s cruise condition (TAS 230 m/s, 10,000 m,
straight-and-level), using `scipy.optimize.root`. This is a static, algebraic solve — no simulation
time advances, no `sim.step()`/`sim.run()` is ever called on the trim's own `Simulation` instance.

Non-goals (out of scope for this PR): general multi-condition/multi-aircraft trim API; lateral-
directional unknowns (`phi`, `beta`, aileron, rudder are pinned to 0 by the scenario, not solved);
any wind/environment coupling (still air only, per Joe's instruction); adding `elevator_min/max` or
`throttle_min/max` as new plugin config keys (see D10 below — this PR does not touch
`aircraft/**/physical` or `aircraft/**/hardware` at all, which is what keeps it Tier 2).

Assumptions: `cruise.yaml`'s `vehicle.models`/`physical`/`initial_controls` blocks already build a
valid, steppable `Simulation` (proven by PR 4/PR 4a/PR 5's own tests — not re-proven here).
`Simulation.derivative(state)` is bit-identical to the real integrator's own k1 (PR 5's R9,
independently Codex-verified) — this PR trusts that contract rather than re-deriving the EOM.
`write_input`'s existing raise-on-unregistered-name and raise-on-non-finite-value behavior
(`tests/python/test_write_input.py`) is the safety net that catches a wrong or stale control name;
this PR does not add its own duplicate name-validation. `wgs84_j2` gravity and `isa1976` atmosphere
give finite, real values at 10,000 m (exercised by the M2 NESC harness and M3 PR 4/4a already; not
re-validated here).

## Contract and failure behavior

**Residual (D8):** the 6-vector `[v̇_b, ω̇_b]`, read from `derivative(state)` via the named slices
`astra.VELOCITY`/`astra.VELOCITY_SIZE` and `astra.ANGULAR_RATE`/`astra.ANGULAR_RATE_SIZE` (PR 5,
ADR-0021 R4 — the same constants `tests/python/test_derivative.py`'s R8 test already proves
authoritative). `ṗ_n`/`q̇_nb` are excluded. `residual = np.concatenate([v_dot, omega_dot])`;
acceptance metric is `np.max(np.abs(residual))`, no weighting, no unit scaling — S&L §3.6 (main
checkout, `~/repos/ASTRA/docs/refs/textbooks`) uses a weighted sum-of-squares of state derivatives
as its own trim-residual form; this PR's own simplification drops the weights and the squaring
(max-abs of un-weighted, mixed-unit terms is not "smaller is better" in a squared-error sense, but
is a strictly simpler pass/fail gate that avoids inventing a weighting scheme PLAN.md's own D8 note
already rejected raw SSE for: the state mixes m/s² and rad/s² terms, and a sum would let one
dominate silently — max-abs makes the binding term visible instead).

**Unknowns and fixed scenario:** TAS = 230 m/s, altitude = 10,000 m, `beta = phi = psi = 0`,
`omega_b = 0`, `aileron = rudder = 0`, still air. `gamma = 0` (straight-and-level) makes
`theta = alpha`, so the nominal 4-unknown/6-residual problem collapses to the square 3-unknown
(`elevator`, `throttle`, `alpha`) / 3-residual (`v̇_x`, `v̇_z`, `q̇`) problem `scipy.optimize.root`
actually solves; `v̇_y`, `ṗ`, `ṙ` are asserted small by symmetry at the solution, not solved for --
heading north (`psi = 0`) at latitude 0 puts `wgs84_rotating`'s Earth-rate vector parallel to the
velocity vector, so the Coriolis term vanishes for the lateral axis; `ixz` is this aircraft's only
off-diagonal inertia term (`ixy = iyz = 0`, `cruise.yaml`), coupling only x/z, so a pure `M_y`
pitching moment (no `M_x`/`M_z`, guaranteed by `beta = aileron = rudder = 0`) gives `q̇ != 0` but
`ṗ = ṙ = 0` through the rigid-body inertia coupling itself, not approximately. A reviewer should
read a loud failure of this specific assertion as evidence one of those preconditions (heading,
latitude, zero side-force/moment, or the inertia tensor's own off-diagonal terms) isn't actually
holding at runtime — not as a tolerance to loosen.
State is built from `alpha`: `v_b = [V cos α, 0, V sin α]` (body axes, `beta = 0`); `q_nb` from
`euler_nb = [0, α, 0]` — collapses `frames.hpp::quaternion_from_euler`'s general Stevens & Lewis
eq. 1.8-16 formula to `q_nb = [cos(α/2), 0, sin(α/2), 0]` when `psi = phi = 0` (verified directly
against the real formula in R3 below, not re-derived from scratch). `p_n` is held at `cruise.yaml`'s
own `initial.state.p_n` (`[0, 0, 10000]`, `wgs84_rotating` lat/lon/altitude) — position is fixed,
not solved.

**Still air construction — D9, approved:** `cruise.yaml` itself configures
`physical.wind: plugins/physical/wind/dryden` (a real, load-bearing part of the cruise config, not
an oversight). `Simulation.derivative()` raises on a wind-configured, never-stepped `Simulation`
(PR 5's R7a, `test_derivative_raises_on_a_never_stepped_simulation_with_wind_configured` — this is
"by design," per Joe's own instruction, not a bug to work around by stepping). Since the trim's
`Simulation` is never stepped, `cruise.yaml` cannot be handed to `Config.from_dict`/`from_file`
unmodified — the `physical.wind` key must be absent, not merely quiesced. `Config` has no
Python-exposed way to enumerate a map's keys or build a modified copy (`core/include/astra/config/
config.hpp`: `at`/`has`/`size`/`is_map`/`is_sequence`/`is_scalar`/typed scalar getters only, no
key-listing or merge API) — round-tripping an arbitrary `Config` tree into a Python dict without one
is not possible without guessing each scalar's original YAML type. **Decided**: add `pyyaml` to the
`dev` dependency group — `pyproject.toml`'s own dev-group comment already names it as an
anticipated future addition ("scipy, h5py, pandas, pyarrow, pyyaml ... join when a milestone uses
them"); PR 6 is that milestone. `astra/trim.py` reads `cruise.yaml` with `yaml.safe_load`, deletes
the `physical.wind` key if present, and passes the rest to `Config.from_dict` — `cruise.yaml` stays
the single source of truth for every other model/coefficient/initial-condition number; only the
wind entry is dropped, in Python, not by hand-editing a second copy of the file. **Pin version at
implementation time, not from this plan**: confirm the actual current release (`pip index versions
pyyaml`, or the resolver's own pick) when `pyproject.toml` is actually edited, rather than trusting
this planning session's own snapshot (`6.0.3`, checked 2026-09-21) — record in the implementation
commit what was pinned and why. Same treatment for `scipy` (this plan's own snapshot: `1.18.1`).

**Control ranges — D10, approved as recommended:** Joe's instruction says "read them from the registry" for
control *names* — **not actually satisfied, corrected per Joe's implementation review**:
`write_input`'s existing raise-on-an-unregistered-name behavior is a runtime *guard* against a wrong
or stale literal string, not a *read* of the registry's own names. `trim.py` hardcodes the literal
strings `"controls.elevator"`/`"controls.throttle"`, read once from `aero.cpp`'s/`turbofan.cpp`'s
own `bind_inputs()` source at planning time, not discovered at runtime. Accepted as a deviation for
this PR (the Python binding exposes no `InputRegistry::names()` enumeration — it exists in C++, PR
2, but isn't bound); PLAN.md's PR 7 line carries the follow-up to bind it so trim/linearize can
discover control names instead of hardcoding them. This deviation is orthogonal to "read [ranges] from the
plugin configs" for the elevator/throttle *bounds* — but neither bound is actually declared
anywhere the plugins or `cruise.yaml` can be read from. Checked directly: `turbofan.cpp`'s own
comment states throttle is deliberately *not* range-checked by the plugin ("Not range-checked
against [0, 1] ... the thrust law is linear in throttle with no clamp"); the only documented
convention is `icd/astra_icd.h:46`'s comment, `throttle[2]; /* 0..1 per engine */` — an ICD
convention, not a config value. Elevator has no declared numeric range anywhere in this repository
at all — `icd/astra_icd.h:45` documents sign convention only (PLAN.md's own open item), and neither
`aero.cpp` nor `cruise.yaml` carries an `elevator_min`/`max` key. The one real numeric source is
`docs/refs/aircrafts/737.xml` (main checkout) itself, whose `<flight_control>` block's "Elevator
Control" `aerosurface_scale` (lines 201–206) clamps `fcs/elevator-pos-rad` to `[-0.3, 0.3]` rad —
the same transcription source PR 1/PR 4's own coefficients already cite. **Decided**: no new
`elevator_min`/`max` config keys on the aero plugin (that would be an `aircraft/**/physical` change,
Tier 3, out of proportion to a plausibility-bound check) — hardcode `elevator ∈ [-0.3, 0.3]` rad
(cite `docs/refs/aircrafts/737.xml:201-206`) and `throttle ∈ [0, 1]` (cite `icd/astra_icd.h:46`) in
`tests/python/test_trim.py` itself, not silently invented, and not sourced from a plugin config key
that doesn't exist. **PLAN.md follow-up** (one line, not this PR's scope): this is the second
control-surface property — after the positive-deflection-sense gap already on PLAN.md — with no
declared home in the ICD or any plugin config; added to PLAN.md's unchecked-items list alongside it.

**Failure behavior:** if `scipy.optimize.root` reports `success=False`, `trim_straight_and_level`
raises `RuntimeError` naming the scipy message and the last residual — never returns a
`TrimResult` claiming convergence it didn't reach. `write_input`'s own raises (unregistered name,
non-finite value) propagate unchanged; this module adds no new exception type.

**Determinism:** `scipy.optimize.root(method="hybr")` (MINPACK `hybrd`) is a deterministic
algorithm with no RNG; once `physical.wind` is dropped (D9), every remaining model in this config
(`wgs84_rotating` earth, `wgs84_j2` gravity, `isa1976` atmosphere, the notional aero/mass/turbofan
plugins) is a pure, seedless function of state. Bitwise-identical output across two runs from the
same guess is therefore expected structurally, not merely hoped for — R4 below tests it directly
anyway, per Joe's explicit acceptance requirement, and to catch an accidental cross-call state leak
the way PR 5's own R5a/R5b probe-isolation tests do.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | Residual < 1e-6 at cruise | `pytest tests/python/test_trim.py::test_trim_converges_at_cruise` — `trim_straight_and_level(cruise.yaml, 230.0, 10000.0)`, assert `np.max(np.abs(result.residual)) < 1e-6`. |
| R2 | Residual is discriminating | `pytest tests/python/test_trim.py::test_untrimmed_guess_fails_by_a_wide_margin` — same still-air config, `derivative()` evaluated once at `cruise.yaml`'s own untrimmed `initial.state`/`initial_controls` (alpha=0, elevator=0, throttle=0.5), assert `np.max(np.abs(residual)) > 1e-2`. Hand-estimate (cited in the test): at alpha=0, `CL = cl_0 = 0.20` against a required `CL ≈ 0.31` for level flight at this q̄/S/weight — a lift deficit of order 10⁵ N, order 1 m/s² of `v̇_z` — four-plus orders of magnitude above the 1e-6 bound and above the 1e-2 threshold. |
| R3 | Physically plausible solution | `pytest tests/python/test_trim.py::test_solution_is_physically_plausible` — `0 <= alpha_deg <= 10`; `-0.3 <= elevator_rad <= 0.3` (D10, `docs/refs/aircrafts/737.xml:201-206`); `0.0 <= throttle <= 1.0` (D10, `icd/astra_icd.h:46`); combined `WrenchSource` body-x force at trim equals `+m g sin(alpha)` (the exact EOM identity — `rigid_body_6dof_dynamics.cpp`'s `v̇_x = F_x/m + g_b_x`, `g_b` from `frames.hpp`'s real `dcm_from_euler`/`rotate_n_to_b`, not a small-angle approximation) to within `m * 1e-6` — **stated as the EOM identity for a reviewer's benefit, but not independent**: R1's own residual bound already guarantees it algebraically, so this is documentation, not a second check. **The genuinely independent cross-check** is the classical wind-axis relation `T cos(alpha) = D` (exact at `gamma = 0`, `omega_b = 0`, `beta = 0` — combining the body-x *and* body-z EOM equations, not just x alone; a first draft of this plan used `T = D + m g sin(alpha)`, which double-counts the weight term already absorbed into `D`'s own equilibrium via the z-equation and was wrong by ~31% against the worked numbers below — corrected per Joe's review): hand-compute `CL = W cos(alpha)/(q̄S)`, `CD = cd_0 + cd_k·CL²`, `D = CD·q̄·S`, all typed directly from `cruise.yaml`'s own transcribed coefficients, never reusing `aero_math.hpp`/`turbofan_math.hpp` code; assert `abs(throttle · thrust_ref_n · (ρ/ρ_ref) · cos(alpha) - D) / D < 0.05`. Worked numbers (this session, ISA-1976 estimate at 10,000 m — confirm against the real `isa1976` plugin at implementation time, not copied from here): `ρ ≈ 0.4127 kg/m³`, `q̄ ≈ 10,916 Pa`, `CL ≈ 0.3108`, `CD ≈ 0.02515`, `D ≈ 29,868 N`, `alpha ≈ 1.46°`, `T = D/cos(alpha) ≈ 29,878 N` — matching `cruise.yaml`'s own `thrust_ref_n = 29,878.12` N almost exactly (that file's own comment: `thrust_ref_n` was *sized* to this exact equilibrium at throttle 0.5), i.e. thrust and drag agree to a few hundredths of a percent here, comfortably inside the 5% bound; the bound stays at 5% (not tightened to match this specific coincidence) because it is meant to catch a wrong-sign or mis-transcribed coefficient, not to certify this one config to hand-calculation precision. |
| R4 | Determinism | `pytest tests/python/test_trim.py::test_two_runs_from_the_same_guess_are_bitwise_identical` — two independent `trim_straight_and_level` calls, same config/guess; `np.array_equal(result_a.state, result_b.state)` and `result_a.elevator_rad == result_b.elevator_rad and result_a.throttle == result_b.throttle` (exact `==`, not `np.isclose` — same bitwise-identity convention as PR 2/PR 5's own determinism tests). |

Two supporting (not separately named by Joe, but needed to make R1–R4 trustworthy) checks belong in
the same test file: (a) the collapsed-quaternion helper (`_state_from_alpha`'s `q_nb` construction)
matches `Simulation.from_config`'s own real `initial.state.euler_nb` construction at 2–3 sample
angles, proving the collapsed formula against the actual bound implementation rather than a
hand-typed duplicate; (b) the still-air config genuinely has no `physical.wind` — implicit in every
other test (a leaked wind key would make R1/R2/R4 raise immediately, per PR 5's R7a), but worth one
direct assertion (`_load_still_air_config(...)` output has no wind key reachable, or equivalently a
`Simulation` built from it does not raise on the very first `derivative()` call with no prior
`step()`).

## Questions and decision

| Question | Consequence | Owner | Answer/reference |
|---|---|---|---|
| D9: add `pyyaml` to the dev group to build the still-air config from `cruise.yaml`? | Blocks how `astra/trim.py` loads/mutates the config; the only dependency-free alternative is regex text-surgery on a shared config file (rejected). | Joe | **Approved, 2026-09-21.** Pin the actual current release at implementation time (`pip index versions pyyaml`), not this plan's snapshot (`6.0.3`); record what was pinned and why. |
| D10: hardcode elevator `[-0.3, 0.3]` rad (737.xml:201-206) / throttle `[0, 1]` (icd/astra_icd.h:46) in the test, rather than adding config keys to the aero/turbofan plugins? | Keeps this PR Tier 2 (no `aircraft/**/physical`/`hardware` edit). | Joe | **Approved as recommended, 2026-09-21.** Add a one-line PLAN.md follow-up noting this as the second undeclared control-surface property (not this PR's scope). **Also, per Joe's implementation review**: control *names* are hardcoded too, not read from the registry as originally instructed (`write_input`'s raise is a guard, not a read) — accepted for PR 6 since the binding exposes no `InputRegistry::names()`; PLAN.md's PR 7 line gets the follow-up to bind it. |

Both were open only until Joe's review above; nothing in R1–R4's own numbers depended on either
answer except the still-air config loader (D9) and the R3 range bounds/cross-check (D10), both now
unblocked.

---

# Implementation plan: `astra.trim`

Spec/brief: this file (combined, per plan-feature step 3's "a small change can keep spec and plan
together in a short brief").
Risk: Tier 2 (see above).
Status: approved (Revision 2)
Plan approval: **Joe approved Revision 2, 2026-09-21** (D9/D10 above, plus R3's thrust-cross-check
fix, the symmetry rationale, and the convergence-tolerance note below).

## Repository findings

- `python/astra/__init__.py` already documents `trim` as one of the pure-Python tools "later
  milestones" add — `python/astra/trim.py` is the expected home, no package-layout decision needed.
- `Simulation.derivative(state)`, `Simulation.write_input(name, value)`, `Config.from_dict`,
  `Config.from_file`, and the module-level `astra.VELOCITY`/`VELOCITY_SIZE`/`ANGULAR_RATE`/
  `ANGULAR_RATE_SIZE`/`STATE_SIZE` constants are all bound and tested (`python/bindings/_core.cpp`
  lines 187–195, 466–471; `tests/python/test_derivative.py`, `tests/python/test_write_input.py`) —
  no C++/binding change needed for this PR.
- `aircraft/notional_transport/configs/cruise.yaml` is the acceptance fixture as-is, with one
  caveat: `physical.wind` (dryden) must be dropped for the trim's own `Simulation` (D9). Its
  `initial.state`/`initial_controls` blocks double as R2's untrimmed-guess fixture with no
  duplicated numbers.
- `aero.cpp`'s/`turbofan.cpp`'s `bind_inputs()` register `"controls.elevator"`, `"controls.aileron"`,
  `"controls.rudder"` (aero) and `"controls.throttle"` (turbofan, shared by both `station` instances
  by name) — confirmed by reading the plugin source directly, not assumed; `write_input`'s existing
  raise-on-unregistered-name test (`test_write_input_raises_on_an_unregistered_name`) is the runtime
  guard against these ever drifting silently.
- `frames.hpp::quaternion_from_euler` (Stevens & Lewis eq. 1.8-16) collapses algebraically to
  `[cos(α/2), 0, sin(α/2), 0]` when `psi = phi = 0` (worked by hand this session, confirmed against
  the actual formula at lines 32–41, not assumed) — R3's cross-check proves this against the real
  binding rather than trusting the by-hand algebra alone.
- `rigid_body_6dof_dynamics.cpp:39-44` rotates gravity into body axes via the real `q_nb`
  (`frames::rotate_n_to_b`), not a small-angle formula — confirms `v̇_x = 0 ⟹ F_x = m g sin(alpha)`
  exactly (to the residual's own 1e-6 tolerance) at this PR's `phi = 0` scenario. Combined with the
  `v̇_z = 0` equation (`F_z = -m g cos(alpha)`) and `aero_math.hpp:117-124`'s own `force_w -> force_b`
  rotation (`force_b_x = -D cos(alpha) + L sin(alpha)`, `force_b_z = -D sin(alpha) - L cos(alpha)`),
  the two combine algebraically to the classical wind-axis identity `T cos(alpha) = D` — this is
  R3's genuinely independent cross-check; the `F_x = m g sin(alpha)` identity alone is not
  independent of R1's own residual bound and is documented as such, not used as a second check.
- No `elevator_min`/`max` or `throttle_min`/`max` config key exists anywhere (D10) — checked
  directly in `aero.cpp`, `turbofan.cpp`, and `cruise.yaml`, not assumed from the settled-decision
  text; PLAN.md gets a one-line follow-up noting this alongside the existing positive-deflection-
  sense gap.
- `Config` (`core/include/astra/config/config.hpp`) has no key-enumeration or dict-export API —
  checked directly; this is why D9 exists at all.
- `pyproject.toml`'s `dev` group presently has no analysis libraries; its own comment anticipates
  `scipy`/`pyyaml` joining "when a milestone uses them" — this PR is that milestone for both.
- `tests/python/` convention: one file per binding surface, `PHYSICAL`/`_sim()` fixture helpers
  local to the file, docstring at top listing every named requirement the file covers (followed
  here for `test_trim.py`).

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance command |
|---|---|---|---|
| Still-air config loader (D9) | New: `python/astra/trim.py::_load_still_air_config` | `pyyaml` (pending D9), `Config.from_dict` (PR 5, stable) | Implicit in every test below (a leaked wind key raises immediately per PR 5's R7a) |
| Residual extraction | New: `python/astra/trim.py::_residual6` | `astra.VELOCITY`/`ANGULAR_RATE` constants (PR 5, stable) | Exercised by R1/R2 |
| State construction from alpha | New: `python/astra/trim.py::_state_from_alpha` | `frames.hpp` collapse (verified, not core-side) | New: `test_trim.py::test_state_from_alpha_matches_the_real_quaternion_construction` |
| `trim_straight_and_level` + `TrimResult` | New: `python/astra/trim.py` | `scipy.optimize.root` (new dep, approved), `Simulation.write_input`/`derivative` (PR 5, stable) | R1 |
| Untrimmed-guess fixture | Reuses `cruise.yaml`'s own `initial.state`/`initial_controls` | none new | R2 |
| Plausibility bounds (D10) | `tests/python/test_trim.py` constants, cited to `docs/refs/aircrafts/737.xml` and `icd/astra_icd.h` | none new (no plugin/config change) | R3 |
| Determinism | `tests/python/test_trim.py` | none new | R4 |
| Dependency pin | `pyproject.toml` `dev` group: `scipy`, `pyyaml` (D9, approved) — both pinned to the actual current release confirmed at implementation time, not this plan's snapshot | Joe's approval (given) | `just venv` installs cleanly; pinned versions and the reason recorded in the implementation commit |

One Claude builder; no split (the whole surface is one small module plus one test file, no
independent-path parallelism to gain).

## Validation and risks

Fast gate: no C++/core change, so `scripts/check-fast.sh`'s ASan/unit-test portion is unaffected by
this PR's own diff (still run once to confirm no incidental regression). Full gate
(`scripts/check-full.sh`/`just check`) still required per Tier 2's own row (ruff/pyright/pytest
portions are what actually exercise this PR; the C++ presets/tidy portions should be unchanged
no-ops). `pytest tests/python/test_trim.py -v` is the PR's own acceptance command; the full
`pytest` run (`tests/python/`) must stay green alongside it — no existing test should be touched.

Risks: (1) D9/D10 are resolved (above); nothing left blocked. (2) `scipy.optimize.root`'s `hybr`
(MINPACK `hybrd`) should be *expected*, not merely risked, to need tighter-than-default tolerances:
`∂v̇_z/∂alpha ≈ q̄ S cl_alpha / m ≈ 1,187,468 · 4.34783 / 37,648 ≈ 137 m/s² per rad` (worked this
session from `cruise.yaml`'s own numbers), and `hybr`'s default `xtol` (MINPACK's `1.49012e-08`)
bounds the solution vector's own step size, not the residual directly — a step of that size in
`alpha` alone projects to a residual of order `137 * 1.5e-8 ≈ 2e-6`, landing right at R1's `1e-6`
line, not comfortably under it. Implementation must tighten `xtol` (tried in order: default,
`1e-10`, `1e-12`) until R1's bound holds with real margin, and must assert on the actual computed
`max(abs(residual))` after the solve every time — never on `sol.success` alone, which reports
`xtol`-convergence, not the physical residual bound this PR actually promises. (3) This session's
own hand-worked numbers (R2's `1e-2` margin, R3's `5%` cross-check and its worked `ρ`/`q̄`/`CL`/`CD`/
`D`/`T` values) are estimates, not yet run against the real implementation — flagged per STATE.md's
own honesty rule ("a trace matched" is not "verified"); the implementation session must re-derive
or confirm these against the real solve before treating them as final, not merely copy them from
this plan (this is also why R3's `5%` bound must not be widened if the real solve doesn't match
this session's own arithmetic — a mismatch there is a defect to find, not a tolerance to relax).
(4) No golden/tolerance file is touched — the oracle-guard hook does not apply to this PR.

No compatibility/migration/rollback concerns (new module, no existing caller). No documentation
change beyond this plan and the PLAN.md/STATE.md bookkeeping already committed this session.

## Plan critique and decisions

No Codex plan critique (PLAN.md's own PR 6 line: "Plan critique: no" — this PR introduces no new
core/plugin interface, ADR, or unpinned numerical design; D9/D10 are dependency/scope-tier calls for
Joe directly, not the "expensive, unpinned design" class workflow.md's critique trigger targets).

## Done when

R1–R4 pass under `pytest tests/python/test_trim.py`; the full gate (`just check`) is green;
`scipy`/`pyyaml` are pinned in `pyproject.toml` at their actual current-release versions confirmed
at implementation time (D9); the PLAN.md follow-up item (D10) is added; Codex general review
(`/review-cycle`) has run and any findings are resolved or explained (Tier 2's own row: "independent
review, human packet" applies even with no plan critique); the human-review packet is prepared
(`/human-review`) before Joe's merge decision.
