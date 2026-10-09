# ADR-0029: Sim-side flight software as an in-process peer, and the port's equivalence

Status: **proposed**, 2026-10-09
Date: 2026-10-09
Owner: Joe
Related/supersedes: supersedes nothing; no accepted record is edited. It reads ADR-0022's "A transport plugin
under `plugins/sim/transport/`" with architecture §4's taxonomy rule (Decision 1).

This record elaborates ten others:
- ADR-0001: frames, for Decision 5's aileron sign and Decision 7's DCM from `core/util/frames`;
- ADR-0007: a second in-process peer, after `LoopbackTransport`;
- ADR-0017 and ADR-0022: the write seam and the exchange stage the peer's commands pass through;
- ADR-0008: the port's boundary;
- ADR-0006: no ICD change;
- ADR-0002 and ADR-0030: units and config keys;
- ADR-0010: time keyed to frames;
- ADR-0028: the bands the peer flies against.

Drafted with:
- `docs/plans/m7-f16-nesc-oracle.md`: Q8 to Q13, Q26 and Q27, amendments 4 and 7;
- `docs/plans/m7-f16-inventory.md`: §5, §6, §8 and §10;
- `docs/plans/m7-pr1b-adrs.md`: J5, J8, amendments 8, 9 and 11, and Joe's 2026-10-09 decisions.

Governs PLAN.md's M7 PR 11a, 11b and 12.

## Context

- **Joe's decision** (2026-09-29, PLAN.md's M7 row): the NESC F-16 control laws run outside `embedded/` first,
  are shown to match the ensemble, and are then ported to `embedded/`.
- **The laws.** `F16_control.dml` (Rev F) flies cases 13.1 to 13.4, and `F16_gnc.dml` (Rev C) flies 15 and 16.
  - Their SHA-256s are in the addendum's §1.1.
  - Vol II calls them "the normative description" (p.16, p.26).
  - Both are memoryless, and neither states an execution rate (addendum §5).
  - Their units are ft, KEAS, deg, rad/s and normalized controls (§6).
- **ICD v1** (`icd/astra_icd.h`) carries the state in SI:
  - p, q and r "relative to INERTIAL space (what a gyro measures)";
  - `tas`, `cas` and `mach`, but no EAS;
  - `lat`, `lon` and `altitude`;
  - status bits that mark placeholder zeros.
  It carries no command field and no Earth rate. ICD v2 is M9's, and `icd/` does not change in M7.
- **The exchange stage** (ADR-0022) packs the frame at `frame % divisor == 0`, after the environment sample and
  before the vehicle's `step()`. It validates the reply, stages the command map through ADR-0017's
  `InputRegistry` handles, and lets the same frame's `latch()` commit them.
- **`LoopbackTransport`** (`plugins/sim/transport/loopback`) is already an in-process peer built from config, with
  its own `transport.peer` block and a schedule keyed to frames.
