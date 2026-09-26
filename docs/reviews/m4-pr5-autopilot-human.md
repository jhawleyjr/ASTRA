# Human review packet: M4 PR 5, embedded/autopilot and the standalone embedded build

PR / candidate base and head: base `34eec2d` (`origin/main`, PR 4 #55's merge; re-fetched after the round-1 gate, unmoved).
**Correction round 1 candidate `fc22cc8`** (product and tests) on branch `worktree-m4-pr5-autopilot`; the head is
`a0eec8c`, whose commits after `fc22cc8` add evidence and review records only. Round 0 candidate `56ea2ec` was the one
Joe reviewed ("not approved yet", correction round 1 of 2, findings F1 and F2 plus one minor point). Commits since
`origin/main`:
- `05c4b4b` plan; `297edca` implementation; `49e54b3` frozen gains and design test; `56ea2ec` tidy fixes (round 0
  candidate; the first gate on `49e54b3` failed at clang-tidy, a failed integration, 1 of 2); `2db8989`, `302adba`
  the round-0 packet and STATE;
- `fc22cc8` **round 1 fix**; `a0eec8c` round 1 evidence and the findings record.

The tree is clean at the head. **Not pushed and no PR opened** (stop before push, as instructed).

Spec/plan/task: [PR 5 plan](../plans/m4-pr5-autopilot.md), revision 2 **approved by Joe** (2026-09-26, with three text
edits applied). It sits under the [M4 plan](../plans/m4-lockstep.md) PR 5 row, Q10, Q12, Q13, Q16, A5, and follows
ADR-0006, ADR-0008, ADR-0022, ADR-0024.

Risk: Tier 2 (a new component; build/CI behavior). No `core/`, `icd/`, golden, tolerance, or ADR file changed.

Decision requested: re-review round 1 (the fix delta `56ea2ec..fc22cc8`), and if satisfied approve behavior for this
PR; then Joe pushes as a draft PR and squash-merges. No release requested.

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
  | rudder | none | `r` only: a pure yaw-rate damper (no sideslip term, no washout) |

  Outputs are clamped per surface at the 737 ranges (elevator +/-0.3 rad, aileron and rudder +/-0.35 rad, throttle
  0..1), with conditional-integration anti-windup. `mode_flags` is the constant `0x1`, `fault_flags` 0.
- **CLI:** `--segment`, `--handshake-timeout-ms`, `--peer-timeout-ms`, `--altitude-m`, `--airspeed-mps`,
  `--heading-rad`, `--elevator-trim`, `--throttle-trim`, `--theta-trim` (all required; `--airspeed-mps` must be positive; the trims are
  `TrimResult.elevator_rad`, `.throttle`, `.alpha_rad`, and `theta = alpha` at gamma = 0), plus the echo peer's
  `--notify-fd` with `--stop-after N` or `--abort-after N`. A bad command line exits 1 before the HAL is touched; a
  trim outside the output limits is one. Exit status is `hal_exit_status()` of the last HAL status, unchanged
  (CLOSED 0, timeout 2, handshake timeout 3, bad layout 4, protocol 5, error 6). On every exit after parsing it prints
  `autopilot: handled <n> held <m> status <s>`.
- **`dt` and invalid input:** the only use of `dt` is the integrators, `hal_now() - t_prev`, no clock in the autopilot.
  A frame with a non-finite read field or `WIND_INVALID` repeats the previous command (the trim on frame 0) and
  changes no loop state; `t_prev` still advances, so recovery integrates one period. `ATMOSPHERE_INVALID`,
  `CAS_SUPERSONIC`, `ACCEL_INVALID`, `LATLON_NOT_MODELED` are ignored on purpose (they zero no field a loop reads);
  `beta` is no longer read at all.
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

**Diff summary (excluding the evidence directory, at `fc22cc8`):** 27 files, 3162 insertions, 45 deletions. The
deletions are the round-1 replacements (rudder law, gains, test), the top-level
flags block moved to `cmake/`, the old HAL line, and the HAL CMake header comment.

**End-to-end path:** `astra_autopilot` -> `hal_init` -> `hal_wait_frame` -> `Autopilot::update(in, hal_now(), out)`
-> `hal_send`; PR 6's driver will spawn it, pass the trim and setpoints, and read its exit status. Nothing in `core/`
knows its path.

## Correction round 1 (Joe's review of `56ea2ec`)

Findings recorded with IDs H-1, H-2, H-3 in [the review record](m4-pr5-autopilot-review.md), each verified by Codex.
- **H-1 (F1), the yaw damper had the destabilizing sign.** `cn_beta = +0.26` and `cn_rudder = -0.20`
  (`aero_math.hpp`, `cruise.yaml`); `rudder = kYawRate r + 0.269 beta` commanded nose-left rudder for `beta > 0`, so the
  effective `cn_beta` fell to 0.206, and the rudder's only finite upward gain margin (+17.2 dB) came from that sign. Joe
  was right. **Fix: the sideslip term was dropped** (a pure yaw-rate damper) because the search showed every floor and the
  new criterion are met without it, while a stiffening-sign term was pushed to its bound with nothing asking for it. The
  test now asserts the physical meaning through `cn_rudder`'s sign (the rudder's moment opposes `r`, and the moment times
  `beta` is never negative), and the design test asserts closed-loop effective `cn_beta` is not below open loop
  (`0.2062 >= 0.26` fails when the old term is put back). Mutation: the destabilizing beta term is caught.
