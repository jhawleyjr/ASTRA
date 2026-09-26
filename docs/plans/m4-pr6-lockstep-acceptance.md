# M4 PR 6: turbulence configs, `astra.lockstep`, and the M4 acceptance runs

Spec and plan in one brief. Parent: [M4 plan](m4-lockstep.md) PR 6 row, R1 to R4b, R12b, Q3, Q5, Q7, Q15, Q19,
"Proposed placement", A6. Handoff: the end of [the PR 5 packet](../reviews/m4-pr5-autopilot-human.md).
Binding: [ADR-0007](../decisions/0007-shared-memory-lockstep-futex.md),
[ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md), [ADR-0022](../decisions/0022-lockstep-exchange-stage.md),
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md),
[ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md).
Risk: **Tier 3** (transport lifecycle and cleanup; acceptance of the milestone).
Status: **revision 3, approved by Joe 2026-09-26** (revision 2, `bf8ca3b`, approved with decisions Q1 to Q4 and edits
E1 to E4, folded in below; no second critique before code).
Base: `origin/main` `f4de27d` (confirmed by `git fetch`; PR 5 #56 merged). Branch `worktree-m4-pr6-lockstep-acceptance`.
Correction rounds 0/2, failed integrations 0/2.

## Outcome and scope

The M4 acceptance, executed: the notional transport, started from the trimmed cruise, flown for 10,000 exchanges by
the real `astra_autopilot` over the shm transport, at both of Joe's gating turbulence levels, with the lockstep's
health, bit-identical reproduction, peer and sim death, and the ICD-versus-state cross-check each proved by a test.

In scope, exactly the parent's PR 6 row:
- `aircraft/notional_transport/configs/cruise_light_turbulence.yaml` and `cruise_moderate_turbulence.yaml`.
- `python/astra/lockstep.py` (the driver) and its tests.
- The acceptance tests R1 to R4b and R12b: full-length in pytest (Release), shortened in ctest on all four presets,
  one full-length asan run labelled `acceptance`.

Out of scope: PLAN.md check-off and the duplicate §15 bullet (PR 7); any change to the HAL, the shm layout, `icd/`,
or the transport's halt semantics; `cruise.yaml`; any golden, tolerance file or ADR. `embedded/autopilot` changes are
limited to a gain retune under the handoff rules, and **none is planned** (see the feasibility trial). A sideslip term
is a law change and goes to Joe first.

## Repository findings (observed at `f4de27d`)

- **The recorder is not reachable from Python.** `Simulation::exchange_recorder()` (`core/include/astra/kernel/
  simulation.hpp:167`) and `ExchangeRecorder::serialize()` exist, but `python/bindings/_core.cpp` binds neither, nor
  `transport_fault()`. The counters are reachable: `transport.exchanges`, `transport.missed`, `transport.fault` are
  registry signals (`lockstep_exchange.cpp:152-154`). R1, R3 and R12b need the recorder, so PR 6 must add one binding
  (decision D1).
- `run()`/`step()` bindings hold the GIL (`_core.cpp:446-455`). No PR 6 test needs them released (D8).
- The transport block is top-level `transport:` (`simulation.cpp:452`). Keys: `plugin`, `segment` (required by the shm
  plugin, format `astra_<1-10 digits>_<1-32 alnum>`, `shm.cpp:85-96`), `divisor` (default 10), `timeout_ms`,
  `handshake_timeout_ms`, `peer_timeout_ms` (must be >= `timeout_ms`), `commands`, `limits` (all four keys required,
  finite, lo <= hi; `lockstep_exchange.cpp:98-150`). Recorder capacity comes from `sim.frames`.
- After a sim failure nothing is written to the segment; the peer ends at its own `peer_timeout_ms` (exit 2). The peer
  writes no "closed" state on exit, so after the peer is gone the sim's next exchange waits its full `timeout_ms`
  (`astra_shm_layout.h` Shutdown paragraph).
- `cruise.yaml`: `euler_nb: [psi, theta, phi] = [0.0, 0.023586859573503434, 0.0]`, so **the initial psi is 0** and the
  heading setpoint is 0. `p_n = [0, 0, 10000]` (`wgs84_rotating`: lat, lon, altitude). Its elevator/throttle and
  `euler_nb[1]` are `astra.trim.trim_straight_and_level(cruise.yaml, 230.0, 10000.0)` at full precision (its header).
  `astra.trim` drops `physical.wind` itself (`_load_still_air_config`), so trimming either new config is still-air
  and equals the cruise trim (lesson 2026-09-22).
- `just python` builds every target into `build/python` (Release, `ASTRA_BUILD_TESTS=ON`). Verified in this worktree:
  `build/python/embedded/autopilot/astra_autopilot` and `build/python/tests/lockstep/astra_echo_peer` exist, and
  `_core.default_plugin_dir()` is `build/python/plugins`.
- Autopilot CLI (`embedded/autopilot/cli.cpp`): all of `--segment --handshake-timeout-ms --peer-timeout-ms
  --altitude-m --airspeed-mps --heading-rad --elevator-trim --throttle-trim --theta-trim` required; optional
  `--notify-fd FD` with exactly one of `--stop-after N` / `--abort-after N`. Exit summary line
  `autopilot: handled <n> held <m> status <s>` (`status` is the HAL enum; CLOSED = 1 at exit code 0).
  Echo peer: `--mode hold --commands e,a,r,t`, summary `echo_peer: handled <n> status <s>`.
- `tests/lockstep/shm_test_support.hpp` already has `unique_segment()`, `Child` (spawn with notify pipe), the
  dead-creator sweep and a no-leftover check; `astra_lockstep_test()` in `tests/lockstep/CMakeLists.txt` builds a
  gtest executable with them. `static-plugins` inherits `asan`'s sanitizers (`CMakePresets.json`), so "asan only" is
  `ASTRA_SANITIZERS` set and `ASTRA_STATIC_PLUGINS` off.
- Dev group has no matplotlib, no pytest-timeout, no xdist (`pyproject.toml`); nothing new is added (D9, D10).
- ICD pack (`core/include/astra/util/icd_pack.hpp:82-102`): `phi/theta/psi` from `frames::euler_from_quaternion`
  (DCM, `atan2(c12, c11)`, `-asin(clamp(c13))`, `atan2(c23, c33)`); `u/v/w`, `p/q/r` copied from state;
  `altitude = earth.altitude(vs)`; `lat/lon = p_n(0), p_n(1)` for round Earth.

### Feasibility trial (scratchpad, not evidence)

A throwaway script in the session scratchpad (not in the repository) flew the cruise airframe with each Dryden level,
the shm transport, and the real autopilot (or the hold peer), stepping one frame at a time from Python and reading the
live state. 100,000 frames each; wall time 0.43 s per run including per-frame Python stepping. Margins below are max
|altitude - 10,000 m| and max |wrapped psi| over every frame:

| Level (sigma_high, w20) | Peer | Seed 42 | Seeds 43 to 50 (worst) |
|---|---|---|---|
| light (0.15 m/s, 15 kn) | autopilot | 1.3 m, 0.08 deg | 1.6 m, 0.12 deg |
| light | hold (constant trim) | 7.4 m, 0.60 deg | 7.7 m, 0.64 deg |
| moderate (1.5 m/s, 30 kn) | autopilot | 10.4 m, 0.83 deg | 16.4 m, 1.23 deg |
| moderate | hold | 21.0 m, 1.97 deg | 42.4 m, 1.94 deg |

Also at seed 42: autopilot 10,000 handled, 0 held, exit 0; moderate throttle 0.310 to 0.633 (trim 0.499), elevator
0.024 to 0.029 rad, aileron within +/-0.011 rad, air-relative sideslip within +/-0.93 deg (hold: +/-1.48 deg). No
channel came near a limit. **Consequences:** (1) no retune is indicated, so `embedded/autopilot` is not expected to
change; (2) **the R2 bands are non-discriminating at light for both quantities, and for heading at both levels**: a
constant-trim output also stays inside +/-30 m and +/-2 deg for 100 s (the airframe is stable enough over 100 s).
Only moderate altitude discriminates (hold misses in 6 of 8 seeds, not at seed 42). R2c records this as the parent plan
requires (lesson 2026-09-19); whether to add a discriminating gate is Joe's (Q1). The trial is not verified evidence;
the committed tests produce that.

## Contract

### Configs (D2)

Each new file is `cruise.yaml` copied verbatim (airframe, `initial`, `initial_controls`, comments) except:
- `physical.wind: { plugin: plugins/physical/wind/dryden, seed: 42, w20_knots: 15.0, sigma_high: 0.15 }` (light) or
  `w20_knots: 30.0, sigma_high: 1.5` (moderate), with the label and Joe's Q7 read cited: MIL-F-8785C Fig. 7 (p. 49) at
  32,800 ft, light = the 10^-2 curve (about 0.5 ft/s), moderate = the MODERATE line (about 5 ft/s), Joe's chart read of
  2026-09-24; plus the one-line note: "`cruise.yaml`'s `sigma_high` 3.0 m/s lies between the 10^-4 curve and SEVERE at
  10 km, despite its `# light` comment."
- `sim.frames: 100000` (10,000 exchanges at divisor 10; Q1 of the parent).
- A `transport:` block:

  ```yaml
  transport:
    plugin: plugins/sim/transport/shm
    # segment: supplied per run by astra.lockstep (astra_<driver pid>_<nonce>); absent here on purpose, so
    # this file alone fails loudly (ConfigError naming transport.segment) instead of using a shared name.
    divisor: 10                # 100 Hz exchanges (ADR-0022 default)
    timeout_ms: 1000           # ADR-0022 default (parent Q3), sanitizer-safe
    handshake_timeout_ms: 5000 # ADR-0022 default
    peer_timeout_ms: 2000      # >= timeout_ms (Q19); covers the driver's gap between handshake and run()
    commands:
      elevator: controls.elevator
      aileron: controls.aileron
      rudder: controls.rudder
      throttle: [controls.throttle, controls.throttle]   # ICD throttle[2] -> one shared input (Q10)
    limits:                    # EQUAL to embedded/autopilot/limits.hpp (ClosedLoopAcceptance.ConfigLimits...)
      elevator: [-0.3, 0.3]
      aileron: [-0.35, 0.35]
      rudder: [-0.35, 0.35]
      throttle: [0.0, 1.0]
  ```

- Tests pinning this: `ClosedLoopAcceptance.ConfigLimitsEqualAutopilotLimits` (C++, includes `limits.hpp`, reads both
  YAMLs through `astra::Config`, exact `==` on all eight numbers; the literals parse to the same doubles) and
  `test_turbulence_configs_differ_from_cruise_only_where_intended` (pytest, YAML dicts: equal to `cruise.yaml` except
  `physical.wind`, `sim.frames`, `transport`; wind seed 42 and the two Q7 values).

### Driver `astra.lockstep` (D3 to D7)

Pure Python, pyright-strict with the rest of `python/astra`. Linux only (POSIX shm under `/dev/shm`). Public surface:

- `Setpoints(altitude_m, airspeed_mps, heading_rad)`; `AutopilotTrim(elevator_rad, throttle, theta_rad)`;
  `trim_for(config_path, setpoints) -> AutopilotTrim` calls `astra.trim.trim_straight_and_level(config_path,
  airspeed, altitude)` (wind stripped by `astra.trim` itself) and maps `alpha_rad` to `theta_rad` (gamma = 0). Heading
  must be 0 (the trim pins psi = 0); anything else is a `ValueError`.
- `new_segment_name() -> str`: `astra_<os.getpid()>_<secrets.token_hex(8)>` (16 lowercase hex, inside the plugin's
  1-32 alnum rule).
- `remove_stale_segments(directory="/dev/shm") -> list[str]`: unlinks `astra_<pid>_*` entries whose `<pid>` is not a
  live process (`os.kill(pid, 0)` raises `ProcessLookupError`); a live or unparsable pid is left alone. The pid in the
  name is the driver's, matching the C++ `shm_test_support` sweep (same namespace, same rule). **Best-effort and
  conservative** (critique C-4): a reused pid keeps a stale name, and the probe and the unlink are not atomic; the sweep
  claims neither ownership nor complete reclamation. A leftover name can only exist when a sim process died between
  `shm_open` and the end of its handshake: after the handshake the plugin has unlinked the name (`shm.cpp:168`), and a
  failed handshake unlinks it too (`shm.cpp:170-174`).
- `load_config(config_path, *, segment, frames=None, seed=None, timeouts=None) -> dict`: YAML to dict; requires a
  `transport` block whose plugin is `plugins/sim/transport/shm` and **no** `segment` key (a present one is an error:
  the name is the driver's); sets `transport.segment`; optional overrides for `sim.frames`, `physical.wind.seed`, and
  the three timeouts (test use only; none changes a trajectory).
- `autopilot_argv(exe, config, setpoints, trim, *, notify=None)` and `hold_peer_argv(exe, config, commands)`: the
  segment and both peer-side timeouts are read **from the config dict**, so the sim and the peer can never disagree.
  Floats are passed with `repr()` (shortest round-trip, parsed exactly by `strtod`).
- `PeerProcess`: `subprocess.Popen` with stdout/stderr to temporary files (no pipe-buffer deadlock),
  `close_fds=True` plus explicit `pass_fds` (see the descriptor table below). `wait(timeout)` returns the status or
  `None` (never lets `TimeoutExpired` escape); `stop(grace_s)`: SIGCONT (a jittered peer may be stopped), SIGTERM,
  wait `grace_s`, SIGKILL, wait (bounded; a process that survives SIGKILL's bounded reap raises `LockstepError`
  after the rest of cleanup has run). Idempotent. `summary()` parses the exit line.
- `lockstep_session(config, peer_argv, plugin_dir=None)`, a context manager. Cleanup is **nested** so that no step
  can skip a later one (critique C-1): each step runs in its own `try/finally`, `KeyboardInterrupt` included, and the
  body's original exception is re-raised after cleanup (a cleanup error is chained to it, never replaces it).
  1. validates the executable exists and is executable **before** creating anything;
  2. launches the peer (it retries `shm_open` until its handshake timeout, so order is free); a `Popen` failure after
     validation closes every descriptor the driver opened and propagates;
  3. `Simulation.from_config(...)` (creates the segment, handshakes; a handshake failure raises `ConfigError`, and
     the plugin has already unlinked its name, `shm.cpp:170-174`);
  4. yields a `Session(sim, peer, segment)`;
  5. cleanup: (a) compares `sys.getrefcount` on the `Simulation` against a **baseline taken inside
     `lockstep_session` right after construction** (Joe, E2: never a hard-coded count; CPython 3.14 can report
     smaller values because it borrows stack references, and `requires-python` is >= 3.12): any extra reference (the caller's, or a
     `reference_internal` object such as `clock`, `registry`, `health`, `vehicles`, a live `state` view,
     `_core.cpp:458-478`) means the destructor cannot run, so `CLOSED` would never be sent; the driver records a
     `LockstepError("Simulation still referenced ...")` to raise at the end; (b) drops the session's reference (the
     destructor closes: `CLOSED` on normal completion, nothing after a failure); (c) if the run completed normally
     and no extra reference was found, `peer.wait(clean_grace)`; a timeout there, or any other case, escalates to
     `peer.stop(grace)` (bounded, always reaps); (d) unlinks `/dev/shm/<segment>` (FileNotFoundError ignored; the
     plugin normally unlinked it at handshake, `shm.cpp:168`), in a `finally` of its own.
  `run_closed_loop` copies every value it needs out of the `Simulation` and never exposes it.

  Descriptor ownership (critique C-3): the notify pipe is created by whoever launches the helper that reads it.
  | End | Held by | Closed by the launcher |
  |---|---|---|
  | write | the autopilot only (`--notify-fd`, `pass_fds`) | immediately after the autopilot's `Popen` returns |
  | read | the signaler only (`pass_fds`), or the launcher itself when it reads the byte | after the signaler's `Popen` returns |
  On any partial launch failure every end still open is closed and every launched process is stopped and reaped.
  EOF before the byte (the autopilot died first) is a test failure reported as such, never a hang.
- `run_closed_loop(config_path, peer, *, setpoints, frames=None, seed=None, ...) -> RunResult`: trim, session,
  one `run()` for all frames, counters from the registry, `exchange_recorder_bytes()`, close, peer exit status and
  summary. `RunResult` carries the halt, the counters, the raw recorder bytes, their SHA-256, the peer's return code
  and parsed summary.
- `parse_recording(data: bytes) -> Recording`: validates the 48-byte header (magic `ASXR`, version 1, record sizes 200,
  64, 112, reserved 0, byte length equals header + counts x sizes) and returns numpy structured arrays for requests,
  replies and states (dtypes transcribed from `icd/astra_icd.h`; their `itemsize` is asserted against the header's
  sizes). The transcription is checked by its own test, `test_recording_decoder_layout` (critique C-8): (i) every
  field's offset against an explicit expected table (the ICD structs are two `uint32`, a `uint64`, then 8-byte fields
  with no padding, so each offset is hand-countable); (ii) a short session against the echo peer in `echo` mode, whose
  reply is a known function of the request (`elevator = phi`, `aileron = theta`, `rudder = psi`, `throttle = {p, q}`,
  `fault_flags = (uint32)frame ^ status_flags`, `mode_flags = FNV-1a32` of the 200 raw request bytes;
  `tests/lockstep/echo_peer.cpp`), so every reply field is checked against a different request field and the FNV check
  covers the raw request record. R12b's own claim is limited to the twelve fields it compares.