- **The tools refuse a config with a `transport` block:** `astra run`, `astra trim`, `astra.montecarlo`,
  `astra.scenario` and `astra.log.replay`. `astra.lockstep` accepts only the shm transport. Joe chose to keep
  that (the M7 plan's Q9 (b)).
- **13.4.** Vol II gives it no formula: "user-supplied logic" (C.1.16, p.65).

## Decision

1. **The peer (Q8).** A transport plugin, `aircraft/f16/sim/transport/nesc_control`, runs the law on the packed
   `astra_sim_to_sw_v1` frame at the exchange stage and returns `astra_sw_to_sim_v1`.
   - **Its location** rests on architecture §4's taxonomy rule: inside either plugin root, "transports and other
     swappable infrastructure live under `sim/`", and an aircraft folder is filed by "the *same* kinds". ADR-0022's
     "A transport plugin under `plugins/sim/transport/`" names where the shared transports live. It is not
     superseded.
   - **In-process,** as the loopback is. It zeroes its reply before filling it (ADR-0022), and it reads no clock,
     so ADR-0023's two clock sites are unchanged.
   - **Its commands** reach the inputs only through ADR-0022's command map onto ADR-0017's `InputRegistry`. They
     are written as pending and latched the same frame, so commands computed from frame k's state apply from
     frame k. The peer holds no write handle.
   - Whether `F16_gnc` is a second plugin beside it, or a law selected in its config, is PR 11b's to decide.
2. **Its inputs, and the refusal list.** The law reads ICD v1's SI fields, plus EAS derived from `tas`, `cas` and
   `mach`, for valid subsonic frames only. The derivation inverts `icd_pack::calibrated_airspeed` for the static
   pressure p, then takes EAS = a0·M·√(p/p0), with the packer's own a0 and p0. The peer never computes from a
   placeholder zero. A frame is refused when:
   - `ASTRA_ICD_STATUS_CAS_SUPERSONIC`, `ASTRA_ICD_STATUS_ATMOSPHERE_INVALID` or `ASTRA_ICD_STATUS_WIND_INVALID` is
     set, or Mach is not above zero;
   - `ASTRA_ICD_STATUS_LATLON_NOT_MODELED` is set, meaning lat = lon = 0 on a flat Earth. This applies on every
     path that reads `lat` or `lon`: 13.4's logic, `F16_gnc`'s navigator, and the rate (Decision 7);
   - for 13.4's base point (Decision 6), the first exchange's frame is not 0.
   A refused frame yields non-finite commands, and the exchange halts `kTransport`, naming the field (ADR-0022,
   unconditionally).
3. **The schedule (Q10).**
   - The law's commands are `transport.peer` config keys, each step keyed to a frame, after the loopback's
     precedent. They are the initial values, and each case's steps from Vol II Tables 37 to 43.
   - Values follow ADR-0002 and ADR-0030: SI, unless a key's suffix names another unit, converted at load.
   - The port takes the same schedule as arguments keyed to frames. R10 checks both against Vol II's tables.
4. **The rate (Q12).** Exchange divisor 10 (100 Hz). The DMLs state no rate, so Joe's decided fallback applies,
   which is the architecture's default (addendum §10). A divisor-1 run is reported, never selected. A band failure
   at divisor 10 is a finding for Joe.
5. **The units (Q26).** The law runs in SI end to end.
   - Each gain and limit is the DML's value times named `core/util/units` conversions (`degrees_to_radians`,
     `feet_to_meters`, `knots_to_meters_per_second`). It is written where the constant is declared, never as a
     premultiplied number.
   - KEAS converts with the exact knot, 1852/3600 m/s.
   - The DML's own numeric constants stay as named law constants, with their effect kept: π = 3.14159265 inside
     `F16_gnc`'s `cos` and `atan2`, 6,076.12 ft/nm, and 60 nm/deg. They are part of the law, not unit conversions.
     The truncated π alone moves the GNC's east distance by about 2.1e-6 relative at 89.95° (addendum §8).
   - **Three boundary steps, and only three:**
     - the aileron's sign, from the DML's +LWD to ICD v1's right wing down, flipped after the 0.008 interconnect
       has read the +LWD value;
     - PLA percent to a 0..1 throttle, written to both throttle entries;
     - the normalized pilot controls, which stay dimensionless.
   - Every DAVE-ML `minValue`/`maxValue` is a saturation, inputs included.
   - The port, which cannot link `core/` (ADR-0008), declares the same products with its own named constants.
     Customary units appear only in R11's independent reference.
6. **13.4's logic (Q27, J8).**
   - **The base point.** lat0, lon0 and h0 are ICD v1's `lat`, `lon` and `altitude`, latched from the exchange
     whose frame is 0. A first exchange whose frame is not 0 is refused, and so is a frame-0 exchange flagged
     `LATLON_NOT_MODELED`. Neither is ever latched.
   - **The base course** is the initial-plane line: through the base point, at χ = 45° true, in that point's local
     north-east plane, with the plane's scales fixed there.
     - Δn = (lat − lat0)·(M0 + h0) and Δe = (lon − lon0)·(N0 + h0)·cos lat0.
     - M0 and N0 are WGS-84's meridian and prime-vertical radii at lat0: `geodesy::meridian_radius` and
       `geodesy::prime_vertical_radius` in the peer, and the port's own named constants.
   - **The distance.** The signed cross-track distance is d = −Δn·sin χ + Δe·cos χ, positive to the right.
     `lateralDeviationError` = d − 609.6 m (2,000 ft) from frame 20000 (t = 20 s), and 0 before it.
   - The logic reads only `lat` and `lon`, at every exchange, with h0 fixed.
   - The rhumb line through the base point is a reported variant, never selected. The M7 plan's R6 lists the
     others.
