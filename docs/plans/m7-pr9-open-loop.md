# M7 PR 9: cases 11 and 12 open loop (Tier 3)

Status: **approved by Joe, 2026-10-13, on his word**, with his amendments 1 to 10 and his four follow-up
amendments 11 to 14 folded in (below). The four questions are decided as recommended, under the conditions in
those amendments. D1 to D5 are accepted as recommended, and mismatches M1 to M3 are accepted as stated.
Name: the plan-mode draft (main checkout, untracked) was moved (`mv`, before its first add) to
`docs/plans/m7-pr9-open-loop.md`. Nothing is left in the main checkout.
Owner: Joe. Builder: Claude, one builder. Tier: **3** (an oracle harness against ADR-0028's bands).
Base: `origin/main` = `bff4c4e` (#117), confirmed by `git fetch` after approval. The worktree `m7-pr9` (branch
`worktree-m7-pr9`) is at `bff4c4e`.
Binding: ADR-0013, 0014, 0015, 0016, 0028, 0032, 0033 (all accepted).

**The design is pinned.** This plan cites it and does not redesign it. There is no Codex plan critique (Joe).
- The M7 plan (`docs/plans/m7-f16-nesc-oracle.md`): row 9 (`:853`), R4's F-16 half (`:401`), R5 (`:402`), R9
  (`:406`), Q5 under (c) (`:459`), Q6 with Joe's (b) (`:514`), and Q7 with amendment 2 and its 2026-10-08
  amendment (`:520-546`).
- PR 7's evaluator (`tests/physics/nesc/independent_evaluator.hpp`); PR 8's band headers and the thread's rules;
  PR 5's trim round trip (`docs/plans/m7-pr5-trim.md`); PR 14's tracked-config rule.
- STATE.md's PR 9 thread: D8, 6b's decision 4, and `copy_inputs()` after each sample's `run(1)`.
- The measured band preview (amendment 7, rows 11 and 12).

## Context

Cases 11 and 12 fly the trimmed F-16 open loop for 180 s, subsonic (Mach 0.525 at 10,013 ft) and supersonic
(Mach 2.01 at 30,013 ft). ADR-0028's bands are among the tightest in M7: in case 11, altitude 0.14 m, roll
2.7e-5 rad, rate_q 3.2e-7 rad/s and force_z 0.48 N. This PR does three things:
- it commits the two configs, trimmed by PR 5's `astra trim` under `n_frame`;
- it flies them in a harness built on PR 7's evaluator;
- it plants this family's negative controls.

Before any run, it brings Joe four answers: Q7's residual, D8, the throttle check and the negative controls.

## Joe's decisions (2026-10-13)

1. **Q1 is accepted:** the method, the four-part split and the 1e-12 budget, with two conditions.
   - (a) The table below holds the script's values at its own trim. The constants pasted into
     `Case1{1,2}TrimResiduals` are the script's totals at each committed config's literals. The PR body shows both
     sets and their difference.
   - (b) The agreement between the script's independent trim and the config's is pre-registered at **1e-6 in
     each** of θ (rad), the elevator (rad) and the throttle (unit throttle). Inside that bound it is reported.
     Outside it, it is a stop condition that goes to Joe, never a re-trim.
2. **Q1's budget is written as three numbers per case** (the table under Q1), not as a formula.
3. **`derive_residual.py` reads the config's scalars as text** and converts them with `float()`, never through
   PyYAML's implicit typing (YAML 1.1 reads an exponent with no dot as a string; lessons 2026-09-28 and
   2026-09-30). Amendment 12 replaces this item's original `repr` round-trip check.
4. **Q2 is accepted.** The ellipsoidal reading holds for 11 and 12. There is no geoid model, and `engine.cpp` and
   ADR-0033 are untouched. In the /wrap docs pass, the answer is recorded where the question was asked:
   - one dated line (2026-10-13) under the addendum's D8 bullet, pointing at this plan's Q2;
   - one dated line beside the M7 plan's Assumption 1, pointing at the same.
   STATE.md's one-line note stays as planned.
5. **Q3 is accepted:** the [0, 1] assertion in `TrimResiduals`, with no change to trim, the engine or the tools.
6. **Q4 is accepted:** the four plants and their expected bands, as tabled.
   - Izx's sign in case 11 is a reported control. Case 12 is not run for it.
   - This changes R9's expectation. At /wrap, a dated amendment to R9 in the M7 plan records it: Izx's sign is
     reported in 11 and gated in 13.x, and PR 11a carries the clause.
