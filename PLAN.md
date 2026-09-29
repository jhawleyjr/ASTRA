# PLAN — ASTRA v1 milestones

Authoritative acceptance criteria: `docs/architecture.md` §16. Edited in place; no task IDs.
This file holds the milestone checklist, the open items, and one line per PR:
`PR n (#NN): scope. Plan: <link>. Packet: <link>.` It records nothing that becomes true only at merge (merge
hashes, merge or approval dates, round counts, review narratives): git log and the PR are the record.
Code and ADR comments that cite a PLAN.md PR line mean the narrative from before this file was compacted, in
the PR titled "Workflow calibration": read it with `git show <that commit>^:PLAN.md`.

## Milestones

- [x] **M0 Bootstrap** — core + one dummy plugin build under all presets; loader loads by path, rejects a bad
      `core_api_version`, static-plugins build passes the same test; ICD static asserts hold under clang-18 and
      `gcc -std=c11`; hooks fire; LSPs resolve symbols; CI runs `just check` on PRs and `main` requires it;
      ADR-0001…0011 accepted. Evidence: the PRs below (no retained packet).
  - PR 1 (#1): bootstrap files, ADR-0001…0011, oracle-guard hook. Plan: none. Packet: none.
  - PR 2 (#2): CMake presets, `justfile`, core skeleton, plugin loader + dummy plugin, `icd/`, GitHub Actions
    (job `check`), hook test runner in `just check`. Plan: none. Packet: none.
  - PR 3 (#6): Python package (`astra._core` via nanobind, stubs), pyright/ruff/pytest, clangd config. Plan:
    none. Packet: none.
  - PR 4 (#9): Stop hook (fast check), Codex review wired, `check` required in the ruleset. Plan: none. Packet:
    none.
- [x] **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free energy/angular-momentum
      drift < 1e-9 over 1000 s, |q| within 1e-12, ballistic arc closed form, RK4 vs Euler order shown, zero
      allocations over 1000 frames; health monitor halts on injected NaN and broken quaternion, naming the source.
      Evidence: the PRs below and their tests under `tests/physics/` and `tests/unit/` (no retained packet).
  - PR 1 (#15): `core/include/astra/util/frames.hpp` (quaternion ↔ DCM ↔ 3-2-1 Euler, `q_nb` kinematics), the kernel
    `Integrator` (Euler, RK4, post-step renormalize); ADR-0001 sign-pinning test. Plan: none. Packet: none.
  - PR 2 (#16): `Config`, `SignalRegistry`, `VirtualClock`, `Scheduler`, `Vehicle`, `Simulation`; ctest labels
    `unit`/`physics`. Plan: none. Packet: none.
  - PR 3 (#19): `EarthModel` (`earth/flat`), `GravityModel` (`gravity/constant`), `MassPropertiesModel`
    (`mass/constant`), `Eom`; torque-free, ballistic and precession physics tests. Plan: none. Packet: none.
  - PR 4 (#21): `HealthMonitor` (NaN/Inf, |q| drift > 1e-6), typed halt reason, allocation counter. Plan: none.
    Packet: none.
- [x] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
      Evidence: all ten cases pass under all four presets, PR 3c's gate
      ([packet](docs/reviews/pr3c-substage-wrench-human.md), ADR-0016).
  - PR 1 (#22): `earth/{round, wgs84_rotating}`, `gravity/{inverse_square, wgs84_j2}`, position-representation
    check, `cppcoreguidelines-no-malloc`. Plan: none. Packet: none.
  - PR 1b (#24): `AtmosphereModel` + `atmosphere/isa1976`, ISA golden table. Plan: none. Packet: none.
  - PR 2 (#25): `WindModel` + `wind/uniform` + the `EnvironmentSample` latch; `Simulation::for_testing`. Plan:
    none. Packet: none.
  - PR 2b (#26): `core/include/astra/util/random.hpp` + `wind/dryden` (MIL-F-8785C), Dryden PSD test. Plan: none. Packet: none.
  - PR 2c (#27): per-frame domain validity (ADR-0012), plus ADR-0013. Plan: none. Packet: none.
  - PR 3a (#29): `EnvironmentSample::atmosphere`, `earth/round rotating`, `wind/table`, `tests/plugins/nesc_aero`.
    Plan: none. Packet: none.
  - PR 3b (#31): the NESC ten-case harness (ADR-0014). Spec: [spec](docs/specs/nesc-harness.md). Plan:
    [plan](docs/plans/nesc-harness.md). Packet: [packet](docs/reviews/nesc-harness-human.md).
  - ADR-0015 (#32): drop `tol_rel`, exclude SIM 2 from cases 4/5. Plan:
    [plan](docs/plans/nesc-tol-rel-removal.md). Packet: [packet](docs/reviews/nesc-tol-rel-human.md).
  - case05 gravity fix (#33): `gravity/inverse_square` gains the centripetal term (`rotating` key). Plan:
    [plan](docs/plans/nesc-case05-gravity-centripetal.md). Packet:
    [packet](docs/reviews/nesc-case05-gravity-centripetal-human.md).
  - PR 3c (#34): per-RK4-substage wrench evaluation (ADR-0016), closes cases 4/5. Plan:
    [plan](docs/plans/pr3c-substage-wrench.md). Packet: [packet](docs/reviews/pr3c-substage-wrench-human.md).
- [x] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise (10,000 m, 230 m/s
      TAS, ADR-0002 SI) with residual < 1e-6 (norm TBD, PR 6); linearized modes match design values,
      each mode against its own tolerance; a 300 s open-loop run matches its own frozen regression
      trajectory (not an oracle), independently cross-checked against the linearized model's
      phugoid-cycle prediction. §16's M3 row is corrected to this wording by PR 0. Evidence: trim residual,
      PR 6 (D8 max-abs 8.3e-17, still air); linearized modes, PR 7 (five modes within their own tolerance,
      central-difference `A`/`B`); regression trajectory + independent cross-check, PR 8 (300 s bitwise-frozen
      run, four presets; phugoid period/damping against PR 7's 5×5 `A` within 0.5%/0.004). Packets:
      [PR 6](docs/reviews/pr6-trim-human.md), [PR 7](docs/reviews/pr7-linearize-human.md),
      [PR 8](docs/reviews/pr8-cruise-regression-human.md).
  - PR 0 (#36): architecture.md not-yet-built markers, §16 M3 row corrected, `aircraft/**/physical|hardware` in
    workflow.md's tier-3 row. Plan: none (M3 decomposition, #35). Packet: none.
  - PR 1 (#37): `aircraft/_template/` + `aircraft/notional_transport/` scaffolding, `just new-aircraft`. Plan:
    none. Packet: [packet](docs/reviews/m3-pr1-scaffolding-human.md).
  - PR 2 (#38): control-input write seam, `InputRegistry` (ADR-0017). Plan: [plan](docs/plans/pr2-write-seam.md).
    Packet: [packet](docs/reviews/pr2-write-seam-human.md).
  - PR 3 (#39): `AeroModel`/`PropulsionModel` as `WrenchSource` specializations (ADR-0018). Plan:
    [plan](docs/plans/pr3-aero-propulsion.md). Packet: [packet](docs/reviews/pr3-aero-propulsion-human.md).
  - PR 3d (#40): framework realignment (ADR-0019/0020/0021) and ADR-0020's implementation; closes the
    atmosphere-domain-check misattribution window (ADR-0016's disclosed gap). Plans:
    [realignment](docs/plans/m3-framework-realignment.md),
    [implementation](docs/plans/pr3d-adr0020-implementation.md). Packet:
    [packet](docs/reviews/pr3d-adr0020-human.md).
  - PR 4 (#41): `notional_transport` aero + turbofan plugins (ADR-0020). Plan:
    [plan](docs/plans/pr4-notional-aero-turbofan.md) (written against ADR-0018's `compute()`, rebuilt on
    `wrench_b()`). Packet: [packet](docs/reviews/pr4-notional-aero-turbofan-human.md).
  - PR 4a (#42): ADR-0021 `DynamicsModel`, `RigidBody6DofDynamics`, two-phase `Vehicle` init, R1 golden. Plan:
    [plan](docs/plans/pr4a-dynamics-model.md). Packet: [packet](docs/reviews/pr4a-dynamics-model-human.md).
  - PR 5 (#43): nanobind bindings (`Simulation`, `Config`, `SignalRegistry`, write seam, `derivative`). Plan:
    [plan](docs/plans/pr5-nanobind-bindings.md). Packet: [packet](docs/reviews/pr5-nanobind-bindings-human.md).
  - PR 6 (#44): `astra.trim`. Plan: [plan](docs/plans/pr6-trim.md). Packet: [packet](docs/reviews/pr6-trim-human.md).
  - PR 7 (#45): `astra.linearize`, classical mode check. Plan: [plan](docs/plans/pr7-linearize.md). Packet:
    [packet](docs/reviews/pr7-linearize-human.md).
  - PR 8 (#46): cruise config, 300 s regression trajectory, phugoid cross-check. Plan:
    [plan](docs/plans/pr8-cruise-regression.md). Packet: [packet](docs/reviews/pr8-cruise-regression-human.md).
  - PR 9 (#47): M3 closeout; `icd/astra_icd.h` states the elevator/aileron/rudder positive-deflection sense
    directly (comment-only). Plan: none. Packet: none.
  - PR 10 (#48): fresh-checkout `tidy` fix (module scanning off, fresh-configure regression). Plan:
    [plan](docs/plans/pr10-fresh-tidy.md). Packet: [packet](docs/reviews/pr10-fresh-tidy-human.md).
- [x] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout. Evidence per clause (§16's M4 row is authoritative; it adds moderate Dryden as a
      second gating level): [PR 6 packet](docs/reviews/m4-pr6-lockstep-acceptance-human.md) and the
      `docs/reviews/evidence/4e418e1-*` logs (`4e418e1-full-gate.log`, `4e418e1-a6-pytest.log`,
      `4e418e1-a6-ctest-asan.log`, `4e418e1-a6-reports.json`, `4e418e1-r4-timings.json`). Spec and plan:
      [plan](docs/plans/m4-lockstep.md).
  - PR 1 (#51): ADR-0022/0023/0024 and architecture rev 8. Plan: [plan](docs/plans/m4-lockstep.md). Packet:
    [packet](docs/reviews/m4-pr1-adrs-human.md).
  - PR 2 (#52): ICD truth packing (`icd_pack.hpp`, `status_flags` bits, oracle tests; R12a). Plan:
    [plan](docs/plans/m4-lockstep.md). Packet: [packet](docs/reviews/m4-pr2-icd-pack-human.md).
  - PR 3 (#53): core seam (`Transport`, exchange stage, loopback, recorder, `kTransport`). Plan:
    [plan](docs/plans/m4-pr3-core-seam.md). Packet: [packet](docs/reviews/m4-pr3-core-seam-human.md).
  - PR 4 (#55): shm transport and HAL shm backend. Plan: [plan](docs/plans/m4-pr4-shm-transport.md). Packet:
    [packet](docs/reviews/m4-pr4-shm-transport-human.md).
  - PR 5 (#56): `embedded/autopilot` and the standalone embedded build. Plan:
    [plan](docs/plans/m4-pr5-autopilot.md). Packet: [packet](docs/reviews/m4-pr5-autopilot-human.md).
  - PR 6 (#57): turbulence configs, `astra.lockstep` driver, the M4 acceptance runs (R1 to R4b, R12b). Plan:
    [plan](docs/plans/m4-pr6-lockstep-acceptance.md). Packet:
    [packet](docs/reviews/m4-pr6-lockstep-acceptance-human.md).
  - PR 7 (#58): M4 closeout (check-off, duplicate §15 bullet removed, M4 follow-ups, architecture §7/§10 status
    and `Transport` sketch). Plan: [plan](docs/plans/m4-lockstep.md). Packet: none.
- [x] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test. Spec and plan: [plan](docs/plans/m5-tooling.md).
      Evidence, clause by clause:
      - HDF5 logging and replay: PR 4
        (`ctest --preset asan -R 'RunLog|Hdf5|NoAlloc|Health|SimulationTransport'`, 119 passed; R1
        `ctest --preset release -R NotionalTransportCruiseRegression`, 2 passed; R4 byte-identical Dryden
        logs —
        [summary](docs/reviews/evidence/baddd0a-gate-summary.log)) and PR 5 (`pytest tests/python -k
        'log or replay'`, 34 passed — [summary](docs/reviews/evidence/3ff2eac-gate-summary.log); the R9
        replay cases in the PR body);
      - the scenario harness over `tests/scenarios/`: PR 6 (`pytest tests/scenarios tests/python -k
        scenario`, 64 passed — [summary](docs/reviews/evidence/63777fc-gate-summary.log); the Q15 seed
        scenarios and bands in the PR body);
      - the 64-run Monte Carlo under `just check`: PR 7 (`pytest tests/python -k montecarlo`, 58 passed
        including `test_montecarlo_q19_64_runs_complete`, and collected by `just pytest`'s gate run, 333
        passed — [summary](docs/reviews/evidence/b41bdcb-gate-summary.log); the Q19 case in the PR body);
      - FlightGear packets verified by a decoder test: PR 8 (`pytest tests/python -k fgbridge`, 29 passed —
        [summary](docs/reviews/evidence/5d51da9-gate-summary.log); the table-built decoder over a real UDP
        socket in the PR body).
  - Planning (#62): M5 spec, plan and nine-PR decomposition; Q1 to Q24 decided by Joe. Plan:
    [plan](docs/plans/m5-tooling.md). Packet: none.
  - PR 1 (#63): ADR-0025 (run log and replay), ADR-0026 (derived signals), architecture rev 9, AGENTS.md
    wall-clock scope. Plan: [plan](docs/plans/m5-tooling.md). Packet: in the PR body.
  - PR 2 (#64): HDF5 RAII wrapper in `core/signals/` (`Hdf5Error`, time tracking off, version bounds fixed at
    1.8 to 1.10), byte-identity and LSan tests. Plan: [plan](docs/plans/m5-tooling.md). Packet: in the PR body.
  - PR 3 (#65): derived flight-condition signals `v<i>.derived.*` (ADR-0026), a separate frame-start
    `pack_sim_to_sw` per physics vehicle; exchange untouched (R10). Plan: [plan](docs/plans/m5-tooling.md).
    Packet: in the PR body.
  - PR 4 (#66): run log stage in `Simulation` (ADR-0025): `log` block, rings reserved before side effects,
    sticky `kLog`, drain/`flush_log`/`close_log`, provenance, `/plugins`, `/writes`, `/summary`,
    `/final_state`; bindings and `astra.lockstep.Session`. Plan: [plan](docs/plans/m5-tooling.md). Packet: in
    the PR body.
  - PR 5 (#67): `astra.log`: `load` (pandas per rate group; refuses a log without `complete = 1` or inconsistent
    with its `/summary`), `replay` (byte identity; refuses transport and `LOG`-halted logs); h5py and pandas
    pinned. Plan: [plan](docs/plans/m5-tooling.md). Packet: in the PR body.
  - PR 6 (#68): `astra.scenario` and `tests/scenarios/` (one item per YAML; invalid is a collection error),
    `cruise_still_air.yaml`, five Q15 seed scenarios; reads each run's own log strictly (frames_run, halt,
    warn counters: the finalization open item's consumer assertion). Plan: [plan](docs/plans/m5-tooling.md).
    Packet: in the PR body.
  - PR 7 (#69): `astra.montecarlo` (spawn pool, SHA-256 per-run seeds, in-house sampler, `summary.parquet`;
    halts are rows, failures are `error` rows, a dead worker fails the batch), helpers shared with
    `astra.scenario` in `astra._runkit`, pyarrow pinned, the Q19 64-run case under `just check`. Plan:
    [plan](docs/plans/m5-tooling.md). Packet: in the PR body.
  - PR 8 (#70): `astra.fgbridge` (FGNetFDM v24 over UDP from a live `Simulation` or a strictly loaded log, one
    packet per `frame % divisor == 0`, flat Earth refused), the transcribed table
    `tests/golden/flightgear/fgnetfdm_v24.py` (FlightGear 2024.1.7) and its by-hand g++ cross-check
    `scripts/fgnetfdm_layout_check.py`, a table-built decoder test over a real socket. Plan:
    [plan](docs/plans/m5-tooling.md). Packet: in the PR body.
  - PR 9 (#71): M5 closeout: PLAN.md check-off with evidence links, open items for the held-out scope
    (writer thread, CLI, recorder retirement, wind events, closed-loop scenarios/Monte Carlo, lockstep
    replay, bridge pacing), the finalization item extended with the F1 residual, crash-readable log and
    load cross-check points, architecture §12 status. Plan: [plan](docs/plans/m5-tooling.md). Packet: in the
    PR body.
  - PR 10 (#72): post-M5 cleanup: the wall-clock symbol scan (`scripts/wallclock_scan.py`, `just wallclock-scan`,
    in `just check`; ADR-0023's two sites allowlisted), `astra.fgbridge` pacing on by default, Follow-up A's
    comment fixes in `tests/unit/icd/icd_pack_test.cpp`, and the Finalization item's F1 citation; those three
    open items (wall-clock symbol scan, `astra.fgbridge` pacing, Follow-up A) are removed. Plan:
    [plan](docs/plans/m5-pr10-cleanup.md). Packet: in the PR body.
  - PR 11 (#73): `astra_add_plugin_test()`: a test executable opts into the plugin build order with one call in
    its own `CMakeLists.txt`, and `astra_finalize_plugins()`'s hardcoded test list is gone. Every listed target
    migrated; `aircraft/_template/tests/` carries the template's own tests, so a copy from `just new-aircraft`
    builds and runs them with no framework edit (architecture §18); `tests/python/test_new_aircraft_build.py`
    builds and tests such a copy from a clean directory, shared and static. The open item on that list is
    removed. Plan: [plan](docs/plans/plugin-test-helper.md). Packet: in the PR body.

## Open items

- [ ] Earth/gravity rotation-consistency check at init (origin: M2 case05 fix; deferred by Joe, 2026-09-19) —
      `docs/plans/nesc-case05-gravity-centripetal.md`'s D2(b), deferred (D2(a), disclosed only, for that task).
      Two independently-configured plugins can each pass their own init-time validation while disagreeing about
      whether Earth rotates: `gravity/inverse_square`'s `rotating` config key must be set consistently with
      `earth/round`'s own `rotating` by hand, with nothing enforcing it (exactly the shape of the case05 bug,
      restated as a residual, not eliminated). Covers **both** mismatch directions: `earth/round rotating: true`
      paired with a non-rotating `gravity/inverse_square` (the actual bug), and the symmetric, currently-latent
      case, `gravity/wgs84_j2`'s own *unconditional* rotation paired with a non-rotating Earth model (no current
      config does this, so it is latent, not manifest). Needs a new `core/` interface path (an
      `EarthModel`/`GravityModel` accessor pair, mirroring the existing
      `assumed_position_representation()`/`position_representation()` check already in `Simulation::from_config`)
      — a real, if modest, interface change, not a `plugins/physical`-only fix; plan-review before code. Not
      started.
- [ ] `plugins/physical/mass/constant`'s `ixy`/`ixz`/`iyz` config keys have no documented sign convention
      (origin: M3 PR 1 review): the plugin places them directly, unnegated, into the inertia tensor's
      off-diagonal entries, which only matches the standard aerospace convention (tensor entry =
      `-integral(x*z*dm)`) if the config value is already meant as that negated tensor entry rather than the
      product of inertia itself. `aircraft/_template`'s and `aircraft/notional_transport`'s own `physical/mass`
      plugins (M3 PR 1) document and use the product-of-inertia convention explicitly instead; the shared
      plugin's keys are unchanged and still undocumented. Not started.
- [ ] Elevator/throttle deflection *range* has no declared home (origin: M3 PR 6 planning,
      `docs/plans/pr6-trim.md`, D10): the second control-surface property (after the positive-deflection sense,
      resolved by M3 PR 9) with no config key or ICD field to read it from. PR 6's own plausibility check
      hardcodes `elevator ∈ [-0.3, 0.3]` rad (737.xml) and `throttle ∈ [0, 1]` (the ICD's documented convention,
      not a config value) directly in its test rather than adding config keys. Narrowed by M3 PR 9: per-aircraft
      surface limits live with the aircraft's own actuator model (M4+), not the ICD — `astra_sw_to_sim_v1`'s
      `elevator`/`aileron`/`rudder`/`throttle` fields carry a commanded angle the sim does not clamp in v1; the
      ICD does not carry limits. Not started.
- [ ] `astra.trim`'s own hardcoded control-input names (origin: M3 PR 7 planning; D10, PR 6,
      `docs/plans/pr6-trim.md`) are not retrofitted to `InputRegistry::names()` by PR 7's own binding of it: PR 7
      reads control names through it for `astra.linearize` only, and `trim_straight_and_level` keeps its
      literals. Not started.
- [ ] `astra.linearize`'s 9-state `A` (PR 7, D11) omits altitude (origin: M3 PR 8 finding,
      `docs/plans/pr8-cruise-regression.md` finding 4; owner: Joe's call): position is held fixed, so `A` has no
      d/dh and no density/thrust-lapse coupling. Against a nonlinear open-loop run from the trimmed cruise,
      `modes(A).phugoid`'s damped period is 125.3 s against a measured 108.6 s (15% long; the measured period is
      13% short of it) and its zeta 0.052 against 0.027; `A` plus one altitude column and the h_dot row matches
      (108.5 s, 0.027), as S&L Example 3.8-5 describes. PR 8 builds that 5x5 in its own test only. An opt-in
      altitude state in `astra.linearize` itself would be a D11 revision — Joe's call, not scheduled. Not
      started.
- [ ] `tests/golden/dynamics/capture_r1_golden.cpp` (the R1/R1-Sim capture harness, M3 PR 4a) is stale (origin:
      M3 PR 10 finding; owner: Joe): it includes `astra/kernel/eom.hpp` and uses
      `astra::Eom`/`post_step_renormalize`, all retired by ADR-0021 in PR 4a itself. A future R1 re-capture needs
      a source fix to that harness under Joe's authorization (it is under `tests/golden/**`), not just re-adding
      `add_subdirectory(golden/dynamics)` as `tests/CMakeLists.txt`'s comment implies. Its script
      `capture_r1_golden.sh` also has no arming check of its own (unlike PR 8's); today it refuses at its
      clean-tree and baseline-drift (`26ad594`) steps, before its first commit. Not started.
- [ ] ADR-0002's literal "no unit suffixes in identifiers (`altitude`, not `altitude_m`)" text (origin: M3 PR 3,
      ADR-0018) vs. `AtmoState`'s own field names (`temperature_k`, `pressure_pa`, `density_kg_m3`,
      `speed_of_sound_m_s`): whether the rule means signal-name strings specifically (its own example,
      `registry.add("aero.CL", ...)`, is a string) or every C++ identifier is unresolved; PR 3's own plan accepted
      the unit-suffix precedent rather than settling the wording. `AeroInputs`/`PropulsionInputs` (`alpha_rad`,
      `dynamic_pressure_pa`, `true_airspeed_mps`) are gone under ADR-0020; `AtmoState`'s field names are
      ADR-0020's cited precedent for whatever unit-suffixed fields a concrete plugin's own
      `air_data.hpp`-derived, plugin-side `Inputs`-shaped struct uses instead; the wording question is otherwise
      unchanged. Not started.
- [ ] §15 envelope monitors (α, β, Mach, dynamic pressure, load-factor limits declared by the aero model)
      (origin: deferred from M3 PR 3, ADR-0018): no declaration hook exists yet on `AeroModel`, because no concrete
      plugin had real numbers to declare before M3 PR 4. A later PR adds
      the declaration virtual (a further `kCoreApiVersion` bump, precedented) and wires `HealthMonitor`'s own
      per-frame check against it. Not started.
- [ ] Moderate-turbulence throttle jitter (origin: M4 PR 6; Joe 2026-09-26, nonblocking): airspeed gusts pass
      through the autopilot's speed loop to the throttle, up to 0.019 per exchange at moderate (throttle 0.310 to
      0.633 around trim 0.4986), no limit reached (`docs/reviews/m4-pr6-lockstep-acceptance-human.md`). Revisit
      when a sensor or actuator model lands. Not started.
- [ ] Remove the `OPTIONAL_GAIN_NAMES`/`kSideslip` support from `tests/python/autopilot_design.py` (origin: M4
      PR 5 follow-up, carried by PR 6; owner: whoever next touches the gains): `gains.hpp` defines no `kSideslip`
      and no sideslip term was needed at M4 acceptance. Not started.
- [ ] Follow-up B (origin: M4 PR 2; Tier 0, authorized by Joe; Joe arms the golden guard for that session):
      correct the equation citations in `core/include/astra/util/frames.hpp` and in the comments of
      `tests/golden/frames/adr0001_cases.hpp` (comment-only, no data change): DCM Eq. (1.3-10), Euler from DCM
      Eq. (1.3-11), half-angle Euler-to-quaternion Example 1.8-1 Eq. (1) p. 52, q to DCM Eq. (1.8-16)/(1.8-18).
      Not started.
- [ ] Follow-up C (origin: M4 PR 3; Joe 2026-09-25, not scheduled): `LockstepExchange` recorder sizing for an
      absurd `sim.frames`: preallocation throws `std::bad_alloc` at init instead of a `ConfigError` naming
      `sim.frames`, and `sim.frames` near `UINT64_MAX` wraps the capacity arithmetic in its constructor
      (`docs/reviews/m4-pr3-core-seam-human.md`). Not started.
- [ ] Wall-clock scan, timed I/O waits (origin: M5 PR 10 review, Codex thread `01a0eb14-1425-7c02-bb04-44d54eb9960e`;
      Joe's stop rule, 2026-09-28): `scripts/wallclock_scan.py`'s `multiplexing` family covers `select`, `pselect`,
      `poll`, `ppoll`, `epoll_wait` and `epoll_pwait`, but not three more members of it that take a timeout:
      `epoll_pwait2`, `aio_suspend` (and `aio_suspend64`) and `recvmmsg`. Each shows as an undefined symbol under
      `nm -u -C` on clang++-18 and passes the scan today. Add anchored patterns and one compiled case per family.
      The scan stays a tripwire, not a proof (its docstring and architecture §15 list the blind spots). Not started.
- [ ] Evaluate Jev (TypeSafe AI) for workflow routing: model/effort selection at session launch and context
      selection, advisory with today's fixed settings as the fallback and file-path rules for Tier 3 kept
      (origin: Joe, 2026-09-26; owner: Joe, starting with console.typesafe.ai signup).
- [ ] One superseding ADR to ADR-0025's Finalization (origin: M5 PR 5; Joe 2026-09-27; extended by M5 PR 9,
      2026-09-28; Tier 3, scheduling is Joe's call), covering four points:
      a. record how a run log was finalized: `close_log()` versus the `Simulation` destructor, so
         `astra.log.load` can flag, not refuse, a destructor-finalized log (`Session` relies on the destructor
         path). A driver that raises mid-run leaves its `Simulation` to GC and the destructor writes a
         consistent file with `complete = 1`, which `load`'s checks cannot tell apart. Until this lands, the
         PR 6 and PR 7 consumers assert `frames_run == duration/dt` and no unexpected halt;
      b. PR 4 review finding F1's residual (`docs/reviews/m5-pr4-run-log-review.md`, Finding F1): the
         file's own close can fail after it writes `complete = 1`, which Joe accepts as a disclosed residual
         (2026-09-28). The fix is a rename-on-complete protocol: write to a temporary name and rename to
         `log.path` only after a successful close, so a file at the final path is complete by construction;
         this also covers a killed run;
      c. a crash-readable log: the writer keeps HDF5 metadata in memory until `close_log()`, so a killed or
         failed run leaves a file no reader can open, and `load`'s `allow_incomplete` never recovers a real
         run. Needs a flush at drain;
      d. minor: `load`'s optional cross-checks, `final_state.frame == frames_run` and the rate groups' signal
         names against `/summary`'s min/max names. These catch only hand-edited files.
      Not started.
- [ ] `RealTime` mode's logger writer thread (origin: M5 plan §9/architecture §9, held-out scope; Joe's call):
      M5's logger drains only inline (`AsFastAsPossible`); `RealTime` mode itself is not built, so the
      separate non-RT drain thread architecture §9 describes has nothing to run on yet. Not started.
- [ ] A CLI, `astra run` with `--strict` (origin: M5 plan Q24, architecture §15/§18; Joe's call): §16's M5 row
      does not require it; `--strict` exits non-zero on any halt or nonzero warn counter, as the scenario
      harness already does internally. Not started.
- [ ] Retire the M4 `ExchangeRecorder` once a lockstep test compares HDF5 logs instead (origin: M5 plan Q12,
      ADR-0022; Joe's call): kept through M5 because M4's byte-identity acceptance tests
      (`closed_loop_acceptance_test.cpp`) read it; M5's log already carries `v0.controls.*` and `transport.*`
      for lockstep runs. Retiring it would also moot Follow-up C's recorder-sizing item above. Not started.
- [ ] Wind events in scenarios (origin: M5 plan Q14; Joe's call): `astra.scenario` events are `write_input`
      only; there is no write seam for wind, so a wind change is a config choice (for example `wind/uniform`),
      not an event, until a seam exists. Not started.
- [ ] Closed-loop (autopilot) scenarios and Monte Carlo (origin: M5 plan Q16; Joe's call): not in M5, since
      adding the autopilot peer means subprocess management in the harness; M4's own acceptance already covers
      closed loop. Not started.
- [ ] Replaying a lockstep run (origin: M5 plan Q11; Joe's call): `astra.log.replay` refuses a log with a
      `transport` block, since replaying commands without the peer is a different feature. Not started.

## Recorded only here

Decisions and disclosures from the compacted narratives that no plan, packet or ADR holds:

- M2 PR 2: `Simulation::for_testing` is public on the shipped `Simulation` class, accepted explicitly rather than
  gated behind a test-only build; the guard is its name and doc comment (`simulation.hpp`). Re-decide if a
  Python binding or driver executable could reach it.
- M2 PR 2: `WindModel`/`GravityModel::assumed_position_representation()` is declarative, not enforced: a plugin
  can declare `kAny` while reading `vs.p_n()` (`wind_model.hpp`). A real gate is a separate design decision.
- M2 PR 2b: the Dryden PSD test drives `begin_frame`/`wind_n` with a synthetic, held-constant `VehicleState`,
  because MIL-F-8785C's PSD is stationary and meaningful only at fixed V and h (`dryden_psd_test.cpp`).
- M2 PR 2c: `check_domain`'s lookahead is first-order in velocity. Within about 4.9 µm of a boundary
  (0.5·|a|·dt² at standard gravity and dt = 1 ms, pinned by `simulation_halt_test.cpp`) and at near-zero
  closing velocity, a halt can come one frame late as `kNonFiniteState` instead of `kOutOfDomain`. An
  acceleration-aware lookahead needs its own plan-reviewed PR (ADR-0012 pins the Euler formula).
- M4: Joe rendered and checked the trace SVGs during his PR 6 review; this supersedes the PR 6 packet's "could
  not render them" note.
