# M3 PR 8: trimmed cruise config, 300 s regression trajectory, phugoid cross-check

Spec: Joe's PR 8 instruction (2026-09-22, in session), D19–D21; PLAN.md M3 PR 8 line.
Risk: Tier 3 (`aircraft/**` config, `tests/golden/**`). Status: **draft, two decisions pending (Q1, Q2)**.
Plan approval: pending. Plan critique: none (PLAN.md). Golden capture: armed by Joe separately.
Base: `origin/main` `553700f`. Branch `worktree-m3-pr8-cruise-regression`.

## Repository findings

Numbers below come from this session's exploratory runs (`just python` Release build at `553700f`,
scratch scripts, nothing committed). They are planning evidence, not acceptance evidence.

1. **`Simulation.run()` is already bound.** `python/bindings/_core.cpp` binds `run(frames)`, `run()`
   and `step()` (PR 5; `tests/python/test_simulation.py:40,47`). D21's new binding is therefore
   unnecessary: **no binding or core change in this PR**. The Python test calls `sim.run(1000)` 300 times.
2. **Trim result** (`trim_straight_and_level(cruise.yaml, 230.0, 10000.0)`, default `xtol=1e-12`):
   α = `0.023586859573503434`, elevator = `0.026674474137989946`, throttle = `0.4985979490507746`,
   `v_b = [229.9360238725, 0.0, 5.42447469393253]`, D8 max-abs residual `8.3e-17`. With these written
   as `initial.state.v_b`/`euler_nb: [0.0, α, 0.0]`/`initial_controls`, the config's own initial state
   equals `TrimResult.state` bit for bit, and its own D8 residual is `8.3e-17`. The trim ignores the
   config's initial v_b/euler/elevator/throttle (it uses fixed guesses and its own α), so re-running
   it on the edited file yields the same result.
