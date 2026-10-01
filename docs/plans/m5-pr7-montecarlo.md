# M5 PR 7 (Tier 2): `astra.montecarlo`, sampler, `summary.parquet`, the Q19 64-run case

Recovered from `~/.claude/plans/pasted-content-id-e636-m5-pr-optimized-sutton.md` on 2026-09-30. Approval: the file records Joe's amendments at approval; the work merged as #69. Never committed: neither the file nor git history says why.

## Context

The PR 7 row of `docs/plans/m5-tooling.md` (Q2, Q17 to Q20; R14 to R17, R15b), binding ADR-0005/0010/0025.
Architecture §16's M5 row needs "a Monte Carlo of 64 dispersed runs completes" under `just check`. PR 5 gave
`astra.log` (strict `load`, `replay`); PR 6 gave `astra.scenario` (overrides, `TRIM_OWNED`, trim on a copy,
the explicit-close/strict-load/unlink run pattern, the frames_run consumer assertion). PR 7 builds the batch
tool on those and reuses them instead of copying. Base `origin/main` = `6227a17` (verified by `git fetch`).

Observed facts that shape the design:
- pyarrow 25.0.1 is installed and is the latest on PyPI; it ships **no `py.typed`** and no `.pyi` (checked), so
  the lessons 2026-09-21 treatment applies (`cast` at the boundary, narrow `pyright: ignore[...]` per rule).
- Python 3.12.3, 32 cores, 401 GB free. PR 6's 60 s `cruise_hold` scenario runs in well under a second, so the
  60 s Q19 case likely fits; it will be measured, not assumed.
- `cruise.yaml` leaves: mass `physical.mass.mass`, aero reference x `vehicle.models.0.r_aero_b_x_m` (0.3556),
  Dryden seed `physical.wind.seed` (plugin requires 0 <= seed <= 2^53). None is in `TRIM_OWNED`; trim drops
  wind (still air). The mass plugin refuses mass <= 0 (`ConfigError` naming `physical.mass.mass`).
- C++ `astra::random::Rng` (`core/include/astra/util/random.hpp`): uniform01 = top 53 bits × 2^-53;
  Box-Muller with u1 = 1 − uniform01, caches the sin variate. The Python sampler mirrors it.

## Setup

`EnterWorktree` → worktree `m5-pr7-montecarlo`, branch off `origin/main` `6227a17`. `just python` there.
Worktree guard (lessons 2026-09-26/27): multi-command or regex commands go in a scratchpad script (Write,
`chmod +x` separately, run by path); git commands plain and alone; PR body via Write + `gh api -X PATCH
.../pulls/<n> -F body=@file`.

## Step 1: factor shared helpers out of `python/astra/scenario.py` (behavior and messages unchanged)

Mechanical; `tests/python/test_scenario.py` and `tests/scenarios` must pass untouched.
- **Leaf rules**: `_override`'s walk becomes `numeric_leaf(config, key)` (read) and
  `set_numeric_leaf(config, key, value)`, raising `LeafError` with today's reason text ("no key 'x' in the
  config", "'3' is not an index of a 2-item list", "... is below a scalar", "the config's value ... is not a
  number"). `_override` keeps its `_number` check first and re-raises `ScenarioError(f"{path}: {where}:
  {reason}") from None`: same type and message.
- **TRIM_OWNED**: `trim_owned_reason(key) -> str | None`; `_refuse_trim_owned` formats its unchanged message.
- **Trim on a copy**: `_apply_trim`'s body becomes `apply_trim(data, setpoint, *, origin, config_name)`
  (still refuses a `log` block in `data`; same D8 bound and message); `_apply_trim` calls it.
- **Run pattern**: `run_logged(data, log_path, *, origin, plugin_dir, drive) -> log.RunLog`: refuses an existing
  path, adds the `log` block, `from_config` inside the try, `drive(sim)`, `_close_log(sim)` (module global, so
  the existing monkeypatch still works), strict `log.load` while `sim` is alive, `except BaseException:` drop
  sim, unlink, re-raise. `scenario.run` keeps its early exists check, then trim, then `run_logged(...,
  drive=lambda sim: _drive(sim, scenario))`.
