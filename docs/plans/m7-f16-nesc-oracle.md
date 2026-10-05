# M7 F-16 and the NESC oracle: spec, plan and PR decomposition

Status: **draft; Joe decided Q1 to Q27 on 2026-10-04** (recorded under "Joe's decisions"), with his
amendments folded in. The spec and plan as a whole are **not yet approved**, and nothing is implemented.
- The Codex plan critique (verdict rework, C-01 to C-11) is folded in: all 11 are accepted, and C-04's unit
  question became Q26.
- The Q25 verification pass (verdict rework, N-01 to N-08) is folded in too: all 8 accepted.
- See "Plan critique" below, and [the critique record](m7-plan-critique.md).
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
identifier wording), and ADR-0031 (the envelope declaration; Q16 keeps it in M7). ADR-0028, 0030 and 0031
are written in PR 1a. ADR-0029 is written in PR 1b, after PR 6a (amendment 1). Q5 and Q9 are decided as
options (c) and (b), so no further record is needed.

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
  names `controls.elevator` and `controls.throttle` (`:170`), which is the trim-names item, although
  `Simulation.input_names` is already bound (`python/bindings/_core.cpp:661`).
- **Trim's consumers rebuild the old state themselves** (critique C-03):
  - `_runkit.trim_fragment` (`python/astra/_runkit.py:250`) writes `euler_nb = [0, alpha, 0]`,
    `omega_b = 0` and literal control names into the config.
  - `astra trim` (`cli.py:656`) holds all six residuals to D8's 1e-6 (`_runkit.TRIM_RESIDUAL_BOUND`).
  - Its default altitude (`cli.py:694`) reads `p_n[2]`, which is down under `earth/flat`.
  - `astra.scenario`, `astra.linearize` (`linearize.py:248`), `astra.lockstep` (`lockstep.py:49`) and
    `astra.montecarlo` call the solver too.
- **Health consumers** (critique C-11). `_runkit.WARN_COUNTERS` and `HEALTH_SIGNALS`
  (`_runkit.py:63`) fix the health signals the tools know, and `astra run` raises on any other one
  (`cli.py:501`). A new monitor reaches `--strict` (`cli.py:522`), scenarios and reports only through
  them.
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
- **The core API.** `kCoreApiVersion` is 9 (`core/include/astra/core_api.hpp.in:51`). Its history records
  a bump for every new virtual on an existing interface, version 4 being `EarthModel`/`GravityModel`'s
  `in_domain` (`core_api.hpp.in:18`), so PR 3's accessor pair and PR 13's declaration each bump it (critique
  C-10). The next ADR number is 0028.
- **The purity contract** (`core/include/astra/interfaces/wrench_source.hpp:61`). `wrench_b` is pure
  over the stage's state, its environment, its latched inputs, and any own or peer stage-buffer binding
  (ADR-0021). An independently loaded instance does not acquire those bindings through `init()` and
  `bind_inputs()` (critique C-07).

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
Control names come from the already-bound `Simulation.input_names`. The unknowns, the residuals and their
expected values per case are Q7's. A trim that does not converge, or misses its threshold, raises naming
the residual; it never returns a state silently out of tolerance.

Every consumer moves with the solver (critique C-03):
- `_runkit.trim_fragment` writes the solved heading, attitude, rate and the solved controls' own names.
- `astra trim` and `_runkit.apply_trim` (`_runkit.py:232`, the scenario and Monte Carlo path) apply Q7's
  residual policy, not one 1e-6 bound on all six residuals (verification N-03).
- `astra trim`'s default altitude comes from the Earth model's representation.
- `astra.scenario`, `astra.linearize`, `astra.lockstep` and `astra.montecarlo` take the new result.
A round trip proves it: a config rebuilt from what each tool emits reproduces the solved state, controls,
altitude and residuals.

**The sim-side law (Q8 to Q12).**
- The peer runs the DML law on the ICD v1 frame the exchange stage packs, at the exchange divisor (Q12).
  It returns `astra_sw_to_sim_v1`.
- It derives EAS from `tas`, `cas` and `mach` as above, for valid subsonic inputs only (critique C-08).
  A frame whose status flags mark any input the law reads as invalid (`CAS_SUPERSONIC`,
  `ATMOSPHERE_INVALID`, `WIND_INVALID`), or whose Mach is not above zero, is refused: the peer returns
  non-finite commands, so the exchange halts `kTransport`, naming the field (architecture §15). It never
  computes from a placeholder zero.
- It runs in SI end to end (Q26, decided; verification N-01). Its inputs are ICD v1's SI fields and the
  EAS above. Its gains and limits are the DML's values, each converted once into SI where it is declared.
  Its outputs are radians and a 0..1 throttle. The only boundary steps are three: the aileron's sign
  (Table 9's +LWD to ICD v1's right wing down), the PLA scale (percent of the DML's 0 to 100 to throttle 0
  to 1), and the normalized pilot controls (Tables 8 and 10), which the law keeps as dimensionless.
  Customary units (degrees, feet, KEAS) appear only in the independent DML reference that R11's and Q26's
  tests compare against (critique C-04).
- Trim's elevator and throttle become `trimmedPilotControl_long` and `trimmedPilotControl_throttle`
  through the inverse of the law's own limit-and-scale block (Vol II Figure 9; the DML is normative).
  The proof: the disengaged law (SAS and autopilot off) reproduces the trimmed physical controls (R23).
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
- The plugins read the write seam's controls in ICD v1's convention (aileron positive right wing down,
  radians, throttle 0..1). Each converts once, inside the plugin, to the DML's own input convention and
  units, which PR 6a's addendum records (critique C-05). Two things are pinned: the native tables, and
  the physical response. A positive ICD aileron gives a right-wing-down rolling moment.
- PR 6a's addendum also records whether the aero model binds any own or peer continuous state, which Q5's
  route depends on (critique C-07).
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
- The tools learn the new counter (critique C-11): `_runkit.WARN_COUNTERS` and `HEALTH_SIGNALS`, `astra
  run --strict`, scenarios and reports. An envelope-only warning gives `astra run --strict` exit 2, and the
  counter appears in the summary.