### Binding (D1)

One additive method: `Simulation.exchange_recorder_bytes() -> bytes | None` (the `serialize()` output copied into a
new Python `bytes`; `None` without a transport). **A copy, never a view and never `reference_internal`** (Joe, Q2), so
it cannot keep the `Simulation` alive and trip the driver's refcount check; a test asserts the refcount is unchanged
after holding the returned object. No core change, no `kCoreApiVersion` bump (bindings only), stub
regenerated by `just python`. A `test_simulation.py` test: `None` without a transport.

### Placement of runs (D4, D5, D8)

**All lockstep runs in pytest happen in child processes**, never in the pytest process: a `tests/python/
lockstep_child.py` script (the "sim host") runs one scenario and writes JSON (and the recorder bytes) to a given
directory; the test starts it with `start_new_session=True` and a hard `subprocess` timeout, and on expiry kills the
whole process group. This gives a real hard timeout (a GIL-holding `run()` cannot block pytest), makes R3's runs
fresh-process as the parent plan requires, and frees the pytest process to act as the killer where needed.
`tests/python/lockstep_signaler.py` is the helper a child launches when it must act on the peer **during** its own
GIL-holding `run()`: `kill-on-notify` (block on the notify pipe, record `time.monotonic_ns()` **before** `kill()`,
SIGKILL the pid, print the timestamp) and `jitter` (below). Result: **no GIL release, no binding change for it** (the
"killer subprocess" branch of the brief). Every child stops and reaps its signaler on every exit path (SIGTERM, bounded
wait, SIGKILL), and the jitter signaler's SIGTERM handler sends a final SIGCONT.

