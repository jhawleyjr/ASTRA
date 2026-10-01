# Future work (unscheduled)

Captured from Joe's brainstorm in a Cowork session, 2026-09-29, after the plugin-test guard (#75) merged.
Joe mapped these items to milestones M6 to M11 the same day ("Milestone map" below; the milestones and the
open items' prefixes in [PLAN.md](../PLAN.md)). Each milestone still gets its own spec and plan and follows
the workflow's tier and approval rules. Items already tracked as PLAN.md open items are named here only
where they connect; the open item stays the record.

## Direction (Joe, 2026-09-29)

- Focus on aircraft and atmospheric flight.
- The GNC stack is Joe's own. No third-party flight software (PX4, ArduPilot) in the loop.
- Tools that design or analyze (tuning, sizing a Monte Carlo) run outside the 6-DoF run and reuse its
  models, rather than living inside it.

## Milestone map (Joe, 2026-09-29)

In this order; Joe accepted it as proposed, with the decisions listed after it.

- **M6 Analysis tooling:** plotting and plot packages, Monte Carlo visualization, failure-to-draw
  correlation, Monte Carlo sizing, the run report, the frame-time benchmark, `astra run --strict`, and the
  superseding ADR to ADR-0025's Finalization.
- **M7 F-16 and the NESC oracle:** `aircraft/f16`, NESC cases 11, 12, 13.1 to 13.4, 15 and 16, the tutorial
  and the config-key reference. Takes the open items on Earth/gravity rotation consistency, the mass
  product-of-inertia convention, trim's control names, §15 envelope monitors and ADR-0002's wording.
- **M8 Actuators and controller tuning:** the actuator models, gains loaded at startup, the tuning tool,
  frequency-domain and handling-qualities analysis. Takes the open items on deflection range, throttle
  jitter, linearize's altitude state and the `kSideslip` cleanup.
- **M9 Sensors and ICD v2:** IMU levels, GNSS PVT, baro, air data, magnetometer and a field model;
  non-standard days, gusts, shear and microbursts; ICD decode fuzzing.
- **M10 Navigation, faults and closed-loop Monte Carlo:** the INS/GNSS EKF, NEES and NIS, fault injection,
  closed-loop scenarios and Monte Carlo. Takes the open items on wind events, closed-loop runs, retiring
  `ExchangeRecorder`, Follow-up C and lockstep replay.
- **M11 Real-time:** `RealTime` mode and the benchmark over time. Takes the open items on the logger writer
  thread and the wall-clock scan's timed waits.
- **Later (M12 and beyond, unordered):** GNSS pseudorange and Doppler; radar or laser altimeter with
  terrain; the synthetic feature sensor; an adaptive controller example; prototyping a model in Python;
  fuel burn with mass as state; ground contact; the FPGA link and hardware in the loop; generating the ICD
  from a schema.
- **Not milestones:** the workflow items below (small Tier 0 or Tier 1 PRs at any time); coverage, a
  whole-tree gcc build and gate time (folded into whichever milestone next touches the gate); and two
  unscheduled open items that are Joe's (the stale R1 capture harness, the Jev evaluation).

Decisions (Joe, 2026-09-29):

- The NESC F-16 control laws run outside `embedded/` first and are shown to match the NESC ensemble; then
  they are ported to `embedded/`.
- The superseding ADR to ADR-0025's Finalization is in M6.
- Prototyping a model in Python waits (Later).

## Analysis and debugging tools

- **Plotting.** Time histories from the HDF5 run logs, and an easy way to define plot packages: named,
  reusable sets of plots for one purpose (longitudinal, lateral, controls, health, a debugging view)
  applied to any log. Output to files. The plotting dependency is Joe's decision. Architecture §1 marks
  visualization *planned* for M6 (PRs 5, 6 and 8); today only `astra.fgbridge` exists, and M4's trace SVGs
  came from a scratch script.
- **Monte Carlo visualization.** Distributions of each dispersed parameter's draws, draws against each
  other colored by outcome, and trajectory envelopes (percentile bands) across runs.
- **Correlating failures to draws.** `summary.parquet` already holds, per run, every drawn value, the
  status, the halt check, source and frame, and each requested signal's minimum and maximum, so this is
  Python over existing data. Candidate methods: Monte Carlo filtering (compare each parameter's draws
  between passing and failing runs, for example with a two-sample KS test, and rank them); rank
  correlation of draws against continuous margins (closest approach to a limit), which carries more
  information than pass/fail when most runs pass; a shallow decision tree to surface interactions as a
  readable rule. Report effect sizes and account for testing many parameters at once. Then one command
  reruns a flagged run from its seed with full logging and a plot package. Limits: the Monte Carlo is
  open loop today (closed-loop Monte Carlo is a PLAN.md open item), and trends need hundreds to thousands
  of runs.
- **Sizing a Monte Carlo.** A tool that recommends the number of runs from the question asked and the
  number of dispersed parameters: a bound on a failure probability (zero failures in n runs bounds it
  below about 3/n at 95 % confidence), the confidence interval on a probability or a percentile, or a
  sensitivity ranking (which needs more runs as parameters grow). Plus a convergence check after a batch:
  the estimate and its interval against run count.
- **Run report.** One command produces the plots, the check results and the provenance (commit, config,
  seed) for a scenario or a Monte Carlo batch, usable in a review packet.
- **Frequency domain and handling qualities** on top of `astra.linearize`: Bode plots, margins, and
  criteria from MIL-HDBK-1797 (in `docs/refs/textbooks`).
- **A generated reference** of every plugin's config keys.

## Controller tuning

A tool, not part of a 6-DoF run. `tests/python/autopilot_design.py` already computes closed-loop poles and
loop-at-a-time margins for the gains that ship.

- **Prerequisite:** the autopilot's gains are compiled constants in `embedded/autopilot/gains.hpp`. An
  optimizer needs them loaded at startup (a config file or an ICD parameter message) so it can iterate
  without a rebuild. This touches `embedded/` (ADR-0008's boundary) and possibly `icd/` (Tier 3).
- **Close the loop with the real control law,** not a Python copy of it: through the existing lockstep
  `Session`, or an in-process binding of `astra_autopilot_lib`. Tuning a replica drifts from what flies.
- **Levels:** spec-driven on the linear model (targets such as short-period damping, CAP, bandwidth and
  margins, solved for gains); time-domain on the nonlinear sim (rise time, overshoot and settling through
  the scenario harness); robustness (worst-case margins across Monte Carlo draws, and gain schedules over
  a trim and linearize grid).
- Adaptive control (MRAC, L1) fits as a second example controller rather than as part of the tool.

## Vehicles and external oracles

- **A second vehicle: an F-16.** Sources: Stevens & Lewis (in `docs/refs/textbooks`), the NESC study's
  F-16 in DAVE-ML (NASA publishes the model files; they are not in `docs/refs`), and JSBSim's F-16 for
  comparison. It tests whether the model interfaces hold for a different aircraft.
- **An external oracle for a whole aircraft.** NESC cases 11, 12, 13.1 to 13.4, 15 and 16 fly the F-16;
  `docs/refs/Atmospheric_checkcases` already holds three participating simulations' trajectories for each,
  and JSBSim took part in the study. The same ensemble-as-oracle pattern as M2's cases 1 to 10
  (ADR-0013). The maneuver cases fly the NESC's own control laws, defined in DAVE-ML (Vol II): 13.x use
  `F16_control.dml` (stability augmentation and a three-axis autopilot) and 15 and 16 use `F16_gnc.dml`.
  Case 17 (two-stage rocket to orbit) is out of scope by Joe's call.

## Model fidelity

- **Sensors** (architecture §19; they replace truth in `sim_to_sw`, so ICD v2 with timestamps and
  per-sensor rates). References: Groves and Titterton in `docs/refs/textbooks`.
  - IMU at several fidelity levels: white noise only; bias, bias instability, random walk, scale factor
    and misalignment; then quantization and vibration.
  - GNSS: PVT first; then pseudorange and Doppler, which need satellite ephemeris, a receiver clock model
    and atmospheric delays.
  - Barometric altimeter and air data (airspeed, possibly angle-of-attack and sideslip vanes).
  - Radar or laser altimeter, which needs a terrain model.
  - Magnetometer, which needs a magnetic field model (for example WMM).
  - Camera for visual odometry: no rendering in the sim. Start with a synthetic feature sensor (known
    landmarks projected with noise); bridge to an external renderer (Gazebo, Isaac) only if real images
    are needed.
- **Actuators:** a first-order lag, and a nonlinear second-order model with rate and position limits, as
  generic plugins with per-aircraft instances. Higher fidelity later. These give the deflection-range open
  item a home and are the trigger the throttle-jitter open item waits on.
- **Environment:** discrete 1-cosine gusts, wind shear and microbursts, non-standard (hot and cold) days,
  terrain, and a magnetic field model. Fuel burn with mass as state, and ground contact, remain in
  architecture §19.
- **Fault injection** as scenario events: sensor faults (bias, stuck, dropout), GNSS-denied, actuator
  faults (stuck, hardover), engine out. Adjacent to the wind-events open item.

## Embedded GNC example

- **A navigator:** an INS/GNSS EKF in `embedded/`, so the autopilot flies on estimated state. With truth,
  measurements and estimates all logged, a Monte Carlo can report estimator consistency (NEES, NIS), and
  a GNSS-denied fault shows real navigation drift.
- **Prototyping a model in Python:** architecture §12 notes there are no model-base trampolines, so a model
  can be selected and driven from Python but not written there.

## Real-time path

- A frame-time benchmark tracked over time (the margin at 1 kHz), before `RealTime` mode and its logger
  writer thread (PLAN.md open items). Monte Carlo throughput benefits from the same measurement.

## Verification and engineering health

- Coverage measurement (llvm-cov), to find untested paths before a reviewer does.
- Fuzzing YAML config parsing and ICD decode (libFuzzer ships with clang-18).
- A whole-tree gcc build: compiler-specific undefined behavior, and determinism across compilers.
- Gate time: the full gate took 1001 s from a fresh configure on the PR #75 candidate.

## Documentation

- A tutorial path: add a model, add an aircraft, write a scenario, run a Monte Carlo. Architecture §18
  promises this on-ramp; README.md's how-tos sketch each step, but no tutorial walks through it yet (M7).

## Workflow

- Codex review effort: Joe chose `high` for the user-level Codex `model_reasoning_effort` (2026-09-29).
  Record it in the workflow's "Model and effort" section, and have PR bodies record the effort beside the
  model for each Codex pass.
- Plan approval: have `/plan-feature` commit the plan as a draft, with Joe's approval recorded only on his
  explicit instruction (lesson 2026-09-29 on the refused plan-file write). Partly done by #83: a plan-mode
  draft still awaiting Joe's decision at `/wrap` is committed with an awaiting-approval status line
  (`docs/agents/claude-code.md`); `/plan-feature` itself does not commit a draft yet.

## Considered and set aside

- A PX4 or ArduPilot software-in-the-loop bridge (Joe, 2026-09-29): the stack is his own, and the embedded
  example already exercises the ICD seam.
- NESC case 17, the two-stage rocket (Joe, 2026-09-29): aircraft and atmospheric flight are the focus.