- **Consumer assertion**: `run_length_failures(run_log, frames) -> list[str]`: no halt → frames_run == frames
  ("frames_run is X, not duration / dt = N frames"); halt → frames_run == halt.frame ("frames_run is X, not
  the halt's frame F"). `verify` and `_halt_failures` produce those two strings through the same helpers.
- **Schema validators reused by the spec loader** renamed public, bodies unchanged: `_closed` → `closed_map`,
  `_number` → `number`, `_text` → `text`, `_frames` → `frames_of`, `_trim` → `trim_setpoint`.

Commit, push, open the draft PR (template `docs/templates/pr-description.md`, body marked in progress).

## Step 2: pin pyarrow

`pyproject.toml` dev group: `"pyarrow==25.0.1"` under a comment "M5 PR 7 (astra.montecarlo, Q2, approved by Joe
2026-09-26): current release at pin time, <date from `date -u`>; ships no py.typed". Update the group's header
comment ("pyarrow: M5 PR 7" → joined). `just venv` in the worktree. No other dependency.

## Step 3: `python/astra/montecarlo.py`

Docstring carries the whole contract (schema, sampler, clip, columns, failure policy, killed-worker decision).

**Spec (`load(path, *, root=None) -> Spec`)**, closed maps; every error a `SpecError` naming file and key
(shared validators' `ScenarioError` re-raised as `SpecError` with the same message):
```yaml
config: aircraft/notional_transport/configs/cruise.yaml   # transport block refused; log block refused
trim: {true_airspeed: 230.0, altitude: 10000.0}           # optional
duration: 60.0          # s, > 0, exact multiple of dt (frames_of)
runs: 64                # int >= 1
seed: 2026              # master seed, int in [0, 2^64)
dispersions:            # required, non-empty; drawn in file order
  physical.mass.mass: {normal: {sigma: 0.02, mode: relative, clip: 3.0}}
  vehicle.models.0.r_aero_b_x_m: {uniform: {lo: 0.2556, hi: 0.4556}}
  physical.wind.seed: seed
signals: [v0.derived.altitude, v0.derived.tas, v0.derived.alpha]   # optional: min/max columns
```
- Each key: `numeric_leaf` (same rules as overrides, list indices included). With `trim`, a key with a
  `trim_owned_reason` is refused: "dispersions.<key>: <reason>, so with trim set this dispersion would be
  discarded or leave the run untrimmed". `TRIM_OWNED` imported, never copied. `physical.wind.*` stays allowed.
- `uniform {lo, hi}`: absolute bounds of the drawn value, finite, lo < hi; value = lo + (hi − lo)·u.
- `normal {sigma, mode, clip}`: all required. absolute: value = base + sigma·z; relative: value = base +
  (sigma·|base|)·z, base ≠ 0. **clip is in standard deviations (dimensionless), > 0; a draw with |z| > clip is
  redrawn** (a truncated normal, no atoms at the bounds).
- `seed`: the base leaf must be an integer (not bool); draws an integer uniform on [0, 2^53) (top 53 bits of a
  64-bit draw), exactly representable as a double, as the Dryden plugin and the SignalRegistry require.
- `signals` must be registered: `load` builds a `Simulation` from the base config (no log, no trim), as
  `scenario.load` does; a config that does not build is a `SpecError`.

**Sampler**: `run_seed(master, index)` = first 8 bytes (big-endian) of SHA-256 over master and index each as
unsigned 64-bit big-endian; `Rng(seed)` wraps `random.Random(seed)` (int seed: stable MT19937 stream) and
mirrors the C++ transforms (`getrandbits(64) >> 11` × 2^-53; Box-Muller with cache). `draw(spec, index)` returns
the run seed and the values, one `Rng` per run, dispersions in spec order. No numpy distributions.

**Per run (`run_one(spec, index, out_dir) -> Row`, public so tests drive it in-process)**: deep-copy the config,
`set_numeric_leaf` each drawn value, `apply_trim` if set (copy without log; transport already refused), then
`scenario.run_logged(..., origin=f"{spec.path} run {index}", drive=sim.run(frames))`. Classification:
- any `Exception` (ConfigError from a drawn value, trim missing D8, close/log/load failure) → `status = error`,
  `error_type`, `error_message`; `run_logged` already removed the log, so none is counted;
- `run_length_failures` non-empty (consumer assertion) → error row `RunLengthError`, log removed;
- recorded halt `LOG` (a log write failure) → error row, log removed;
- other halt → `status = halt` with check/source/frame, log kept; otherwise `status = ok`.

**Batch (`run(spec, out_dir, workers=None) -> pd.DataFrame`)**: `spec` a `Spec` or a path; `out_dir` created if
missing and refused if non-empty. Draw every run in the parent first. Own pool: `get_context("spawn")`, `min(workers
or os.cpu_count(), runs)` `Process`es, one duplex `Pipe` each; workers set SIGINT to `SIG_IGN`, loop `recv` task
→ `run_one` → `send` row, exit on `None`. Parent assigns work and waits on `multiprocessing.connection.wait(conns
+ sentinels)`. A sentinel firing before shutdown, `EOFError` on `recv`, `KeyboardInterrupt` or any parent
exception → **kill and join every worker**, remove `run_<i>.h5` for every run not completed (a killed worker's
file is partial and unreadable, PR 5's finding; every remaining file is a complete, strictly loaded log), write
no summary, and raise `BatchError` (or `BatchInterrupted`, a `KeyboardInterrupt` subclass) naming the runs not
completed. On success: rows sorted by run, `summary.parquet` written with an explicit pyarrow schema to a temp
name then `os.replace`; wall time and real-time factor printed to stdout only.

**`summary.parquet` columns**: `run` int64, `seed` uint64, `status` string, one column per dispersion (float64;
int64 for `seed`), `error_type`, `error_message`, `frames_run`, `halt_check`, `halt_source`, `halt_frame`,
`health.*` (from `/summary`), `final.<vehicle>.<name>` (from `/final_state`), `min.<signal>`/`max.<signal>` for
`signals`; nullable where a row has no value. Disclosed limit: an error message that names the run's log path
differs between output directories.

## Step 4: tests, `tests/python/test_montecarlo.py` + `tests/python/montecarlo_fixtures/`

- **R14** (`cruise_64.yaml`, the Q19 spec above): `run(workers=None)`; 64 files `run_0000..0063.h5`, each
  `complete = 1` (h5py attribute read), 64 sorted rows, all `ok`, zero halts, zero errors, frames_run 60000.
  Teardown removes the logs (about 64 × tens of MB). Timed.
- **R15**: an 8-run, 1 s variant; `workers=1` vs `workers=4` into different dirs: `summary.parquet` and every log
  byte-identical; each drawn value equals the leaf in the log's stored config text (parsed, `numeric_leaf`) and
  the summary, bitwise; a different master seed changes every dispersed value.
- **R16**: `run_seed` vs an independent `hashlib` computation; recorded draws as `float.hex` literals (uniform01,
  normal, a spec's `draw` for runs 0 to 2); 10^5-draw moments within 5 standard errors: uniform (mean,
  variance), unclipped normal (clip 50), clipped normal at 2σ against `scipy.stats.truncnorm` moments.
- **R17**: one batch run's log passes `astra.log.replay` (returns True).
- **R15b**: mass uniform on [−40000, 40000], fixed seed, short, no trim: `error` rows name
  `physical.mass.mass` with no log, other rows present, summary written; a worker SIGKILLed mid-batch (helper
  thread waits for the first `run_*.h5`, kills an `active_children()` pid): `BatchError` names incomplete runs,
  no summary, their logs absent, remaining logs load strictly, no live child; SIGINT to the main thread
  (`signal.pthread_kill`): `BatchInterrupted`, same checks; a transport config (`cruise_light_turbulence.yaml`)
  is a `SpecError`.
- **Carry-overs, each seen to fire** (in-process `run_one`, monkeypatch): `scenario._close_log` a no-op → error
  row, no file; a drive that raises mid-run → error row, no file; a drive one frame short → `RunLengthError`
  row, no file; a halting run (dispersed altitude past the atmosphere ceiling) → `halt` row with frames_run =
  halt frame, and a tampered `frames_run` → error row; a `LOG` halt (patched `log.load`) → error row; each
  `TRIM_OWNED` prefix as a dispersion with trim → `SpecError` naming key and reason, and `physical.wind.seed`
  accepted; a trim spy sees a config without `log`/`transport` and no run file yet.
- Schema negatives: unknown key, non-leaf/typo'd path, lo >= hi, bad mode, missing clip, relative on a zero base,
  `seed` on a float leaf, unregistered signal, non-multiple duration, non-empty `out_dir`.
- One scratch mutation per key guard (explicit close, strict load, unlink on raise, run-length check,
  TRIM_OWNED refusal, batch cleanup), applied with Edit, confirmed by grep, test run separately, reverted;
  results in the PR body.

## Step 5: docs and gate

- `docs/process/validation.md`: a `.venv/bin/pytest tests/python -k montecarlo` row. PLAN.md: the PR 7 line.
- `./scripts/check-full.sh` exit 0 with `check: OK`; commit `docs/reviews/evidence/<head>-gate-summary.log`
  with the acceptance run and the measured Q19 and whole-pytest wall times (baseline: 275 passed in 37.59 s).
- `/review-cycle` (Codex, Tier 2); `/human-review` fills the PR body; `/wrap`.

## Verification

- `.venv/bin/pytest tests/python -k montecarlo` exit 0 (R14 to R17, R15b).
- `.venv/bin/pytest tests/scenarios tests/python -k scenario` unchanged pass count (64) after Step 1.
- `ruff`, strict pyright on `python/astra` (inside the gate). `./scripts/check-full.sh` exit 0, `check: OK`.
- Measured: Q19 test wall time and the added pytest time, reported; if 60 s does not fit, the measurement goes to
  Joe; no timeout raised, the case never leaves the gate.

## Joe's amendments at approval (these override the steps above)

1. Q19 disk cost: measure total log size and peak disk use beside wall time; out_dir under `tmp_path`,
   removed by a finalizer that also runs on failure.
2. Error messages recorded with the out_dir prefix replaced by a fixed placeholder; R15's workers=1 vs N
   byte identity also run on the R15b error batch.
3. Q19 disclosure: dispersing `physical.mass.mass` leaves the inertia tensor unchanged.
4. Shared helpers go in a private module `python/astra/_runkit.py` (leaf rules, TRIM_OWNED + reason,
   TrimSetpoint + parse, apply_trim, run_logged, run_length_failures, the schema validators raising
   `SchemaError`); scenario.py imports them, keeps `TRIM_OWNED` importable, wraps `load` to re-raise
   `SchemaError` as `ScenarioError` with the same message; `run_logged` takes a `close` callable so
   `scenario._close_log` stays monkeypatchable. No scenario.py internals made public.

## Needs Joe's decision (for the PR body)

Q19 duration (with the measurement) and dispersions (normal 2 % relative clipped at 3σ with redraw; uniform
0.2556 to 0.4556 m; Dryden seed on [0, 2^53)); the schema readings (uniform as absolute bounds, clip in σ with
redraw, relative σ = sigma·|base|, draw order = spec order in one per-run stream); a consumer-assertion failure
and a `LOG` halt as `error` rows; completed logs kept after a failed batch; the added `health.*` columns; the
scenario.py helpers made public; approval of the candidate.