**Reaping is owned by a surviving supervisor** (critique C-2). Killing a process group terminates grandchildren but
does not reap them, and an orphan reparents to init or the session's subreaper. So `test_lockstep.py` makes the pytest
process a child subreaper for the module's duration (`prctl(PR_SET_CHILD_SUBREAPER, 1)` through `ctypes`, standard
library, Linux; reset to 0 at module teardown). Each child reports the pids it launched (autopilot, signaler) on
stdout **as soon as it launches them**, while the child is alive and has not reaped them, so the pids cannot yet be
reused. When a child dies, its descendants reparent to pytest, which then `os.waitpid`s each reported pid with a
bounded poll (`WNOHANG` plus a deadline): that is collection, asserted separately from termination, and it also yields
the orphan's exit status. **pytest calls `waitpid` only on the pids the children reported, never `waitpid(-1)`**
(Joe, E3): reaping a `Popen`-managed child behind `Popen`'s back makes `Popen` see `ECHILD` and report return code 0,
which would silently turn a failure into a pass. The code carries the same comment. The hard-timeout path does the same after the group kill. A module-teardown guard reaps any
reported pid still unreaped and fails the module if one survives its bound.

## Acceptance (what each test asserts and reports)

Full-length tests (pytest, `tests/python/test_lockstep.py`, Release). Runs, each a fresh child, each once per session
(module-scoped fixture), 100,000 frames unless stated: **A** light/autopilot, **B** A repeated, **C** A with jitter,
**D** light/autopilot seed 43, **E** light/hold, **F** moderate/autopilot, **G** moderate/hold (Joe Q1 (b)), sweep
seeds 44 to 50 light/autopilot (D is the sweep's first seed). About 14 runs of about 1 s (trial: 0.43 s plus child
start and trim).