7. **The laws' rate frame: Earth-relative.** Both laws read the body rate relative to the Earth.
   - **How it was decided.** The M7 plan's obligation "1b, 11a" asked for a decision from data.
     - M7 PR 1b pre-registered an analysis (`docs/plans/m7-pr1b-adrs.md`, Deliverable 1), and committed its
       script before the first run. Its evidence is in `docs/reviews/evidence/m7-pr1b-rate-frame/`:
       `rate_frame.py`, then `floor.log`, then `compare.log`.
     - The analysis backed el, ail and rdr out of each member's 13.3 aero channels. It then evaluated
       `F16_control.dml` with inertial rates (H_I) and with Earth-relative rates (H_E).
     - **The rule's result: "inconsistent".** SIM 5's verdict is H_E. SIM 4's is "neither", because the rule
       requires every counting sample to support one hypothesis. By the rule, that result defaults to ICD v1's
       inertial rates.
     - **Joe's decision (2026-10-09): Earth-relative,** against the rule's default, on the evidence in
       `compare.log`:
       - SIM 5 supports H_E at 202 of 202 counting samples. |e(H_E)| is at most 1.5e-11° before the course step
         and 2.4e-9° after it.
       - SIM 4 supports H_E at 192 of 193 counting samples. Over 8 to 15 s, |e(H_E)| is at most 1.2e-6° (ail)
         and 3.8e-7° (rdr), against |Δ| of about 3.76e-3° and 2.39e-3°. All 111 of its rudder samples support
         H_E.
       - H_I is rejected at 394 of 395 counting samples.
       - The rate relative to n, reported and never selected, misses in both members.
     - **The one exception.** SIM 4's aileron at t = 24.9 s supports H_I. There are two post hoc readings, and
       neither is verified:
       - The floor's timing term, F_t, is a central difference, which nearly vanishes at a turning point. The
         aileron there reads 0.17287°, 0.17483° and 0.17239° at t = 24.8, 24.9 and 25.0 s.
       - SIM 4's sample-to-law offset may exceed the floor's δ = 0.02 s while maneuvering: its residuals after the
         step are at the 1e-4° level, against 1e-6° before it.
     - The rule, the script and both logs stand as committed, and nothing was rerun.
   - **What the evidence covers.** It is `F16_control`'s, from 13.3. Both members met the precondition there, so
     case 15 was never evaluated.
     - `F16_gnc`'s rate frame rests on the assumption the plan pre-registered with the rule (Deliverable 1, (d)):
       each member is one tool across every F-16 case, and both laws read the same S-119 rate variables into an
       LQR that is identical from `chiEst` on (addendum §5).
     - PR 11b's plan proposes whether to run the committed script on case 15 as a reported confirmation.
   - **How the peer and the port form it, from the ICD v1 frame alone.**
     - ω_eb = (p, q, r) − C_nb(φ, θ, ψ)·Ω·(cos lat, 0, −sin lat).
     - **C_nb** is the n-to-b DCM: v_b = C_nb·v_n, the EOM's `c_nb` (`frames::dcm_from_quaternion(q_nb)`,
       `core/include/astra/util/frames.hpp`). Here it is formed from the frame's Euler angles. This record writes
       C_nb for it throughout.
     - lat is the frame's geodetic latitude, and φ, θ, ψ are its 3-2-1 Euler angles.
     - Ω = 7.292115e-5 rad/s, Vol II Table 73's value for the atmospheric cases. The peer uses
       `geodesy::kOmegaIe`, and the port an equal named constant.
     - The peer forms C_nb through `core/util/frames` (ADR-0001: "no model writes its own"), and the port forms
       it with its own code.
     - A frame flagged `LATLON_NOT_MODELED` is refused (Decision 2).
   - **Why the peer computes the rate itself, which ADR-0032 rejects for the aero.** ADR-0032 has the core supply
     the aero's Earth-relative rate, because a wrench source's own Earth rate can disagree with the configured
     Earth model. The peer cannot take that route: it sees only the ICD frame, ICD v1 carries no Earth rate, and
     `icd/` does not change in M7.
   - **The residual risk, disclosed.** The peer cannot see the configured Earth model, so under a non-rotating
     Earth it still subtracts Ω.
     - PR 11a's plan proposes whether the peer refuses such a config at init.
     - An Earth rate, or an Earth-relative body rate, in ICD v2 (M9) would remove the limitation.
