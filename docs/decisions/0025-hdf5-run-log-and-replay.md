# ADR-0025: HDF5 run log and replay

Status: proposed
Date: 2026-09-26
Owner: Joe
Related/supersedes: elaborates ADR-0005 (HDF5 through the C API), ADR-0010 (frame counter), ADR-0011 (vehicle
list), ADR-0022 (exchange stage and recorder) and ADR-0023 (clock exception). Supersedes nothing. Two readings are
stated here rather than superseded: ADR-0005's "the loaded plugin set with versions and build strings" is
realized as the plugin identity below, because `PluginDescriptor` has no version field; ADR-0022's "M5's HDF5
logger supersedes the recorder" is realized by a follow-up after M5, not within it. Drafted with
`docs/plans/m5-tooling.md` (decisions Q3 to Q12, critique C1 to C7).

## Context

ADR-0005 fixed the format family (HDF5, C API, RAII wrapper, rate-group datasets, provenance attributes,
overflow is a halt) but not what a row means, when buffers drain, how the logger fails, what the file contains,
or what replay is. Every M5 tool (`astra.log`, `astra.scenario`, `astra.montecarlo`, `astra.fgbridge`) reads this
format, and PR 4 puts the logger on the per-frame path, so these choices are expensive to unwind. Observed at
`d3e82e3`: published `v0.state.*` are end-of-frame while `v0.wrench_total.*` and the air data are frame-start, so
sampling pointers at §5's last position mixes t_{k+1} with t_k; `Simulation::step()`/`run()` are `noexcept` and
return a `HaltReason`; only transport halts are sticky; architecture §15 asks for wall time in the file, which
core cannot measure under ADR-0010.

## Decision

**Enabling (Q5).** A top-level `log: { path, buffer_frames }` block (`buffer_frames` default 4096). No `log` block
means no logger object at all and behavior bit-identical to a build without this record. Every registered
signal is logged; there is no selection filter. At init an unknown key, an existing file at `path` (never
overwritten) or an unwritable path is a `ConfigError` naming `log.<key>`.

**Row timing (Q3).** Row k describes one instant, t_k = k·dt: the frame-start state (rigid body and model-owned),
the frame-k environment sample, the derived signals (ADR-0026), the controls latched for frame k and the k1
wrench, which is a function of exactly those. The state after the last completed frame goes to `/final_state`.
The one disclosed exception is the derived `ax ay az`, which keep the ICD's previous-frame definition (ADR-0026).
The mechanism (for example an allocation-free frame-start snapshot) is PR 4's.

**Buffering and drain (Q4).** One preallocated ring per rate group, `buffer_frames` rows each. `step()` first
reserves the frame's row in every due ring, before any environment, exchange, model, integrator or RNG side
effect. At frame end it fills the row with t_k values, with no allocation:
- **Rigid-body and model-owned state:** from an allocation-free frame-start snapshot. The published
  `v<i>.state.*` are end-of-frame (see Context).
- **Every other signal:** through its registry pointer, which already holds the frame-k value at frame end.

The mechanism detail stays PR 4's, checked by R3. `step()` never calls HDF5. Rows are drained whole, outside `step()`, only at three points: `run()` between frames once a ring passes
its high-water mark, an explicit `Simulation::flush_log()`, and `close_log()`. `astra.lockstep.Session`, which
hides its `Simulation`, drains at its own step and run calls and exposes `flush_log()`/`close_log()`.

**Failure (Q4, Q7).** `HealthCheck::kLog` is appended after `kTransport`, so existing values keep their integers.
A ring with no free row halts `HaltReason{kLog, source = "log", frame}` at reservation, before any side effect,
so the clock, state and RNG state do not advance. A failed HDF5 write during a drain is the same halt. `kLog`
is unconditionally terminal: there is no `warn`/`off` action and no `sim.health.log` key. It is sticky with or
without a transport: every later `step()`, `run()` and `flush_log()` returns the same halt.

**Finalization.** `Simulation::close_log()` is explicit, off the per-frame path and bound to Python. It drains the
rings and writes `/summary` and `/final_state`, then sets the root attribute `complete = 1`. It throws a typed
error naming the failed write, and it is idempotent. The destructor closes best-effort and never sets
`complete = 1` after any failure. A file without `complete = 1` is incomplete; `astra.log.load` refuses it
unless called with `allow_incomplete=True`.

**File layout (Q6).** Each rate group has `/rate_<divisor>/frame` (uint64) and `/rate_<divisor>/values`
(float64, rows × signals, chunked, uncompressed), with `names` and `units` string attributes on `values`.
Provenance lives in root attributes. Four more objects:
- `/plugins`: a compound table.
- `/writes`: every external `Simulation::write_input` call, as frame, name and value.
- `/summary`: frames run, the halt's check, source and frame, every health counter, and the min and max of each
  logged signal.
- `/final_state`: the combined state buffer after the last call, with its frame and an `after_halt` flag.

`after_halt` is needed because a post-step state or domain halt can leave the integrated state in place, while a
wrench fault restores it (`Vehicle::integrate`).