7. **A halt before `Band::kFirstGatedSample` in any control run is a control failure.** It goes to Joe. It is
   never a pass, and never read as the band being exceeded. Q4 states why the elevator plant's dive stays in
   range through t = 8 s.
8. **The frame count is stated** ("Harness design", the frame count). The answer is verified against the core
   before commit 3, and the configs' header comment says the same.
9. **Verification step 1** first runs the `-N` count on the base build and records 0, so `NescF16Band.*` must not
   match. It then records 9 on the candidate, before the gated run.
10. **If the auto-mode classifier refuses commit 1's Status-line write** (lesson 2026-09-29), it is not retried
    by any route. The PR body names the refusal, the work continues with commit 2, and Joe instructs the write in
    session.

Joe's follow-up amendments (2026-10-13, at approval):

11. **D1 to D5 are accepted** as recommended, and marked decided.
12. **The scalar check is a regex, not `repr`.** Both readers are correctly rounded (Python's `float()` and
    yaml-cpp's `strtod`), so plain decimal text gives the same double. `repr(float(text)) == text` would refuse a
    legitimate `%.17g` literal such as `0.046058165000000001`.
    - Each scalar's text must match a strict decimal-or-exponent regex. That excludes a YAML special (`.inf`,
      `.nan`, `~`), a bare integer where a float is expected, and a bool word.
    - The derivation log prints each value as `%.17g` beside its text.
    - A text that fails the regex is the stop condition.
13. **The constants are listed before the first R4 run.** `derive_residual.py`'s log lists every physical
    constant it uses, with its source, beside the core's value read from the plugin or `core/util`:
    - ISA 1976: T0, p0, L, g0, R* and M0 (or R), and whether geometric altitude is converted to geopotential
      with r0 = 6,356,766 m;
    - WGS-84: a, f, ω_ie and GM, plus J2;
    - the slug and foot conversions.
    Any difference is reported in the PR body with its expected effect on the budget, so that a budget miss is
    diagnosed in advance. The script still shares no code with the core: it states its own values, and the core's
    are read as text for the comparison only.
14. **Verification step 3's `-R F16` also matches the nine new tests**, so it runs the two 180,001-frame cases
    under ASan. Step 3 records its `-N` count and wall time. The F16Aero, F16Engine, F16Mass, F16NoAlloc and
    F16Evaluator statement stays as the unchanged-set claim it is.

## Joe's four questions (decided as recommended, with the conditions above)

All numbers below are *computed* in plan mode by a read-only scratch script. The script parsed P1's XML
directly (`F16_aero.dml`, `F16_prop.dml`, `F16_inertia.dml`), with no ASTRA code, plugin or table header. It used
WGS-84, J2 by PR 5's closed form (S&L eqs. 1.6-17, 1.6-24, 1.6-27), and ISA-1976 below 11 km. It solved its own
three-unknown trim (θ, el, PWR) under Q6's n-frame rate, with the aero fed the Earth-relative rate (ADR-0032)
and the moments moved to the CM (D4). Commit 2 commits that derivation as a script (below).

Cross-checks of the script against the members:
- θ: case 11's 2.638938° is within 2.1e-7 rad of SIM 5's t = 0 pitch, and case 12's −0.0129429 rad is within
  4e-10 rad of the band's reference.
- Aero Y, L and N at the CM: −0.0285042 lbf, −0.0997636 and 0.388028 lbf·ft, against SIM 5's −0.0285042,
  −0.0997637 and 0.388028.
- A linear lateral model driven by the residual below (Q4) reaches roll −1.275e-3 rad at 180 s in case 11 and
  −2.566e-3 rad in case 12. The band references peak at 1.28e-3 and 2.59e-3 rad.

### Q1. The full expected lateral residual (Q7, amendment 2)

The trim's three solved residuals (u̇, ẇ, q̇) are zero to roundoff in the script. The lateral three, split into
their parts:

