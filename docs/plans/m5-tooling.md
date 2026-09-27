# M5 Tooling: spec, plan and PR decomposition

Status: draft, pending Joe's decisions on the open questions (Q1 to Q24) and approval of the spec/plan
Owner: Joe
Risk: 3 overall. PR 3 and PR 4 change `core/kernel/` (per-frame order, health behavior); the rest are Tier 2
or lower. Each PR's own tier is in the decomposition table.
Intent/approval: Joe's M5 planning request (2026-09-26): planning only, spec/plan/PR decomposition with
acceptance commands and binding ADRs, unpinned decisions listed as questions with a recommendation each,
HDF5 is the only dependency already approved, one Codex plan critique before Joe sees it. **No spec, plan or
question is approved yet.**
Base: `origin/main` = `d3e82e3`.
Architecture/ADRs: [architecture](../architecture.md) §2 (principles 1 to 3), §5 (per-frame order), §9
(signals, logger, replay), §12 (`astra.log`, `astra.montecarlo`, `astra.scenario`, `astra.fgbridge`), §13
(config), §14, §15 (log overflow, post-run summary), §16 (M5 row: the acceptance);
[ADR-0002](../decisions/0002-si-units-and-signal-metadata.md),
[ADR-0005](../decisions/0005-hdf5-c-api-logging.md),
[ADR-0009](../decisions/0009-plugin-architecture.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md),
[ADR-0011](../decisions/0011-one-vehicle-as-list-of-one.md),
[ADR-0016](../decisions/0016-per-rk4-substage-wrench-evaluation.md),
[ADR-0017](../decisions/0017-control-input-write-seam.md),
[ADR-0021](../decisions/0021-dynamics-model-owned-state.md),
[ADR-0022](../decisions/0022-lockstep-exchange-stage.md),
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md). Proposed: ADR-0025 (run log), ADR-0026
(derived flight-condition signals), both drafted in PR 1 only if Joe agrees (Q1).
System context: the logger is the last stage of architecture §5's per-frame order (environment → transport
exchange → vehicle step → latch → integrator → post-step → **logger sample**). It reads every
`SignalRegistry` entry by pointer; it produces one HDF5 file per `Simulation`. Its consumers are all Python:
`astra.log` (load, replay), `astra.scenario` (checks read the log), `astra.montecarlo` (one log per run plus a
summary), and `astra.fgbridge` (packets from a live run or a log). The derived signals (PR 3) are produced once
per frame from truth by the M4 `icd_pack` code and consumed by the logger, the scenario checks, and the bridge.

## Outcome and boundaries

**Outcome.** Architecture §16's M5 row, under `just check`: the scenario harness runs every YAML in
`tests/scenarios/`; a Monte Carlo of 64 dispersed runs completes; FlightGear receives valid packets, checked by
a packet decoder test, not by launching FlightGear. Behind those: an HDF5 run log (ADR-0005) and replay (§9),
which the harness, the Monte Carlo and the bridge all build on.