**Provenance (Q9, Q10).**
- The run's config text, stored **without its `log` block**. The path and buffer size are output settings, not
  run inputs.
- `git describe --always --dirty`, from a build-time generated translation unit that is rewritten only when the
  string changes. A build outside git records `unknown`. Disclosed limit: this describes the tree at the last
  build, not the files at run time.
- The core version and `kCoreApiVersion`, and `dt`.
- A `/plugins` row per loaded plugin: path, category, name, `core_api_version`, `built_with`, and the SHA-256 of
  the loaded `.so`, computed at init (`static` under `ASTRA_STATIC_PLUGINS`). `PluginDescriptor` does not
  change.

Seeds are recorded as part of the config text.

**Determinism (Q7).** The criterion is byte-identical files: two runs of one config with the same external writes,
on the same build and machine, written to different paths. This relies on four settings: HDF5 object time
tracking off, fixed library-version bounds, the config stored without `log`, and no nondeterministic value in
the file. **Conditional fallback, recorded now so this record needs no later edit:** if PR 2 (the RAII wrapper)
shows that libhdf5 1.10 cannot produce identical bytes for identical writes, the criterion becomes equality of
every dataset's shape, type and contents and of every attribute's value. PR 2's evidence and every test that
uses the fallback must say so. Neither criterion permits a tolerance.

**No wall time (Q8).** Core writes no wall-clock value anywhere in the file (ADR-0010, ADR-0023). The Python tools
print wall time and real-time factor to stdout. §15's envelope min/max waits for the envelope monitors; until
then the summary carries min/max of every logged signal.

**Replay (Q11).** Replay is re-execution. `astra.log.replay(path, out)` does the following:
1. Rebuilds the `Simulation` from the stored config plus a `log` block naming `out`.
2. Applies `/writes` at their frames and runs the completed frame count.
3. For a halted log, makes one more `step()`, which must reproduce the recorded halt.
4. Reports whether `out` meets the determinism criterion against `path`.

Two kinds of log are refused, each with an error naming the reason:
- **A run with a `transport` block.** Replaying commands without the peer is a different feature.
- **A log whose recorded halt is `kLog`** (Joe, 2026-09-27, Codex review finding F1). That halt is an
  output-side event, not part of the run: it depends on `buffer_frames` and on when the caller drained, which
  the stored config deliberately omits, or on an I/O failure. A replay could not reproduce it.

Every other halt (state, wrench, input, environment, domain) replays and must reproduce. Sending a log to
FlightGear is a bridge source (ADR-0026), not replay.

**The M4 recorder (Q12).** `ExchangeRecorder` stays through M5, because M4's byte-identity acceptance tests read
it. It is retired by a follow-up once a lockstep test compares HDF5 logs instead. The run log already carries
`v0.controls.*` and `transport.*` for lockstep runs.

## Rejected alternatives

- **Sampling the pointers at §5's end-of-frame position** (as the recorder does): it pairs t_{k+1} state with
  t_k wrench and air data, so logged α disagrees with logged state by one frame.
- **Preallocating the whole run and writing once at close:** about 40 MB per 100,000 frames at 50 signals, ×64
  for the Monte Carlo, and no path to a RealTime writer thread.
- **Overwriting an existing file:** loses a previous result silently.
- **Compound per-signal columns:** the schema would depend on the signal count and pandas loading gets harder.
- **Checking overflow at frame end:** physics and RNG would already have advanced, so the halted state is not
  the pre-frame state (critique C5).
- **Wall time in the file:** breaks ADR-0010 and byte identity.
- **A plugin version field:** a descriptor ABI change for information the `.so` hash already pins.
- **Replay as only "same config, same seed"** (§9 before this record): misses mid-run writes and halts.
- **`warn`/`off` for `kLog`:** a silently truncated log is not a log (ADR-0005).
- **Recording `buffer_frames` and every drain call so a `kLog` halt replays:** more format and work for a
  halt that describes the output, not the run; a failed drain write still could not be reproduced.
- **Retiring the recorder in M5:** removes the oracle M4's acceptance tests read before a replacement exists.

## Consequences and validation

Additive behind an absent `log` block. M3's frozen regression and the M4 recorder bytes stay untouched (plan R1).
Costs:
- one ring copy per frame per rate group;
- a new halt kind bound to Python;
- two new `Simulation` calls;
- a build-time generated source;
- a SHA-256 per plugin at init.

Whether any of this changes `kCoreApiVersion` is PR 4's decision under ADR-0009's version check. Validation is
the plan's:
- R2 (columns, units, frames), R3 (row coherence), R4 (byte identity), R5 (no allocation), R6/R6b (loud,
  state-preserving failures), R7 (provenance and summary), R8 (`load`) and R9 (replay, including a halted run,
  a refused lockstep log and a refused `kLog`-halted log);
- run in PR 2 (wrapper, byte identity), PR 4 (logger) and PR 5 (`astra.log`).

This record's own check: `python3 scripts/docs_check.py`, `git diff --check` and one Codex review. Not covered:
cross-machine identity, readers other than h5py, and a RealTime writer thread.

## Approval

Pending Joe's explicit decision.