| | Case 11 (36.019° N, ψ = 45°, 172.42 m/s, 3,051.96 m) | Case 12 (same, 609.6 m/s, 9,147.96 m) |
|---|---|---|
| Trim (script) | θ 0.046058165 rad, el −0.0564223 rad, throttle 0.138738 | θ −0.012942936 rad, el −0.0266662 rad, throttle 0.910658 |
| ω_b (n frame), rad/s | (4.421e-5, −6.875e-5, −5.478e-5) | (4.030e-5, −1.372e-4, −9.242e-5) |
| Earth-relative ω, rad/s | (5.79e-7, −2.705e-5, −1.386e-5) | (−8.44e-7, −9.554e-5, −4.900e-5) |
| v̇_y kinematic, −(ω_b + ω_ie)×v_b | +1.717977e-2 m/s² | +8.215739e-2 m/s² |
| v̇_y gravity, J2 plus centrifugal | +4.935e-6 | +3.836e-5 |
| v̇_y aero rate terms, Y/m | −1.364e-5 | −8.165e-5 |
| v̇_y engine | 0 (FEY ≡ 0) | 0 |
| **v̇_y total** | **+1.717107e-2 m/s²** | **+8.211410e-2 m/s²** |
| ṗ, ṙ kinematic, I⁻¹(−ω×Iω) | −2.984e-9, +2.126e-9 rad/s² | −9.921e-9, +3.708e-9 |
| ṗ, ṙ gravity | 0 | 0 |
| ṗ, ṙ aero rate terms, I⁻¹(L, N at CM) | −9.8858e-6, +5.9955e-6 | −2.23870e-5, +3.77024e-5 |
| ṗ, ṙ engine (no rotor, no moments, D5) | 0, 0 | 0, 0 |
| **ṗ, ṙ total** | **−9.8888e-6, +5.9977e-6 rad/s²** | **−2.23969e-5, +3.77061e-5 rad/s²** |

What the table shows:
- The kinematic term dominates v̇_y. It agrees with critique C-01's 0.017 and 0.082 m/s².
- The aero rate terms dominate ṗ and ṙ: the transport rate is damped through clp, clr, cnp and cnr at the trim
  α. The gyroscopic coupling is about 3e-4 of them.
- At β = 0 the static Cl0 and Cn0 are exactly 0 at both α, so the rate terms are the whole lateral aero, as in
  D3's check.

**How R4 uses it** (decided; amendments 1 to 3):
- Commit 2 adds `docs/reviews/evidence/m7-pr9-residual/derive_residual.py`, with its output log beside it.
  - It runs on `.venv/bin/python -I`, with NumPy from the dev group and no `import astra`.
  - It checks P1's SHA-256 against the addendum's §1.1.
  - **It reads the config's scalars as text** (amendments 3 and 12). Its line reader finds the flow sequences and
    scalars it needs: `p_n`, `v_b`, `euler_nb`, `omega_b`, and the four `controls` values.
    - Each text must match a strict decimal-or-exponent regex, `^[-+]?(\d+\.\d*|\.\d+)([eE][-+]?\d+)?$` or
      `^[-+]?\d+[eE][-+]?\d+$`. That excludes a YAML special, a bare integer and a bool word.
    - It is converted with `float()`, and the log prints it as `%.17g` beside its text.
    - A text that fails the regex stops the work and goes to Joe.
  - **Its log lists every physical constant first** (amendment 13). Each is shown with its source, beside the
    core's value read as text from the plugin or `core/util`:
    - ISA 1976: T0, p0, L, g0, R*, M0, and the geopotential conversion with r0 = 6,356,766 m;
    - WGS-84: a, f, ω_ie and GM, plus J2;
    - the slug and foot conversions.
    Any difference goes in the PR body with its expected effect on the budget, before the first R4 run.
  - It evaluates all six residuals by parts **at each committed config's own initial state and controls**.
  - It also solves its own independent trim (θ, el, PWR). Its agreement with the config is pre-registered at
    1e-6 in θ (rad), the elevator (rad) and the throttle (amendment 1(b)). Inside that bound it is reported.
    Outside it, it is a stop condition.
- The harness's `NescF16.Case1{1,2}TrimResiduals` builds the config and calls `Simulation::derivative` at the
  initial state. It checks three things:
  - **the solved three ≤ 1e-6** (D8; R4);
  - **each lateral component equal to the script's total at the config's literals**, within the absolute
    bounds below;
  - **the throttle in [0, 1]** (Q3).