**Non-goals.** `RealTime` mode and its writer thread (§9's non-RT drain thread); retiring the M4 exchange
recorder (Q12); a CLI (`astra run --strict`, §15/§18; Q24); §15 envelope monitors (an open item); closed-loop
(autopilot) scenarios or Monte Carlo (Q16); replaying a lockstep run (Q11); sensor or actuator models; wind
events in scenarios (no write seam exists for wind; Q14); launching FlightGear; any `icd/` layout change; any
change to `tests/golden/**`, tolerance files or accepted ADRs other than what Joe explicitly authorizes (the
FGNetFDM table, Q21).

**Invariants.** A config without a `log` block behaves bit-identically to today (M3's frozen regression and the
M4 recorder byte comparison stay untouched). Per-frame path (`step()`): no allocation, no exceptions, no
blocking I/O, no `std::string`; HDF5 calls happen only in the drain, never in `step()` (ADR-0005). Time is the
frame counter; `t = frame * dt` is derived, never accumulated or logged as an independent value (ADR-0010).
No wall-clock read in `core/`, `plugins/` or `aircraft/` (ADR-0010, ADR-0023). SI inside; unit conversion only
at boundaries (the FlightGear packet is one, ADR-0002). Randomness is seeded and logged. Nothing assumes one
vehicle (ADR-0011).

**Assumptions.** The M3 plugins, trim and the M4 `icd_pack` truth definitions are correct as shipped and are not
re-validated here (M4 PR 2's R12a oracles cover `icd_pack`). Same-build, same-machine determinism only
(`docs/lessons.md` 2026-09-21). `libhdf5-dev` 1.10.10 is installed (verified, `dpkg -l`). What this does not
validate: FlightGear's own rendering of the packets, cross-machine log identity, HDF5 file readability by
tools other than h5py.

## Repository findings (observed 2026-09-26 at `d3e82e3`)

Observed facts, with the path that shows each:

- **No HDF5 code or CMake use exists.** No `find_package(HDF5)` anywhere; `libhdf5-dev` is in
  `scripts/setup-ubuntu.sh`'s apt list and installed locally. PR 2 adds the first link, from `astra_core`.
- **Signals.** `SignalRegistry` (`core/include/astra/signals/signal_registry.hpp`) holds `{name, const double*,
  unit, divisor}`; `freeze()` closes it. Every current registration uses divisor 1, so today there is one rate
  group. Registered: health counters (`health.*`, `health_monitor.cpp:71`), wind (`wind.*`), gravity, mass,
  transport counters (`transport.*`), per-vehicle wrench total (`v0.wrench_total.*`, `vehicle.cpp:104`), the
  13 rigid-body states (`v0.state.*`, `vehicle.cpp:125`, pointing into the stage buffer), model-owned states
  (`v0.<model>.state.<i>`), and every write-seam input (`v0.controls.*`, `input_registry.cpp:38`).
- **No derived quantities are published.** Euler angles, α, β, TAS/CAS/Mach, altitude, lat/lon exist only
  inside `astra::icd_pack::pack_sim_to_sw` (`core/include/astra/util/icd_pack.hpp:73`), called by the exchange
  stage at frame start after the environment sample. It sets `ASTRA_ICD_STATUS_LATLON_NOT_MODELED` for a
  non-geodetic Earth. A scenario check on "altitude" or a FlightGear packet needs these (Q13).
- **Published state is end-of-frame.** `v0.state.*` point into `Vehicle`'s stage buffer, synced to the newly
  accepted state after post-step; `v0.wrench_total.*` is the k1-stage wrench (frame-start state);
  `v0.controls.*` hold the value latched for the frame. Sampled at §5's logger position, one row mixes
  t_{k+1} state with t_k wrench and air data (Q3).
- **The M4 recorder precedent.** `ExchangeRecorder` (`core/include/astra/kernel/exchange_recorder.hpp`)
  preallocates, halts on capacity (`TransportFault::kRecorderFull`), and records the state at `open()` (frame
  0) and after each step at `new_frame` (`lockstep_exchange.cpp:164,199`). ADR-0022 says M5's HDF5 logger
  supersedes it.
- **`Simulation`** (`core/include/astra/kernel/simulation.hpp`): `step()` and `run()` are `noexcept` and return
  a `HaltReason`; transport halts are sticky. `HealthCheck` ends at `kTransport` (appended last, bound to
  Python). The allocation test (`tests/physics/no_alloc_test.cpp`) measures across `sim.run()`, so a drain
  inside `run()` would count (Q4).
- **Config.** `Config` has no serializer; `Config.from_dict` (Python) builds YAML text with a yaml-cpp emitter
  and round-trips exactly (M3 PR 5, R2a). A second `from_config` on one `Config` object is safe only because
  `apply_initial_controls` no longer uses `unknown_keys()` (`docs/lessons.md` 2026-09-19), which the Monte Carlo
  depends on (it builds a fresh `Config` per run anyway).
- **Plugin provenance.** `PluginDescriptor` has `category, name, core_api_version, built_with` and **no version
  field**; §13 and ADR-0005 ask for "each plugin's version and build string" in the log (Q9).
- **Python.** `python/astra/` has `trim.py`, `linearize.py`, `lockstep.py`. `pyproject.toml`'s dev group pins
  numpy, scipy, pyyaml; its comment says "h5py, pandas, pyarrow join when a milestone uses them".
  `scripts/setup-ubuntu.sh` installs them unpinned (h5py 3.16.0, pandas 3.0.5, pyarrow 25.0.1 in the main
  checkout's venv today). `testpaths = ["tests/python"]`. `tests/scenarios/` does not exist.
- **FlightGear.** No `net_fdm.hxx` in `docs/refs/`; FlightGear is not installed. Architecture §12 says
  `FGNetFDM` version 24, big-endian, 408 bytes, UDP 5500 at 60 Hz. 60 Hz is not an integer divisor of 1 kHz
  (Q23).
- **Wall clock.** ADR-0010's prohibition covers `core/`, `plugins/`, `aircraft/`; §15's post-run summary asks
  for "wall time, real-time factor" in the HDF5 file, which core cannot measure without breaking it (Q8).
  `astra.lockstep` (Python) already uses `time` for reap timeouts.
- **Gate cost.** PR 8 recorded test wall time per preset at 76/1/163/346 s (debug/release/static/asan)
  (`docs/reviews/pr8-cruise-regression-human.md`). The Python tests run the Release build (`pyproject.toml`).
  No per-frame Release timing of the notional transport is recorded; the main checkout's installed module is
  stale, so it was not measured here. PR 7 measures before fixing the Monte Carlo duration (Q19).

## Contract and failure behavior

Recommended contracts; the parts marked with a question number are unpinned until Joe decides.

**Run log (PR 4, ADR-0025).** Enabled by a `log` block (Q5); absent means no logger object at all. At init:
open the file (refuse an existing path, an unwritable path, or an unknown key, each a `ConfigError` naming
`log.<key>`), build one ring buffer per rate group sized `log.buffer_frames` rows (Q4), write the provenance
attributes (Q6, Q9, Q10). Per frame, inside `step()`: copy each rate group's due signals into the ring (no
allocation). Outside `step()`: drain whole rows to HDF5 (`run()` between frames when a ring passes its
high-water mark, an explicit `flush_log()`, and close). Row k is coherent at t_k (Q3). Failures: a ring that
fills before anyone drains it halts with a new, appended `HealthCheck::kLog` naming `log` and does not advance
the clock (§15: a truncated log is not a log); a failed HDF5 write in the drain is the same halt; neither has a
`warn`/`off` action (Q7). On close: drain, write `/summary` (frames run, halt reason, every health counter,
per-signal min/max; no wall time, Q8) and `/final_state`, set `complete = 1`. A file without `complete = 1`
is incomplete; `astra.log.load` refuses it unless `allow_incomplete=True`.

**Replay (PR 4 core record, PR 5 Python).** The log stores the config text, the frames run, and every
external input write (`Simulation::write_input`: frame, name, value). `astra.log.replay(path, out)` rebuilds the
`Simulation` from the stored config, applies the stored writes at their frames, runs the stored frame count,
and returns whether the new log's data is byte-identical (Q11). A log from a run with a `transport` block is
refused with an error naming the reason.

**Derived signals (PR 3, ADR-0026).** Per vehicle with physics, whether or not a transport is configured,
`v<i>.derived.<field>` for each `astra_sim_to_sw_v1` truth field (`phi theta psi p q r u v w alpha beta tas
cas mach altitude vertical_speed lat lon ax ay az status_flags`) plus NED velocity `v_n_0..2`, computed once
per frame by the existing `icd_pack::pack_sim_to_sw` at the frame-start point, after the environment sample.
The exchange stage sends the same struct (one computation). Units from the ICD header comments (SI);
`status_flags` as a double holding the integer bitmask. Invalid fields keep the ICD's own convention (the
status bit, value as packed).

**Scenario harness (PR 6).** A YAML file under `tests/scenarios/` is one pytest item (a `conftest.py`
`pytest_collect_file` hook; `testpaths` gains `tests/scenarios`). Schema (Q14):
`config` (path) plus optional `overrides` (dotted path to value, existing numeric leaves only), optional
`trim` (the `astra.trim` straight-and-level call and its setpoints), `duration` (s, an exact multiple of `dt`),
`events` (`at` in s, an exact multiple of `dt`; `write` input name and value), `checks` (signal name; one of
`within: {target, tol}`, `between: {lo, hi}`, `angle_within` (wrap-aware); window `from`/`to` in s or
`final`), and `expect_halt` (check name and source) for scenarios that must halt. Strict: an unexpected halt
fails; any nonzero warn counter fails. Failure output names the scenario, check, signal, window, worst value
and its frame. An unknown key, an unknown signal, or a time that is not a multiple of `dt` is a collection
error, not a skip. The harness reads the run's own HDF5 log, so every scenario also exercises the logger.

**Monte Carlo (PR 7).** `astra.montecarlo.run(spec, out_dir, workers=None)`. Spec (Q17): base config path,
optional trim, duration, `runs`, master `seed`, `dispersions` (each an existing numeric config leaf: `uniform
{lo, hi}`, `normal {sigma, mode: absolute|relative, clip}`, or `seed` for an integer plugin seed). Per-run seeds
are derived from the master seed and the run index only, so results do not depend on worker count or order.
One HDF5 log per run (`run_0000.h5` ...) plus `summary.parquet` (one row per run: run, derived seed, every
dispersed value, halt check/source/frame, frames run, final state, per-signal min/max for a named signal list).
A run that halts is a row with its halt, not a crash of the batch. `multiprocessing` with the `spawn` start
method (Q20).

**FlightGear bridge (PR 8).** `astra.fgbridge` packs `FGNetFDM` version 24 (408 bytes, network byte order) from
the derived signals of a live `Simulation` or of a log, converts units at this boundary (the field list and
units come from the transcribed oracle table, Q21), and sends to UDP (default `127.0.0.1:5500`) every
`divisor` frames (Q23). Fields with no source are zero (Q22). A config whose derived `status_flags` carry
`LATLON_NOT_MODELED` (flat Earth) is refused. Pacing to wall time is optional and lives in Python only (Q23).

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | No `log` block: behavior bit-identical to `d3e82e3` | Existing M3 frozen regression (`notional_transport_cruise_regression`) and M4 recorder byte-equality tests pass unchanged; no golden touched |
| R2 | With `log`: every registered signal is a column with its unit attribute, one row per frame, `frame` exact | gtest over a 1000-frame config: column set equals `registry.signals()`, units equal, `frame` = 0..999 |
| R3 | Row coherence at t_k (Q3) | pytest: at every row, α and β recomputed in numpy from the logged `v_b`, `q_nb` and `wind.*` equal `v0.derived.alpha/beta` within 1e-12 rad (reassociation only; the same formula on the same doubles); logged state rows equal a step-by-step readout of `Simulation` state bitwise |
| R4 | Deterministic log | Two runs of the Dryden cruise config produce byte-identical files (SHA-256), same build and machine (Q7) |
| R5 | No allocation with logging on | `no_alloc` variant: 1000 `step()` calls with a ring larger than 1000 rows allocate zero |
| R6 | Log failures are loud | Ring full without a drain halts `kLog` naming `log`, clock unchanged; an existing or unwritable path, or an unknown `log` key, is a `ConfigError` naming the key; an injected write failure halts `kLog`. Each is seen to fire (§15) |
| R7 | Provenance and summary | Attributes hold config text, git describe, core version and API, `dt`, plugin table with SHA-256 (Q9); `/summary` holds frames, halt, health counters, min/max; `/writes` holds external writes; `complete = 1` |
| R8 | `astra.log.load` | pandas frame per rate group indexed by `frame`, `t` column exactly `frame * dt`, units exposed; runs in the same process as `astra._core` (two HDF5 runtimes, see risks) |
| R9 | Replay | `astra.log.replay` reproduces byte-identical data for a Dryden cruise and for a run with mid-run `write_input` events; a lockstep log is refused with a named reason |
| R10 | Derived signals are the ICD truth | With the loopback transport, `v0.derived.*` at frame k equal the recorder's `sim_to_sw` fields for frame k bitwise; the M4 request bytes are unchanged (R1) |
| R11 | Harness collects every scenario | `pytest tests/scenarios` collects exactly the `*.yaml` files present (count asserted by a meta-test) |
| R12 | Harness fails when it should | Negative fixtures in `tests/python/`: a violated check fails naming the signal and frame; an unexpected halt fails; a nonzero warn counter fails; a missed `expect_halt` fails; a non-multiple-of-`dt` time, an unknown key or signal is a collection error |
| R13 | Seed scenarios pass | The Joe-approved scenario list (Q15) passes under `just pytest` |
| R14 | 64-run Monte Carlo completes | 64 logs, each `complete = 1`, and a 64-row summary with zero halts, for the Q19 case |
| R15 | Monte Carlo is deterministic | Same master seed, `workers=1` vs `workers=N`: identical summary and per-run log bytes; a different master seed changes every dispersed value |
| R16 | Dispersion sampler | Fixed seed gives recorded values bitwise; uniform and normal sample moments over 10^5 draws within 5 standard errors of theory |
| R17 | A Monte Carlo run replays | One run's log passes `astra.log.replay` |
| R18 | FlightGear packets are valid | A real localhost UDP socket receives 408-byte packets; the decoder, built from the transcribed table and not from the packer's format, decodes version 24 and fields equal to the sim's derived signals in FlightGear units within float32 rounding |
| R19 | Packet rate and sources | Live and log sources send one packet for each frame with `frame % divisor == 0`, carrying that frame's values |
| R20 | Flat Earth refused | A flat-Earth config raises naming `LATLON_NOT_MODELED` |
| R21 | Gate | `./scripts/check-full.sh` exit 0 with `check: OK` per code PR; review per tier |

Human-owned files stay unchanged except the FGNetFDM table, which needs Joe's authorization (Q21).

## Open questions

Each is unpinned by architecture and the accepted ADRs. "ADR" means the recommendation should be recorded in
ADR-0025 or ADR-0026 (PR 1); "plan" means Joe's decision recorded here is enough. Blocks: the first PR that
cannot start without it.

**Records and dependencies**

- **Q1. Two new ADRs? (blocks PR 1).** Recommend yes: ADR-0025 "HDF5 run log and replay" (Q3 to Q12) and
  ADR-0026 "Derived flight-condition signals" (Q13), with architecture rev 9 (§5, §9, §12, §15, §16 notes).
  They change the per-frame path, add a halt, and fix the log format every tool reads, which is expensive to
  unwind. Alternative: record them here only (cheaper, but the next milestone would re-decide the log format).
  PR 1 writes under `docs/decisions/`, so Joe arms the guard for that session.
- **Q2. New Python dependencies (blocks PR 5 and PR 7).** Recommend approving `h5py`, `pandas` (PR 5) and
  `pyarrow` (PR 7), pinned in the dev group at the then-current releases (3.16.0, 3.0.5, 25.0.1 today).
  ADR-0005 and architecture §9/§12 already name pandas frames and a summary parquet written with Arrow from
  pip; nothing else is needed (the harness is a `conftest.py` hook, fan-out is `multiprocessing`, the bridge
  is `struct` and `socket`). Alternative without pyarrow: summary as an HDF5 table, which contradicts
  ADR-0005's consequences and would need a superseding record.

**Run log (ADR-0025)**

- **Q3. Row timing (blocks PR 4).** Recommend: row k describes one instant, t_k = k·dt: the frame-start state
  (rigid body and model-owned), the frame-k environment sample, the derived signals, the controls latched for
  frame k, and the k1 wrench (a function of exactly those). The state after the last frame, y(t_N), goes to
  `/final_state`. Mechanism is PR 4's (for example a frame-start snapshot of the combined state taken where
  the environment is sampled, allocation-free); R3 is the check. Alternative: sample the pointers at §5's
  end-of-frame position as the M4 recorder does (row label `new_frame`), which pairs t_{k+1} state with t_k
  air data and wrench, so logged α disagrees with logged state by one frame.