3. **One existing consumer depends on the untrimmed values (Q1).**
   `tests/python/test_trim.py::test_untrimmed_guess_fails_by_a_wide_margin` uses cruise.yaml's own
   initial state as "the untrimmed guess" and asserts residual > 1e-2. After D19 that residual is
   8e-17, so the test fails. Per your instruction I stopped there and report it (Q1). The other
   seven consumer files don't read initial v_b/euler/controls as values: `test_linearize.py`,
   `aero_wrench_test.cpp`, `turbofan_wrench_test.cpp`, `notional_transport_cruise_config_test.cpp`,
   `no_alloc_test.cpp`, `notional_transport_mass_test.cpp`, `aero_math_namespace_isolation_test.cpp`.
   Including test_trim, that is **eight** files; if your count of nine includes another, name it.
   `notional_transport_cruise_config_test.cpp` stays green, but its header comment ("not a trimmed
   flight") becomes false; I'll update the comment only.
4. **The phugoid cross-check as specified fails (Q2).** From trim + `v_b[0] += 2.0`, still air,
   300 s, u sampled at 1 Hz, first 20 s discarded:

   | Source | Damped period 2π/ω_d | ζ |
   |---|---|---|
   | Nonlinear run (D21 method: zero crossings, log decrement) | **108.55 s** (crossings 75.50, 184.07, 292.59 s) | **0.0271** (peaks 1.692, 1.426 m/s) |
   | `linearize.modes(A).phugoid` (PR 7, 4×4 `[u,w,q,θ]`) | 125.33 s (**−13.4%**, tol 5%) | 0.0520 (**Δ0.025**, tol 0.02) |
   | PR 7's `A` + altitude state (5×5 `[u,w,q,θ,h]`) | 108.53 s (−0.02%) | 0.0273 (Δ0.0002) |

   Cause: PR 7's `x` holds position fixed (D11), so `A` has no ∂/∂h. The one added column
   (central difference `p_n[2] ± 1 m` through the same `linearize._evaluate` seam:
   ∂u̇/∂h = −2.8e-5 s⁻¹, ∂ẇ/∂h = +1.20e-3 s⁻¹, from density in lift, drag and thrust lapse) plus the
   kinematic row ḣ = u sinθ − w cosθ closes the gap entirely. It also adds a slow real height
   mode, λ = −0.00173 s⁻¹. The nonlinear simulation is right, and the linear reference is
   missing a state. S&L §3.8, Example 3.8-5 (pp. 212–213) describes this directly: with the altitude
   state included, "because of the atmosphere model, there are small coupling terms from altitude to
   several other states", and the nonlinear response "agrees closely with the linear model with
   altitude included."
5. **Altitude coupling, disclosed** (your instruction). Energy exchange gives a phugoid altitude
   amplitude ≈ V·Δu/g = 230 × 2.0 / 9.80665 = **46.9 m**. The observed peak is +70.2 m above trim;
   the excited height mode shifts the mean upward. ISA troposphere at 10 km: d ln ρ/dh =
   −g/(R T) − L/T = **−1.24e-4 m⁻¹**, so density changes **0.58%** at 47 m and 0.87% at 70 m. The
   speed perturbation changes dynamic pressure by 2Δu/V = 1.74%, so the density term is about one
   third as large as the speed term. That ratio is large enough to move the period by 13%.
6. **Trim isn't a perfect steady state on a rotating round Earth** (disclosed, out of scope). D8
   zeroes body accelerations at `ω_b = 0`. Level flight over `wgs84_rotating` actually needs `ω_b` ≈
   Earth rate + transport rate (≈ 7e-5, 4e-5 rad/s). Unperturbed from trim, still air, 300 s: u
   drifts −0.19 m/s, altitude +5.8 m, heading −3.2°. This is about 10% of the +2 m/s signal and
   is present in both the measured and (implicitly) the 5×5 result above, which still agree to 0.02%.
7. **The regression run includes cruise.yaml's Dryden turbulence** (`seed: 42`, `w20_knots: 15`),
   because D20 says "from cruise.yaml". From trim: no halt, altitude 9859–10120 m, u 227.4–237.9 m/s
   (Release). That makes the two-run determinism test exercise the RNG path. The phugoid test
   drops `physical.wind` (still air, matching trim/linearize); turbulence would swamp a 2 m/s signal.
8. **`tests/golden/**` is guard-blocked until you arm the session**, including a harness or script
   placed there. R1 kept its harness in `tests/golden/dynamics/`, which only works in an armed
   session. **Routine call:** the harness and script go in `tests/capture/`, which is unguarded,
   so all code is written, reviewed and guard-tested before you arm. Only the header and manifest
   land in `tests/golden/notional_transport/`.
9. **Neither existing generator checks arming itself.** `capture_r1_golden.sh` and
   `generate_nesc_band.py` rely on the hook, and the hook inspects command text, not what a
   script writes. The new script checks arming itself, using the same test as `guard-oracle.sh`:
   `ASTRA_TOUCH_GOLDENS=1`, or `.claude/touch-goldens` in this tree or in the main checkout
   (resolved via `git rev-parse --git-common-dir`).
10. **Cross-preset bit-equality over 300,000 frames is the main technical risk.** R1-Sim (1000
    frames, same plugins, Dryden) matched across all four presets. `docs/lessons.md` (2026-09-21)
    records `-ffp-contract` contraction differing between debug and release. I'll dry-run the
    harness under all four presets to scratch **before** you arm, so a disagreement surfaces as a
    finding, not a failed capture. No mitigation is pre-planned (`-ffp-contract=off` is a build
    change and your call).

## Pending decisions

**Q1: `test_untrimmed_guess_fails_by_a_wide_margin`.** Recommended: change only how the test gets
its untrimmed state. It builds the state explicitly, `trim._state_from_alpha(p_n, 230.0, 0.0)` with
`write_input` elevator 0.0 and throttle 0.5, which are exactly today's cruise.yaml values. The
assertion (> 1e-2) and intent (R2: the residual discriminates) stay the same. Alternative: leave the
test and don't trim cruise.yaml, which contradicts D19.

**Q2: phugoid reference.** Recommended: compare against PR 7's numerical `A`'s longitudinal 4×4,
unchanged, augmented with the altitude state (finding 4). The h column is a central difference
through `linearize._evaluate` with h step 1 m; the ḣ row is kinematic. It is computed in
`test_cruise_phugoid.py`, so `astra.linearize` stays untouched. Tolerances stay at 5% and 0.02. The
4×4 values are printed as a disclosed diagnostic, not asserted. Alternative: keep the 4×4 as the
reference as D21 states; both tests then fail by 13.4% and 0.025 and are recorded as findings, not
shipped green.

## Mapping and sequence

One builder. Commit A is everything before capture. Then you arm and I run capture (commit B,
script-made). Commit C adds the golden-comparing test.

| Req | Change / owned paths | Acceptance (one named test each) |
|---|---|---|
| R1 D19 trim literals | `aircraft/notional_transport/configs/cruise.yaml`: `initial.state.v_b`, `euler_nb`, `initial_controls.controls.{elevator,throttle}` at repr precision; provenance comment: D8 residual `8.3e-17`, `astra.trim` at `553700f` (unchanged since `53064e0`), call args, D8 = max-abs of `[v̇_b, ω̇_b]`, still air | `pytest tests/python/test_cruise_phugoid.py::test_cruise_config_initial_state_is_trimmed`: `Config` from cruise.yaml with `physical.wind` dropped (a never-stepped Dryden can't be probed, PR 5 R7a), `derivative(vehicles[0].state)`, D8 max-abs < 1e-6; no trim call |
| R2 consumers | Q1's `test_trim.py` change (if approved); comment-only fix in `notional_transport_cruise_config_test.cpp` | the eight files above green, unmodified otherwise |
| R3 shared run | `tests/support/cruise_regression_run.hpp`: `from_config(cruise.yaml)`, sample the 13-state at frame 0, then 300 × `run(1000)`, sampling after each; stop at the first halt and return it; bit patterns via `std::bit_cast` | used by R4, R5 and R6, so the harness and the test can't drift |
| R4 determinism | `tests/unit/kernel/notional_transport_cruise_regression_test.cpp`, own executable, `TEST_PREFIX "notional_transport_cruise_regression."`, label `physics` (too slow for the Stop hook's `unit` subset) | `…::TwoRunsFromTheSameConfigAreBitwiseIdentical`, and asserts no halt |
| R5 capture | `tests/capture/capture_cruise_regression.{cpp,sh}` + `tests/capture/CMakeLists.txt` (`EXCLUDE_FROM_ALL`, never built by `just check`). Script order: **armed check first** → whole tree clean → `553700f` is an ancestor of HEAD → build and run all four presets → byte-for-byte cross-check → refuse on any disagreement **or any halt** → write `tests/golden/notional_transport/cruise_regression_golden.hpp` (`std::array<std::array<std::uint64_t,13>,301>`) + `cruise_regression_manifest.md` (baseline `553700f`, capture SHA, harness blob, per-preset toolchain) → clang-format → commit | `pytest tests/python/test_capture_cruise_regression_guard.py`: copies the script into a temp `git init` tree, strips `ASTRA_TOUCH_GOLDENS` from the env, then (a) unarmed: nonzero exit, "unarmed" on stderr, no `tests/golden/`, no `build/`, no commit; (b) `ASTRA_TOUCH_GOLDENS=0`: same; (c) armed via marker but dirty tree: refuses before any build |
| R6 regression | commit C: `…::MatchesTheFrozenTrajectoryBitwise` includes the golden header and asserts `EQ` sample by sample and component by component | `ctest --preset <p> -R notional_transport_cruise_regression` under debug, release, asan, static-plugins |
| R7 phugoid | `tests/python/test_cruise_phugoid.py`: dict from cruise.yaml, wind dropped, `v_b[0] += 2.0`, everything else as the trimmed config; `run(1000)` × 300; u from `registry.find(f"{vehicle.name}.state.v_b_0")`; drop t < 20 s. Period: linearly interpolated positive-going zero crossings of u − u_trim, first two → one period. Damping: first two peaks (max of u − u_trim between successive positive-going crossings), δ = ln(x₁/x₂), ζ = δ/√(4π²+δ²). Reference per Q2 | `::test_phugoid_period_matches_the_linear_model` (5%), `::test_phugoid_damping_matches_the_linear_model` (0.02 abs), both labeled approximate |

**Citations for R7.** Phugoid: S&L §4.2, "The Phugoid Approximation" (pp. 261–263, eq. 4.2-13 to
4.2-18: the speed/pitch-attitude exchange with α and q quasi-steady), plus Example 3.8-5 for the
altitude state. Log decrement: no reference in `docs/refs/` defines it (searched S&L, MIL-HDBK-1797,
MIL-F-8785C, Zipfel). It is derived in the test docstring from the second-order standard form S&L
uses for eq. 4.2-15, x(t) = A e^(−ζω_n t) cos(ω_d t + φ): successive peaks are T_d = 2π/ω_d apart,
so δ = ζω_n T_d = 2πζ/√(1−ζ²), which inverts to ζ = δ/√(4π²+δ²). Why the method works here: the
short period (ζ 0.55, ω_n 1.0 rad/s) has decayed by e⁻¹¹ at 20 s. The height mode is slow and
non-oscillatory; it biases crossings and peaks slightly, but the 0.02% agreement above includes that bias.

## Validation and risks

- Before capture: `check-fast`; focused `pytest` for R1, R2, R5, R7; `ctest -R notional_transport_cruise_regression`
  (R4 only); dry run of the four-preset harness to scratch (finding 10).
- After you arm: `tests/capture/capture_cruise_regression.sh` (commit B), then commit C, then
  `./scripts/check-full.sh`, whose log is retained at `docs/reviews/evidence/<sha>-full-gate.log`.
  Then Codex review (`/review-cycle`) and your packet.
- Risks: finding 10 (cross-preset), and ASan runtime (three 300 s runs; measured when built, and it
  moves to its own label if it bloats the gate). The golden is O5: it freezes behavior at the
  capture SHA, and any later physics change will fail it, as intended. Re-capture requires your
  authorization again.
- No new dependencies, no core/binding/ADR change. `astra.trim`/`astra.linearize` unchanged.

## Done when

R1–R7 pass. The golden is captured by the armed script with four-preset agreement and no halt. The
full gate is green at the candidate. Codex review is clean or its findings are resolved (0/2 rounds
used). The packet is ready. Merge is yours.
