# M5 PR 6 (Tier 2): `astra.scenario`, the scenario harness

Recovered from `~/.claude/plans/pasted-content-id-a280-m5-pr-vast-hopper.md` on 2026-09-30. Approval: the file records none; the work merged as #68. Never committed: neither the file nor git history says why.

## Context

The plan's PR 6 row (`docs/plans/m5-tooling.md`, Q14 to Q16, R11 to R13; ADR-0025) calls for a YAML scenario
harness: each file under `tests/scenarios/` becomes one pytest item. The item runs open loop on the notional
transport, writes its own HDF5 run log, and checks bands read back through `astra.log.load`. It is also the
consumer-side assertion that PLAN.md's open item "Record how a run log was finalized" asks for. The
`Simulation` destructor also writes `complete = 1`, so the harness itself has to prove that the run was the
one it asked for. There is no `core/` change and no new dependency. Base: `origin/main` `0cd3f78`. Work goes
in a new worktree, `worktree-m5-pr6-scenario`. Commit, push and open the draft PR at the first commit.

## Files

- `python/astra/scenario.py` (new; pyright strict): `load`, `run`, `verify`, errors, result types.
- `tests/scenarios/conftest.py` (new, thin): the `pytest_collect_file` hook, `ScenarioFile`, `ScenarioItem`.
- `tests/scenarios/*.yaml`: the five Q15 seed scenarios.
- `aircraft/notional_transport/configs/cruise_still_air.yaml` (new): `cruise.yaml` verbatim without
  `physical.wind`, with a header explaining why.
- `tests/python/test_scenario.py`, plus `tests/python/scenario_fixtures/` for the R12 run-time negative
  fixtures and the one warn-counter config they need.
- `pyproject.toml`: `testpaths = ["tests/python", "tests/scenarios"]`. `pyrightconfig.json`: `include`
  gains `tests/scenarios` (standard level).
- `PLAN.md`: the PR 6 line. `docs/reviews/evidence/<head>-gate-summary.log`.
- Not touched: `tests/scenarios/**` stays out of the oracle guard (Q15). No file name under `tests/` contains
  "tolerance", because the guard's regex blocks such paths.

## `astra.scenario` design

**Reuse.**
- `astra.log.load` (strict, never `allow_incomplete`) and `astra.log.replay` (in tests).
- `astra.trim.trim_straight_and_level`, called on a temp YAML copy the way `lockstep.trim_for` does it
  (`yaml.safe_dump(sort_keys=False)` round-trips floats exactly).
- `_core.Config.from_dict`, `Simulation.input_names()`, `registry.signals`, `HealthCheck`.

**Schema (Q14).**

Top-level keys:
- `config`: a path relative to `root`. `root` is the pytest rootdir, and the tests pass the repo root.
- `overrides`: `{dotted.path: number}`. Integer segments index lists, for example `initial.state.p_n.2`.
  Only an existing numeric (non-bool) leaf can be overridden, and the new value must be a finite number.
- `trim`: `{true_airspeed, altitude}`.
- `duration`.
- `events`: a list of `{at, write, value}`.
- `checks`: a list of `{signal, window, within | between | angle_within}`, where:
  - `within` is `{target, tol}`;
  - `between` is `{lo, hi}`;
  - `angle_within` is `{target, tol}`, wrap-aware;
  - `window` is `final` or `{from, to}`.
- `expect_halt`: `{check, source, frame}`. `frame` was added per Joe's PR 5 carry-over.

Every map is closed: an unknown key at any level is a `ScenarioError`, and so is a wrong type. The key names
carry no unit suffixes (ADR-0002).

**Collection-time validation (`load`).**

Anything wrong here raises `ScenarioError`, which names the file and the key path. `load` does the following,
in order:
1. Parse the schema.
2. Refuse a base config with a `transport` block (Q16). This happens before anything is built.
3. Refuse a base config with a `log` block, because the harness owns the log.
4. Apply the overrides.
5. Validate the times. A time is valid when the decimal the author wrote, `Fraction(repr(x))`, divided by
   `Fraction(repr(sim.dt))`, is an integer. That rule is exact, not a float comparison. After that point
   everything is integer frames.
   - `duration` must be greater than 0.
   - Each event needs `0 ≤ at < duration`.
   - Each window is half-open, `[from, to)`, with `0 ≤ from < to ≤ duration`. It must also end at or before
     `expect_halt.frame`.
6. Refuse a non-finite event value.
7. Refuse an `expect_halt.check` of `NONE`, `LOG` or `TRANSPORT`, or one that is not a `HealthCheck` name.
   `expect_halt.frame` must be below the duration's frame count.
8. Build a `Simulation` from the resolved config, with no log and no trim. Check each signal name against
   `registry.signals` and each `write` against `input_names()`. A `ConfigError` from this build is a
   collection error too.

**Run (`run(scenario, log_path)`).**
1. Resolve the config dict. If `trim` is set, write a temp YAML of the dict **without `log`**, call trim, and
   apply its result to the dict:
   - `p_n`, `v_b` and `euler_nb = [0, α, 0]` from the trim state;
   - `omega_b = 0`;
   - `vehicle.initial_controls.controls.{elevator, throttle}`.

   Only after that, add `log: {path: log_path}`.