- **Q4. Buffering and drain point (blocks PR 4).** Recommend a fixed ring per rate group (`log.buffer_frames`,
  default 4096 rows) drained outside `step()`: by `run()` between frames at a high-water mark, by an explicit
  `Simulation::flush_log()` (bound to Python, for `step()`-driven callers such as `astra.lockstep.Session`),
  and at close. `step()` never touches HDF5. Alternative: preallocate the whole run (`sim.frames + 1` rows) and
  write once at close, as the recorder does: simplest, but about 40 MB per 100,000 frames at 50 signals, and
  64 of those in parallel for the Monte Carlo; it also cannot serve a later RealTime writer thread.
- **Q5. Config block (blocks PR 4).** Recommend a top-level `log: { path, buffer_frames }`, every registered
  signal logged (no selection filter until a run needs one), an existing file refused rather than overwritten.
  Python tools set `path` per run in the config dict.
- **Q6. File layout (blocks PR 4).** Recommend per rate group `/rate_<divisor>/frame` (uint64) and
  `/rate_<divisor>/values` (float64, rows × columns, chunked, uncompressed) with `names` and `units` string
  attributes; root attributes for provenance; `/plugins` (a compound table), `/writes`, `/summary`,
  `/final_state`. Compound per-signal columns were considered; a 2-D array keeps pandas loading trivial and the
  schema independent of the signal count.
