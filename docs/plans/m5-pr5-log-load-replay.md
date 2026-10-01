# M5 PR 5 (Tier 2): `astra.log` load and replay

Recovered from `~/.claude/plans/pasted-content-id-0523-m5-pr-eager-teacup.md` on 2026-09-30. Approval: the file records Joe's changes at approval; the work merged as #67. Never committed: neither the file nor git history says why.

## Context

ADR-0025 fixed the run-log format and PR 4 (#66, merged as `3324d1e`) writes it from `Simulation`. PR 5 adds
the Python reader every later M5 tool builds on (the scenario harness, Monte Carlo and FlightGear bridge):
`astra.log.load` (pandas per rate group) and `astra.log.replay` (re-execution checked by byte identity). It also
pins h5py 3.16.0 and pandas 3.0.5 (Q2, already approved). Joe's PR 4 review adds four carry-overs:
- R8 must show that h5py imported first does not trigger the core's refusal to log (F2).
- `load` must check internal consistency.
- `load` must refuse a file whose `close_log()` failed (R6b's load half).
- Optionally, correct `Session.flush_log`'s docstring.

Scope is the plan's PR 5 row (`docs/plans/m5-tooling.md`); acceptance is R3, R8, R9 plus the carry-overs.

**Joe's decision (this session), on a finding from reading `simulation.cpp:617` and `run_log.cpp:350`.** When a
`Simulation` is destroyed mid-run, its destructor runs the full `close()`. That writes a self-consistent file
with `complete = 1`, which the consistency check cannot flag. The carry-over's premise was wrong and the PR body
says so plainly. Decided treatment:
- **Accept and disclose.** The likely case is a Python driver that raises mid-run: its `Simulation` goes to GC,
  and the destructor writes a consistent file with `complete = 1`.
- **Test.** In a child process, drop the `Simulation` after N < `sim.frames` steps. `load` then returns
  `frames_run == N`, no halt, and `complete`.
- **PLAN.md open item.** Record how a log was finalized (`close_log()` vs the destructor) so that `load` can
  flag it, not refuse it, because `Session` relies on the destructor path. This changes ADR-0025's
  Finalization, so it needs a superseding ADR (Tier 3); scheduling is Joe's call. PR 6 and PR 7 consumers must
  assert `frames_run == duration/dt` and that there was no unexpected halt; PR 5 does not build that.
- **Wording.** Never claim the check catches "corruption". It checks row counts against `frames_run` and that
  frames are gap-free, not values.

## Setup

- `EnterWorktree` → a new worktree `m5-pr5-log` off `origin/main` (`3324d1e`, verified by `git log`). Then run
  `just venv` and `just python` there.
- Commit and push as I go. Open the draft PR at the first commit; its body follows
  `docs/templates/pr-description.md`.

## Changes

**`pyproject.toml`.** Add `h5py==3.16.0` and `pandas==3.0.5` to the dev group with a dated M5 PR 5 comment, and
update the "join when a milestone uses them" comment. Both versions are already installed and work with numpy
2.5.3. The bundled libhdf5 is **2.0.0** (`h5py.libs/libhdf5-9e18f0c6.so.320`), not the 1.14 the plan's risk
section assumed. I'll disclose that; it changes nothing, since the soname is distinct and h5py loads it
`RTLD_LOCAL`.

**`python/astra/log.py` (new; pyright strict).** h5py and pandas have no stubs, so I confine them to narrow
`cast(Any, …)` sites with `# pyright: ignore[reportMissingTypeStubs]` on the imports. No stub packages are
added (that would be a new dependency). `astra/__init__.py` does not import it, so `import astra` never pulls
in h5py.

- **Errors.** `LogError`-style typed exceptions local to the module:
  - `IncompleteLogError`: no `complete = 1`, or an inconsistent file.
  - `LogFormatError`: unopenable, or an unknown `log_format_version`.
  - `ReplayError`: a refusal, a halt not reproduced, or an early halt.
- **`load(path, *, allow_incomplete=False) -> RunLog`**, where `RunLog` is a frozen dataclass:
  - `rates: dict[int, pd.DataFrame]`: per divisor, index `frame` (uint64), a `t` column equal to
    `frame.astype(float64) * dt` (the same expression as `VirtualClock::t()`), then the signal columns in file
    order.
  - `units: dict[str, str]`.
  - `dt`, `frames_run`, and `halt` (check, source, frame, vehicle; `None` for `"NONE"`).
  - `complete: bool`, `config: str`, and `provenance` (`git_describe`, `core_version`, `core_api_version`,
    `log_format_version`).
  - `plugins` and `writes` as DataFrames (names decoded).
  - `health: dict[str, float]`, `minimum`/`maximum: pd.Series`.
  - `final_state: dict[str, pd.Series]` plus its `frame` and `after_halt`.
- **Checks, in order.**
  1. The file opens and its format version is known.
  2. The root attribute `complete == 1`.
  3. Consistency:
     - `/summary` exists with `frames_run`;
     - for every `/rate_<d>`, `frame` equals `arange(0, frames_run, d)` exactly: row count `ceil(frames_run/d)`
       and gap-free;
     - `values.shape == (len(frame), len(names))` and `len(units) == len(names)`.

  Step 2 or 3 failing raises `IncompleteLogError` naming the failed check. `allow_incomplete=True` skips
  steps 2 and 3 and loads whatever exists, with missing parts as `None`.
- **`replay(path, out, *, plugin_dir=None) -> bool`** (ADR-0025 Q11):
  1. Load `path` strictly: an incomplete or inconsistent log is refused.
  2. Refuse a stored config with a `transport` block, or a recorded halt of `LOG`, each with a `ReplayError`
     naming the reason. I detect `transport` by parsing the stored text with PyYAML (already pinned).
  3. Build the config as the stored text plus `log:\n  path: <json-quoted out>\n`, then `Config.from_string`.
     No `buffer_frames` is needed: PR 4's staging block makes the bytes independent of it. `from_config` uses
     `plugin_dir` (default `_core.default_plugin_dir()`).
  4. Walk the `/writes` frames in order.
     - `run(n)` covers each gap. It drains at the high-water mark, so a `step()`-only loop cannot fill the ring.
     - Each write is applied through `write_input` before the step of its frame. For a non-finite value,
       `_core.Error` is caught (the core recorded the write before latch raised); any other error is re-raised.
     - An unexpected halt before `frames_run` is a `ReplayError` naming the frame.
  5. Apply the writes at `frames_run`. For a halted log, make one `step()`, which must equal the recorded
     halt's check, source, frame and vehicle, or `ReplayError`.
  6. `close_log()`, then return `out.read_bytes() == path.read_bytes()`.

  Disclosed limits (docstring and PR body):
  - Writes made after a halted call carry the same frame as those before it, so replay applies all of them
    before the step.
  - A run that continued past a non-sticky halt is not replayable (step 4's error).

**`python/astra/lockstep.py`** (the optional carry-over). Fix `Session.flush_log`'s docstring: rows leave the
ring for a 1024-row staging block and reach disk only when a block fills or at `close_log()`.

**`tests/python/test_log.py` (new).** Every name contains "log" or "replay", so `-k 'log or replay'` selects them.
Runs are built with `Config.from_dict`/`from_file` + `default_plugin_dir()`, as `test_simulation.py` does. The
fixtures are Dryden `aircraft/notional_transport/configs/cruise.yaml` (a few thousand frames, crossing the
1024-row block), `tests/configs/health_nan_wrench.yaml`, and the loopback `tests/configs/no_alloc_transport.yaml`,
each with a `log` block under `tmp_path`.
- **R8 load.**
  - One DataFrame per rate group, indexed by `frame` = 0..N-1.
  - `t` is bitwise `frame * dt`.
  - Columns and units equal `sim.registry.signals` names and units, in order.
  - Provenance, `/writes`, `/summary` (`frames_run`, halt, health) and `/final_state` equal the live `Simulation`
    (`final_state` against `vehicles[0].state`).
- **R8 carry-over (subprocess).** `python -c` runs `import h5py` first, then `import astra._core`, writes a
  logged run and `close_log()`s it, then reads it with h5py. It asserts `has_log`, no `ConfigError`,
  `complete == 1` and the expected rows, and reports both HDF5 versions (h5py 2.0.0 vs the system 1.10.10).
  **Positive control**, in the same test module, so the detector is seen to fire: a child that calls the system
  `libhdf5_serial.so.103`'s `H5open()` through ctypes before importing `astra._core` gets the `ConfigError` at
  `log.path`. If the h5py-first case refuses, I stop and bring it to Joe (the from-source h5py fallback is his
  call).
- **R3.** Dryden cruise, `step()`-driven with `flush_log()`.
  - Before each step, copy `vehicles[0].state`. Row k's `v0.state.*` columns equal it bitwise.
  - In numpy: `v_rel_b = v_b − rotate_n_to_b(q_nb, [wind.north, wind.east, wind.down])`, with the quaternion
    product written out as in `frames.hpp:132`. Then `alpha = atan2(w, u)` and
    `beta = atan2(v, hypot(u, w))` (`air_data.hpp:35-43`) equal `v0.derived.alpha/beta` within 1e-12 rad at
    every row.
- **Consistency (seen to fire).** Tampered copies written with h5py, each refused, then loaded with
  `allow_incomplete=True`:
  - the last `/rate_1` row dropped (both datasets);
  - a gap in `frame`;
  - `frames_run` off by one;
  - `complete` deleted.

  A synthetic multi-rate file (`/rate_1`, `/rate_10`, `frames_run = 25`) is accepted with frames 0, 10, 20 and
  refused with 0, 10. No real signal has a divisor other than 1 today.
- **R6b load half.** A child sets `SIGXFSZ` ignored and `RLIMIT_FSIZE`, as in PR 4's `lockstep_child.py`
  `log-close-failure`, and runs a logged cruise. `close_log()` raises `LogError` and the child exits. `load`
  then raises `IncompleteLogError`. I also add the same one-line `load` assertion to the existing
  `test_session_log_write_failure_halts_and_close_log_raises`.
- **Dropped mid-run (Joe's test).** A child runs N < `sim.frames` steps and drops the `Simulation` without
  `close_log()`. `load` gives `frames_run == N`, `halt is None`, `complete`, and N rows. The docstring states
  that this is the disclosed gap.
- **R9 replay.** Each case asserts `replay(...) is True` with a different `out` path:
  - Dryden cruise;
  - mid-run `write_input` events (several frames, two writes in one frame, one after the last step);
  - the NaN-wrench halt (halt reproduced at frame 500);
  - a non-finite `write_input` leading to a `NON_FINITE_INPUT` halt.

  Refusals, each with a named reason: the loopback-transport log; a `LOG`-halted log (a small `buffer_frames`,
  `step()` without flush, then `close_log()`). Detectors seen to fire:
  - a copy with one `/rate_1/values` element changed replays to `False`;
  - a copy whose `halt_check` was edited raises `ReplayError` "halt not reproduced".

**`PLAN.md`.** Add the PR 5 line, plus the open item worded per Joe's decision above.

## Verification

1. While iterating: `.venv/bin/pytest tests/python -k 'log or replay'`, `.venv/bin/pyright`, `.venv/bin/ruff check .`
   and `ruff format --check`.
2. Acceptance: `.venv/bin/pytest tests/python -k 'log or replay'` exits 0, and each carry-over detector is seen
   to fire (the tests above).
3. Gate: `./scripts/check-full.sh` via Bash `run_in_background`, then read the exit status and `check: OK`.
   Commit the trimmed `docs/reviews/evidence/<head>-gate-summary.log` per `docs/process/validation.md#evidence`.
4. Review: `/review-cycle`, with one independent Codex review (Tier 2) on the committed branch against
   `3324d1e`. Verify its findings, with at most two correction rounds, and rerun the full gate after substantive
   fixes.
5. PR body (`/human-review`): commands, exits and counts, and the Codex verdict and thread id. Also:
   - Joe's corrected carry-over, stated plainly;
   - the libhdf5 2.0.0 finding;
   - replay's disclosed limits;
   - "Needs Joe's decision": approval, and scheduling of the finalization-marker open item.

   If the branch needs CI rerun after a later push, use `gh pr ready --undo` then `gh pr ready`.

## Joe's changes at approval (bind the above)

- Replay detects `transport` with `_core.Config.from_string(stored, ...).has("transport")`, not PyYAML (YAML 1.1 vs
  1.2). Before appending the `log` block: stored text must end with `\n` and have no top-level `log` key, else
  `LogFormatError`.
- More consistency checks: a recorded halt has `halt.frame == frames_run`; `/writes` frames never decrease and are
  all `<= frames_run`. One tampered-file case each.
- R8 positive control: cite PR 4's `test_a_log_is_refused_when_libhdf5_was_initialized_before_astra` (already
  selected by `-k 'log or replay'`); no second one. The h5py-first child opens a file with h5py before importing
  `astra._core`.
- Replay docstring and PR body: True requires the same build (git describe and plugin SHA-256s are in the file;
  a rebuilt plugin or dirty tree gives False even for a deterministic run).