2. Build the `Simulation`. Apply events through `write_input` before the step of their frame. The loop has
   the same shape as `astra.log._rerun`, using `sim.run(next_event_frame − current)`, and it stops at a halt.
3. Call `sim.close_log()` explicitly. Keep `sim` referenced until `load` returns, so a missing close shows up
   as a refused log and never as one the destructor finalized.
4. If anything raises before the log has loaded, drop `sim` and unlink `log_path`, the same as `replay`
   does. An aborted harness run then never leaves a log that looks complete.
5. `load(log_path)` strictly, then call `verify`.

**`verify(scenario, log) -> list[str]`** is pure, so every assertion can be fed a real or modified `RunLog`
in tests.
- `frames_run == duration_frames` exactly, or, with `expect_halt`, `frames_run == expect_halt.frame`.
- No halt without `expect_halt`. With `expect_halt`, the recorded halt must match its check, source and frame
  exactly, and `frames_run` must equal that frame. A missed halt is a failure.
- Warn counters are `health.nan`, `health.quaternion`, `health.domain` and `health.input`.
  `health.max_q_drift` is a drift, not a counter. Each counter must be 0. The one exception is the expected
  halt's own counter, which must be exactly 1: the health monitor counts a halting detection before it halts,
  and with a `halt` action it cannot have counted warnings.
  - If the log's `health.*` set is not exactly those five names, that is a failure. A new counter then forces
    a deliberate update and is never silently ignored.
- Checks read only the log's rows. Each failure names the scenario, `checks[i]` and its kind, the signal, the
  window in seconds and frames, the worst value, and its frame and time. The worst value is the one with the
  largest violation; a NaN counts as the worst.
  - `angle_within` wraps `x − target` to [−π, π].
- All failures are collected into a single message.