| ID | Test | Asserts | Reports |
|---|---|---|---|
| R1 | `test_r1_light`, `test_r1_moderate` (A, F) | halt falsy; `transport.exchanges == 10000`, `missed == 0`; recorder counts 10,000/10,000/100,001; every request and reply magic/version valid; `reply.frame == request.frame` and `request.frame == 10 k`; autopilot exit 0 and summary `handled 10000` (independent count); `held 0` (D11) | counts |
| R2a/b | `test_r2_light`, `test_r2_moderate` (A, F; gating, Q7) | every one of the 100,001 recorded states (frames 0 to 100,000, the final state included): \|`p_n[2]` - 10,000\| <= 30 m; \|wrap(psi)\| <= 2 deg, psi from `q_nb` by test-side numpy (3-2-1, S&L 1.3-11), never the ICD | max errors, margins, frame of each max; throttle min/max and max \|throttle - trim\|, max per-exchange throttle step; elevator/aileron/rudder ranges and exchanges at a limit; ICD `beta` range; `tas` range |
| R2c | `test_r2c_hold_is_reported` (E light, G moderate; Joe Q1 (b)) | lockstep health only (R1's counters, echo exit 0) | the same margins, and "non-discriminating" when hold also passes a band; never fails on R2 |
| R2d | `test_r2d_autopilot_beats_hold_light` (A, E; Joe Q1 (c), **light only**, seed 42) | autopilot max altitude error <= 0.5 x hold's, and autopilot max heading error <= 0.5 x hold's. Not at moderate: at seed 42 the altitude ratio is 0.495, too tight. (Included on the literal reading of Joe's approval, which kept the optional (c) line; removable by deleting this test.) | both ratios |
| R3 | `test_r3_bit_identical` (A, B, C), `test_r3_seed_changes_bytes` (A, D) | SHA-256 and byte equality of the whole serialized recorder (header, all three streams, the final state is the last state record) for A = B = C; C had >= 20 **observed** stops inside the `run()` window (see D6); A != D, and specifically their state streams differ | hashes; observed stop count, total and max observed stopped duration |
| Q15 | `test_seed_sweep_light` (D plus seeds 44 to 50) | lockstep health only | per-seed margins (non-gating, Q15) |
| R4 | `test_r4_sigkill_kill_to_halt` | autopilot `--notify-fd --stop-after 500`; signaler SIGKILLs it on the byte; child's `run()` returns `TRANSPORT` halt at frame 5000; `t_return - t_kill <= 550 ms` (500 + max(20, 50)) **and `t_kill < t_return`** (Joe, E1: `--stop-after` sends reply N and then `pause()`s, so the sim halts at its deadline whether or not the kill has landed; the -SIGKILL return code alone does not prove the kill preceded the halt; both are `CLOCK_MONOTONIC`); clock frame == halt frame; state finite; recorder has 500 replies and its last state record is frame 5000; a second `step()` returns the same halt with clock and state bytes unchanged (commands from before death not applied after halt); peer return code -SIGKILL | kill-to-halt |
| R4 | `test_r4_abort_kill_to_halt` | `--abort-after 500`; the child runs 4,990 frames (499 exchanges, `run()` returns), takes `t0 = monotonic_ns()`, then `run()`s on: the 500th exchange (frame 4990) is answered, the autopilot aborts, and the halt lands at frame 5000. `t0` precedes the final reply by construction, so `t_return - t0 <= 550 ms` bounds the reply-to-halt interval from above (critique C-5: the notify byte's receipt time could be late and would understate it); the child also confirms the byte arrived and the return code is -SIGABRT; the same frame/state/sticky-halt checks | reply-to-halt upper bound |
| R4 | `test_r4_wait_duration` | `--stop-after 500`; the child runs exactly 5,000 frames (500 exchanges), reads the byte, SIGKILLs **and reaps** the autopilot, then times one `step()` (frame 5000, an exchange frame): `500 ms <= duration <= 550 ms` (the deadline is neither early nor restarted) | duration |
| R4b | `test_r4b_sim_death` | pytest launches the autopilot itself (its child, so pytest reads the status) and a sim-only child on the same segment. **Barrier** (critique C-6): the child runs 1,000 frames, checks `transport.exchanges == 100`, prints `ready`, then calls `run()` for 200,000 more frames (about 1 s in Release, far longer than pytest's reaction, so it cannot close normally before the kill; `sim.frames` is raised to 201,000 so the recorder has capacity; a slow reaction makes the test fail with exit 0, never pass falsely). On `ready`, pytest confirms the autopilot and the child are both still running, takes `t_kill`, SIGKILLs the child, and reaps it. The autopilot's deadline began at its last reply, which precedes `t_kill`, so: autopilot exits **2** (timeout, not 0: no `CLOSED` was ever written) within `peer_timeout_ms` + allowance of `t_kill` (500 + 50 ms), and is reaped; no named segment remains (the handshake unlinked it) | kill-to-exit |
| R4b | `test_r4b_sim_pause` | the child runs 1,000 frames, sleeps 800 ms (> `peer_timeout_ms` 500), confirms the autopilot exited **2**, then `run()` halts `TRANSPORT` at the next exchange frame (1000) within `timeout_ms` + allowance (a detected failure, not a hang) | pause, halt time |
| R12b | `test_r12b_icd_matches_state` (A, F) | at each of the 10,000 exchange frames, recorded ICD `phi, theta, psi, altitude, lat, lon, u, v, w, p, q, r` against the state record of the same frame, each derived test-side: \|diff\| <= 1e-12 absolute (D12); **liveness** (Joe, E4): for every attitude and velocity field, the **maximum over all exchange frames** of the difference against the next frame's state exceeds 1e-9 (a one-frame lag cannot pass; individual frames are not required to exceed it) | max diff per field |
| Driver | `test_segment_name_*`, `test_remove_stale_segments_*`, `test_session_cleans_up_*` | name format and uniqueness; stale sweep removes a dead-pid entry and keeps a live-pid one; cleanup on: missing executable (nothing created), peer exits at once (bad args: handshake `ConfigError` with `handshake_timeout_ms` 300, peer reaped, no segment), exception inside the body after handshake (peer stopped and reaped within grace, segment gone, exception propagates), a SIGSTOPped peer (SIGTERM ignored until SIGCONT: `stop()` still reaps it), normal completion (peer exit 0); a retained `registry` view (the driver raises `LockstepError`, the peer is still stopped and reaped, the segment is gone); `KeyboardInterrupt` raised inside the body (same cleanup, the interrupt propagates). **Driver death, split** (critique C-4): (1) after the handshake: a child running the whole driver behind the same barrier as R4b is SIGKILLed; its orphaned autopilot reparents to pytest (subreaper), which reaps it with status 2 within `peer_timeout_ms` + allowance, and no named segment exists; (2) before the handshake: a child creates the `Simulation` with no peer and a 30 s handshake timeout, pytest waits until `/dev/shm/<segment>` exists, SIGKILLs and reaps the child, then `remove_stale_segments()` removes exactly that name. Plus a synthetic sweep test on a file named after a reaped pid | none |
| Config | `test_trim_matches_config_initial_state` | `trim_for` on each new config reproduces its `initial_controls` elevator/throttle and `euler_nb[1]` within 1e-12 absolute (the C++ tests below take the trim from the config, so this links the two) | the differences |

Timing tests use `timeout_ms = peer_timeout_ms = 500` (parent Q3), set by `load_config`'s override. Pytest runs last
in `just check` and serially, so no other gate stage loads the machine; other sessions on the same machine can
(lesson 2026-09-16), which is the flakiness risk disclosed below.

ctest (`tests/lockstep/closed_loop_acceptance_test.cpp`, C++, suite `ClosedLoopAcceptance`, `A6`'s selector):
- `ConfigLimitsEqualAutopilotLimits` (all presets, instant).
- `ShortLight` (all presets): light config, `sim.frames` 10,000 (1,000 exchanges), real autopilot via `Child` with the
  trim taken from the config's `initial_controls` and `euler_nb[1]`; R1's counters/echo/exit 0/`handled 1000`, and R2's
  bands over the 10,001 states (C++ test-side psi from `q_nb`, not `frames::`).
- `ShortReproducible` (all presets): two such runs in sequence in one process, `serialize()` byte-equal. (The
  fresh-process R3 is pytest's; this one is disclosed as same-process.)
- `FullLengthLight` (asan only, label `acceptance`, own executable so it is registered only there): the 100,000-frame
  light run with R1 and R2. About 65 s estimated; it overlaps PR 8's 348 s test under `--parallel`.
- ctest runs raise `timeout_ms`/`peer_timeout_ms` to 5000/10000 (a liveness bound under a 32-way parallel sanitizer
  load, not an accuracy tolerance; trajectories do not depend on it).

## Decisions (recommendations; Joe approves the plan)

- **D1 recorder binding**: add `Simulation.exchange_recorder_bytes()`. Needed by R1/R3/R12b; additive, no core change.
  Alternative: a core-side dump-to-file, rejected (file I/O and a path in `core/`).
- **D2 configs**: full copies of `cruise.yaml` plus a drift test (the config system has no include/merge). `segment`
  absent from the files (driver-owned). `timeout_ms` 1000, `handshake_timeout_ms` 5000, `peer_timeout_ms` 2000.
- **D3 trim and setpoints**: `trim_for` (wind stripped by `astra.trim`), `--theta-trim = alpha_rad`; setpoints altitude
  10,000 m, TAS 230 m/s, heading 0 (the config's psi is 0, confirmed).
- **D4 binaries**: `astra.lockstep` takes explicit executable paths; it knows no build layout. Tests resolve them from
  `ASTRA_AUTOPILOT` / `ASTRA_ECHO_PEER` if set, else from the imported module's build tree
  (`Path(_core.default_plugin_dir()).parent / "embedded/autopilot/astra_autopilot"` and
  `".../tests/lockstep/astra_echo_peer"`), and **fail** (not skip) if absent. ctest uses `$<TARGET_FILE:...>` defines.
  Nothing in `core/` or the binding learns either path.
- **D5 R2c**: `astra_echo_peer --mode hold --commands <elev>,0,0,<thr>` (trim via `repr`) on the light config,
  informational. See Q1 for moderate.
- **D6 R3 jitter**: `lockstep_signaler.py jitter --pid P --seed S`: loop {sleep U(0.5, 5) ms; SIGSTOP; poll
  `/proc/<pid>/stat` until the state is `T` (observed stop, `monotonic_ns`); sleep U(0.1, 10) ms; SIGCONT; poll until
  the state is no longer `T` (observed resume)} until SIGTERM, then a final SIGCONT; prints each observed interval.
  The 10 ms is the **requested** sleep, not a guaranteed bound (critique C-7): the helper can be descheduled while the
  autopilot is stopped. The observed durations are reported (total and max); a stop longer than the 1000 ms timeout
  would halt the run, which R1's counters and the byte comparison both report as a failure, never a silent pass.
  "Overlap with exchanges" is the count of observed stops inside the child's `run()` window (monotonic timestamps on
  both sides), required >= 20. The different-seed run uses seed 43.
- **D7 R4/R4b timing**: allowance max(20 ms, 10% of 500 ms) = 50 ms; kill-to-halt and wait duration are separate
  tests; every child has a hard `subprocess` timeout (60 s for a full run, 20 s for a kill test).
- **D8 GIL**: not released; the killer is a subprocess (the pytest process or the signaler).
- **D9 reports**: each child writes `summary.json` (the "Reports" column) and `trace.csv` per exchange (frame, altitude
  error, psi error, throttle, elevator, aileron, rudder, ICD beta, tas) to pytest's `tmp_path`, or to
  `$ASTRA_LOCKSTEP_REPORT_DIR` when set (how I produce the packet's evidence). No plotting dependency: the packet's
  trace figures are SVG written by a scratch script from the CSVs, and the CSVs decimated to 0.1 s are kept under
  `docs/reviews/evidence/`.
- **D10 hard timeout**: per-child `subprocess` timeouts plus group kill; no pytest-timeout dependency.
- **D11 `held 0`**: asserted in the gating runs. A held frame means the loop did not fly that exchange; R2 passing with
  held frames would overstate the autopilot. (Proposal; drop if Joe prefers report-only.)
- **D12 R12b tolerance 1e-12 absolute** (Joe's proposal, kept). Nine of the twelve fields are the same double copied
  (`u..r`, `lat`, `lon`, and `altitude = p_n[2]` for `wgs84_rotating`), expected exactly equal; the three angles are the
  same DCM formula evaluated in a different order, a few ULP of a value below pi, about 1e-15. 1e-12 leaves three decades
  of headroom and is nine decades below a one-frame lag (about 1e-6 rad in attitude, 1e-3 m/s in velocity per frame in
  turbulence), which the liveness half proves on the recorded data. Observed maxima are reported.
- **D13 R2 miss at moderate**: the gating test fails; I stop and report to Joe with the traces and margins before any
  retune (first knobs per the handoff: `kHeadingP` and the bank limit; `kAltP`/`kAltI`, then `kPitchTheta`/`kPitchRate`;
  any retune edits `gains.hpp` and reruns `test_autopilot_design.py`). A sideslip term is a law change: ask first.
  The trial says this is not expected.

- **D14 subreaper** (critique C-2): `prctl(PR_SET_CHILD_SUBREAPER)` via `ctypes` in the pytest process, scoped to
  `test_lockstep.py` and reset at teardown. Standard library, Linux only (as `/dev/shm` already is); no dependency.
  Alternative: a dedicated supervisor process per test, rejected as more moving parts for the same guarantee.

## Questions and Joe's decisions (2026-09-26)

1. **The R2 bands barely discriminate (trial above).** Constant trim passes +/-30 m and +/-2 deg at light for every
   seed tried, and +/-2 deg at moderate for every seed; only moderate altitude catches it (6 of 8 seeds, not seed 42).
   **Decision: (b) and (c).** (b): the hold peer also runs on the moderate config (run G), informational, reported in
   the packet beside the light hold run; the packet states that at seed 42 no R2 band separates hold from the
   autopilot, and that moderate hold heading is 1.97 deg against the 2 deg band. (c): gated at **light only**, seed 42:
   the autopilot's max altitude error and max heading error are each at most 0.5x the hold peer's (R2d). Not at
   moderate: at seed 42 the altitude ratio is 0.495, too tight. (Joe's approval carried (c) as an optional line marked
   "delete if not wanted" that was not deleted; it is implemented on that literal reading and is removable by deleting
   one test.)
2. **D1 binding**: approved. `Simulation.exchange_recorder_bytes()` returns a copied `bytes` (no view, no
   `reference_internal`); the stub is regenerated.
3. **D11**: assert `held 0` in the gating runs.
4. **Critique pass 2**: none before code. The Codex adversarial review brief must name the revision-2 mechanisms
   explicitly: the subreaper and reaping, the R4b and driver-death barrier, the nested cleanup and exception chaining,
   and the notify-pipe descriptor ownership.

Edits E1 to E4 (Joe, same approval), folded into the text above: E1 `t_kill < t_return` in the SIGKILL test; E2 the
refcount baseline taken right after construction; E3 `waitpid` only on reported pids, never `waitpid(-1)`; E4 R12b
liveness on the maximum over all exchange frames.

## Mapping and sequence (one builder)

| Step | Change / owned paths | Check |
|---|---|---|
| 1 | Configs (D2) | drift and limits tests |
| 2 | Binding (D1): `python/bindings/_core.cpp` | `test_simulation.py` addition |
| 3 | `python/astra/lockstep.py` | driver unit tests, pyright strict |
| 4 | `tests/python/lockstep_child.py`, `lockstep_signaler.py`, `test_lockstep.py` | A6 pytest |
| 5 | `tests/lockstep/closed_loop_acceptance_test.cpp`, `tests/lockstep/CMakeLists.txt` | A6 ctest |
| 6 | `docs/process/validation.md` (the A6 commands, the report directory variable) | docs-check |
| 7 | Full gate; evidence under `docs/reviews/evidence/` | `./scripts/check-full.sh` |

Files not touched: `core/`, `icd/`, `embedded/`, `plugins/`, `cruise.yaml`, `tests/golden/**`, PR 3/4/5 tests, ADRs.

## Validation and risks

- **A6**: `.venv/bin/pytest tests/python/test_lockstep.py`; `ctest --preset asan -R ClosedLoopAcceptance
  --no-tests=error`; `./scripts/check-full.sh`, log kept under `docs/reviews/evidence/`. PR 3/4/5 tests and the M3
  regression unmodified. Mutations (scratch, reverted, reported): a limit in one config changed (limits test fails),
  `held` forced (R1 fails), the R12b comparison shifted one frame (fails), jitter disabled (R3's cycle count fails),
  `timeout_ms` read from the wrong key (R4 wait-duration fails).
- **Runtime**: pytest about +20 to 30 s; ctest asan about +20 s for the short tests plus the 65 s full run in parallel.
- **Risks**: timing tests under another session's load (50 ms allowance; a failure is reported, never widened);
  LeakSanitizer under ptrace in this environment (lesson 2026-09-17: run the unchanged gate from a normal terminal if it
  blocks); `/dev/shm` leftovers after a crashed run (sweep fixture at session start and end); a leaked `Simulation`
  reference in a future caller keeps the segment open until garbage collection (documented).
- **Rollback**: revert the PR; everything is additive (two configs, one binding method, one module, tests).

## Plan critique and decisions

One fresh, read-only Codex critique on revision 1: `codex-companion.mjs task --fresh --prompt-file ...` (companion
1.0.6, no `--write`, exit 0, thread `01a0def9-4eed-7223-9761-0829d70bef9a`). Verdict "needs rework". Raw output stays in
the session scratchpad, as for the earlier M4 critiques. Eight findings, none rejected; I checked each against the
repository or the documented Linux/CPython behavior before accepting it. (An earlier `task --help` invocation started an
unrelated throwaway thread, `01a0def9-2170-...`, that only asked which command was meant; it read nothing.)

| ID | Sev | Finding | Evidence checked | Resolution (revision 2) |
|---|---|---|---|---|
| C-1 | Major | Sequential cleanup can be cut short; other bound objects keep the `Simulation` alive | `Popen.wait(timeout)` raises `TimeoutExpired`; `clock`/`registry`/`health`/`vehicles` are `reference_internal` (`_core.cpp:458-478`); close is destructor-only (`lockstep_exchange.cpp:157-160`) | Nested cleanup, escalation always reaps, original exception preserved, refcount check with `LockstepError`; tests for a retained view and `KeyboardInterrupt` |
| C-2 | Major | A group kill terminates but does not reap grandchildren; a pidfd shows termination, not collection | Linux reparenting semantics | pytest is a child subreaper for the module; children report pids at launch; bounded `waitpid` of each; teardown guard |
| C-3 | Major | Notify-pipe ends left open prevent EOF | `pass_fds` does not close the parent's copy; EOF needs every write end closed | Descriptor-ownership table; EOF-before-byte is a failure |
| C-4 | Moderate | Post-handshake driver death leaves no name to sweep; PID sweep is best-effort | `shm.cpp:168` and `:170-174` (I had found the first independently) | Driver-death test split (post-handshake: no name; pre-handshake: controlled crash, then sweep); sweep documented as conservative |
| C-5 | Major | Timestamping the notify byte's receipt can understate reply-to-halt | `main.cpp:60-72`: reply, then notify, then abort | `t0` taken before the final exchange runs, an upper bound by construction |
| C-6 | Moderate | "Inside `run()`" is not observable; a late kill could land after a normal close (exit 0) | GIL held in `run()` (`_core.cpp:449-455`); close precedes expiry (`hal_shm.cpp:185-209`) | Counter-checked barrier, then a long `run()`; liveness checked before the kill |
| C-7 | Moderate | Requested sleeps do not bound the stop; a successful `kill()` does not prove a stop | CPython `sleep` is a minimum; SIGCONT can be handled before an observable stop | Observed stop/resume via `/proc/<pid>/stat`, durations reported, 10 ms called a request |
| C-8 | Moderate | R12b does not validate every decoder offset | R12b compares twelve request fields only | `test_recording_decoder_layout` (explicit offsets plus an echo-mode session); R12b claim narrowed |

Revision 2 was not re-critiqued (Joe, Q4).

## Done when

R1 to R4b and R12b proved by A6 on the final candidate with retained evidence; Codex adversarial review (lifecycle,
cleanup, timing bounds, determinism); Joe's human-review packet with the R2 traces. Stop before push.
