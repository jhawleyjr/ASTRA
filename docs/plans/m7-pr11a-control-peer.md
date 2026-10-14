# M7 PR 11a: the sim-side F16_control peer and cases 13.1 to 13.4 (Tier 3)

Status: **approved by Joe, 2026-10-13, on his word**, with his amendments 1 to 14 folded in (below, "Joe's
decisions"). Every number in this plan stays pre-registered as written; the amendments add checks and fix scope,
and move no band, bound or expectation.
Name: the plan-mode draft (main checkout, untracked) was moved (`mv`, before its first add) to
`docs/plans/m7-pr11a-control-peer.md`. Nothing is left in the main checkout.
Owner: Joe. Builder: Claude, one builder. Tier: **3** (a new aircraft plugin at the exchange seam, and an
oracle harness against ADR-0028's bands).
Base: `origin/main` = `86b17ce` (#118), confirmed by `git fetch` after approval. The worktree `m7-pr11a`
(branch `worktree-m7-pr11a`) is at `86b17ce`.
Binding: ADR-0002, 0007, 0017, 0022, 0028, 0029, 0032, 0033 (all accepted).

**The design is pinned.** This plan cites it and does not redesign it. There is no Codex plan critique (Joe).
- The M7 plan (`docs/plans/m7-f16-nesc-oracle.md`): row 11a (`:857`); R4's 13.x half (`:403`), R6 (`:405`), R9
  as amended 2026-10-13 (`:408`), R10 (`:409`), R11 (`:410`), R12 (`:411`), R22 (`:420`), R23 (`:421`); Q8
  (`:573`), Q10 (`:596`), Q11 with amendment 5 (`:608`), Q12 (`:622`), Q26 (`:738`), Q27 with Joe's two
  amendments (`:754`); the three Obligations rows for 11a (`:821`, `:824`, `:825`).
- ADR-0029 Decisions 1 to 7 and 9; ADR-0028 (window, footnotes, zero-accepting rule); PR 9's
  `tests/physics/nesc/f16_flight.hpp` and its plan's rules (amendments 7 and 12 to 14).
- STATE.md's PR 11a thread: 13.4's base point latches from the frame-0 exchange; the new plugin needs a key block
  (PR 14's rule); the first edit of `f16_flight.hpp` fixes its wrap artifact.
- The measured band preview (amendment 7, rows 13.1 to 13.4).

## Joe's decisions (2026-10-13)

The plan is approved on Joe's word, with these amendments. Where a later section says otherwise, this list
governs.

1. **Scope (M4).** `tests/plugins/nesc_control_variant` is accepted, with `tests/physics/CMakeLists.txt`,
   `aircraft/f16/tests/CMakeLists.txt` and the two evidence directories.
   - The variant plugin depends on the peer only through `control_law.hpp` and `nesc_control_peer.hpp`, never by
     linking the shipped `.so`.
   - `nesc_control.cpp` and its descriptor carry no variant or test key.
   - M1, M3 and M6 are accepted as stated.
2. **D8: PR 9's test sources are not edited.** `nesc_f16_open_loop_test.cpp` and
   `nesc_f16_negative_controls_test.cpp` stay byte-unchanged.
   - The generalized helpers (trim residuals by config path, the constant-mass swap by config path) are new
     functions in `f16_flight.hpp`, and the Case13 tests use those.
   - The wrap-artifact fix and the additive `FlightOptions` and callback stay.
   - "PR 9's cases 11 and 12 unchanged" means their sources too.
3. **D9: the exchange limits.** In each of the four configs, each limit is written as the `%.17g` text of the
   same core expression the law saturates with: `degrees_to_radians(25)`, `degrees_to_radians(21.5)`,
   `degrees_to_radians(30) + 0.008 * degrees_to_radians(21.5)`, and 1.
   - `F16Control.ExchangeLimitsContainTheLawsRange` reads the limits from each tracked config through `Config`,
     not from a C++ constant. It asserts that the law's saturated outputs pass the exchange's own inclusive check.
   - The case this protects: 13.1's elevator at frame 5000, exactly −25°.
   - A `kCommandLimit` halt in a nominal case is a stop condition.
   - *Wording fix, 2026-10-13 (Joe; amendment 3 as applied).* The rudder limit is the law's own saturated
     expression, `degrees_to_radians(30) + 0.008 * degrees_to_radians(21.5)`, in the law's factor order. It is not
     `degrees_to_radians(30 + 0.008 * 21.5)`, a different expression that can differ by one ulp. The YAML limit
     is the `%.17g` text of that value.
     - This is not a number move: the limit is still the law's range.
     - The test also asserts that each configured rudder limit is bit-identical to the law's saturated value, as
       it does for the elevator and the aileron. A future rewrite of either expression then fails the test,
       rather than halting 13.3 or 13.4 at the course step.
     - Q2's rudder full scale for the floor stays 30.172°: it is a scale, not a limit, and one ulp there is
       immaterial.
4. **The round trip.** `astra trim` refuses a transport block by design (ADR-0029 Context, Q9 (b)), so this is
   not an open risk.
   - Commit 4's evidence: case 11's base round trip rerun on this branch (PR 9's route, the same command, D3's
     check), plus the pre-registered bit identity of each 13.x config's state and control text with case 11's.
   - Verification 4 keeps the exit-65 check on the 13.x configs.
5. **The key reference.** Every commit that changes the key block regenerates `docs/reference/config-keys.md`,
   so commit 3 regenerates it as commit 2 does. Commit 3's message says `test_config_keys.py` is expected to fail
   until commit 4.
6. **D1's frame-0 EAS check** in `Case13p*ClosedLoop` is **1e-9 relative**, not 1e-12.
   - It guards a stale literal, which a re-trim moves by far more.
   - The literal and the peer come from two independent ISA implementations.
   - R12 proper keeps 1e-12, where both sides read one `AtmoState`.
   - Fixed now, before the first run.
7. **Q8's refusal signal.** The two synthetic bits (`0x100` for Mach not above zero, `0x200` for the base-point
   frame) are declared above ICD v1's highest status bit, with a `static_assert` that neither overlaps any
   `ASTRA_ICD_STATUS_*` mask.
   - The plan states how the tests read `transport.peer.refusal` after a frame-0 halt (Q8).
8. **Name the per-sample guard in `Case13p*ClosedLoop` before the first run:** what it compares, against what
   independent evaluation, and at what bound. The harness section ("Per-sample guards") defines each one.
9. **D10 (M2)** is accepted as placed. The PR body records it as a deviation from ADR-0029's Consequences line
   ("`tests/physics/no_alloc_test.cpp` gains a peer config"), with the reason: the brief keeps that file and
   F16NoAlloc unchanged. No ADR edit.
10. **Q4 is accepted: no refusal.**
    - Add one test that each tracked 13.x config pairs the peer with `physical.earth` =
      `earth/wgs84_rotating`, so a config edit that drops rotation fails the suite (Q4's mitigation names it).
    - The peer's header and key block state the assumption.
    - At /wrap the answer goes into STATE's PR 3 line, and nowhere under `docs/decisions/`.
11. **Q7's Izx:** gated in 13.3, reported in 13.1, 13.2 and 13.4, as proposed.
    - The 13.3 gate names force_y and moment_n as the expected channels, and passes on any discriminating channel
      (PR 9's form).
    - If 13.4 also leaves a band, that is reported in the PR body, never gated after the fact.
12. **Q1:** the PR body shows the copied `derive_residual.py`'s diff against PR 9's script. The diff is limited
    to the `CASES` table and the evidence paths; anything else in it is a stop condition.
13. **M5** (`trimmedKEAS` inert with the autopilot on) is a reported finding, and Q11 stands. The /wrap docs pass
    adds one dated line (2026-10-13) under the M7 plan's Q11 pointing at this plan's Context, and the PR body
    carries it.
14. **Accepted as written:** Q2, Q3, Q5, Q6, Q8, D1 to D7, D11 and the stop conditions.

## Joe's decisions after the measurement (2026-10-13)

*Recorded 2026-10-13.* Joe made these decisions after reading commit 6a's results: the R6 finding, the reported
variants, the lag diagnosis and the figures (`docs/reviews/evidence/m7-pr11a-r6/`). The M7 plan's amendment 5
governs them. They are recorded as decisions made after the measurement, never as pre-registered ones. Where an
earlier section of this plan says otherwise, this list governs, and the earlier text stays as the pre-registered
record.

1. **Q11 is amended** (a dated line under the M7 plan's Q11).
   - The LQR's reference angle of attack and pitch are ASTRA's own trim θ (α = θ at γ = 0). The reference airspeed
     is the initial equivalent-airspeed command.
   - Joe's rationale: the references and the trimmed stick must describe one equilibrium. SIM 4 reached that
     through its stick trim (PR 1b's `compare.log:232`), and SIM 5 through its references. The DML's hard-coded
     numbers are one participant's trim solution.
   - The hard-coded references become the reported variant: `dml_references` in
     `tests/plugins/nesc_control_variant`, replacing `sim5_references`, flown by
     `NescF16Variant.Case13p{1-4}DmlReferences`.
   - One new key, `peer.trim.theta` (rad). It is checked bit-identical to `initial.state.euler_nb[1]` by D3's test
     and by the round-trip script, and the round trip also checks it against the trim's solved `alpha_rad`. The
     key block and `docs/reference/config-keys.md` are regenerated.
   - ADR-0029 Decision 9 cites Q11 to the plan, so no ADR changes.
2. **Q12 stands: divisor 10.** ADR-0029 Decision 4 is not superseded. Commit 6a's divisor-1 runs and the lag table
   stay in the evidence as the record of why the maneuver transients differ.
3. **R6 for 13.3 and 13.4, Joe's decision as oracle owner.**
   - `Case13p3ClosedLoop` and `Case13p4ClosedLoop` gate every kinematic channel at every gated sample. The six aero
     force and moment channels are reported, each with its largest ratio.
   - R6 is recorded as **not met** for those six channels in those two cases. The reason: the misses are at the
     command-step samples. There, ASTRA's 100 Hz law trails members that ran faster by 30–40 ms, and the members
     differ from each other by about as much. ADR-0028 already names these bands loose.
   - This is a deviation from R6 and from D7. It is recorded here, in the PR body and in PLAN.md's PR 11a line,
     never as a pass.
   - 13.1 and 13.2 keep all 21 channels gated.
   - *As built:* the asserted set in 13.3 and 13.4 is every channel but the six aero ones, 15 in all: latitude,
     longitude, altitude, the three velocities, the three Euler angles, the three rates, density, pressure and
     temperature. Joe's list names density. Pressure and temperature are not aero channels, so they stay gated:
     the reading that gates more. `FlightOptions::reported` in `f16_flight.hpp` carries the choice. It is
     additive and reports nothing by default, so PR 9's tests are unchanged.
4. **The rerun.** The four decided runs are flown again under item 1 at divisor 10, and the table is shown again.
   - Joe's expectation: 13.1 and 13.2 inside every band; 13.3 and 13.4 inside every kinematic band.
   - Any kinematic miss comes back to Joe and stops the PR.
   - *Result:* as expected (`amended_table.md`). The largest asserted ratios are density 0.418 (13.1), rate_q 0.786
     (13.2), roll 0.798 (13.3) and roll 0.938 (13.4).
   - The reported channels above 1: 13.3's moment_l 2.40, force_y 1.44 and moment_n 1.44, at sample 156; and 13.4's
     moment_l 1.52 (first at 200), force_y 1.13 and moment_n 1.14, at 206.
   - Guards: G1 0, G2 at most 2.3e-15, G4 0 mismatches, G5 1.15e-15.
   - Commit 6a's SIM 5 rows equal the amended decided rows, and its decided rows the DML-references rows, in every
     channel of all four cases.
5. **D11 at divisor 1**, one more reported run set, so the record says where the oracle would have landed with
   both changes. `NescF16Variant.Case13p{1-4}Divisor1` now fly the trim's references at divisor 1. Reported only.
   - *Result:* 13.1, 13.2 and 13.3 are inside every band, with largest ratios v_down 0.411, density 0.562 and v_down
     0.198.
   - 13.4 keeps moment_l 1.30 at sample 200 (the offset step) and is inside every other band.
6. **Commit 6b under item 3's gating:**
   - the elevator-sign plant asserts pitch in all four cases;
   - the aileron-sign plant asserts roll in 13.3 and 13.4;
   - the wrong-divisor control is reported: "divisor 1 within every gated band", as R9 allows. The `Divisor1` runs
     print that line against the decided gating, and no `WrongDivisorFails` test exists;
   - Izx's sign is reported in all four cases (`NescF16Control.Case13p{1-4}IzxSignReported`), since its only
     expected exceedances, force_y and moment_n, are now reported channels. R9's Izx clause was first to be
     recorded as "not discriminated by the 13.x kinematic bands"; the measurement below contradicted that, and
     Joe's final wording closes it (the last bullet of this item);
   - the positive control stays (`NescF16Control.Case13p3MassRoutePositiveControl`).
   - *Result:* the elevator-sign plant fails pitch at sample 80 in all four cases, at ratios 76.3, 436, 5.58 and
     5.17, with no halt. Q7's *scratch* gave 179 to 14,400. The aileron-sign plant fails roll at sample 80 in 13.3
     and 13.4, at 2.70 and 3.31 (*scratch*: 37 and 97). The positive control is bit-identical.
   - *Result, for Joe:* the measurement contradicts the Izx clause's wording. 13.3's flipped Izx leaves the
     **latitude** band, a kinematic channel: 1.07 at sample 300, first at sample 257. It also brings force_y (1.44
     → 0.76) and moment_n (1.44 → 0.77) inside, and lowers moment_l (2.40 → 2.17). 13.4's flipped Izx stays inside
     every asserted band; its moment_l falls from 1.52 to 1.38, and force_y and moment_n come inside. 13.1 and
     13.2 stay inside every band. The control stays reported, never gated after the fact (amendment 11). The
     clause "not discriminated by the 13.x kinematic bands" is not recorded as worded, because it does not hold
     for 13.3. Its wording is Joe's.
   - *The Izx clause, Joe's wording (2026-10-13, after the measurement), which closes it:* "Izx's sign is a reported control in 13.x. In 13.3 the flipped sign leaves the latitude band (1.07 at sample 300, first out at sample 257) and moves force_y and moment_n inside theirs (1.44 to 0.76 and 0.77); 13.1, 13.2 and 13.4 stay inside every asserted band. It was pre-registered as gated on force_y and moment_n in 13.3, which became reported channels under Joe's R6 decision, so it stays reported; a gate on latitude would be chosen after the measurement and is not added. The clause closes here: the sign is discriminated weakly, by one kinematic band in one case."
7. **Then** the gate, the Codex review (at most two rounds) and `/human-review`, as planned. The PR body's R6
   section leads with item 3's recorded failure.
8. **/wrap.**
   - PLAN.md's PR 11a line states the two amendments (Q11 and R6's 13.3/13.4 gating) and the recorded R6 result.
   - PR 11b, 12 and 15 move to unscheduled items, one line each: "the NESC closed-loop ensemble is two members
     with loose maneuver bands; Joe, 2026-10-13".
   - PR 13 stays eligible as a framework feature.
   - STATE's next step: a planning PR for the closed-loop oracles that replace them, which Joe will brief
     separately. Those oracles are an exact sampled-data closed-loop check on a linear plant, the port pattern on
     M4's autopilot, and symmetry tests.
   - This replaces the earlier /wrap section's "PR 11b and PR 13 eligible, PR 12 waits on 11b", and its carrying
     of the rate-frame and Izx clauses to 11b.

## Context

Cases 13.1 to 13.4 fly the F-16 from case 11's trimmed initial condition with `F16_control.dml` engaged at
frame 0: +100 ft at 5 s, −5 KEAS at 5 s, +15° course at 15 s, and a 2,000 ft right side step at 20 s
(Vol II Tables 37 to 40, C.1.13 to C.1.16). The law runs in a new in-process transport plugin,
`aircraft/f16/sim/transport/nesc_control`, on the ICD v1 frame the exchange packs (ADR-0029 Decision 1). The
harness flies the four cases against ADR-0028's bands on PR 9's `f16_flight.hpp`, and the negative controls and
reported variants show what the bands can and cannot see. PR 12 ports the law; nothing of it is here.

### What the sources fix (observed, cited)

- **The initial condition is case 11's, in all four cases.** Tables 37 to 40 repeat Table 35 exactly
  (36.01916667°, −75.67444444°, 10,013 ft; [400, 400, 0] ft/s; heading 45°; rates 0), and C.1.13 to C.1.16
  each say "the same initial conditions as atmospheric case 11". So the trim is case 11's, bit for bit: the same
  `astra trim … --airspeed 172.42091752452774 --altitude 3051.9624000000003 --heading 0.7853981633974483
  --rate-convention n_frame` on the same base (`docs/reviews/evidence/m7-pr9-trim/trim11_n_frame.out`).
- **Durations** (the band headers, Vol II): 13.1 and 13.2 run 20 s (201 samples), 13.3 30 s (301), 13.4 60 s
  (601). `kFirstGatedSample` is 80 in all four.
- **The law** (`F16_control.dml` Rev F, read with a MathML printer in plan mode): memoryless; Rev F's OR
  (`fsasOn = sasOn + apOn`); the LQR's hard-coded references `trimmedAlpha = trimmedTheta = 2.653813535191715°`
  and `trimmedKEAS = 287.8088596053291`; the chiErr wrap at ±180°; every `minValue`/`maxValue` a saturation.
  Inventory addendum §5 matches it line for line.
- **The DML's `trimmedKEAS` is inert with the autopilot on.** `keasCmdSw = keasCmd` when `apOn > 0.5`, and
  `trimmedKEAS` feeds only the autopilot-off branch. In 13.x, Q11's decided choice (the hard-coded references)
  acts only through `trimmedAlpha` and `trimmedTheta`: 2.6538° against ASTRA's trim θ = α = 2.6389°, a
  −0.0149° offset in both Δα and Δθ. Vol II D.1.13's prose attributes SIM 4's −0.11 ft/s airspeed offset to
  "287.8 KEAS" against "the trim solver's speed of 287.98 KEAS". The MathML does not route `trimmedKEAS` there.
  This is reported to Joe (M5) and changes nothing in the decision.
- **The initial airspeed command is the trim's own EAS.** D.1.14: "a command to decrease speed 5 kt to 282.98
  KEAS", so the initial `equivalentAirspeedCommand` is 287.98 KEAS, the trim solver's speed. ASTRA's IC gives
  287.9815820335 kt (ISA-1976 at 3,051.9624 m; ρ = 0.9044035853 kg/m³, ρ0 = 1.2249991559 kg/m³ from
  `icd_pack`'s constants).
- **What the law commands** (the MathML, evaluated in plan mode at ASTRA's trim with the Earth-relative rate):
  - frame 0: el −4.4550° against the trim's −3.2328°, PWR 14.150 against 13.874, ail −1.19e-4° (+LWD), rdr
    −3.62e-4°. The hard-coded references kick the pitch at engagement: q̇ = 0.217 rad/s² at frame 0, inside the
    ungated span (footnote (a)'s "initial transients");
  - 13.1 at frame 5000: el −25° (the stick saturates: ΔθCmd lands exactly on its +5° limit) and PWR 100;
  - 13.2 at frame 5000: el −12.33°, PWR 0 (saturated);
  - 13.3 at 15000 and 13.4 at 20000: ail −21.5° +LWD (saturated; ICD +0.37525 rad, right wing down), rdr
    +29.828°.
  - The law disengaged (SAS and autopilot off) returns the trim's −3.23275769647866° and PWR 13.873817846824657
    exactly (R23's premise).
- **The exchange recorder is sized from `sim.frames`** (`lockstep_exchange.cpp:103-105`, `ExchangeRecorder::
  can_record`). PR 9's harness drives 100(N − 1) + 1 frames, one more than `sim.frames`. With a transport, that
  extra frame is an exchange frame, and the run would halt `kRecorderFull` (source `transport.recorder`). The
  13.x configs therefore set `sim.frames` to 20,001, 20,001, 30,001 and 60,001 (D6).
- **The peer cannot see the Earth model.** `Transport::init` receives only the `transport` block
  (`lockstep_exchange.cpp:150`), `Config` has no access to the document root (`config.hpp`), and no Earth-rate
  signal is registered. This decides Joe's question 4.
- **Unknown keys are errors** (`simulation.cpp:481`), and `test_config_keys.py` requires every documented key to
  be consumed by a tracked config's build, so every key the peer documents must appear in one of the four configs.

## Joe's questions (each with a recommendation; decided before the first run)

Numbers marked *scratch* come from a read-only plan-mode computation that wrote nothing: a flat-Earth 6-DoF with
`derive_residual.py`'s DML aero and engine, ISA-1976, |g| = 9.7888408 m/s², and the law evaluated from the
MathML by `rate_frame.py`'s `DaveModel` at 100 Hz zero-order hold. It reproduces 13.3's maneuver shape (roll
peak 0.645 rad against the reference's 0.615, rate_p 2.67 against 2.69 rad/s). It is flat and non-rotating, so
it cannot be compared with the bands themselves. Only its *differences* between a variant and its own nominal
run are used, as ratios to each band's `tol_abs`. It is not committed.

### Q1. The full expected lateral residual at each 13.x initial condition (Q7, R4)

**Recommendation:** each 13.x config carries case 11's trim literals, so each residual is case 11's, derived by
PR 9's route. A copy of `derive_residual.py`, with its `CASES` pointing at the four 13.x configs, reruns the
derivation. That route reads P1's XML with no ASTRA code, checks SHA-256, reads the scalars as text through the
strict regex, and lists the constants first.

| 13.1, 13.2, 13.3 and 13.4 (each) | kinematic | gravity (J2 + centrifugal) | aero rate terms | engine | **total** | bound |
|---|---|---|---|---|---|---|
| v̇_y, m/s² | +1.717976865e-2 | +4.935440258e-6 | −1.363562756e-5 | 0 | **0.017171068467368312** | 9.7888e-12 |
| ṗ, rad/s² | −2.984217492e-9 | 0 | −9.885844340e-6 | 0 | **−9.8888285577672511e-6** | 9.8858e-18 |
| ṙ, rad/s² | +2.126376334e-9 | 0 | +5.995539746e-6 | 0 | **5.9976661226860661e-6** | 5.9955e-18 |

- The solved three (u̇, ẇ, q̇) are at most 1e-6 (D8). The throttle lies in [0, 1] (PR 9's Q3).
- **Pre-registered:** the four configs' state and control text is identical to case 11's, so the script's totals
  at each config's literals equal the constants above bit for bit, and its independent trim agrees within 1e-6
  in θ, the elevator and the throttle. Either differing is a stop condition.
- `NescF16.Case13p{1,2,3,4}TrimResiduals` assert these with PR 9's bounds.
- **The copy's diff** (amendment 12). The PR body shows `diff docs/reviews/evidence/m7-pr9-residual/
  derive_residual.py docs/reviews/evidence/m7-pr11a-residual/derive_residual.py`. It may change only the `CASES`
  table, the `BUDGETS` keys that name the cases, and the evidence paths in the docstring. Anything else in it is
  a stop condition.
  - D1's EAS literal does not come from this script. It comes from the separate `ic_eas.py` in the same directory,
    so the copy's diff stays within that rule.
- **Reported, never asserted:** the derivative under the law's own frame-0 command (above) is v̇_y 0.017124 m/s²,
  ṗ 2.79e-5, ṙ 3.23e-5 and q̇ 0.217 rad/s². That is the law's engagement transient, not the trim's residual.

### Q2. R11's absolute floor and saturation-boundary exclusion margin

**Recommendation:**
- **The floor.** Each ICD command is compared as |c_SI − c_ref| ≤ 1e-12 · max(|c_ref|, F), where F is that
  command's full scale in ICD units:
  - elevator 25°, aileron 21.5°, rudder 30.172° (30 + 0.008 · 21.5), each in rad (4.363e-1, 3.752e-1, 5.266e-1);
  - throttle 1.
  So the absolute floor is 3.8e-13 to 1.0e-12. Why it is enough:
  - The worst unsaturated cancellation on the drawn envelope is about 100 stick units of summands (Δφ 30° times
    3.078), which leaves about 1.7e-14 rad of roundoff. That is 25 times inside the floor.
  - Any unit or sign defect is at least 1.7e-2 relative (ft↔m 0.3048, kt 0.514, deg 57.3), so the floor can
    never hide one.
  - Same shape as F16Aero's 1e-12 · max(1, |c|).
- **The margin.** A seeded frame is redrawn when a boundary quantity, evaluated by the reference before
  saturation, lies within 1e-9 · max(|b|, 1) of its boundary b, in the reference's units. The boundaries: ΔθCmd
  ±5°, ΔχCmd ±30°, φCmd ±30°, the three sticks ±1, the throttle total 0 and 1, and the chiErr wrap at ±180°. The
  margin is 1e6 times the two evaluations' relative disagreement (about 1e-15), and it excludes a measure of
  about 1e-9 of the draws. The test prints the count, which should be 0.
- **Disclosed:** min/max saturation is continuous, so a straddle there moves the output by roundoff only, and the
  margin there is the obligation's redundant safeguard. The **chiErr wrap is a true discontinuity:** at ±180°, φCmd flips
  between +30° and −30°. That boundary is the one the margin protects. Hand-built frames may sit exactly on a
  min/max limit (13.1's ΔθCmd lands on +5° at its step), never on the wrap.
- ***Deviation 1, 2026-10-13 (Joe, two decisions in this PR's commit-2 session): the seeded bound follows the
  law's input conditioning.***
  - **The premise was wrong.** It assumed roundoff scales with the output's summands. The law forms differences
    of its inputs before its gains, and the two implementations round those inputs differently (degrees against
    radians, feet against meters).
  - **The first run on the redesigned draws** (deviation 2): 97 of the 5,000 near-design frames missed the floor
    alone, worst at 1.75 times it. 95 misses were on the aileron, through chiErr = β + ψ − course with |ψ| up to π
    and a gain of about 662 rad/rad. Two were on the elevator, through altErr = altitude − altCmd with altitude up
    to 11 km and a gain of 0.226 rad/m.
  - **The seeded bound per frame and command** is 1e-12 · max(|c_ref|, F) + eps · Σ over rounding steps of |that
    step's own result| · |G|, with eps = 2^-52.
    - G is the law's gain from that step's quantity to the command, derived in the test from the reference's own
      DML constants and conversions, never read from the SI law.
    - Each step between a raw input and the difference the law forms is charged relative to its own result, both
      implementations summed. The counts are named per input in `control_test.cpp`'s comment and printed.
    - **ψ: 3.** The reference's ψ/deg (|ψ|), the reference's β + ψ (|chiEst|), and the SI's β + ψ (|chiEst|).
    - **Course: 3.** The reference's ΔχCmd + base (|chiCmd|), the SI's deg-to-rad conversion (|course|), and the
      SI's Δχ + course (|chiCmd|).
    - chiErr's own subtraction and its wrap are charged at their own (small) results, separately.
  - **The throttle term at low Mach.** In the EAS route, each step carries a derived coefficient on ln V. The two
    pow − 1 cancellations amplify a step's rounding by κ = B^3.5 / (B^3.5 − 1), with B = 1 + 0.2 M², about 143
    at Mach 0.1, and the law's ΔV gain (about one throttle per knot) carries it into the throttle. The EAS route
    does not change: this is roundoff between the two implementations, about 3e-14 relative in EAS, not an error
    in either. ADR-0029 Decision 8 fixes the peer's expression order for the port.
  - **The gains are verified, not capped.** For every frame and command, each gain must agree within 1 % with the
    reference's own central finite difference, with a step of 1e-6 of each input's full scale.
    - Within ten steps of a saturation or chiErr's wrap, the difference is one-sided on the open side.
    - A saturated command is still charged with its unsaturated gain (conservative), and the check skips it.
    - Both branches are counted and printed.
  - Three planted defects must still fail by at least 1e8 times the bound: a deg↔rad slip on longLQR14, the
    aileron's flip omitted, and a ft↔m slip on the altitude gain.
  - The floor F, the margin and the chiErr wrap do not move.
- ***Deviation 2, 2026-10-13 (Joe accepted it, recorded; it is not absorbed by the plan): the seeded draws were
  redesigned after the first design's comparison ran.***
  - **The first design** drew the pilot's inputs past ±1, to −0.2..1.2 for the throttle and ±1.3 for the sticks.
    The other draws were wide: φ ±60°, θ ±30°, α −10..30°, β ±10°, rates ±3 rad/s and course 0..360°.
  - **What it produced:** 425 seeded frames with SAS and autopilot off. A pilot input past its limit is saturated
    before it reaches the totals, so totLatStk and totPedal sat exactly on Q2's ±1 boundaries and were redrawn by
    the margin. The plan named this stop condition ("beyond a handful").
    - Every compared frame agreed (worst 0.75 of the floor). The 425 redrawn frames, compared as a diagnostic,
      agreed to 4e-4 of it. 95 % of the draws saturated a stick.
  - **The new draws:**
    - the pilot's inputs inside their limits; the past-limit cases are hand-built ("pilot inputs past their limits"
      in the MathML anchor);
    - alternate frames near the design point, which keep the LQR's sums inside their limits: φ ±0.1°, β ±0.05°,
      α and θ within 0.1° of 2.6538°, rates ±0.005 rad/s, altCmd within 1 ft, keasCmd within 0.2 kt, and a
      course within 0.005° of the track estimate;
    - the first design's wide ranges, otherwise unchanged.
    The envelope's other ranges are unchanged: latitude ±89.95°, longitude ±180°, altitude 0 to 11 km, heading
    ±180°, Mach 0.1 to 0.95, and static pressure 20 to 110 kPa.

### Q3. Where R12's ρ comes from

**Recommendation:** ρ comes from the same `AtmoState` the ICD frame is packed from, in two places.
- **R12 proper** (`F16Control.EquivalentAirspeedEqualsTasTimesRootDensityRatio`):
  - The test loads `plugins/physical/atmosphere/isa1976`, calls its `at(altitude)`, and packs the frame with
    `icd_pack::pack_sim_to_sw` from that `EnvironmentSample` (still air).
  - It compares the peer's EAS against tas · √(ρ/ρ0) to 1e-12 relative, with ρ = that `AtmoState`'s
    `density_kg_m3`, the sim's own atmosphere output, and ρ0 = p0·M0/(R*·T0) from `icd_pack`'s constants.
  - The grid: altitude 0 to 11 km and Mach 0.1 to 0.95. 13.x's, 15's and 16's 3.0 to 3.1 km and Mach about
    0.5 sit inside it.
- **In flight** (every sample of the four cases): the peer's `transport.peer.equivalent_airspeed` signal (D4)
  against |v_b| · √(ρ/ρ0), with ρ from the vehicle's latched environment, the same source the harness's density
  channel reads (`f16_flight.hpp:232`), to 1e-12 relative.
- No atmosphere signal is logged, and neither check needs one.

### Q4. Does the peer refuse a non-rotating Earth at init?

**Recommendation: no, because it cannot see one.**
- Its `init` gets only the `transport` block, `Config` cannot reach `physical.earth`, and no signal carries the
  Earth rate (Context). A refusal needs a core change: `Transport::init` would have to see the `EarthModel`, or
  `Simulation` would refuse the pairing. That is Tier 3 core work, out of this PR's scope, and nearest PR 3's
  rotation check.
- A key restating Ω would only duplicate the risk ADR-0029 J8 rejected.
- **The exposure is narrow:**
  - a flat Earth sets `LATLON_NOT_MODELED`, which the peer already refuses at every frame, since the rate path
    reads `lat` (ADR-0029 Decision 2);
  - so only a geodetic non-rotating Earth (`earth/round` with `rotating: false`) runs. Its error is Ω =
    7.29e-5 rad/s in the rate fed to the law: about 0.018° of elevator through the q gain (10.11 s/rad), and
    about 0.007° of aileron.
- **Mitigation:**
  - the peer's header and key block state the assumption, a rotating WGS-84 Earth at `geodesy::kOmegaIe`;
  - the four tracked configs pair it with `wgs84_rotating`;
  - **`F16Control.TrackedConfigsPairThePeerWithTheRotatingEarth`** (amendment 10). It finds every git-tracked
    config whose `transport.plugin` is `aircraft/f16/sim/transport/nesc_control` (at least the four 13.x configs;
    the count is asserted). Through `Config`, each must name `physical.earth` as
    `plugins/physical/earth/wgs84_rotating`. So a config edit that drops rotation fails the suite;
  - PLAN.md's existing (M9) ICD v2 Earth-rate item already removes the limitation.
- At /wrap the answer goes into STATE.md's PR 3 line, and nowhere under `docs/decisions/` (amendment 10).

### Q5. Q27's 13.4 sensitivity

The three reported variants against the decided initial-plane line. "Geometric" is each variant's d minus the
decided line's d, evaluated along 13.4's band reference track (WGS-84 radii; the great circle as the ellipsoidal
geodesic at h0). It is also the difference in `lateralDeviationError`.

| Variant | geometric at 20 s | at 60 s | flown (*scratch*), largest gated ratio |
|---|---|---|---|
| Rhumb line through the base point | −0.24 m | −2.38 m | longitude 0.30, latitude 0.12 |
| Great circle through the base point | −0.72 m | −6.68 m | longitude 0.84, latitude 0.35 |
| Decided line through the 20 s position | −1.02 m | −2.06 m | longitude 0.30, latitude 0.13 |

- **13.4's position bands as cross-track** (course 45°): longitude 7.30e-7 rad (3.77 m east) is **5.34 m**;
  latitude 1.66e-6 rad (10.5 m north) is 14.9 m. Yaw (0.045 rad) and the velocities are insensitive.
- The rhumb-line figure agrees with N-02's 0.242 m and 2.184 m from the IC alone.
- The decided line's own d(20 s) on the reference is +1.016 m, so the error at the step is −608.58 m.
- **Expected:** all three are inside every band. The great circle comes closest: 0.84 of longitude flown,
  against a geometric steady-state equivalent of 1.25 if the loop fully converged by 60 s. A great-circle
  exceedance late in the run would therefore not surprise. Each variant is reported, never selected.

### Q6. The law at the wrong divisor (R9 as amended; Q12)

**Recommendation:** measure first, then gate only where the measurement shows an exceedance (Joe's rule).
- Commit 6a flies divisor 1 in all four cases as R6's reported variant, with the configs' `transport.divisor:
  10` replaced by 1 in the test's config text.
- For each case where that run leaves at least one discriminating band at a gated sample, commit 6b's
  `NescF16Control.Case13pNWrongDivisorFails` asserts that again. The assertion's form is fixed now: any
  non-zero-accepting channel, any gated sample. Where it stays inside, the test reports, and the PR body records
  R9's clause: "divisor 1 within every band".
- ***Scratch* prediction:**
  - 13.3 leaves: moment_l 2.5, force_y 1.4 and moment_n 1.4, all at sample 156, the course-step transient;
  - 13.4 leaves: moment_l 1.6, force_y 1.0 and moment_n 1.05, at sample 206;
  - 13.1 stays inside (largest v_down 0.56, pitch 0.45), and so does 13.2 (rate_q 0.36).
  The real run's ratio is against the band reference, so it also carries ASTRA's own offset at divisor 10.

### Q7. The negative controls: each defect planted without editing the peer's law or aircraft/f16

**Recommendation:** four plants plus a positive control. PR 9's rules apply: each control's expected band is
fixed here; a control stops at its first gated exceedance and logs every channel's ratio; and **a halt before
`Band::kFirstGatedSample` fails the control and goes to Joe** (PR 9's amendment 7).

| Control | How it is planted | Expected to fail | *Scratch* estimate |
|---|---|---|---|
| Positive control for the mass route | 13.3's config text with `physical.mass` swapped for `mass/constant` carrying P1's values (`ixz` +982 slug·ft²), as PR 9 did | none: `Simulation::derivative` bit-identical to the F-16 mass's at the trim state and at ω_b + (0.1, 0.05, 0.1) | proves the route plants only the sign |
| **Izx's sign**, gated in 13.3 (amendment 11) | the same swap with `ixz` −982 | **any discriminating channel**, PR 9's form; the test names force_y (2.14e4 N) and moment_n (1.12e5 N·m) at the step as the expected channels and logs which channel actually left first | force_y 1.21, moment_n 1.18 at sample 156; roll 0.38, yaw 0.36 |
| Izx's sign, reported in 13.1, 13.2 and 13.4 | the same | none asserted; completes; ratios logged. If 13.4 also leaves a band, the PR body reports it; it is never gated after the fact | 13.4: force_y 0.90, moment_n 0.90; 13.1 and 13.2 below 0.1 |
| **The elevator's sign** at the law boundary (all four) | test plugin: the peer's reply elevator negated | **pitch** | pitch 179 to 14,400 at sample 80; no halt: Mach stays 0.21 to 0.53 and altitude 1,167 to 3,052 m to 8 s |
| **The aileron's sign** at the law boundary (13.3, 13.4) | test plugin: the reply aileron negated after the +LWD interconnect, as an omitted flip would be | **roll** | roll 37 (13.3) and 97 (13.4) at sample 80; Mach at least 0.18 |
| The wrong divisor | Q6 | Q6 | Q6 |

- **The plant route is a test-only plugin, accepted by Joe (amendment 1, M4).** In closed loop the exchange
  overwrites the controls every 10 frames, so PR 9's write-seam plant cannot reach a law-boundary sign. The
  command map stages without scaling, and Joe has excluded editing the peer or aircraft/f16. The route is
  `tests/plugins/nesc_control_variant`.
  - It depends on the peer only through `control_law.hpp` and `nesc_control_peer.hpp`, included as headers. It
    never links or `dlopen`s the shipped `.so`.
  - `nesc_control.cpp` and its descriptor carry no variant or test key.
  - It is a transport plugin that holds the shipped peer class from its header, reads one
    `transport.peer.test_variant` key, and applies one of:
    - `elevator_sign` or `aileron_sign`: negate that field in the reply;
    - `one_exchange_delay`: reply with the previous exchange's commands (R22's plant);
    - the reported variants: `sim5_references` (Q11), and `rhumb_line`, `great_circle` and
      `line_at_offset_frame` (Q27), through the peer's `Options` (D5).
  - `tests/plugins/` is a discovery root (`tests/CMakeLists.txt:5`) that `config_keys.py` skips, so the shipped
    peer carries no variant or defect code.
  - Rejected with it: the variants as documented peer keys, which would put code in the shipped peer that PR 12
    must refuse; and config-only plants, which lose the aileron plant.
- **Izx's margin is thin.** At 1.2 times in one case, the *scratch* model's accuracy (flat Earth, no rotating
  frame) matters. If 13.3 does not fail, that is a stop condition for Joe, never a wider band or another case.
  The closed loop moves Izx's effect into the step's force and moment transient, not the kinematics.

### Q8. The EAS route from tas, cas and mach, and its refusals

**Recommendation:** ADR-0029 Decision 2, as follows.
- **Route** (once per exchange, `control_law.hpp`):
  - qc/p0 = (1 + 0.2 (cas/a0)²)^3.5 − 1, the inverse of `icd_pack::calibrated_airspeed`'s last step;
  - p = p0 · (qc/p0) / ((1 + 0.2 M²)^3.5 − 1);
  - EAS = a0 · M · √(p/p0), with `icd_pack::kSeaLevelPressure` and `sea_level_speed_of_sound()`.
  - `tas` does not enter the formula. Both `cas` and `mach` already carry it through the packer, and a zero tas
    is a `WIND_INVALID` frame.
  - Conditioning: about 5 at Mach 0.5, and about 1/(0.7 M²) at small Mach, which is why R12's grid stops at
    Mach 0.1.
- **Refusals.** The peer returns all five commands as quiet NaN when the frame has any of these:
  - `CAS_SUPERSONIC` (cas is a placeholder 0);
  - `ATMOSPHERE_INVALID` (mach and cas are 0);
  - `WIND_INVALID` (tas, α, β, mach and cas are 0);
  - Mach not above 0;
  - `LATLON_NOT_MODELED`, at every frame, because the rate path reads `lat`;
  - and, with 13.4's offset configured, a first exchange whose frame is not 0 (nothing is latched).
  `ACCEL_INVALID` is not refused: the law reads no specific force, and frame 0 always carries it.
- **What halts.** The exchange halts at its non-finite check (`lockstep_exchange.cpp:277`) with `kTransport`,
  `TransportFault::kNonFiniteCommand` and source `astra_sw_to_sim_v1.elevator`, the first field in ADR-0022's
  order. That is the field the halt names.
- **Why the cause is visible:** the peer sets `transport.peer.refusal` to the refused status bits, so the cause
  reaches the tests (and the log, on a frame that is sampled).
  - The ICD bits go in as they are. Two synthetic bits sit above ICD v1's highest status bit,
    `ASTRA_ICD_STATUS_WIND_INVALID` (`0x10`, `icd/astra_icd.h:42`): `0x100` for Mach not above zero, and `0x200`
    for the base-point frame (amendment 7).
  - `control_law.hpp` declares both. A `static_assert` checks that each is disjoint from every
    `ASTRA_ICD_STATUS_*` mask: `ACCEL_INVALID`, `LATLON_NOT_MODELED`, `ATMOSPHERE_INVALID`, `CAS_SUPERSONIC` and
    `WIND_INVALID`, and their OR.
  - **How the tests read it after a frame-0 halt.** A refused frame 0 halts before any log sample exists. The
    test holds the `const double*` that `Simulation::registry().find("transport.peer.refusal")` returns after
    `from_config`. After `run(1)` returns the halt, it reads the value through that pointer: the peer wrote it in
    `publish()` during the halted frame. The test then compares it with the expected bit, cast to an integer.
    The unit-level test reads the same signal from the `SignalRegistry` it passed to the peer's `init`.
- **Tests:**
  - `F16Control.RefusesEveryFlaggedFrame`: each condition at the unit level, through the Transport API;
  - `F16Control.FlaggedFramesHaltTheExchangeNamingTheField`, through `Simulation`:
    - `CAS_SUPERSONIC` from case 12's IC with the peer;
    - `LATLON_NOT_MODELED` from `earth/flat`;
    - `ATMOSPHERE_INVALID` from `tests/plugins/nan_atmosphere`;
    - `WIND_INVALID` from `tests/plugins/nan_wind`.
    Each halts at frame 0 with `kTransport` before any physics halt, since the exchange runs before the wrench
    stage.

## Smaller decisions (decided; amendments 2, 3, 6 and 9 apply to D8, D9, D1 and D10)

- **D1. The initial commands.**
  - `altitude_ft: 10013`, the IC altitude (ICD `altitude` at frame 0 is exactly `feet_to_meters(10013)`).
  - `course_deg: 45`.
  - `equivalent_airspeed_knots`: the IC's EAS from an independent ISA, written `%.17g` (287.98158203…).
    `docs/reviews/evidence/m7-pr11a-residual/ic_eas.py` computes it from the config's literals read as text,
    with `derive_residual.py`'s constants and its own ISA. 13.2's step is that minus 5.
  - `NescF16.Case13p*ClosedLoop` asserts that the peer's frame-0 EAS equals the literal to **1e-9 relative**
    (amendment 6). That guards against a stale literal: a re-trim moves it by far more, and the literal and the
    peer come from two independent ISA implementations. R12 proper keeps 1e-12.
- **D2. The keys** (ADR-0029 Decision 3, ADR-0030 rule 3: a suffix names the unit as written, converted once
  at load by `core/util/units`, the Dryden `w20_knots` precedent):
  - `peer.trim.elevator` (rad) and `peer.trim.throttle` (1);
  - `peer.commands.{altitude_ft, equivalent_airspeed_knots, course_deg}`;
  - `peer.schedule[].{frame, altitude_ft, equivalent_airspeed_knots, course_deg}`: frames ≥ 0, strictly
    ascending; each applies from the first exchange at or after its frame;
  - `peer.lateral_offset.{frame, offset_ft}`: 13.4 only; the line's course is `commands.course_deg`.
  Every key is consumed by one of the four configs. The 13.x values then read like Vol II's tables.
- **D3. One trim result.** `peer.trim.*` repeats `vehicle.initial_controls`' text, and the peer applies the
  inverse limit-and-scale at init: `longStkTrim = −elevator / degrees_to_radians(25)` and `throttleTrim =
  throttle`.
  - It refuses |longStkTrim| > 1 or a throttle outside [0, 1] with a `ConfigError` naming the key.
  - `F16Control.EachConfigsPeerTrimEqualsItsInitialControls` checks the two copies bit for bit in each tracked
    config. The round-trip script checks them too.
  - SAS and autopilot are always on, and the pilot inputs are 0 (Vol II p.16), so there are no mode keys. R23
    drives the law function with both off.
- **D4. Peer signals** (registered at init, held between exchanges, under `transport.peer.*`, which R13's
  exclusion of `transport.*` already covers):
  - `altitude_command` (m), `equivalent_airspeed_command` (m/s), `course_command` (rad);
  - `lateral_deviation_error` (m), `equivalent_airspeed` (m/s);
  - `refusal` (1).
  They serve R10's timeline, Q3's in-flight check, Q8's cause and 13.4's diagnosis.
- **D5. Code shape:**
  - `control_law.hpp`: the SI law as pure functions, its constants written as the DML's value times named
    conversions, the EAS route, ω_eb by `frames::dcm_from_euler` and `geodesy::kOmegaIe`, and the cross-track
    expression;
  - `nesc_control_peer.hpp`: the `Transport` class, with an `Options` struct holding the LQR references and the
    cross-track function. Its defaults are the decided ones, and the shipped plugin uses only those;
  - `nesc_control.cpp`: the key block in its leading comment, the descriptor `{"sim",
    "transport_nesc_control"}` and `create()`. It carries no variant or test key (amendment 1).
  - The variant plugin (Q7) includes the two headers and builds its own copy of the peer class. It never links or
    loads the shipped `.so` (amendment 1).
  The independent reference is `aircraft/f16/tests/control_reference.hpp`, written from the DML text in its own
  units, as `dml_reference.hpp` is for the aero.
  - **The MathML anchor.** A committed evidence script
    (`docs/reviews/evidence/m7-pr11a-law/mathml_commands.py`, which imports `rate_frame.py`'s `DaveModel`)
    evaluates `F16_control.dml` itself at the hand-built frames and prints `%.17g` commands.
  - `F16Control.ReferenceMatchesTheMathMlEvaluation` pastes those commands, so a transcription error common to
    the reference and the SI law cannot pass. The law has no checkData (addendum §7).
- **D6. `sim.frames`** is 20,001, 20,001, 30,001 and 60,001 (Context), and each header comment says why.
- **D7. Exclusions, pre-registered (ADR-0028 item 2): none.**
  - Footnote (a) is the window, and (b) is SIM 2. Footnotes (d) and (e) for 13.1, 13.3 and 13.4 explain
    force and moment transient timing, which shows as band width.
  - All 21 channels are asserted at samples 80 onward in all four cases.
  - Zero-accepting, asserted and never cited as evidence (measured from the headers; matches the preview):
    - 13.1: force_y and moments l, m, n;
    - 13.2: moments l, m, n;
    - 13.3 and 13.4: moment_m.
- **D8. `f16_flight.hpp` is edited**, additively (amendment 2). PR 9's two test sources stay byte-unchanged,
  and NescF16.* and NescF16Control.Case11/12* keep their names, configs, assertions and logs. The edits:
  - a `FlightOptions` stop "at any discriminating channel's first gated exceedance";
  - an optional per-sample callback (amendment 8's guards);
  - new functions, generalized to a config path: the trim-residual check, and the constant-mass swap. PR 9's
    anonymous-namespace originals are left where they are;
  - and, per STATE.md, its wrap artifact fixed (lines 6 to 8, "from p_n / (the").
  `independent_evaluator.hpp` is not edited.
- **D9. The exchange limits** in each config are the law's own ranges: elevator ±25°, aileron ±21.5°, rudder
  ±30.172° (in rad), throttle [0, 1]. Each is written as the `%.17g` text of the same core expression the law
  saturates with: `units::degrees_to_radians(25.0)`, `units::degrees_to_radians(21.5)`,
  `units::degrees_to_radians(30.0) + 0.008 * units::degrees_to_radians(21.5)` and 1 (amendment 3, with its
  2026-10-13 wording fix: the rudder's in the law's own factor order).
  - `F16Control.ExchangeLimitsContainTheLawsRange` reads each tracked config's limits through `Config`. It checks
    two things:
    - the law's saturated outputs pass the exchange's inclusive `lo ≤ v ≤ hi` check. 13.1's −25° elevator at
      frame 5000 is the case this protects;
    - each configured limit (elevator, aileron, rudder, both signs) is bit-identical to the law's saturated value.
      A rewrite of either expression then fails here, not at 13.3's or 13.4's course step.
  - So a law output beyond its own limits halts `kCommandLimit`, never clamped (§15). In a nominal case, that
    halt is a stop condition.
- **D10. The no-alloc test** is `F16ControlNoAlloc.ExchangesAllocateNothing`, in a new
  `aircraft/f16/tests/control_no_alloc_test.cpp` in the F-16's no-alloc executable. It flies a config with
  13.4's latch and a step inside its 1,000 frames. `no_alloc_test.cpp` and F16NoAlloc stay byte-unchanged
  (M2).
  - The PR body records this as a deviation from ADR-0029's Consequences line ("`tests/physics/no_alloc_test.cpp`
    gains a peer config"). The reason: the brief keeps that file and F16NoAlloc unchanged. No ADR edit
    (amendment 9).
- **D11. Q11's reported variant values:** `trimmedAlpha = trimmedTheta` = the trim's θ (0.0460581645127969
  rad), and `trimmedKEAS` = D1's EAS.
  - *Scratch:* inside every band in 13.3 and 13.4 (below 0.1).
  - 13.1: altitude 0.21.
  - **13.2: rate_q 1.56**. That band, 1.86e-6 rad/s, is the most sensitive to Q11 (see Risks).

## Mismatches with the brief (for Joe)

- **M1.** The brief's "four 180,001-frame cases": 13.x are 20,001, 20,001, 30,001 and 60,001 frames. All
  variants and controls together come to about 0.75 M frames per preset (measured, verification step 6).
- **M2.** ADR-0029's consequences say `tests/physics/no_alloc_test.cpp` gains a peer config. The brief keeps
  F16NoAlloc unchanged and lists aircraft/f16/tests, so D10 places it in the F-16's no-alloc executable, a new
  file. `ctest -R NoAlloc` still selects it. Accepted by Joe (amendment 9); the PR body records the deviation
  and its reason, and no ADR is edited.
- **M3.** R23 names 15's and 16's trims. Their configs are PR 11b's, so PR 11a covers case 11's (which is 13.x's)
  trim plus a synthetic grid, and 15 and 16 carry to 11b. R10's port half is PR 12's.
- **M4.** `tests/plugins/nesc_control_variant` (Q7) is outside the listed paths. Also outside them:
  `tests/physics/CMakeLists.txt` (the new sources, as PR 9's M2), `aircraft/f16/tests/CMakeLists.txt`, and the
  two evidence directories: `docs/reviews/evidence/m7-pr11a-trim/` and `m7-pr11a-residual/`. The MathML anchor's
  `m7-pr11a-law/` sits beside them. Accepted by Joe (amendment 1).
- **M5.** Q11's "hard-coded reference airspeed" is inert in 13.x (Context), and Vol II D.1.13's prose reads it
  otherwise. Reported only; Q11 stands. The PR body carries the finding, and /wrap adds one dated line
  (2026-10-13) under the M7 plan's Q11 pointing at this plan's Context (amendment 13).
- **M6.** `ctest --preset asan -R 'F16Control|NoAlloc'` also matches PR 9's five `NescF16Control.Case11/12*`
  tests, including the 180,001-frame Izx flight, and every new `NescF16Control.Case13*`. Step 3 records its `-N`
  count and wall time.

## Plugin and harness design

- **Per exchange,** the peer computes the law in this order:
  1. Refusal check (Q8).
  2. Apply the schedule.
  3. 13.4's latch at frame 0, then d and `lateralDeviationError` from frame 20000.
  4. EAS.
  5. ω_eb = (p, q, r) − C_nb·Ω·(cos lat, 0, −sin lat).
  6. The SI law.
  7. The three boundary steps: aileron sign after the interconnect, throttle to both entries, sticks
     dimensionless.
  8. Zero the reply, then fill it. No clock, no allocation, no exception.
- **The four configs:**
  - `aircraft/f16/configs/case13p1_subsonic_altitude_change.yaml`, `case13p2_subsonic_airspeed_change.yaml`,
    `case13p3_subsonic_heading_change.yaml`, `case13p4_subsonic_lateral_side_step.yaml`;
  - each is case 11's models and trim text, plus `transport` (plugin, `divisor: 10`, the command map onto
    `controls.*` with `throttle: [controls.throttle, controls.throttle]`, D9's limits, `peer`);
  - each has a header comment citing its Vol II table, the exact `astra trim` command, D6 and D1.
- **The harness, `tests/physics/nesc/nesc_f16_closed_loop_test.cpp`,** flies each config through
  `f16::Flight`:
  - every gated sample is asserted against its band (D7);
  - the ungated span is logged per PR 9's D3;
  - the per-sample guards below run;
  - the frame count is 100(N − 1) + 1 = `sim.frames`, asserted at the end.
  Variants and controls live in `nesc_f16_closed_loop_controls_test.cpp`.
- **Per-sample guards in `Case13p*ClosedLoop`** (amendment 8). They are defined here before the first run. Each
  is its own `EXPECT`, at every sample k = 0 to N − 1, gated window or not, since none of them is a band. A miss
  in any is a stop condition. The sample's state is the pre-step state the harness copies before `run(1)`, and
  sample k's frame 100k is an exchange frame: 100 is a multiple of the divisor 10.
  - **G1, Q5's route** (PR 9's D5, unchanged):
    - compares the evaluator's aero wrench plus its engine wrench at the frame-start state;
    - against the sim's `v0.wrench_total.*` for that frame;
    - bound: PR 7's `matches_relative`, 1e-12 normwise, force and moment separately.
  - **G2, EAS in flight** (Q3):
    - compares `transport.peer.equivalent_airspeed`, read after `run(1)`, the value the peer computed at frame 100k;
    - against an independent evaluation, |v_b| · √(ρ/ρ0). v_b is the pre-step state's body velocity (still air,
      so it is the air-relative velocity). ρ is the latched environment's `atmosphere.density_kg_m3` for that
      frame. ρ0 = `icd_pack::kSeaLevelPressure · kMeanMolecularWeight / (kGasConstant · kSeaLevelTemperature)`;
    - bound: |difference| ≤ 1e-12 · expected.
  - **G3, 13.4's cross-track** (13.4 only; in 13.1 to 13.3 the signal must read exactly 0 at every sample):
    - compares `transport.peer.lateral_deviation_error`, read after `run(1)`;
    - against an independent evaluation in the harness, written from ADR-0029 Decision 6, not from
      `control_law.hpp`:
      - the base point is lat0, lon0 and h0 from the frame-0 pre-step state's `p_n`;
      - M0 = `geodesy::meridian_radius(lat0)` and N0 = `geodesy::prime_vertical_radius(lat0)`;
      - with the sample's own lat and lon from its pre-step `p_n`: Δn = (lat − lat0)(M0 + h0) and Δe = (lon −
        lon0)(N0 + h0) cos lat0;
      - d = −Δn sin χ + Δe cos χ, with χ = `degrees_to_radians(45)`;
      - expected = d − 609.6 m from frame 20000, and exactly 0 before.
      The ICD's `lat` and `lon` are that same `p_n`, copied with no arithmetic (`icd_pack.hpp:102`), so the two
      sides see the same inputs;
    - bound: |difference| ≤ 1e-12 · max(|expected|, 609.6 m). Before frame 20000 both sides must be exactly 0.
  - **G4, zero compute delay** (ADR-0022, ADR-0017):
    - compares the latched `v0.controls.elevator`, `aileron`, `rudder` and `throttle` after `run(1)`;
    - against the last reply in `sim.exchange_recorder()->replies()`: frame 100k's own reply, its `frame` field
      checked to equal 100k. The fields map as `elevator`, `aileron`, `rudder` and `throttle[0]`, and
      `throttle[1]` must equal `throttle[0]`;
    - bound: bit-identical (`std::bit_cast`).
  - **G5, D1's literal** (frame 0 only; amendment 6):
    - compares `transport.peer.equivalent_airspeed` at frame 0;
    - against the config's `transport.peer.commands.equivalent_airspeed_knots`, converted by
      `units::knots_to_meters_per_second`;
    - bound: |difference| ≤ 1e-9 · expected.
  The guards' largest misses are printed with PR 9's D3 summary and reported in the PR body. G2 to G5 run
  through the per-sample callback D8 adds.
- **The tests** (`-N -R 'Nesc.*Case13'`: **0** on the base build, **30** on the candidate):

| Test | Asserts |
|---|---|
| `NescF16.Case13p{1-4}TrimResiduals` | Q1 (R4) |
| `NescF16.Case13p{1-4}ClosedLoop` | R6: 21 channels at every gated sample, no halt, guards G1 to G5 |
| `NescF16Variant.Case13p{1-4}Sim5References` | reported: completes, ratios logged |
| `NescF16Variant.Case13p{1,2}Divisor1` and `NescF16Control.Case13p{3,4}WrongDivisorFails` (by Q6's measurement; the names follow it, the count stays 4) | Q6 |
| `NescF16Variant.Case13p4{RhumbLine,GreatCircle,LineAtOffsetFrame}` | reported (Q5) |
| `NescF16Control.Case13p3MassRoutePositiveControl` | bit-identical derivative |
| `NescF16Control.Case13p3IzxSignFails`, `…Case13p{1,2,4}IzxSignReported` | Q7 |
| `NescF16Control.Case13p{1-4}ElevatorSignFailsPitch` | Q7 |
| `NescF16Control.Case13p{3,4}AileronSignFailsRoll` | Q7 |

- **Peer unit tests,** `aircraft/f16/tests/control_test.cpp` (the `F16Control.*` suite, 17 tests):
  - constants (R11);
  - hand-built and seeded frames against the reference (R11, Q2);
  - the MathML anchor (D5);
  - the ω_ie_b subtraction at 0°, 36.02° and ±89.95° latitude and several headings (R11, R22);
  - the cross-track on the course, 609.6 m left (−) and 609.6 m right (+), with h0 fixed (R11);
  - R12, the refusals, and the exchange halts (Q8);
  - R23 (case 11's/13.x's trim and a synthetic grid; disengaged law, within 1e-12);
  - D3 and D9 (amendment 3: the limits read through `Config` from each tracked config);
  - `TrackedConfigsPairThePeerWithTheRotatingEarth` (Q4, amendment 10);
  - R10 (each tracked config's timeline, read from D4's signals at every exchange, against Vol II Tables 37 to
    40 in their units, with 287.98 at 0.01 kt);
  - R22:
    - at frames 0, 5000, 15000 and 20000, the reply to the recorded request equals an independent evaluation;
    - the latched `v0.controls.*` equals the reply bit for bit at that frame;
    - the `one_exchange_delay` plant fails the check;
  - init refusals.
  Plus `F16ControlNoAlloc` (D10).

## Commits (Joe's order)

| # | Content |
|---|---|
| 1 | This plan, Status "approved by Joe, <date>, on his word"; push; draft PR. If the classifier refuses the Status write, it is not retried by any route: the PR body names the refusal (PR 9's amendment 10) |
| 2 | The peer (`control_law.hpp`, `nesc_control_peer.hpp`, `nesc_control.cpp`, `CMakeLists.txt`) with its key block for the trim and initial-command keys; `control_reference.hpp`; R11, R12 and R23's unit tests; the MathML anchor's evidence; `docs/reference/config-keys.md` regenerated |
| 3 | The schedule and lateral-offset keys; D4's signals; `tests/plugins/nesc_control_variant`; R10's and R22's fixtures on configs built in the test from case 11's text; `docs/reference/config-keys.md` regenerated again (amendment 5). The commit message says `test_config_keys.py` is expected to fail until commit 4 |
| 4 | The four configs. `docs/reviews/evidence/m7-pr11a-trim/`: case 11's base round trip rerun on this branch by PR 9's route and command, with D3's `peer.trim.*` check; the pre-registered bit identity of each 13.x config's state and control text with case 11's. `astra trim` refuses a transport block by design (ADR-0029 Context, Q9 (b)), so the trim is solved on the base and the configs carry its text (amendment 4). `m7-pr11a-residual/` (Q1, and `ic_eas.py` for D1). R10 against the tracked configs; Q4's rotating-Earth test |
| 5 | The harness, the `f16_flight.hpp` edits (D8), `tests/physics/CMakeLists.txt` |
| 6a | The reported variants: Q11, Q27, and divisor 1, measured |
| 6b | The negative controls, Q6's gating from 6a's measurement |
| 7 | Gate evidence, `docs/reviews/evidence/<head>-gate-summary.log` |

- Every commit that changes the key block regenerates `docs/reference/config-keys.md` (commits 2 and 3), so
  `config_keys.py --check` passes at each.
- `test_config_keys.py` passes from commit 4 on: a documented key with no consuming config fails it before then.
  Commits 2 and 3 are not gate candidates, and their messages say so.
- Then comes the Codex adversarial review (`/review-cycle`, at most two correction rounds). Its focus: the SI
  conversions and the boundary steps, the refusals, 13.4's latch and geometry, the schedule timing, the gating
  and exclusions, and the plants.
- Then `/human-review`. Joe reads the peer's law and its SI conversions, 13.4's logic, the configs and their
  schedules, and the harness's gating.

## Stop conditions (each goes to Joe; nothing is widened, excluded, re-trimmed or swapped)

- Any gated sample outside its band in `Case13p*ClosedLoop`. Q11's and Q12's decided choices are never swapped
  for a variant.
- R4: a solved residual above 1e-6, a lateral miss beyond Q1's bounds, or the 13.x literals not bit-identical to
  case 11's.
- R11, R12 or R23 outside their pre-registered tolerances; any seeded exclusion inside Q2's margin beyond a
  handful (counted).
- A halt in any case, or in any control before sample 80; a `kCommandLimit` halt in a nominal case
  (amendment 3). A control that does not fail its pre-registered band (13.3's Izx included). A miss in G1 to G5.
  The positive control not bit-identical.
- The copied `derive_residual.py`'s diff reaching beyond the `CASES` table and the evidence paths (amendment 12).
- Any need to touch `core/`, `plugins/`, `icd/`, `embedded/`, `tests/golden/`, tolerance files,
  `docs/decisions/`, or aircraft/f16's aero, mass or engine.

## Verification

1. **The release selector.**
   - `ctest --preset release -N -R 'Nesc.*Case13'`: on the base build, before commit 5, it must be **0**. On the
     candidate it must be **30**, before the gated run.
   - Then `ctest --preset release -R 'Nesc.*Case13' --no-tests=error`. `|` in a shell is never written `\|`.
   - The PR body gives, per case: each discriminating channel's largest gated ratio, PR 9's D3 ungated maxima,
     each variant's ratios, each control's exceeded band, sample and ratio, Q6's measurement, and the guard
     maxima.
2. **The ASan selector.** `ctest --preset asan -N -R 'F16Control|NoAlloc'` on base and candidate, then the run
   with `--no-tests=error`. Count and wall time are recorded (M6).
3. **The config-key reference.**
   - `python3 -I -B scripts/config_keys.py --check`: byte-identical to main except the new plugin's entries
     (shown by `git diff origin/main -- docs/reference/config-keys.md`).
   - `.venv/bin/pytest tests/python -k config_keys`.
4. **Q9 (b) unchanged.**
   - `astra run` and `astra trim` on a 13.x config exit 65, naming `transport`.
   - `test_cli`, `test_montecarlo`, the scenario and log tests, and `lockstep.py:261`'s shm-only check are
     unchanged.
5. **The unchanged set.** F16Aero, F16Engine, F16Mass, F16NoAlloc, F16Evaluator and NescF16.Case11/12* pass
   unchanged. `git diff origin/main..HEAD -- aircraft/f16` touches only `sim/`, `configs/` and `tests/`.
6. **Gate time.** The release and ASan wall times of the new tests (about 0.75 M frames per preset). A large
   increase goes to Joe (Q23).
7. **The full gate.** `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`. The blast radius
   is unchanged: PR 9's cases 11 and 12, M2's cases 1 to 10, M3's regression, the R1 golden, M4's logs and M6's
   fixtures. A failed run's full log is copied to its per-head name before any rerun.
8. `git diff --check origin/main..HEAD`.

Results go in the PR body: commands, exit codes, counts, the Codex verdict and thread id.

## /wrap (before Joe's merge)

- PLAN.md's PR 11a line.
- STATE.md, as main reads it after the merge:
  - PR 11b and PR 13 are eligible, PR 12 waits on 11b, and PR 3 waits on Joe's go;
  - the PR 11a thread closes;
  - the rate-frame clause (`rate_frame.py` on case 15) and the Izx clause carry to 11b;
  - Q4's answer (no refusal; the peer cannot see the Earth model) goes into STATE's PR 3 line, and nowhere
    under `docs/decisions/` (amendment 10).
- The docs pass: one dated line (2026-10-13) under the M7 plan's Q11, pointing at this plan's Context for M5's
  finding (amendment 13).
- The PR body records D10's deviation from ADR-0029's Consequences (amendment 9), M5's finding (amendment 13),
  and the copied script's diff (amendment 12).
- Before the merge: `git fetch`. If main has moved:
  - merge `origin/main` in;
  - rewrite STATE.md and PLAN.md;
  - rerun what the tier requires for what the merge touched.

## Risks and rollback

- **Tight lateral bands in the longitudinal cases.** These are the likeliest R6 findings:
  - 13.2: rate_r 8.5e-8, rate_p 3.6e-7 and rate_q 1.9e-6 rad/s;
  - 13.1: yaw 2.0e-6 and roll 4.7e-6 rad.
  The *scratch* puts Q11's choice at 1.56 of 13.2's rate_q band, which suggests that band can tell the members'
  reference choices apart. A miss goes to Joe, with D11's variant as the reported comparison.
- **Izx's thin margin** (Q7) and **the wrong divisor's dependence on ASTRA's own offset** (Q6).
- **Rollback:** revert 6b, 6a, 5, 4, 3 and 2 in that order. Each stands without the later ones.
