# M7 F-16 and the NESC oracle: spec, plan and PR decomposition

Status: **draft, awaiting Joe's decisions** (Q1 to Q25 below). Nothing in it is approved. The Codex plan
critique and its record are in [the critique record](m7-plan-critique.md).
Owner: Joe
Risk: 3 overall: `aircraft/**/physical`, `aircraft/**/hardware`, goldens and tolerances, ADR-0013's
oracle, `core/` interfaces (rotation consistency, envelope monitors), and the transport seam. This planning
PR is Tier 0 (docs only). Each PR's tier is in the decomposition table.
Intent/approval: PLAN.md's M7 row, mapped 2026-09-29 from [future work](../future-work.md). Joe's own
decision there: the NESC F-16 control laws run outside `embedded/` first, are shown to match the ensemble,
and are then ported to `embedded/`. Joe's planning instruction and his eleven amendments (2026-10-04) set
this plan's scope. Approval of the plan does not approve any later PR's diff.
Base: `origin/main` = `9c534c5` (#103, M6 closed out).
Architecture/ADRs:
- [architecture](../architecture.md): §7 (model interfaces), §9 (signals, the run log), §10 and §11 (the
  ICD, the transport, `embedded/`), §12 (`astra.trim`, the tools), §15 (health, envelope monitors, exit
  codes), §16 (an M7 row), §18 (the on-ramp the tutorial walks).
- [ADR-0001](../decisions/0001-frames-and-attitude.md), [ADR-0002](../decisions/0002-si-units-and-signal-metadata.md),
  [ADR-0006](../decisions/0006-icd-versioned-c11-structs.md), [ADR-0007](../decisions/0007-shared-memory-lockstep-futex.md),
  [ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md), [ADR-0009](../decisions/0009-plugin-architecture.md),
  [ADR-0010](../decisions/0010-integer-frame-counter-time.md).
- The oracle: [ADR-0013](../decisions/0013-nesc-check-case-oracle.md),
  [ADR-0014](../decisions/0014-nesc-per-channel-tolerance-floor.md),
  [ADR-0015](../decisions/0015-nesc-tol-rel-drop.md).
- The wrench and input seams: [ADR-0016](../decisions/0016-per-rk4-substage-wrench-evaluation.md),
  [ADR-0017](../decisions/0017-control-input-write-seam.md),
  [ADR-0020](../decisions/0020-aero-propulsion-true-interfaces.md),
  [ADR-0021](../decisions/0021-dynamics-model-owned-state.md).
- The exchange and the log: [ADR-0022](../decisions/0022-lockstep-exchange-stage.md),
  [ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md),
  [ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md),
  [ADR-0026](../decisions/0026-derived-flight-condition-signals.md),
  [ADR-0027](../decisions/0027-run-log-finalization.md).

Proposed records (Q20): ADR-0028 (the F-16 oracle, elaborating ADR-0013 to ADR-0015), ADR-0029 (sim-side
flight software as an in-process peer, and the port's equivalence criterion), ADR-0030 (ADR-0002's
identifier wording), and ADR-0031 (the envelope declaration) if Q16 keeps it in M7. A further record is
needed if Q5 picks core-published per-source wrenches.

System context: M7 is the first vehicle built from `aircraft/_template` by the §18 on-ramp, and the first
whole-aircraft external oracle. It consumes:
- the WGS-84 rotating Earth, J2 gravity and ISA-1976 atmosphere plugins (M2);
- the `AeroModel`/`PropulsionModel` interfaces and the write seam (M3);
- the exchange stage, the ICD v1 frames and the shm lockstep (M4);
- the run log and the tools (M5, M6).
It adds the F-16's models and a sim-side flight-software peer at the existing exchange stage. It adds a C++17
port of that peer in `embedded/`, eight band headers, and the harness that compares against them. Nothing
in M7 changes `icd/`.

## Outcome and boundaries

**Outcome.**
- `aircraft/f16` is built from the template by `just new-aircraft` and flies NESC atmospheric cases 11, 12,
  13.1 to 13.4, 15 and 16 inside bands derived from the participating simulations' own disagreement.
- The NESC control laws (`F16_control.dml` for 13.x, `F16_gnc.dml` for 15 and 16) run sim-side first,
  are banded against the ensemble, and are then ported to `embedded/`. The port is shown equivalent to
  the sim-side run.
- A user follows a tutorial, written from this path, from `just new-aircraft` to a closed-loop run, and
  looks up any plugin's config keys in a generated reference.
- Every M7-prefixed open item in PLAN.md is closed by a PR or left with a stated reason.

**Non-goals.**
- NESC case 17, the two-stage rocket (Joe, 2026-09-29); case 14 was never run by NESC (Vol II C.1.17).
- Sensors and ICD v2 (M9): the peer reads truth from ICD v1.
- Actuator dynamics (M8): NESC modeled none (Vol II p.16).
- Closed-loop Monte Carlo and closed-loop scenarios (M10).
- Any change to `icd/`, to an accepted ADR except by a superseding or elaborating record, or to a
  committed golden without Joe's `touch-goldens` authorization.

**Invariants.**
- M2's cases 1 to 10 keep their bands byte for byte (the generator's extension leaves `case01` to
  `case10` unchanged).
- M3's frozen regression and M4's lockstep acceptance pass unchanged.
- Two runs of one config give byte-identical logs (M5 R4), including a run with the in-process peer.
- The per-frame path allocates nothing (`tests/physics/no_alloc_test.cpp` gains an F-16 and peer
  config).
- `t = frame * dt` (ADR-0010). Units are SI inside (ADR-0002), and the NESC data's US customary units are
  converted in the generator only (ADR-0013).
- Tolerances come from the ensemble's own disagreement and are never fitted to ASTRA's output. k = 3 is
  unchanged.
- `embedded/` links only `icd/` and `embedded/hal/` and stays C++17 (ADR-0008).

**Assumptions.** These are checked by the PR named, not assumed silently:
- NASA's `altitudeMsl_ft` is height above the WGS-84 ellipsoid, as ASTRA's `wgs84_rotating` altitude
  is (PR 9 states the evidence).