- **Q7. Determinism criterion and halt policy (blocks PR 2's wrapper settings and PR 4).** Recommend
  byte-identical files as the test (principle 2), made possible by disabling HDF5 object time tracking, fixing
  the library-version bounds, and keeping every nondeterministic value out of the file (Q8). If PR 2 shows HDF5
  cannot give identical bytes, fall back to dataset-and-attribute equality and say so. `kLog` halts
  unconditionally, with no `sim.health.log` key, as `kTransport` does.
- **Q8. Wall time in the summary (blocks PR 4).** §15 lists "wall time, real-time factor" in the file's
  summary; measuring them in core breaks ADR-0010. Recommend: core writes no wall-clock value; the Python
  tools print wall time and real-time factor to stdout (and the Monte Carlo may put them in the parquet, which
  is not a log). Envelope min/max (§15) waits for the envelope monitors; the summary carries min/max of every
  logged signal instead.
- **Q9. "Plugin version" (blocks PR 4).** The descriptor has no version field, and adding one is a plugin ABI
  change. Recommend recording path, category, name, `core_api_version`, `built_with` and the SHA-256 of the
  loaded `.so` (computed at init), or `static` under `ASTRA_STATIC_PLUGINS`. No descriptor change.
- **Q10. Git describe (blocks PR 4).** Recommend a build-time generated source (a custom command that runs
  every build, rewriting one tiny translation unit only when the string changes) holding
  `git describe --always --dirty`; a build outside git records `unknown`. Disclosed limit: it describes the
  tree at the last build, not the files at run time.
- **Q11. Replay scope (blocks PR 4's write record, PR 5).** Recommend: replay means re-execution from the stored
  config, stored external writes and frame count, checked by byte identity; lockstep logs are refused in M5
  (they need the peer; replaying commands without it is a different feature). Playback of a log to FlightGear
  is a bridge source, not replay.
- **Q12. The M4 exchange recorder (blocks nothing; affects PR 4 scope).** ADR-0022 says the HDF5 logger
  supersedes it. Recommend keeping it through M5 (M4's byte-identity acceptance tests read it) and retiring it
  in a follow-up once a lockstep test compares HDF5 logs instead; M5's log already carries `v0.controls.*` and
  `transport.*` for lockstep runs.

**Derived signals (ADR-0026)**

- **Q13. Derived signal set and computation point (blocks PR 3).** Recommend the ICD truth fields plus `v_n`,
  computed by `icd_pack::pack_sim_to_sw` once per frame per vehicle at the frame-start point, shared with the
  exchange stage, published as `v<i>.derived.*`. This reuses the only derivation M4 validated rather than
  writing a second one in Python (where the flat versus geodetic `p_n` meaning would be re-implemented).
  Consequences: every physics config gains about 25 signals and one `pack_sim_to_sw` per frame; the exchange's
  request bytes must not change (R10). Alternative: compute in Python from logged state (duplicates frames and
  Earth-model math outside the core, rejected).

**Scenario harness**

- **Q14. Scenario schema (blocks PR 6).** Recommend the schema in the contract above. Events are
  `write_input` only; the architecture's own example event (`wind_n = [5,0,0]`) has no write seam, so wind
  changes need a config (for example `wind/uniform`) in M5 and a wind event waits for a seam. Values in SI
  (ADR-0002), no unit suffixes or conversions in scenario files.
- **Q15. Seed scenarios and their bands (blocks PR 6's R13).** Recommend five, on a still-air variant of the
  notional transport cruise (a new config under `aircraft/notional_transport/configs/`), each band argued in the
  file from accepted evidence or a stated physical reason, and approved by Joe in PR 6's packet: (1) trimmed
  cruise hold, 60 s, altitude and pitch within bands; (2) elevator step, pitch-rate sign per ADR-0001 and the
  aero plugin's `cm_elevator_per_rad < 0`; (3) aileron step, roll-rate sign; (4) throttle step, TAS rises;
  (5) an expected halt (for example an initial condition that leaves the atmosphere's valid range,
  halting `kOutOfDomain`). Whether `tests/scenarios/**` joins the oracle guard is Joe's call; recommend not (AGENTS.md
  already forbids weakening a test).
- **Q16. Closed-loop scenarios (blocks nothing).** Recommend not in M5: a scenario with the autopilot peer adds
  subprocess management to the harness; M4's own acceptance already covers closed loop.

**Monte Carlo**

- **Q17. Dispersion schema and randomness (blocks PR 7).** Recommend the spec in the contract above and a
  per-run seed of the first 8 bytes of SHA-256(master seed, run index), feeding Python's `random.Random`
  (MT19937, a stable stream) with in-house uniform and Box-Muller transforms, mirroring the C++ rule. Not
  numpy's `Generator` distributions: numpy is unpinned (`numpy>=2`) and does not promise identical streams
  across versions. Every drawn value lands in the run's logged config text and in the summary.
- **Q18. What a dispersion may touch (blocks PR 7).** Recommend existing numeric leaves only (a typo'd path is
  an error, never a new key), plus `seed` for plugin seeds; dispersing a quantity changes the config the run is
  built from, so each run re-trims when `trim` is set.
- **Q19. The 64-run acceptance case and where it runs (blocks PR 7's R14).** Recommend
  `cruise_light_turbulence.yaml`, re-trimmed per run, 60 s open loop, dispersing mass (normal, 2 % relative),
  the aero reference point x offset (a CG shift; uniform ±0.1 m) and the Dryden seed; "completes" means 64
  logs with `complete = 1`, 64 rows, zero halts. Run it in `just pytest` if PR 7 measures the batch under about
  2 minutes; otherwise a small Monte Carlo stays in pytest and the 64-run case becomes a `just` recipe whose
  trimmed output is committed as evidence. Joe's call on the dispersions and the placement.
- **Q20. Fan-out (blocks PR 7).** Recommend `multiprocessing` with `spawn` (a forked child would inherit
  `dlopen`ed plugins and HDF5 library state), default workers `os.cpu_count()`, each worker building its own
  `Config` and `Simulation`.

**FlightGear**

- **Q21. The packet oracle (blocks PR 8's R18).** Recommend Joe places FlightGear's `src/Network/net_fdm.hxx`
  (the revision defining `FG_NET_FDM_VERSION 24`) in `docs/refs/`; PR 8 transcribes its field order, types and
  unit comments into `tests/golden/flightgear/fgnetfdm_v24.py` with the file's revision and line numbers (Joe
  arms the guard); the decoder test reads that table, and the packer is written separately. An optional,
  by-hand script compiles the reference header with `offsetof` to cross-check the table, never run by
  `just check` (the ADR-0013 generator pattern). Alternatives: install FlightGear (a large new dependency,
  rejected); derive the layout from memory (no provenance, rejected per `docs/lessons.md` 2026-09-25).
- **Q22. Field mapping (blocks PR 8).** Recommend populating position (lon, lat, altitude; `agl` equal to
  altitude, since there is no terrain), attitude, α, β, calibrated airspeed, climb rate, NED and body
  velocities and body accelerations, each converted to the unit the oracle table states; engines, gear, fuel,
  surfaces (no declared deflection range, an open item) and Euler rates are zero. A flat-Earth config is
  refused (Q13's status bit).
- **Q23. Rate and pacing (blocks PR 8).** 60 Hz is not an integer divisor of 1 kHz. Recommend a bridge divisor
  of 16 (62.5 Hz; FlightGear's side is then `--native-fdm=socket,in,62.5,,5500,udp` or its nearest accepted
  rate), configurable to any integer divisor, and architecture §12's "60 Hz" corrected in PR 1. Wall-clock
  pacing of the bridge is Python-only, outside ADR-0010's `core/`/`plugins/`/`aircraft/` scope, never feeds
  sim time, and is off in tests. AGENTS.md's wording is broader than ADR-0010's; Joe to confirm this reading.
- **Q24. CLI (blocks nothing).** Recommend not in M5; §16's M5 row does not require `astra run`. Add an open
  item in PR 9 for the CLI and `--strict`.

## PR decomposition

One Claude builder, sequential; PR 8 depends only on PR 3 and PR 5 and may run beside PR 6/PR 7 in its own
worktree if Joe assigns it. Each PR opens as a draft, gets the review its tier requires, and waits for Joe's
merge. Every code PR runs `./scripts/check-full.sh` (exit 0 and `check: OK`) and commits a trimmed
`docs/reviews/evidence/<head>-gate-summary.log`.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance commands (beyond the full gate) |
|---|---|---|---|---|---|
| 1 | ADR-0025, ADR-0026, architecture rev 9 (§5, §9, §12 incl. the bridge rate, §15 summary, §16 note), PLAN.md M5 PR lines | 2 | 0005, 0010, 0011, 0022, 0023 | Q1, Q3 to Q13, Q23 decided | `python3 scripts/docs_check.py`; `git diff --check`; one Codex review; no full gate (docs only) |
| 2 | `find_package(HDF5 COMPONENTS C)`, RAII wrapper in `core/signals/` (file, group, dataset append, attribute; errors to typed exceptions; time tracking off, version bounds fixed); unit tests with temp files; ASan/LSan behavior with the uninstrumented libhdf5 settled | 2 | 0005 | PR 1 | `ctest --preset asan -R Hdf5 --no-tests=error` (PR 2's new suite); same under `static-plugins`; a byte-identity test of two identical writes (Q7) |
| 3 | Derived signals: frame-start `pack_sim_to_sw` per vehicle, shared with the exchange stage; `v<i>.derived.*` | 3 | 0001, 0002, 0010, 0022, 0026 | PR 1 | `ctest --preset asan -R 'Derived\|IcdPack\|LoopbackTransport\|SimulationTransport\|NoAlloc\|NotionalTransportCruiseRegression' --no-tests=error` (R1, R5, R10; `Derived` is PR 3's new suite) |
| 4 | Logger stage in `Simulation`: `log` config, rings, drain in `run()`/`flush_log()`/close, `kLog`, provenance, `/summary`, `/final_state`, `/writes`; bindings for `flush_log`, `kLog` | 3 | 0005, 0010, 0011, 0016, 0017, 0022, 0025 | PR 2, PR 3 | `ctest --preset asan -R 'RunLog\|Hdf5\|NoAlloc\|Health\|SimulationTransport' --no-tests=error` (R2, R4 to R7; `RunLog` is PR 4's new suite); `ctest --preset release -R NotionalTransportCruiseRegression --no-tests=error` (R1) |
| 5 | `astra.log`: `load` (pandas per rate group), `replay`; h5py and pandas pinned (Q2) | 2 | 0005, 0010, 0025 | PR 4, Q2 | `.venv/bin/pytest tests/python -k 'log or replay'` (R3, R8, R9) |
| 6 | `astra.scenario`, `tests/scenarios/conftest.py`, `testpaths`, still-air config, seed scenarios, negative fixtures | 2 | 0001, 0002, 0010, 0017 | PR 5, Q14, Q15 | `.venv/bin/pytest tests/scenarios tests/python -k scenario` (R11 to R13) |
| 7 | `astra.montecarlo`, sampler, summary parquet; pyarrow pinned (Q2); the 64-run case | 2 | 0005, 0010, 0025 | PR 5, Q17 to Q20 | `.venv/bin/pytest tests/python -k montecarlo` (R14 to R17); the Q19 recipe if the 64-run case is not in pytest |
| 8 | `astra.fgbridge` (live and log sources), transcribed FGNetFDM table (guard armed by Joe), decoder test over UDP | 2 | 0002, 0010, 0026 | PR 3, PR 5, Q21 to Q23 | `.venv/bin/pytest tests/python -k fgbridge` (R18 to R20) |
| 9 | M5 closeout: PLAN.md check-off with evidence links, architecture §12 status, open items (CLI, recorder retirement, wind events, closed-loop scenarios) | 0 | none | PR 1 to 8 | `python3 scripts/docs_check.py`; `git diff --check`; one Codex pass |

PR 3 and PR 4 are Tier 3: independent adversarial Codex review focused on per-frame allocation, row timing,
halt behavior and determinism, and Joe reads the kernel diff and tests. PR 1's ADRs need Joe's acceptance
before PR 3 or PR 4 starts.

## Validation and risks

- **Two HDF5 runtimes in one Python process.** `astra._core` links the system libhdf5 1.10; h5py's wheel bundles
  its own (1.14). Both load when a test writes through the core and reads with h5py (R8). PR 5 tests exactly
  that; if symbols clash, the fallback is h5py built from source against the system library
  (`pip install --no-binary h5py`), which is a dependency-shape change for Joe.
- **ASan with an uninstrumented libhdf5.** LeakSanitizer may report HDF5's own global allocations at exit; PR 2
  settles it (library close at exit, or a narrow, commented suppression) before any other PR depends on it.
- **Gate time.** The logger adds writes to tests that enable it; the Monte Carlo and scenarios add Release-build
  Python time. PR 7 measures before choosing Q19's placement; no timeout is raised without Joe.
- **Signal-count changes.** PR 3 adds signals to every physics config; any test asserting an exact signal list
  must be updated deliberately, never loosened.
- **Rollback.** Everything is additive behind the `log` block or new Python modules, except PR 3's shared
  frame-start pack, whose rollback is reverting that PR (R1/R10 guard it).
- **Scope held out.** RealTime writer thread, CLI, recorder retirement, wind events, closed-loop scenarios and
  Monte Carlo, lockstep replay: follow-ups, added to PLAN.md's open items by PR 9.

## Plan critique and decisions

Codex plan critique: pending (dispatched after this draft is pushed; its findings, accepted and rejected, are
recorded here and in the PR body).

Joe's decisions: none yet. Q1 to Q24 are open.

## Done when

R1 to R21 pass on their PRs; each PR has its tier's review with results in its PR body; PR 9 checks M5 off in
PLAN.md with evidence links. Implementation completion is separate from Joe's approval and merges.