**Definition of "final" (Joe's carry-over; recorded in the docstring, the PR body and a test).** `window:
final` is the last logged row, frame `frames_run − 1`, at t = (frames_run − 1)·dt. Per ADR-0025, that row
holds the frame-start values of the last frame that ran. `/final_state` is never used by checks, for two
reasons: it holds state only, not the derived signals or controls, and one definition should cover every
signal. The one-frame offset from `duration` is disclosed.

**conftest.**
- `pytest_collect_file` yields a `ScenarioFile` for each `*.yaml`, and refuses a `*.yml` so that it is never
  silently skipped.
- `ScenarioFile.collect()` turns a `ScenarioError` into `self.CollectError`, which is a collection error and
  never a skip.
- `ScenarioItem.runtest()` runs in a `tempfile.TemporaryDirectory` and raises `ScenarioFailure` if `verify`
  returned failures. `repr_failure` prints the message.

## Seed scenarios (Q15)

All five run on `cruise_still_air.yaml`. Each band is argued in a YAML comment before the first run, and the
YAMLs are committed before anything runs. Numbers below are the planned arguments; implementation recomputes
them from the config's coefficients.

1. **`cruise_hold.yaml`**: `trim {230, 10000}`, 60 s. This is exactly PR 8's unperturbed still-air run.
   Accepted evidence (`docs/plans/pr8-cruise-regression.md` findings 6 and 8) shows altitude up at most
   +5.8 m, u within [−0.193, +0.012] m/s, and heading drifting −3.2° over 300 s. Checks:
   - `v0.derived.altitude` within 10000 ± 20 m;
   - `v0.derived.theta` within α_trim ± 5e-3 rad (γ amplitude ≈ h·ω_p/V ≈ 1.5e-3);
   - `v0.derived.psi` `angle_within` 0 ± 0.056 rad (the 300 s drift);
   - all over [0, 60).

   This scenario is also the one that exercises trim through the harness.
2. **`elevator_step.yaml`**: 1.5 s, elevator set to trim − 0.01 rad at 1.0 s. `cm_elevator_per_rad < 0`, so
   ΔCm > 0 and the pitch is nose-up, which is q > 0 in ADR-0001 FRD. The check is `v0.derived.q` between
   [1e-3, 0.02] rad/s over [1.1, 1.5).
   - q̇₀ = q̄ S c̄ Cm_δe Δδe / Iyy ≈ 0.019 rad/s².
   - At 0.1 s after the step, q ≈ 1.9e-3 rad/s; pitch damping, at about 0.49 s⁻¹, takes about 2.5 % off that.
   - `hi` is twice the undamped bound q̇₀·0.5 s.
3. **`aileron_step.yaml`**: 1.5 s, aileron set to +0.01 rad at 1.0 s. `croll_aileron_per_rad > 0`, so the
   rolling moment is positive and p > 0 (right wing down). The check is `v0.derived.p` between [1.5e-3,
   0.035] rad/s over [1.1, 1.5).
   - ṗ₀ ≈ 0.034 rad/s², with a roll-damping time constant of about 0.88 s. At 1.1 s, p ≈ 3.2e-3 rad/s.
4. **`throttle_step.yaml`**: 20 s, throttle set to trim + 0.1 at 1.0 s.
   - ΔT ≈ 5.98 kN, so ΔV̇ ≈ 0.159 m/s².
   - With the accepted phugoid (108.57 s, ζ 0.027), ΔV(t) ≈ (ΔT/m)·sin(ω t)/ω, which is 0.63 m/s at 4 s and
     2.45 m/s at 19 s after the step. The unperturbed drift is at most 0.193 m/s.
   - The check is `v0.derived.tas` between [230.2, 235] over [5, 20).
5. **`atmosphere_ceiling_halt.yaml`**: overrides `p_n.2 = 85950`, `v_b = (230, 0, −100)` and `euler_nb.1 = 0`,
   so the aircraft climbs at 100 m/s through ISA 1976's 86 km top. Near vacuum (q̄ ≈ 0.2 Pa, thrust about
   1 N) makes the flight ballistic.
   - The net vertical acceleration is about −9.51 m/s²: J2 gravity at 86 km, less the centrifugal and
     curvature terms.
   - The crossing is at t ≈ 0.5125 s, so the halt should be at frame 512. The margin is about 0.5 ms either
     side, against roughly 0.07 ms of uncertainty.
   - `expect_halt {OUT_OF_DOMAIN, plugins/physical/atmosphere/isa1976, 512}`, plus a `final` check that
     `v0.derived.altitude` is in [85990, 86000].
   - The Earth and gravity domains have no upper bound (`wgs84_rotating`, `wgs84_j2`), so ISA is the first
     check to fire.

If a seed scenario fails, I report it with its measured worst value and never move the band. Every band goes
under the PR body's "Needs Joe's decision", with its measured worst value.

## Tests (`tests/python/test_scenario.py`; all selected by `-k scenario`)

- **R11 meta-test.** A subprocess runs `pytest --collect-only -q tests/scenarios`. The set of collected node
  ids must equal `{<f>::<stem>}` over `tests/scenarios/**/*.yaml`, and so must the count.
- **R12, run-time fixtures** (`tests/python/scenario_fixtures/`, all cheap):
  - a violated check (TAS between 0 and 100), whose message names the signal, the window, the worst value and
    the frame, which equals the log's argmax;
  - an unexpected halt (`tests/configs/health_nan_wrench.yaml`, NON_FINITE_WRENCH at frame 500);
  - a nonzero warn counter (the same plugin with `sim.health.nan: warn`, plus `domain: warn` if the flat
    Earth's domain would otherwise halt on NaN);
  - a missed `expect_halt`;
  - a wrong halt frame, a wrong source and a wrong check.
- **R12, collection errors.** YAML strings written to `tmp_path` for:
  - an unknown top-level key and an unknown nested key;
  - an unknown signal and an unknown input;
  - a `duration`, an event `at` and a window that is not a multiple of dt (0.0005);
  - an override path that does not exist, and one that points at a non-numeric leaf;
  - a `transport` config and a `log` config;
  - `expect_halt LOG`;
  - a window past the duration or past the halt frame, and an event at the duration.

  One subprocess case copies the conftest into `tmp_path` and proves pytest exits INTERRUPTED with the error
  (a collection error, not a skip).
- **Carry-over assertions, each seen to fire:**
  - `frames_run` ≠ duration/dt: a real short log is verified against a longer scenario.
  - `frames_run` ≠ halt frame: `dataclasses.replace` on a `RunLog`.
  - No explicit close: monkeypatch the harness's `_close_log` seam to a no-op. `run` must then raise
    (`LogFormatError` or `IncompleteLogError`) and leave no file behind.
  - The trim copy: spy on trim and assert that the file it is given has no `log` key and that `log_path` does
    not exist during the trim.
- **"final":** a ballistic config (`tests/configs/physics_ballistic_level.yaml`) with a violated final check.
  The reported frame must be `frames_run − 1`, its value must equal the last row, and it must differ from
  `/final_state`.
- **Events and replay:** a short cruise scenario with an elevator event at 0.1 s. `/writes` must hold (100,
  `controls.elevator`, value). Row 99's `v0.controls.elevator` must be the trim value and row 100's the new
  value. `astra.log.replay(log, out)` must return `True`.
- **Check math:** a wrap unit test (target π, value −π + ε passes), a NaN counting as worst, and inclusive
  `between` bounds.
- **Still-air config:** it must equal `cruise.yaml` with only `physical.wind` removed (the dicts are compared).

## Verification

1. `.venv/bin/pytest tests/scenarios tests/python -k scenario` exits 0 (R11 to R13).
2. `.venv/bin/pytest tests/scenarios --durations=0` on the Release module gives the added wall time for the
   PR body.
3. `./scripts/check-full.sh` exits 0 with `check: OK`; commit the trimmed gate summary.
4. `/review-cycle`: a Codex Tier 2 review, with findings verified. Then `/human-review` writes the PR body per
   `docs/templates/pr-description.md`. The body covers:
   - the "final" definition;
   - the consumer-side assertion for the finalization open item;
   - the gate cost;
   - the seed results against their bands;
   - "Needs Joe's decision", which covers the bands, the `expect_halt.frame` schema addition, and
     `window: {from, to}` being half-open.