- `bodyAngularRateWrtEi` is the inertial body rate, as in M2 (`nesc_cases_test.cpp`'s header).
- The DML's `bodyAngularRate_*` inputs are the same rates. PR 6a's inventory addendum reads the DML's
  variable definitions; if they are Earth-relative, the peer converts.

## Inventory of the sources (read-only, the main checkout's `docs/refs`, 2026-10-04)

**Trajectories.** Cases 11, 12, 13.1 to 13.4, 15 and 16 each have exactly three files: `sim_02`, `sim_04`
and `sim_05`. Vol II Table 79 names the same three. All sample at 10 Hz. Durations follow Vol II Tables 35
to 43, with three exceptions:

| Case | Vol II duration | `sim_02` | `sim_04` | `sim_05` |
|---|---|---|---|---|
| 11, 12 | 180 s (Tables 35, 36) | 0 to 200 s | 0 to 180 s | 0 to 180 s |
| 13.1, 13.2 | 20 s (Tables 37, 38) | 0 to 20 s | 0 to 20 s | 0 to 60 s |
| 13.3 | 30 s (Table 39) | 0 to 30 s | 0 to 30 s | 0 to 239.9 s |
| 13.4 | 60 s (Table 40) | 0 to 60 s | 0 to 60 s | 0 to 239.9 s |
| 15, 16 | 180 s (Tables 42, 43) | 0 to 180 s | 0 to 180 s | 0 to 180 s |

- `sim_05`'s times are float32. They sit up to 6.1e-6 s off k·0.1 (1.5e-6 s in 13.1 and 13.2), above
  ADR-0013's 1e-6 s matching rule.
- `sim_05` carries the column `feVelocity_ft_s_Z` twice. The two copies are equal in every row of all
  eight cases (checked this session).
- Columns: `sim_02` has 27, `sim_04` 32 and `sim_05` 38. Every channel of the generator's `CHANNELS` list
  is present in both `sim_04` and `sim_05`.

**NASA's own treatment** (Vol II Tables 79 and 80, doc p.593-594, footnotes):
- (a) "The first 8 sec for all F-16 test cases due to initial transients (all simulation tools) were not
  included."
- (b) "SIM 2 results are ignored for all F-16 check-cases (11 through 16 ...) due to large trim
  differences."
- (c) to (f) Tiny vertical-velocity differences in case 12, and mismatched transient timing of forces and
  moments between SIM 4 and 5 (13.1, 13.3, 13.4, 15).

NASA's F-16 comparison is therefore two members. Vol I §6.2.7 (doc p.13) set "a minimum of three data
sets" for inclusion; the F-16 cases meet it only by counting SIM 2.

**Why the members differ** (Vol II D.1.11 to D.1.18, doc p.228-339):
- *Trim angular rate.* SIM 2 holds zero inertial rate. SIM 4 holds the pitch rate that keeps pitch
  constant over the curved Earth. SIM 5 holds the three-axis rate that keeps all three Euler angles
  constant relative to the local vertical.
- *SIM 2's other differences.* It used a geocentric-vertical gravity simplification and reported aero
  moments at the MRC. In 13.2 it flew a larger airspeed step than the others.
- *SIM 4 in case 15.* It started untrimmed: elevator +1.09°, and a pitch acceleration of -44 deg/s².
- *The LQR references.* Only SIM 5 replaced the LQR's hard-coded reference states (angle of attack, pitch,
  airspeed) with its trim values. All three set `trimmedPilotControl_*` from trim.
- *Execution rate.* Possible differences in the control law's execution rate are named as a cause
  (D.1.15, D.1.16).

**The F-16 in Vol II** (B.1.5, doc p.14-30). Present:
- Table 7: S = 300 ft², b = 30 ft, c̄ = 11.32 ft; Ixx = 9,496, Iyy = 55,814, Izz = 63,100 and
  Izx = 982 slug·ft²; m = 637.26 slug; x̄ = 25 % MAC.
- The control laws' input and output signal tables (Tables 8 to 11): units, signs and ranges.
- Block diagrams, Figures 2 to 11. Their gains and limits are vector text that shows only when the page is
  rendered; Figure 7's eight LQR gains per axis were read this way.
- The prose values: the altitude gain -0.05 deg/ft limited to ±5°, and the bank command limited to ±30°.
- The initial conditions and commands (Tables 35 to 43).

Absent: every aero table, the propulsion model and the inertia file. Vol II says the `.dml` files are
"the normative description" and the diagrams only explain them (doc p.16, p.26).

**Model files.** None of the F-16 `.dml` files is in `docs/refs`. Vol I ref. 7 (doc p.26) and Vol II p.16
name the NESC flightsim repository (`nescacademy.nasa.gov/flightsim`) as their source; NTRS holds the
reports, not the model files. The model's upstream sources, Vol II refs [3] and [4], are NTRS reports.

**Prerequisites for Joe to place in `docs/refs`.** Each blocks the PR named. Nothing is transcribed from
memory, and the figures are a cross-check only (Q1).
- **P1.** The F-16 aero, propulsion and inertia DAVE-ML files, as published in the NESC flightsim
  repository. Vol II does not name them. Blocks PR 6a and PR 6b.
- **P2.** `F16_control.dml` (blocks PR 11a) and `F16_gnc.dml` (blocks PR 11b), same source.
- **P3** (optional, NTRS). Cross-check sources, used only if P1 carries no `checkData`:
  - Garza & Morelli, *A Collection of Nonlinear Aircraft Simulations in MATLAB*, NASA TM-2003-212145;
  - Nguyen et al., NASA TP-1538 (1979).

## Repository findings (observed at `9c534c5`)

- **Trim.** `astra.trim.trim_straight_and_level` (`python/astra/trim.py:130`) solves elevator, throttle
  and alpha. It does so with ψ = φ = β = 0 and ω_b = 0 (inertial), which is SIM 2's convention, the one
  NASA rejected. It refuses `earth/flat` (`:152`), which is the §18 step 4 item. It writes the literal
  names `controls.elevator` and `controls.throttle` (`:170`), which is the trim-names item.