- **H-2 (F2), the lateral inner loop was as slow as the outer one.** `kBankP = 0.055`: bank loop alone slowest pole
  -0.153 (time constant 6.5 s), heading-loop poles |lambda| 0.051, ratio 2.99. **Fix: lateral gains re-searched** with no
  small-gain preference on the inner loop, the criterion asserted in the design test (bank loop alone every pole at
  Re <= -0.5, i.e. time constant <= 2 s, and bandwidth >= 3x the heading loop's), and `kBankP <= 0.6` (a full-scale
  0.35 rad bank error asks for at most 60 percent of the aileron limit). Shipped: `kHeadingP` 2.5, `kHeadingI` 0.0816,
  `kBankP` 0.394, `kRollRate` 0.360, `kYawRate` 0.494. Bank loop poles (heading open) -1.342, -1.022, -0.866 +/- 1.712j
  (time constant **1.16 s**); heading-loop poles -0.0514 +/- 0.0305j; ratio **14.5**; aileron demand 39 percent of the limit.
  Pitch versus altitude/speed: short period 1.272 rad/s (time constant 1.7 s) against a fastest outer pole of 0.152 rad/s,
  ratio **8.4**, so the longitudinal gains are unchanged. `kHeadingP` = 2.5 is the smallest tried value (1.5, 2.0, 2.5,
  3.0, 4.0) meeting every target, not a search bound (2.0 misses at -0.043); the bank command's linear range is 8.0 degrees
  of heading error.
- **H-3, minor:** `--airspeed-mps <= 0` is rejected at parse time (exit 1); CLI and process tests added.
- The frozen gains, the design evidence, and the `kHeadingP` sweep are in [gain design](evidence/m4-pr5-gain-design.md).

## Evidence

Toolchain: clang-18 (presets), GCC 13.3 (standalone recipe and C11 checks), CMake 3.28+, Python 3.12 venv.
CI is local-gate mode; green CI is not a full-gate result.

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| `./scripts/check-full.sh` (round 1) | exit 0, `check: OK`. debug 555, release 556, static-plugins 555, asan 555 tests passed; hooks 144/144; pyright 0 errors; pytest 135 passed | [`fc22cc8-full-gate.log`](evidence/fc22cc8-full-gate.log), candidate `fc22cc8`, clean tree |
| A5 `just embedded-standalone` | exit 0, `embedded-standalone: OK` (clang++-18 and g++, full warning set, `-Werror`) | [`fc22cc8-a5-standalone.log`](evidence/fc22cc8-a5-standalone.log) |
| A5 `ctest --preset asan -R '(Autopilot\|EmbeddedBoundary)' --no-tests=error` | exit 0, 45 of 45 passed (35 unit-label, 10 lockstep-label) | [`fc22cc8-a5-ctest.log`](evidence/fc22cc8-a5-ctest.log) |
| A5 `.venv/bin/pytest tests/python/test_autopilot_design.py` | exit 0, 19 passed | [`fc22cc8-a5-pytest.log`](evidence/fc22cc8-a5-pytest.log) |
| PR 4 lockstep tests, PR 3 tests, M3 regression, PR 8 phugoid test | pass **unmodified** inside the full gate (none of those files is in the diff) | same full-gate log |
| Mutation checks (round 1 additions and re-run of round 0) | all caught by a named test: destabilizing beta term, stiffening beta term, yaw damper sign, beta read again, airspeed <= 0 accepted, `kSideslip = 0.269` in the gains file (caught by the effective-`cn_beta` test only), `kBankP` 0.055 and 0.9, `kHeadingP` 1.0, `kPitchRate` 0.02, `kYawRate` sign, and the 14 round-0 mutations | [`fc22cc8-a5-mutations.md`](evidence/fc22cc8-a5-mutations.md), [`56ea2ec-a5-mutations.md`](evidence/56ea2ec-a5-mutations.md) |
| Gain design | searches, `kHeadingP` sweeps, pole/margin results, round-0 recomputation | [`m4-pr5-gain-design.md`](evidence/m4-pr5-gain-design.md) |
| Round 0 gate and history | `56ea2ec` gate exit 0 `check: OK`; `49e54b3` failed clang-tidy | [`56ea2ec-full-gate.log`](evidence/56ea2ec-full-gate.log), [`49e54b3-full-gate-failure-tidy.log`](evidence/49e54b3-full-gate-failure-tidy.log) |

