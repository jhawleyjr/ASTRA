# Human review packet: M4 PR 5, embedded/autopilot and the standalone embedded build

PR / candidate base and head: base `34eec2d` (`origin/main`, PR 4 #55's merge; re-fetched after the gate, unmoved).
Product and test candidate `56ea2ec` on branch `worktree-m4-pr5-autopilot`; the branch head is `3dc9ca7`, whose
commits after `56ea2ec` add evidence documents only. Commits:
- `05c4b4b` the plan; `297edca` the implementation with provisional gains; `49e54b3` frozen gains, the design test,
  and evidence;
- `56ea2ec` clang-tidy fixes in the new tests (the first gate on `49e54b3` failed there: a failed integration, 1 of 2);
- `3dc9ca7` evidence logs for `56ea2ec`.

The tree is clean at the head. **Not pushed and no PR opened** (stop before push, as instructed).

Spec/plan/task: [PR 5 plan](../plans/m4-pr5-autopilot.md), revision 2 **approved by Joe** (2026-09-26, with three text
edits applied). It sits under the [M4 plan](../plans/m4-lockstep.md) PR 5 row, Q10, Q12, Q13, Q16, A5, and follows
ADR-0006, ADR-0008, ADR-0022, ADR-0024.

Risk: Tier 2 (a new component; build/CI behavior). No `core/`, `icd/`, golden, tolerance, or ADR file changed.

Decision requested: approve behavior for this PR, then Joe pushes as a draft PR and squash-merges. No release
requested.

## What changes and why

**Before:** `embedded/` held only the HAL, built in-tree by a direct `add_subdirectory(embedded/hal)`; nothing
flew the sim, and ADR-0008's "buildable standalone with only a C++17 compiler" was untested.

**After:**
- **`astra_autopilot`** (`embedded/autopilot/`, C++17): a straight-and-level autopilot that is a plain HAL client. It
  includes only `astra_hal.h` and the ICD and links only `icd/` and `embedded/hal/`. Four command channels, each an
  outer loop plus fixed inner-loop gains:

  | Channel | Outer loop | Inner feedback |
  |---|---|---|
  | elevator | altitude PID (D on measured `vertical_speed`) | `theta - theta_trim`, `q` |
  | aileron | heading PI to a bank command clamped at 0.35 rad | bank `phi`, roll rate `p` |
  | throttle | `tas` PI | none |
  | rudder | none | `r`, `beta` (no washout) |

  Outputs are clamped per surface at the 737 ranges (elevator +/-0.3 rad, aileron and rudder +/-0.35 rad, throttle
  0..1), with conditional-integration anti-windup. `mode_flags` is the constant `0x1`, `fault_flags` 0.
- **CLI:** `--segment`, `--handshake-timeout-ms`, `--peer-timeout-ms`, `--altitude-m`, `--airspeed-mps`,
  `--heading-rad`, `--elevator-trim`, `--throttle-trim`, `--theta-trim` (all required; the trims are
  `TrimResult.elevator_rad`, `.throttle`, `.alpha_rad`, and `theta = alpha` at gamma = 0), plus the echo peer's
  `--notify-fd` with `--stop-after N` or `--abort-after N`. A bad command line exits 1 before the HAL is touched; a
  trim outside the output limits is one. Exit status is `hal_exit_status()` of the last HAL status, unchanged
  (CLOSED 0, timeout 2, handshake timeout 3, bad layout 4, protocol 5, error 6). On every exit after parsing it prints
  `autopilot: handled <n> held <m> status <s>`.
- **`dt` and invalid input:** the only use of `dt` is the integrators, `hal_now() - t_prev`, no clock in the autopilot.
  A frame with a non-finite read field or `WIND_INVALID` repeats the previous command (the trim on frame 0) and
  changes no loop state; `t_prev` still advances, so recovery integrates one period. `ATMOSPHERE_INVALID`,
  `CAS_SUPERSONIC`, `ACCEL_INVALID`, `LATLON_NOT_MODELED` are ignored on purpose (they zero no field a loop reads).
- **Standalone build:** `embedded/CMakeLists.txt` (its own `project()`, `cmake_minimum_required(3.21)`) replaces the
  top-level HAL line. `cmake/AstraBuildFlags.cmake` is now the one definition of `astra_build_flags`, included by the
  top level (moved verbatim) and by the standalone build, which also `add_subdirectory`s the same `icd/`.
  `just embedded-standalone` builds with no preset, once with clang++-18 and once with g++, checks that the
  configure named no Eigen/yaml-cpp/GTest/Python, runs the no-`astra_core` binary check, and smoke-runs the
  autopilot (no arguments: exit 1). It is in `just check` right after `docs-check`.
- **Tests:** `Autopilot*` (control law and CLI on scripted frames), `AutopilotProcess*` (the real binary against
  PR 4's `Creator`: replies equal the library over 200 scripted frames, every exit status, the hooks), and
  `EmbeddedBoundary_*` (no `libastra_core` NEEDED and no undefined `astra::` symbol; no clock, sleep or `<chrono>`
  symbol in the autopilot's own objects; include closure inside `embedded/` and `icd/`).
- **Gain design (Q13 (a)):** `tests/python/autopilot_design.py` builds PR 8's 5x5 altitude augmentation and the
  lateral block, reads the gains from `embedded/autopilot/gains.hpp` (strictly), and
  `test_autopilot_design.py` asserts stability, damping, margins, the sampled loop, and the MIL-F-8785C lower bounds.
  `astra.linearize` and PR 8's test are untouched.

**Diff summary (excluding evidence logs):** 24 files, 2714 insertions, 20 deletions. The 20 deletions are the top-level
flags block moved to `cmake/`, the old HAL line, and the HAL CMake header comment.

**End-to-end path:** `astra_autopilot` -> `hal_init` -> `hal_wait_frame` -> `Autopilot::update(in, hal_now(), out)`
-> `hal_send`; PR 6's driver will spawn it, pass the trim and setpoints, and read its exit status. Nothing in `core/`
knows its path.

## Evidence

Toolchain: clang-18 (presets), GCC 13.3 (standalone recipe and C11 checks), CMake 3.28+, Python 3.12 venv.
CI is local-gate mode; green CI is not a full-gate result.

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| `./scripts/check-full.sh` | exit 0, `check: OK`. debug 553, release 554, static-plugins 553, asan 553 tests passed; hooks 144/144; pyright 0 errors; pytest 130 passed | [`56ea2ec-full-gate.log`](evidence/56ea2ec-full-gate.log), candidate `56ea2ec`, clean tree |
| First gate on `49e54b3` | **failed** at clang-tidy on 5 diagnostics in the new test files (after all four preset test runs passed); fixed in `56ea2ec` (a failed integration, 1 of 2) | [`49e54b3-full-gate-failure-tidy.log`](evidence/49e54b3-full-gate-failure-tidy.log) |
| A5 `just embedded-standalone` | exit 0, `embedded-standalone: OK` (clang++-18 and g++, full warning set, `-Werror`) | [`56ea2ec-a5-standalone.log`](evidence/56ea2ec-a5-standalone.log) |
| A5 `ctest --preset asan -R '(Autopilot\|EmbeddedBoundary)' --no-tests=error` | exit 0, 43 of 43 passed (33 unit-label, 10 lockstep-label) | [`56ea2ec-a5-ctest.log`](evidence/56ea2ec-a5-ctest.log) |
| A5 `.venv/bin/pytest tests/python/test_autopilot_design.py` | exit 0, 14 passed | [`56ea2ec-a5-pytest.log`](evidence/56ea2ec-a5-pytest.log) |
| PR 4 lockstep tests, PR 3 tests, M3 regression, PR 8 phugoid test | pass **unmodified** inside the full gate (none of those files is in the diff) | same full-gate log |
| In-tree build unchanged by the CMake refactor | the first rebuild after moving the flags block reported `ninja: no work to do`, i.e. identical compile commands | this session; see plan implementation record |
| Mutation checks | 15 of 15 caught by a named test (dt, signs, clamps, anti-windup, hold mask, held-frame time, heading wrap, bank clamp, theta trim, `mode_flags`, exit status, a clock read, an `astra_core` include, linking `astra_core`) | [`56ea2ec-a5-mutations.md`](evidence/56ea2ec-a5-mutations.md) |
| Gain design | search, `kHeadingP` sweep, pole/margin results | [`m4-pr5-gain-design.md`](evidence/m4-pr5-gain-design.md) |

**Numerical results on the shipped gains (linear design model, not the acceptance oracle):**
- Open-loop 5x5 phugoid 108.5343 s, zeta 0.02730 (PR 8's recorded 108.53 s, 0.0273).
- Longitudinal: largest real part -0.0497 1/s, minimum damping 0.457; lateral: -0.0499, Dutch roll wn 1.693 rad/s,
  zeta 0.4016, zeta*wn 0.680. Thresholds: -0.03 and 0.30 (provisional, yours).
- Loop-at-a-time phase margin: elevator 92.7, throttle 69.6, aileron 111.0, rudder 93.4 degrees; gain margin
  unlimited upward except rudder +17.2 dB (threshold 45 degrees, 6 dB).
- Sampled closed loop, zero-order hold at 10 ms and no extra delay: max |z| 0.999498 (longitudinal), 0.999501
  (lateral); with one extra exchange of delay (conservative, reported only) 0.999499 and 0.999502.
- Reference sources actually read: MIL-F-8785C pages 12, 13, 22 as page images (Category B Level 1: phugoid zeta
  >= 0.04; short period 0.30 to 2.00; Dutch roll zeta >= 0.08, zeta*wn >= 0.15, wn >= 0.4); MIL-STD-1797A Appendix A
  for the 45 degree / 6 dB pairing (a bandwidth definition, hence provisional); 737.xml lines 204-205 for the
  elevator range.

**Coverage limits, disclosed:**
- The design model has no saturation and no turbulence. Whether the loops hold the M4 bands is PR 6's nonlinear R2.
- Margins are loop-at-a-time, not simultaneous multi-loop; they are meaningful only for a loop that is stable when
  closed (a sign-flipped elevator loop is unstable yet reports large margins), so the test asserts stability beside
  them.
- The C++ law and the Python model are two transcriptions of one written law (the `gains.hpp` header comment). The
  C++ unit tests pin each channel's signs and closed-form values; nothing numerically links the C++ to the Python
  model other than the shared gains file and those tests.
- LeakSanitizer cannot run under ptrace (lesson 2026-09-17); the asan test binaries pass their assertions but exit 1
  when run under a tracer (Codex hit this). The gate itself ran clean.
- Not exercised: a real turbulence run, a real sim process (the process tests use a test-side sim), long runs.

## Independent review

Claude built; Codex reviewed (companion 1.0.6, thread `01a0dd3b-7c1c-7152-93bd-e12a474d6174`).
- **Plan critique** (fresh, read-only, on revision 2): four findings, all verified in the repo and accepted and fixed
  in the plan before any code (out-of-range trim, held-frame `dt`, hold mask and the `WIND_INVALID` rationale, the
  clock scan missing `main.o`). Recorded in the plan's critique section.
- **Candidate review** (Tier 2, one pass, `review --base origin/main`, the committed branch diff `34eec2d..3dc9ca7`):
  **no actionable regressions.** It reported the ASan binary's LeakSanitizer/ptrace exit and that it did not run a
  fresh build or the full gate (the gate is ours). Log: [`3dc9ca7-codex-review.log`](evidence/3dc9ca7-codex-review.log).
- Correction rounds 0 of 2; failed integrations 1 of 2 (the tidy failure above).
- Remaining findings: none critical, major, or minor. Rejected or accepted-risk findings: none.

## Joe's attention

**Read first:** `embedded/autopilot/autopilot.cpp` (the law, the hold mask, the time bookkeeping),
`pid.hpp` (the anti-windup rule), `cli.cpp` (the argument rules), `main.cpp` (exit-status pass-through, hooks),
`embedded/CMakeLists.txt` and `cmake/AstraBuildFlags.cmake` (the build seam), `justfile` (`embedded-standalone`),
`tests/python/test_autopilot_design.py` (thresholds and their sources).

**Decisions and assumptions to confirm:**
1. **Gains are a build output, frozen at three figures** (`gains.hpp`), from a search against stricter targets than
   the test asserts. `kHeadingP = 2.0` is the smallest tried value meeting them, not a search bound (sweep in the
   evidence). A different design is a gains-file change plus a rerun of the design test.
2. **Provisional thresholds you accepted:** 45 degrees / 6 dB, damping 0.30, real part -0.03 1/s. The conventional
   source (MIL-F-9490D) is not in `docs/refs`.
3. **The yaw damper has no washout** (wings-level flight has `r = 0`, `beta = 0`); a steady turn would need one and is
   out of scope.
4. **Hold-on-invalid** repeats the previous command with no state change; the held reply always satisfies the output
   limits because the trim is rejected if it does not.
5. **`--airspeed-mps` and the other setpoints are checked finite only**: a zero or negative airspeed setpoint is not
   rejected (plan D2 said finite; disclosed in the plan's implementation record).
6. **Standalone recipe uses g++ as well as clang++-18.** Both pass the full warning set today, so no clang-only
   fallback was needed.
7. **Nested `cmake_minimum_required(3.21)`** applies to the in-tree `embedded/` directory too; the in-tree rebuild was a
   no-op, and the full gate passed on all four presets.
8. The process tests reuse PR 4's `Child` through its `ASTRA_ECHO_PEER_PATH` define pointed at the autopilot (a naming
   quirk noted in the test); PR 4's files are untouched.

**Rollback:** revert the PR. Shared-file edits are the top-level `CMakeLists.txt`, `justfile`, `tests/CMakeLists.txt`,
`embedded/hal/CMakeLists.txt` (comment only), and `docs/process/validation.md`; the new files are self-contained.

**PR 6 handoff (recorded here for the next task):**
- The autopilot CLI above, with `--theta-trim` = `TrimResult.alpha_rad`.
- The sim's `transport.limits` must **equal** the autopilot's limits (elevator +/-0.3, aileron +/-0.35, rudder
  +/-0.35, throttle [0, 1]); equal limits raise no false halt because the sim halts only on `v < lo` or `v > hi`,
  while a broken clamp still halts. PR 6 owns a test that `limits.hpp` equals the config's limits.
- The frame-0 reply equals trim only with the measurement at its setpoint: under Dryden, frame 0 already carries a
  gust (lesson 2026-09-22), so PR 6 must not assert an exactly-trim first reply in turbulence runs.
- R2c's constant-trim run is `astra_echo_peer --mode hold --commands <elev>,0,0,<thr>` (the autopilot has no zero-gains
  mode); R3's jitter is injected by the driver (SIGSTOP/SIGCONT or similar); the autopilot has no timing hook.
- **Throttle activity from `tas` gusts is to be inspected in the traces**, and **`kHeadingP` and the bank limit are the
  first knobs if heading misses under moderate turbulence.**
- Exit statuses (D6) and the exit-summary line are the driver's independent counters (R1).

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: the design model is deliberately conservative (thresholds above the specification
lower bounds) and deliberately not an oracle; the trade is that closed-loop performance under turbulence is proven only
in PR 6, in exchange for shipping a boundary-clean, fully tested component now.
Accepted risks: the loops may need retuning under moderate turbulence (owner: PR 6, first knobs `kHeadingP` and the
bank limit); the Python model and the C++ law are separate transcriptions (owner: this PR's tests pin the C++; a
numerical cross-link could be added if PR 6 finds a mismatch).
Nonblocking follow-ups: none tracked beyond the PR 6 handoff above.
Architectural decisions: none (ADR-0008 followed; no new ADR).