- **Aero channels.**
  - `nesc_cases_test.cpp` compares `v0.wrench_total.*`, the core's frame-start-stage sum of every
    `WrenchSource` (`core/src/kernel/vehicle.cpp:104`). For the F-16 that sum includes thrust, and NESC's
    `aero_body*` columns are aero only.
  - A plugin cannot publish its own wrench from `wrench_b()`, which runs four times per frame. The purity
    contract forbids hidden per-call state, which is why `v0.nesc_aero.*` was removed
    (`nesc_cases_test.cpp`'s header).
  - The same harness already evaluates an independently loaded aero instance at each sample's frame-start
    state and environment (R12 of M2 PR 3c).
- **Transport refusals.** Every one of these refuses a config with a `transport` block:
  - `astra run` and `astra trim` (`cli.py:465`, `:617`, exit 65 until M10);
  - `astra.montecarlo` (`montecarlo.py:425`) and `astra.scenario` (`scenario.py:302`);
  - `astra.log.replay` (`log.py:244`).
  `astra.lockstep` accepts only `plugins/sim/transport/shm` (`lockstep.py:261`). So an in-process peer
  configured as a transport runs today only through `Simulation` itself: C++, or `astra._core` from
  Python.
- **The ICD.**
  - ICD v1 (`icd/astra_icd.h`) carries `tas`, `cas` and `mach`, but no EAS. EAS is exact from those three
    when subsonic: invert `icd_pack::calibrated_airspeed` (`core/include/astra/util/icd_pack.hpp:52`) for
    the static pressure p, then EAS = a0·M·√(p/p0). This uses the same a0 and p0 the packer uses, with
    no density and no altitude model. No ICD change is needed for the law.
  - v1's `aileron` is positive right wing down. Vol II Table 9's `aileronDeflection` is +LWD, so the sign
    flips. Elevator (+TED) and rudder (+TEL) match ICD v1.
- **The exchange.** The command map stages ICD fields into named inputs with no scaling
  (`lockstep_exchange.hpp`). `throttle[2]` must hold two equal values (architecture §15), which applies to
  a single-engine aircraft too. The loopback transport is the precedent for an in-process peer built from
  config (`plugins/sim/transport/loopback/loopback.cpp`).
- **The generator.** `tests/golden/nesc/generate_nesc_band.py`:
  - assumes 301 samples over 30 s (`SAMPLE_COUNT`) and a 1e-6 s matching tolerance;
  - wraps yaw only;
  - excludes participants by Table 78's footnotes;
  - emits one scalar `tol_abs` per channel and case.
  `nesc_band_common.hpp` fixes `kSampleCount`. Cases 15 and 16 cross ±180° longitude, so longitude
  needs the same wrap as yaw.
- **`mass/constant`.** It places `ixz` unnegated in the tensor (the `ixy` convention open item). The template's
  own mass plugin uses the product-of-inertia convention, and Table 7's Izx = 982 slug·ft² is a product of
  inertia.
- **The core API.** `kCoreApiVersion` is 9 (`core/include/astra/core_api.hpp.in:51`). The next ADR
  number is 0028.

## Measured band preview (amendment 7)

A scratch script read the CSVs read-only. It is not the guarded generator and is not committed. For each
channel and case it computed ADR-0013's band on SIM 4 and SIM 5: `tol = 3 · max_t |sim4 − sim5|`, with
yaw and longitude on the wrapped difference, at t ≥ 8 s, in SI. "Zero-accepting" is ADR-0015's
`accepts_zero` test: the band is at least the reference's peak magnitude, so a trajectory of zeros
passes. Such a channel is reported but is no evidence. Every other channel can fail the R that gates its
case: R5 for cases 11 and 12, R6 for 13.x, R7 for 15 and 16.

| Case | Selected bands (SI) | Zero-accepting channels |
|---|---|---|
| 11 | altitude 0.140 m; v_north 0.015, v_east 0.024 m/s; pitch 1.8e-5 rad; rate_q 3.2e-7 rad/s; force_x 1.84 N, force_z 0.48 N | v_down, force_y, moment_l, moment_m, moment_n |
| 12 | altitude 0.93 m; v_north 0.057 m/s; pitch 5.4e-5 rad; force_x 6.9 N, force_z 4.7 N | v_down, moment_l, moment_m, moment_n |
| 13.1 | altitude 0.23 m; v_down 0.021 m/s; pitch 2.2e-4 rad; rate_q 2.5e-4 rad/s; force_x 482 N, force_z 2,411 N | force_y, moment_l, moment_m, moment_n |
| 13.2 | altitude 0.15 m; v_north 0.073 m/s; pitch 1.0e-4 rad; force_x 27.9 N, force_z 73 N | moment_l, moment_m, moment_n |
| 13.3 | altitude 0.58 m; yaw 0.041 rad; roll 0.042 rad; rate_p 1.25, rate_q 0.122 rad/s; force_y 2.1e4 N, force_z 6.26e4 N; moment_l 1.3e5 N·m | moment_m |
| 13.4 | altitude 0.60 m; yaw 0.045 rad; rate_q 0.139 rad/s; force_z 6.24e4 N; moment_l 2.1e5 N·m | moment_m |
| 15 | altitude 0.46 m; longitude 3.3e-3 rad; roll 0.020 rad; rate_q 3.3e-3 rad/s; force_z 1,361 N | rate_p, force_y, moment_l, moment_m, moment_n |
| 16 | altitude 0.45 m; latitude 1.5e-6 rad; roll 0.027 rad; rate_q 4.3e-3 rad/s; force_z 1,741 N | rate_p, force_y, moment_l, moment_m, moment_n |

What the preview shows:
- **The trim cases discriminate sharply.**
  - Case 11's altitude band is 0.14 m (±0.46 ft) over 180 s, and its force_z band is 0.48 N against a lift
    of about 9.1e4 N.
  - A trim residual, a gravity or Earth-rate error, or a mass or inertia slip would leave these bands.
  - The near-zero lateral channels (force_y, the moments) are zero-accepting. They test nothing beyond
    "small".
- **The maneuver cases' force and moment bands are loose.**
  - In 13.3 and 13.4 the worst spread falls at the command step (t ≈ 15.5 s and 20.5 s): footnote (e)'s
    transient timing.
  - With one scalar per channel, force_z's band of 6.26e4 N (about ±14,000 lbf) is about 69 % of the
    aircraft's weight (about 9.1e4 N, 20,500 lb).
  - The pitching moment is zero-accepting.
  - The kinematic channels (altitude, velocities, attitude) still discriminate.
- **The window matters.**
  - In case 15 the full window raises force_z's band to 3.69e4 N (SIM 4's untrimmed start); from t ≥ 8 s
    it is 1,361 N.
  - In 13.1 a window from t ≥ 2 s takes in the 5 s step's transient, and force_z's band becomes
    1.36e5 N, above the aircraft's weight.

These numbers motivate Q3 and Q4. The generator in PR 8 recomputes them; the preview never becomes a
golden.

## Contract and failure behavior

**The oracle (ADR-0028).**
- Per case, the band is ADR-0013's with ADR-0014's floors and ADR-0015's absolute-only criterion,
  computed over the members and window Q2 and Q3 choose.
