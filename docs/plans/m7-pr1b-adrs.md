# M7 PR 1b: ADR-0029, ADR-0032 and architecture rev 12

Spec: [M7 plan](m7-f16-nesc-oracle.md), approved by Joe at `3381693`, with its dated 2026-10-08 amendments: the PR
table's row 1b, Q8 to Q13, Q20, Q26, Q27 and the Obligations row "1b, 11a". [Inventory addendum](m7-f16-inventory.md)
§5, §6, §8, D3 and §10 (Joe's decisions).
Risk: Tier 3. The records set the sim-side flight-software seam (a transport, the port) and a core interface
(`EnvironmentSample`, a `kCoreApiVersion` bump in PR 3b). Docs only, plus one evidence script and its output under
`docs/reviews/evidence/`. Nothing changes in `core/`, `plugins/`, `aircraft/`, `embedded/`, `python/` or `tests/`.
Status: **approved by Joe, 2026-10-08, in plan mode.** J1 to J7 are decided, and amendments 1 to 14 are folded in
below; 11 to 14 were the conditions of approval. J8 and the laws' rate frame wait for Joe at the stop after
Deliverable 1.
Base: `origin/main` `2f0a1eb` (#108), re-checked after `git fetch`. Worktree `m7-pr1b`, branch `worktree-m7-pr1b`,
created after approval. Joe arms the `docs/decisions/` guard; Claude never arms it.

## Context

Q20 put two records in PR 1b. ADR-0029 is the sim-side flight software: an in-process peer at the exchange stage,
its schedule, rate and units, 13.4's logic, and the port's equivalence criterion. ADR-0032, added by Joe's D3 (B1),
has the core supply the body rate relative to the Earth to wrench sources. ADR-0032 must be accepted before PR 3b
starts, and ADR-0029 before PR 11a. One clause of ADR-0029 is still open: which rate frame the NESC laws read.
The obligation is to try to decide it from the members' data first. This PR pre-registers that analysis, runs it,
brings the result to Joe, and only then writes the records.

## Repository findings (read at `2f0a1eb`)

**For the analysis.**
- 13.3's CSVs: SIM 4 has 301 rows (0 to 30 s) and SIM 5 has 2,400 (0 to 239.9 s). Neither carries α, β or a body
  velocity, and SIM 4 has no airspeed column. Both carry:
  - `feVelocity_ft_s_{X,Y,Z}`, which is NED (the generator's v_north, v_east, v_down, `generate_nesc_band.py:196-198`);
  - the Euler angles, the inertial `bodyAngularRateWrtEi_deg_s_*` and `latitude_deg`;
  - `dynamicPressure_lbf_ft2`, `airDensity_slug_ft3`, and the six `aero_body*` channels, with moments at the CM
    (Vol II D.1.11, p.229; D4).
- Float32 columns (Joe, amendment 1). In SIM 5's 13.3, 11 and 15 files, every value of `time`, `mach`,
  `dynamicPressure_lbf_ft2` and `trueAirspeed_nmi_h` round-trips through float32, though each is printed with 17
  digits. No other column does, and none of SIM 4's columns does. The back-out divides by q̄.
- Vol II Table 39 (p.65): 13.3 starts at case 11's condition (36.01916667°, −75.67444444°, 10,013 ft, ψ = 45°),
  with the autopilot and SAS engaged. At t = 15 s it commands a 15° right course change through
  `trueBaseCourseCommand`. The duration is 30 s. Case 15 (Table 42) has no command step; its autopilot engages at
  the first time step.
- Vol II Table 73 (p.93) gives the atmospheric cases' Earth rate, 7.292115e-5 rad/s. That equals `geodesy::kOmegaIe`
  (`core/include/astra/util/geodesy.hpp:23`).
- Vol II p.16: "No actuator dynamics were modeled", and "No pilot inputs were used".
- `F16_control.dml`'s dependency graph (its `variableDef`s, read with `xml.etree` this session):
  - `ail` and `rdr` depend only on `apOn`, `sasOn`, `latStk`, `pedal`, `latOffset`, `baseChiCmd`, `beta`, `psi`,
    `phi`, `pb` and `rb`. No trimmed control and no LQR reference reaches them.
  - `el` reads `longStkTrim`, `trimmedAlpha`, `trimmedTheta`, `trimmedKEAS`, `Vequiv`, `keasCmd`, `altCmd`, `altMsl`,
    `alpha`, `theta` and `qb`.
- The signal has a closed form, from addendum §5's law (each LQR output is a negated sum; ail = −21.5·totLatStk;
  rdr = −30·totPedal + 0.008·ail; el = −25·totLongStk). Where no limit is active:
  - Δail = 21.5·(k₁₃·ω_ie,x + k₁₄·ω_ie,z) deg, with `latdLQR13`, `latdLQR14`;
  - Δrdr = 30·(k₂₃·ω_ie,x + k₂₄·ω_ie,z) + 0.008·Δail deg, with `latdLQR23`, `latdLQR24`;
  - Δel = 25·k_q·ω_ie,y deg, with `longLQR13`;
  - each gain is in frac·s/rad, and ω_ie is in body axes, rad/s. No value has been computed.
- `F16_control.dml` has no checkData (addendum §7), so the law evaluator has no file-supplied test. Amendment 3
  adds one.

**For ADR-0032.**
- The EOM forms `omega_ie_b = c_nb * earth_.omega_ie_n(vs)` on every derivative call
  (`core/src/kernel/rigid_body_6dof_dynamics.cpp:28-35`).
- Each RK4 stage builds its environment sample (`core/src/kernel/vehicle.cpp:426-457`):
  - it copies the frame-latched `environment_` into `stage_env`, and refreshes its atmosphere per stage
    (`prepare_stage_environment`, `:314`);
  - it then calls every `wrench_b(vs_stage, stage_env)`, and every `integrate_state`.
  - `Simulation.derivative` (trim, linearize) runs the same `evaluate_derivative_stage`.
- `EnvironmentSample` (`core/include/astra/kernel/environment_sample.hpp`):
  - It holds `wind_n`, whose zero default means still air, and `atmosphere`, all-NaN by default.
  - Its header comment still says the sample is "held constant through that vehicle's wrench and EOM stages".
    That has been untrue for the atmosphere since ADR-0016.
  - It crosses the plugin ABI, so a new field bumps `kCoreApiVersion`. The version is 9 today
    (`core_api.hpp.in:51`), and ADR-0016 states the test: "which type crosses the versioned `dlopen` loader
    boundary".
- ADR-0017's Decision (`0017:56-59`): every entry's values "default-initialize to a quiet NaN, never an unspecified
  value: "never written" and "written non-finite" are deliberately the same, loudly reported state".
- `check_wrench` sees only a sim's own stages, and there the core always sets the new field. Code that builds its
  own `EnvironmentSample` never passes through `check_wrench`.
- The template's aero sets `in.omega_b = vs.omega_b()` (`aircraft/_template/physical/aero/aero.cpp:96`). Its tests
  drive `aero_math`'s own `Inputs`.
- `core/include/astra/util/air_data.hpp` holds ADR-0020's free functions: `relative_velocity_b(vs, env)` and the rest.
- Log column sets are pinned by tests (M7 PR 1a's D6), so any new published signal moves them.

**For ADR-0029.**
- `transport.peer` is already the loopback transport's own config block, with a `schedule` keyed to `frame`
  (`plugins/sim/transport/loopback/loopback.cpp:7-27`). That is Q10's precedent.
- Architecture §4's taxonomy rule: "Inside either root, plugins are filed by what they *are*: … transports and other
  swappable infrastructure live under `sim/`." `aircraft/<name>/` is "filed by the *same* kinds inside". So
  `aircraft/f16/sim/transport/nesc_control` is a location the plugin model already has, though the template has no
  `sim/` folder.
- The peer's commands reach the inputs through ADR-0022's command map onto ADR-0017's `InputRegistry`, latched once
  per frame. `Simulation` resolves the command map's handles at init, after every `bind_inputs()` (ADR-0022), so the
  transport itself holds no write handle.
- `icd/astra_icd.h:50-52`: p, q and r are "relative to INERTIAL space (what a gyro measures)".
- ICD v1's status bits (`icd/astra_icd.h:27-42`) mark fields that carry a placeholder of exactly 0:
  - `ACCEL_INVALID`;
  - `LATLON_NOT_MODELED` (`:36`): lat = lon = 0 on a flat Earth;
  - `ATMOSPHERE_INVALID`;
  - `CAS_SUPERSONIC`;
  - `WIND_INVALID`.
- `core/include/astra/util/units.hpp` has `kMetersPerFoot`, `kMetersPerSecondPerKnot = 1852/3600` (the exact
  knot) and `kRadiansPerDegree`. It also has the named functions `feet_to_meters`, `knots_to_meters_per_second` and
  `degrees_to_radians`.
- Neither `cmake/AstraBuildFlags.cmake` nor `embedded/CMakeLists.txt` sets `-march` or `-ffp-contract` today. PR 12
  states both builds' flags (amendment 7).
- Q27 fixes what 13.4's logic reads at each exchange (`lat` and `lon`, with h0 fixed). It does not fix where the base
  point (lat0, lon0, h0) comes from. That is J8.

**Process.**
- `ruff check .` and `ruff format --check .` (`justfile:86-88`) grade every `.py` in the tree, so a committed
  script must pass both. Pyright does not include `docs/`.
- `docs_check.py` skips `docs/decisions/` and `docs/architecture.md` (item 8), so those need a separate link check.

## Joe's decisions (2026-10-08)

- **J1, the pre-registration:** as written, with amendments 1 to 4. They are folded into Deliverable 1.
- **J2, where the evidence lives:** `docs/reviews/evidence/m7-pr1b-rate-frame/`, holding `rate_frame.py`,
  `floor.log` (phases 1 and 2) and `compare.log` (phase 3). Ruff grades the script, and the gate does not run it.
- **J3, ADR-0032's interface shape:**
  - a field, `EnvironmentSample::omega_ie_b`: the Earth's rotation rate relative to inertial, in body axes, rad/s;
  - an accessor, `air_data::omega_eb_b(vs, env)`, returning `vs.omega_b() − env.omega_ie_b`, the body rate relative
    to the Earth;
  - not published as a signal;
  - the default is NaN (amendment 5), and the zero default goes under rejected alternatives.
  The names follow the EOM's subscripts (`omega_ie_b`, `omega_en_b`, `omega_nb_b`). ADR-0030 allows a unit suffix on
  C++ identifiers but does not require one, and `wind_n` has none.
- **J4:** ADR-0032 supersedes in part the clause of ADR-0020's Consequences that reads "…with zero further core
  interface change", for the Earth-relative rate only.
- **J5:** ADR-0029 includes, under Q8 and Q26, the EAS derivation from `tas`, `cas` and `mach` (subsonic frames
  only). It also includes refusing a flagged frame (C-08): non-finite commands, so the exchange halts `kTransport`.
  Whether `F16_gnc` is a second plugin or a law selected in `nesc_control`'s config is left to PR 11b.
- **J6:** the full gate runs once, on the final candidate.
- **J7:** the adversarial Codex review covers both ADRs, the architecture diff, and the script and its output. A
  finding that changes the result returns to Joe. There is no pre-run Codex critique.
- **Amendments 5 to 10** are folded into Deliverables 2 to 4 and the supersession tables. In brief:
  - 5: the NaN default;
  - 6: the per-stage field;
  - 7: ADR-0017 and §4's taxonomy rule;
  - 8: the port's expression order;
  - 9: the new J8;
  - 10: §16's M7 note.
- **Amendments 11 to 14**, the conditions of Joe's approval:
  - 11: ADR-0029's refusal list names `LATLON_NOT_MODELED` for every path that reads `lat` or `lon`. Under J8 (a), a
    flagged frame-0 exchange is refused too, never latched.
  - 12: the NaN precedent quotes ADR-0017's Decision sentence (`0017:56-59`), not its header's drafting note.
  - 13: where a forgotten NaN shows. It fails PR 7's self-check or the unit test's own assertion, not
    `check_wrench`.
  - 14: every result names both members' verdicts.

## Open: J8, decided by Joe at the stop after Deliverable 1, with the rate frame

**Where 13.4's base point (lat0, lon0, h0) comes from.** ADR-0029 records the answer.

**Recommended: (a) latched from the frame-0 exchange.** The peer and the port each store ICD v1's `lat`, `lon` and
`altitude` from the exchange whose `frame` is 0.
- Q27 defines the base course through "the initial position (t = 0)", and only the frame-0 ICD frame carries the
  run's own initial position.
- Both implementations read the same bytes, which R13 already compares, so their base points are identical by
  construction.
- Nothing is duplicated in config, so nothing can disagree with the vehicle's initial condition. That matches Q13's
  "one trim result" rule.
- Costs:
  - three latched values outside the memoryless DML law, though 13.4's logic is user-supplied and outside it anyway;
  - a first exchange whose `frame` is not 0 must be refused (non-finite commands, a `kTransport` halt), never latched
    late;
  - a frame-0 exchange flagged `LATLON_NOT_MODELED` is refused the same way, never latched (amendment 11);
  - R11's fixtures must supply a frame-0 frame.

**Alternative: (b) schedule keys in `transport.peer`, and the same values as the port's arguments.**
- Every input to the logic is then explicit, R10 checks them against Table 40, and the peer holds no latched state.
- But they duplicate the vehicle's initial position, and an edit to one that misses the other moves the base line
  silently.

## Deliverable 1: the laws' rate frame, pre-registered (item 5, amendments 1 to 4)

All of this is fixed before anything is computed. Nothing is tuned after phase 2's output exists. A defect found
later is fixed in a new commit that names it, and every earlier output is kept.

**Hypotheses.**
- H_I: the members fed the law inertial rates, the CSV's `bodyAngularRateWrtEi`, as ICD v1 carries them.
- H_E: the body rate relative to the Earth, H_I − ω_ie_b.
- Reported, never selected: H_N, the rate relative to n, H_E − ω_en_b.

**Inputs.** All are read-only, by absolute path, from the main checkout's `docs/refs` (passed as `--refs`), and
none is copied into the worktree.
- 13.3's `sim_04` and `sim_05` CSVs; case 11's `sim_05` (phase 1 only); case 15's CSV only for a member that (d)
  sends there.
- `F16_aero.dml`, `F16_control.dml` and `F16_inertia.dml`; `F16_gnc.dml` only for case 15.
- Each DML's SHA-256 must equal addendum §1.1. Each CSV's SHA-256 is printed.
- Constants come from sources only:
  - Ω from Table 73;
  - S, b and c̄ from `F16_aero.dml:368-384`;
  - DXCG at CG 25 % from `F16_inertia.dml:141-162` (D4);
  - exact π, to convert the CSVs' degrees;
  - WGS-84's a and f (Table 73), for H_N only.

**Per sample.** Samples are at 10 Hz, matched to k/10 within 1e-5 s (ADR-0028). `time` enters only that matching;
the central differences use the nominal spacing.
- C_bn comes from the CSV's 3-2-1 Euler angles.
- v_b = C_bn·feVelocity. There is no wind, so this is air-relative. Then vt = |v_b|, α = atan2(w, u) and
  β = asin(v/vt).
- ω_ie_b = C_bn·Ω(cos lat, 0, −sin lat), with lat the CSV's geodetic latitude.

**(a) The method.**
- **The evaluator.** One stdlib Python file (`xml.etree`, `math`, `csv`, `hashlib`, `struct`) holds a small
  DAVE-ML evaluator that runs the files as written:
  - their MathML calculations;
  - gridded tables with linear interpolation and clip (addendum §2, §3);
  - every `minValue`/`maxValue` as a saturation (§8).
  An unknown MathML element stops the run. No table or gain is transcribed.
- **The back-out** uses D3's aero and D4's transfer.
  - Moments go to the MRC: M_mrc = M_cm − DXCG·Z and N_mrc = N_cm + DXCG·Y.
  - Coefficients use the CSV's own q̄, with S, b and c̄.
  - The aero is evaluated with H_E rates.
  - ail and rdr come from cy, cl and cn. Given α, β and the rates, those three equations are linear in them; they
    are solved by least squares in coefficient space.
  - el comes from cx, cz and cm, which are piecewise linear in el. They are solved by least squares in each DE1
    cell, keeping the cell with the smallest residual. el is reported only (see (c)).
- **The law.** `F16_control.dml` is evaluated at each sample with:
  - φ, θ and ψ from the CSV, and α and β as derived;
  - pb, qb and rb under H_I and under H_E;
  - apOn = sasOn = 1, the sticks and the pilot throttle at 0, and latOffset = 0;
  - baseChiCmd at 45° before t = 15 s and 60° from then on (Table 39).
  The el path also needs altCmd, keasCmd, Vequiv, longStkTrim and the references, listed under "Reported" below.
- **Phase 1, self-checks.** These run before any 13.3 sample is read, and a failure stops the run there.
  - 1a. The hashes match.
  - 1b. All 16 of `F16_aero.dml`'s checkData shots pass the file's own test: every output within its `tol`,
    |y − y_good| < tol (addendum §7).
  - 1c. D3's chain holds. SIM 5's case 11 Y, L and N at t = 0, computed from H_E rates with D4's transfer, are within
    1e-7 relative of its CSV. The addendum found 3e-8, which is consistent with a float32 q̄ (amendment 1).
  - 1d. The disengaged law, with the DML's default trims, gives el = −25·longStkTrim = −3.2410° and PWR = 13.9019 %:
    README Table 11's printed values.

**(b) The signal.** Δ_s(i) = s_law(H_I) − s_law(H_E), for each surface s at each sample i, from the law evaluator.

**The phase 2 self-check on the law (amendment 3).** At every 13.3 sample, under H_I and under H_E, the MathML
evaluator's ail and rdr must equal a closed form. That closed form is written separately, from addendum §5's text
and not from the MathML. It covers:
- chiEst = beta + psi;
- the ±180° wrap of chiErr;
- chiCmd = baseChiCmd + deltaChiCmd, with deltaChiCmd's ±30° limit;
- phiCmd = −10·chiErr with its ±30° limit, through the autopilot switch;
- the lat and dir LQRs as negated sums;
- the ±1 saturations of totLatStk and totPedal;
- the mixer, ail = −21.5·totLatStk and rdr = −30·totPedal + 0.008·ail, including the interconnect.
The tolerance is 1e-12 relative, with an absolute floor of 1e-12 deg. A mismatch stops the run before phase 3.
`floor.log` prints only the largest deviation, never the law's outputs. A member that (d) sends to case 15 gets the
same check against `F16_gnc.dml`, with the closed form extended by §5's polar navigator: `deg_to_ft`,
`circleRadius_ft`, `latOffsetPolar` and `baseChiCmdPolar`.

**(c) The noise floor.** Per sample and lateral surface, F = F_b + F_t + F_p. The actuator and reference terms are
zero.
- **F_b, the back-out's residual.** It is the largest |s_pair − s_lsq| over the three two-channel solutions,
  {cy, cl}, {cy, cn} and {cl, cn}. Six channels against three unknowns leave one residual degree of freedom per
  sample for (ail, rdr), and two for el.
- **F_t, timing and holds.** F_t = δ·max(|ṡ_backed|, |ṡ_law(H_I)|, |ṡ_law(H_E)|), with ṡ a central difference over
  the neighboring samples (±0.1 s) and δ = 0.02 s.
  - Vol II states no law rate (addendum §5). The F-16 tools ran at 100 to 120 Hz (addendum §8: Tables 21 and 22,
    B.6.5 to B.6.7). So δ allows one 100 Hz hold plus one period of compute delay.
  - A steady segment gives a small F_t. A maneuvering one gives a large F_t, which the rule then excludes; that is
    how timing drops out.
- **F_p, the data's precision (amendment 1).** For every CSV value x that the back-out or the law reads, the script
  takes an uncertainty u(x):
  - for a column whose every value in that file round-trips through float32, half a float32 ulp of x;
  - for any other column, half a unit in x's last printed digit.
  - The script classifies each column per file, and `floor.log`'s header lists every column's class.
  - F_p sums |s(x + u) − s(x − u)|/2 over the values read. The back-out and the law are summed separately and added,
    taking the law's larger sum over H.
- **Actuators: 0.** Vol II p.16: "No actuator dynamics were modeled".
- **References and trimmed controls: 0 for ail and rdr,** by the dependency graph above.
  - For el, three things make an unknown constant per member: its `longStkTrim`, SIM 5's replaced references (Q11),
    and the ρ0 inside its KEAS. That constant is the same order as Δel, and any estimate from the data absorbs it
    together with the signal's constant part.
  - So el is reported, never decisive.

**(d) The decision rule. Every step is decided per member (amendment 2).**
- **Surfaces:** ail and rdr. No parameter is estimated from the data beyond each sample's two backed-out surfaces.
- **13.3's samples:** for each member, every sample with 8.0 s ≤ t ≤ 30.0 s, except:
  - samples with |t − 15.0 s| ≤ 0.2 s, at the course step;
  - any sample without both neighbors inside 7.9 to 30.0 s.
  The window is footnote (a)'s start and Table 39's duration. SIM 5's later rows are ignored, as ADR-0028 does.
- **Counting:** a sample counts for a surface when |Δ| ≥ 10·F.
- **Precondition, per member:** at least 10 counting samples, over ail and rdr together.
- **The fallback, per member.**
  - A member short in 13.3 is evaluated in case 15, with `F16_gnc.dml`, under the same rule. Case 15's samples run
    from 8.0 to 180.0 s under the same neighbor rule (inside 7.9 to 180.0 s), with no step to exclude. The
    navigator is fed the CSV's latitude and longitude, with `circlePoleSW` = 1.
  - Each member's verdict comes from the first case in which it meets the precondition. The other member's 13.3
    verdict stands.
  - A member short in both cases is "cannot discriminate", and no comparison is computed for it.
- **Comparison, per member, in its deciding case:** e(H) = s_backed − s_law(H). A counting sample supports H when
  |e(H)| ≤ |Δ|/4; the other hypothesis is then at least 3|Δ|/4 away.
- **Verdicts.** A member's verdict is H when every counting sample supports H, and "neither" otherwise.
- **The result** is one of three labels, and it always names both members' verdicts and deciding cases
  (amendment 14):
  - "discriminates: H" when both members' verdicts are H;
  - "cannot discriminate" when either member has no deciding case;
  - "inconsistent" otherwise. That covers "neither" against "neither", and H_I against H_E; the two verdicts show
    which.
  Every result goes to Joe before ADR-0029's rate clause is written. "Cannot discriminate" and "inconsistent" come
  with ICD v1's inertial rates as the default.
- **The judgment numbers, fixed here:** 10 for the ratio, 1/4 for support, 10 counting samples, δ = 0.02 s, the
  windows, and the ±0.2 s exclusion.
- **Assumptions, named:**
  - Each member fed p, q and r in one frame. ail and rdr test only ω_ie's x and z components.
  - A verdict covers both laws. Their rate inputs are the same S-119 variables, feeding an LQR identical from
    `chiEst` on (addendum §5), and each member is one tool across every F-16 case.

**Reported, never selected** (in `compare.log`):
- **Segment statistics (amendment 4).** Per member and surface, e(H_I) and e(H_E) for each segment of the deciding
  case: the count, the mean, and the largest |e|.
  - In 13.3 the segments are 8 to 15 s, and after the step.
  - Case 15 has one segment.
  - They are over the segment's counting samples. They make a common-mode bias visible, and (d)'s rule is
    unchanged.
- e under H_N.
- The back-out with inertial aero rates, D3's rejected reading, as a sensitivity.
- el's comparison, with every estimated parameter counted:
  - `longStkTrim`, fitted at t = 0 under each H: one per member and hypothesis;
  - `altCmd` and `keasCmd` at their t = 0 values: two per member;
  - SIM 5's three references at its t = 0 α, θ and KEAS;
  - KEAS taken as vt·√(ρ/ρ0), with ρ0 the US 1976 sea-level density.

**Outputs** (J2):
- `floor.log`:
  - a header with the inputs, their SHA-256s, the Python version, and each column's precision class;
  - phase 1's results;
  - the phase 2 self-check's largest deviation;
  - per member and sample: t, the backed-out surfaces, each channel's residual, Δ, F_b, F_t, F_p and the ratio;
  - each member's count, precondition and deciding case.
  It never prints the law's outputs.
- `compare.log`:
  - per member, e(H_I) and e(H_E) at each counting sample;
  - the verdicts and the result;
  - the segment statistics and the reported variants.
- Values are printed with `repr`, so a rerun is byte-identical.

**Order.**
1. Write the script and pass `ruff`. Commit and push it before its first run, so git records the pre-registration.
2. Run phase 1, then phase 2 on 13.3 for both members, then phase 2 on case 15 for any member short in 13.3. Commit
   `floor.log`.
3. Run phase 3 for each member that has a deciding case, and commit `compare.log`.
4. Stop, and bring Joe the result, the counts, the per-surface ratios, the reported variants, and J8's
   recommendation.

**An Earth-relative result** comes with the questions ADR-0029's clause must answer, none decided here:
- How each forms ω_ie_b from ICD v1 alone: from `lat` (geodetic) and φ, θ, ψ, as C_bn·Ω(cos lat, 0, −sin lat). A
  frame flagged `LATLON_NOT_MODELED` is refused, never computed from its placeholder zero (amendment 11).
- Ω's source: in the peer, `geodesy::kOmegaIe` (Table 73's value); in the port, its own named constant of the same
  value (ADR-0008).
- The DCM: the peer uses `core/util/frames` (ADR-0001: "no model writes its own"), and the port writes its own,
  in the same operation order (amendment 8).
- A non-rotating Earth: the peer cannot see the Earth model through the frame, so a config with a non-rotating Earth
  would still subtract Ω.

## Deliverable 2: ADR-0032, `docs/decisions/0032-earth-relative-body-rate.md` (outline only)

**Related.** It elaborates:
- ADR-0003: the Earth model's rotation rate gains a second consumer;
- ADR-0016: the new field is computed per stage;
- ADR-0017: its quiet-NaN default is the precedent for this field's;
- ADR-0020: the rate arrives through `EnvironmentSample`, with a utility;
- ADR-0021: the `wrench_b` signature stays fixed.
It supersedes in part one clause of ADR-0020's Consequences (J4).

**Context.**
- The DMLs are silent on the frame.
- Vol II D.1.17 (p.309): "In SIM 4, the zero rotational rate relative to the Earth induced no aerodynamic side
  force, rolling moment, or yawing moment."
- D3's computed check on case 11:
  - Earth-relative rates predict SIM 5's Y, L and N to 3e-8;
  - inertial rates miss by factors of 4 to 16;
  - SIM 4's values, about 1e-11 to 1e-15, fit Earth-relative rates only.
- D3 also lists every reader of body rates in the tree.

**Decision** (D3 (B1), J3, and amendments 5 and 6):
- **What the core supplies.** The body rate relative to the Earth, ω_b − ω_ie in body axes:
  - a field `EnvironmentSample::omega_ie_b` (rad/s), computed as `c_nb · earth.omega_ie_n(vs_stage)`, the EOM's own
    expression;
  - an accessor `air_data::omega_eb_b(vs, env)` returning `vs.omega_b() − env.omega_ie_b`, which readers call. Its
    comment states that it is Earth-relative, not air-relative.
  - ω_ie_b is zero for `earth/flat` and for a non-rotating `earth/round`, where the relative rate equals ω_b.
  - It is not published as a signal.
- **A per-stage field (amendment 6).**
  - It is per stage, like the refreshed atmosphere and unlike the frame-latched `wind_n`.
  - It is set in each stage's environment before every `wrench_b` and `integrate_state`, on the step path and on
    `Simulation.derivative`'s probe path alike.
  - Anything that builds its own `EnvironmentSample` sets it from the sim's Earth model at the evaluated state.
  - PR 3b corrects the struct's header comment, which says the sample is "held constant through that vehicle's
    wrench and EOM stages".
- **A NaN default (amendment 5).** This follows `atmosphere`, and ADR-0017's Decision, quoted (`0017:56-59`):
  entries "default-initialize to a quiet NaN, never an unspecified value: "never written" and "written non-finite"
  are deliberately the same, loudly reported state".
  - The core always sets the field on a sim's own stages, which are the only ones `check_wrench` sees. The default
    therefore reaches only code that builds its own `EnvironmentSample`: PR 7's evaluator, a binding, or a unit
    test.
  - There, a forgotten value gives the reader a NaN wrench (amendment 13). That fails PR 7's self-check against
    `wrench_total`, or the unit test's own assertion. It never runs as a non-rotating Earth.
  - An `earth/flat` test sets zero explicitly.
- **Readers.** The F-16 aero (PR 6b) and the template's aero (PR 3b). The template's `earth/flat` values stay
  identical, which a test in PR 3b shows.
- **Not readers.** The notional transport's aero and `tests/plugins/nesc_aero` keep the inertial ω_b. B2 stays
  PLAN.md's unscheduled item.
- ω_b stays the state's inertial rate (ADR-0003, ADR-0021), and ICD v1's p, q and r stay inertial.
- **Excluded, and named:** Earth-relative is not air-relative. Under a non-uniform wind (a shear, or turbulence's
  rotational part) the air mass rotates, and that rotation is not supplied. With no wind or a uniform wind the two
  are equal.
- The `kCoreApiVersion` bump and the stale-plugin rejection are PR 3b's. The record gives no version number.

**Rejected alternatives:**
- (A) the plugin computes the rate itself. Its Earth rate can disagree with the configured Earth model, and the
  latitude lives in the model's own position representation.
- (B2) every aero migrates now. That moves M3, R1, M4 and M6 values, and it is measured first.
- Inertial rates everywhere, with D3's numbers: inertial rates miss by factors of 4 to 16, and damping inertial rates
  gives about 99 times case 11's roll band at 180 s.
- Supplying an air-relative rate now.
- A field holding the relative rate, ω_b − ω_ie_b. That puts a copy of state into the environment.
- An accessor with no field. That needs the Earth model inside the plugin, a new binding.
- Publishing the rate as a signal. Nothing reads it from a log, it is computable offline, and a new column moves
  every column-pinning test.
- **A zero default.** It was proposed for three reasons: a non-rotating Earth is a valid physical case, `wind_n`'s
  zero means still air, and hand-built samples would need no edit. It is rejected because a forgotten value would
  then run silently as a non-rotating Earth.

**Consequences:**
- PR 3b implements it, and lists every test that builds an `EnvironmentSample` for a reader.
- PR 6b's aero reads it.
- PR 7's independent evaluator sets `omega_ie_b` from the sim's Earth model at the sample's frame-start state. A
  forgotten value gives a NaN wrench, which fails its self-check against `wrench_total`.
- Trim and `Simulation.derivative` see the rate through the same stage path, so D5's aero rate terms are
  Earth-relative.
- Q5's sub-condition holds, since the rate comes from the state and the environment, never a peer binding.
- The tree carries two documented conventions until B2.
- `check_environment`'s scope is unchanged. The field is the core's own value, not an environment plugin's output.

## Deliverable 3: ADR-0029, `docs/decisions/0029-sim-side-flight-software-peer.md` (outline only)

**Related.**
- It elaborates:
  - ADR-0007: the loopback's in-process precedent;
  - ADR-0017 and ADR-0022: the write seam and the exchange stage;
  - ADR-0008: the port's boundary;
  - ADR-0006: no ICD change;
  - ADR-0002 and ADR-0030: units and keys;
  - ADR-0010: frame-keyed time;
  - ADR-0028: the bands R6 and R7 fly against.
- It supersedes nothing, as the table below shows.

**Context.**
- Joe's 2026-09-29 decision: the NESC laws run outside `embedded/` first, are shown to match the ensemble, and are
  then ported.
- ICD v1 carries no command field and no EAS.
- Both DMLs are memoryless and state no execution rate (addendum §5).
- Vol II calls the `.dml` files normative (p.16, p.26).
- P2's SHA-256s are in addendum §1.1.

**Decision.** It covers both laws: `F16_control` for PR 11a, and `F16_gnc` for PR 11b.
- **Q8, the peer.** A transport plugin, `aircraft/f16/sim/transport/nesc_control`.
  - Its location rests on architecture §4's taxonomy rule, quoted: "transports and other swappable infrastructure
    live under `sim/`", inside either root.
  - It runs the law on the packed ICD v1 frame at the exchange stage and returns `astra_sw_to_sim_v1`.
  - It is in-process, like the loopback. It zeroes its reply and reads no clock.
  - Its commands reach the inputs only through ADR-0022's command map onto ADR-0017's `InputRegistry`, latched the
    same frame. It holds no write handle.
  - Its inputs are ICD v1's SI fields plus EAS derived from `tas`, `cas` and `mach` (J5).
  - **The refusal list (amendment 11).** The approved contract refuses a frame whose status flags mark any input
    the law reads, and it never computes from a placeholder zero. A frame is refused when:
    - `CAS_SUPERSONIC`, `ATMOSPHERE_INVALID` or `WIND_INVALID` is set, or Mach is not above zero;
    - `LATLON_NOT_MODELED` (`icd/astra_icd.h:36`; lat = lon = 0 on a flat Earth) is set, on every path that reads
      `lat` or `lon`. Those paths are 13.4's logic, `F16_gnc`'s navigator, and the rate clause if it comes out
      Earth-relative.
    - Under J8 (a), a flagged frame-0 exchange is refused, never latched.
    A refused frame yields non-finite commands, and the exchange halts `kTransport`, naming the field.
  - Q9 (b) follows as a consequence. The tools' transport refusal stays, and the peer's log is not replayable. The
    peer runs through `Simulation`, from C++ or `astra._core`.
- **Q10, the schedule.** It lives in `transport.peer` keys, each step keyed to a frame, after the loopback's
  precedent. Values follow ADR-0002 and ADR-0030. The port takes the same schedule as arguments, and R10 checks both.
- **Q12, the rate.** Exchange divisor 10. A divisor-1 run is reported, never selected.
- **Q13 with amendment 7, the port's equivalence.**
  - Bitwise equality on R13's objects, with only N-06's exclusions: the `transport` block, the transport's
    `/plugins` row, and the `transport.*` signals.
  - Two implementations: `aircraft/f16` in C++23, and `embedded/` in C++17, using only `icd/` and `embedded/hal/`.
    No include runs from `aircraft/` into `embedded/`.
  - Both start from one trim result, written once into the shared config.
  - Both builds' `-march` and `-ffp-contract` are stated by PR 12 before the first comparison. Today neither is set.
- **Q26, the units.**
  - The law runs in SI. Each gain and limit is the DML's value times named `core/util/units` conversions, using the
    functions where one exists.
  - The DML's own constants stay named law constants, keeping their effect: π = 3.14159265 inside `cos` and `atan2`,
    6,076.12 ft/nm, and 60 nm/deg.
  - KEAS converts with the exact knot.
  - The only boundary steps:
    - the aileron's sign, +LWD to ICD v1's right wing down, flipped after the interconnect reads +LWD;
    - PLA percent to a 0..1 throttle, written to both entries;
    - the normalized pilot controls, which stay dimensionless.
  - The port uses its own named constants. Customary units appear only in R11's independent reference.
- **Q27, 13.4's logic.**
  - The base course is the initial-plane line: through the base point, at χ = 45° true, with the plane's scales
    fixed at lat0 (WGS-84's M0 and N0, and h0).
  - The base point's source is J8's answer.
  - From frame 20000 the desired course is 609.6 m to its right.
  - The error is d − 609.6 m, positive to the right.
  - The logic reads only `lat` and `lon`, at every exchange, with h0 fixed.
  - The rhumb line is a reported variant, never selected (R6 lists the others).
- **The laws' rate frame:** per Joe's decision after Deliverable 1.
- Q11 is cited, not recorded: it is the M7 plan's decision.

**Rejected alternatives,** from the plan's questions:
- Q8: a core controller kind, a Python `write_input` driver, or a C++ test harness;
- Q10: an ICD `_v2` command message;
- Q12: divisor 1 as the selection;
- Q13: a band against the sim-side run, a band against the ensemble, or one C++17 source compiled into both;
- Q26: the DML's units inside the law;
- Q27: the rhumb line as the base course;
- J8: the alternative not chosen.

**Consequences:**
- PR 11a, 11b and 12 implement it, through R6, R7, R10, R11, R13, R22 and R23.
- **What Q13's bitwise criterion requires of the port (amendment 8).** The port must keep the peer's floating-point
  expression order:
  - the same factors in the same order for each SI constant;
  - the same libm calls (`cos`, `atan2`, `sqrt`);
  - the same DCM operations, if the rate clause needs one.
  A difference in expression order is a porting defect, not a floating-point cause under Q13.
- The no-alloc and two-run determinism invariants cover the peer.
- ADR-0023's two clock sites are unchanged.
- `icd/` is unchanged.

## Deliverable 4: architecture rev 12 (amendment 10)

- **The revision line:** "rev 12, <date>: §7/§10/§11/§16 for ADR-0029 (…) and ADR-0032 (…)". It reads proposed until
  Joe accepts both. It is a status line, so it is written in an edit Joe approves manually.
- **§7:** a paragraph on the body rate relative to the Earth (*ADR-0032*, *planned*, PR 3b). It covers the per-stage
  field and its NaN default, the accessor, the readers, the two conventions and the exclusion. §6 is unchanged: it
  defines the inertial ω_b, which stays true.
- **§10:** `nesc_control` joins the Implementations list (*planned*, PR 11a and 11b, *ADR-0029*), as an
  aircraft-local, in-process peer. The F-16 laws run at divisor 10.
- **§11:** `embedded/` gains the F-16 port (*planned*, PR 12): a separate C++17 implementation that takes the
  schedule as arguments, held to ADR-0029's bitwise equivalence.
- **§16:** one sentence in the M7 note. It replaces "ADR-0029 … follows in PR 1b", which goes stale at the merge,
  and it names ADR-0032.

## Supersession (item 4, amendment 7)

No accepted record is edited. "Not superseded" means the sentence stays true as written.

**ADR-0032 touches:**

| Record | Sentence | Verdict |
|---|---|---|
| ADR-0003 | "ω_b (3) body angular rate relative to inertial, body axes (p, q, r)" | Not superseded: the state stays inertial |
| ADR-0003 | "The Earth model owns the meaning of `p_n` and supplies the EOM with three things: … the Earth rotation rate expressed in n (zero for flat and round-non-rotating) …" | Not superseded: elaborated, since the same rate now also reaches the stage environment |
| Arch. §6 | "ω_b (3) body angular rate relative to inertial frame, expressed in body axes (p, q, r)"; "with the Earth model supplying three things …" | Unchanged and true |
| ADR-0018 | "(2) assembles `AeroInputs` (`true_airspeed_mps`, `alpha_rad`, `beta_rad`, `omega_b`, …)" | Already superseded by ADR-0020; nothing further |
| ADR-0020 | "reads whatever it needs from `(VehicleState, EnvironmentSample)` and its own config-resolved members" | Not superseded: the rate arrives through `EnvironmentSample` |
| ADR-0020 | "A concrete plugin overrides `WrenchSource::wrench_b(const VehicleState&, const EnvironmentSample&) const noexcept` directly" | Not superseded: the signature is unchanged |
| ADR-0020 | "**Air-data math becomes core utilities**, not a mandatory struct or sealed method" | Not superseded: elaborated by one utility |
| ADR-0020 | "`HealthMonitor::check_environment(…)` … checking `wind_n` when wind is configured and every populated `atmosphere` field when atmosphere is configured for NaN/Inf" | Not superseded: the new field is the core's own value, and the core always sets it on a sim's own stages. A forgotten value in code that builds its own `EnvironmentSample` gives a NaN wrench, which fails PR 7's self-check against `wrench_total` or the unit test's own assertion; `check_wrench` never sees it |
| ADR-0020 | "Benefit: any future vehicle's aero/propulsion model reads exactly the inputs already available to it (…) with zero further core interface change." | **Superseded in part (J4):** that clause, for this rate only |
| ADR-0021 | "Every accessor's name/return value (`p_n()`, `v_b()`, `q_nb()`, `omega_b()`) is unchanged, and no plugin call site changes" | Not superseded: `VehicleState` is untouched |
| ADR-0021 | "no `wrench_b(vs, env)` signature change (ADR-0020 keeps it fixed)"; "`integrate_state()` takes … the identical `(vs, env)` shape" | Not superseded: the field reaches `integrate_state` too |
| ADR-0016 | "`WrenchSource::wrench_b()` and the `AtmosphereModel::at()` sample it consumes are evaluated fresh at each of `Integrator`'s four RK4 sub-stages, using that stage's own state" | Not superseded: elaborated, since the new field is also per stage |
| ADR-0016 | "No plugin-facing interface changes: … `EnvironmentSample` gains no field, and `kCoreApiVersion` is not bumped" | Not superseded: it describes ADR-0016's own change, and its loader-boundary test is what makes PR 3b bump |
| ADR-0017 (`:56-59`) | Every entry's values "default-initialize to a quiet NaN, never an unspecified value: "never written" and "written non-finite" are deliberately the same, loudly reported state" | Not superseded: the precedent for `omega_ie_b`'s NaN default |
| ADR-0001 | "Inertial \| i \| ECI; used only by the rotating-Earth models"; "Any later frame (wind, stability, sensor mount) is defined relative to b or n here, never freshly." | Not superseded: a rate between existing frames, with no new frame |
| ADR-0001 | "The ICD (ADR-0006) carries φ, θ, ψ, p, q, r, u, v, w in exactly these conventions." | Not superseded: ICD p, q and r stay inertial |

**ADR-0029 touches:**

| Record | Sentence | Verdict |
|---|---|---|
| ADR-0002 | "Inside `core/`, `plugins/`, and `aircraft/` every quantity is a plain `double` in SI … Conversions happen only at boundaries …" | Not superseded: Q26's law is SI, and its DML constants are law constants, not conversions |
| ADR-0002 | "Every boundary conversion is a named function in `core/util/units` with a unit test; nothing multiplies by `0.3048` inline." | Not superseded: the named functions are used; `embedded/` is outside the record's scope |
| ADR-0030 | "A config key may carry a suffix naming the unit of the value as written …" | Not superseded: the schedule keys follow it |
| ADR-0006 | "A layout is **never edited**. Growing the ICD (…) adds a `_v2` struct and bumps the version" | Not superseded: no ICD change |
| ADR-0001 | "Control-surface sign conventions in the ICD (positive elevator, aileron, rudder) are defined against these axes …"; "`core/util/` owns the single quaternion/DCM/Euler conversion set; no model writes its own." | Not superseded: the aileron flips at the law's boundary; the peer would use `core/util` for any DCM |
| ADR-0017 | "Three callers need to over the project's life: a config literal (open-loop, this PR), Python trim/linearize (PR 5), and M4's transport/`ActuatorModel` later. All three must write through one mechanism …" | Not superseded: the peer is a transport, and its commands use that one mechanism |
| ADR-0017 | "`InputRegistry::latch()`, called once per vehicle per major frame by `Simulation::step` — after `vehicle.step(ctx)`, before `vehicle.integrate(...)` … commits every entry's pending value"; "Zero-order hold across a frame's four RK4 sub-stages follows from `latch()` running exactly once per frame" | Not superseded: the exchange writes the peer's commands as pending, and the same frame's `latch()` commits them (ADR-0022) |
| ADR-0017 | "It never commits a non-finite pending value, warn or halt alike" | Not superseded: the peer's non-finite commands never reach `write()`, because ADR-0022's validation halts first |
| ADR-0017 (`:191-195`) | "A transport writer or `ActuatorModel` that is not itself a `WrenchSource` … has no seam-provided route to a write handle for a name it does not own. Even a writer that could reach `find()` would depend on `vehicle.models`' load order …" | Not superseded: ADR-0022 resolved it for transports. `Simulation` resolves the command map's handles at init, after every `bind_inputs()`, and the peer holds no handle |
| ADR-0007 | "In v1 the flight software runs as a separate process on the same host and must be stepped in lockstep by the sim …" | Not superseded: the port is that process; the peer is a sim-side verification instrument |
| ADR-0007 | "`LoopbackTransport` calls an in-process functor for tests."; "The transport has its own divisor (default 10 → 100 Hz) …" | Not superseded: elaborated, a second in-process peer at divisor 10 |
| ADR-0008 | "`embedded/` links only `icd/` and `embedded/hal/`; a link dependency on `astra_core` is a build error." | Not superseded: the port obeys it, with two sources and no include into `embedded/` |
| ADR-0008 | "A user's real flight software lives in its own repo and vendors or submodules `icd/` and `embedded/hal/` …" | Not superseded: the port is an in-tree example, as the M4 autopilot is |
| ADR-0022 | "A transport plugin under `plugins/sim/transport/` implements a `Transport` interface" | Not superseded: it names where the shared transports live. ADR-0029 quotes §4's taxonomy rule ("transports and other swappable infrastructure live under `sim/`", inside either root) as the basis for `aircraft/f16/sim/transport/` |
| ADR-0022 | "Commands computed from frame-k state are applied from frame k, with zero compute delay (disclosed)." | Not superseded: the peer computes inside the exchange (R22) |
| ADR-0022 | "`transport: { plugin, divisor, timeout_ms, handshake_timeout_ms, peer_timeout_ms, commands, limits }`" (unknown keys are errors) | Not superseded: `peer` is the transport's own block, as for the loopback |
| ADR-0022 | "the peer zeroes its reply struct before filling it."; "A non-finite command halts `kTransport` naming the ICD field, unconditionally." | Not superseded: the peer zeroes its reply, and its refusal uses this halt |
| ADR-0022 | "Same-build, same-machine determinism only." | Not superseded: R13 is a test result across two peer builds, with the flags stated |
| ADR-0024 | "It is an include, not a link dependency, so ADR-0008's rule (…) holds." | Not superseded: the port speaks the shm layout unchanged; the in-process peer uses none of it |
| ADR-0023 | "Exactly two sites are allowed" | Not superseded: the peer reads no clock |
| ADR-0010 | "`t = frame × dt` is computed on demand, never accumulated." | Not superseded: the schedule is keyed to frames |

## Deliverable 5: PLAN.md, the M7 plan and STATE.md (at /wrap)

- **PLAN.md:** PR 1b's line.
- **The M7 plan:** one dated edit, to the Obligations row "1b, 11a". It records the outcome, with a pointer to this
  plan, and carries forward whatever remains for PR 11a. Nothing else in that plan changes.
- **STATE.md,** written as main reads it after the merge:
  - PR 3b is eligible once Joe accepts ADR-0032;
  - PR 11a's carried items (the R11 floor and margin, and R12's ρ) remain;
  - the rate frame's outcome and J8's answer are recorded.

## Execution (item 6's order)

1. **The worktree.** Run `git fetch origin`, then EnterWorktree `m7-pr1b`, and confirm HEAD equals `origin/main`.
   If it does not, run `git worktree add .claude/worktrees/m7-pr1b -b worktree-m7-pr1b origin/main` instead. Move
   this draft into the worktree as `docs/plans/m7-pr1b-adrs.md` (`mv` before its first add), leaving nothing
   untracked in the main checkout.
2. **The plan's status line.** Write "approved by Joe" in its own Edit, which Joe approves manually. A refusal is
   not retried by any route (lesson 2026-09-29). Commit, push, and open the draft PR.
3. **Deliverable 1.**
   - Write the script and commit it.
   - Run phase 1, and phase 2 per member (13.3, then case 15 where needed). Commit `floor.log`.
   - Run phase 3 per member, and commit `compare.log`.
   - **Stop, and bring Joe the result and J8's recommendation.**
4. **Joe decides** the laws' rate frame and J8.
5. **The guard.** Tell Joe the ADR writes are ready, and wait until Joe says the `docs/decisions/` guard is armed.
   Never arm it.
6. **The records.**
   - Write ADR-0032, and ADR-0029 with its rate clause and base point per Joe's decisions, both without status
     lines.
   - Write each "proposed" status line in its own Edit, which Joe approves manually.
   - Write architecture rev 12, its revision line the same way.
   - Touch only these two files under `docs/decisions/`, and nothing under `tests/golden/`.
   - Commit and push.
7. **Checks.**
   - `python3 scripts/docs_check.py`.
   - The manual link check: a scratchpad script, written with Write, that runs `docs_check.check_links()` over both
     ADRs, `docs/architecture.md` and this plan.
   - `git diff --check origin/main..HEAD`.
   - `ruff check` and `ruff format --check` on the script.
8. **`/review-cycle`.** An adversarial Codex review, on committed-branch scope, of both ADRs, the architecture diff,
   and the script and its output (J7).
   - Its read-only input includes the main checkout's `docs/refs` path, the Vol II pages cited, the addendum and the
     M7 plan.
   - At most two correction rounds. A change to a decision, or a finding that changes the result, goes to Joe.
9. **The full gate (J6).** Run it in the background on the final candidate. It passes on exit 0 with `check: OK`.
   Commit the trimmed summary.
10. **`/human-review`.** The PR body holds:
    - the commands, exit codes and counts;
    - the link check's result;
    - Codex's verdict and thread id;
    - the analysis result;
    - `git diff --name-only origin/main..HEAD`.
11. **Acceptance.** Joe accepts each ADR by name.
    - Each "accepted" line, and rev 12's, is written in its own Edit, which Joe approves manually.
    - That is a Markdown-only delta, so it gets docs-check, the link check and `git diff --check <gated-head>..HEAD`.
12. **`/wrap`,** per Deliverable 5.

## Verification

- Phase 1's four self-checks pass before any 13.3 number exists.
- Phase 2's law self-check passes before phase 3 runs.
- `floor.log` is committed before `compare.log` exists, and git shows that order.
- `floor.log` lists every column's precision class, and never prints the law's outputs.
- docs-check, the link check and `git diff --check origin/main..HEAD` all exit 0. `ruff` passes on the script.
- The full gate exits 0 with `check: OK`.
- Codex's adversarial verdict, with its findings verified and resolved, within two rounds.
- Every number in both ADRs is traced to the addendum, a cited page, or the committed output.
- Joe's explicit acceptance of ADR-0029 and ADR-0032, each by name.

## Risks and rollback

- **The analysis cannot discriminate, or is inconsistent.** ICD v1's inertial rates are then the default, and Joe
  decides. That is a valid outcome, not a failure.
- **A script defect found after phase 2.** It is fixed in a new commit naming it, every output is kept, and none
  of (d)'s numbers changes.
- **An Earth-relative result.** It adds amendment 8's DCM-order requirement and the non-rotating-Earth question,
  which come to Joe with the result.
- **A refusal by the guard or the classifier.** It is never retried by another route; it is named in the PR body.
- **Rollback.** Documents and one evidence script only, so the PR reverts cleanly.
