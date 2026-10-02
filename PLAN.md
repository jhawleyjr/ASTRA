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

- [ ] **M6 Analysis tooling** — plotting with reusable plot packages over run logs; Monte Carlo visualization
      (draws, outcomes, trajectory envelopes); failure-to-draw correlation over `summary.parquet`; a tool that
      sizes a Monte Carlo from the question asked and the number of dispersed parameters; a run report with
      provenance; a frame-time benchmark; `astra run --strict`; the superseding ADR to ADR-0025's
      Finalization (Tier 3). Detail: [future work](docs/future-work.md#analysis-and-debugging-tools).
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
  - PR 9 (#91): the frame-time benchmark. `astra_frame_benchmark` (`tests/benchmark/`) times each `step()` of the
    notional transport's cruise and still-air configs and of `dummy_1000`, after a warm-up instance, checking
    every timed frame ran with no halt. It also times R19b's five logged cases end to end through `close_log()`.
    It writes JSON (mean, median, p99, max, fps, margins against `dt`, toolchain, CPU, load) only after every
    check passes. `just benchmark` takes a Release baseline; the gate runs its smoke, which asserts no speed.
    Closes C-19 and N-05. Plan: [plan](docs/plans/m6-pr9-benchmark.md). Packet: in the PR body.
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
- [ ] (M7) `plugins/physical/mass/constant`'s `ixy`/`ixz`/`iyz` config keys have no documented sign convention
      (origin: M3 PR 1 review): the plugin places them directly, unnegated, into the inertia tensor's
      off-diagonal entries, which only matches the standard aerospace convention (tensor entry =
      `-integral(x*z*dm)`) if the config value is already meant as that negated tensor entry rather than the
      product of inertia itself. `aircraft/_template`'s and `aircraft/notional_transport`'s own `physical/mass`
      plugins (M3 PR 1) document and use the product-of-inertia convention explicitly instead; the shared
      plugin's keys are unchanged and still undocumented. Not started.
- [ ] (M8) Elevator/throttle deflection *range* has no declared home (origin: M3 PR 6 planning,
      `docs/plans/pr6-trim.md`, D10): the second control-surface property (after the positive-deflection sense,
      resolved by M3 PR 9) with no config key or ICD field to read it from. PR 6's own plausibility check
      hardcodes `elevator ∈ [-0.3, 0.3]` rad (737.xml) and `throttle ∈ [0, 1]` (the ICD's documented convention,
      not a config value) directly in its test rather than adding config keys. Narrowed by M3 PR 9: per-aircraft
      surface limits live with the aircraft's own actuator model (M4+), not the ICD — `astra_sw_to_sim_v1`'s
      `elevator`/`aileron`/`rudder`/`throttle` fields carry a commanded angle the sim does not clamp in v1; the
      ICD does not carry limits. Not started.
- [ ] (M7) `astra.trim`'s own hardcoded control-input names (origin: M3 PR 7 planning; D10, PR 6,
      `docs/plans/pr6-trim.md`) are not retrofitted to `InputRegistry::names()` by PR 7's own binding of it: PR 7
      reads control names through it for `astra.linearize` only, and `trim_straight_and_level` keeps its
      literals. Not started.
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
- [ ] (M7) ADR-0002's literal "no unit suffixes in identifiers (`altitude`, not `altitude_m`)" text (origin: M3 PR 3,
      ADR-0018) vs. `AtmoState`'s own field names (`temperature_k`, `pressure_pa`, `density_kg_m3`,
      `speed_of_sound_m_s`): whether the rule means signal-name strings specifically (its own example,
      `registry.add("aero.CL", ...)`, is a string) or every C++ identifier is unresolved; PR 3's own plan accepted
      the unit-suffix precedent rather than settling the wording. `AeroInputs`/`PropulsionInputs` (`alpha_rad`,
      `dynamic_pressure_pa`, `true_airspeed_mps`) are gone under ADR-0020; `AtmoState`'s field names are
      ADR-0020's cited precedent for whatever unit-suffixed fields a concrete plugin's own
      `air_data.hpp`-derived, plugin-side `Inputs`-shaped struct uses instead; the wording question is otherwise
      unchanged. Not started.
