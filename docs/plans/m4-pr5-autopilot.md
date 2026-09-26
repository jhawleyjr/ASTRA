# M4 PR 5: `embedded/autopilot` and the standalone embedded build

Spec and plan in one brief (a small, Tier 2 change). Parent: [M4 plan](m4-lockstep.md) PR 5 row, Q12, Q13, Q16.
Binding: [ADR-0006](../decisions/0006-icd-versioned-c11-structs.md),
[ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md), [ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md).
Risk: **Tier 2** (a new component; build/CI behavior; no kernel, ICD-layout, golden or ADR edit).
Status: **revision 2 approved by Joe; text edits applied (no re-approval needed unless something else changes)**. Base: `origin/main` `34eec2d` (confirmed by
`git fetch`; PR 4 #55 merged). Branch `worktree-m4-pr5-autopilot`. Correction rounds 0/2, failed integrations 0/2.

## Outcome and scope

The example flight software the M4 acceptance runs (PR 6) fly: a C++17 straight-and-level autopilot that is a
plain HAL client, plus the standalone build and boundary checks ADR-0008 promises.

In scope, exactly the plan's PR 5 row: `embedded/autopilot/`; `embedded/CMakeLists.txt` (replaces the top-level
`add_subdirectory(embedded/hal)` line; the HAL builds standalone too); `just embedded-standalone` wired into
`just check`; the autopilot `nm` boundary test; scripted-frame unit tests; the Q13 gain design test.
Out of scope: the Python driver, turbulence configs, the M4 acceptance runs (PR 6); any change to `astra_hal.h`,
the shm layout, `icd/`, `astra.linearize`, PR 8's test, or any golden/ADR.

## Repository findings (observed)

- The HAL's whole surface is `embedded/hal/astra_hal.h` (`hal_init`, `hal_wait_frame`, `hal_send`, `hal_now`,
  `hal_close`, `hal_exit_status`); `hal_now()` returns the `t` of the last frame, frame-derived (`hal_shm.cpp:229`).
- `embedded/hal/CMakeLists.txt` assumes the in-tree `astra::icd` and `astra_build_flags`; its header comment says PR 5
  makes it standalone-capable. `astra_build_flags` is defined inline in the top-level `CMakeLists.txt:32-42`;
  `astra_icd` in `icd/CMakeLists.txt` uses `ASTRA_SOURCE_DIR` (top level sets it).
- `tests/lockstep/check_no_astra_core.cmake` already implements the ADR-0008 binary check (readelf NEEDED + `nm`
  undefined `_ZN5astra`), used by `EchoPeer_links_no_astra_core`. PR 5 reuses it unmodified.
- `tests/lockstep/shm_test_support.*` has `Creator` (plays the sim for the HAL) and `Child` (spawns a process; its
  path comes from the `ASTRA_ECHO_PEER_PATH` define). PR 4's tests stay unmodified; PR 5 links the same support source
  from its own test target.
- ICD ranges: `astra_sw_to_sim_v1` elevator/aileron/rudder rad, `throttle[2]` 0..1; the sim halts (never clamps) on a
  command outside `transport.limits` (ADR-0022, `lockstep_exchange.cpp`); limits keys are `elevator|aileron|rudder|throttle`.
- `astra.trim.TrimResult` carries `elevator_rad`, `throttle` (aileron = rudder = 0 by construction).
  `test_cruise_phugoid.py` holds PR 8's 5x5 construction privately (`_altitude_column`, `_altitude_rate_row`), including
  `linearize._evaluate` and `_LONGITUDINAL_INDICES` (private); that file is not edited.
- Environment note: the main checkout's editable `astra._core` is stale (no `Config`); `just python` was run in this
  worktree (exit 0) and the D7 feasibility trial ran there, in the session scratchpad. Gain numbers are not pinned here.

## Decisions the plan makes (each is a proposal for Joe to approve or redirect)

**D1. Standalone `astra::icd` and warning flags, without forking.**
- Move the `astra_build_flags` block, verbatim, to `cmake/AstraBuildFlags.cmake`, guarded by
  `if(NOT TARGET astra_build_flags)`. The top level `include()`s it (same flags, same sanitizer hook: the tree's
  behavior is unchanged) and so does `embedded/CMakeLists.txt`. One definition, two includers.
