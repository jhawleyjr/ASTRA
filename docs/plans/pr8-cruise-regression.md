# M3 PR 8: trimmed cruise config, 300 s regression trajectory, phugoid cross-check

Spec: Joe's PR 8 instruction (2026-09-22, in session), D19–D21; PLAN.md M3 PR 8 line.
Risk: Tier 3 (`aircraft/**` config, `tests/golden/**`). Status: **Revision 2, approved by Joe 2026-09-22** (with five conditions, below).
Plan approval: pending. Plan critique: none (PLAN.md). Golden capture: armed by Joe separately.
Base: `origin/main` `553700f`. Branch `worktree-m3-pr8-cruise-regression`.

Revision 2: Joe decided Q1/Q2 and accepted the spec changes (below), and asked for three gaps to be
closed: Dryden at t = 0, drift versus the period measurement, and 47 m versus 70 m.

## Joe's decisions (2026-09-22)

- **Q1.** `test_trim.py::test_untrimmed_guess_fails_by_a_wide_margin` changes only how it gets its
  state. It loads cruise.yaml, drops `physical.wind` (as today via `_load_still_air_config`), and
  overrides `v_b = [230, 0, 0]`, `euler_nb = [0, 0, 0]`, elevator 0.0 and throttle 0.5 through
  `Config.from_dict`: the pre-D19 guess. The assertion and the comment's hand-estimate stay.
- **Q2.** The phugoid reference is PR 7's `A` plus the altitude state, computed in the test,
  subject to three conditions. (a) The column is built with linearize's own machinery about the
  same trim: PR 7's `A` plus one column and the ḣ row, not a second linearization. (b) The 4×4
  numbers are printed, not asserted, and PLAN.md gets a PR 7 follow-up recording the 4×4 miss, with
  an opt-in altitude state in `astra.linearize` left as a D11 revision and Joe's call. The packet
  discloses the 4×4 miss. (c) The packet states that 5% / 0.02 were set for the 4×4 and now carry
  slack against the 5×5 match. The tolerances stay as written.
- **Spec changes accepted.** No new binding (`Simulation.run(frames)` is already bound, PR 5). The
  harness and script go in `tests/capture/`. The script checks arming itself. Dryden stays on the
  regression run and is dropped for the phugoid run.

