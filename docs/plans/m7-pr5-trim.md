# M7 PR 5: trim at any heading, Q6's rate convention, any Earth model

Status: **approved by Joe, 2026-10-08, with step 3 decided in advance: (b)** ("Joe's decisions", below).
One fresh read-only Codex plan critique ran before he saw it (verdict rework, P-01 to P-10); all ten are
accepted and folded in ("Plan critique", below).
Owner: Joe. Builder: Claude. Base: `origin/main` = `6e0f3de` (#107, PR 6a; confirmed by `git fetch`).
Branch and worktree: `worktree-m7-pr5`, from `origin/main`. This file started as the session's plan-mode
draft and was moved into the worktree (`docs/agents/claude-code.md`).
Tier: **2 if P0 = P1 holds on the unchanged set**; otherwise the work stops and goes to Joe.

## Joe's decisions (2026-10-08)

1. **Step 3 is (b), Q6's alternative.** `rate_convention` is an option on every consumer, `"zero_inertial"`
   the default; the F-16 configs (PR 9 to 12) select `"n_frame"`. Reason: under D3 (B1) the notional
   transport keeps damping the inertial ω_b until B2 is measured and decided, so under `"n_frame"` its trim
   is not an equilibrium in roll (the estimate below, ṗ ≈ −8e-5 rad/s²); (a) would put every
   notional-transport caller on a non-equilibrium trim. The callers' migration joins PLAN.md's existing B2
   open item, not a new item. A dated amendment under the M7 plan's Q6 records (b) and this reason.
2. **P2b and its release build are dropped.** P2 is replaced by one reported number: the notional
   transport's `cruise_still_air` trim under `"n_frame"`, with its six residuals, confirming the roll
   estimate. The B2 item measures the combined migration later.
3. **P0 against P1 stays in full.** On the unchanged set it matches bit for bit, apart from item 4's
   differences. Any other difference is a defect, fixed before the PR goes on, never a finding.
4. **Approved P0/P1 differences** (the contract): flat Earth trims (rows 1 and 2); `trim_for` accepts a
   nonzero heading; the residual-bound stderr names the solved component; and, only if the first
   measurement shows it, the canonical state's last-ulp quaternion, reported with its size.
5. **Approved expectation changes:** the three under "Expectation changes this plan asks Joe to approve".
6. **Tier 2** if P0 = P1 holds on the unchanged set; if it does not, stop and bring it to Joe.
7. **`kJ2`:** an unscheduled PLAN.md open item (the original WGS 84 value, 3e-6 relative from TR8350.2's
   current C̄2,0; either its source is stated or the value changes, by Joe's decision). No change here.

The sections below are the plan as critiqued. Where they describe P2, P2b, (a1) or (a2), the decisions
above supersede them: rows 15 to 17's P2b captures are not run, and rows 1 to 17 are measured P0 against P1.
Intent: the M7 plan's PR table, row 5 ([m7-f16-nesc-oracle.md](m7-f16-nesc-oracle.md), approved at
`3381693`, with its 2026-10-08 amendments); Q6, Q7, Q18; R4; critique C-01, C-03, N-03 to N-05.
Binding: ADR-0001 (frames, `q_nb`), ADR-0003 (`v_b` relative to the Earth, `ω_b` relative to inertial; the
Earth model owns `p_n`), ADR-0017, ADR-0021; architecture §12 and §18.
Not in this PR: PR 5b (not scheduled, addendum §10 item 2); the F-16's aero rate terms and each F-16 case's
full expected residual (PR 9, 11a, 11b; Q7 as amended by D3 and D5); the notional transport's rate-frame
migration (PLAN.md's unscheduled B2 item).

## Context

`astra.trim.trim_straight_and_level` (`python/astra/trim.py:130`) holds ψ = φ = β = 0 and ω_b = 0, SIM 2's
convention, which NASA's footnote (b) set aside. It refuses `earth/flat` (`:152`) and writes the literal
names `controls.elevator` and `controls.throttle` (`:170`). Its consumers rebuild the old state themselves
(`_runkit.trim_fragment` writes `euler_nb = [0, α, 0]` and `omega_b = 0`, `_runkit.py:250`), and
`astra trim` and `_runkit.apply_trim` bound all six residuals to 1e-6 (`cli.py:657`, `_runkit.py:233`).
PR 9 to 12 trim the F-16 at Vol II's headings and latitudes under Q6's convention (SIM 5's: the Euler angles
held relative to the local vertical). This PR moves the solver and every consumer, and measures what that
moves before Joe decides how far the new convention reaches.

## Design

**The solver** (`python/astra/trim.py`), its name and positional arguments kept:
```
trim_straight_and_level(config, true_airspeed_mps, altitude_m, *, heading_rad=0.0,
                        rate_convention=<default: step 3>, elevator_input="controls.elevator",
                        throttle_input="controls.throttle", <the three guesses>, xtol=1e-12)
```
- **Unknowns and state (Q7).** Pitch θ, elevator and throttle, at heading ψ, with φ = 0, still air and
  level flight. The Earth-relative velocity is v_n = V(cos ψ, sin ψ, 0), so v_b = (V cos θ, 0, V sin θ):
  β = 0 and α = θ at any heading (S&L eq. 1.3-10's 3-2-1 C_frd/ned with φ = 0).
- **The state's construction (P-08).** Inside the iteration, `q_nb` comes from (ψ, θ, 0) through
  `linearize`'s port of `frames.hpp::quaternion_from_euler` (moved into `trim.py`, `linearize` imports
  it). The **returned** state is canonical: the core builds it from the emitted `euler_nb = [ψ, θ, 0]`
  (a `Config.from_dict` copy of the still-air config with that initial state, read back from
  `vehicles[0].state`), and ω_b and the residual are recomputed at that exact state. So a config rebuilt
  from the emitted values reproduces the result bit for bit by construction, whatever NumPy's and libm's
  trigonometry do. Whether the port and the core agree bit for bit at the cruise θ is measured first
  (step 3); if not, that is the one expected P0/P1 difference and is reported with its size.
- **The rate convention (Q6).** `"n_frame"`: ω_b = ω_in^b = ω_ie^b + ω_en^b, so ω_nb = 0 and all three
  Euler angles hold. `"zero_inertial"`: ω_b = 0, today's. Under `"n_frame"` the solver takes ω_in^b from
  the core's own kinematics, so any Earth model and its own constants are honored:
  `Simulation.derivative`'s `q̇_nb` slice is linear in ω_b, so it is evaluated at ω_b = 0 and at the three
  unit vectors (the trial θ and v_b), and `J ω = −q̇_nb(0)` is solved by least squares (J = ½Ξ(q), 4×3 with
  orthonormal columns: well conditioned). This recurs in each residual evaluation, since ω_in^b depends
  on θ. A flat Earth gives ω = 0 exactly. R4's third check compares it with an independent closed form.
- **Any Earth model (Q18).** `p_n[2] = altitude` under `plugins/physical/earth/round` and `wgs84_rotating`
  (geodetic), `p_n[2] = −altitude` under `plugins/physical/earth/flat`. Latitude and longitude (or north
  and east) come from the config's initial `p_n`, as today. Any other Earth plugin is refused, naming
  `physical.earth`: the position representation is not visible from Python (`Simulation::earth_` is
  private; binding it would be a `core/kernel` change), and a trim at a guessed altitude is worse than a
  refusal.
- **Names from `Simulation.input_names` (the M7 trim-names item).** The two solved inputs default to ICD v1's
  command names and are checked against `Simulation.input_names()` before the solve; a missing one is
  refused naming the registered list. Every other registered input keeps the config's initial value, as
  today, and `TrimResult.controls` records solved and held inputs alike. The fragment writes each under its
  own name (`controls.elevator` → `vehicle.initial_controls.controls.elevator`).
- **Residual policy (Q7, N-03).** The solved residuals (u̇, ẇ, q̇) at most 1e-6 (m/s², rad/s², D8): the
  solver raises `RuntimeError` naming the worst component and its value when it misses (the M7 contract:
  never a state silently out of tolerance), beside the existing non-convergence raise. The lateral
  residuals (v̇, ṗ, ṙ) are reported in `TrimResult.residual` and never bounded: their kinematic and gravity
  parts are derived and checked in R4 below, and their aero rate terms are PR 9's to 11b's.
- **`TrimResult`** keeps `state`, `elevator_rad`, `throttle`, `alpha_rad` (= θ) and `residual` (6), and
  gains `heading_rad`, `rate_convention`, `controls` and the `solved_residual`/`lateral_residual` views.

**The consumers** (C-03):

| Consumer | Change |
|---|---|
| `_runkit.trim_fragment` | `euler_nb = [ψ, θ, 0]`; `omega_b` from the returned state (never literal zeros); the two solved inputs under their own names (as built: every other input is already the config's, so the round trip holds and `astra trim`'s stdout stays byte-identical) |
| `_runkit.apply_trim`, `trim_setpoint` (scenario, Monte Carlo) | The solver's bound replaces the six-residual check; the setpoint gains an optional `heading` (default 0.0); `TRIM_OWNED`'s reasons restated (`euler_nb` is `[heading, pitch, 0]`, `omega_b` the convention's rate) |
| `astra trim` (`cli.py:600`) | The bound through the solver's raise (exit 1, the component named); `--heading` (rad), defaulting like `--airspeed` and `--altitude` to the config's initial state (D6): ψ of the initial `q_nb` (S&L eq. 1.3-11); the default altitude is the Earth model's (−p_n[2] when flat); the flat-Earth refusal and `_TRIM_FLAT_EARTH` go; an unknown Earth plugin or a missing solved input is a config error (65) |
| `astra.linearize` (P-01, P-09) | `heading_rad` and `rate_convention` passed through; x0's ψ is the heading; u0 is built from `TrimResult.controls` (D11's four-name check unchanged), so held aileron and rudder are the ones trim used. Under `"n_frame"` the three Euler-rate rows use ω_b − ω_in^b(x), with ω_in^b(x) from the core's kinematics at each evaluated state (the solver's J-solve), so the transport rate's dependence on the perturbed velocity and attitude is differentiated, not frozen. Under `"zero_inertial"` the rows are today's, bit for bit (a documented flat, non-rotating approximation, as now) |
| `astra.lockstep.trim_for` | The heading refusal (`lockstep.py:178`) goes; `Setpoints.heading_rad` reaches the solver; `AutopilotTrim` gains the trim fragment, so a driver writes the run's initial state from the same trim result (M7's "one trim result", Q13) |
| `astra.scenario`, `astra.montecarlo` | Through `apply_trim`; docstrings restated |