- The expected totals are pasted from the log as `%.17g` constants, with the script and the config named. The
  PR body shows the table's values (the script's own trim) beside the pasted ones (the config's literals), and
  their difference (amendment 1(a)).
- A miss goes to Joe and is never widened.

**The budget, pre-registered as numbers** (amendment 2). PR 5's roundoff budget: 1e-12 times |g| for v̇_y, and
1e-12 times the table's largest-magnitude part for ṗ and ṙ. |g| comes from the same J2-plus-centrifugal closed
form at each initial condition: 9.7888408 m/s² in case 11 and 9.7700805 m/s² in case 12.

| Bound | Case 11 | Case 12 |
|---|---|---|
| \|v̇_y − expected\| | ≤ 9.7888e-12 m/s² | ≤ 9.7701e-12 m/s² |
| \|ṗ − expected\| | ≤ 9.8858e-18 rad/s² | ≤ 2.2387e-17 rad/s² |
| \|ṙ − expected\| | ≤ 5.9955e-18 rad/s² | ≤ 3.7702e-17 rad/s² |

### Q2. D8: does the engine's geoid altitude, read against the ellipsoid, hold for 11 and 12?

**Recommendation: yes, for both cases.** Read the ellipsoidal height, as ADR-0033's `altitude_m` already does,
and add no geoid model. The evidence:
- **Both members' `altitudeMsl_ft` is the WGS-84 ellipsoidal height.** The *computed* geodetic height of each
  member's own position equals its `altitudeMsl_ft`:
  - SIM 5, from `gePosition_ft_*`, at t = 0, 90 and 180 s in both cases: to about 1e-6 ft. For example, case 11
    at 90 s gives 10012.988059 against the CSV's 10012.988059420.
  - SIM 4, from `eiPosition_ft_*` with the Earth rotated back: to 3e-5 ft or better (2e-5 ft at t = 0).
  - So the members' "MSL" is the ellipsoid. This is the M7 plan's Assumption 1, and its evidence (`:92`).
- **The geoid alternative would not change a gated channel.**
  - The altitude difference at the initial condition is the geoid height N. It needs a geoid model, and none is
    in `docs/refs`. For scale only: NGS's GEOID18 service gives N = −38.985 m at (36.01916667°, −75.67444444°).
    That was looked up 2026-10-13; it is not a `docs/refs` source, and nothing depends on it.
  - A geoid reading puts the engine about 39 m higher: 10,141 ft and 30,141 ft. Both stay inside the same thrust
    table cells, 10,000 to 20,000 ft and 30,000 to 40,000 ft.
  - Re-trimmed in the script, θ and el are unchanged. Only the throttle moves: +3.1e-4 in case 11 (PWR 13.874 to
    13.905) and +3.82e-3 in case 12 (91.066 to 91.448). Thrust at trim is identical, and the altitude slope stays
    in the same cell.
  - No gated channel reads the throttle or the thrust: the aero channels are aero only (Q5).
- **Left as is.** `engine.cpp`'s comment and ADR-0033 Decision 1 still call D8 "PR 9's question". The engine is
  out of scope and ADRs are immutable. The answer is recorded in three places:
  - this plan and the PR body;
  - at /wrap, a dated line under the addendum's D8 bullet and another beside the M7 plan's Assumption 1
    (amendment 4);
  - STATE.md's one-line note, for the next PR that edits `engine.cpp`.

### Q3. Is the configs' throttle range-checked? (6b's decision 4)

**Recommendation: yes, in the harness. No change to trim, the engine or the tools.**
- `NescF16.Case1{1,2}TrimResiduals` also asserts that the config's latched `controls.throttle` lies in [0, 1].
- The script's values are 0.138738 and 0.910658: case 12 trims in afterburner, PWR 91. Both are inside the range,
  so the engine's extrapolation is never reached.
- A re-trim that left the range would fail loudly, never fly silently.
- The general home for a declared control range stays PLAN.md's existing M8 item ("Elevator/throttle deflection
  *range* has no declared home"). `astra run` and `astra trim` still accept any throttle. Narrowing that is not
  this PR's scope.

### Q4. The negative controls (R9, Q24): planted without editing aircraft/f16's aero, mass or engine

Each control has a fixed expected band, decided here before any run. A control asserts that this band is
exceeded at some gated sample (from `Band::kFirstGatedSample` on). It may stop at the first exceedance, and it
logs every channel's largest ratio |d|/tol_abs up to that point.

**Every control asserts that no halt occurs before `Band::kFirstGatedSample`** (amendment 7). Such a halt fails
the control and goes to Joe. It is never a pass, and never read as the band being exceeded.

| Control | How it is planted | Expected to fail | Estimate |
|---|---|---|---|
| Positive control for the mass route (case 11) | Case 11's config with `physical.mass` swapped for `plugins/physical/mass/constant`, which carries P1's values as `units::*` doubles written `%.17g` (`ixz` = +982 slug·ft²), built in the test by `Config::from_string` | none: `Simulation::derivative` is **bit-identical** to the F-16 mass's, at the trim state and at it with ω_b + (0.1, 0.05, 0.1) rad/s, where Izx couples | proves the route plants only what it says |
| **Izx's sign** (case 11 only; decided as reported, amendment 6) | The same swap, with `ixz` = −982 slug·ft². The committed trim is kept: Izx enters q̇ only through Izx(p² − r²), about 3e-11 rad/s² | **none expected (see below)** | linear lateral model: largest gated ratios rate_p 0.33, rate_r 0.32, roll 0.25, yaw 0.08. Case 12 (≤ 0.34 by the same model) is not run |
| **The elevator's sign** (case 11) | `Simulation::write_input("controls.elevator", −el)` before frame 0, through the write seam. No config file | **force_z** (0.477 N), with pitch and rate_q | ΔCZ = −0.19·Δel/25 alone is about 1.8e4 N at frame 0 |
| **SIM 2's zero-inertial-rate trim** (case 11) | A test-only config `tests/physics/nesc/case11_f16_zero_inertial_trim.yaml`, emitted by `astra trim --rate-convention zero_inertial` from the same base | **rate_q** (3.25e-7 rad/s), with pitch | inertial q ≈ 0 against the reference's −6.88e-5 rad/s (212×); pitch drifts about 6.9e-5 rad/s, about 5.5e-4 rad (30×) at 8 s |
| **SIM 2's trim** (case 12) | `case12_f16_zero_inertial_trim.yaml`, the same way | **rate_q** (8.83e-7), with pitch | q ≈ 0 against −1.37e-4 (155×); pitch about 1.1e-3 rad (20×) at 8 s |

Two findings come before any run:
- **Izx's sign is not expected to fail case 11, or 12.**
  - With trim rates of about 5e-5 rad/s, Izx only re-mixes the aero rate terms' L and N into ṗ and ṙ.
  - The linear lateral model (DML derivatives at the trim α; it reproduces the members' roll drift above) puts
    the flipped run within 0.33 of every lateral band.
  - R9 lists Izx for (11, 13.x), and Q24 says a defect that does not fail its family is reported to Joe, not
    dropped.
  - **Recommendation:** fly it as a **reported** control. It asserts only that the run completes, and that the
    defect is in effect: the derivative at the trim state with the perturbed ω_b differs from the positive
    control's. It logs every ratio. The PR body records "Izx's sign does not discriminate in the trim cases", and
    13.x (PR 11a, with p and r near 1 rad/s) carries Izx's R9 clause.
  - If the run fails a band after all, that is reported too.
- **An elevator sign flipped consistently is invisible open loop.** If the aero and the trim both read a flipped
  sign, the trim finds +el, and the trajectory is identical.
  - So the plant is the trim/plugin disagreement: a committed trim applied with the wrong sign, as a twice-done
    ICD conversion would give.
  - The plugin's own sign is pinned by F16Aero's checkData and R2's physical-response tests. 13.x's closed loop is
    where a law's sign error shows.
  - **Decided:** plant it as above.

**Why the elevator plant's dive stays in range through t = 8 s** (amendment 7). The estimate is a *computed*
longitudinal 3-DoF of the DML: tables, J2 |g|, ISA, trim thrust held, and el = +3.2328° from frame 0. Over 0 to
8 s:

| Quantity | Range over 0 to 8 s | Limit | Source of the limit |
|---|---|---|---|
| Altitude | 3,052 m down to 2,243 m | −5,000 to 86,000 m | isa1976's domain, the only out-of-domain check this config can trip (`isa1976.cpp:163`) |
| α | −9.9° to +2.6° | ALPHA1's −10° to 45° | edge crossings clip (DAVE-ML "neither"); they never halt |
| True airspeed | 169 to 189 m/s | | |
| Mach | ≤ 0.57 | 0 to 1 | the engine's table range |
| Altitude, in feet | 7,360 to 10,013 ft | 0 to 50,000 ft | the engine's table range |
| \|n_z\| | ≤ 3.3 | | |
| θ | down to about −89° at 8 s | | |

- The core's halting checks are non-finite state, wrench, input and environment, the quaternion norm, and out of
  domain (`health_monitor.hpp:33`). None of them is an attitude limit. The state is a quaternion, so θ near −90°
  halts nothing. The harness's Euler extraction stays finite there.
- The control stops at its first gated exceedance, which the estimate puts at sample 80 (force_z). The run never
  reaches the inverted part of the dive.
- α's margin to the −10° edge is 0.1°. Crossing it clips the tables, so it cannot halt the run, but the estimate
  is disclosed.

## Smaller decisions (decided as recommended, amendment 11)

- **D1. Config location.** `aircraft/f16/configs/case11_trim_check_subsonic.yaml` and
  `case12_trim_check_supersonic.yaml`.
  - The M7 plan's tutorial runs "`astra trim` and `astra run` on case 11's open-loop config" (`:389`), and PR 6b
    deleted `aircraft/f16/configs/cruise.yaml` for "PR 9 writes the F-16's configs".
  - The harness finds them through the existing `ASTRA_AIRCRAFT_DIR` define.
  - The SIM 2 variants are test fixtures, so they go in `tests/physics/nesc/`.
- **D2. "Rebuilds its trim bit for bit" is shown by evidence, not a pytest** (Joe limited `tests/python` to a
  config-key need).
  - `docs/reviews/evidence/m7-pr9-trim/` holds each emission's exact `astra trim … --airspeed V --altitude H
    --heading ψ --rate-convention n_frame` command (explicit setpoints, so no |v_b| roundoff enters) and its
    stdout.
  - It also holds a short round-trip script that imports `astra` and applies PR 5's criterion: the config built
    from the merged fragment gives `TrimResult.state`, the controls and the `derivative` residual bit for bit.
  - The C++ R4 test then guards against a stale trim: a plugin change that breaks the trim fails its 1e-6 bound.
- **D3. The ungated log.** For each channel, the harness prints:
  - sample 0's ratio |d|/tol_abs;
  - the maximum ratio over samples 0 to 79, and its sample;
  - the maximum gated ratio, and its sample;
  - whether the channel is zero-accepting (ADR-0015's test, computed from the header).
  Every gated sample is asserted. Ungated samples are never asserted. A per-sample dump is not added unless Joe
  wants one.
- **D4. Exclusions, pre-registered (ADR-0028 item 2): none.**
  - Footnote (a) is the window, and (b) is SIM 2, already out of the band.
  - (c), case 12's tiny vertical velocity, explains and excludes nothing, so v_down is gated.
  - (d) to (f) do not apply to 11 and 12, and unfootnoted "0.000" cells are not exclusions.
  - So all 21 channels are asserted at samples 80 to 1800 in both cases.
  - Zero-accepting, from the headers (matching the preview), asserted and **never cited as evidence**:
    - case 11: v_down, force_y, moment_l, moment_m, moment_n;
    - case 12: v_down, moment_l, moment_m, moment_n.
- **D5. A guard on Q5's route.** At every sample, the aero instance plus a second evaluator instance of the
  engine equals `v0.wrench_total.*` by PR 7's `matches_relative` (D5, 1e-12 normwise). This costs one engine call
  per sample. Q3 above uses the same IndependentWrenchSource route.

## Mismatches with the brief (for Joe)

- **M1.** The M7 plan's Obligations table has no PR 9 row. PR 9's carried obligations live in STATE.md's thread
  (D8, the throttle, `copy_inputs()`), Q7's amendment 2 and Q16's amendment 8 (PR 13 later sets case 12's Mach
  action). This plan takes them from there.
- **M2.** One file outside `tests/physics/nesc`: `tests/physics/CMakeLists.txt` gains the new sources in
  `astra_physics_tests` and its header comment, as PR 7's M2 did. The configs are under `aircraft/f16/configs/`,
  per D1. Evidence goes under `docs/reviews/evidence/`.
- **M3.** The config-key reference lists plugins, not configs (`scripts/config_keys.py`). The new configs add no
  entry, so the expectation is **byte-identical as a whole**: `python3 -I -B scripts/config_keys.py --check`.
  - The four new tracked configs (two cases, two SIM 2 fixtures) must build under `test_config_keys.py` with no
    `EXPECTED_REFUSALS` change. The F-16's plugins read no key.

## Harness design (`tests/physics/nesc/nesc_f16_open_loop_test.cpp`)

One helper flies a case: `fly(case_name, config, band, mode)`.
- **Setup.** `Simulation::from_config`. One `FrameStartEnvironment`. Two `IndependentWrenchSource`s:
  `vehicle.models[0]` is the aero and `[1]` the engine.
- **Per sample k = 0 to 1800,** in the R12 order:
  1. Copy the pre-step state, and form the channels from it:
     - lat, lon, alt from `p_n`, with the altitude ellipsoidal (Q2);
     - v_n = `rotate_b_to_n(q_nb, v_b)`;
     - the Euler angles from `q_nb`;
     - p, q, r as ω_b (inertial, as `bodyAngularRateWrtEi`).
  2. `run(1)`, with any halt fatal.
  3. **`copy_inputs()`** on both instances.
  4. env = `FrameStartEnvironment::at(sim.vehicles().front().environment(), pre_step)`.
  5. The aero channels are the aero instance's `at(pre_step, env)`, about the CG (= CM, D4).
  6. The atmosphere comes from the latched environment.
  7. The D5 guard runs.
  8. `run(99)`, except after the last sample.
- **The difference.** It is taken against the band's reference, wrapped for yaw and longitude (ADR-0028
  item 6).
- **Gating.** `mode = kGate` asserts `|d| ≤ tol_abs` for every channel at k ≥ `Band::kFirstGatedSample`, by
  `EXPECT_LE` naming the case, channel, sample, t, actual, reference and tol. It logs D3's lines.
  `mode = kControl` asserts nothing per sample. It records per-channel maxima and the first gated exceedance, and
  it may stop at a named channel's first gated exceedance.
- **The frame count** (amendment 8). The loop drives 180,001 frames: for k = 0 to 1799, `run(1)` then `run(99)`,
  then a final `run(1)` for sample 1800. The configs keep `sim.frames: 180000`.
  - `Simulation::run(n)` steps n frames and is not bounded by the config's frames (`simulation.cpp:811-822`).
    `frames_` is read only by `from_config` (`:330`), the lockstep exchange (`:471`) and `:555`. M2's cases drive
    30,001 frames against `frames: 30000` the same way.
  - So sample 1800's aero channels are the 180,001st frame's frame-start wrench, at the state `astra run`'s
    180,000 frames end on (t = 180 s).
  - Before commit 3, this is read in the core again, and the harness asserts `sim.clock().frame() == 180001` at
    the end.
  - Each config's header comment says the same: `astra run` flies 180,000 frames to the state at t = 180 s, and
    the harness drives one frame more for that sample's wrench.
- **The window comes from the header.** `kFirstGatedSample` and `kSampleCount` are read from the header type,
  never restated as 80 or 1801 in the harness. NescF16Band already restates them from the ADR.

The tests (all match `Nesc.*Case1[12]`; `-N` predicted **9**, all in `astra_physics_tests`, prefix `physics.`):

| Test | Asserts |
|---|---|
| `NescF16.Case11TrimResiduals`, `NescF16.Case12TrimResiduals` | Q1's R4 check, and Q3's throttle range |
| `NescF16.Case11OpenLoop`, `NescF16.Case12OpenLoop` | R5: 21 channels at every gated sample; no halt; D5 guard; D3's log |
| `NescF16Control.Case11MassRoutePositiveControl` | Q4: derivative bit-identical, both states |
| `NescF16Control.Case11IzxSignReported` | Q4: no halt, completes 180 s, the defect is in effect, ratios logged, no band asserted (amendment 6) |
| `NescF16Control.Case11ElevatorSignFailsForceZ` | Q4: force_z exceeded at a gated sample |
| `NescF16Control.Case11ZeroInertialTrimFailsRateQ`, `…Case12…` | Q4: rate_q exceeded at a gated sample |

No existing test name matches the regex. `Nesc.Case10…` does not, and neither do `NescF16Band.*` or the
`NescAero` unit tests.

`independent_evaluator.hpp` is not edited. If an edit turns out to be needed, the same commit fixes its header
comment's wrap artifact (STATE.md, #116's follow-up).

## Commits

| # | Content | Paths |
|---|---|---|
| 1 | This plan, Status "approved by Joe, 2026-10-13, on his word"; push; draft PR. If the classifier refuses the write, it is not retried by any route: the PR body names the refusal, and Joe instructs the write (amendment 10) | `docs/plans/m7-pr9-open-loop.md` |
| 2 | The configs with their trim evidence, and Q1's derivation | `aircraft/f16/configs/case1{1,2}_*.yaml`; `tests/physics/nesc/case1{1,2}_f16_zero_inertial_trim.yaml`; `docs/reviews/evidence/m7-pr9-trim/`; `docs/reviews/evidence/m7-pr9-residual/` |
| 3 | The harness, R4's residual tests | `tests/physics/nesc/nesc_f16_open_loop_test.cpp`, `tests/physics/CMakeLists.txt` |
| 4 | The negative controls | `tests/physics/nesc/nesc_f16_negative_controls_test.cpp`, `tests/physics/CMakeLists.txt` |
| 5 | Gate evidence | `docs/reviews/evidence/<head>-gate-summary.log` |

Then comes the Codex adversarial review (`/review-cycle`, at most two correction rounds). Its focus names the
gating and exclusion logic, the residual derivation and the plants. Then `/human-review`. Joe reads the configs,
the harness's gating and exclusion logic, and the residual derivation.

**Each config** carries a header comment. It cites Vol II Table 35 or 36, converted with `core/util/units`:
- p_n = (lat, lon, h) in rad, rad and m;
- V = |(400, 400)| ft/s, or |(1414.213562, 1414.213562)| ft/s;
- ψ = 45°.
It also names the exact `astra trim` command and D8's reading. The models are the F-16's three, with
`wgs84_rotating`, `wgs84_j2` and `isa1976`, still air, and `sim.frames: 180000`. The pair is consistent, which is
why PR 9 does not depend on PR 3.

## Stop conditions (each goes to Joe; nothing is widened, excluded or re-trimmed to pass)

- Any gated sample outside its band in `Case1{1,2}OpenLoop`.
- R4's solved residual above 1e-6, or a lateral miss beyond Q1's tabled bounds.
- The script's independent trim off the config's by more than 1e-6 in θ, the elevator or the throttle.
- A config scalar whose text fails the script's decimal regex (amendment 12).
- A halt in either case, or in any control before `Band::kFirstGatedSample`.
- A control that does not fail its pre-registered band. Izx in case 11 is reported, not gated.
- A D5 guard miss.
- The positive control not bit-identical.
- Any need to touch `core/`, `plugins/`, `tests/golden/`, a tolerance file, `docs/decisions/`, or aircraft/f16's
  aero, mass or engine. A defect found in those comes to Joe, never a fix here.

## Verification

1. `ctest --preset release -N -R 'Nesc.*Case1[12]'`, first on the base build (before commit 3), where the count
   must be **0**: `NescF16Band.*` must not match. Then on the candidate, where it must be **9**, before the gated
   run (amendment 9). Then `ctest --preset release -R 'Nesc.*Case1[12]' --no-tests=error` (`|` in a shell is
   never written `\|`).
   - The PR body gives each channel's largest gated ratio per case, citing only the channels that are not
     zero-accepting.
   - It gives the D3 ungated maxima, each control's exceeded band, sample and ratio, and Izx's ratios.
   - It gives R4's values and misses, and the D5 guard's maximum.
2. `ctest --preset asan -R NoAlloc --no-tests=error`, unchanged (F16NoAlloc and the physics NoAlloc).
3. `ctest --preset asan -N -R F16`, then `ctest --preset asan -R F16 --no-tests=error`. The selector also matches
   the nine new `NescF16.*` and `NescF16Control.*` tests, including the two 180,001-frame cases under ASan, so
   this step records its `-N` count and wall time (amendment 14).
   - The unchanged-set claim: F16Aero, F16Engine, F16Mass, F16NoAlloc and F16Evaluator pass;
   - `git diff origin/main..HEAD -- aircraft/f16` touches only `aircraft/f16/configs/`.
4. `python3 -I -B scripts/config_keys.py --check`, byte-identical (M3).
5. `.venv/bin/pytest tests/python -k config_keys`: the four new configs build.
6. Gate time. The release and ASan wall times of the new tests are measured and reported: the gate runs debug,
   release, static-plugins and asan, and the gated cases are 180,001 frames each. A large increase goes to Joe
   (Q23); no label changes without him.
7. `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`. The pre-registered blast radius is
   unchanged:
   - M2's cases 1 to 10, M3's regression and the R1 golden;
   - M4's logs and M6's fixtures.
   The trimmed summary is committed.
8. `git diff --check origin/main..HEAD`.

Results go in the PR body: commands, exit codes, counts, the Codex verdict and thread id.

## /wrap (before Joe's merge)

- **PLAN.md:** the PR 9 line.
- **Dated amendments, 2026-10-13, each one line pointing at this plan:**
  - Q2's answer, under the addendum's D8 bullet and beside the M7 plan's Assumption 1 (amendment 4);
  - R9 in the M7 plan: Izx's sign is reported in 11 and gated in 13.x, and PR 11a carries the clause
    (amendment 6).
- **STATE.md,** as main reads it after the merge:
  - PR 11a and PR 13 are eligible, and PR 3 waits on Joe's go;
  - the PR 9 thread closes, with D8's answer and the `engine.cpp` comment note;
  - PR 11a carries the R9 Izx clause.
- **Before the merge:** `git fetch`. If main has moved:
  - merge `origin/main` in;
  - rewrite STATE.md and PLAN.md;
  - rerun what the tier requires for what the merge touched.

## Risks and rollback

- **Tight bands.** A miss is a finding for Joe (the stop conditions above). The likeliest causes are named in
  advance: the trim's θ (the script agrees with SIM 5 to 2e-7 rad), the J2 constant's 3e-6 provenance (PLAN.md's
  `kJ2` item), and the members' own drift.
- **Gate time** (verification step 6).
- **Rollback.** Revert commits 4, 3 and 2 in that order. Each stands without the later ones.