**Rotation consistency (Q17).** At init, `Simulation::from_config` refuses an Earth model and a gravity
model that disagree on rotation, in both directions, naming both plugin paths. The check mirrors the existing
`assumed_position_representation()` check. The accessor pair is a new virtual on two existing interfaces,
so `kCoreApiVersion` is bumped, and a plugin built against the preceding version is rejected (critique
C-10).

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
| R2 | Aero model equals its source | The compiled plugin, driven through `wrench_b`, reproduces the DML's `checkData` within the tolerance the DML states, or P3 if there is none (Q14). The `checkData` extraction shares no code path with the table emitter (lesson 2026-09-25). Points beyond every table's breakpoints follow that table's DAVE-ML extrapolation semantics. Driven through the write seam in ICD convention, a positive aileron gives a positive (right-wing-down) rolling moment, and positive elevator and rudder give the moments ADR-0001's signs require (critique C-05) |
| R3 | Propulsion equals its source | As R2. Throttle 0..1 maps to PLA 0..100 % in one place, pinned by a test at 0, 0.5 (MIL) and 1 (MAX). Engine states, if any, follow ADR-0021 |
| R4 | Trim | In PR 5, for synthetic and notional-transport conditions spanning latitude and heading (case 15's 89.95° N included), three checks: the three solved residuals, recomputed through `Simulation.derivative`, are at most 1e-6; the lateral residuals match Q7's closed-form kinematic and gravity derivation on a test wrench source with no rate dependence; and ω_b at the solution equals an independent n-frame-rate computation (Q6). For each F-16 case's initial condition, the solved residuals are at most 1e-6, and the lateral residuals equal that case's full expected residual. That residual comes from the DML, independently of ASTRA's plugin, and is brought to Joe in PR 9 (11, 12), PR 11a (13.x) and PR 11b (15, 16). Each consumer's before-and-after values are measured in PR 5 (Q6's blast radius). If PR 6a finds that a model owns continuous state (the engine's power lag), trim builds the full combined-state buffer, solves those states for equilibrium as the DML defines it, checks their derivatives against the same threshold, and carries them through every consumer's config round trip (PR 5b; verification N-05). An arbitrary heading, flat Earth (the template copy) and names from `Simulation.input_names` are each seen to work. The round trip holds: each consumer's emitted config (`astra trim`, `_runkit` for scenarios and Monte Carlo, `astra.linearize`, `astra.lockstep`) rebuilds the solved state, controls, altitude and residuals (critique C-03). `astra trim` on a fresh template copy no longer exits 65 for its Earth model |
| R5 | Cases 11, 12 open loop | Every non-excluded channel within ADR-0028's band at every gated 10 Hz sample, aero channels via Q5's route; the ungated span is logged |
| R6 | Cases 13.1 to 13.4, sim-side `F16_control` | As R5, with the peer flying Vol II Tables 37 to 40's schedules, Q11's and Q12's decided choices, and Q27's 13.4 logic. Reported, never selected: SIM 5's LQR references, divisor 1, and Q27's geometry variants |
| R7 | Cases 15, 16, sim-side `F16_gnc` | As R5, yaw and longitude wrapped; latitude near 90° and the ±180° crossings in both directions (Vol II C.1.18, C.1.19) |
| R8 | Discrimination is reported | The generator lists each case's zero-accepting channels and every band's ratio to its floor; ADR-0028 records them. A zero-accepting channel is never cited as evidence |
| R9 | Negative controls (Q24) | Each planted defect fails at least one discriminating band of each case family it touches: Izx's sign (11, 13.x), the elevator's sign (11, 13.x), the aileron's sign at the law boundary (13.3, 13.4, 15, 16), SIM 2's zero-inertial-rate trim (11, 12, 16), and the law at the wrong divisor (13.x), unless Q12's measurement shows that divisor within the band, in which case that is recorded instead |
| R10 | Command schedules agree | The peer's config schedule and the port's arguments give the same command timeline, frame for frame, in a test comparing both against Vol II Tables 37 to 43 |
| R11 | Boundary conversions | Unit tests of the peer and of the port. Every SI gain and limit equals the DML's value times its stated conversion. On hand-built and randomized ICD v1 frames, the SI law's ICD commands equal an independent reference evaluation of the DML law in its own units (degrees, feet, KEAS, rad/s, PLA percent), followed by the reference's own conversion, to 1e-12 relative. This covers the aileron's sign and the throttle pair (critique C-04, verification N-01). Q27's cross-track expression on fixtures on the course, 609.6 m left of it and 609.6 m right of it (signs -, +), with its h0 fixed, in the peer and the port alike (verification N-02) |
| R12 | EAS from ICD v1 | For valid subsonic frames over 13.x's, 15's and 16's flight condition range, the peer's EAS from `tas`, `cas` and `mach` equals TAS·√(ρ/ρ0) from the sim's own atmosphere signals to 1e-12 relative, with ρ0 = p0·M0/(R*·T0) from `icd_pack`'s constants. A frame flagged `CAS_SUPERSONIC`, `ATMOSPHERE_INVALID` or `WIND_INVALID`, or with Mach not above zero, makes the peer return non-finite commands and the exchange halt `kTransport`, naming the field (critique C-08) |
| R13 | Port equivalence (Q13) | For 13.1 to 13.4, 15 and 16, both runs complete (`complete = 1`) with the same `frames_run` and exchange count, and these are bit-identical: the `sim_to_sw` and `sw_to_sim` streams at every exchange (the recorder's), every signal name, unit and rate group, every state and physics signal column, and `/final_state`. The stored config and `/plugins` are compared after removing only the `transport` block and the transport plugin's row; every physics plugin's identity and SHA-256 must match. Excluded and named: the `transport` config block, the transport's `/plugins` row, and the `transport.*` signals (critique C-09). Both runs start from one trim result (amendment 7), and PR 12 records both builds' `-march` and `-ffp-contract` |
| R14 | The port's boundary | `just embedded-standalone` and `scripts/embedded_deps_check.py` pass with the port added; it compiles as C++17 and links only `icd/` and `embedded/hal/` |
| R15 | Rotation consistency | Both mismatch directions (`earth/round rotating: true` with a non-rotating `inverse_square`; `wgs84_j2` with a non-rotating Earth) are refused at init with a `ConfigError` naming both paths; every tracked config still builds; `kCoreApiVersion` is bumped and a descriptor carrying the preceding version is rejected (critique C-10) |
| R16 | Config cycles | The three reproducers in PLAN.md's item give a `ConfigError` naming the key, `astra run` exits 65, and nothing segfaults |
| R17 | `mass/constant`'s convention | The keys' convention is documented in the plugin and in the config-key reference, and a test pins the tensor entry's sign for a positive `ixz` |
| R18 | Envelope monitors (Q16) | A test drives α, β, Mach, q̄ and n past declared limits and sees each check fire with its action and source; the default is `warn`; M6's R9 batch is unchanged under `warn`. An envelope-only warning gives `astra run --strict` exit 2, a scenario's strict outcome, and its counter in the summary and the report; a `kCoreApiVersion` bump rejects a stale plugin (critique C-11). The NESC harness's warn-counter policy (Q16): no halt and zero envelope counters in every case, except case 12's nonzero Mach counter, with its other envelope counters zero (verification N-07) |
| R19 | Config-key reference | The generated file is current (docs-check), and every key consumed by every tracked config is documented (pytest via `Config::unknown_keys`'s complement) |
| R20 | Tutorial | A pytest runs the tutorial's command blocks on a throwaway `just new-aircraft` copy and checks each step's stated outcome |
| R22 | Law onset and engagement (critique C-06) | Law and exchange fixtures at engagement (frame 0) and at the command steps (frames 5000, 15000, 20000; at Q12's divisor, the exchange at or after each). Each step's hand-built ICD frame and schedule give the law's first changed command, computed independently from the DML's gains. That command is staged and latched at the same frame (ADR-0017, ADR-0022: zero compute delay). A planted one-exchange application delay fails these fixtures. This covers 13.1's and 13.2's onset, which the 8 s window does not gate |
| R23 | Trimmed pilot controls (critique C-04) | With SAS and autopilot off, the law fed trim's `trimmedPilotControl_long` and `trimmedPilotControl_throttle` (the inverse limit-and-scale) returns trim's own elevator and throttle within 1e-12, for cases 11, 13.x, 15 and 16's trims |
| R21 | Records and gate | ADR-0028 to ADR-0031 accepted by Joe; `./scripts/check-full.sh` exit 0 with `check: OK` per PR except Tier 0; review per tier |

## Open questions

Each has a recommendation. "ADR" means the answer is recorded in an ADR; "plan" means Joe's decision here
is enough. Blocks: the first PR that cannot start without it.

- **Q1. Prerequisites (blocks PR 6a, 11a, 11b).** Joe places P1 and P2 from the NESC flightsim
  repository, and P3 from NTRS if wanted. Recommend that the figures serve only as a cross-check of P2,
  never as its replacement, since Vol II names the `.dml` files normative.
- **Q2. Ensemble members (ADR-0028; blocks PR 1a).** Recommend honoring footnote (b): the bands come from
  SIM 4 and SIM 5 only, and SIM 2's distance from the band is reported, never gated. This is the
  comparison NASA itself made. Alternatives:
  - all three members, which takes in SIM 2's documented gravity and trim differences and makes every band
    wider;
  - a per-channel SIM 2 list, the shape ADR-0015 rejected because it makes references inconsistent within
    a case.
  Disclosed: two members fall below Vol I's three-data-set ground rule. The band is k times one pairwise
  difference, so a channel where SIM 4 and SIM 5 happen to agree is tighter than three tools might give.
- **Q3. The gated window and generator mechanics (ADR-0028; blocks PR 1a).** Recommend honoring footnote
  (a): gate t ≥ 8 s in every case and report 0 to 8 s.
  - Disclosed: 13.1's and 13.2's commands fire at t = 5 s, inside the ungated span, so the gate sees only
    the tail of their response (8 to 20 s). The onset is checked independently by R22's fixtures at the
    command frames (critique C-06), not by equivalence: two implementations can share one wrong phase.
  - Alternative, decided per case family: gate from the end of the start-up oscillation (t ≥ 2 s, Vol II
    D.1.11's first-second oscillation). That gates 13.1's and 13.2's onset, at the cost of wider force
    and moment bands: 13.1's force_z goes from 2,411 N to 1.36e5 N.
  - The mechanics, recommended as described in the contract: a 1e-5 s matching tolerance (the float32
    half-ulp bound), per-case durations, a longitude wrap, the duplicate-column check, and k = 3
    unchanged.
- **Q4. The band's shape for the maneuver cases (ADR-0028; blocks PR 1a).** Recommend ADR-0013's scalar
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
  in `nesc_cases_test.cpp`. It needs no core change and is Tier 3 for the test tree.
  - It holds only if PR 6a's addendum finds that the F-16 aero model binds no own or peer continuous
    state (critique C-07). `wrench_b` is pure over those bindings too (ADR-0021), and a second instance
    does not acquire them. If the aero model does bind state, the evaluator must reproduce the complete
    frame-start snapshot, or the choice returns to Joe with (a).
  - A self-check on the F-16 itself guards it: with controls that change during the run, the evaluator's
    aero wrench plus an independently evaluated propulsion wrench equals `wrench_total` to 1e-12 at every
    sample.
  - That sum holds only if the propulsion evaluation sees the engine's frame-start state. S&L's F-16
    engine carries a power-lag state, a model-owned state under ADR-0021. PR 6a's addendum records whether
    the F-16 propulsion binds own state. If it does, the self-check reads that state's frame-start value
    from the sim (the state buffer at the sample's pre-step frame) and gives it to the independent
    propulsion instance through its own `bind_state`. Alternatively, it compares aero against
    `wrench_total` minus the sim's own frame-start propulsion contribution, with that route stated in
    PR 7's plan (amendment 6).
  Alternatives:
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
  - **The blast radius (amendment 3).** The new convention moves every runtime trim caller already in the
    gate:
    - `astra.lockstep`, which M4's acceptance uses; `trim_for` also raises on a nonzero heading
      (`lockstep.py:178`);
    - `astra.linearize` and its mode checks;
    - `astra trim` (`test_cli`), and `test_trim` and `test_pyyaml_route`;
    - the scenarios `cruise_hold`, `elevator_step`, `throttle_step` and `aileron_step`;
    - the Monte Carlo fixture `cruise_64.yaml`;
    - M6's batch fixtures `planted.yaml` and `planted_pilot.yaml` (R9 and its negative control);
    - `test_cruise_phugoid` (`tests/python/test_cruise_phugoid.py:197`), whose linear reference
      `linearize` recomputes while the nonlinear run starts from committed literals (verification N-04);
    - `tests/python/autopilot_design.py:122` (`build_plants`), which `test_autopilot_design` checks against
      recorded phugoid values and controller margins (N-04).
    PR 5's plan lists each caller, measures it before and after on the same build, and states whether any
    acceptance value, scenario check or fixture expectation moves. If any moves, PR 5 becomes Tier 3 and
    Joe decides.
  - The alternative to present then: the new convention as an explicit option (`rate_convention`), with
    today's as the default until each caller is migrated in its own reviewed step.
- **Q7. Trim unknowns and thresholds per case (plan; blocks PR 5).** Vol II C.1.11: the participants
  varied "pitch attitude, elevator position, and throttle setting" only. Recommend those three unknowns,
  at the case's heading with φ = β = 0 and aileron = rudder = 0:
  - The three solved residuals (u̇, ẇ, q̇) must be at most 1e-6 (m/s², rad/s²), as M3's D8.
  - The three lateral residuals (v̇, ṗ, ṙ) do not vanish with φ, β, aileron and rudder held at zero. Under
    Q6's rate convention, the velocity equation's rotation terms leave about 0.017 m/s² in case 11 and
    0.082 m/s² in case 12 (critique C-01, from Table 7's mass and SIM 5's t = 0 values). This plan's first
    draft guessed a 1e-4 bound, which these values refute.
  - The full lateral residual at trim has four parts:
    - the kinematic terms (Earth rate, transport rate, rotation times velocity);
    - the J2 gravity's lateral component;
    - the aero model's rate terms at the trim state (the body rates Q6 imposes);
    - any engine gyroscopic term.
    The last two need P1, and in case 15 they dominate ṗ and ṙ. SIM 5's t = 0 aero moments (L = -225.83,
    N = 857.28 lbf·ft) alone give about -0.022 and 0.013 rad/s² with Table 7's inertias (amendment 2).
  - So the derivation is split by what each PR can know:
    - **PR 5** derives and checks only the kinematic and gravity parts. It derives them independently, in
      closed form, from the latitude, heading, airspeed and altitude, never reading ASTRA's own EOM. It
      checks them on a test wrench source whose force and moment do not depend on body rates, so the
      derivation is the whole residual.
    - **Each F-16 case's full expected residual** is derived from the DML (P1) by a reference evaluation
      that shares no code with ASTRA's plugin, and brought to Joe: cases 11 and 12 in PR 9, 13.x in PR
      11a, 15 and 16 in PR 11b. R4 checks the solved state against it within a tolerance stated in that
      PR.
  - Case 16 is at the equator heading north, where the Earth rate lies along the body x axis and the
    transport rate's vertical term is zero.
  - Case 15 (89.95° N, ψ = 90°). Holding the local-vertical attitude there needs a body yaw rate of about
    0.031 rad/s (v_e·tan φ / R; SIM 5's CSV starts at -1.764 deg/s). Its lateral residuals are therefore
    large: about 5.28 m/s² of v̇, mostly the rate-times-velocity term, per the critique (C-02). They are
    derived like the others, not bounded by a guess.
  - Aerodynamic reference checks are separate from these derivative checks (C-02). At t = 0 of case 15,
    ASTRA's aero side force, rolling moment and yawing moment are compared, in their own units, with SIM 5's
    t = 0 values from its CSV: -63.00 lbf, -225.83 lbf·ft and 857.28 lbf·ft. Vol II D.1.17's prose says
    786 lbf·ft for the yawing moment; the CSV is the published data and is used, and the discrepancy is
    recorded. This comparison is reported, never asserted. t = 0 is inside Q3's ungated span, and no
    member-derived tolerance exists there: SIM 4 starts case 15 untrimmed, with a pitching moment of
    -42,923 lbf·ft in its CSV. The autopilot engages at the first frame and absorbs these terms.
  - **Model-owned states (verification N-05).** `trim.py:104` builds a 13-element state, while
    `Simulation.derivative` needs the full combined state (`_core.cpp:179`). If PR 6a finds that the F-16
    owns continuous state (the engine's power lag), PR 5b extends trim:
    - the full buffer is built;
    - each model state is an added unknown, with its derivative an added residual at Q7's threshold;
    - its equilibrium is checked against the DML's own definition;
    - every consumer's round trip carries it.
    Otherwise PR 5b is not scheduled.
  Alternative: a six-unknown solve (adding φ, aileron and rudder) that zeroes all six residuals in every
  case. It is more complete, but no member did it, and in case 15 it would bank the aircraft before the
  autopilot engages.
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
  - **Fixed before any run (amendment 5).** PR 11a flies the decided choice. It may also fly SIM 5's
    replacement as a reported comparison, never as a selection criterion. If the decided choice fails its
    band, that is a finding for Joe. It is never swapped for a variant that passes, which would fit the
    model to the oracle.
  Alternative: SIM 5's replacement, which tracks SIM 5 and departs from the normative file.
- **Q12. The law's execution rate (ADR-0029; blocks PR 11a).** Recommend the DML's rate if it states one.
  Otherwise the exchange divisor 10 (100 Hz, the architecture's default). Vol II names execution-rate
  differences as a possible cause (D.1.15, D.1.16). The divisor is fixed by PR 6a's addendum before any run
  (amendment 5). A divisor-1 run is a reported comparison, never a selection criterion, and a failing band
  at the decided divisor is a finding for Joe.
- **Q13. The port's equivalence criterion (ADR-0029; blocks PR 12).** Recommend bitwise equality against
  the sim-side run, for the objects R13 names:
  - the `sim_to_sw` and `sw_to_sim` streams at every exchange;
  - every signal name, unit and rate group, and every state and physics column;
  - `/final_state`, `frames_run`, the exchange count and completion;
  - the stored config and `/plugins`, compared after removing only the `transport` block and the
    transport's own row.
  The only exclusions are the `transport` config block, the transport's `/plugins` row and the
  `transport.*` signals, which differ by construction (verification N-06). The inputs are identical by
  Q8's construction. Both builds use clang-18 with the
  same floating-point flags (no `-ffast-math`, the same `-ffp-contract`), so equality is the expected
  result, and any difference is a porting defect until shown otherwise. If a stated floating-point cause
  makes bitwise infeasible, it comes back to Joe. Alternatives:
  - a band against the sim-side run;
  - a band against the ensemble.
  Each is weaker: a porting defect smaller than the band passes, and the ensemble's maneuver bands (Q4) are
  wide.
  - **Two implementations (amendment 7).** The sim-side law (`aircraft/f16`, C++23, in the plugin build)
    and the port (`embedded/`, C++17, `icd/` and `embedded/hal/` only) are separate sources. That is how
    Joe's 2026-09-29 decision reads ("run outside `embedded/` first, then ported"), and it makes R13 test a
    real port.
  - The alternative, recorded and not recommended: one C++17 source compiled into both. R13 would then
    test only the hosting (the exchange, the transports and the build), and `aircraft/` would gain a new
    include direction into `embedded/`, which ADR-0008 does not have.
  - **One trim result.** Both runs consume one trim result: the same initial-state literals and the same
    `trimmedPilotControl` values, written once into the shared config. Never two separate solves.
  - PR 12 states `-march` and `-ffp-contract` (and every other floating-point-relevant flag) for both
    builds before the first comparison.
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
  - **Case 12 (amendment 8).** At Mach about 2, case 12 is outside the F-16's declared domain once PR 13
    lands. Its config states the Mach envelope's action explicitly: `warn`, because NESC flew the case
    there with the same model. The NESC harness gains a warn-counter policy, which today it lacks
    (`nesc_cases_test.cpp` asserts only that no halt occurs). It asserts no halt in every case, and every
    envelope counter zero in every case except one: in case 12 the Mach counter must be nonzero,
    documenting the excursion, while its α, β, q̄ and load-factor counters must stay zero (verification
    N-07). PR 13 updates PR 9's case 12 accordingly and must not break it.
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
- **Q20. ADRs M7 writes (blocks PR 1a and PR 1b).** Split by what each record needs (amendment 1):
  ADR-0028, ADR-0030 and ADR-0031 in PR 1a, which needs neither P1 nor P2; ADR-0029 in PR 1b, after PR 6a
  has finalized Q12 and Q26 from P2.
  - ADR-0028: the F-16 oracle (Q2 to Q4), elaborating ADR-0013 to ADR-0015.
  - ADR-0029: sim-side flight software as an in-process peer, its schedule, rate and units, and the port's
    equivalence criterion (Q8, Q10, Q12, Q13, Q26).
  - ADR-0030: ADR-0002's identifier wording. Recommend: the no-unit-suffix rule governs signal names,
    while C++ identifiers and config keys may carry a unit suffix, as `wing_area_m2`, `AtmoState`'s fields
    and `alpha_max_rad` already do. It supersedes only that sentence of ADR-0002.
  - ADR-0031: the envelope declaration (Q16: inside M7).
  - No record for Q5 or Q9: Joe chose (c) and (b), neither of which needs one.
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
- **Q25. This plan's own review (plan).** The critique's verdict was rework, and all 11 findings are
  folded in. Recommend one verification pass on the final diff, as M6 ran (fresh, read-only, checking
  C-01 to C-11's resolutions and any new finding). Its alternative: Joe waives it and the critique stands
  as this Tier 0 PR's Codex pass.
- **Q26. The law's units (ADR-0029; blocks PR 11a; from critique C-04).** Tables 8 and 10 state the law's
  inputs in degrees, feet, KEAS, rad/s and normalized controls. Its gains and limits are written in those
  units: Figure 7's eight gains per axis, the -0.05 deg/ft altitude gain, the ±30° bank limit. ADR-0002
  keeps `aircraft/` in SI.
  - Recommend an SI realization. PR 11a converts every gain and limit once into SI, where each is
    declared:
    - each constant is written as the DML's value times named `core/util/units` conversions, never as a
      pre-multiplied number;
    - the law runs in SI on the ICD frame;
    - a test evaluates the DML's own law, in its own units, on randomized frames and compares to 1e-12
      relative.
    This satisfies ADR-0002 without exception. The port, which cannot link `core/` (ADR-0008), declares
    the same products with its own named conversion constants, and R11's fixtures pin both.
  - Alternative: keep the DML's units inside the law, converting ICD inputs to them and outputs back at the
    peer's boundary. Every gain then matches the DML's text exactly, but `aircraft/` holds a
    customary-unit interior, an exception to ADR-0002 that ADR-0029 would have to state.
- **Q27. 13.4's lateral-deviation logic (ADR-0029; blocks PR 11a; amendment 4). Decided by Joe
  (2026-10-04), with his two amendments.** Vol II gives no formula: C.1.16 says "user-supplied logic", and
  D.1.16 reports only the 2,000 ft asymptote and a heading deviation of +0.14° to +0.21°. The decision is
  fixed before PR 11a flies and is never selected by its fit to the bands.
  - *Geometry: the initial-plane line.* The base course is the straight line through the initial position
    (t = 0) at χ = 45° true in that position's local north-east plane, with the plane's scales fixed at
    the initial position. D.1.16 says the offset is "to the right of the initial path". From t = 20 s the
    desired course is that line moved 609.6 m (2,000 ft) to its right.
  - *What it is not.* A constant `trueBaseCourseCommand` holds a rhumb line, whose east scale follows
    cos(lat). The decided line departs from that rhumb line by about tan χ·(M0 + h0)·tan(lat0)·Δlat²/2
    in east, about 2 m of cross-track by 60 s. The verification pass computed 0.242 m at 20 s and 2.184 m
    at 60 s from 13.4's initial condition (N-02).
  - *Distance.* The signed cross-track distance from the base course:
    - Δn = (lat − lat0)·(M0 + h0) and Δe = (lon − lon0)·(N0 + h0)·cos(lat0);
    - M0 and N0 are WGS-84's meridian and prime-vertical radii at lat0, and h0 is the initial altitude;
    - d = −Δn·sin χ + Δe·cos χ;
    - `lateralDeviationError` = d − 609.6 m from t = 20 s, and 0 before (heading hold, as in 13.1 to
      13.3).
  - *Sign.* Positive when the aircraft is right of the desired course (Table 8's "+right"; the
    verification pass confirmed it against Figure 3). At t = 20 s the error is d(20 s) − 609.6 m, which
    is not exactly −609.6 m because d(20 s) need not be zero, and the law turns right.
  - *Inputs and update rate.* The logic reads only the ICD's `lat` and `lon`, at every exchange (Q12's
    divisor), in the peer and the port alike. h0 stays fixed at the initial altitude: the altitude's
    effect on the offset's scale is below 1 cm over 13.4's few-meter altitude changes. R11's fixtures pin
    the expression in both implementations.
  R6 reports three variants, flown and never selected:
  - the great circle through the initial position;
  - the true rhumb line through the initial position;
  - the decided line through the position at 20 s.
  A great circle and a rhumb line diverge by about 4 m over the run, against an end separation of 11.6 ft
  (3.5 m) between SIM 4 and SIM 5.

## Joe's decisions (2026-10-04)

- **Decided as recommended:** Q1 to Q4, Q6, Q8 to Q10, Q12, Q14 to Q24, and Q26.
- **Decided as recommended, subject to Joe's amendments:**
  - Q5, with amendment 6 (the propulsion's frame-start state in the self-check);
  - Q7, with amendment 2 (aero and engine terms derived from P1 in PR 9, 11a and 11b);
  - Q11, with amendment 5 (fixed before any run; variants reported, never selected);
  - Q13, with amendment 7 (two implementations, one trim result, the build flags stated).
- **Q25:** one fresh read-only Codex verification pass after the amendments are folded in. It covers
  C-01 to C-11's resolutions and the amendments.
- **The other amendments:**
  - 1: PR 1 split into PR 1a and PR 1b, with the dependencies updated;
  - 3: PR 5's blast radius, under Q6;
  - 4: the new Q27, 13.4's lateral-deviation logic. Decided as recommended, with two amendments: name the
    course as the initial-plane line it is, with the true rhumb line a reported variant; and state the
    inputs it reads (`lat` and `lon`, with h0 fixed);
  - 8: case 12 and the envelope monitors, under Q16.
- Q14 and Q15 stay provisional until PR 6a's addendum, as decided. Q12's divisor and Q26's details are
  finalized there too, before PR 1b.

The Q25 verification pass ran (verdict rework, N-01 to N-08), and its findings are folded in with Q27.
Approval of the spec and plan as a whole is Joe's next decision; nothing is implemented.

## PR decomposition

One Claude builder, sequential unless Joe assigns a parallel worktree. PR 2, 3 and 4 are independent of
the F-16. Each PR opens as a draft, gets its tier's review, and waits for Joe's merge. Every PR but Tier 0
runs `./scripts/check-full.sh` (exit 0 and `check: OK`) and commits a trimmed gate summary.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance commands (beyond the full gate) |
|---|---|---|---|---|---|
| 0 | This planning PR: the spec and plan, the critique record, PLAN.md's Planning line, architecture §16's M7 row | 0 | 0013 to 0015 | none | `python3 scripts/docs_check.py`; `git diff --check origin/main..HEAD`; the Codex plan critique |
| 1a | ADR-0028 (the oracle), ADR-0030 (ADR-0002's wording), ADR-0031 (the envelope declaration, Q16); architecture rev 11 (§7, §12, §15, §16, §18); PLAN.md M7 PR lines. Needs neither P1 nor P2 | 3 | 0002, 0013 to 0015, 0020 | Q2 to Q4, Q16, Q20 | `python3 scripts/docs_check.py`; adversarial Codex review of the ADRs; Joe accepts them |
| 1b | ADR-0029 (sim-side flight software as an in-process peer: its schedule, rate, units, 13.4's logic, two implementations, one trim result, and the port's equivalence criterion); architecture §10 and §11 | 3 | 0002, 0006 to 0008, 0017, 0022, 0024 | PR 6a, Q8, Q10 to Q13, Q26, Q27 | `python3 scripts/docs_check.py`; adversarial Codex review of the ADR; Joe accepts it |
| 2 | Config cycle safety: `unknown_keys` and `value_to_yaml` cycle-safe; the three reproducers; `_yaml_route`'s guard kept or removed | 2 | 0025 | Q19 | `ctest --preset asan -R Config --no-tests=error`; `.venv/bin/pytest tests/python -k 'pyyaml_route or cli'` (R16) |
| 3 | Earth/gravity rotation consistency at init, both directions; the accessor pair on `EarthModel` and `GravityModel`, the `kCoreApiVersion` bump and a stale-plugin rejection test | 3 | 0003, 0009, 0021 | Q17; its own plan review | `ctest --preset asan -R 'Simulation\|Gravity\|Earth\|PluginLoader' --no-tests=error`; `ctest --preset release -R Nesc --no-tests=error` (R15) |
| 4 | `mass/constant`'s product-of-inertia convention, documented and pinned | 3 | 0001, 0002 | Q19 | `ctest --preset asan -R Mass --no-tests=error` (R17) |
| 5 | Trim: heading, Q6's rate convention, Q7's unknowns and the lateral residuals' closed-form kinematic and gravity parts, flat Earth, names from `Simulation.input_names`; every consumer (`_runkit.trim_fragment`, `_runkit.apply_trim`'s and `astra trim`'s residual policy, `astra trim`'s default altitude, `astra.scenario`, `astra.linearize`, `astra.lockstep` and `trim_for`'s heading refusal, `astra.montecarlo`) with round-trip tests; Q7's kinematic and gravity parts only; every gated caller measured before and after (Q6's blast radius) | 2, or 3 if any acceptance value, scenario check or fixture expectation moves (Joe decides) | 0001, 0017, 0021 | Q6, Q7, Q18 | `.venv/bin/pytest tests/python -k 'trim or linearize or cli or scenario or montecarlo or lockstep or planted or pyyaml_route or cruise_phugoid or autopilot_design'`, which reaches every caller Q6's list names; `.venv/bin/pytest tests/scenarios` (R4, less its F-16 cases) |
| 5b | (Only if PR 6a finds a model-owned state.) Trim with model-owned states: the full combined buffer, each state's equilibrium as an added unknown and residual, every consumer's round trip | 2 | 0021 | PR 5, PR 6a | `.venv/bin/pytest tests/python -k 'trim or scenario or montecarlo or lockstep'` with `tests/plugins/state_owner` (R4) |
| 6a | Inventory addendum to this spec from P1 and P2: tables, breakpoints, extrapolation, `checkData`, engine dynamics, gyroscopic terms, Mach dependence, the models' input conventions and units (the aileron's sign), whether the aero and the propulsion bind own or peer state (the engine's power lag), the aero rate terms and the engine gyroscopic term for Q7, and the law's units, limit-and-scale block and execution rate; Q5's condition, Q12, Q14, Q15 and Q26 finalized | 0 | none | P1, P2 | `python3 scripts/docs_check.py`; Joe confirms Q14 and Q15 |
| 6b | `aircraft/f16` from the template: mass, the table generator and generated aero tables, propulsion; unit tests on `checkData`; ICD-convention control inputs converted once to the DML's | 3 | 0001, 0002, 0016, 0017, 0020, 0021 | PR 1a, PR 4, PR 6a | `ctest --preset asan -R F16 --no-tests=error`; `.venv/bin/pytest tests/python -k new_aircraft` (R1 to R3) |
| 7 | The aero-only channel route (Q5): under (c), the harness's independent aero evaluator, valid only if PR 6a finds no aero state binding; its self-check on the F-16 with changing controls (aero plus independently evaluated propulsion, with the engine's frame-start state, equals `wrench_total`) | 3 | 0013, 0016, 0020, 0021 | PR 6a, PR 6b, Q5 (and its record under (a)) | `ctest --preset release -R 'Nesc\|F16Evaluator' --no-tests=error` (M2's cases unchanged; the self-check) |
| 8 | Generator extension and band headers for cases 11 to 16; `nesc_band_common.hpp` per-case sample counts; cases 1 to 10 byte-identical. Joe arms `touch-goldens` and runs the generator (ADR-0013) | 3 | 0013 to 0015, 0028 | PR 1a | `python3 tests/golden/nesc/generate_nesc_band.py` (by Joe; byte-identical `case01` to `case10`); `ctest --preset release -R Nesc --no-tests=error` (R8) |
| 9 | Cases 11 and 12 open loop: configs, trim to Vol II's initial conditions, the DML-derived full expected trim residuals brought to Joe (Q7), aero channels via PR 7, negative controls for this family. Not dependent on PR 3: the F-16 configs pair `wgs84_rotating` with `wgs84_j2` consistently | 3 | 0013 to 0016, 0028 | PR 5 (and 5b if scheduled), 6b, 7, 8 | `ctest --preset release -R 'Nesc.*Case1[12]' --no-tests=error`; `ctest --preset asan -R NoAlloc --no-tests=error` (R4, R5, R9) |
| 10 | Not scheduled: Joe chose Q9 (b), so the `transport` refusals stay until M10, and the tutorial's closed loop runs through `astra.lockstep` | none | none | none | none |
| 11a | Sim-side `F16_control` peer: EAS, boundary conversions, schedule keys, 13.4's offset logic; cases 13.1 to 13.4; Q11's and Q12's decided choices (variants reported, never selected); Q27's 13.4 logic and its sensitivity; the DML-derived full expected trim residuals for 13.x brought to Joe; Q26's SI realization of the gains; the input conversions, the inverse limit-and-scale and the refusal of flagged inputs; onset and engagement fixtures; negative controls | 3 | 0002, 0007, 0017, 0022, 0028, 0029 | P2, PR 1b, PR 6a, PR 9, Q8, Q10 to Q13, Q26, Q27 | `ctest --preset release -R 'Nesc.*Case13' --no-tests=error`; `ctest --preset asan -R 'F16Control\|NoAlloc' --no-tests=error` (R6, R9 to R12, R22, R23) |
| 11b | Sim-side `F16_gnc` peer (the circumnavigator); cases 15 and 16; their DML-derived full expected trim residuals brought to Joe | 3 | 0007, 0022, 0028, 0029 | PR 11a | `ctest --preset release -R 'Nesc.*Case1[56]' --no-tests=error` (R7, R9) |
| 12 | The embedded port: a separate C++17 implementation in `embedded/` (`icd/` + `hal/` only), the schedule as arguments, the lockstep equivalence tests from one trim result; both builds' `-march` and `-ffp-contract` stated; gate scope proposed from measured time | 3 | 0006 to 0008, 0022 to 0024, 0029 | PR 11a, 11b, Q13 | `just embedded-standalone`; `.venv/bin/pytest tests/python -k f16_port` (R10, R11, R13, R14) |
| 13 | §15 envelope monitors (Q16): the declaration virtual and `kCoreApiVersion` bump, the health check, the F-16's and the notional transport's limits, the tools' health consumers (`_runkit.WARN_COUNTERS`/`HEALTH_SIGNALS`, `astra run --strict`, scenarios, reports), and case 12's explicit Mach action with the NESC harness's warn-counter policy (Q16) | 3 | 0020, 0025, 0031 | PR 1a, PR 6b, PR 9 | `ctest --preset asan -R 'Health\|Envelope\|PluginLoader' --no-tests=error`; `.venv/bin/pytest tests/python -k 'planted or cli or scenario or report'` (R18) |
| 14 | Generated config-key reference: the key blocks, the generator, docs-check currency, the consumption test | 2 | 0009 | PR 6b, Q21 | `.venv/bin/pytest tests/python -k config_keys`; `python3 scripts/docs_check.py` (R19) |
| 15 | Tutorial from the F-16 path, with its test | 2 | 0009, 0025 | PR 12, PR 14, Q22 | `.venv/bin/pytest tests/python -k tutorial` (R20) |
| 16 | M7 closeout: PLAN.md check-off with evidence links, architecture status, open items for held-out scope | 0 | none | all | `python3 scripts/docs_check.py`; `git diff --check`; one Codex pass |

Every M7-prefixed open item in PLAN.md is taken:
- `Config` cycles: PR 2.
- Rotation consistency: PR 3.
- `mass/constant`'s `ixy` convention: PR 4.
- Trim's names and §18 step 4: PR 5.
- ADR-0002's wording: PR 1a (ADR-0030).
- Envelope monitors: PR 13 (Q16: inside M7).

PR 1a, 1b, 3, 4, 6b, 7, 8, 9, 11a, 11b, 12 and 13 are Tier 3, and PR 5 too if a value moves. Each gets an
adversarial Codex review, and Joe reads the critical diff and tests. ADR-0028 (PR 1a) needs Joe's
acceptance before PR 6b and PR 8 start, and ADR-0029 (PR 1b) before PR 11a starts.

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
- **Rollback.** Every PR is additive, except PR 5 (trim's signature and convention, whose gated callers
  it measures before and after, Q6), PR 3 and PR 13 (core API bumps). Each reverts on its own.
- **Held out.** Case 17, case 14, sensors (M9), actuators (M8), closed-loop Monte Carlo and scenarios
  (M10), Q5 (a)'s per-source wrench signals, and Q9 (a)'s narrowing of the transport refusals (Joe chose
  (c) and (b)). PR 16 records each.

## Plan critique

One fresh, read-only Codex plan critique ran on `8fd7f32`:
- companion 1.0.6, Codex CLI 0.160.0, `task --fresh --effort high`, the companion's default model;
- job `task-muure8f3-4om5ig`, thread `01a10a5a-b750-77e1-94e2-4021b1562130`;
- exit 0, verdict **rework**: 11 findings, 5 high and 6 medium.

Claude Code re-read every cited line. **All 11 are accepted** and folded in above; none is rejected. The
full record is in [the critique record](m7-plan-critique.md).

| # | Sev. | Finding (verified) | Resolution in this plan |
|---|---|---|---|
| C-01 | high | Q7's guessed 1e-4 lateral trim bound is refuted by the rotation terms (about 0.017 m/s² in case 11, 0.082 m/s² in case 12) | Q7 and R4: PR 5 derives and checks the kinematic and gravity parts on a rate-independent test model; each F-16 case's full residual is derived from the DML and brought to Joe in PR 9, 11a and 11b (amendment 2) |
| C-02 | medium | Case 15 compared acceleration residuals with aero forces "within a factor"; SIM 5's CSV yawing moment is 857.28 lbf·ft, not the prose's 786 | Q7: derivative checks separate from aero reference checks; the CSV is used and the discrepancy recorded; the t = 0 aero comparison is reported, not asserted |
| C-03 | high | Trim's consumers rebuild the old state (`_runkit.trim_fragment`, `astra trim`'s six-residual bound and flat default altitude); the names binding already exists | Findings, contract and PR 5: every consumer moves with the solver; round-trip tests (R4) |
| C-04 | high | The law's input units and the trim-to-normalized-control mapping are unspecified | Contract, R11 (inputs), R23 (disengaged law reproduces trim), and Q26 (an SI realization of the gains, or the DML's units at the boundary) |
| C-05 | high | The aileron conversion stops at the ICD; the aero plugin's own convention is unspecified | Contract, PR 6a and 6b: the models' input conventions in the addendum; R2 pins native tables and the physical rolling response |
| C-06 | medium | Equivalence cannot check the ungated command onset | R22: independent onset and engagement fixtures; a planted one-exchange delay must fail |
| C-07 | medium | The independent evaluator ignores ADR-0021's state bindings, and its self-check ran on M2's models only | Q5: the route depends on PR 6a finding no aero state binding; PR 7's self-check runs on the F-16 with propulsion and changing controls |
| C-08 | medium | R12 included supersonic case 12, where ICD v1 sets CAS to zero | R12 and the contract: valid subsonic frames only; ρ0 defined; flagged frames make the peer return non-finite commands, so the exchange halts |
| C-09 | medium | R13 excluded whole datasets that should stay comparable, and omitted the streams, counts and `/final_state` | R13: provenance compared after removing only the transport's parts; streams, counts, signal layout, completion and `/final_state` all bit-identical |
| C-10 | medium | PR 3's accessor pair needs a `kCoreApiVersion` bump and a stale-plugin rejection | Contract, R15 and PR 3 |
| C-11 | medium | Envelope monitors stopped before the tools' health consumers | Contract, R18 and PR 13 |

Verification pass (Q25, decided by Joe):
- one fresh read-only Codex task, same route and flags;
- job `task-muussfet-1xnlr9`, thread `01a10a7e-71ff-7c40-a6e6-70aca3dad1aa`;
- `8fd7f32..d436581`; exit 0; verdict **rework**.

Of the first critique's findings, C-01, C-02 and C-05 to C-08, C-10 and C-11 were resolved, and C-03, C-04
and C-09 partially (closed by N-03, N-01 and N-06 below). Amendments 1, 2 and 5 to 7 were implemented, and 3,
4 and 8 partially (closed by N-04, N-02 and N-07). Claude Code re-read every cited line. All eight new
findings are accepted and folded in; none is rejected.

| # | Sev. | New finding (verified) | Resolution |
|---|---|---|---|
| N-01 | high | The contract and R11 converted the law's inputs to customary units, contradicting Q26's SI law | The law runs in SI; customary units appear only in the independent DML reference (contract, R11) |
| N-02 | high | Q27 declared a rhumb line but its formula is a straight line in the initial plane (0.242 m off at 20 s, 2.184 m at 60 s) | Joe's Q27 amendments: named as the initial-plane line, the rhumb line a reported variant, the step error d(20 s) − 609.6 m, fixtures on, left of and right of the course (R11) |
| N-03 | high | `_runkit.apply_trim` (`_runkit.py:232`) still rejects any of six residuals above 1e-6 | The trim contract and PR 5 move it to Q7's policy, covering scenarios and Monte Carlo |
| N-04 | medium | `test_cruise_phugoid` and `autopilot_design.build_plants` were missing from PR 5's caller list | Added to Q6's list and PR 5's selector |
| N-05 | high | A model-owned engine state has no trim definition, and `trim.py:104` builds 13 elements while `derivative` needs the full buffer (`_core.cpp:179`) | Q7 and R4: PR 5b (only if PR 6a finds model-owned state) extends trim to the full buffer with state equilibrium |
| N-06 | medium | Q13 still excluded whole provenance datasets that R13 compares | Q13 restated around R13's objects and exclusions |
| N-07 | medium | Case 12's exemption let unrelated envelope warnings pass, and the harness has no warn-counter assertion today | Q16 and R18: only case 12's Mach counter is exempt; the other counters stay zero |
| N-08 | low | The C-01 summary contradicted amendment 2's ordering | The C-01 row restated |

## Done when

R1 to R23 pass on their PRs, each PR has its tier's review with results in its PR body, and PR 16 checks
M7 off in PLAN.md with evidence links. Implementation completion is separate from Joe's approval and
merges.
