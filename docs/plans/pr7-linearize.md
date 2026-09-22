# Feature: M3 PR 7 — `astra.linearize`

Status: approved (Revision 3)
Owner: Joe
Risk: Tier 2 — feature, no `core/kernel/`, `plugins/physical`, `aircraft/**/physical`,
`aircraft/**/hardware`, or ICD change. The one `python/bindings/_core.cpp` addition (binding
`InputRegistry::names()`) is a pure-copy, no-lifetime-hazard read (see Contract, below) added to an
existing bound method's own "first vehicle" pattern — not the ownership/lifetime class of change
that made PR 5 itself Tier 3, and no `kCoreApiVersion` bump (no ABI/vtable change, purely additive
Python surface over already-existing C++ methods).
Intent/approval: Joe's session instruction (2026-09-21); D11/D12/D13 settled (state form,
differencing steps, mode formulas/tolerances); the `InputRegistry::names()` binding is PR 6's own
carried-forward follow-up (`docs/plans/pr6-trim.md`, D10; PLAN.md's PR 7 line). **Revision 2 approved
by Joe, 2026-09-21**, three revisions: (D14) `LinearizeResult` exposes the actual differenced
`Simulation` instance, not a separately built one, so R6 inspects real state; (D15) column isolation
(R6) is checked after each individual column during differencing, via a separately callable internal,
not only once after `linearize()` returns; (D16) roll/spiral compare the signed `Re(lambda)`/`1/tau`
directly, never `abs(tau)` — a sign disagreement (the spiral mode may be mildly unstable at cruise) is
a failure, not a magnitude-only comparison. **Revision 3 approved by Joe, 2026-09-21**, after running
R4 for real against `cruise.yaml` and finding three of five modes missed their bound (D18, below):
not a widened tolerance and not a new physics extension, but a correction to D13's own closed-form
*input* list. `aero_math.hpp`'s own header (lines 14-15) states its `cm_alpha_per_rad`/
`croll_beta_per_rad`/`cn_beta_per_rad` are tabulated about AERORP and transferred to the CG via
`moment_b = moment_native_b + r_aero_b.cross(force_b)` (`r_aero_b` itself a `cruise.yaml` coefficient,
lines 26-28) — S&L Sec. 4.2's own dimensional derivatives are about the CG throughout, so the
closed-form comparison needs the CG-referenced coefficients, not the raw tabulated ones D13's own
list named. PLAN.md's own PR 7 line: "Plan critique: no" — S&L pins
both the differencing method and the mode-approximation forms; no new core/plugin interface or
unpinned numerical design is introduced here.
Architecture/ADRs: ADR-0021 (state layout, `derivative()`, R4 named slices), ADR-0017 (write seam —
`InputRegistry::names()`, PR 2), ADR-0001 (frames/quaternion — ported to Python here, not bound; the
one core convention this module must reproduce exactly). No new ADR.
System context: `astra.linearize` is a pure-Python consumer of PR 5's bindings (`Simulation.
derivative`, `Simulation.write_input`, this PR's own new `Simulation.input_names`) plus PR 6's
`astra.trim.trim_straight_and_level`, whose `TrimResult` supplies the operating point this PR
differences around — the reuse PR 6's own plan flagged this module for. It is the second of the
three M3 Python tools in this shape (`docs/plans/pr5-nanobind-bindings.md`'s module list: trim,
linearize, montecarlo/log/scenario/fgbridge later). PR 8 consumes this PR's `A`/`B` (specifically the
phugoid mode) to cross-check its own 300 s open-loop regression run.

## Outcome and boundaries

Central-difference `A`/`B` about `astra.trim.trim_straight_and_level`'s cruise solution, in the
Euler-angle state form D11 pins, and report the five classical modes (short period, phugoid, Dutch
roll, roll, spiral) computed both numerically (eigenvalues of `A`'s own longitudinal/lateral blocks)
and via S&L §4.2's closed-form approximations, so a test can compare the two. Includes PR 6's own
follow-up: bind `InputRegistry::names()` so this module (and a future retrofit of `astra.trim`, not
this PR) can read control names from the registry instead of a literal.

Non-goals: general multi-condition/multi-aircraft linearization API (one `linearize()` call, one
config, one flight condition, matching `astra.trim`'s own shape); retrofitting `astra.trim`'s own
hardcoded control-name literals to `input_names()` (PLAN.md's own new follow-up line, commit
`73ffe43` — out of scope here, `astra.trim` is unmodified by this PR except as an imported callable);
a re-solve of trim (differencing is about `trim_straight_and_level`'s already-returned point, never a
second `scipy.optimize.root` call); position/altitude as a state (D11: dropped, altitude-density
coupling is negligible over a mode's own period and S&L's own approximations assume it away).

Assumptions: `astra.trim.trim_straight_and_level` already returns a converged `TrimResult` whose
`residual` is `< 1e-6` (PR 6's own R1, not re-proven here — this PR's own R3 below re-checks the
residual at the *linearization's own* re-encoded state, since D11's Euler-angle `x`/quaternion
reconstruction is a different code path from `TrimResult.state` itself, not a re-trim). `Simulation.
derivative(state)` is bit-identical to the real integrator's own k1 (PR 5's R9, inherited, not
re-proven). `frames.hpp::quaternion_from_euler` (eq. 1.8-16) is the single core-side reference this
module's own Python port must match — not independently re-derived, ported line for line and cited
the same way `astra.trim._state_from_alpha`'s docstring already does for its own (collapsed)
special case.

## Contract and failure behavior

**State and control vectors (D11):** `x = [u, v, w, p, q, r, phi, theta, psi]` (9,) — body velocity
`v_b`, body rate `omega_b`, then Euler angles in `(phi, theta, psi)` order (note: `frames.hpp`'s own
`EulerAngles`/`quaternion_from_euler` argument order is `(psi, theta, phi)`, matching the state
vector's own stored `euler_nb` construction order — this module's own `x` vector uses `(phi, theta,
psi)` order instead, D11's own choice, so every place `x`'s Euler slice is fed into the ported
quaternion formula must permute it, not pass it straight through — flagged here so the
implementation doesn't silently transpose two unrelated conventions). `u = [elevator, aileron,
rudder, throttle]`, in `Simulation.input_names()`'s own registration order (verified below, not
assumed) — `x`/`u` never carry position or altitude.

**Citation correction (repository finding, not a question — resolved here, not deferred to Joe):**
D11 says "per S&L §4.1"; read directly against `docs/refs/textbooks/`'s own copy (main checkout) at
planning time, §4.1 is "Introduction" (historical prose, no equations) and §4.2 is "Aircraft
Rigid-Body Modes" — the closed-form approximations D13 already correctly cites. The actual sources
for D11's own two formulas are: eq. **1.8-16** (quaternion from Euler angles, Ch. 1 §1.3, the same
equation `frames.hpp::quaternion_from_euler` already cites and this module ports) and eq. **1.4-4**
(the Euler kinematical equations, Ch. 1 §1.4 — `[phi_dot; theta_dot; psi_dot] = H(Phi) [P; Q; R]`,
`H`'s rows `[1, sin(phi)tan(theta), cos(phi)tan(theta)]`, `[0, cos(phi), -sin(phi)]`, `[0, sin(phi)/
cos(theta), cos(phi)/cos(theta)]`) for the Euler-rate rows D11 calls "the standard kinematic matrix."
The central-difference *method* itself (D12) is S&L §3.7 "Numerical Linearization" (eq. 3.7-1 through
3.7-3 — this module implements the two-point form, eq. 3.7-3, not the four-point eq. 3.7-4 higher-
order form D12 doesn't ask for). Cite 1.8-16/1.4-4/§3.7 in the implementation, not §4.1.

**The function being differenced, `f(x, u)` (9,):** build a full combined state from `x` (fixed
`p_n` at the trim's own value; `v_b`/`omega_b` from `x` directly; `q_nb` from `x`'s `(phi, theta,
psi)` via the ported eq. 1.8-16), write `u`'s four components through `Simulation.write_input` (one
call per component, same pattern `astra.trim` already uses), call `sim.derivative(state)`, and read
back `v_dot`/`omega_dot` via the named `VELOCITY`/`ANGULAR_RATE` slices (`astra.trim._residual6`'s
own pattern, reused, not re-derived). The Euler-rate three rows of `f` are **not** read from
`derivative()`'s own `q_dot_nb` slice at all — D11 is explicit that they come from `omega_b` through
eq. 1.4-4, evaluated at `x`'s own current `(p, q, r, phi, theta)`, a closed-form kinematic identity
independent of the aerodynamic/EOM derivative. `f(x, u) = concatenate([v_dot, omega_dot,
euler_dot])`.

**Differencing (D12):** one central-difference column per state/control component, step sizes fixed
by D12 (`0.1` m/s for `v_b`; `1e-3` rad/s for `omega_b`; `1e-3` rad for the three Euler angles and the
three surface controls; `1e-2` for throttle) — no step-halving search (unlike `astra.trim`'s own
`xtol` search; D12 pins the steps directly, R1 below only checks the *result's* sensitivity to
halving them, it does not make the implementation itself adaptive). `A[:, i] = (f(x0 + dx_i·e_i, u0)
- f(x0 - dx_i·e_i, u0)) / (2·dx_i)`; `B[:, j]` likewise over `u0 ± du_j·e_j`.

**Column isolation, testable seam (D15):** the per-column routine is its own internal, callable
independent of the full `linearize()` sweep — `linearize._column(sim, p_n, x0, u0, index, step,
kind)` (`kind` is `"x"` or `"u"`), performing exactly one column's plus-evaluation,
minus-evaluation, central difference, **and then explicitly restoring `u0`'s own four components via
`Simulation.write_input`** before returning — the one place D12's "trim inputs are restored after
every column" is actually enforced in code, not merely true as a side effect of always passing `u0`
to `_evaluate` on an `"x"`-kind column (which it also does, but a `"u"`-kind column's own two
evaluations leave a perturbed component latched until this explicit restore runs). `linearize()`
itself is `_column` called once per state index (9) and once per control index (4), assembling `A`/
`B` column by column — not a second, differently structured implementation. R6 drives `_column`
directly, once per index, asserting the registry equals `u0` after each call (not only after the
whole sweep finishes) — an end-only check is inspection, not evidence of column-by-column isolation.

**Trim reuse and the result type (D14):** `linearize()` calls `astra.trim.trim_straight_and_level
(config_path, ...)` once, builds its own `Simulation` for the differencing (same still-air-config
loading `astra.trim` already does — reusing `trim._load_still_air_config`, a same-package private
import, not a duplicated second copy of the wind-dropping logic), and builds `x0` from the
`TrimResult`'s own `state`/`elevator_rad`/`throttle` (`v_b`/`omega_b` read from `state`'s named
slices; `(phi, theta, psi) = (0, alpha_rad, 0)` by `trim_straight_and_level`'s own scenario, not
re-derived by inverse-quaternion — trim's own `_state_from_alpha` docstring already establishes this
collapse). `u0 = [elevator_rad, 0.0, 0.0, throttle]` (aileron/rudder pinned to 0 by the same
straight-and-level scenario). `TrimResult` itself carries no `Simulation` (PR 6 never needed one past
its own solve), so `LinearizeResult` exposes the actual differenced instance directly:

```
@dataclass(frozen=True)
class LinearizeResult:
    trim: trim.TrimResult
    simulation: _core.Simulation   # the instance _column actually perturbed -- for R6, not a
                                    # separately built "same config" instance, which would pass
                                    # column-isolation checks vacuously
    x0: np.ndarray                 # (9,) [u, v, w, p, q, r, phi, theta, psi]
    u0: np.ndarray                 # (4,) [elevator, aileron, rudder, throttle]
    input_names: list[str]         # sim.input_names(), registration order
    a: np.ndarray                  # (9, 9)
    b: np.ndarray                  # (9, 4)
```

**`Simulation.input_names()` (the PR 6 follow-up, this PR's scope addition):** new nanobind free
function in `python/bindings/_core.cpp`, mirroring `simulation_derivative`'s own "free function over
`sim.vehicles().front()`" shape (not a new `astra::Simulation` C++ method — no header/source change
under `core/` at all): calls `sim.vehicles().front().inputs().names()`
(`InputRegistry::names() const`, already existing, PR 2) and converts each `std::string_view` into an
owned Python `str` at the point of return — safe by construction, not merely by convention: unlike
`PyHaltReason::source` (a `std::string_view` stored in a long-lived bound *struct field*, read lazily
whenever Python touches the property, which is why that one needed an eager-copy wrapper type),
`input_names()` is an ordinary bound *function* whose `std::vector<std::string_view>` return value is
converted to Python objects immediately, inside the call, while the owning `Simulation`/`Vehicle`/
`InputRegistry` is still alive on the stack above it — no property, no deferred read, nothing to
dangle. Returns `list[str]`, a copy (Joe's own scope note) — order is `InputRegistry`'s own
registration order (verified in Repository findings below: aero's `bind_inputs()` registers
elevator/aileron/rudder, turbofan's registers throttle, and `cruise.yaml`'s own `vehicle.models` list
puts the aero entry before both turbofan entries — so `input_names()` on `cruise.yaml` is exactly `u`'s
own D11 order, checked directly, not assumed). No `kCoreApiVersion` bump — additive Python surface
over existing C++ methods, no vtable/ABI change.

**Modes (D13):** `A_long = A[np.ix_([0,2,4,7], [0,2,4,7])]` (u, w, q, theta), `A_lat = A[np.ix_([1,3,5,6],
[1,3,5,6])]` (v, p, r, phi) — `psi` (index 8) belongs to neither block, matching S&L's own reduced
state (2.6-5a drops `psi` from the linearized dynamics at `psi_e = 0`). Eigenvalues via
`numpy.linalg.eigvals` on each block. Short period is the complex pair with the larger `|lambda|`
(faster mode), phugoid the smaller; Dutch roll is the lateral block's one complex pair; roll is the
real eigenvalue with the larger `|Re(lambda)|` (faster/shorter time constant), spiral the smaller —
this classification-by-magnitude rule is this session's own expectation from S&L's physical
description ("rolling mode is much faster than spiral," Ch. 4 prose), not yet checked against
`cruise.yaml`'s own computed eigenvalues; the implementation must verify the ordering holds for the
real numbers and raise loudly (not silently mislabel) if two candidates are ambiguously close, rather
than picking one arbitrarily. `omega_n = |lambda|`, `zeta = -Re(lambda)/|lambda|` for each complex
pair.

**Roll/spiral sign (D16):** report and compare `Re(lambda)` directly (equivalently `1/tau =
Re(lambda)`) for both real poles, sign included — **never `abs(tau)`**. The spiral mode may be mildly
unstable at cruise (a small positive `Re(lambda)`, i.e. a formally negative time constant under the
usual "decay time" convention); collapsing to `abs(tau)` would let an unstable numerical spiral match
a stable closed-form prediction of similar magnitude, or vice versa, and hide exactly the sign
disagreement that matters physically. `LinearizeResult`'s own mode-report carries
`sigma_roll_per_s`/`sigma_spiral_per_s` (`Re(lambda)`, signed, 1/s) rather than a `tau` field that
would need its own sign convention documented and re-documented at every call site.

**Closed-form comparison (D13, S&L §4.2), computed in the test from `cruise.yaml`'s own coefficients,
not the implementation module:**
- Short period: eq. **4.2-11**/**4.2-12**, using `cl_alpha_per_rad`, `cm_alpha_per_rad`,
  `cm_q_per_rad`, `wing_area_m2`, `mean_chord_m`, `iyy`, `mass`, and dynamic pressure/density at the
  trim point. `Cm_alpha_dot` (the alpha-rate pitch-damping term eq. 4.2-12's own numerator carries)
  is **0** — `aircraft/notional_transport/physical/aero` has no such coefficient (PLAN.md's own open
  item, M3 PR 4: "`Cmadot`/alpha-rate damping still dropped"), and this is the *consistent* choice,
  not a simplification introduced here: the numerical `A`/`B` this test compares against was built
  from the same aero model, so it has no alpha-rate feedback either.
- Phugoid: eq. **4.2-16** (or the simplified level-flight eq. **4.2-17a**/**4.2-17b**, since
  `trim_straight_and_level`'s own scenario is `gamma_e = 0`) for `omega_n`; eq. **4.2-18** (the
  level-flight simplified damping form, since eq. 4.2-15b's own general form needs thrust/drag
  speed-derivatives this notional model's linear coefficients don't carry either — no Mach/speed
  dependence in `cd_0`/`cd_k` or `thrust_ref_n`) for `zeta`. Exact substitution algebra (which terms
  are genuinely zero for this model, beyond the ones named here) must be re-derived against
  `cruise.yaml`'s real numbers at implementation time, not assumed from this plan alone — flagged in
  Validation and risks below, same posture PR 6's plan took for its own hand-worked R3 numbers.
- Dutch roll: eq. **4.2-30**/**4.2-31**, the unprimed-dimensional-derivative forms (not eq.
  4.2-28b/4.2-29's `J_xz`-corrected "primed" forms) — a deliberate simplification, not an oversight:
  the unprimed forms are what a test can build directly from `cruise.yaml`'s own dimensionless
  `cn_beta_per_rad`/`cn_r_per_rad`/`cy_beta_per_rad` without a second inertia-axis-rotation
  implementation, and D13's own 30%/0.15 Dutch-roll tolerance (looser than the longitudinal modes')
  already exists because these unprimed approximations are known to be the less accurate of the two
  forms S&L itself gives (§4.2's own "Accuracy of the Lateral-Mode Approximations").
- Roll/spiral: eq. **4.2-35**/**4.2-36**, using `croll_p_per_rad`, `croll_beta_per_rad`,
  `croll_r_per_rad`, `cn_beta_per_rad`, `cn_r_per_rad` — evaluated as the literal signed algebraic
  quotients the equations give (never wrapped in `abs()`), then compared as `1/tau_approx` against
  the numerical `Re(lambda)` directly (D16): a sign mismatch between the two is a failure regardless
  of how small the magnitude difference is, never masked by comparing `abs(tau)` instead.

**CG-referenced dimensional derivatives (D18, Revision 3):** running R4 against the real
`cruise.yaml` numbers (evidence below) found short period, phugoid, and spiral outside D13's own
tolerance; short period and spiral were traced to a real, verified defect in D13's own coefficient
list, not the differencing code (`test_the_trim_points_own_derivative_is_near_zero` (R3),
`test_longitudinal_and_lateral_blocks_do_not_cross_couple` (R2), and a hand check of the numerical
`dM/dw` against the derivation below to four significant figures all independently confirm
`astra.linearize` itself is correct). `aircraft/notional_transport/physical/aero/aero_math.hpp`'s
own header (lines 14-15) states plainly: its `Cl`/`Cm`/`Cn` coefficients are computed about AERORP
(the source aircraft's own moment reference point) and transferred to the CG via
`moment_b = moment_native_b + r_aero_b.cross(force_b)` (`cruise.yaml`'s own `r_aero_b_x_m`/
`r_aero_b_z_m`, lines 26-28, `r_aero_b_y_m = 0`) -- a real, first-order effect for this aircraft's own
substantial CG-to-AERORP offset. S&L Sec. 4.2's own dimensional derivatives (`M_alpha`, `L_beta`,
`N_beta`, the ones eq. 4.2-11/4.2-30/4.2-33-36 actually use) are about the CG throughout (S&L's own
Table 2.6-1, "the derivative symbols X/Y/Z... indicate which force component," all evaluated at the
vehicle's own reference point, which S&L never separates from the CG). D13's own coefficient list
named the raw, AERORP-referenced `cm_alpha_per_rad`/`croll_beta_per_rad`/`cn_beta_per_rad` directly
-- a defect in that list, not a new physics extension: the fix re-derives the CG-referenced
coefficients the equations actually need, from first principles, in the test, and feeds them into
the *same, unchanged* eq. 4.2-11/4.2-12/4.2-30/4.2-31/4.2-33-36 -- no new equation, no change to any
tolerance.

*Sign conventions cited* (independently re-derived here, never calling `aero_math.hpp` or
`astra.linearize`): body axes are FRD (ADR-0001); the moment-transfer theorem is the standard
`moment_about_CG = moment_about_reference_point + r_(reference->CG-corrected, i.e. reference-relative-
to-CG) x force`, matching `aero_math.hpp`'s own `r_aero_b.cross(force_b)` (`r_aero_b` is AERORP
relative to CG, so this *adds* the transfer term when moving the reference from AERORP to CG, exactly
as coded); the aero plugin's own force sign convention (`aero_math.hpp`'s `compute_wrench`, S&L eq.
2.3-2's `C_{w/bf}` transform), collapsed at `beta = 0` (this PR's own linearization point for every
`alpha`-only column, and the trim point itself): `Fx_b = -D cos(alpha) + L sin(alpha)`,
`Fz_b = -D sin(alpha) - L cos(alpha)`, `Fy_b = -D sin(beta) + Y cos(beta)` with `Y = qbar S
Cy_beta_per_rad beta` (`Fy_b(0) = 0`, matching R2's own decoupling result: no beta-dependence enters
`Fx`/`Fz` at `beta = 0`, and no alpha-dependence enters `Fy` at `beta = 0` -- confirmed algebraically
below, not merely assumed, which is *why* the longitudinal/lateral blocks stay decoupled under this
correction too).

*Pitch (affects `Cm_alpha` only -- `Cm_q` has no transfer term, since `p`/`q`/`r` never enter this
aero model's force equations, confirmed directly in `aero_math.hpp`: only `alpha`/`beta`/the surface
deflections feed `force_b`)*: with `CL = cl_0 + cl_alpha alpha` (elevator held at its trim value),
`CD = cd_0 + cd_k CL^2`, `D = qbar S CD`, `L = qbar S CL`,

```
dFx/dalpha = D sin(alpha) - cos(alpha) dD/dalpha + L cos(alpha) + sin(alpha) dL/dalpha
dFz/dalpha = -sin(alpha) dD/dalpha - D cos(alpha) - cos(alpha) dL/dalpha + L sin(alpha)
Cm_alpha_cg = cm_alpha_per_rad + (r_aero_b_z_m * dFx/dalpha - r_aero_b_x_m * dFz/dalpha)
              / (qbar * S * mean_chord_m)
```

*Roll and yaw (affects `Cl_beta`/`Cn_beta` only -- `Cl_p`/`Cl_r`/`Cn_r` likewise have no transfer
term)*: `dFy/dbeta|_{beta=0} = -D + qbar S cy_beta_per_rad` (drag rotating into the y-axis as beta
grows, plus the side-force derivative itself),

```
Cl_beta_cg = croll_beta_per_rad + (-r_aero_b_z_m * dFy/dbeta) / (qbar * S * wingspan_m)
Cn_beta_cg =    cn_beta_per_rad + ( r_aero_b_x_m * dFy/dbeta) / (qbar * S * wingspan_m)
```

`Cl_beta_cg`/`Cn_beta_cg`/`Cm_alpha_cg` replace the raw coefficients as *inputs* to eq. 4.2-11/
4.2-12/4.2-30/4.2-31/4.2-33-36 -- every equation and tolerance in D13 stands unchanged.

*Before/after, measured this session against real `cruise.yaml` numbers at cruise (evidence, not a
claim -- superseded by the actual `pytest` run once the test is updated to match):*

| Mode | Numerical | Approx. (native coeffs, D13) | Approx. (CG-referenced, D18) | D13 bound | Verdict |
|---|---|---|---|---|---|
| Short period `omega_n` | 1.000 rad/s | 1.277 (21.7% off) | 0.998 (0.2% off) | 15% | **fixed** |
| Short period `zeta` | 0.547 | 0.426 (0.121 abs off) | 0.545 (0.003 abs off) | 0.1 abs | **fixed** |
| Phugoid `omega_n` | 0.0502 rad/s | 0.0603 (16.7% off) | unchanged -- eq. 4.2-17b has no `Cm_alpha` term at all | 15% | **still misses -- expected (Joe)** |
| Dutch roll `omega_n`/`zeta` | 1.881/0.109 | 1.874/0.115 (already inside bound) | 1.828/0.118 (still inside bound) | 30%/0.15 | passes either way |
| Roll `sigma` | -1.145 /s | -1.129 (1.4% off) | unchanged -- `Cl_p` has no transfer term | 30% | passes either way |
| Spiral `sigma` | -0.01072 /s | -0.00332 (223% off) | -0.01269 (18.4% off, sign now agrees) | 30% | **fixed** |

Phugoid stays outside its bound after the correction, exactly as expected (Joe's own instruction,
confirmed by the derivation: eq. 4.2-17b, the level-flight Lanchester result this PR uses for
`omega_n`, is `(g/V_T) sqrt(2)` -- airspeed and gravity only, no stability derivative of any kind, so
no coefficient correction can move it). Recorded as `pytest.mark.xfail(strict=True, reason=...)`
naming the measured miss and eq. 4.2-17b -- the gate stays green, an unexpected future pass fails
loudly (a real change in the aircraft or the differencing would need to be re-examined, not silently
absorbed), and the miss stays visible here, in STATE.md, in PLAN.md's PR 7 line, and in the
human-review packet. No tolerance changed anywhere in this correction.

**PLAN.md follow-up (PR 8):** PR 8's own phugoid cross-check must use this PR's numerical `A` (the
actual linearized model), never S&L's eq. 4.2-17a/17b approximation -- this session's own result
that the closed-form phugoid frequency misses by 16.7% even after the CG-referenced correction.

**Failure behavior:** `linearize()` raises whatever `trim_straight_and_level` raises, unmodified
(non-convergent trim, flat-Earth config); `write_input`'s own raises (unregistered name, non-finite
value) propagate unchanged. No new exception type.

**Determinism:** identical to `astra.trim`'s own argument (D9-adjacent reasoning already accepted for
PR 6, not re-litigated) — still air, no RNG anywhere in the differenced path, so two `linearize()`
calls from the same config/condition are structurally expected to be bitwise identical; R5 tests it
directly per Joe's own acceptance list, the same convention as PR 5's R5a/R5b and PR 6's R4.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | Halving every differencing step changes `A`/`B` by < 1e-3 relative (Frobenius) | `pytest tests/python/test_linearize.py::test_halving_every_step_changes_a_and_b_by_less_than_the_bound` — compute `A`/`B` at D12's steps and again at half those steps; assert `norm(A2-A1, 'fro')/norm(A1, 'fro') < 1e-3` and the same for `B`. **Measured, `009715a`**: `A` changed `5.29e-9` relative, `B` changed `4.42e-13` relative — both far inside the bound. |
| R2 | Block structure: cross-coupling < 1e-3 relative to its own block's norm | `pytest tests/python/test_linearize.py::test_longitudinal_and_lateral_blocks_do_not_cross_couple` — `A[np.ix_(long_idx, lat_idx)]` and `A[np.ix_(lat_idx, long_idx)]` each `< 1e-3 * norm(A[np.ix_(long_idx, long_idx)])` (respectively the lateral block's own norm). |
| R3 | Trim point's own derivative `< 1e-6` before differencing | `pytest tests/python/test_linearize.py::test_the_trim_points_own_derivative_is_near_zero` — `f(x0, u0)`, `np.max(np.abs(...)) < 1e-6`; this exercises the *linearization's own* state/quaternion reconstruction (eq. 1.8-16 port) and the eq. 1.4-4 kinematic rows, not merely `TrimResult.residual` again. |
| R4a–e | One test per mode against S&L §4.2 | `pytest tests/python/test_linearize.py::test_short_period_matches_the_approximation` (15% `omega_n`, 0.1 abs `zeta`), `::test_phugoid_matches_the_approximation` (15% / 0.1 abs), `::test_dutch_roll_matches_the_approximation` (30% / 0.15), `::test_roll_matches_the_approximation` (30% relative on the signed `Re(lambda)`/`1/tau`, sign asserted equal first), `::test_spiral_matches_the_approximation` (same, signed — the spiral may be mildly unstable at cruise; D16) — five separate tests, per Joe's own instruction, so a lateral miss doesn't hide the longitudinal result. |
| R5 | Two `linearize()` calls give bitwise-identical `A`/`B` | `pytest tests/python/test_linearize.py::test_two_calls_are_bitwise_identical` — `np.array_equal` on both matrices. |
| R6 | Column isolation, checked after every individual column | `pytest tests/python/test_linearize.py::test_input_registry_reads_trim_values_after_any_column` — drives `linearize._column` directly, once per state index (9) and once per control index (4), against `LinearizeResult.simulation` (D14, the actual perturbed instance — not a separately built one, which would pass vacuously); asserts `sim.registry.find("v0.controls.<name>")` equals each `u0` component **after every single call**, not only once after the whole sweep. |
| R7 | `input_names()` returns exactly the four registered names | `pytest tests/python/test_linearize.py::test_input_names_returns_the_four_registered_control_names` — `sim.input_names() == ["controls.elevator", "controls.aileron", "controls.rudder", "controls.throttle"]` (order asserted, not just set-equality) on a `Simulation` built from `cruise.yaml`. |
| R8 | Full acceptance | `pytest tests/python/test_linearize.py` and `scripts/check-full.sh`/`just check` both green. |

## Questions and decision

None blocking — D11/D12/D13 and the `input_names()` scope addition are already Joe's own settled
instruction; the one repository-level finding (the §4.1 citation not matching the actual book, §3.7/
eq. 1.4-4/1.8-16/§4.2 being the real sources) is resolved above, not escalated, since it changes no
tolerance, no scope, and no design — only which equation numbers the implementation's comments cite.

| Question | Consequence | Owner | Answer/reference |
|---|---|---|---|
| D18: R4 missed on 3/5 modes against D13's raw AERORP-referenced coefficients — extend the closed form (deviates from D13's reviewed coefficient list) or disclose as-is? | Whether `pytest tests/python/test_linearize.py` can be literally green, and what the human-review packet reports. | Joe | **Resolved, 2026-09-21, reframed by Joe**: not an extension — a correction to D13's own coefficient list (S&L's dimensional derivatives are CG-referenced; `cm_alpha_per_rad`/`croll_beta_per_rad`/`cn_beta_per_rad` are AERORP-referenced per `aero_math.hpp`'s own header). Derive the CG-referenced coefficients from first principles in the test (never calling `aero_math.hpp`), apply to every affected derivative (longitudinal and lateral), keep every equation/tolerance unchanged, record before/after for all five modes. Phugoid is expected to still miss (eq. 4.2-17b has no stability-derivative term) — `xfail(strict=True)` naming the measured miss and equation, not a tolerance change. PLAN.md gets a one-line PR 8 follow-up: PR 8's own phugoid cross-check must use this PR's numerical `A`, not the §4.2 approximation. |

## Repository findings

- `python/astra/trim.py` (PR 6) is the direct model for this module's own shape and conventions
  (module docstring citing the plan, `TrimResult`-style frozen dataclass, `_residual6`-style named-
  slice extraction, `_load_still_air_config` reuse) — read in full this session, not assumed from the
  plan text alone.
- `core/include/astra/util/frames.hpp` (`quaternion_from_euler`, eq. 1.8-16) and the Euler kinematical
  equations (eq. 1.4-4, `docs/refs/textbooks/` S&L PDF, main checkout, read directly this session via
  `pdftotext` — page/line citations above are this session's own, not copied from an earlier plan) are
  the two formulas this module ports to Python; neither is bound to Python today, so porting (not
  binding) is the only option without a new core-side binding surface, consistent with D11's own
  "implemented in Python" instruction.
- `python/bindings/_core.cpp`: `Simulation.derivative`, `Simulation.write_input`,
  `Simulation.vehicles`, `Config.from_dict`, and the `astra.VELOCITY`/`ANGULAR_RATE`/`QUATERNION`
  constants are already bound (lines 421–476, 187–195) — read directly, not assumed. No
  `Simulation.input_names` or any `InputRegistry` binding exists yet — this PR adds exactly one new
  free function plus one `.def(...)` line, following `simulation_derivative`'s own pattern in the
  same anonymous namespace.
- `core/include/astra/signals/input_registry.hpp::names()` (line 86) already exists, returns
  `std::vector<std::string_view>`, in registration order, documented as "Init-time enumeration only"
  — a read-only call, no mutation, safe to call any time after `Simulation::from_config` returns.
  `core/include/astra/kernel/vehicle.hpp::inputs() const` (line 244) already exposes the registry
  `Simulation::write_input` itself already reaches through (`simulation.cpp:658`) — no new core-side
  accessor needed.
- `aircraft/notional_transport/physical/aero/aero.cpp:78-80` registers `controls.elevator`,
  `controls.aileron`, `controls.rudder` in that order; `aircraft/notional_transport/hardware/
  propulsion/turbofan/turbofan.cpp:68` registers `controls.throttle` — read directly, confirming
  registration order matches D11's own `u` order given `cruise.yaml`'s own `vehicle.models` list
  (aero first, both turbofan instances after).
- `aircraft/notional_transport/configs/cruise.yaml`'s own aero-plugin block (`cl_alpha_per_rad`,
  `cl_elevator_per_rad`, `cd_0`, `cd_k`, `cm_alpha_per_rad`, `cm_q_per_rad`, `croll_beta_per_rad`,
  `croll_p_per_rad`, `croll_r_per_rad`, `croll_aileron_per_rad`, `croll_rudder_per_rad`,
  `cn_beta_per_rad`, `cn_r_per_rad`, `cn_rudder_per_rad`, `cy_beta_per_rad`) supplies every
  dimensionless derivative the closed-form comparison needs — read directly, matches D13's own
  named coefficients one-for-one, no missing term beyond the already-flagged `Cm_alpha_dot` (PLAN.md
  M3 PR 4's own disclosed gap).
- `pyproject.toml`'s `dev` group already carries `scipy==1.18.1`/`pyyaml==6.0.3` (PR 6) and
  `numpy>=2` is a base dependency — no new dependency for this PR, matching Joe's own instruction.
- `python/astra/__init__.py` needs no change — it imports only `_core` today; `python/astra/trim.py`
  (PR 6) is likewise not re-exported there, and `test_trim.py` imports it directly
  (`from astra import _core, trim`) — this PR's own `tests/python/test_linearize.py` will do the same
  (`from astra import _core, linearize, trim`).

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance command |
|---|---|---|---|
| `Simulation.input_names()` binding | `python/bindings/_core.cpp`: one new free function + one `.def(...)` line, kept in its own labeled commit/diff hunk so it's separately identifiable in the Codex review request (Joe's own instruction) | `InputRegistry::names()` (PR 2, stable, unchanged) | R7 |
| Ported quaternion-from-Euler (eq. 1.8-16) and Euler-kinematics (eq. 1.4-4) | New: `python/astra/linearize.py` | None (pure Python, no new core/binding surface) | R3 (exercises both) |
| `f(x, u)`, `_column` (D15) and central differencing (D12) | New: `python/astra/linearize.py` | `Simulation.derivative`/`write_input` (PR 5, stable); `Simulation.input_names` (this PR, above) | R1, R6 |
| `linearize()` + `LinearizeResult` (D14: carries `simulation`), trim reuse | New: `python/astra/linearize.py` | `astra.trim.trim_straight_and_level` (PR 6, stable); `trim._load_still_air_config` (PR 6, same-package private reuse) | R3, R5 |
| Mode extraction (numerical) | New: `python/astra/linearize.py` | `numpy.linalg.eigvals` | R4a–e (numerical half) |
| Closed-form S&L §4.2 comparison, CG-referenced (D18) | `tests/python/test_linearize.py` only, not the implementation module (D13: "computed in the test") | `cruise.yaml`'s own coefficients plus the D18 moment-transfer correction, re-typed/re-derived in the test (same convention as `test_trim.py`'s own R3 cross-check — never reusing `aero_math.hpp`/`turbofan_math.hpp` or this PR's own `linearize.py`) | R4a/c/d/e; R4b (phugoid) `xfail(strict=True)`, D18 |

One Claude builder; no split (one module, one binding addition, one test file — no independent-path
parallelism to gain).

## Validation and risks

Fast gate: the `_core.cpp` addition is a pure-Python-facing binding over already-existing C++ methods
with no header/ABI change, so `scripts/check-fast.sh`'s ASan/unit-test portion should be an
unaffected no-op — still run once to confirm. Full gate (`scripts/check-full.sh`/`just check`)
required per Tier 2's own row. `pytest tests/python/test_linearize.py -v` is this PR's own acceptance
command; the full `pytest tests/python/` run must stay green alongside it, including `test_trim.py`
unmodified (this PR does not touch `astra.trim`).

Risks: (1) The mode-classification-by-magnitude rule (short period vs. phugoid, roll vs. spiral) is
this session's own expectation, not yet checked against `cruise.yaml`'s real eigenvalues —
implementation must verify the ordering actually holds and fail loudly, not silently mislabel, if two
candidates are close (flagged in Contract above); separately, and regardless of that classification,
D16's own signed-comparison rule means a correctly classified spiral mode can still legitimately fail
R4e outright on a sign disagreement — that is the test working as designed, not a bug to route
around. (2) The phugoid/Dutch-roll/roll/spiral closed-form
substitution algebra (which terms are genuinely zero for this specific notional aero/propulsion
model, beyond `Cm_alpha_dot`) must be re-derived against the real `cruise.yaml` numbers at
implementation time — this plan names the right equations but does not claim to have hand-verified
every substitution the way PR 6's plan hand-verified its own R3 cross-check numbers; a mismatch here
is a defect to report (per D13's own "a miss is a finding to report, not a tolerance to widen"), not
a plan error to silently patch by loosening R4's bounds. **Materialized as D18** (above): short
period and spiral were a real defect in D13's own coefficient list (fixed by the CG-referenced
derivation, not a tolerance change); phugoid is a genuine, expected miss (`xfail(strict=True)`, not
loosened). (3) The Euler-angle-order permutation
between this module's own `x` vector `(phi, theta, psi)` and `frames.hpp`'s argument order `(psi,
theta, phi)` (flagged in Contract) is a one-line transpose but a real trap — worth its own explicit
unit check inside R3/R2, not just trusted from this plan's own prose. (4) No golden/tolerance file is
touched — the oracle-guard hook does not apply; S&L §4.2's own tolerances are explicitly "labeled
approximate," per D13, so a miss inside the stated bound is success, and a miss outside it is a
reportable finding, never a bound to widen.

No compatibility/migration/rollback concerns (new module, no existing caller besides the new binding
line). No documentation change beyond this plan and PLAN.md/STATE.md bookkeeping (already committed
this session, `73ffe43`).

## Plan critique and decisions

No Codex plan critique (PLAN.md's own PR 7 line: "Plan critique: no" — S&L §3.7/§4.2 pin both the
differencing method and the approximation forms; the one binding addition reuses an existing,
already-reviewed C++ method with no ABI change, not a new interface design).

## Done when

R1–R3, R4a/c/d/e, R5–R8 pass under `pytest tests/python/test_linearize.py`; R4b (phugoid) is
`xfail(strict=True)`, D18's own disclosed and expected miss, not silently dropped or loosened — the
gate stays green because that outcome is itself the expected, recorded result, not because the check
was weakened. The full gate (`just check`) is green; the `Simulation.input_names()` diff hunk is
separately identifiable in the Codex review request (Joe's own scope instruction); Codex general
review (`/review-cycle`) has run and any findings are resolved or explained; the human-review packet
is prepared (`/human-review`), disclosing D18 and the phugoid xfail explicitly, before Joe's merge
decision.

**Implemented and validated, 2026-09-21**: candidate `009715a` (binding + module + tests), evidence
retained at `docs/reviews/evidence/009715a-full-gate.log` — full gate green (four presets 387/387,
hooks 144/144, pyright 0, `pytest tests/python/` 97 passed + 1 expected `xfail`). Codex review not
yet run.