- `embedded/CMakeLists.txt` starts with its own `cmake_minimum_required(VERSION 3.21)` (3.21 is the floor because
  `PROJECT_IS_TOP_LEVEL` needs it; nothing else here needs more, and ADR-0008's older embedded toolchain is the reason
  not to copy the tree's 3.28), then `project(astra_embedded LANGUAGES CXX)`. In-tree, the nested `cmake_minimum_required`
  only sets policies for this directory. If `PROJECT_IS_TOP_LEVEL` (standalone):
  set `ASTRA_SOURCE_DIR` to the repo root, append `cmake/` to `CMAKE_MODULE_PATH`, include `AstraBuildFlags`, and
  `add_subdirectory(${ASTRA_SOURCE_DIR}/icd icd)` (the same `icd/CMakeLists.txt`, not a second definition of
  `astra::icd`). In-tree, those targets already exist and none of this runs. Then `add_subdirectory(hal)` and
  `add_subdirectory(autopilot)`, the same either way.
- "Standalone" means no top-level `project()`, no Eigen/yaml-cpp/GoogleTest/Python: `embedded/` still lives at its
  repo position beside `icd/` and `cmake/` (a vendoring user copies the same layout, per ADR-0008). Standard is C++17,
  extensions off, set per target as today.
- `just embedded-standalone`: `cmake -S embedded -B build/embedded-standalone` with **no preset** (default generator,
  no `find_package`), built once with `clang++-18` and once with `g++` (ADR-0008's "older embedded toolchain"; both
  are installed on the supported host), each in its own build dir. It then asserts the configured cache names no
  Eigen3/yaml-cpp/GTest, runs `check_no_astra_core.cmake` on the built `astra_autopilot`, and smoke-runs it (no
  arguments: exit 1 with usage). Added to `check`'s dependency list right after `docs-check` (cheap; fails fast).
  Risk: `-Wnull-dereference`/`-Wconversion` differ under GCC; a GCC-only warning is fixed in the source, never by
  dropping a flag. If GCC noise proves out of proportion, the fallback is clang-only (a scope note to Joe, not silent).

**D2. Autopilot CLI** (`astra_autopilot`, hand-parsed, no dependency). Required and optional arguments:

    astra_autopilot --segment S --handshake-timeout-ms N --peer-timeout-ms N
                    --altitude-m H --airspeed-mps V --heading-rad PSI
                    --elevator-trim R --throttle-trim T --theta-trim R
                    [--notify-fd FD (--stop-after N | --abort-after N)]

- The nine named arguments are required; each `--key value` once. Timeouts are positive `uint32` (the HAL enforces
  `> 0`; `peer_timeout_ms >= sim timeout_ms` is the driver's contract, ADR-0022, not re-checked here). The three setpoints
  and three trims must be finite; `--elevator-trim` inside the elevator limit [-0.3, +0.3] rad and `--throttle-trim` in [0, 1] (D4: a trim outside the
  autopilot's own output range is rejected, since the frame-0 or held reply is the trim itself); the segment non-empty. Anything else (unknown key, missing,
  repeated, malformed, non-finite): usage on stderr and **exit 1** (the HAL reserves 1 for argument errors). Bad
  arguments never reach `hal_init`.
- **Test hooks, with the echo peer's exact semantics** (parent plan R4): `--notify-fd FD` names a pipe the process
  writes one byte to; `--stop-after N` writes it after the Nth reply and then `pause()`s until killed (R4's SIGKILL
  case); `--abort-after N` writes it after the Nth reply and calls `abort()` (R4's abort case). `--notify-fd` is
  required with either of the other two, and the two are mutually exclusive; both counts are positive. They run only
  after a completed `hal_send`, outside the control computation, so they cannot change a reply. They are the only
  test-only code in the binary and the boundary test covers them (no astra symbol, no clock).
- **Exit summary (parent plan R1):** on every exit after argument parsing succeeds (all HAL statuses, not the
  abort/stop paths), one line on stdout, in the echo peer's format: `autopilot: handled <n> held <m> status <s>`, where
  `n` is the count of frames replied to, `m` the count of those replies that held the previous command (D3), and `s` the
  `astra_hal_status` integer. Flushed before the exit status is returned. Tested in R6.
- **Q12 biases:** `--elevator-trim` and `--throttle-trim` are `TrimResult.elevator_rad` and `.throttle` (PR 6's driver
  passes them, and `--theta-trim`, from `astra.trim`; **`--theta-trim` is `TrimResult.alpha_rad`**: `TrimResult` has no theta field, and at straight-and-level trim (gamma = 0, beta = phi = 0) theta = alpha exactly (checked at this cruise: `alpha_rad` = 0.023586859573503434 = the pitch of `TrimResult.state`); the autopilot never computes a trim). Aileron and rudder biases are the constant 0.0
  (trim has aileron = rudder = 0). Each loop's integral starts at 0, so its output is `bias + P + I + D` and the
  integrator is "initialized at the bias". **The frame-0 reply equals trim only when the frame-0 measurement is at its
  setpoint**, which is true in the scripted unit test and **not** under Dryden turbulence, where frame 0 already carries
  a gust (lesson 2026-09-22): PR 6 must not assert an exactly-trim first reply in the turbulence runs. Setpoints are
  explicit rather than latched from frame 0, so a config that does not match the trim is a visible transient.
- Speed reference is `tas` (the trim's `true_airspeed_mps`), not `cas` (approved), so the status bits that zero
  only `cas`/`mach` (`ATMOSPHERE_INVALID`, `CAS_SUPERSONIC`) do not affect the loop (see D3's hold mask).
- **No zero-gains mode, no timing hook.** R2c's "constant trim output" run is `astra_echo_peer --mode hold --commands
  <elev>,0,0,<thr>` (recorded in the PR 6 handoff), and R3's jitter is injected by PR 6's driver
  (SIGSTOP/SIGCONT or similar) outside the autopilot. The autopilot has no gain-override, sleep, or delay option.

**D3. Loop structure (decided, not a fallback), `dt`, and non-finite input.** "Four PID loops" means four command
channels, each an outer PID/PI on its tracked quantity plus **fixed inner-loop feedback gains** (plain gains on
measured signals; not a fifth loop). One `Pid` class (integral in command units, derivative on measurement, output
clamp with conditional-integration anti-windup) serves the outer terms. Sign convention: ICD comment (positive elevator
nose-down, positive aileron right-wing-down, positive rudder nose-left), so each row's signs are stated with the
measured signal's sign.

| Channel | Outer loop | Fixed inner-loop feedback | Signals read from `astra_sim_to_sw_v1` |
|---|---|---|---|
| elevator | PID on `e_h = altitude_cmd - altitude`; D on measurement uses **`vertical_speed`** (measured, not differenced) | `+K_theta * theta_err + K_q * q` (pitch attitude and pitch rate; `theta_err = theta - theta_trim`, see below) | `altitude`, `vertical_speed`, `theta`, `q` |
| aileron | PI on `e_psi = wrap(heading_cmd - psi)` (to [-pi, pi]) producing a **bank command** `phi_c`, clamped to +/-0.35 rad | `K_phi * (phi_c - phi) - K_p * p` on the aileron | `psi`, `phi`, `p` |
| throttle | PI on `e_v = airspeed_cmd - tas` | none | `tas` |
| rudder | none (pure yaw damper) | `+K_r * r + K_beta * beta` | `r`, `beta` |

- **Bank limit +/-0.35 rad (20 degrees):** it keeps a heading correction inside the small-angle regime the linear
  design assumes (the outer gain reaches it at about a 10-degree heading error) and inside a comfortable transport
  bank for a straight-and-level task. Provisional; PR 6's nonlinear runs are where it is proven.
- **`theta_trim`:** `K_theta` and `K_q` multiply a deviation, so the elevator term is
  `K_theta * (theta - theta_trim)`. The trim pitch is an explicit ninth required argument, `--theta-trim` (D2), taken by
  PR 6 as `TrimResult.alpha_rad` from the same `astra.trim` call as the elevator bias (0.0236 rad at this cruise; theta = alpha at gamma = 0, and `TrimResult` has no theta field): latching it from the first
  frame would be the hidden setpoint D2 rejects, and omitting it would bias the elevator by `K_theta * 0.0236` and pull
  altitude off.
- **Yaw damper without a washout, and why:** the task is wings-level heading hold, whose steady state has `r = 0` and
  `beta = 0` (trim has aileron = rudder = 0), so a steady-state-blocking washout has nothing to unblock; it would add a
  filter state, another `dt`-dependent update, and a time constant to design. The trial shows the plain `r + beta`
  feedback meets the floors. A steady turn would need a washout (or turn-coordination); a turn is out of scope, and
  the plan says so rather than leaving it implicit.
- Every quantity the design test differentiates is the one the C++ reads (the loops above are the design model,
  one to one).
- **`dt` from frame-derived time, no clock in the autopilot:** the only place `dt` enters is the integrators (D
  terms use measured rates). `dt = hal_now() - t_prev`, with `t_prev` the previous **frame's** `hal_now()`; `hal_now()`
  is `(double)frame * dt_sim` (ADR-0010), so `dt` is the exchange period whatever `transport.divisor` is, up to
  floating-point rounding of the two `t` values (deterministic, and not exactly 10 ms), and the autopilot does not
  know the divisor. Frame 0 has no previous frame: `dt = 0`, integrals unchanged. A `dt` that is not finite,
  `<= 0`, or `> 1 s` skips integration for that frame (P and D still act). **Time bookkeeping is separate from
  controller state:** `t_prev` advances on **every** frame, held or not, so the first valid frame after held frames
  integrates over one exchange period (the held frames' intervals are simply not integrated), never over the whole
  gap. No `<chrono>`, `time`, `clock_gettime` in `embedded/autopilot` (R4 scans each of its objects; the HAL's own
  ADR-0023 site is inside `astra_hal`, allowed there only).
- **Invalid input holds the last command.** Hold mask, exactly: any field a loop reads (`altitude`, `vertical_speed`,
  `theta`, `q`, `psi`, `phi`, `p`, `tas`, `r`, `beta`) non-finite, **or `status_flags & WIND_INVALID`**, which the ICD
  says zeroes `tas`, `alpha`, `beta`, `mach`, `cas` (`icd/astra_icd.h`); a zero `tas` would command full throttle. The
  other bits are **ignored on purpose** because they touch no field a loop reads: `ATMOSPHERE_INVALID` (zeroes `mach`,
  `cas` only), `CAS_SUPERSONIC` (`cas` only), `ACCEL_INVALID`, `LATLON_NOT_MODELED`. A held frame's reply repeats the
  previous reply's commands (the trim biases on frame 0), with no integration and no change to loop state (integrals,
  the bank command, the last output); only `t_prev` and the counters advance. `fault_flags` stays 0 (Q16: recorded, not
  acted on). The held count is internal and printed in the exit summary (D2). The held reply always satisfies D4
  because D2 rejects a trim outside the output range.
- Per-frame path: no allocation, no exceptions, no `std::string`, no I/O inside the loop (C++17, per AGENTS.md).

**D4. Output limits and the sim's `transport.limits`.** Compile-time constants in `embedded/autopilot/limits.hpp`,
per surface: elevator **[-0.3, +0.3] rad**, aileron **[-0.35, +0.35] rad**, rudder **[-0.35, +0.35] rad**, throttle
**[0, 1]**. Sources: `docs/refs/aircrafts/737.xml` lines 204-205 (elevator scale range +/-0.3; read from the main
checkout, `docs/refs` is gitignored), aileron and rudder +/-0.35 as the same file and `docs/plans/pr6-trim.md` D10 use, and
the ICD's throttle range (`icd/astra_icd.h`). The autopilot clamps each command at its range itself. PR 6's
`transport.limits` use **these same values, not wider ones**: a clamped value equals `hi`, and the sim's check is
`v > hi` (`lo`: `v < lo`), so equal limits raise no false halt, while a broken clamp (a value beyond the range) still
halts. PR 5 cannot check this against a sim config that does not exist yet, so PR 6 owns one test (the autopilot's
`limits.hpp` values equal the config's `transport.limits`), recorded in the PR 6 handoff. A non-finite value never
leaves: outputs are clamped and `std::isfinite`-guarded (D3's hold).

**D5. Constant `mode_flags`.** `kModeFlags = 0x00000001u` (named in `limits.hpp`, with the comment that the ICD
defines no mode bits in v1 and the sim records but does not act on it, Q16). Nonzero on purpose: a zeroed or
sim-fabricated reply is then distinguishable from a real autopilot reply in the recorder's reply stream (a test
asserts every reply carries it). `fault_flags = 0`.

**D6. Exit status is `hal_exit_status` unchanged.** The main loop is the HAL header's own template: the last
`astra_hal_status` from `hal_init` / `hal_wait_frame` / `hal_send`, then `hal_close()`, then
`return hal_exit_status(s)`. CLOSED exits 0; TIMEOUT 2, HANDSHAKE_TIMEOUT 3, BAD_LAYOUT 4, PROTOCOL 5, ERROR 6; 1 is
only the argument error. No remapping, no `atexit`, no extra failure path (a `hal_send` failure is the status
returned). Tested end to end (R6).

**D7. Gain design (Q13, decided (a); structure fixed by D3).** `tests/python/autopilot_design.py` (a helper module,
not a test) builds the closed-loop models; `tests/python/test_autopilot_design.py` asserts on them.
- **Plants.** Longitudinal: PR 8's 5x5 `[u, w, q, theta, h]` (the longitudinal block of PR 7's `A`, the
  finite-difference altitude column, the kinematic `h_dot = u sin(theta) - w cos(theta)` row, S&L Example 3.8-5) with
  the elevator and throttle columns of `B` and a zero `h_dot` row. Lateral: the 9-state `A` restricted to
  `[v, p, r, phi, psi]` (its `psi` row is `r / cos(theta)`), aileron and rudder columns of `B`. `astra.linearize`
  is untouched; the helper uses its public `linearize()` result and, for the altitude column, the same private
  `_evaluate` PR 8's test uses (a pyright-ignore, as there), which is the one coupling to disclose.
- **The design ties to the shipped C++:** the test reads the gains from `embedded/autopilot/gains.hpp` (fixed
  `constexpr double kName = value;` lines, strict regex, a missing or extra name fails), and the same file's limits
  and bank clamp are not in the linear model (disclosed: saturation is not analyzed here, PR 6's R2 is the check).
- **Assertions (thresholds below; every one has a stated source or is marked provisional):**
  1. Closed-loop stability of the coupled longitudinal system (elevator + throttle + both integrators) and the
     lateral system (aileron + rudder + heading integrator): every pole `Re < 0`, largest real part at or below the
     floor, every oscillatory pole's damping ratio at or above the floor.
  2. **Loop-at-a-time margins, broken-loop, at each actuator input** (elevator, throttle, aileron, rudder; the other
     channels closed): the return ratio `L(jw) = C_i (jwI - F_i)^-1 B_i` with channel `i`'s feedback removed from the
     plant input and every other channel closed, critical point +1 in this sign convention. Phase margin at every
     `|L| = 1` crossing and gain margin (both the increase and the decrease direction) at every 0-degree crossing,
     over 1e-3 to 2e2 rad/s. Loop-at-a-time margins do not bound simultaneous multi-loop variation; that is disclosed,
     not claimed.
  3. **Discretization, exactly the sim's application order:** `docs/architecture.md` §5 and ADR-0022 apply a command
     in the frame it arrives, with zero compute delay, so the plant is discretized with a **zero-order hold at the
     exchange period (10 ms) and no extra delay**: `u_k = C x_k` (integrators updated with `e_k` at frame `k`), held
     over `[t_k, t_k + 10 ms)`. `|z| < 1` for every eigenvalue of that closed loop. A one-exchange-delay variant is also
     computed and reported as **conservative** (it is not the acceptance).
  4. **Helper cross-check against recorded PR 8 values, no import of `test_cruise_phugoid.py`:** the helper's 5x5
     open-loop phugoid must reproduce PR 8's recorded reference, period 108.5 s and damping 0.027 (PLAN.md; recorded to
     108.53 s and 0.0273 in `docs/plans/pr8-cruise-regression.md`), within **0.5 % in period and 0.002 in damping**
     (a stated tolerance: PR 8 itself agrees to 0.04 % / 0.0002, so this catches a broken helper without being
     brittle to a rebuild).
- **Thresholds and their sources.**
  - Phase margin >= **45 degrees** and gain margin >= **6 dB** (both directions): the MIL-STD-1797A Appendix A
    bandwidth definition in `docs/refs/textbooks/MIL-HDBK-1797 ... .PDF` (Appendix A "D. Bandwidth, Time Delay"; "phase
    margin at least 45 deg and gain margin at least 6 dB"). **Provisional:** that text defines a pilot-in-the-loop
    handling-qualities bandwidth, not a flight-control stability-margin requirement, and I found no design-margin
    requirement in `docs/refs`. **Accepted by Joe as ASTRA's provisional thresholds.** The conventional source, MIL-F-9490D, is not in `docs/refs`
    and is frequency-dependent, less strict below 0.06 Hz, so a flat 45 degrees / 6 dB is conservative there.
  - Damping floor **0.30** on every closed-loop oscillatory pole (accepted as provisional). Cruise is **Category B**. Lower bounds from `docs/refs/MIL-F-8785C.pdf`, each
    **checked against the page image, not the OCR text**: phugoid Level 1 zeta >= 0.04 (3.2.1.2, p. 12); short
    period zeta 0.30 to 2.00, Category B Level 1 (3.2.2.1.2, Table IV, p. 13); Dutch roll, Table VI (p. 22), Category B
    Level 1, all classes: zeta_d >= 0.08, zeta_d*wn_d >= 0.15 rad/s, wn_d >= 0.4 rad/s (Class III may be excepted from
    the wn_d minimum). The 0.30 floor is ASTRA's own **provisional** design target, above all of
    those; the MIL-F-8785C values are checked in addition as hard lower bounds on the modes the test can identify.
  - Largest real part **at or below -0.03 1/s** (accepted as provisional, informed by the trial: the slowest closed-loop pole
    is the heading integrator's, about -0.063 1/s, and the throttle/altitude slow pair about -0.096 1/s; -0.03 keeps a
    factor of two). To be finalized against the frozen gains.
  - The design model is not the acceptance oracle; the nonlinear closed-loop runs with turbulence (PR 6, R2) are.
- **Feasibility trial (session scratchpad, not in the repo; numbers are not pinned).** The revised structure meets
  every floor above on the linear models, at gains bounded to sane actuator use (below). Longitudinal: poles all
  `Re <= -0.096`, min damping 0.66, phase margin 94 degrees (elevator) and 74 degrees (throttle), gain margin unlimited
  upward (57 dB downward on the elevator), discrete `max |z| = 0.99900`. Lateral: poles `Re <= -0.063`, min damping
  0.35, phase margin 133 degrees (aileron) and 56 degrees (rudder), discrete `max |z| = 0.99936`. Optimizer gains, for
  scale only: `K_h ~ 2.9e-4 rad/m`, `K_theta ~ 0.23`, `K_q ~ 0.42`, throttle `K_v ~ 0.067 /(m/s)`, heading `K_psi ~
  2.0 rad/rad` (at the trial's search bound: **the build must not freeze `gains.hpp` with `K_psi` at an arbitrary bound; widen the search or record why the bound is right**), `K_phi ~ 0.12`, `K_p ~ 0.47`, `K_r ~ 0.22`, `K_beta ~ 0.53`. The trial's own
  0.999 discrete-pole target was tighter than `|z| < 1`; the lateral search flagged it as its only unmet term, which
  is not a requirement. If the built design cannot meet the floors, stop and report (no fifth loop, no cascade).

## Requirements and mapping

| Req | Change / owned paths | Acceptance |
|---|---|---|
| R1 | `embedded/autopilot/{CMakeLists.txt,main.cpp,autopilot.{hpp,cpp},pid.hpp,gains.hpp,limits.hpp}`; the loop logic is a static library `astra_autopilot_lib` linked only to `astra::icd`; `main.cpp` links `astra::hal` and includes only `astra_hal.h` and `icd/astra_icd.h` | ctest `Autopilot*` |
| R2 | D1: `cmake/AstraBuildFlags.cmake`, top-level `CMakeLists.txt` (include replaces the block, and `add_subdirectory(embedded)` replaces the HAL line), `embedded/CMakeLists.txt`, `embedded/hal/CMakeLists.txt` (header comment updated; still the same targets) | `just embedded-standalone`; the whole tree still builds in all four presets |
| R3 | `justfile` (`embedded-standalone`, added to `check`); `docs/process/validation.md` gets one line naming the recipe | `just embedded-standalone`, `just check` |
| R4 | `tests/embedded/CMakeLists.txt` (added from `tests/CMakeLists.txt`): `EmbeddedBoundary_autopilot_links_no_astra_core` (existing script on `astra_autopilot`), plus the added no-clock rule (`nm --undefined-only` on **each autopilot object**, `astra_autopilot_lib`'s archive members **and `main.o`**, shows no `clock_gettime`/`gettimeofday`/`time`/`clock`; the linked binary is not scanned for this because it legitimately contains `astra_hal`'s ADR-0023 site, and the HAL's own objects are the only allowed place), plus, **optionally**, that `astra_autopilot_lib` and `main.o` include no `astra_core` header (the include closure is `icd/` and `embedded/hal/astra_hal.h` only, by a `-M` dependency check); this part is optional and is dropped, with a note in the packet, if it proves fiddly (the link rule and the `nm` test already carry ADR-0008's boundary) | ctest `EmbeddedBoundary*` |
| R5 | `tests/embedded/autopilot_test.cpp`, gtest, suite `Autopilot`: scripted `astra_sim_to_sw_v1` frames into `Autopilot::update(in, now, out)` (a pure function, no HAL) | ctest `Autopilot*` |
| R6 | `tests/embedded/autopilot_process_test.cpp`, suite `AutopilotProcess`: the real binary spawned against `Creator` (the sim side) with scripted frames | ctest `Autopilot*` |
| R7 | D7: `tests/python/autopilot_design.py`, `tests/python/test_autopilot_design.py`, `embedded/autopilot/gains.hpp` | pytest |

R5 scripted-frame cases (each asserts the exact reply bytes where the arithmetic is closed-form): frame-0 reply is
trim with the measurement at setpoint (R2/Q12); each channel's response to a single-signal step (altitude, `theta`, `q`, heading, `phi`, `p`, tas, `r`, `beta`) has the D3 sign and magnitude, the bank command clamps at 0.35 rad, and `theta` feedback acts on `theta - theta_trim`; the integrator
advances by `Ki * e * dt` with `dt` from the `t` sequence; frame 0 and a repeated `t` do not integrate; `divisor`
independence (frames 10 ms apart and 20 ms apart give the corresponding integral, no assumed period); anti-windup
holds the integral at saturation and unwinds without overshoot; every output inside the D4 limits under a violent
input; heading wrap (+179 to -179 degrees commands the short way); `mode_flags == kModeFlags`, `fault_flags == 0`,
`magic`/`version`/`frame` echoed, throttle entries identical, all 64 bytes deterministic (padding zero); NaN input in each read field and `WIND_INVALID` hold the previous command with no loop-state change, while `ATMOSPHERE_INVALID`, `CAS_SUPERSONIC`, `ACCEL_INVALID` and `LATLON_NOT_MODELED` (each set alone) do **not** hold; recovery after one and after several held frames integrates over exactly one exchange period (`t_prev` advanced on every frame); an out-of-range `--elevator-trim`/`--throttle-trim` exits 1 (R6), and a run whose frame 0 is invalid replies with in-range trim biases; replay of the same
script twice is bit-identical; the update path allocates nothing (existing `tests/support/alloc_counter.cpp`).

R6 cases: the exit summary line `autopilot: handled <n> held <m> status <s>` (n, m, s checked on a scripted run with a held frame); `--stop-after`/`--abort-after` with `--notify-fd` (one byte after the Nth reply, then killable / abort status, same as `EchoPeer` R4 cases); `CLOSED` (Creator `close_normal`) exits 0; peer silence past `--peer-timeout-ms` exits 2 (500 ms tests);
no segment past `--handshake-timeout-ms` exits 3; a corrupted header exits 4; a request with a wrong sequence exits
5; each argument error exits 1 and never touches a segment; and the process's replies over 200 scripted frames equal
`Autopilot::update` on the same frames (the binary and the library agree). `Child`'s program path is the
`ASTRA_ECHO_PEER_PATH` define, which this target sets to the autopilot binary (a naming quirk, noted in the test; PR 4's
files are untouched).

## Sequence (one Claude builder, one writer)

1. D1 CMake refactor first, proving the in-tree build unchanged (`git diff` of the preset builds, PR 3/4 tests
   unmodified and green) before any autopilot code exists. 2. `Pid` + `Autopilot` + unit tests (R1, R5) with
   placeholder gains. 3. Standalone recipe + boundary tests (R2 to R4). 4. Process tests (R6). 5. Gain design (R7) then
   freeze `gains.hpp`; re-run R5/R6 on the final gains. 6. `check-full.sh`, evidence log, Codex review, packet.

## Validation

- A5, as given: `just embedded-standalone`; `ctest --preset asan -R '(Autopilot|EmbeddedBoundary)' --no-tests=error`;
  `.venv/bin/pytest tests/python/test_autopilot_design.py`; then `./scripts/check-full.sh`, its log kept as
  `docs/reviews/evidence/<candidate SHA>-full-gate.log` (read the `check: OK` line; nothing is verified before it).
- Must pass **unmodified**: PR 4's lockstep tests, PR 3's tests, the M3 frozen regression, PR 8's phugoid test. The
  tree's warnings and sanitizers stay the same (D1 moves, not edits, the flags).
- Mutation checks recorded in the evidence: wrong `dt` handling, a removed clamp, a sign flip in each loop, an autopilot
  that links `astra_core`, and a `clock_gettime` in the loop each fail a named test.
- Known environment limit: LeakSanitizer under ptrace (lesson 2026-09-17) applies to the asan runs; disclosed, not
  worked around.
- No new dependency, toolchain, or human-owned file is touched. The oracle boundary holds: no `tests/golden/**`,
  tolerance, or ADR change.
- Rollback: revert the PR; the only shared-file edits are the top-level `CMakeLists.txt`, `justfile`, and `tests/CMakeLists.txt`.

## Plan critique and decisions

- **Codex plan critique: run once, fresh, read-only** (`/codex:rescue --fresh --wait`, per `docs/agents/review-dispatch.md`),
  on revision 2 of this plan. It returned four findings and no failed assumption for the standalone-build design (1),
  the D7 design test (2), or the parent-plan gaps (5). That silence is **not** a clearance: the critique's own report
  said nothing on D7's thresholds and sources, so D7's margin and damping thresholds stay labeled provisional and
  the design test is checked by its own mutation cases, not by this critique. Each finding was verified against the
  repo before it was accepted:

  | # | Finding | Evidence checked | Disposition |
  |---|---|---|---|
  | 1 | An accepted `--elevator-trim` outside +/-0.3 rad would be sent unchanged on an invalid first frame and halt the sim | `lockstep_exchange.cpp:287`: `values[i] < lo \|\| values[i] > hi` halts; HAL copies without clamping | **Accepted.** D2 rejects an out-of-range elevator or throttle trim (exit 1); R5/R6 test it and a first-frame-invalid run |
  | 2 | "No state change" on held frames vs. `t_prev`: a frozen `t_prev` would integrate the whole gap on recovery | D3's own definition | **Accepted.** D3 separates time bookkeeping (`t_prev` advances every frame) from controller state; R5 tests recovery after one and several held frames |
  | 3 | Hold rationale and test list wrong: `ATMOSPHERE_INVALID` zeroes only `mach`/`cas`, `WIND_INVALID` zeroes `tas`; a "each bit" test contradicts the `tas` choice | `icd/astra_icd.h:36-42` | **Accepted.** Hold mask is now exactly non-finite read fields plus `WIND_INVALID`; the other bits are ignored on purpose and tested as ignored. My earlier "atmosphere-invalid zeroes `tas`" claim was wrong |
  | 4 | The clock scan covered only the library archive, not `main.o`; the reused script checks no clock | `check_no_astra_core.cmake:15-26` (NEEDED + `_ZN5astra` only) | **Accepted.** R4 scans every autopilot object including `main.o`; the linked binary is excluded because it contains the HAL's permitted site |

  Reported not-findings (Codex's progress log, not its findings): the standalone CMake design looked viable and the
  transport's limit comparisons are strict, which is what D4's "equal limits raise no false halt" relies on
  (checked separately here: `lockstep_exchange.cpp:287`).
- **Approvals:** the parent plan's Q12, Q13 (decision (a)), Q16 and Q10 (2026-09-24). **Joe approved revision 2** on
  the review that followed this critique, including D1 (`cmake/AstraBuildFlags.cmake`, GCC in the standalone recipe;
  clang-only only if disclosed), `tas`, hold-on-invalid, D5, D6, the 45 degrees / 6 dB / 0.30 / -0.03 thresholds as
  provisional, the 0.35 rad bank limit, the no-washout yaw damper, and per-surface limits equal to `transport.limits`.
  Nothing is pending.
- **Open uncertainty:** the D7 trial is on the linear models only, at optimizer gains; saturation (bank clamp, output
  limits) and turbulence are not in it and are PR 6's R2. If the built design cannot meet the floors, the builder stops
  and reports (no fifth loop, no cascade).

## Implementation record (deviations and additions, as built)

- `embedded/autopilot/main.cpp` is its own OBJECT library (`astra_autopilot_main`) that the executable links, so
  `EmbeddedBoundary_autopilot_reads_no_clock` can scan exactly that object plus the library archive; nothing else changed.
- `--airspeed-mps`, `--altitude-m`, `--heading-rad` and `--theta-trim` are checked finite only (as planned); a zero or
  negative airspeed setpoint is not rejected.
- The R4 `-M` include-closure check was **kept** (`EmbeddedBoundary_autopilot_include_closure`): it was not fiddly.
- Gains are frozen from the design step; the search, the `kHeadingP` sweep, and the pole/margin results are in
  `docs/reviews/evidence/m4-pr5-gain-design.md`. `kHeadingP` = 2.0 is the smallest tried value that meets the stricter
  search targets, not a search bound.
- The design test's margin machinery is proved live by a weakened elevator loop (a tenth of the gains: stable, phase
  margin 38 degrees, below 45). Loop-at-a-time margins are meaningful only for a loop that is stable when closed
  (a sign-flipped elevator loop is unstable yet reports large margins), so the test asserts stability alongside them.
- Mutation checks run (each fails a named test; results in the packet): dt ignoring timestamps, elevator sign, clamp
  removed, anti-windup removed, `WIND_INVALID` not held, `ATMOSPHERE_INVALID` also held, `t_prev` frozen on held frames,
  heading not wrapped, bank command not clamped, `theta` feedback on absolute theta, `mode_flags` zero, exit status
  remapped, a clock read in the loop, an `astra_core` header in the include closure, and the autopilot linking
  `astra_core` (the last fails `EmbeddedBoundary_autopilot_links_no_astra_core`).

## Done when

R1 to R7 proven by the A5 commands and `check-full.sh` on one candidate SHA with the log retained; PR 3/4/M3/PR 8
suites unmodified and green; one Codex review of the candidate verified per `/review-cycle`; the `/human-review`
packet written; **stop before push**. PR 6 handoff (recorded in the packet): the autopilot CLI (D2, with
`--theta-trim` = `TrimResult.alpha_rad`), the limits `transport.limits` must equal (D4), the exit codes (D6), the
frozen gains, that the frame-0 reply equals trim only with the measurement at setpoint (not under Dryden), that R2c's
constant-trim run is `astra_echo_peer --mode hold --commands <elev>,0,0,<thr>` and R3's jitter is injected by the
driver, that **throttle activity from tas gusts is to be inspected in the traces**, and that **`K_psi` and the bank
limit are the first knobs if heading misses under moderate turbulence**.
