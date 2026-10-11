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
  - PR 1 (#1): bootstrap files, ADR-0001…0011, oracle-guard hook. Plan:
    [plan](docs/plans/m0-kickoff.md). Packet: none.
  - PR 2 (#2): CMake presets, `justfile`, core skeleton, plugin loader + dummy plugin, `icd/`, GitHub Actions
    (job `check`), hook test runner in `just check`. Plan:
    [plan](docs/plans/m0-pr2-build-core-skeleton.md). Packet: none.
  - PR 3 (#6): Python package (`astra._core` via nanobind, stubs), pyright/ruff/pytest, clangd config. Plan:
    [plan](docs/plans/m0-pr3-python-package.md). Packet: none.
  - PR 4 (#9): Stop hook (fast check), Codex review wired, `check` required in the ruleset. Plan:
    [plan](docs/plans/m0-pr4-stop-hook-ci.md). Packet: none.
- [x] **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free energy/angular-momentum
      drift < 1e-9 over 1000 s, |q| within 1e-12, ballistic arc closed form, RK4 vs Euler order shown, zero
      allocations over 1000 frames; health monitor halts on injected NaN and broken quaternion, naming the source.
      Evidence: the PRs below and their tests under `tests/physics/` and `tests/unit/` (no retained packet).
  - PR 1 (#15): `core/include/astra/util/frames.hpp` (quaternion ↔ DCM ↔ 3-2-1 Euler, `q_nb` kinematics), the kernel
    `Integrator` (Euler, RK4, post-step renormalize); ADR-0001 sign-pinning test. Plan:
    [plan](docs/plans/m1-pr1-frames-integrator.md). Packet: none.
  - PR 2 (#16): `Config`, `SignalRegistry`, `VirtualClock`, `Scheduler`, `Vehicle`, `Simulation`; ctest labels
    `unit`/`physics`. Plan: [plan](docs/plans/m1-pr2-config-scheduler.md). Packet: none.
  - PR 3 (#19): `EarthModel` (`earth/flat`), `GravityModel` (`gravity/constant`), `MassPropertiesModel`
    (`mass/constant`), `Eom`; torque-free, ballistic and precession physics tests. Plan:
    [plan](docs/plans/m1-pr3-earth-gravity-mass.md). Packet: none.
  - PR 4 (#21): `HealthMonitor` (NaN/Inf, |q| drift > 1e-6), typed halt reason, allocation counter. Plan:
    [plan](docs/plans/m1-pr4-health-monitor.md). Packet: none.
- [x] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
      Evidence: all ten cases pass under all four presets, PR 3c's gate
      ([packet](docs/reviews/pr3c-substage-wrench-human.md), ADR-0016).
  - PR 1 (#22): `earth/{round, wgs84_rotating}`, `gravity/{inverse_square, wgs84_j2}`, position-representation
    check, `cppcoreguidelines-no-malloc`. Plan: [plan](docs/plans/m2-pr1-earth-gravity.md). Packet: none.
  - PR 1b (#24): `AtmosphereModel` + `atmosphere/isa1976`, ISA golden table. Plan:
    [plan](docs/plans/m2-pr1b-atmosphere.md). Packet: none.
  - PR 2 (#25): `WindModel` + `wind/uniform` + the `EnvironmentSample` latch; `Simulation::for_testing`. Plan:
    [plan](docs/plans/m2-pr2-wind.md). Packet: none.
  - PR 2b (#26): `core/include/astra/util/random.hpp` + `wind/dryden` (MIL-F-8785C), Dryden PSD test. Plan:
    [plan](docs/plans/m2-pr2b-random-units-dryden.md). Packet: none.
  - PR 2c (#27): per-frame domain validity (ADR-0012), plus ADR-0013. Plan:
    [plan](docs/plans/m2-pr2c-domain-validity.md). Packet: none.
  - PR 3a (#29): `EnvironmentSample::atmosphere`, `earth/round rotating`, `wind/table`, `tests/plugins/nesc_aero`.
    Plan: [plan](docs/plans/m2-pr3-nesc-split.md). Packet: none.
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
  - PR 1 (#51): ADR-0022/0023/0024 and architecture rev 8. Plans:
    [milestone](docs/plans/m4-lockstep.md), [PR](docs/plans/m4-pr1-adrs.md). Packet:
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
    wall-clock scope. Plans: [milestone](docs/plans/m5-tooling.md), [PR](docs/plans/m5-pr1-adrs.md). Packet: in
    the PR body.
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
    pinned. Plans: [milestone](docs/plans/m5-tooling.md), [PR](docs/plans/m5-pr5-log-load-replay.md). Packet:
    in the PR body.
  - PR 6 (#68): `astra.scenario` and `tests/scenarios/` (one item per YAML; invalid is a collection error),
    `cruise_still_air.yaml`, five Q15 seed scenarios; reads each run's own log strictly (frames_run, halt,
    warn counters: the finalization open item's consumer assertion). Plans:
    [milestone](docs/plans/m5-tooling.md), [PR](docs/plans/m5-pr6-scenario.md). Packet: in the PR body.
  - PR 7 (#69): `astra.montecarlo` (spawn pool, SHA-256 per-run seeds, in-house sampler, `summary.parquet`;
    halts are rows, failures are `error` rows, a dead worker fails the batch), helpers shared with
    `astra.scenario` in `astra._runkit`, pyarrow pinned, the Q19 64-run case under `just check`. Plans:
    [milestone](docs/plans/m5-tooling.md), [PR](docs/plans/m5-pr7-montecarlo.md). Packet: in the PR body.
  - PR 8 (#70): `astra.fgbridge` (FGNetFDM v24 over UDP from a live `Simulation` or a strictly loaded log, one
    packet per `frame % divisor == 0`, flat Earth refused), the transcribed table
    `tests/golden/flightgear/fgnetfdm_v24.py` (FlightGear 2024.1.7) and its by-hand g++ cross-check
    `scripts/fgnetfdm_layout_check.py`, a table-built decoder test over a real socket. Plans:
    [milestone](docs/plans/m5-tooling.md), [PR](docs/plans/m5-pr8-fgbridge.md). Packet: in the PR body.
  - PR 9 (#71): M5 closeout: PLAN.md check-off with evidence links, open items for the held-out scope
    (writer thread, CLI, recorder retirement, wind events, closed-loop scenarios/Monte Carlo, lockstep
    replay, bridge pacing), the finalization item extended with the F1 residual, crash-readable log and
    load cross-check points, architecture §12 status. Plans: [milestone](docs/plans/m5-tooling.md),
    [PR](docs/plans/m5-pr9-closeout.md). Packet: in the PR body.
  - PR 10 (#72): post-M5 cleanup: the wall-clock symbol scan (`scripts/wallclock_scan.py`, `just wallclock-scan`,
    in `just check`; ADR-0023's two sites allowlisted), `astra.fgbridge` pacing on by default, Follow-up A's
    comment fixes in `tests/unit/icd/icd_pack_test.cpp`, and the Finalization item's F1 citation; those three
    open items (wall-clock symbol scan, `astra.fgbridge` pacing, Follow-up A) are removed. Plan:
    [plan](docs/plans/m5-pr10-cleanup.md). Packet: in the PR body.
  - PR 11 (#74): Follow-up B: Stevens & Lewis equation citations corrected (comments only, Tier 0) in
    `core/include/astra/util/frames.hpp` and the golden header `tests/golden/frames/adr0001_cases.hpp`, and the
    half-angle citation in four Python files (scope widened by Joe); the Follow-up B open item is removed.
    Plan: [plan](docs/plans/follow-up-b-citations.md). Packet: in the PR body.
  - PR 12 (#73): `astra_add_plugin_test()`: a test executable opts into the plugin build order with one call in
    its own `CMakeLists.txt`, and `astra_finalize_plugins()`'s hardcoded test list is gone. Every listed target
    migrated; `aircraft/_template/tests/` carries the template's own tests, so a copy from `just new-aircraft`
    builds and runs them with no framework edit (architecture §18); `tests/python/test_new_aircraft_build.py`
    builds and tests such a copy from a clean directory, shared and static. The open item on that list is
    removed. Plan: [plan](docs/plans/plugin-test-helper.md). Packet: in the PR body.
  - PR 13 (#75): the plugin-test guard: configure fails, shared and static, for an executable compiled with
    `ASTRA_TEST_PLUGIN_DIR` that is not registered with `astra_add_plugin_test()`, naming the target and its
    directory (`astra_finalize_plugins()` walks every directory's targets). `tests/embedded`'s
    `astra_autopilot_process_tests` is registered; `tests/python/test_new_aircraft_build.py` configures a copy
    with the template's call removed and expects the refusal. The open item on the omission is removed. Plan:
    [plan](docs/plans/plugin-test-guard.md). Packet: in the PR body.

M6 to M11 were mapped from [future work](docs/future-work.md) by Joe on 2026-09-29, in this order. Each still
gets its own spec, plan and architecture §16 row at its planning step. Open items carry the milestone that
takes them as a prefix.

- [x] **M6 Analysis tooling** — plotting with reusable plot packages over run logs; Monte Carlo visualization
      (draws, outcomes, trajectory envelopes); failure-to-draw correlation over `summary.parquet`; a tool that
      sizes a Monte Carlo from the question asked and the number of dispersed parameters; a run report with
      provenance; a frame-time benchmark; `astra run --strict`; the superseding ADR to ADR-0025's
      Finalization (Tier 3). Detail: [future work](docs/future-work.md#analysis-and-debugging-tools). Spec and
      plan: [plan](docs/plans/m6-analysis-tooling.md). Evidence, clause by clause. Each summary is its PR's last
      gated candidate (`check-full.sh` exit 0, `check: OK`; CTest counts are debug/release/static-plugins/asan),
      and the R-IDs are the plan's:
      - plot packages over run logs: PR 5 (R1 to R4, R3b; CTest 658/659/660/661, pytest 1314 passed —
        [summary](docs/reviews/evidence/8126aff-gate-summary.log); `-k plot` 110 passed in the PR body),
        refined by PR 12 (the TAS/CAS panels and equal aspect; pytest 1834 passed —
        [summary](docs/reviews/evidence/02af820-gate-summary.log)) and PR 14 (the plotly page's size; pytest
        1863 passed — [summary](docs/reviews/evidence/5a08393-gate-summary.log));
      - Monte Carlo visualization: PR 6 (R5; CTest 658/659/660/661, pytest 1402 passed —
        [summary](docs/reviews/evidence/3a09f45-gate-summary.log); `-k mc_analysis` 75 passed in the PR body),
        with the envelope's size in PR 8b (pytest 1698 passed —
        [summary](docs/reviews/evidence/efbac92-gate-summary.log)) and its failing-run overlays in PR 13
        (pytest 1843 passed — [summary](docs/reviews/evidence/74b6505-gate-summary.log)), each keeping R5;
      - failure-to-draw correlation over `summary.parquet`: PR 6 (R6 to R8, R7b, R10; the same summary) and
        PR 7b (R9, the planted cause in the notional transport with its negative control; CTest
        669/670/671/672, pytest 1485 passed — [summary](docs/reviews/evidence/f184b4c-gate-summary.log),
        [timing](docs/reviews/evidence/f184b4c-planted-timing.log); the `-k planted` runs in the PR body);
      - sizing a Monte Carlo from the question and the number of dispersed parameters: PR 7 (R11, R12; pytest
        1479 passed — [summary](docs/reviews/evidence/80f239e-gate-summary.log); `-k mc_sizing` 77 passed in
        the PR body);
      - a run report with provenance: PR 8 (R13; pytest 1687 passed —
        [summary](docs/reviews/evidence/02152f2-gate-summary.log); `-k report` 228 passed in the PR body);
      - a frame-time benchmark: PR 9 (R19, and R19b's end-to-end figure tracked; pytest 1402 passed —
        [summary](docs/reviews/evidence/100f491-gate-summary.log); the smoke, 11/11, in the PR body);
      - `astra run --strict`: PR 3 (R14; CTest 649/650/651/652, pytest 579 passed —
        [summary](docs/reviews/evidence/e1a8dec-gate-summary.log); `-k cli` 72 passed in the PR body);
      - the superseding ADR to ADR-0025's Finalization (Tier 3): PR 1 (ADR-0027; CTest 621/622/623/624,
        pytest 473 passed — [summary](docs/reviews/evidence/574f9fe-gate-summary.log); Joe's two docs-only
        commits after the gate, waived, in the PR body), implemented by PR 2 (R16 to R18b, R19b; CTest
        649/650/651/652, pytest 510 passed — [summary](docs/reviews/evidence/ede0cf3-gate-summary.log); the
        acceptance suites in the PR body).

      Beyond the row: PR 4's units (R21; pytest 1204 passed —
      [summary](docs/reviews/evidence/2de663a-gate-summary.log)) and PR 11's PyYAML route (pytest 1822 passed —
      [summary](docs/reviews/evidence/53db70f-gate-summary.log); #99 merged after the gate, in the PR body). R15
      is split across PR 3 (`run`, `trim`), PR 5 (`plot`), PR 6 (`mc analyze`, `mc rerun`), PR 7 (`mc size`)
      and PR 8 (`report`). R20 is each summary above; PR 10 is Tier 0, with no gate.
  - Planning (#78): M6 spec, plan and ten-PR decomposition; Q1 to Q23 decided by Joe, after a Codex plan
    critique and a verification pass. Plans: [milestone](docs/plans/m6-analysis-tooling.md),
    [critique](docs/plans/m6-plan-critique.md). Packet: none.
  - PR 1 (#79): ADR-0027, superseding ADR-0025's Finalization: the `.partial` and its exclusive create,
    publication by `renameat2(RENAME_NOREPLACE)`, the destructor's `finalized_by` marker without a rename, a flush
    and `rows_committed` after every block, what a killed run leaves, `load`'s states and cross-checks, and who
    deletes what. Also architecture rev 10 (§1, §9, §12, §15, §16 M6 row, with M6 features marked planned). The
    Finalization open item stays until PR 2. Plan: [plan](docs/plans/m6-pr1-adr0027.md). Packet: in the PR body.
  - PR 2 (#80): ADR-0027 implemented. The core writes `<log.path>.partial` through a held directory descriptor, keeps an
    `O_PATH` descriptor to its file, flushes and sets `rows_committed` after every block, publishes only from
    `close_log()` by `renameat2(RENAME_NOREPLACE)` after an identity check, marks a dropped run `finalized_by =
    destructor`, and deletes only through `discard_log()` (bound) or a failing init; format version 2. `astra.log`
    reports the three states and refuses `.partial` names, with the cross-checks and certified rows; replay follows
    `out`'s rules; one cleanup rule through `_runkit`; `Session` publishes on its normal close. Plan:
    [plan](docs/plans/m6-pr2-finalization.md). Packet: in the PR body.
  - PR 3 (#81): the `astra` console script (`astra.cli`). `astra run <config> [--strict] [--log PATH]` runs open
    loop, closes the log with `close_log()`, prints §15's summary plus wall time and real-time factor, and exits
    per §15's table; it refuses a `transport` block (65) and deletes no log file. `astra trim` prints the trim and
    its config fragment as exact YAML. Plan: [plan](docs/plans/m6-pr3-cli.md). Packet: in the PR body.
  - PR 4 (#82): units. `core/util/units` gains `degrees_to_radians`/`radians_to_degrees` by one named constant,
    `kRadiansPerDegree`, with exact and near identities pinned. `astra._core` binds all six conversions, float
    and 1-D float64 array, bitwise equal element by element, with the constants unbound. `astra.fgbridge` holds
    no conversion constant, and its packets are unchanged. Plan: [plan](docs/plans/m6-pr4-units.md). Packet: in
    the PR body.
  - PR 5 (#89): plots. `astra.plot` reads YAML plot packages (figures, panels, series; `{v}` over every vehicle;
    display units through the bound `core/util/units`; a Python function by module or by a `.py` beside the
    package), every map closed and every error naming the file and key before any file is written. matplotlib
    writes byte-deterministic SVG (or PNG) per figure, plotly one offline HTML page; nothing is overwritten.
    Framework packages, a template starter set and a notional-transport package ship; `astra plot` joins the
    CLI; matplotlib and plotly are pinned. Plan: [plan](docs/plans/m6-pr5-plots.md). Packet: in the PR body.
  - PR 6 (#92): Monte Carlo analysis. `astra.mc_analysis` covers:
    - outcomes under a closed criterion grammar (default `status == halt`; `error` rows apart);
    - KS with Benjamini-Hochberg, Spearman and PRCC against a metric, ranked by p, then |effect|, then spec order;
    - an in-house depth-3 tree whose fail leaves read as conjunctions;
    - the histogram, scatter and envelope figures (counts per frame, failing runs overlaid).

    `astra mc analyze` and `astra mc rerun` join the CLI. A batch now records its spec and base config bytes in
    `summary.parquet`, which rerun runs from, comparing the row and the log. Every matplotlib legend sits
    outside its axes. Plan: [plan](docs/plans/m6-pr6-mc-analysis.md). Packet: in the PR body.
  - PR 7 (#93): Monte Carlo sizing, as Joe re-scoped it on 2026-10-02. `astra.mc_sizing` offers four options,
    probability, percentile, precision and ranking, each with a formula mode (R11's oracles) and a data mode on a
    batch:
    - calibrated verdicts (exact Clopper-Pearson; percentile as exceedance; one non-strict convention decided in
      rational arithmetic) with a producer-risk N;
    - nominal projections, never verdicts (precision with Bonett's std interval and stated limits; ranking with a
      jackknife Spearman variance);
    - convergence at every prefix.

    A pilot only sizes; the verdict comes from a fresh final batch. Coverage is tested at K = 1047.
    `astra mc size` joins the CLI. R9 and C-08 move to PR 7b. Plan: [plan](docs/plans/m6-pr7-mc-sizing.md).
    Packet: in the PR body.
  - PR 7b (#94): R9, the planted cause in the notional transport, closing C-08 (`tests/python/test_mc_planted.py`,
    `-k planted`). Joe's numbers: `cm_alpha_per_rad` uniform [−0.6, +0.6], 10 s, `max.v0.derived.alpha > 0.23`.
    Each gate runs the protocol: 120-run pilots on seed 7071, sized by the ranking option and deleted, then final
    batches of 130 on seed 707. KS and Spearman rank the cause first with the stated minimum effect and margin,
    and the control flags nothing. R9's config is also checked through `astra.montecarlo`'s PyYAML route. Plan:
    [plan](docs/plans/m6-pr7b-planted.md). Packet: in the PR body.
  - PR 8 (#95): the run report (R13, R15's report case, C-11 to C-13). `astra.report` and `astra report` turn a
    run log, a scenario's log (`--scenario`) or a batch (a directory) into one self-contained HTML file: the
    summary, the scenario's check rows (`astra.scenario.evaluate`), every figure inline with its ids namespaced
    by ordinal, the batch's outcomes, sizing table and rankings, and provenance read from the log (the config
    text escaped, never re-parsed). `tests/python/resource_refs.py` becomes an allow-list (D9), closing that
    open item; R3b uses it. `-k report`. Plan: [plan](docs/plans/m6-pr8-report.md). Packet: in the PR body.
  - PR 8b (#96): the batch envelope's size, closing that open item. Over more than 901 frames,
    `mc_analysis.draw_envelope` draws each band (min-max, 5-95 %) as a step over `ENVELOPE_COLUMNS = 900`
    columns that share their boundary frames, so the band still contains every finite frame and the segments
    between them, in at most 3,603 vertices per band (an all-NaN column is a gap, and its neighbors keep their
    width). Shorter envelopes draw the exact band. The lines, the count and
    the overlays keep every frame, so R5 stands. 16 runs of 300 s: the alpha envelope goes from 34.9 to 4.2 MB,
    and the batch report with alpha and altitude from 66.2 to 4.8 MB. Everything else is byte-identical, and
    the envelopes differ only inside their two fill groups
    ([before/after](docs/reviews/evidence/2579fd7-before-after.log)). `-k mc_analysis`. Plan:
    [plan](docs/plans/m6-pr8b-envelope.md). Packet: in the PR body.
  - PR 9 (#91): the frame-time benchmark. `astra_frame_benchmark` (`tests/benchmark/`) times each `step()` of the
    notional transport's cruise and still-air configs and of `dummy_1000`, after a warm-up instance, checking
    every timed frame ran with no halt. It also times R19b's five logged cases end to end through `close_log()`.
    It writes JSON (mean, median, p99, max, fps, margins against `dt`, toolchain, CPU, load) only after every
    check passes. `just benchmark` takes a Release baseline; the gate runs its smoke, which asserts no speed.
    Closes C-19 and N-05. Plan: [plan](docs/plans/m6-pr9-benchmark.md). Packet: in the PR body.
  - PR 10 (#103): M6 closeout: the check-off with evidence links, open items for the held-out scope (a plotting
    UI or live plotting, PDF output, Sobol and adaptive sampling; the closed-loop item extended), the
    docs-check item, the review-record item made unscheduled, PR 14's divisor clause, and architecture §12 and
    §16 status. Plans: [milestone](docs/plans/m6-analysis-tooling.md), [PR](docs/plans/m6-pr10-closeout.md).
    Packet: in the PR body.
  - PR 11 (#97): configs read through PyYAML, closing that open item. `astra.montecarlo`, `astra.scenario`,
    `astra.trim` and `astra.lockstep` read a config through `astra._yaml_route.load`, which compares PyYAML's
    dict leaf by leaf with the core's reading of the same text (the four getters and the node kind; a number
    written two ways does not count). A differing scalar keeps the core's own text, so `sim.health.nan: off`
    builds and `1_000`, `1:30`, `0x10` or `0b101` in a number key is refused at build as `astra run` refuses
    it. What text cannot repair (a null, a duplicate or merge key, a key that is not text, a set) is refused
    through the tool's own error, naming the key. No binding change. Every tracked config needs no repair, and
    R9's, the descent and Q19 batches, the scenarios, `astra trim`, `astra.linearize` and the lockstep config
    are byte-identical to `72877da` ([before/after](docs/reviews/evidence/18bb8c1-before-after.log)).
    `-k pyyaml_route`. Plan: [plan](docs/plans/m6-pr11-yaml-route.md). Packet: in the PR body.
  - Gate fix (#98): `just embedded-standalone`'s dependency check reads the CMakeCache's dependency entries and the
    compile and link lines (`scripts/embedded_deps_check.py`), not the checkout path, which failed M6 PR 11's
    worktree `m6-pr11-yaml-route`. Tier 1. Brief: [brief](docs/plans/embedded-deps-check.md). Packet: in the PR body.
  - Gate fix follow-ups (#99): the check also strips the checkout's real path (CMake records the physical path
    of a symlinked checkout) and reads generated files with `errors="replace"`. Tier 1. Brief: the same, its
    Follow-ups. Packet: in the PR body.
  - PR 12 (#100): the TAS/CAS panels and the ground track's aspect, closing those two open items. `flight`'s
    `air_data` draws TAS and CAS on panels of their own. `astra.plot.Panel` gains `aspect` (`"auto"` or
    `"equal"`; no YAML key, since YAML's x is always `t`). An equal panel draws one unit the same length on both
    axes (matplotlib `adjustable="datalim"`, to its own 0.5 % tolerance; plotly `scaleanchor`), and it must be
    its figure's only panel. The template's ground track is equal-aspect. Every other figure, report section,
    batch figure and plotly page is byte-identical to `6458e04`
    ([before/after](docs/reviews/evidence/800149c-before-after.log)). `-k panel_scale`. Plan:
    [plan](docs/plans/m6-pr12-plots-fixes.md). Packet: in the PR body.
  - PR 13 (#101): the batch envelope's failing-run overlays, closing that open item. Over more than 901 frames,
    `mc_analysis.draw_envelope` draws each failing run's line through a reduction on the band's own 900 columns,
    clipped to the run: each finite stretch inside a column keeps its first, minimum, maximum and last frame,
    plus the run's first and last frames and the first NaN after each stretch, all of them the run's own
    samples. Neighboring columns share their boundary frame, so a trace with no NaN keeps at most 2,701 points
    (`3 × 900 + 1`, not the item's 3,600). Every failing run is still overlaid under its index, so R5 stands; PR
    6's overlay check becomes "the line equals the column reduction of the log column". Alpha's envelope goes
    from 7.51 to 3.29 MB on R9 (58 failing runs of 10 s), 2.77 to 1.30 MB at 16 runs of 60 s and 4.22 to 1.84
    MB at 16 runs of 300 s; altitude's is almost unchanged. Everything else is byte-identical to `fa3a3c0`, and
    the envelopes differ only inside the overlay groups
    ([before/after and sizes](docs/reviews/evidence/444497e-before-after.log)). `-k envelope_overlay`. Plan:
    [plan](docs/plans/m6-pr13-envelope-overlays.md). Packet: in the PR body.
  - PR 14 (#102): the plotly page's size, closing that open item. `draw_plotly` writes a trace's x as `x0`/`dx`
    when plotly.js's own rebuild, `x0 + i * dx` in float64, gives it bit for bit, and as an array otherwise:
    lossless and always on, though not always `x0`/`dx`. At dt = 1 ms a rate group's t keeps an explicit
    array at divisors such as 25, 50 and 100, while divisors such as 1 to 5, 7, 10, 20 and 40 rebuild exactly
    (the full set in M6 PR 10's PR body). On request, `astra plot --html --html-columns N` reduces each trace of more than N + 1
    points on its own N index columns to each column's ends and extremes, all of them the trace's own samples,
    and the figure's title says so. That is at most 3N + 1 points with an increasing x and no gap, 5N + 1 for an
    x-y path, and more with gaps. The notional `cruise` page goes from 25.1 to 15.6 MB at 60 s and from 105 to
    58 MB at 300 s by default, and to 5.8 MB at both durations with 2,000 columns. Every SVG, report and batch
    figure is byte-identical to `d851b8f`; every changed page draws the same doubles
    ([before/after and sizes](docs/reviews/evidence/ec51474-before-after.log)). `-k plotly_size`. Plan:
    [plan](docs/plans/m6-pr14-plotly-size.md). Packet: in the PR body.
  - Workflow (#83): plan-mode drafts land in the session's `docs/plans/` (`plansDirectory`), with the rule for
    renaming, committing or deleting them in `docs/agents/claude-code.md` and `/wrap`; 27 never-committed plans
    recovered from `~/.claude/plans`. Plan: [plan](docs/plans/claude-plans-directory.md). Packet: in the PR body.
  - Follow-ups from #83 (#85): `.claude/settings.local.json` ignored; workflow.md's model sentence matches
    `.claude/settings.json`; `docs/agents/claude-code.md` says a worktree-guard refusal is rerun or written with
    Write/Edit, never asked; recovered plans linked from their PR lines above. Plan:
    [plan](docs/plans/followups-83.md). Packet: in the PR body.
  - README rewrite (#86): `README.md` as a top-level README (what ASTRA is, features, quick start, how-tos,
    architecture, layout, verification, roadmap, development, references); the ruleset commands moved into
    workflow.md's enforcement bullets; `docs/README.md`'s stale unbuilt-paths line fixed. Plan:
    [plan](docs/plans/readme-rewrite.md). Packet: in the PR body.
  - Leftovers from #86 (#87): README's `astra run` exit codes match `cli.py` (70 added); the #86 review record's
    R-001/R-002 verified; `/wrap` no longer asks Joe about a guard refusal; claude-code.md's main-checkout draft
    location observed; sessions start in plan mode (`permissions.defaultMode`). Plan:
    [plan](docs/plans/readme-leftovers.md). Packet: in the PR body.
  - Docs sweep (#88): `/wrap` gains a docs step (branch scope by default, repo scope when Joe asks and at
    every milestone closeout), the PR template a Docs impact section; one repo-scope run brings today's docs
    and milestone comments current, including architecture §9, §12 and §15's M6 markers (that open item is
    removed) and `docs/refs/README.md`. Plan: [plan](docs/plans/wrap-docs-sweep.md). Packet: in the PR body.
  - STATE and trailers (#90): `/wrap` writes STATE.md as main reads it after the merge (Next step, Open threads,
    Honesty rules; no PR stage), and `scripts/docs_check.py` enforces that shape, the 40-line cap and the
    honesty rules text; `"attribution": false`, and `no-main.sh` refuses a commit message with a co-author
    trailer. Plan: [plan](docs/plans/state-and-attribution.md). Packet: in the PR body.
- [ ] **M7 F-16 and the NESC oracle** — `aircraft/f16` built from the template; NESC atmospheric cases 11, 12,
      13.1 to 13.4, 15 and 16 against the participating simulations' trajectories in `docs/refs` (ADR-0013's
      ensemble as oracle); the tutorial and the generated config-key reference written from this path. The
      NESC F-16 control laws (`F16_control.dml` for 13.x, `F16_gnc.dml` for 15 and 16) run outside
      `embedded/` first and are shown to match the ensemble, then are ported to `embedded/` (Joe,
      2026-09-29). Plan questions: where the sim-side control law lives, the port's equivalence criterion, and
      hand-porting the aero tables versus a generic DAVE-ML table plugin.
  - Planning (#104): M7 spec, plan and PR decomposition (PR 0 to 16, PR 1 split into 1a and 1b); approved
    by Joe with Q1 to Q27 and his amendments, after a Codex plan critique and a verification pass. The NESC
    DAVE-ML files (P1, P2) are still to be placed in `docs/refs`. Plans:
    [milestone](docs/plans/m7-f16-nesc-oracle.md), [critique](docs/plans/m7-plan-critique.md). Packet: in
    the PR body.
  - PR 1a (#105): three records and architecture rev 11, docs only. ADR-0028, the F-16 oracle: SIM 4 and SIM 5,
    SIM 2 reported and never gated, a gated window of t ≥ 8 s, 1e-5 s matching, ADR-0013's scalar band
    with k = 3, footnotes governing exclusion, the aero channels against an aero-only frame-start wrench.
    It supersedes ADR-0013 in part, for cases 11 to 16 only. ADR-0030: no unit suffix in signal names, and
    one allowed in C++ identifiers and config keys, superseding one ADR-0002 bullet. ADR-0031: `AeroModel`'s
    envelope declaration, one `kEnvelope` check with five action keys and counters, and q̄ and n_z
    published. Plan: [plan](docs/plans/m7-pr1a-adrs.md). Packet: in the PR body.
  - PR 8 (#106): ADR-0028 in the band generator and eight F-16 band headers, generated by Joe; cases 1 to 10
    byte-identical. `nesc_band_common.hpp` gives each case its sample count and first gated sample
    (`CaseBandOf<N, G>`; `CaseBand` stays cases 1 to 10's 301 samples, all gated). The generator's report
    (floor ratios, zero-accepting channels, SIM 2's ratio, matching offsets), in the PR body, is the record
    of the generated values (R8). `NescF16Band.*` checks their shape, floors and Vol II initial conditions;
    PR 9, 11a and 11b fly them. Plan: [plan](docs/plans/m7-pr8-nesc-bands.md). Packet: in the PR body.
  - PR 6a (#107): the inventory addendum from P1 and P2 (the NESC F-16 DAVE-ML files, archive SHA-256s
    recorded), docs only. Joe's decisions (2026-10-08): Q14 and Q15 confirmed; Q5's condition holds, so PR 5b
    is not scheduled; Q12 divisor 10; the aero damps the body rate relative to the Earth, which the core
    supplies (ADR-0032 in PR 1b, implemented by the new core PR 3b before PR 6b); the laws' rate frame
    deferred to PR 1b; the DML's mass; the F-16 declares Mach ≤ 1.0. The M7 plan carries dated amendments.
    Plan: [addendum](docs/plans/m7-f16-inventory.md) (brief in the PR body). Packet: in the PR body.
  - PR 5 (#108): trim at any heading, under any of the three shipped Earth models (flat altitude −p_n[2]),
    with the two solved inputs checked against `Simulation.input_names`, closing the trim-names and §18 step 4
    items. Q6's rate convention is an option on every consumer, `zero_inertial` the default and `n_frame` for
    the F-16 (Joe's decision (b), 2026-10-08: the notional transport damps the inertial ω_b until B2, so its
    `n_frame` trim leaves ṗ = −8.2e-5 rad/s²). The solver bounds the three solved residuals and reports the
    lateral three; their kinematic and gravity parts match a closed form from Groves, Titterton, S&L and
    TR8350.2 to roundoff, case 15's 89.95° N included. Every consumer's emitted config rebuilds the trim bit
    for bit. Plan: [plan](docs/plans/m7-pr5-trim.md). Packet: in the PR body.
  - PR 1b (#109): two records and architecture rev 12, docs only, with the pre-registered rate-frame analysis
    (`docs/reviews/evidence/m7-pr1b-rate-frame/`: the script, committed before its first run, then its two
    logs). ADR-0029, sim-side flight software: `aircraft/f16/sim/transport/nesc_control` as an in-process peer,
    its refusal list (`LATLON_NOT_MODELED` included), its schedule in `transport.peer` keys keyed to frames,
    divisor 10, SI with the DML's own constants, 13.4's initial-plane line from the frame-0 base point (J8
    (a)), the laws' Earth-relative rate frame (the rule's result was "inconsistent", and Joe decided on the
    evidence, 2026-10-09), and the port's bitwise equivalence. ADR-0032: the core supplies a per-stage
    `EnvironmentSample::omega_ie_b`, NaN by default, and `air_data::omega_eb_b`, read by the F-16's and the
    template's aero; it supersedes one Consequences clause of ADR-0020, and PR 3b implements it. Plan:
    [plan](docs/plans/m7-pr1b-adrs.md). Packet: in the PR body.
  - PR 3b (#110): ADR-0032 in the core. `Vehicle::prepare_stage_environment` sets
    `EnvironmentSample::omega_ie_b` = C_nb·ω_ie^n first in every RK4 stage, on the step and probe paths. A zero
    is canonicalized to +0.0. The field is NaN by default and in the latched sample. `air_data::omega_eb_b`
    returns ω_b − ω_ie_b. `kCoreApiVersion` goes 9 to 10, and `tests/plugins/stale_api_version` is rejected.
    The template's aero damps the Earth-relative rate, with its `earth/flat` values bit-identical
    (`docs/reviews/evidence/m7-pr3b-template-identity/`). The notional transport and `nesc_aero` are unchanged.
    Plan: [plan](docs/plans/m7-pr3b-earth-relative-rate.md). Packet: in the PR body.
  - PR 4 (#111): `mass/constant`'s `ixy`, `ixz` and `iyz` are S&L products of inertia (Ixz = ∫xz dm), negated
    into the tensor, closing the product-of-inertia item. The negation is written `0.0 - p` (D1), so a config
    with no product key keeps the pre-PR tensor bit for bit; the template's and the notional transport's mass
    plugins keep `-p`. The `ConstantMass` tests pin the bitwise negation, the omitted key's +0.0, a
    two-point-mass sign check built from the definition, and the refusals' paths and messages.
    `kCoreApiVersion` stays at 10. R17's config-key-reference half goes to PR 14 (the M7 plan's obligations
    table). Plan: [plan](docs/plans/m7-pr4-mass-convention.md). Packet: in the PR body.
  - PR 2 (#112): `Config::from_file` and `from_string` refuse a recursive alias at load with a `ConfigError`
    naming the key, so `astra run` exits 65 where it segfaulted, closing the Config cycle item. The walk tracks
    the maps and sequences on the current path by `YAML::Node::is()`, iterating entries (both duplicate keys)
    and key nodes, so a shared alias still loads; no depth cap. `from_dict`'s emitter refuses a dict, list or
    tuple that contains itself, by identity on the current path. `_yaml_route`'s cycle guard is removed: the
    core refuses the reversed duplicate key (PyYAML's dict cyclic, yaml-cpp's lookup a scalar) before `_walk`
    runs. `kCoreApiVersion` stays at 10. Plan: [plan](docs/plans/m7-pr2-config-cycles.md). Packet: in the PR
    body.
  - PR 6b (#113): `aircraft/f16` from `just new-aircraft f16`, with the generator, the tables, the fixtures and
    the aero. `generate_f16_tables.py` and `extract_f16_checkdata.py` (no shared code, each checking P1's
    SHA-256s) emit `aero_tables.hpp`, `engine_tables.hpp` and `f16_checkdata.hpp`, and `test_f16_generated.py`
    regenerates them byte for byte (skipped only without P1). `physical/aero` is the DML's build-up in SI,
    clipped at every table edge, damping `air_data::omega_eb_b` (ADR-0032), with the moments moved 1.132 ft
    from the MRC to the CM; `F16Aero` checks it on the 16 checkData shots. The copy's `configs/cruise.yaml` is
    removed (PR 9 writes the F-16's configs). #113 merged after the plan's commit 4, so mass and propulsion
    are PR 6c's. Plan: [plan](docs/plans/m7-pr6b-f16-aircraft.md). Packet: in the PR body.
  - PR 3c (#114): `EnvironmentSample::altitude_m` (ADR-0033) and the slug and pound-force units.
    `Vehicle::prepare_stage_environment` sets it to `EarthModel::altitude` at each RK4 stage's own state, first
    with `omega_ie_b` and before the atmosphere's range check, on the step and probe paths; the atmosphere
    reads the same value. NaN by default and in the latched sample, not canonicalized; `kCoreApiVersion` goes
    10 to 11. `earth_rate_recorder` reads it (`x − x`), so an unset value halts on a NaN wrench; every
    existing value is unchanged. `core/util/units` gains pound-force, slug and slug·ft² conversions,
    `kKilogramsPerSlug` the correctly rounded quotient (one ulp above the plain one), each pinned in hex and
    bound in `astra._core`. The M7 plan's 2026-10-10 amendment adds PR 6c. Plan:
    [plan](docs/plans/m7-pr3c-altitude-and-units.md). Packet: in the PR body.
- [ ] **M8 Actuators and controller tuning** — a first-order lag and a nonlinear second-order actuator model
      with rate and position limits; autopilot gains loaded at startup; the tuning tool (spec-driven on the
      linear model, time-domain on the nonlinear sim, robustness across draws, gain schedules) with
      frequency-domain and handling-qualities analysis (MIL-HDBK-1797), for the notional transport and the
      F-16.
- [ ] **M9 Sensors and ICD v2** — ICD v2 with timestamps and per-sensor rates; IMU at several fidelity levels,
      GNSS PVT, barometric altimeter, air data, and a magnetometer with a magnetic field model; non-standard
      days, 1-cosine gusts, wind shear and microbursts; fuzzing of ICD decode.
- [ ] **M10 Navigation, faults and closed-loop Monte Carlo** — an INS/GNSS EKF in `embedded/` with the
      autopilot flying on estimated state; truth, measurements and estimates logged, with NEES and NIS over a
      batch; fault injection as scenario events (sensor faults, GNSS-denied, actuator faults, engine out);
      closed-loop scenarios and Monte Carlo, with failure correlation extended to them.
- [ ] **M11 Real-time** — `RealTime` mode, with the frame-time benchmark tracked over time.

## Open items

- [ ] (M7) Earth/gravity rotation-consistency check at init (origin: M2 case05 fix; deferred by Joe, 2026-09-19) —
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
- [ ] (M8) Elevator/throttle deflection *range* has no declared home (origin: M3 PR 6 planning,
      `docs/plans/pr6-trim.md`, D10): the second control-surface property (after the positive-deflection sense,
      resolved by M3 PR 9) with no config key or ICD field to read it from. PR 6's own plausibility check
      hardcodes `elevator ∈ [-0.3, 0.3]` rad (737.xml) and `throttle ∈ [0, 1]` (the ICD's documented convention,
      not a config value) directly in its test rather than adding config keys. Narrowed by M3 PR 9: per-aircraft
      surface limits live with the aircraft's own actuator model (M4+), not the ICD — `astra_sw_to_sim_v1`'s
      `elevator`/`aileron`/`rudder`/`throttle` fields carry a commanded angle the sim does not clamp in v1; the
      ICD does not carry limits. Not started.
- [ ] (M8) `astra.linearize`'s 9-state `A` (PR 7, D11) omits altitude (origin: M3 PR 8 finding,
      `docs/plans/pr8-cruise-regression.md` finding 4; owner: Joe's call): position is held fixed, so `A` has no
      d/dh and no density/thrust-lapse coupling. Against a nonlinear open-loop run from the trimmed cruise,
      `modes(A).phugoid`'s damped period is 125.3 s against a measured 108.6 s (15% long; the measured period is
      13% short of it) and its zeta 0.052 against 0.027; `A` plus one altitude column and the h_dot row matches
      (108.5 s, 0.027), as S&L Example 3.8-5 describes. PR 8 builds that 5x5 in its own test only. An opt-in
      altitude state in `astra.linearize` itself would be a D11 revision — Joe's call, not scheduled. Not
      started.
- [ ] (unscheduled) `tests/golden/dynamics/capture_r1_golden.cpp` (the R1/R1-Sim capture harness, M3 PR 4a) is stale (origin:
      M3 PR 10 finding; owner: Joe): it includes `astra/kernel/eom.hpp` and uses
      `astra::Eom`/`post_step_renormalize`, all retired by ADR-0021 in PR 4a itself. A future R1 re-capture needs
      a source fix to that harness under Joe's authorization (it is under `tests/golden/**`), not just re-adding
      `add_subdirectory(golden/dynamics)` as `tests/CMakeLists.txt`'s comment implies. Its script
      `capture_r1_golden.sh` also has no arming check of its own (unlike PR 8's); today it refuses at its
      clean-tree and baseline-drift (`26ad594`) steps, before its first commit. Not started.
- [ ] (M7) §15 envelope monitors (α, β, Mach, dynamic pressure, load-factor limits declared by the aero model)
      (origin: deferred from M3 PR 3, ADR-0018): no declaration hook exists yet on `AeroModel`, because no concrete
      plugin had real numbers to declare before M3 PR 4. A later PR adds
      the declaration virtual (a further `kCoreApiVersion` bump, precedented) and wires `HealthMonitor`'s own
      per-frame check against it. Not started. Unblocked (Joe, 2026-10-03, at M6 PR 7b): the notional transport
      can declare `alpha_max_rad = 0.23` from the source's CLalpha peak (`docs/refs/aircrafts/737.xml`, CL 1.20 at
      0.23 rad), and a halt there would turn R9's criterion into `status == halt` instead of a threshold. A Tier 3
      PR of its own; scheduling it is Joe's call. Shape decided by ADR-0031 (M7 PR 1a); implemented by M7 PR 13.
- [ ] (M8) Moderate-turbulence throttle jitter (origin: M4 PR 6; Joe 2026-09-26, nonblocking): airspeed gusts pass
      through the autopilot's speed loop to the throttle, up to 0.019 per exchange at moderate (throttle 0.310 to
      0.633 around trim 0.4986), no limit reached (`docs/reviews/m4-pr6-lockstep-acceptance-human.md`). Revisit
      when a sensor or actuator model lands. Not started.
- [ ] (M8) Remove the `OPTIONAL_GAIN_NAMES`/`kSideslip` support from `tests/python/autopilot_design.py` (origin: M4
      PR 5 follow-up, carried by PR 6; owner: whoever next touches the gains): `gains.hpp` defines no `kSideslip`
      and no sideslip term was needed at M4 acceptance. Not started.
- [ ] (M10) Follow-up C (origin: M4 PR 3; Joe 2026-09-25, not scheduled): `LockstepExchange` recorder sizing for an
      absurd `sim.frames`: preallocation throws `std::bad_alloc` at init instead of a `ConfigError` naming
      `sim.frames`, and `sim.frames` near `UINT64_MAX` wraps the capacity arithmetic in its constructor
      (`docs/reviews/m4-pr3-core-seam-human.md`). Not started.
- [ ] (M11) Wall-clock scan, timed I/O waits (origin: M5 PR 10 review, Codex thread `01a0eb14-1425-7c02-bb04-44d54eb9960e`;
      Joe's stop rule, 2026-09-28): `scripts/wallclock_scan.py`'s `multiplexing` family covers `select`, `pselect`,
      `poll`, `ppoll`, `epoll_wait` and `epoll_pwait`, but not three more members of it that take a timeout:
      `epoll_pwait2`, `aio_suspend` (and `aio_suspend64`) and `recvmmsg`. Each shows as an undefined symbol under
      `nm -u -C` on clang++-18 and passes the scan today. Add anchored patterns and one compiled case per family.
      The scan stays a tripwire, not a proof (its docstring and architecture §15 list the blind spots). Not started.
- [ ] (unscheduled) Replace `no-main.sh` rule 3's command-text scanner (`.claude/hooks/commit_trailer.py`) with a check
      of the message git records: a versioned `commit-msg` hook reached through `core.hooksPath`, with `no-main.sh`
      refusing `--no-verify`/`-n` and any `core.hooksPath` override (origin: #90's Codex review; Joe 2026-10-01).
      The scanner enumerates shell shapes, so a trailer behind a variable or an odd argument shape can pass. This
      closes the three findings Joe deferred on #90 (`docs/reviews/state-and-attribution-review.md`): R-001 (a
      `$1`/`$@`/`$*` in an unquoted heredoc body), N-001/N-002 (an apostrophe in a `# comment` flips quote tracking),
      N-004 (a quoted empty argument splits a statement). It also closes the scanner's documented gaps: a message
      reused by `-C`/`-c`/`--amend --no-edit`, and an inline `cd` before `git commit -F <relative>`. Not started.
- [ ] (M11) `RealTime` mode's logger writer thread (origin: M5 plan §9/architecture §9, held-out scope; Joe's call):
      M5's logger drains only inline (`AsFastAsPossible`); `RealTime` mode itself is not built, so the
      separate non-RT drain thread architecture §9 describes has nothing to run on yet. Not started.
- [ ] (M11) The frame-time benchmark's maximum is one sample, and an unisolated desktop sets it (origin: M6 PR 9
      baselines, 2026-10-02). Two quiet-machine Release runs of the same timed code at 100,000 frames agree at p99
      (cruise 1.94 and 1.75 µs) but not at the max (32.7 µs, then 2010 µs, one frame over `dt`). The `powersave`
      governor, no CPU isolation and other sessions were in play (`docs/reviews/evidence/c1c0976-` and
      `cfb91d4-frame-benchmark.json`). M11's tracking needs isolated CPUs, a fixed governor and a stated tail
      statistic before any maximum is compared. The JSON should also record the frame index of the maximum, so a
      stall can be told apart from a frame whose model work is costly (today it records only the value). Not
      started.
- [ ] (unscheduled) The plotly page's 4.8 MB floor is plotly.js inline (4,815,867 bytes of plotly 7.1.0's bundle):
      after M6 PR 14, a `--html-columns 2000` page is 5.0 to 5.8 MB at any duration, mostly plotly.js (origin: M6
      PR 14's plan, D3; Joe 2026-10-04). A partial bundle would be a new vendored asset, since plotly.py ships only
      the full one: Joe's call. Not started.
- [ ] (unscheduled) An equal-aspect panel keeps `draw_matplotlib`'s canvas, 8 × 3.2 in for one panel, so a
      north-going ground track gets only about 2.2 in of height and the panel's width is mostly empty east limits
      (origin: M6 PR 12's plan, D2b; Joe 2026-10-04: deferred). A taller canvas for an equal panel would change
      the figure-size rule for one kind of panel. The same PR may lift PR 12's rule that an equal panel be its
      figure's only panel (with a shared x, matplotlib's `datalim` moves the y limits instead). Not started.
- [ ] (unscheduled) The notional transport's aero (and `nesc_aero`) still damps the inertial ω_b. The template
      damps the body rate relative to the Earth that the core supplies (`air_data::omega_eb_b`, M7 PR 3b), and
      the F-16 will (PR 6b) (origin: M7 PR 6a's D3, option (B2); Joe 2026-10-08,
      [addendum §10](docs/plans/m7-f16-inventory.md#10-joes-decisions)).
      Migrating it moves every notional-transport value on `wgs84_rotating`: M3's frozen regression, the R1
      golden, M4's lockstep and M6's batch. Measure the change first; Joe decides. The same step migrates the
      notional-transport trim callers to `astra.trim`'s `n_frame` rate convention (M7 PR 5, Joe's decision
      (b), 2026-10-08): until the aero damps the Earth-relative rate, an `n_frame` trim of the notional
      transport is not an equilibrium in roll (ṗ = −8.2e-5 rad/s² at cruise), so every caller keeps
      `zero_inertial`; the migration is measured with the rest. Until then,
      `aircraft/notional_transport/physical/aero/aero.cpp:4-6` is stale. It says that file is identical to
      the template's except the class name, but since M7 PR 3b the two also differ in the rate they damp. M7
      PR 3b left the notional transport untouched, per its brief, and the migration fixes the sentence. Not
      started.
- [ ] (unscheduled) `core/include/astra/util/geodesy.hpp`'s `kJ2 = 1.082629989e-3` has no stated source (origin:
      M7 PR 5's plan, Observations; Joe 2026-10-08). It is the original WGS 84 value, √5 times the C̄2,0 that
      NIMA TR8350.2 §3.3 implies, and 3e-6 relative from TR8350.2 Table 5.1's current EGM96 C̄2,0
      (J2 = 1.082626684e-3, as S&L 3rd ed. eq. 1.6-25 gives). Either its source is stated or the value changes,
      by Joe's decision. Not started.
- [ ] (unscheduled) A review fix's final Codex verification has no place in `docs/reviews/*-review.md` (origin:
      #95's V-001 and #96's R-001; Joe 2026-10-03; moved from M6 at its closeout, M6 PR 10's D3).
      Review-dispatch's no-record-only-commit rule keeps the verification result out of a commit of its own, so
      the review record's last finding stays "fixed-unverified" there while the PR body holds the verified
      result. Joe's choice (M6 PR 10's D3, option (a)): the verification lives in the PR body only. The review
      record ends at the fix batch it describes, and its last line points to the PR body's Codex section. The
      workflow edit is a Tier 2 PR of its own, to `docs/agents/review-dispatch.md` and
      `docs/templates/review-findings.md`. The template gains a terminal status that names the PR body (for
      example "fixed; verification in the PR body"), so "fixed-unverified" is no longer the record's last word.
      Not started.
- [ ] (unscheduled) `just docs-check`'s `git diff --check` reads only the uncommitted diff, so a whitespace error
      already committed passes a clean-tree gate (origin: `docs/lessons.md` 2026-10-04, M6 PR 14's review; Joe
      at #102, M6 PR 10's D2). A fix also checks the committed range, `<base>..HEAD`. It has to choose the base
      when `origin/main` is absent or shallow (a fresh clone, CI's `GATE=local`) and whether `hooks-test` gets
      a case. It changes the gate, so it is a gate PR of its own, with the full gate. Until then the lesson's
      rule holds: run `git diff --check <base>..HEAD` before review. Not started.
- [ ] (unscheduled) A plotting UI, or plotting a live run (origin: M6 plan, Non-goals and "Held out"; Joe's
      call): `astra.plot` writes files from a finished log (SVG or PNG per figure, one offline plotly HTML page
      per package), and nothing draws while a `Simulation` runs. Not started.
- [ ] (unscheduled) PDF output (origin: M6 plan, Non-goals and "Held out"; Joe's call): the run report is
      one self-contained HTML file (`astra.report`), and figures are SVG, PNG or plotly HTML. Not started.
- [ ] (unscheduled) Variance-based sensitivity (Sobol) and adaptive sampling (origin: M6 plan, Non-goals and
      "Held out"; Joe's call): both need designed sample sets rather than an existing batch, while
      `astra.mc_analysis` ranks the draws of a batch `astra.montecarlo` already ran (KS with
      Benjamini-Hochberg, Spearman, PRCC, a depth-3 tree). Not started.
- [ ] (M10) Retire the M4 `ExchangeRecorder` once a lockstep test compares HDF5 logs instead (origin: M5 plan Q12,
      ADR-0022; Joe's call): kept through M5 because M4's byte-identity acceptance tests
      (`closed_loop_acceptance_test.cpp`) read it; M5's log already carries `v0.controls.*` and `transport.*`
      for lockstep runs. Retiring it would also moot Follow-up C's recorder-sizing item above. Not started.
- [ ] (M10) Wind events in scenarios (origin: M5 plan Q14; Joe's call): `astra.scenario` events are `write_input`
      only; there is no write seam for wind, so a wind change is a config choice (for example `wind/uniform`),
      not an event, until a seam exists. Not started.
- [ ] (M10) Closed-loop (autopilot) scenarios and Monte Carlo (origin: M5 plan Q16, extended by the M6 plan's
      held-out scope; Joe's call): not in M5, since adding the autopilot peer means subprocess management in the
      harness; M4's own acceptance already covers closed loop. M6's tools inherit the limit. `astra.montecarlo`
      refuses a `transport` block, so `astra.mc_analysis`, a batch's `astra report` and `astra mc rerun` see
      only open-loop runs, and `astra run` refuses a `transport` block until M10 (exit 65). Not started.
- [ ] (M10) Replaying a lockstep run (origin: M5 plan Q11; Joe's call): `astra.log.replay` refuses a log with a
      `transport` block, since replaying commands without the peer is a different feature. Not started.
- [ ] (M9) ICD v2 carries an Earth rate or an Earth-relative body rate, which removes `nesc_control`'s
      non-rotating-Earth limitation (origin: M7 PR 1b; Joe 2026-10-09;
      [ADR-0029](docs/decisions/0029-sim-side-flight-software-peer.md), Decision 7). ICD v1 carries no Earth rate,
      so the peer forms ω_ie from the frame's latitude with Vol II's Ω, and it still subtracts Ω under a
      non-rotating Earth. Not started.
- [ ] (unscheduled) `.claude/hooks/disarm-goldens.sh` is a `SessionStart` hook with no matcher, so it also fires on
      compaction, and an armed oracle guard does not survive an auto-compact (origin: M7 PR 1b; Joe 2026-10-09;
      `docs/lessons.md` 2026-10-09). Joe decides whether it matches only `startup`, `resume` and `clear`, or keeps
      firing on compaction as a fail-closed default. It is a Tier 2 workflow PR of its own. Until then the
      lesson's rule holds: arm right before the guarded writes, in a turn that finishes them. Not started.
- [ ] (unscheduled) A non-scalar map key reaches `Config::unknown_keys()` as a raw yaml-cpp exception, not a
      `ConfigError` (origin: M7 PR 2; Joe 2026-10-10, out of PR 2's scope; Joe's call when). `collect_leaf_paths`
      reads each key with `as<std::string>()`, which throws on a sequence or map key. Probed on M7 PR 2's build:
      `astra run` on `tests/python/mc_analysis_fixtures/descent.yaml` plus `? [a, b]` then `: 1` exits 70
      ("internal error (a defect)", `RuntimeError: yaml-cpp: ... bad conversion`), not 65. Not started.

## Future work

Joe's 2026-09-29 brainstorm, grouped, with the direction he set: [future work](docs/future-work.md). Its
items are mapped to M6 to M11 above; the rest wait under its "Later" list.

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