## Measurement first: every gated caller, before and after

**Staging (P-02, P-03).** The PR is Python only. Every pass shares this worktree's one build: `_core`, the
plugin directory, `astra_autopilot` and `astra_echo_peer` (through the `ASTRA_AUTOPILOT` and
`ASTRA_ECHO_PEER` variables `test_lockstep.py` already reads), so every log's `git_describe` is the same
(lesson 2026-10-04). Each pass runs in its own staging root in the scratchpad, made by `git archive` of a
whole tree, and **runs from that root as cwd** with its own `tests/`, so cwd-relative paths
(`test_trim.py:23`) and `__file__`-relative paths (`test_lockstep.py:41`, `test_mc_planted.py:33`) both
resolve inside it. Each root's `python/astra` gets this build's `_core*.so`; `PYTHONPATH` (absolute)
starts with an empty `_editable_skbc_astra.py` shim, then that root's `python`.
- **P0, before:** `git archive origin/main`.
- **P1, candidate:** `git archive HEAD`, default `"zero_inertial"`.
- **P2, the new convention:** P1 with the one default changed to `"n_frame"` (the diff recorded).
- **P2b, re-trimmed literals:** P2 with the four `notional_transport/configs/cruise*.yaml` trim literals
  (`v_b`, `euler_nb`, `omega_b`, `elevator`, `throttle`) rewritten from P2's own trim by `trim_fragment`,
  every other byte unchanged (the diff recorded). Its C++ consumers (table, rows 15 and 16) need a release
  build of the P2b root in the scratchpad; that build serves only those ctest outcomes, never a log
  comparison.