- [ ] (M7) §15 envelope monitors (α, β, Mach, dynamic pressure, load-factor limits declared by the aero model)
      (origin: deferred from M3 PR 3, ADR-0018): no declaration hook exists yet on `AeroModel`, because no concrete
      plugin had real numbers to declare before M3 PR 4. A later PR adds
      the declaration virtual (a further `kCoreApiVersion` bump, precedented) and wires `HealthMonitor`'s own
      per-frame check against it. Not started.
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
- [ ] (unscheduled) Evaluate Jev (TypeSafe AI) for workflow routing: model/effort selection at session launch and context
      selection, advisory with today's fixed settings as the fallback and file-path rules for Tier 3 kept
      (origin: Joe, 2026-09-26; owner: Joe, starting with console.typesafe.ai signup).
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
- [ ] (M6) Configs round-tripped through PyYAML (YAML 1.1: `yaml.safe_load`, then `Config.from_dict`) in
      `astra.montecarlo`, `astra.scenario`, `astra.trim` and `astra.lockstep` are read differently from the core's
      yaml-cpp (origin: M6 PR 3 review; Joe 2026-09-30). Probed on M6 PR 3's build: `on`/`off`/`yes`/`no` become
      booleans, which the core then reads as `true`/`false` (so `sim.health.nan: off` is refused); `010` is 8 (the
      core reads 10); `1:30` is 90 and `1_000` is 1000 (the core reads neither as a number). `astra run --log`
      avoids the round trip. Not started.
- [ ] (M6) `tests/python/resource_refs.py`, the self-contained-document check R3b uses and PR 8's report will
      reuse, is an enumeration of loading constructs, so anything unlisted passes: known misses are an
      `<?xml-stylesheet?>` processing instruction, CSS `image-set()` with a bare string, and SMIL `<set>`/`<animate>`
      on `href` (origin: M6 PR 5, Joe's review, 2026-10-01). PR 8 replaces it with an approved set: any element,
      attribute or CSS function outside an allowed set is flagged. Not started.
- [ ] (M6) `aircraft/_template/plots/ground_track.py` draws without an equal aspect ratio, so near-straight flight
      autoscales the east axis around zero and exaggerates a tiny lateral drift (origin: M6 PR 5, Joe's review,
      2026-10-01). Not started.
- [ ] (M6) Legends cover data: the `ax.legend(loc="upper right")` call in `python/astra/plot.py` puts every
      legend inside the axes, over TAS's end, alpha's right edge, beta and phi (origin: M6 PR 5, Joe's look at a
      60 s run, 2026-10-01). Place them outside the axes, which keeps the SVG deterministic. Not started.
- [ ] (M6) `python/astra/plots/flight.yaml` puts TAS and CAS on one panel, which spans 270 to 475 kt and flattens
      both (origin: M6 PR 5, Joe's look at a 60 s run, 2026-10-01). Not started.
- [ ] (M6) A 60 s run gives a 25 MB HTML page, because every 1 ms frame is plotted (origin: M6 PR 5, Joe's look
      at a 60 s run, 2026-10-01). Consider an opt-in decimation for HTML, and perhaps SVG. Not started.
- [ ] (M7) §18 step 4 fails on a template copy: `aircraft/_template/configs/cruise.yaml` uses `earth/flat`, which
      `astra.trim` refuses (altitude is geodetic only), so `astra trim` on a new aircraft exits 65 (origin: M6 PR 3;
      Joe 2026-09-30). M7 builds the F-16 from the template. Not started.
- [ ] (M10) Retire the M4 `ExchangeRecorder` once a lockstep test compares HDF5 logs instead (origin: M5 plan Q12,
      ADR-0022; Joe's call): kept through M5 because M4's byte-identity acceptance tests
      (`closed_loop_acceptance_test.cpp`) read it; M5's log already carries `v0.controls.*` and `transport.*`
      for lockstep runs. Retiring it would also moot Follow-up C's recorder-sizing item above. Not started.
- [ ] (M10) Wind events in scenarios (origin: M5 plan Q14; Joe's call): `astra.scenario` events are `write_input`
      only; there is no write seam for wind, so a wind change is a config choice (for example `wind/uniform`),
      not an event, until a seam exists. Not started.
- [ ] (M10) Closed-loop (autopilot) scenarios and Monte Carlo (origin: M5 plan Q16; Joe's call): not in M5, since
      adding the autopilot peer means subprocess management in the harness; M4's own acceptance already covers
      closed loop. Not started.
- [ ] (M10) Replaying a lockstep run (origin: M5 plan Q11; Joe's call): `astra.log.replay` refuses a log with a
      `transport` block, since replaying commands without the peer is a different feature. Not started.

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