8. **The port's equivalence (Q13, amendment 7).** A C++17 program in `embedded/` runs the same laws against the
   same ICD v1 frames over the shm transport, through `astra.lockstep`.
   - **The criterion** is bitwise equality against the sim-side run, on the M7 plan's R13 objects:
     - both runs complete (`complete = 1`), with the same `frames_run` and exchange count;
     - the `sim_to_sw` and `sw_to_sim` streams at every exchange;
     - every signal name, unit and rate group, and every state and physics signal column;
     - `/final_state`;
     - the stored config and `/plugins`, after removing only the `transport` block and the transport's row.
   - **The only exclusions** are the `transport` config block, the transport's `/plugins` row and the
     `transport.*` signals, which differ by construction.
   - **Two implementations.** The peer is in `aircraft/f16` (C++23, the plugin build), and the port is in
     `embedded/` (C++17, `icd/` and `embedded/hal/` only). No include runs from `aircraft/` into `embedded/`.
   - **One trim result.** Both runs consume the same initial-state literals and `trimmedPilotControl` values,
     written once into the shared config.
   - **The build flags.** PR 12 states both builds' `-march`, `-ffp-contract` and every other
     floating-point-relevant flag before the first comparison. Today neither `cmake/AstraBuildFlags.cmake` nor
     `embedded/CMakeLists.txt` sets `-march` or `-ffp-contract`.
   - **A difference** is a porting defect until shown otherwise. If a stated floating-point cause makes bitwise
     equality infeasible, it returns to Joe.
9. **Q11** (the LQR's reference states and the trimmed pilot controls) is the M7 plan's decision. It is cited
   here, not recorded.

## Rejected alternatives

- **(Q8) A new core plugin kind for controllers.** It is Tier 3 core work, and it overlaps M8's actuator seam and
  ADR-0017's disclosed non-`WrenchSource` writer gap.
- **(Q8) A Python driver writing through `write_input`.** The tools accept it, and replay works. But it reads the
  previous frame's signals, so a one-frame timing difference from the port would have to be designed out, and the
  law would be written in Python and then ported again.
- **(Q8) A C++ test harness.** It is cheap, but no user can run it.
- **(Q10) An ICD `_v2` command message.** ICD v2 is M9's.
- **(Q12) Divisor 1 as the selected rate.** It is reported, never selected.
- **(Q13) A band against the sim-side run, or against the ensemble.** A porting defect smaller than the band
  passes, and the maneuver cases' ensemble bands are wide (ADR-0028).
- **(Q13) One C++17 source compiled into both.** The equivalence would then test only the hosting, and
  `aircraft/` would gain an include direction into `embedded/` that ADR-0008 does not have.
- **(Q26) The DML's units inside the law.** Every gain would match the DML's text, but `aircraft/` would hold a
  customary-unit interior, an exception to ADR-0002.
- **(Q27) The rhumb line as the base course.** A constant course command holds one, but it departs from the
  decided initial-plane line by 2.184 m of cross-track by 60 s (the M7 plan's N-02). It is a reported variant.
- **(J8) The base point as `transport.peer` keys and the port's arguments.** It is explicit and stateless, but it
  duplicates the vehicle's initial position, and an edit to one that misses the other moves the base line
  silently.
- **(The rate frame) ICD v1's inertial rates,** the rule's default for "inconsistent". Joe rejected it on the
  evidence in Decision 7.

## Consequences and validation

- **Q9 (b).**
  - Every tool that refuses a `transport` block refuses the peer's configs too.
  - The peer's log cannot be replayed, because `astra.log.replay` refuses a `transport` block.
  - The peer runs through `Simulation`, from C++ or `astra._core`, and the NESC harness flies it.
  - The tutorial's closed loop runs through `astra.lockstep` with the port.
- **What the bitwise criterion requires of the port (amendment 8).** The port keeps the peer's floating-point
  expression order:
  - the same factors in the same order for each SI constant;
  - the same libm calls (`cos`, `atan2`, `sqrt`);
  - the same DCM operations for ω_ie_b (Decision 7).
  A difference in expression order is a porting defect, not a floating-point cause under Decision 8.
- **The M7 invariants cover the peer.** Two runs of one config give byte-identical logs, and the per-frame path
  allocates nothing (`tests/physics/no_alloc_test.cpp` gains a peer config).
- `icd/` is unchanged, and ADR-0023's two clock sites are unchanged.
- **What PR 11a carries:**
  - R11's and R22's fixtures, covering the ω_ie_b subtraction and the `LATLON_NOT_MODELED` refusal;
  - R11's floor and saturation-boundary margin;
  - R12's ρ;
  - the non-rotating-Earth question (Decision 7).
- **Acceptance,** per the M7 plan's PR table:
  - PR 11a: R6, R9 to R12, R22 and R23;
  - PR 11b: R7 and R9;
  - PR 12: R10, R11, R13 and R14.

## Approval

Pending Joe's decision.