- Each pass asserts `astra.__file__`, `astra._core.__file__` and `default_plugin_dir()` in the parent, in a
  `spawn` worker of `astra.montecarlo`'s pool, in a `lockstep_child.py` subprocess and in the `astra`
  console script; and the SHA-256 of every config file each caller reads.
- Each pass records (a) per-test outcomes (`--junitxml`) of the PR table's selector, of `tests/scenarios`,
  and of `-k 'log or fgbridge'` (rows 11, 12), plus the opt-in `test_log_cost` run (row 13); and (b) a
  capture driver's values for each row: floats as `float.hex`, arrays and logs as SHA-256 plus the named
  statistic. Same worker counts and output paths in every pass. The trimmed comparison is committed as
  `docs/reviews/evidence/<head>-before-after.log`; the driver and raw output stay in the scratchpad.

**Comparison sets (P-02).**
- **Unchanged-behavior set, P0 = P1 bit for bit:** every captured value of rows 1 to 17 except the
  approved differences below. Any other difference is a defect fixed before step 3, never a finding.
- **Approved P0/P1 differences** (the contract, listed for Joe's approval with this plan): flat Earth now
  trims (rows 1 and 2's flat-Earth cases); `trim_for` accepts a nonzero heading; the residual-bound stderr
  names the solved component (row 2's loose-solve test); and, only if step 3's first measurement shows it,
  the canonical state's last-ulp quaternion (P-08). New-contract tests are kept out of this set.
- **P1 against P2 and P2b:** the convention's effect, brought to Joe whatever it shows.

**The callers.** A: calls trim at run time. B: reads committed literals that trim once produced, or a copy
of them, so it moves only if those literals change (P2b).

| # | Caller (kind) | What it asserts | Captured |
|---|---|---|---|
| 1 | `test_trim` (A) | six residuals < 1e-6 at cruise; α, elevator, throttle in range; T·cos α = D within 5 %; bitwise repeat; the quaternion helper (`allclose`); flat refusal | `TrimResult` of the four cruise configs at 230 m/s, 10,000 m |
| 2 | `astra trim`, `test_cli` (A) | stdout equal to the in-process trim to the last digit (T18); defaults equal the explicit run; exit codes 64, 65, 1, 74, 70 | stdout bytes (explicit, default); each fixture's code and stderr |
| 3 | `test_pyyaml_route` (A) | trim, the trimmed scenario and `trim_for` on repaired forms equal the quoted spelling; refusals | trim values and log hashes per form |
| 4 | `astra.linearize`, `test_linearize` (A) | f(x0) < 1e-6 in all nine rows (R3); bitwise repeat; column isolation; modes against S&L §4.2 within labeled tolerances | A, B, x0, u0, f(x0) bytes; the five modes |
| 5 | `test_cruise_phugoid` (A and B) | the committed state trimmed: six residuals < 1e-6, no trim call (B); the 300 s run's period and ζ (B) against the 5×5 reference `linearize` recomputes (A); the ḣ row | reference and measured period and ζ; the committed state's six residuals |
| 6 | `autopilot_design.build_plants`, `test_autopilot_design` (A) | phugoid against PR 8's recorded values (0.5 %, 0.002); closed-loop pole floors; MIL-F-8785C levels; margins | plants' bytes; phugoid; poles; margins |
| 7 | `astra.lockstep` (A and B, P-06) | `trim_for` equals each lockstep config's literals to 1e-12 with ψ = 0 (A against B); the runs start from the literals (B), while `lockstep_child.py:64` computes the autopilot's biases and the hold peer's commands from `trim_for` at run time (A); R1, R2 bands, R2c/d, R12b, R3, the seed sweep | `AutopilotTrim` and the hold commands per config, captured apart from the initial state; per run the maximum altitude and heading errors, health counters, the recorder hash |
| 8 | `tests/scenarios/cruise_hold` (A) | altitude within 10 m; θ within 3e-3 of the literal α; ψ within 0.012 rad | log hash; each check's extremes |
| 9 | `elevator_step`, `throttle_step`, `aileron_step` (B) | q, TAS, p windows from `cruise_still_air`'s literals; two event values are copies of the trim elevator and throttle (± 0.01, + 0.1) | as 8, and the event values against P2's trim |
| 10 | `atmosphere_ceiling_halt` (B, P-04) | the halt frame (512) and final altitude; its overrides keep the committed `omega_b` and controls | as 8 |
| 11 | `test_log`, `test_log_finalization` (B, P-04; outside the PR's selector) | log rows against the frame-start state of `cruise.yaml` runs, finalization | outcomes and the asserted values |
| 12 | `test_fgbridge` (B, P-04; outside the selector) | the bridge's packets from `cruise.yaml` runs | outcomes |
| 13 | `test_log_cost` (B, P-04; opt-in, not gated) | log cost on `cruise.yaml` | outcome, reported only |
| 14 | `cruise_64.yaml`, `test_montecarlo` (A, `spawn` workers) | 64 runs complete; 1 and 4 workers byte-identical; error rows independent of the trim copy's path | `summary.parquet` and `run_*.h5` manifest; statistics |
| 15 | `planted.yaml`, `planted_pilot.yaml`, `test_mc_planted` (B) | R9: KS and Spearman rank the cause first with margin; the C-08 control flags nothing; the pilot's sizes | manifest; the tests' statistics |
| 16 | C++ readers of the cruise configs (B, P-04, P-10): M3's frozen regression (`notional_transport_cruise_regression_test`, through `cruise_regression_run.hpp`); `derived_signals_test`, `run_log_test`, `run_log_finalization_test`, `simulation_transport_test`, `notional_transport_cruise_config_test`, `no_alloc_test`, M4's `closed_loop_acceptance_test` (the lockstep configs); the frame benchmark and `check_frame_benchmark.py` | each reads the live literals | P2b's ctest outcomes and failing values. Coefficient-only readers (`aero_wrench_test`, `turbofan_wrench_test`, `notional_transport_mass_test`, `aero_math_namespace_isolation_test`) are confirmed coefficient-only at implementation and listed as such. The R1 golden is **not** a live reader: it runs its frozen fixture (`cruise_end_to_end_r1_golden_test.cpp:24`) and is unchanged under every option |
| 17 | Copied literals (B, P-04, P-05): `simulation_transport_test.cpp:104` (the peer's commands), `derived_signals_test.cpp:146`, `tests/embedded/autopilot_test.cpp:35` and `autopilot_process_test.cpp:56` (the three biases), `test_plot_new_aircraft.py:72`, `test_scenario.py::test_the_cruise_hold_pitch_target_is_the_config_trim_alpha` (two committed literals compared), and docs (architecture §13's example, "every key and value identical"; the README snippet) | each equals the committed literal | under P2b, each copy against the rewritten literal: stale or not |

**What step 3 will most likely show** (estimates, never a substitute for the measurement). At cruise
(equator, ψ = 0, 230 m/s, 10 km), Q6's rate is about ω_b = (7.29e-5, −3.6e-5, 1.7e-6) rad/s. The notional
transport's aero damps the inertial ω_b (`aero_math.hpp:139`; the B2 item), so its roll damping meets the
Earth rate: L ≈ q̄Sb·C_lp·pb/(2V) ≈ −63 N·m, ṗ ≈ −8e-5 rad/s². Three unknowns cannot zero that: under
`"n_frame"` the notional transport's trim is not an equilibrium in roll until B2. Expected under P2: rows 1
(six residuals) and 4 (R3's ṗ row) fail; row 7's 1e-12 link fails; rows 2, 3, 6, 8 and 14 shift. Expected
under P2b besides: row 5's committed-state check fails (six residuals, P-05); row 17's copies go stale,
breaking `test_the_cruise_hold_pitch_target_is_the_config_trim_alpha` and `simulation_transport_test`'s
commands; rows 9 to 13, 15 and 16 shift or fail.

## Step 3: Joe's decision, never an expectation edited to match

The P1/P2/P2b tables go to Joe before anything else changes. If nothing moves, the PR stays Tier 2 with
today's default. If anything moves, it is Tier 3, and Joe chooses:
- **(a) The new convention everywhere:** the default becomes `"n_frame"`; every expectation that P2 moves is
  listed with its old and new values for Joe to authorize one by one. Sub-choice:
  - **(a1)** the committed cruise configs keep today's literals, documented as `"zero_inertial"` trims, and
    row 7's link test passes `rate_convention="zero_inertial"` explicitly;
  - **(a2)** they are re-trimmed: every P2b failure, stale copy (row 17), scenario target and C++ value in
    row 16 is listed for authorization. The R1 golden is unaffected (P-10); M3's frozen regression capture
    is regenerated only with Joe's `touch-goldens`.
- **(b) Q6's alternative:** `rate_convention` stays an option with today's default; every consumer exposes it
  (`astra trim --rate-convention`, the setpoint's `rate_convention`, `trim_for`, `linearize`), PR 9 to 12's
  F-16 configs select `"n_frame"`, nothing in rows 1 to 17 moves, architecture §12's planned text is
  amended, and each caller's later migration is a PLAN.md item (after B2).
No expectation changes to match new output without that decision. Committed goldens and tolerances stay
Joe's (`touch-goldens`).

**Expectation changes this plan asks Joe to approve** (the contract, not a measured move):
- Flat Earth trims (Q18): `test_trim::test_refuses_a_flat_earth_config` becomes a test that a trim-capable
  flat-Earth config (the notional transport's models on `earth/flat` and `gravity/constant`) trims, with
  altitude −p_n[2]. `test_cli`'s `flat-earth` case of `test_trim_config_errors_exit_65` keeps its fixture
  (`no_alloc_aero_propulsion.yaml`, whose stubs register no `controls.throttle`) as a missing-input refusal,
  still 65, now naming the input (P-07); a separate CLI test trims the trim-capable flat config.
- `trim_for` trims a nonzero heading.
- `test_trim_above_the_residual_bound_exits_1`'s stderr names the solved component the solver raised on.

## R4 in this PR: the closed-form kinematic and gravity parts (Q7)

Derived from latitude L, heading ψ, airspeed V, altitude h and the solved θ, never from ASTRA's EOM,
written in the test from these sources (`docs/refs/textbooks`):
1. **Radii:** R_N = a(1 − e²)/(1 − e² sin²L)^{3/2}, R_E = a/(1 − e² sin²L)^{1/2} (S&L 3rd ed. eqs. 1.6-5,
   1.6-9; Groves 2nd ed. eqs. 2.105, 2.106), e² = f(2 − f); a = 6,378,137 m, 1/f = 298.257223563,
   ω_ie = 7.292115e-5 rad/s, GM = 3.986004418e14 m³/s² (NIMA TR8350.2 eqs. 3-1, 3-2, 3-6, 3-3).
2. **Velocity:** v_n = V(cos ψ, sin ψ, 0) (still air, level).
3. **Earth rate:** ω_ie^n = ω_ie(cos L, 0, −sin L) (Titterton & Weston 2nd ed. eq. 3.72; Groves eq. 5.41).
4. **Transport rate:** ω_en^n = (v_E/(R_E + h), −v_N/(R_N + h), −v_E tan L/(R_E + h)) (Groves eq. 5.44;
   Titterton eq. 3.87).
5. **Gravity:** S&L eq. 1.6-24's J2 potential differentiated in geocentric spherical coordinates:
   G_r = −(GM/r²)[1 − (3/2)J2(a/r)²(3 sin²ψ_c − 1)] and G_north,c = −3(GM/r²)J2(a/r)² sin ψ_c cos ψ_c,
   at S&L eq. 1.6-17's ECEF position (r = |p|, sin ψ_c = p_z/r), rotated into geodetic NED through
   D = L − ψ_c; plus S&L eq. 1.6-27's centrifugal term, ω_ie²(p_x, p_y, 0), in NED
   (−ω_ie²(R_E + h) sin L cos L, 0, −ω_ie²(R_E + h) cos²L). This path is spherical where the plugin's is
   Cartesian (Groves eq. 2.142, `wgs84_j2.cpp:1`). J2 is the gravity model's declared 1.082629989e-3
   (`core/util/geodesy.hpp:27`), stated as such (see Observations).
6. **The velocity residual.** At the solution ω_nb = 0, so Ċ_bn = 0 and v̇_b = C_bn v̇_n, with v̇_n = f^n +
   g^n − (ω_en^n + 2ω_ie^n) × v_n (Groves eq. 5.53; Titterton eq. 3.69). With φ = 0 the body y axis is
   e_y^n = (−sin ψ, cos ψ, 0), and the test source's side force is zero, so
   **v̇_y = e_y^n · [g^n − (ω_en^n + 2ω_ie^n) × v_n]**, independent of θ and of the aero.
7. **The rate residuals.** With the test source's rolling and yawing moments zero and a diagonal inertia,
   S&L eq. 1.7-5 gives **ṗ = (I_yy − I_zz) q r / I_xx** and **ṙ = (I_xx − I_yy) p q / I_zz**, with
   (p, q, r) = C_bn(ψ, θ, 0)(ω_ie^n + ω_en^n) from 3 and 4 (S&L eq. 1.3-10).

**The test wrench source:** no rate dependence, and no lateral force or moment at β = 0 with aileron and
rudder zero. `cruise_still_air.yaml`'s models with the notional aero's only rate terms zeroed
(`croll_p_per_rad`, `croll_r_per_rad`, `cm_q_per_rad`, `cn_r_per_rad`; `aero_math.hpp:126-147` reads ω_b
nowhere else), `ixz = 0` (a diagonal inertia, leaving `mass/constant`'s sign convention to PR 4), and
`wgs84_rotating` with `wgs84_j2`, written to a tmp config per condition. The turbofans' thrust lies along
body x at y = ±4.9022 m, so their yawing moments cancel exactly. (Codex found no defect here.)

**Conditions** (synthetic; Vol II's exact initial conditions are PR 9's): L ∈ {0, 36.019°, −45°, 60°,
89.95°}, ψ ∈ {0, 45°, 90°, 180°, −135°}, V ∈ {172.4, 230} m/s, h ∈ {3,048, 10,000} m, as 12 combinations
including case 15's (89.95° N, ψ = 90°, 172.4 m/s, 3,048 m; about 5.3 m/s² of v̇_y), case 16's (0°, ψ = 0)
and case 11's region (36.019° N, ψ = 45°).

**The three R4 checks per condition**, `rate_convention="n_frame"` passed explicitly:
- The solved residuals, recomputed through `Simulation.derivative` at the returned state, at most 1e-6.
- v̇_y, ṗ and ṙ equal items 6 and 7, to a roundoff budget, not a physics guess: 1e-12 times the largest
  term magnitude entering the component (|g| included for v̇_y), about 1e-11 m/s².
- **ω_b at the solution equals the independent n-frame rate** C_bn(ψ, θ, 0)(ω_ie^n + ω_en^n) from items
  1, 3 and 4, to 1e-12 times |ω_ie^n + ω_en^n|. Also checked: `earth/round` (R_N = R_E = its radius; ω_ie
  zero unless `rotating: true`) and `earth/flat` (ω_b = 0 exactly; v̇_y, ṗ and ṙ zero to the roundoff budget: v̇_y is 6.9e-18 m/s² at ψ = 135°, gravity rotated through the quaternion's DCM).
- At each solution, ḣ = 0 and `q̇_nb` = 0 to roundoff (level flight, constant attitude).
- On the notional transport itself (damping kept), on the same grid: the solved residuals and the ω_b check
  only; its lateral residuals are reported (its aero rate terms are not derived here).
- `linearize` under `"n_frame"` (P-01): its Euler-rate rows at x0 are zero to roundoff, and the column
  ∂θ̇/∂u at the equator heading north equals cos θ/(R_N + h) from items 1 and 4.

## Round trips (C-03) and the residual policy (N-03), per consumer

Each round trip rebuilds a config from what the consumer emits, builds a `Simulation` from it (no
`transport`, no `log`), and requires bit for bit: the initial state equals `TrimResult.state` (the
canonical, core-built state, P-08); every control equals `TrimResult.controls`; the altitude read back
(p_n[2], or −p_n[2] when flat) equals the setpoint; and `derivative` there, with those controls latched,
equals `TrimResult.residual`. Conditions: cruise, ψ = 45° at 36° N, case 15's, and flat Earth, under both
conventions.

| Consumer | Emitted | Round trip | Residual policy |
|---|---|---|---|
| `astra trim` | its stdout YAML | merged into the config (`q_nb` dropped) | exit 1 when the solver raises on a solved residual; the lateral three printed in `residual`, unbounded |
| `_runkit.apply_trim` (scenario, Monte Carlo) | the dict it writes | `Config.from_dict` of it | `RuntimeError` naming the component; lateral unbounded |
| `astra.linearize` | `LinearizeResult.trim`, x0, u0 | `.trim`'s fragment → config; x0's (φ, θ, ψ) = (0, θ, ψ), its rates ω_b; u0 equals `.trim.controls` by name | the solver's |
| `astra.lockstep.trim_for` | `AutopilotTrim` and its fragment | the fragment in a copy of the lockstep config without `transport` | the solver's; the biases equal the fragment's controls and θ |

## Implementation order

1. Worktree from `origin/main`; move this draft in as `docs/plans/m7-pr5-trim.md`; commit, push, open the
   draft PR.
2. Solver, consumers and the tests above, default `"zero_inertial"`, with `rate_convention` exposed on every
   consumer (decision 1); `./scripts/check-fast.sh` while working.
3. The port-against-core quaternion comparison; P0 against P1 on the unchanged set (any other difference
   fixed); the one `"n_frame"` number (decision 2). If P0 = P1 fails beyond item 4's differences, stop and
   bring it to Joe.
4. Docs: architecture §12 (planned text amended to the option) and §18 step 4; the M7 plan's dated Q6
   amendment; PLAN.md's trim-names and §18 step 4 items closed, the B2 item extended with the callers'
   migration, and the `kJ2` item added; `cli.py`'s and `scenario.py`'s docstrings.
5. Acceptance, the review per tier, the PR body, `/wrap`.

## Acceptance

- `.venv/bin/pytest tests/python -k 'trim or linearize or cli or scenario or montecarlo or lockstep or planted or pyyaml_route or cruise_phugoid or autopilot_design'`
  (the PR table's selector; the new tests live in `test_trim_kinematics.py` and `test_trim_round_trip.py`,
  selected by module name). It does not reach rows 10 to 13 or 16; those are covered by the full gate and
  the before/after passes.
- `.venv/bin/pytest tests/scenarios`.
- `./scripts/check-full.sh`: exit 0 and its `check: OK` line; the trimmed gate summary committed.
- `python3 scripts/docs_check.py`; `git diff --check origin/main..HEAD`.
- The before/after evidence log and Joe's recorded decision.
- Codex: under Tier 2 an independent review of the committed branch (`review --base origin/main`); under
  Tier 3 an adversarial review whose focus names the derivation, the measurement and Joe's decision.
- R4's other clauses: an arbitrary heading, flat Earth and names from `Simulation.input_names` each seen to
  work; `astra trim aircraft/_template/configs/cruise.yaml` no longer exits 65 for its Earth model (it still
  exits 65 for its first `TODO` zero, the aero's `wing_area_m2`, and the test asserts that reason, not `physical.earth`), and a
  filled-in flat-Earth copy of it trims.
Results (commands, exit codes, counts, the Codex verdict and thread id) go in the PR body.

## As built (deviations from the text above, each recorded here)

- `_runkit.trim_fragment` writes the two solved inputs only, nested by their dotted names; the held inputs
  are already the config's. `astra trim`'s stdout keeps its bytes; `TrimResult.controls` and
  `solved_inputs` record all of them.
- The template's first refusal is its `TODO` `wing_area_m2 = 0`, not the mass (the acceptance clause above
  is corrected).
- A flat Earth's lateral residuals are zero to roundoff, not exactly (the R4 clause above is corrected).
- The port and the core build the same quaternion at ψ = 0 bit for bit (the cruise θ and 2,000 random
  pitches in [−0.5, 0.5] rad), so the canonical state brings no last-ulp P0/P1 difference.
- Decision 2's number (`cruise_still_air`, 230 m/s, 10,000 m, this build): under `zero_inertial` the trim
  reproduces the committed literals exactly (elevator 0.026674474137989946 rad, throttle 0.4985979490507746,
  α 0.023586859573503434 rad). Under `n_frame`: ω_b = (7.290097e-5, −3.624651e-5, 1.715317e-6) rad/s,
  elevator 0.02670727443079087 rad, throttle 0.4984593119156813, α 0.02352507612124377 rad; residuals
  v̇_x 1.3e-16, v̇_y −9.8e-19, v̇_z −4.3e-16 m/s², **ṗ −8.204e-5**, q̇ 5.2e-18, ṙ −1.561e-7 rad/s². The
  estimate (ṗ ≈ −8e-5) is confirmed.

## Observations for Joe (no change in this PR)

- `geodesy.hpp`'s `kJ2 = 1.082629989e-3` has no stated source. It is √5 times the original WGS 84 C̄2,0
  that TR8350.2 §3.3 implies, not Table 5.1's EGM96 value or S&L eq. 1.6-25's (1.082626684e-3), a relative
  difference of 3e-6. R4 uses the model's value; a PLAN.md item if Joe wants it settled.
- Under `"n_frame"` the notional transport cannot trim in roll while its aero damps the inertial rate (the
  estimate above). B2 resolves it; the measurement sizes it.

## Plan critique

One fresh, read-only Codex plan critique (`docs/agents/review-dispatch.md#plan-critique`): companion 1.0.6,
`task --fresh --effort high`, the companion's default model; thread `01a11e41-923b-7b51-8c37-f6d0ff036b87`;
exit 0; verdict **rework**, 10 findings (5 high, 5 medium). The candidate was this draft, untracked in the
main checkout, at base `6e0f3de`. Claude re-read every cited line; **all 10 are accepted**, none rejected.

| # | Sev. | Finding (verified) | Evidence checked | Resolution |
|---|---|---|---|---|
| P-01 | high | Frozen Euler-row subtraction omits the transport rate's dependence on the perturbed state | the EOM forms ω_en from the current velocity (`wgs84_rotating.cpp:74`); ∂θ̇/∂u = cos θ/(R_N + h) is lost | ω_in^b(x) from the core at each evaluated state under `"n_frame"`; a column check in R4 |
| P-02 | high | Unqualified P0 = P1 contradicts the approved behavior changes | `test_cli.py:813` asserts the old stderr | An unchanged-behavior set and an enumerated approved-difference list |
| P-03 | high | P2b mixed trees and defaults | `test_trim.py:23` cwd-relative; `test_lockstep.py:41`, `test_mc_planted.py:33` file-relative; `git archive HEAD` has P1's default | One staging root per pass, run from it; P2b built on P2; config hashes asserted |
| P-04 | high | Caller list missed literal consumers and copies | `test_log.py:190`, `test_log_finalization.py:30`, `test_fgbridge.py:30`, `atmosphere_ceiling_halt.yaml:46`, `simulation_transport_test.cpp:104`, `autopilot_test.cpp:35`; a literal search also found `derived_signals_test.cpp:146`, `autopilot_process_test.cpp:56`, `test_plot_new_aircraft.py:72` | Rows 10 to 13, 16 and 17 |
| P-05 | high | (a2)'s extra failures were absent | `test_cruise_phugoid.py:96` bounds six residuals at the committed state; `test_scenario.py:587` compares two literals | Listed under P2b's expected outcome and (a2) |
| P-06 | medium | Lockstep runs are mixed, not literal-only | `lockstep_child.py:64` computes biases and hold commands from `trim_for` | Row 7 mixed, commands captured separately |
| P-07 | medium | The CLI flat-Earth fixture cannot trim | `aero_stub.cpp:31` registers only the elevator | It stays as a missing-input refusal; a trim-capable flat fixture added |
| P-08 | medium | Bit-exact reconstruction was not established | `linearize.py:123` NumPy trig; tests use `allclose` (`test_linearize.py:459`, `test_trim.py:155`) | The returned state is core-built from the emitted `euler_nb`; the port is compared first |
| P-09 | medium | `linearize` forced aileron and rudder to zero | `linearize.py:277` | u0 from `TrimResult.controls` |
| P-10 | medium | The R1 golden does not read the live config | `cruise_end_to_end_r1_golden_test.cpp:24` reads its frozen fixture | R1 unaffected; only M3's regression (live) can move |