- Each gated sample of ASTRA's trajectory must lie within it.
- The generator gains:
  - a per-case duration (Vol II's table, extra rows ignored);
  - a per-case gated window;
  - a matching tolerance of 1e-5 s, the float32 bound: half an ulp of a float32 below 256 s is at most
    7.6e-6 s;
  - a longitude wrap;
  - a check that duplicate columns agree;
  - a check that SIM 2 is excluded by rule.
- It still reports every channel's band against its floor and every zero-accepting channel.
- `nesc_band_common.hpp` carries each case's own sample count and first gated sample.
- Cases 1 to 10 regenerate byte-identical.
- The ungated span (0 to 8 s, Q3) is still sampled and reported in the test log, never asserted.

**The aero channels (Q5).** NESC's `aero_body*` columns are aero only and about the CM. The harness
compares an aero-only, frame-start-stage wrench obtained by Q5's route, never `wrench_total`.

**Trim (Q6, Q7).** `astra.trim` gains three things:
- an arbitrary heading;
- the rate convention Q6 chooses;
- any Earth model (flat altitude is −p_n[2]).
Control names come from `InputRegistry::names()`, bound to Python. The unknowns, the residuals and their
thresholds per case are Q7's. A trim that does not converge, or misses a threshold, raises naming the
residual. It never returns a state silently out of tolerance.

**The sim-side law (Q8 to Q12).**
- The peer runs the DML law on the ICD v1 frame the exchange stage packs, at the exchange divisor (Q12).
  It returns `astra_sw_to_sim_v1`.
- It derives EAS from `tas`, `cas` and `mach` as above.
- It converts its outputs once, at its own boundary:
  - degrees to radians;
  - Table 9's +LWD aileron to ICD v1's positive right wing down;
  - PLA (0 to 100 %) to throttle (0 to 1), written to both `throttle` entries.
- Its command schedule (the initial commands, and each case's step at its time) is read from the peer's
  config keys, keyed to frame time (Q10).
- 13.4's lateral-deviation logic is part of the peer (Vol II p.65: "user-supplied logic").
- A command outside the law's own limits is limited by the law as the DML specifies. The exchange's
  configured limits then halt on anything still outside them (§15), never clamping.

**The port (Q13).** A C++17 program in `embedded/` runs the same law against the same ICD v1 frames over
the shm transport, through `astra.lockstep`. It takes the same schedule as arguments keyed to frame time,
and its equivalence to the sim-side run is Q13's criterion.

**Aero tables (Q14, Q15; provisional until P1).**
- The F-16 aero and propulsion plugins reproduce the DML's tables, its build-up equations, and each
  table's DAVE-ML extrapolation semantics at the table edges.
- Case 12 at Mach about 2 exercises those edges: the S&L thrust tables stop at Mach 1 (S&L, 3rd ed.,
  in `docs/refs/textbooks`).
- PR 6a's inventory addendum records what P1 actually contains before PR 6b's plan is written: tables
  and breakpoints, the MathML build-up, whether `checkData` is present, engine dynamics and gyroscopic
  terms, Mach dependence, and each table's extrapolation.

**Envelope monitors (Q16).** If in M7:
- `AeroModel` declares α, β, Mach, dynamic-pressure and load-factor limits through a new virtual with a
  safe default (none declared). This bumps `kCoreApiVersion`.
- `HealthMonitor` checks them each frame with the configured action, `warn` by default (§15). A
  plausible-looking run outside the aero model's data is then visible.
- The F-16 declares its table domain; the notional transport declares `alpha_max_rad = 0.23`.

**Rotation consistency (Q17).** At init, `Simulation::from_config` refuses an Earth model and a gravity
model that disagree on rotation, in both directions, naming both plugin paths. The check mirrors the existing
`assumed_position_representation()` check.

**Config-key reference (Q21).** A generated `docs/reference/config-keys.md` lists every plugin's keys:
name, type, unit, default, required, and meaning. The generator reads a structured block per plugin. The
gate fails when the file is stale or when a key a tracked config consumes is undocumented.

**Tutorial (Q22).** `docs/tutorial.md` walks §18 from the F-16:
- `just new-aircraft`, the models and `just build`;
- `astra trim` and `astra run` on case 11's open-loop config;
- a scenario and an open-loop Monte Carlo;
- the closed loop through the embedded port and `astra.lockstep`.
A test runs its commands on a throwaway copy.

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | F-16 mass properties | Unit test: the mass plugin's SI values equal Table 7 (and P1's inertia file, if it differs, reported) through exact conversions (Vol II Table 73's foot and slug constants); Izx's sign is pinned under the product-of-inertia convention |
| R2 | Aero model equals its source | The compiled plugin, driven through `wrench_b`, reproduces the DML's `checkData` within the tolerance the DML states, or P3 if there is none (Q14). The `checkData` extraction shares no code path with the table emitter (lesson 2026-09-25). Points beyond every table's breakpoints follow that table's DAVE-ML extrapolation semantics |
| R3 | Propulsion equals its source | As R2. Throttle 0..1 maps to PLA 0..100 % in one place, pinned by a test at 0, 0.5 (MIL) and 1 (MAX). Engine states, if any, follow ADR-0021 |
| R4 | Trim | For each case's initial condition (Q7's table), the solved residuals, recomputed through `Simulation.derivative`, are within Q7's threshold, and the reported residuals are within their stated bounds. ω_b at the solution equals an independent n-frame-rate computation (Q6). An arbitrary heading, flat Earth (the template copy) and names from `InputRegistry::names()` are each seen to work. `astra trim` on a fresh template copy no longer exits 65 for its Earth model |
| R5 | Cases 11, 12 open loop | Every non-excluded channel within ADR-0028's band at every gated 10 Hz sample, aero channels via Q5's route; the ungated span is logged |
| R6 | Cases 13.1 to 13.4, sim-side `F16_control` | As R5, with the peer flying Vol II Tables 37 to 40's schedules |
| R7 | Cases 15, 16, sim-side `F16_gnc` | As R5, yaw and longitude wrapped; latitude near 90° and the ±180° crossings in both directions (Vol II C.1.18, C.1.19) |
| R8 | Discrimination is reported | The generator lists each case's zero-accepting channels and every band's ratio to its floor; ADR-0028 records them. A zero-accepting channel is never cited as evidence |
| R9 | Negative controls (Q24) | Each planted defect fails at least one discriminating band of each case family it touches: Izx's sign (11, 13.x), the elevator's sign (11, 13.x), the aileron's sign at the law boundary (13.3, 13.4, 15, 16), SIM 2's zero-inertial-rate trim (11, 12, 16), and the law at the wrong divisor (13.x), unless Q12's measurement shows that divisor within the band, in which case that is recorded instead |
| R10 | Command schedules agree | The peer's config schedule and the port's arguments give the same command timeline, frame for frame, in a test comparing both against Vol II Tables 37 to 43 |
| R11 | Boundary conversions | Unit tests of the peer and of the port: a hand-set law output in degrees and PLA percent gives the expected ICD radians (aileron sign flipped) and throttle pair |
| R12 | EAS from ICD v1 | Over each case's flight condition range, the peer's EAS from `tas`, `cas` and `mach` equals TAS·√(ρ/ρ0) from the sim's own atmosphere signals to 1e-12 relative |
| R13 | Port equivalence (Q13) | For 13.1 to 13.4, 15 and 16: the `sw_to_sim` stream (every exchange) and every state and physics signal column of the two HDF5 logs are bit-identical. Excluded and named: the stored config text, `/plugins` and its SHA-256 values, the `transport.*` signals, and any provenance attribute that names the transport |
| R14 | The port's boundary | `just embedded-standalone` and `scripts/embedded_deps_check.py` pass with the port added; it compiles as C++17 and links only `icd/` and `embedded/hal/` |
| R15 | Rotation consistency | Both mismatch directions (`earth/round rotating: true` with a non-rotating `inverse_square`; `wgs84_j2` with a non-rotating Earth) are refused at init with a `ConfigError` naming both paths; every tracked config still builds |
| R16 | Config cycles | The three reproducers in PLAN.md's item give a `ConfigError` naming the key, `astra run` exits 65, and nothing segfaults |
| R17 | `mass/constant`'s convention | The keys' convention is documented in the plugin and in the config-key reference, and a test pins the tensor entry's sign for a positive `ixz` |
| R18 | Envelope monitors (if Q16 in M7) | A test drives α, β, Mach, q̄ and n past declared limits and sees each check fire with its action and source; the default is `warn`; M6's R9 batch is unchanged under `warn` |
| R19 | Config-key reference | The generated file is current (docs-check), and every key consumed by every tracked config is documented (pytest via `Config::unknown_keys`'s complement) |
| R20 | Tutorial | A pytest runs the tutorial's command blocks on a throwaway `just new-aircraft` copy and checks each step's stated outcome |
| R21 | Records and gate | ADR-0028 to ADR-0030 (and 0031 if Q16) accepted by Joe; `./scripts/check-full.sh` exit 0 with `check: OK` per PR except Tier 0; review per tier |

## Open questions

Each has a recommendation. "ADR" means the answer is recorded in an ADR; "plan" means Joe's decision here
is enough. Blocks: the first PR that cannot start without it.

- **Q1. Prerequisites (blocks PR 6a, 11a, 11b).** Joe places P1 and P2 from the NESC flightsim
  repository, and P3 from NTRS if wanted. Recommend that the figures serve only as a cross-check of P2,
  never as its replacement, since Vol II names the `.dml` files normative.
- **Q2. Ensemble members (ADR-0028; blocks PR 1).** Recommend honoring footnote (b): the bands come from
  SIM 4 and SIM 5 only, and SIM 2's distance from the band is reported, never gated. This is the
  comparison NASA itself made. Alternatives:
  - all three members, which takes in SIM 2's documented gravity and trim differences and makes every band
    wider;
  - a per-channel SIM 2 list, the shape ADR-0015 rejected because it makes references inconsistent within
    a case.
  Disclosed: two members fall below Vol I's three-data-set ground rule. The band is k times one pairwise
  difference, so a channel where SIM 4 and SIM 5 happen to agree is tighter than three tools might give.
- **Q3. The gated window and generator mechanics (ADR-0028; blocks PR 1).** Recommend honoring footnote
  (a): gate t ≥ 8 s in every case and report 0 to 8 s.
  - Disclosed: 13.1's and 13.2's commands fire at t = 5 s, inside the ungated span, so the gate sees only
    the tail of their response (8 to 20 s). The response's onset is checked by R13's equivalence only.
  - Alternative, decided per case family: gate from the end of the start-up oscillation (t ≥ 2 s, Vol II
    D.1.11's first-second oscillation). That gates 13.1's and 13.2's onset, at the cost of wider force
    and moment bands: 13.1's force_z goes from 2,411 N to 1.36e5 N.
  - The mechanics, recommended as described in the contract: a 1e-5 s matching tolerance (the float32
    half-ulp bound), per-case durations, a longitude wrap, the duplicate-column check, and k = 3
    unchanged.
- **Q4. The band's shape for the maneuver cases (ADR-0028; blocks PR 1).** Recommend ADR-0013's scalar
  per channel and case, unchanged in every case. The preview shows the cost: in 13.3 and 13.4 the step's
  transient timing sets the force and moment bands, so those channels test little, while the kinematic
  channels still discriminate. Alternative: a per-sample band, `tol(t) = max(k · spread(t), floor_c)`,
  for the maneuver cases (13.x, 15, 16), with k unchanged.
  - Its gain: away from the step, force_z's band would follow the members' agreement there.
  - Its cost: a new judgment number, the floor, and a band that is tightest where the members happen to
    cross.
  ADR-0013 rejected per-time tightening only implicitly; this would be an explicit elaboration.
- **Q5. The aero-only channels (blocks PR 7; ADR if the core option).** Recommend option (c), an
  independent aero evaluator in the harness. The harness loads a second instance of the aero plugin, binds
  its inputs to an `InputRegistry` of its own, writes the sim's latched `v0.controls.*` values into it
  before each sample, and calls `wrench_b(pre_step_state, env)`. This is M2 PR 3c's R12 pattern, already
  in `nesc_cases_test.cpp`. It needs no core change and is Tier 3 for the test tree. A self-check guards
  it: on a propulsion-free config, its result equals `wrench_total` to 1e-12. Alternatives:
  - (a) The core publishes each `WrenchSource`'s frame-start-stage wrench under a per-source name. This is
    Tier 3 core work with §9 text or an ADR. Every log of every config gains columns, so tests that pin a
    log's column set change. No interface changes, so no `kCoreApiVersion` bump is expected, but per-source
    naming must be stable with two instances of one plugin.
  - (b) The harness subtracts a separately evaluated propulsion wrench from `wrench_total`. It is as cheap
    as (c), but the aero result is then a difference of two large numbers.
  (a) is the only option a user's log shows; the recommendation leaves it as a later item.
- **Q6. Trim's rate convention (plan; blocks PR 5).** Recommend SIM 5's: ω_b is the n frame's own rate
  (Earth rate plus transport rate, in body axes), so all three Euler angles hold relative to the local
  vertical. It is the most complete of the three and needs no special case at the pole. Alternatives:
  - SIM 4's pitch-rate-only rate;
  - today's zero inertial rate, SIM 2's, which NASA's footnote (b) attributes to "large trim
    differences".
- **Q7. Trim unknowns and thresholds per case (plan; blocks PR 5).** Vol II C.1.11: the participants
  varied "pitch attitude, elevator position, and throttle setting" only. Recommend those three unknowns,
  at the case's heading with φ = β = 0 and aileron = rudder = 0:
  - The three solved residuals (u̇, ẇ, q̇) must be at most 1e-6 (m/s², rad/s²), as M3's D8.
  - The three lateral residuals (v̇, ṗ, ṙ) do not vanish by symmetry at ψ = 45° and 36° N. They are
    reported, with a stated bound of 1e-4 (m/s², rad/s²) for cases 11, 12 and 16.
  - Case 16 is at the equator heading north, where the Earth rate lies along the body x axis.
  - Case 15 (89.95° N, ψ = 90°) is the exception. Holding the local-vertical attitude there needs a yaw rate
    of about 0.031 rad/s (v_e·tan φ / R), which induces lateral forces and moments. Vol II D.1.17 reports
    SIM 5's as -63 lbf, -226 lbf·ft and 786 lbf·ft with zero aileron and rudder. The autopilot engages at
    the first frame and absorbs them. So case 15's lateral residuals are reported and checked only against
    SIM 5's reported values within a stated factor, not bounded at 1e-4.
  Alternative: a six-unknown solve (adding φ, aileron and rudder) that zeroes all six residuals in every
  case. It is more complete, but it is not what any member did.
- **Q8. Where the sim-side law lives (ADR-0029; blocks PR 11a).** Recommend an in-process peer at the
  existing exchange stage: a transport plugin `aircraft/f16/sim/transport/nesc_control`, which runs the
  law on the packed ICD v1 frame. Why:
  - It needs no core change.
  - It sees exactly what the port will see, at the same exchange and through the same validation and
    staging, which is what makes Q13's bitwise criterion possible.
  - The loopback transport is its precedent.
  Consequence: every tool that refuses a `transport` block refuses it too (Q9). The peer is run by the
  harness through `Simulation`, in C++ or from `astra._core`, and its log cannot be replayed (`log.py:244`).
  Alternatives:
  - A new core plugin kind for controllers. Tier 3 core work and an ADR; it overlaps M8's actuator seam
    and ADR-0017's undecided non-`WrenchSource` writer.
  - A Python driver writing through `write_input`. The tools accept it and replay works. But it reads
    signals published for the previous frame, so a one-frame timing difference from the port must be
    designed out, and the law is written in Python and then ported again.
  - A C++ test harness. It is cheap, but no user can run it.
- **Q9. The tools' transport refusal (plan; adds PR 10 only under (a)).** Recommend (b): the in-process
  peer stays a verification instrument, and the tutorial's closed loop runs through `astra.lockstep` with
  the embedded port. That is §18 step 5's own story, and it leaves the M10 refusals as they are.
  Alternative (a): a PR that narrows the refusal to transports needing a peer process. It touches
  `cli.py` (`run`, `trim`), `montecarlo.py`, `scenario.py` and possibly `log.py`. It needs a way to tell
  the two apart: a path allowlist in Python (Tier 2), or a `Transport` declaration (core, Tier 3, an ADR
  and a `kCoreApiVersion` bump).
- **Q10. Where the command schedules live (ADR-0029; blocks PR 11a).** The 13.x commands are inputs to
  the law, not sim inputs:
  - +100 ft at 5 s;
  - −5 KEAS at 5 s;
  - +15° course at 15 s;
  - a 2,000 ft offset at 20 s;
  - the initial command values.
  `astra.scenario` events write only sim inputs, and ICD v1 carries no command field. Recommend:
  - the peer reads its schedule from `transport.peer` config keys, each step keyed to a frame;
  - the port takes the same schedule as arguments keyed to frames;
  - R10 checks both against Vol II's tables.
  Alternative: an ICD `_v2` command message. Rejected for M7: ICD v2 is M9's.
- **Q11. The LQR reference states (plan; blocks PR 11a).** Recommend the DML as written: its hard-coded
  reference angle of attack, pitch and airspeed, with `trimmedPilotControl_long` and
  `trimmedPilotControl_throttle` set from ASTRA's trim, as Vol II p.16 recommends. That is SIM 4's
  choice; SIM 5 alone replaced the references with its trim values.
  - The DML is normative, and the port must reproduce the law as published.
  - Expected effect: with Q6's trim convention (SIM 5's), ASTRA's trim and LQR are a hybrid of the two
    members. Vol II D.1.13 attributes to this choice an immediate small airspeed change at engagement in
    SIM 4, and about 2.5 ft of the 13.1 position difference at 20 s.
  - 13.1's and 13.2's velocity bands are 0.07 m/s, so the choice can decide R6.
  - PR 11a flies both choices before asking Joe to confirm, and reports each against the bands.
  Alternative: SIM 5's replacement, which tracks SIM 5 and departs from the normative file.
- **Q12. The law's execution rate (ADR-0029; blocks PR 11a).** Recommend the DML's rate if it states one.
  Otherwise the exchange divisor 10 (100 Hz, the architecture's default), with the divisor-1 run measured
  against the bands and reported. Vol II names execution-rate differences as a possible cause (D.1.15,
  D.1.16).
- **Q13. The port's equivalence criterion (ADR-0029; blocks PR 12).** Recommend bitwise equality against
  the sim-side run, for the objects R13 names:
  - the `sw_to_sim` stream;
  - every state and physics column of the HDF5 log.
  The excluded datasets differ by construction: the config text, the plugin table and its hashes, and the
  `transport.*` signals. The inputs are identical by Q8's construction. Both builds use clang-18 with the
  same floating-point flags (no `-ffast-math`, the same `-ffp-contract`), so equality is the expected
  result, and any difference is a porting defect until shown otherwise. If a stated floating-point cause
  makes bitwise infeasible, it comes back to Joe. Alternatives:
  - a band against the sim-side run;
  - a band against the ensemble.
  Each is weaker: a porting defect smaller than the band passes, and the ensemble's maneuver bands (Q4) are
  wide.
- **Q14. Aero tables (plan, provisional until PR 6a; blocks PR 6b).** Recommend an offline stdlib-Python
  generator, using `xml.etree` with no new dependency, that reads P1 at authoring time:
  - It emits committed C++ tables under `aircraft/f16/physical/aero` with the source's SHA-256.
  - The build-up equations are hand-ported.
  - The interpolation, with DAVE-ML's extrapolation semantics, is plugin-local.
  - R2's oracle is the DML's own `checkData`, extracted by a separate reader.
  Alternatives:
  - hand-porting the tables (transcription risk on thousands of numbers);
  - a generic runtime DAVE-ML plugin under `plugins/physical`: a new XML dependency (Joe's decision), a
    MathML evaluator, an ADR and Tier 3 core-adjacent scope, for one consumer.
- **Q15. Case 12 and the table edges (plan, provisional until PR 6a).** Case 12 is flown uncontrolled at
  Mach about 2, which NESC flew with the same model. The plugin reproduces each table's DAVE-ML
  extrapolation at the edges, since S&L's thrust tables stop at Mach 1. The final text of this question
  waits for PR 6a's addendum: whether the aero has any Mach dependence, and what the propulsion tables do
  beyond their last Mach breakpoint.
- **Q16. §15 envelope monitors (ADR-0031; plan).** Recommend inside M7, as a late PR of its own: Tier 3,
  a `kCoreApiVersion` bump, default `warn`. The F-16's table domain gives it real limits, and an F-16
  leaving its data is exactly the run a user should be warned about. R9 of M6 is unchanged under `warn`.
  Alternative: after M7, leaving the item open with that reason.
- **Q17. Earth/gravity rotation consistency (plan).** Recommend inside M7, as an early Tier 3 core PR with
  its own plan review, as the item asks. The F-16's configs pair `wgs84_rotating` with `wgs84_j2`
  consistently, so no F-16 PR depends on it. Alternative: after M7.
- **Q18. §18 step 4 (plan).** Recommend fixing it through Q6's trim (flat altitude is −p_n[2]), so the
  template works as shipped. Alternative: switch the template's config to `wgs84_rotating` with
  `wgs84_j2`.
- **Q19. The other M7 items (plan).**
  - `Config::unknown_keys` and `from_dict` cycle safety: Tier 2 (`core/config` is not a kernel or
    numerical path), with Joe free to raise it.
  - `mass/constant`'s product-of-inertia convention: documented and pinned by a test (Tier 3,
    `plugins/physical`).
  - ADR-0002's wording: Q20.
  Recommend all three in M7.
- **Q20. ADRs M7 writes (blocks PR 1).**
  - ADR-0028: the F-16 oracle (Q2 to Q4), elaborating ADR-0013 to ADR-0015.
  - ADR-0029: sim-side flight software as an in-process peer, its schedule and rate, and the port's
    equivalence criterion (Q8, Q10, Q12, Q13).
  - ADR-0030: ADR-0002's identifier wording. Recommend: the no-unit-suffix rule governs signal names,
    while C++ identifiers and config keys may carry a unit suffix, as `wing_area_m2`, `AtmoState`'s fields
    and `alpha_max_rad` already do. It supersedes only that sentence of ADR-0002.
  - ADR-0031: the envelope declaration (if Q16 keeps it in M7).
  - A record for Q5 (a) or Q9 (a) only if chosen.
  No ADR for trim, rotation consistency (it mirrors an existing check), the table generator or the key
  reference; plan and architecture text only.
- **Q21. The config-key reference (plan; blocks PR 14).** Recommend:
  - a structured key block in each plugin's header comment;
  - `scripts/config_keys.py` generating `docs/reference/config-keys.md`;
  - `just docs-check` failing when the file is stale;
  - a pytest that every key a tracked config consumes is documented.
  Alternative: a key table in `PluginDescriptor`. It is a typed source, but a core API change and a
  `kCoreApiVersion` bump.
- **Q22. The tutorial (plan; blocks PR 15).** Recommend `docs/tutorial.md` from the F-16 path, as in the
  contract, with R20's test. Alternative: prose only, untested (it rots).
- **Q23. Gate scope (plan).** Recommend:
  - all eight cases sim-side in `just check`. At 180,000 frames the long cases are measured per PR (the
    notional transport runs about 2 µs per frame in Release, M6 PR 9), with ASan the cost to watch.
  - the port's lockstep runs: PR 12 measures them and proposes which run in the gate, as M4's acceptance
    does.
- **Q24. Negative controls (plan).** Recommend R9's list. Each defect is applied through a test-only
  config or build variant, never by editing the shipped plugin. A defect that does not fail the expected
  family is reported to Joe, not dropped.
- **Q25. This plan's own review (plan).** Recommend that the one fresh Codex critique stands as this Tier
  0 PR's Codex pass, its findings folded in. A verification pass on the final diff, as M6 ran, is Joe's
  call.

## PR decomposition

One Claude builder, sequential unless Joe assigns a parallel worktree. PR 2, 3 and 4 are independent of
the F-16. Each PR opens as a draft, gets its tier's review, and waits for Joe's merge. Every PR but Tier 0
runs `./scripts/check-full.sh` (exit 0 and `check: OK`) and commits a trimmed gate summary.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance commands (beyond the full gate) |
|---|---|---|---|---|---|
| 0 | This planning PR: the spec and plan, the critique record, PLAN.md's Planning line, architecture §16's M7 row | 0 | 0013 to 0015 | none | `python3 scripts/docs_check.py`; `git diff --check origin/main..HEAD`; the Codex plan critique |
| 1 | ADR-0028, 0029, 0030 (and 0031 if Q16, a record for Q5 (a) or Q9 (a) if chosen); architecture rev 11 (§7, §10, §11, §12, §15, §16, §18); PLAN.md M7 PR lines | 3 | 0002, 0006, 0007, 0013 to 0015, 0017, 0022, 0024 | Q2 to Q4, Q8, Q10, Q12, Q13, Q16, Q20 | `python3 scripts/docs_check.py`; adversarial Codex review of the ADRs; Joe accepts them |
| 2 | Config cycle safety: `unknown_keys` and `value_to_yaml` cycle-safe; the three reproducers; `_yaml_route`'s guard kept or removed | 2 | 0025 | Q19 | `ctest --preset asan -R Config --no-tests=error`; `.venv/bin/pytest tests/python -k 'pyyaml_route or cli'` (R16) |
| 3 | Earth/gravity rotation consistency at init, both directions | 3 | 0003, 0021 | Q17; its own plan review | `ctest --preset asan -R 'Simulation\|Gravity\|Earth' --no-tests=error`; `ctest --preset release -R Nesc --no-tests=error` (R15) |
| 4 | `mass/constant`'s product-of-inertia convention, documented and pinned | 3 | 0001, 0002 | Q19 | `ctest --preset asan -R Mass --no-tests=error` (R17) |
| 5 | Trim: heading, Q6's rate convention, Q7's unknowns and thresholds, flat Earth, names from `InputRegistry::names()` (bound) | 2 | 0001, 0017, 0021 | Q6, Q7, Q18 | `.venv/bin/pytest tests/python -k 'trim or linearize or cli'` (R4, less its F-16 cases) |
| 6a | Inventory addendum to this spec from P1 and P2: tables, breakpoints, extrapolation, `checkData`, engine dynamics, gyroscopic terms, Mach dependence; Q14 and Q15 finalized | 0 | none | P1, P2 | `python3 scripts/docs_check.py`; Joe confirms Q14 and Q15 |
| 6b | `aircraft/f16` from the template: mass, the table generator and generated aero tables, propulsion; unit tests on `checkData` | 3 | 0001, 0002, 0016, 0017, 0020, 0021 | PR 4, PR 6a, PR 1 (ADR-0030) | `ctest --preset asan -R F16 --no-tests=error`; `.venv/bin/pytest tests/python -k new_aircraft` (R1 to R3) |
| 7 | The aero-only channel route (Q5): under (c), the harness's independent aero evaluator and its self-check on the M2 cases | 3 | 0013, 0016, 0020 | Q5 (and its record under (a)) | `ctest --preset release -R Nesc --no-tests=error` (M2's cases unchanged; the self-check) |
| 8 | Generator extension and band headers for cases 11 to 16; `nesc_band_common.hpp` per-case sample counts; cases 1 to 10 byte-identical. Joe arms `touch-goldens` and runs the generator (ADR-0013) | 3 | 0013 to 0015, 0028 | PR 1 | `python3 tests/golden/nesc/generate_nesc_band.py` (by Joe; byte-identical `case01` to `case10`); `ctest --preset release -R Nesc --no-tests=error` (R8) |
| 9 | Cases 11 and 12 open loop: configs, trim to Vol II's initial conditions, aero channels via PR 7, negative controls for this family. Not dependent on PR 3: the F-16 configs pair `wgs84_rotating` with `wgs84_j2` consistently | 3 | 0013 to 0016, 0028 | PR 5, 6b, 7, 8 | `ctest --preset release -R 'Nesc.*Case1[12]' --no-tests=error`; `ctest --preset asan -R NoAlloc --no-tests=error` (R4, R5, R9) |
| 10 | (Only if Q9 (a).) Narrow the `transport` refusal to peer-process transports in `astra run`, `trim`, `montecarlo`, `scenario` (and `replay` if chosen) | 2 or 3 | 0022, 0025 (and Q9's record) | Q9 | `.venv/bin/pytest tests/python -k 'cli or montecarlo or scenario or replay'` |
| 11a | Sim-side `F16_control` peer: EAS, boundary conversions, schedule keys, 13.4's offset logic; cases 13.1 to 13.4; Q11's two choices flown and reported; negative controls | 3 | 0007, 0017, 0022, 0028, 0029 | P2, PR 9, Q8, Q10 to Q12 | `ctest --preset release -R 'Nesc.*Case13' --no-tests=error`; `ctest --preset asan -R 'F16Control\|NoAlloc' --no-tests=error` (R6, R9 to R12) |
| 11b | Sim-side `F16_gnc` peer (the circumnavigator); cases 15 and 16 | 3 | 0007, 0022, 0028, 0029 | PR 11a | `ctest --preset release -R 'Nesc.*Case1[56]' --no-tests=error` (R7, R9) |
| 12 | The embedded port: a C++17 program in `embedded/` (`icd/` + `hal/` only), the schedule as arguments, the lockstep equivalence tests; gate scope proposed from measured time | 3 | 0006 to 0008, 0022 to 0024, 0029 | PR 11a, 11b, Q13 | `just embedded-standalone`; `.venv/bin/pytest tests/python -k f16_port` (R10, R11, R13, R14) |
| 13 | §15 envelope monitors (if Q16): the declaration virtual, the health check, the F-16's and the notional transport's limits | 3 | 0020, 0031 | PR 1, PR 6b | `ctest --preset asan -R 'Health\|Envelope' --no-tests=error`; `.venv/bin/pytest tests/python -k planted` (R18) |
| 14 | Generated config-key reference: the key blocks, the generator, docs-check currency, the consumption test | 2 | 0009 | PR 6b, Q21 | `.venv/bin/pytest tests/python -k config_keys`; `python3 scripts/docs_check.py` (R19) |
| 15 | Tutorial from the F-16 path, with its test | 2 | 0009, 0025 | PR 12, PR 14, Q22 | `.venv/bin/pytest tests/python -k tutorial` (R20) |
| 16 | M7 closeout: PLAN.md check-off with evidence links, architecture status, open items for held-out scope | 0 | none | all | `python3 scripts/docs_check.py`; `git diff --check`; one Codex pass |

Every M7-prefixed open item in PLAN.md is taken:
- `Config` cycles: PR 2.
- Rotation consistency: PR 3.
- `mass/constant`'s `ixy` convention: PR 4.
- Trim's names and §18 step 4: PR 5.
- ADR-0002's wording: PR 1 (ADR-0030).
- Envelope monitors: PR 13, or left open with Q16's reason if Joe chooses after.

PR 1, 3, 4, 6b, 7, 8, 9, 11a, 11b, 12 and 13 are Tier 3: adversarial Codex review and Joe reading the
critical diff and tests. ADR-0028 and ADR-0029 need Joe's acceptance before PR 8 and PR 11a start.

## Validation and risks

- **A two-member oracle (Q2).** Tight where SIM 4 and SIM 5 agree, loose where their transient timing
  differs (Q4). R8 reports both, and R9's negative controls show the bands can fail.
- **Trim convention (Q6, Q7).** The trim cases' bands are millimeters to decimeters over 180 s, so the
  convention decides R5. PR 5 shows the convention by an independent rate computation before PR 9 flies
  against the bands.
- **P1 and P2.** Nothing in PR 6 or PR 11 starts without them. If `checkData` is absent, R2's oracle
  falls back to P3, which Joe places.
- **Bitwise port equivalence (Q13).** Different compile flags in `embedded/` could break it. PR 12
  states the flags of both builds before the first comparison.
- **Gate time (Q23).** Measured per PR. The ASan preset on 180 s cases is the risk.
- **Rollback.** Every PR is additive, except PR 5 (trim's signature and convention; the notional
  transport's committed trim values are literals in its configs and do not move) and PR 13 (a core API
  bump). Each reverts on its own.
- **Held out.** Case 17, case 14, sensors (M9), actuators (M8), closed-loop Monte Carlo and scenarios
  (M10), Q5 (a)'s per-source wrench signals if not chosen, and Q9 (a)'s narrowing if not chosen. PR 16
  records each.

## Plan critique

One fresh, read-only Codex plan critique runs on this draft before Joe sees it. Its record, findings and
resolutions are in [the critique record](m7-plan-critique.md); the findings this plan accepted are folded
in above.

## Done when

R1 to R21 pass on their PRs, each PR has its tier's review with results in its PR body, and PR 16 checks
M7 off in PLAN.md with evidence links. Implementation completion is separate from Joe's approval and
merges.