**Numerical results on the shipped gains (linear design model, not the acceptance oracle):**
- Open-loop 5x5 phugoid 108.5343 s, zeta 0.02730 (PR 8's recorded 108.53 s, 0.0273).
- Longitudinal: largest real part -0.0497 1/s, minimum damping 0.457; lateral: -0.0514, minimum damping 0.460, Dutch roll
  wn 1.904 rad/s, zeta 0.4603, zeta*wn 0.876. Thresholds: -0.03 and 0.30 (provisional, yours).
- Loop-at-a-time phase margin: elevator 92.7, throttle 69.6, aileron 93.9, rudder 106.8 degrees; every gain-margin
  direction unlimited (threshold 45 degrees, 6 dB).
- Inner-loop separation (round 1): bank loop alone time constant 1.16 s, bank/heading ratio 14.5; pitch short period
  1.272 rad/s, pitch/outer ratio 8.4; aileron demand at a full-scale bank error 39 percent of the limit.
- Effective `cn_beta` 0.26 (open loop 0.26): the shipped law has no sideslip term.
- Sampled closed loop, zero-order hold at 10 ms and no extra delay: max |z| 0.999498 (longitudinal), 0.999486 (lateral);
  with one extra exchange of delay (conservative, reported only) 0.999499 and 0.999486.
- Reference sources actually read: MIL-F-8785C pages 12, 13, 22 as page images (Category B Level 1: phugoid zeta
  >= 0.04; short period 0.30 to 2.00; Dutch roll zeta >= 0.08, zeta*wn >= 0.15, wn >= 0.4); MIL-STD-1797A Appendix A
  for the 45 degree / 6 dB pairing (a bandwidth definition, hence provisional); 737.xml lines 204-205 for the
  elevator range; `aero_math.hpp:146` and `cruise.yaml` for `cn_beta` and `cn_rudder`.

**Coverage limits, disclosed:**
- The design model has no saturation and no turbulence. Whether the loops hold the M4 bands is PR 6's nonlinear R2.
- The inner-loop "bandwidths" are modal separation measures (pole magnitudes), not measured frequency-response
  bandwidths (Codex's caveat on the round-1 verification).
- With no sideslip feedback, sideslip in lateral gusts is regulated only by the airframe's own weathervane; PR 6's runs
  decide whether a stiffening (`kSideslip <= 0`) term is needed.
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

Claude built; Codex reviewed (companion 1.0.6).
- **Plan critique** (fresh, read-only, on revision 2): four findings, all verified in the repo, accepted and fixed in the
  plan before any code. Recorded in the plan's critique section.
- **Round 0 candidate review** (Tier 2, one pass, `review --base origin/main`, `34eec2d..3dc9ca7`): no actionable
  regressions. [`3dc9ca7-codex-review.log`](evidence/3dc9ca7-codex-review.log). (Joe's human review then found H-1 to H-3,
  which that pass did not.)
- **Round 1 fix verification** (adversarial, fresh context, `--base 56ea2ec`, head `a0eec8c`, thread
  `01a0dee1-c13a-71d1-a3be-02b89dd1018f`): **approve**; H-1, H-2 and H-3 each verified fixed; **New findings: none.**
  Caveats it recorded: 0.26 and 0.2062 are native (AERORP) coefficients, not total CG derivatives (the moment transfer
  does not change the feedback increment, so the comparison stands); the separation ratios are modal measures.
  [`a0eec8c-codex-verify.log`](evidence/a0eec8c-codex-verify.log); findings and history in
  [`m4-pr5-autopilot-review.md`](m4-pr5-autopilot-review.md).
- Correction rounds **1 of 2**; failed integrations 1 of 2 (the round-0 tidy failure). Open critical/major: none.
  Rejected or accepted-risk findings: none.

## Joe's attention

**Read first:** `embedded/autopilot/autopilot.cpp` (the law, the hold mask, the time bookkeeping),
`pid.hpp` (the anti-windup rule), `cli.cpp` (the argument rules), `main.cpp` (exit-status pass-through, hooks),
`embedded/CMakeLists.txt` and `cmake/AstraBuildFlags.cmake` (the build seam), `justfile` (`embedded-standalone`),
`tests/python/test_autopilot_design.py` (thresholds and their sources).

**Decisions and assumptions to confirm:**
1. **Gains are a build output, frozen at three figures** (`gains.hpp`), from searches against stricter targets than
   the test asserts. After round 1 `kHeadingP = 2.5` is the smallest tried value meeting them, not a search bound (sweep
   in the evidence); `kBankP` 0.394 is interior to its 0.6 cap. A different design is a gains-file change plus a rerun of the design test.
2. **Provisional thresholds you accepted:** 45 degrees / 6 dB, damping 0.30, real part -0.03 1/s; and the round-1
   inner-loop criteria (bank loop alone time constant <= 2 s and bandwidth >= 3x the heading loop's; the same 3x for
   pitch; aileron headroom of at least 40 percent), which are ASTRA's own and equally provisional. The conventional
   source (MIL-F-9490D) is not in `docs/refs`.
3. **The yaw damper is a pure yaw-rate damper: no sideslip term, no washout.** Positive rudder is nose-left, a negative
   yaw moment (`cn_rudder = -0.20`), so `+K beta` weakens the weathervane (`cn_beta` 0.26 to 0.206 at K = 0.269, the
   round-0 defect); only `K <= 0` stiffens it. The term was dropped because the floors are met without it; a stiffening
   term is the first thing to add if PR 6's gust runs show large sideslip. Wings-level flight has `r = 0`, so no
   washout; a steady turn would need one and is out of scope.
4. **Hold-on-invalid** repeats the previous command with no state change; the held reply always satisfies the output
   limits because the trim is rejected if it does not.
5. **`--airspeed-mps` is checked finite and positive** (round 1); the other setpoints and the theta trim are finite only.
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
  first knobs if heading misses under moderate turbulence.** If sideslip is large in lateral gusts, add a stiffening
  `kSideslip <= 0` term (its sign is asserted by the design test).
- Exit statuses (D6) and the exit-summary line are the driver's independent counters (R1).

## Decision record

State: changes-required (round 0, Joe on `56ea2ec`), resubmitted with correction round 1 (`fc22cc8`); decision pending
Human decision/date/reference: round 0: Joe's review of `56ea2ec`, findings F1, F2 and the airspeed point (recorded as H-1
to H-3); round 1: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: the design model is deliberately conservative (thresholds above the specification lower
bounds) and deliberately not an oracle; the trade is that closed-loop performance under turbulence is proven only in
PR 6, in exchange for shipping a boundary-clean, fully tested component now. Dropping the sideslip term trades a
possible gust-sideslip benefit (unmeasured) for one fewer gain and no sign hazard.
Accepted risks: the loops may need retuning under moderate turbulence (owner: PR 6, first knobs `kHeadingP` and the bank
limit, and a stiffening sideslip term); the Python model and the C++ law are separate transcriptions (owner: this PR's
tests pin the C++, including the rudder sign through `cn_rudder`; a numerical cross-link could be added if PR 6 finds a
mismatch).
Nonblocking follow-ups: none tracked beyond the PR 6 handoff above.
Architectural decisions: none (ADR-0008 followed; no new ADR).