- **Revision 2 approval conditions.** (1) The cruise.yaml provenance comment and the manifest state
  that the trim is still-air and that the frozen run starts in seed-42 turbulence (frame-0 gust
  (−3.08, 0.91, 6.63) m/s), so the trajectory is a turbulence response from trim, not a cruise hold.
  (2) The packet reports the four-preset dry run's max |Δu|, altitude and pitch excursions with the
  no-halt result. (3) The plan states the method bias's cause (finding 6: measured, not the
  sampling/interpolation cause the approval note guessed). (4) The altitude column calls
  `linearize._evaluate` directly (precedent: `test_trim.py`'s `trim._residual6`). (5) The packet
  carries finding 6's table, not a single percentage.

- **Pre-capture approval and commit C findings (2026-09-22).** Joe approved candidate `ed10734`
  (base `553700f`), armed the capture, and added two findings to commit C's cycle, not counted as
  a correction round. J-1: the damping test's second peak window runs from the second
  positive-going crossing to the end of the record, not to a third crossing, which lands at
  292.59 s of the 300 s record. The measured ζ is unchanged, 0.027111, with the peak at 215 s either
  way. J-2: the capture script refuses (step 1b, before any build) when the golden header or
  manifest already exists. Arming never authorizes a re-capture; Joe authorizes one by removing the
  existing golden in a commit he approves. There is deliberately no override flag. Commit B:
  `3d0d3a6`, captured at `4ce6a9f`, four presets byte-identical, no halt.

## Repository findings

Numbers come from exploratory runs this session (`just python` Release at `553700f`, scratch scripts,
nothing committed): planning evidence, not acceptance evidence.

1. **Trim result** (`trim_straight_and_level(cruise.yaml, 230.0, 10000.0)`, default `xtol=1e-12`):
   α = `0.023586859573503434`, elevator = `0.026674474137989946`, throttle = `0.4985979490507746`,
   `v_b = [229.9360238725, 0.0, 5.42447469393253]`. Written into the config, its own initial state
   equals `TrimResult.state` bit for bit. The trim ignores the config's initial v_b, euler, elevator
   and throttle (fixed guesses), so re-running it on the edited file gives the same result.
2. **Consumers.** `grep -rl "notional_transport/configs/cruise.yaml" tests python aircraft
   --include=*.cpp --include=*.py` lists `tests/physics/no_alloc_test.cpp`,
   `tests/physics/notional_transport/{aero,turbofan}_wrench_test.cpp`,
   `tests/python/test_{linearize,trim}.py`, `tests/unit/aero/aero_math_namespace_isolation_test.cpp`,
   `tests/unit/kernel/notional_transport_cruise_config_test.cpp` and
   `tests/unit/mass/notional_transport_mass_test.cpp`. Only Q1's test reads the untrimmed values.
   `notional_transport_cruise_config_test.cpp`'s header comment ("not a trimmed flight") becomes
   false; I'll fix the comment only.
3. **Acceptance 1 versus Dryden (gap 1).** The t = 0 Dryden sample is **not** zero. `dryden.cpp`
   starts from the stationary distribution (`stationary_start_`, `scalar_stationary_init`/
   `two_state_stationary_init` over frame 0's five normal draws). With seed 42 at trim, the frame-0
   gust is `wind_n = (−3.08, 0.91, 6.63)` m/s. `derivative()` refuses a wind-configured,
   never-stepped simulation (`Vehicle::derivative`, PR 5 R7a). After one `step()` it uses the
   environment latched at frame 0, which is what RK4's k1 at t = 0 actually saw. Both numbers,
   D8 max-abs at the config's own initial state:
   - **still air** (wind stripped): **8.3e-17**
   - **with frame-0 Dryden gust latched**: **3.77 m/s²**, dominated by ẇ (the 6.6 m/s vertical
     gust changes α by about 1.6°); the six D8 components are
     `[−0.195, 0.130, 3.765, 0.0270, 0.0176, −0.0135]`.

   The test strips `physical.wind` the same way `_load_still_air_config` does (it doesn't call
   trim), states why in its docstring, and asserts < 1e-6 on the still-air value. The packet
   discloses both numbers. The trim is a still-air trim, and the regression run starts from it
   and then flies through turbulence from the first frame.
4. **Phugoid: 4×4 versus 5×5 versus nonlinear.** From trim, `v_b[0] += 2.0`, still air, 300 s, u at
   1 Hz, t < 20 s dropped, D21's measurement against fixed `u_trim`:

   | Source | Damped period 2π/ω_d | ζ |
   |---|---|---|
   | Nonlinear run (asserted) | **108.57 s** (first two crossings 75.50, 184.07 s) | **0.0271** |
   | PR 7 `A`, 4×4 `[u,w,q,θ]` (printed, not asserted) | 125.33 s (−13.4% vs nonlinear) | 0.0520 |
   | PR 7 `A` + altitude, 5×5 `[u,w,q,θ,h]` (reference) | 108.53 s (0.04%) | 0.0273 (Δ0.0002) |

   The first revision quoted 108.55 s: that was the mean of two crossing intervals, not D21's
   first-two-crossings period. Corrected here.
5. **The altitude column (Q2 condition a).** `linearize._column` can only perturb `x` or `u`, and
   altitude lives in `base_state` (D11 holds position fixed). So the test mirrors `_column` exactly:
   it calls `linearize._evaluate` at `base_state` with `p_n[2] ± 1 m`, divides the difference by
   2Δ, and restores `u0` via `write_input` afterwards. It runs on the `LinearizeResult` from
   `linearize.linearize(cruise.yaml, 230, 10000)`: that result's own `simulation`, `trim.state`,
   `x0` and `u0`. The 4×4 is `longitudinal_block(result.a)`, unchanged. The column has converged:
   ∂u̇/∂h = −2.83298e-5 s⁻¹ and ∂ẇ/∂h = 1.20086e-3 s⁻¹ agree to 8–9 significant figures for Δ = 0.5,
   1, 2 m (10 m moves them by 1e-7 relative). ∂q̇/∂h = 0 and ∂θ̇/∂h = 0. The ḣ row is kinematic,
   ḣ = V_T sin γ = u sinθ − w cosθ (S&L eq. 3.6-2 at β = φ = 0, h = −p_D per §2.5). It is
   cross-checked against a central difference of `derivative()`'s own
   `p_ṅ[2]`: ∂ḣ/∂u 0.0235847 vs 0.0235847, ∂ḣ/∂w −0.999722 vs −0.999722, ∂ḣ/∂θ 229.99996 vs 230.0.
   A supporting test asserts that cross-check, so the hand-written row can't silently drift from
   the Earth model.
6. **Drift versus the period measurement (gap 2).** Unperturbed from trim, still air: u stays in
   [−0.193, +0.012] m/s of `u_trim` and ends at −0.043 m/s; altitude rises up to +5.8 m. The first
   revision's "drifts −0.19 m/s" was the minimum, not a trend. The cause is rotating-Earth/transport-
   rate forcing that D8's trim (ω_b = 0) doesn't cancel, so the forcing excites its own small phugoid.
   It is an oscillation, not a ramp. **The test measures against fixed `u_trim`, which is the 5×5
   model's own equilibrium**: the 5×5 is linearized about that exact state, and its eigenvalues
   describe deviations from it. Sensitivity to that choice, same crossing/peak method throughout:

   | Measured signal | Period | vs 108.53 | ζ | vs 0.0273 |
   |---|---|---|---|---|
   | u − u_trim, nonlinear (asserted) | 108.57 s | +0.04% | 0.0271 | −0.0002 |
   | u_perturbed − u_unperturbed, two nonlinear runs | 108.70 s | +0.16% | 0.0280 | +0.0007 |
   | u − u_trim, linear least-squares detrend | 109.02 s | +0.45% | 0.0309 | +0.0036 |
   | 5×5 model's own simulated u(t), same method | 108.63 s | +0.09% | 0.0280 | +0.0007 |

   The last row is the method's own bias, and its cause is the slow height mode's u component
   (λ = −0.00173 s⁻¹) shifting the crossings and peaks. It is not the sampling rate or the crossing
   interpolation: the same method applied to the 5×5 phugoid component alone is off by 2e-7 in
   period and 3e-5 in ζ at 1 Hz, and all modes together give +0.091% at 1, 10 and 100 Hz alike. So the 0.04% is **partly fortuitous**: a +0.09% method
   bias happens to offset the drift's effect. The robust claim is agreement within 0.5% in period
   and 0.004 in ζ under every reasonable reference. That is at least 10× inside 5% and 5× inside
   0.02, so the pass doesn't depend on the reference choice. Linear detrending is the worst row
   because the forcing isn't a ramp. The packet will carry this table and won't quote "0.02%" as
   the headline.
7. **47 m predicted versus 70 m observed (gap 3).** The +2 m/s initial condition isn't a pure
   phugoid initial condition. In the 5×5 it splits into a phugoid component (u amplitude 1.50 m/s,
   h amplitude 35.4 m ≈ V·1.50/g) and the slow height mode (λ = −0.00173 s⁻¹, τ ≈ 580 s, u 0.51 m/s,
   h 35.5 m). Their sum peaks at **64.9 m at t = 53 s**. The nonlinear perturbed-minus-unperturbed
   altitude peaks at **64.9 m at t = 54 s**, and the remaining ~5 m to 70.2 m is the unperturbed
   run's own rotating-Earth climb (finding 6). V·Δu/g = 46.9 m assumes all 2 m/s goes into the
   phugoid. Density: d ln ρ/dh = −g/(RT) − L/T = −1.24e-4 m⁻¹ at 10 km ISA, so −0.58% at 47 m and
   −0.80% at 65 m, against 2Δu/V = 1.74% from the speed perturbation.
8. **Trim versus rotating Earth, disclosed and out of scope.** Heading drifts −3.2° over 300 s,
   unperturbed. Same cause as finding 6.
9. **The regression run keeps Dryden** (seed 42). From trim: no halt; altitude 9859–10120 m;
   u 227.4–237.9 m/s (Release).
10. **Cross-preset bit-equality over 300,000 frames is the main technical risk.** R1-Sim (1000
    frames, same plugins, Dryden) matched on all four presets. `docs/lessons.md` (2026-09-21)
    records `-ffp-contract` contraction differing between debug and release. I'll dry-run the
    harness on all four presets to scratch **before** you arm, so a disagreement surfaces as a
    finding. No mitigation is pre-planned (`-ffp-contract=off` is a build change and your call).

## Mapping and sequence

One builder. Commit A is everything before capture. Then Joe arms, and the script makes commit B
(capture). Commit C adds the golden-comparing test.

| Req | Change / owned paths | Acceptance (one named test each) |
|---|---|---|
| R1 D19 trim literals | `aircraft/notional_transport/configs/cruise.yaml`: `initial.state.v_b`, `euler_nb`, `initial_controls.controls.{elevator,throttle}` at repr precision. Provenance comment: `trim_straight_and_level(…, 230.0, 10000.0)`, `astra.trim` at `553700f` (unchanged since `53064e0`), D8 = max-abs of `[v̇_b, ω̇_b]` = 8.3e-17, **still air**. The comment also notes that the frame-0 Dryden gust makes the with-wind residual 3.77 m/s² | `pytest tests/python/test_cruise_phugoid.py::test_cruise_config_initial_state_is_trimmed`: wind stripped as `_load_still_air_config` does, no trim call, D8 max-abs < 1e-6 |
| R2 consumers | Q1 change in `test_trim.py`; comment-only fix in `notional_transport_cruise_config_test.cpp` | the grep's eight files green, otherwise unmodified |
| R3 shared run | `tests/support/cruise_regression_run.hpp`: `from_config(cruise.yaml)`, sample the 13-state at frame 0, then 300 × `run(1000)`, sampling after each; stop and return at the first halt; `std::bit_cast` patterns | shared by R4, R5 and R6 |
| R4 determinism | `tests/unit/kernel/notional_transport_cruise_regression_test.cpp`, own executable, `TEST_PREFIX "notional_transport_cruise_regression."`, label `physics` | `…::TwoRunsFromTheSameConfigAreBitwiseIdentical` (asserts no halt too) |
| R5 capture | `tests/capture/capture_cruise_regression.{cpp,sh}`, `tests/capture/CMakeLists.txt` (`EXCLUDE_FROM_ALL`, not in `just check`). Script order: **armed check first** → whole tree clean → `553700f` an ancestor of HEAD → four-preset build and run → byte-for-byte cross-check → refuse on disagreement **or any halt** → write `tests/golden/notional_transport/cruise_regression_golden.hpp` (`std::array<std::array<std::uint64_t,13>,301>`) and `cruise_regression_manifest.md` (baseline `553700f`, capture SHA, harness blob, per-preset toolchain) → clang-format → commit | `pytest tests/python/test_capture_cruise_regression_guard.py`: the script is copied into a temp `git init` tree with `ASTRA_TOUCH_GOLDENS` stripped from the env. (a) Unarmed: nonzero exit, "unarmed" on stderr, no `tests/golden/`, no `build/`, no commit. (b) `ASTRA_TOUCH_GOLDENS=0`: same. (c) Armed via marker, dirty tree: refuses before any build |
| R6 regression | commit C: `…::MatchesTheFrozenTrajectoryBitwise`, sample-by-sample and component-by-component `EQ` against the header | `ctest --preset <p> -R notional_transport_cruise_regression`, debug/release/asan/static-plugins |
| R7 phugoid | `tests/python/test_cruise_phugoid.py`. Dict from cruise.yaml, wind dropped, `v_b[0] += 2.0`, `from_dict`; `run(1000)` × 300; u via `registry.find(f"{vehicle.name}.state.v_b_0")`; drop t < 20 s. Period: first two linearly interpolated positive-going zero crossings of u − u_trim. ζ: first two peaks (max between successive positive-going crossings), δ = ln(x₁/x₂), ζ = δ/√(4π²+δ²). Reference: the 5×5 of finding 5; the 4×4 values are printed | `::test_phugoid_period_matches_the_linear_model` (5%), `::test_phugoid_damping_matches_the_linear_model` (0.02 abs), both labeled approximate. Supporting: `::test_altitude_rate_row_matches_the_derivative` (finding 5's cross-check) |
| R8 bookkeeping | PLAN.md: a PR 7 follow-up item (Q2 condition b) | — |

**Citations.** Phugoid: S&L §4.2, "The Phugoid Approximation" (pp. 261–263, eqs. 4.2-13 to 4.2-18),
and §3.8 Example 3.8-5 (pp. 212–213) for the altitude state. Log decrement: no reference in
`docs/refs/` defines it (searched S&L, MIL-HDBK-1797, MIL-F-8785C and Zipfel). The test derives it
from the second-order standard form S&L uses for eq. 4.2-15: x(t) = A e^(−ζω_n t) cos(ω_d t + φ)
puts successive peaks T_d = 2π/ω_d apart, so δ = ζω_n T_d = 2πζ/√(1−ζ²), which gives
ζ = δ/√(4π²+δ²). The short period (ζ 0.55, ω_n 1.0 rad/s) has decayed by e⁻¹¹ at 20 s.

## Validation and risks

- Before capture: `check-fast`; focused `pytest` for R1, R2, R5, R7; `ctest -R notional_transport_cruise_regression`
  (R4); a four-preset harness dry run to scratch (finding 10).
- After Joe arms: the capture script (commit B), then commit C, then `./scripts/check-full.sh`, with
  its log retained at `docs/reviews/evidence/<sha>-full-gate.log`. Then `/review-cycle` (Codex) and
  `/human-review`.
- Packet disclosures: the 4×4 miss; tolerance slack (Q2 condition c); both acceptance-1 residuals;
  finding 6's sensitivity table; finding 7's decomposition; the rotating-Earth trim drift.
- Risks: finding 10; ASan runtime (three 300 s runs; measured when built, and it moves to its own
  label if it bloats the gate). The golden is O5: any later physics change fails it by design, and
  re-capture needs Joe's authorization again.
- No new dependencies, no core, binding or ADR change. `astra.trim`/`astra.linearize` unchanged.

## Done when

R1–R8 pass. The golden is captured by the armed script with four-preset agreement and no halt. The
full gate is green at the candidate. Codex review is clean or its findings are resolved (0/2 rounds
used). The packet is ready. Merge is Joe's.
