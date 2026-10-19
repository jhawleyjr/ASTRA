# M7b PR 3: oracle (b), the port pattern, proven on M4's autopilot

Status: **approved by Joe, 2026-10-18, on his word**. He accepted decisions 1 to 8 as recommended, and his
amendments 1 to 7 ("Joe's amendments", below) are folded in before the plan's first commit. Nothing of PR 3 is
built or run.
- This file was moved (`mv`, before its first add) from the main checkout's plan-mode draft into the worktree as
  `docs/plans/m7b-pr3-autopilot-peer.md`. Nothing stays in the main checkout.
- Nothing of the peer or the variant executes before the Codex plan critique closes (§14, step 4).
- No control run starts before §9's registered S and precondition results are written in as literals (amendment
  1).

Owner: Joe. Builder: Claude, one builder.
Tier 3: the second in-house oracle under ADR-0034; a new shipped transport plugin under `aircraft/`; a bitwise port
criterion with pre-registered negative controls.
Base: `origin/main` = `106c0cb` (#123, PR 2 squash-merged), read in plan mode; `git fetch` confirms it after
approval. STATE.md's step 2 names PR 3.
- **The PR 2 worktree is already gone.** `git worktree list` shows only the main checkout,
  `.claude/worktrees/` is empty, no `worktree-*` branch exists locally, and `origin` holds only `main`. Step 1
  of §14 confirms this after the fetch (`git worktree prune --dry-run`) instead of removing anything.

Binding: ADR-0007, ADR-0008, ADR-0022, ADR-0024, ADR-0029 (Decisions 1, 2 and 8, and the Consequences'
expression-order rule), ADR-0034 (Decisions 2, 3, 4, 5 and 8).
Sources: [the M7b plan](m7b-closed-loop-oracles.md), as follows:
- oracle (b): the peer, the equivalence, the flags, the unit-level equivalence, and the four controls with why
  the earlier four were replaced;
- R5 to R9, PR table row 3, Q7 and Q8 as decided, amendment 2, and N-03.

[PR 2's plan](m7b-pr2-sampled-data.md) gives the pattern: §1 for the gate record, §5 for the flag check, §16 for
the critique record. M4's [PR 5](m4-pr5-autopilot.md) and [PR 6](m4-pr6-lockstep-acceptance.md) plans give the
law, the scenario, the bands and the trim.

## Context

M7b replaces the verification that M7 PR 11b, 12 and 15 would have drawn from the two-member NESC ensemble with
three in-house oracles. Oracle (b) proves the port pattern of ADR-0029 Decision 8 on a law that already has an
accepted port: M4's `embedded/autopilot`. The pattern is to write the law sim-side, prove it, port it, and
prove the two bitwise equal.
- A second implementation of M4's law, written sim-side as a C++23 in-process `Transport`, must be bitwise equal
  to `embedded/autopilot` on Decision 8's objects.
- The comparison flies M4's acceptance scenario at both Dryden levels.
- Four ulp-sized planted porting defects must each break that equality first at a pre-registered exchange, while
  staying inside M4's ±30 m and ±2° bands. That shows the criterion sees a defect no band can see.

ADR-0034's Consequences list what this plan fixes before the first comparison: the reference and its
independence, the shared inputs and their blind spots, the bound (bitwise) on every named object, the comparison
set, and each control's named object.
- A difference on the nominal comparison is a porting defect until shown otherwise. A stated floating-point
  cause goes to Joe.
- A control that does not break equality, or that first breaks anywhere other than its registered record, stops
  the PR and goes to Joe.
- Nothing registered here moves after the first comparison without Joe (ADR-0034 Decisions 3 and 7).

## Joe's amendments (2026-10-18)

Joe approved the plan on his word, and accepted decisions 1 to 8 below as recommended. He made seven
amendments, each folded into the section named:
1. **Decision 1 is tightened** (§9, §14).
   - The replay's S is computed from the shm reference run, which ADR-0034 Decision 3 allows before the first
     run. It is written into §9's table as the registered set, dated, before any control run executes.
   - The variant's control runs are the first run of that path, and they start only once S is a literal in the
     plan.
   - The same holds for controls 1 and 3's precondition results, which are recorded in §9 before the control
     runs: request 0's q = 0.0, and the two elevator inequalities.
2. **Decision 5's hooks** (§2, §3).
   - The shipped translation unit instantiates only `BasicAutopilotPeer<Law>` and `Gains{}`, and no other
     instantiation is reachable from the shipped `.so`.
   - `nm -C` on the shipped `.so` shows no defect-law or `OutputFirstPid` symbol.
   - `ConstantsArePinnedToTheAutopilots` keeps the `Gains{}` check.
3. **§5's flag set gains `-fveclib=*` and `-mrecip*`.** Both are off by default and absent from the expected
   sets, so this only widens what a hit catches.
4. **§9's bands.** The nominal run's altitude and heading maxima at both levels are reported beside each
   control's, from the same recorder states. They are context and not registered, like M4's margins. The
   controls' band pass stays registered as written.
5. **§6, O11.** `git_describe` equality stays the one-build check. The test's failure message says that a
   `-dirty` mismatch means the tree changed between runs (lesson 2026-10-08). So a whole-log difference is read
   first as that attribute, not as the port.
6. **Decision 7 is accepted** (§1). The PR body states the two configures' wall time as part of the pytest cost,
   so the M7b table's "about 10 s" is corrected with its cause named.
7. **Everything else stands as written:**
   - decisions 2, 3, 4, 6 and 8;
   - the comparison set, the objects, the exclusions and the four controls;
   - the precondition order: usable, then reproduce the reference, then the defective expressions;
   - §14's sequence.

   Nothing of the peer or the variant executes before the Codex critique closes. Joe confirmed that the PR 2
   worktree and branch are already gone, so §14's step 1 is confirm-only.

## Decisions (accepted by Joe, 2026-10-18, as recommended)

1. **Control 2's registered object is a field set fixed by the exact replay** (§9). At exchange 1 all three
   PIDs integrate for the first time, so the elevator and the aileron can differ as well as the throttle. The
   M7b plan's row names only `throttle`.
   - Registered: at exchange 1, the differing reply fields are exactly the set S that N-03's exact replay
     computes, and S must contain `throttle`.
   - Predicted: S = {elevator, aileron, throttle}.
   - The alternative, "throttle only", would stop the PR on the elevator's expected difference.
2. **Control 4's registration is the whole run**, not only exchange 0 (§9). `mode_flags` differs at every
   exchange, and every other field of all three streams is bitwise equal at every frame and exchange.
3. **Each replay first reproduces the reference** (§9). Before the defective expressions are trusted, the
   replay's shipped expressions must give the shm run's recorded replies at exchanges 0 and 1 bitwise. This plan
   adds that check.
4. **Objects beyond Decision 8's**, which ADR-0034 Decision 8 allows (§6):
   - the root attributes, `/summary` and `/writes`;
   - `transport.fault`;
   - a determinism repeat;
   - a floating-point environment guard in each sim process;
   - a compiler-target check;
   - include and symbol independence checks.
5. **The shipped law carries three hooks, and only the variant uses them** (§2). They are a `Gains` constructor
   argument, defaulted to the law's named constants, and the template parameters `PidType` (of the law) and
   `LawType` (of the peer).
   - Each control's defect is written only in `tests/plugins/autopilot_peer_variant` (ADR-0034 Decision 5).
   - The precedent is `nesc_control`'s `Options` (`nesc_control_peer.hpp:72-79`).
   - The shipped plugin instantiates only the defaults.
6. **Test placement** (§2).
   - The two C++ test executables go in a new `tests/autopilot_peer/`, added from `tests/CMakeLists.txt`. That is
     the brief's "under tests/" and "tests/ CMake".
   - pytest imports `Host`, `Reaper` and `_executable` from `test_lockstep.py`, after
     `test_plot_new_aircraft.py`'s precedent of importing from a sibling test module. `test_lockstep.py` itself
     is unchanged.
   - `lockstep_child.py` gains additive options only.
7. **The embedded-standalone flags are read from a test-side configure** (§5). The recipe's build directories
   have no `compile_commands.json`, and the justfile is outside this PR's paths.
   - The test configures `embedded/` into `tmp_path` with the recipe's own arguments, read from the justfile,
     plus `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
   - It does this for both compilers the recipe uses.
8. **Gate cost** (§1). pytest is estimated at about 25 s, against the M7b table's "about 10 s". The time is
   measured and reported against 1162 s, 1178 s and 1001 s. No comparison-set change is made for speed.

## 1. The gate record (Q3): no new base measurement

PR 2 measured the base: 1162 s on `406c407` (its §1). PR 2's own gate took 1178 s on `0bbc4fa`
(`docs/reviews/evidence/0bbc4fa-gate-summary.log`). PR 3 measures only its own full gate, after implementation
and the first run, in this form:
1. **Load check** (lessons 2026-10-02 and 2026-10-04):
   - run `pgrep -af '^just (check|test|build)'`, `pgrep -af 'python[0-9.]* -m [p]ytest'` and
     `pgrep -af 'bin/[p]ytest'`;
   - exclude the check's own shell and its ancestors, and `readlink /proc/<pid>/cwd` for each remaining hit;
   - ignore the other project's `just autopilot` dispatcher;
   - `ps -C clangd -o pid,pcpu,etime` must show clangd idle;
   - record `/proc/loadavg` and `nproc` before and after.
2. `just venv` if the worktree has no `.venv`. This is untimed, and the record says whether it ran.
3. `rm -rf build && ./scripts/check-full.sh` from the clean worktree at the gated head, through the Bash tool's
   `run_in_background`, timed. This is like-with-like with 1162 s, 1178 s and #75's 1001 s. It passes only on
   exit 0 plus its `check: OK` line.
4. Record the result here and in the PR body:

| Measured | Value |
|---|---|
| Timed command, and the checkout it ran in | (after the run) |
| Whether `just venv` ran first (untimed) | |
| Exit status and `check: OK` | |
| Wall time; delta against 1162 s (`406c407`), 1178 s (`0bbc4fa`) and 1001 s | |
| Load before and after; `nproc`; other builds | |
| Per-preset CTest and pytest times, against PR 2's gate (debug 109.75 s, release 11.94 s, static-plugins 241.50 s, asan 422.39 s; pytest 141.99 s) | |
| PR 3's own tests: `ctest -R AutopilotPeer` per preset, and `pytest -k autopilot_peer --durations=0` | |
| The two standalone configures' wall time inside pytest (§5; amendment 6) | |

The PR body states the pytest cost against the M7b table's "about 10 s" and names its causes (amendment 6):
- the twelve lockstep-length runs;
- the four logs;
- the two standalone configures.

The full log stays outside git. Its trimmed summary is committed as
`docs/reviews/evidence/<head>-gate-summary.log`.

## 2. What is built

Code goes only where the brief names:
- `aircraft/notional_transport/sim/transport/autopilot`, with its key block;
- the two tracked configs and their drift test;
- `docs/reference/config-keys.md`, regenerated;
- `tests/plugins/autopilot_peer_variant`;
- `tests/autopilot_peer`;
- `tests/python`;
- `tests/` CMake.

There is no `core/`, `icd/`, `embedded/`, golden or tolerance change, and `kCoreApiVersion` stays 11.

| File | Content |
|---|---|
| `aircraft/notional_transport/sim/transport/autopilot/CMakeLists.txt` | `astra_add_plugin(SOURCES autopilot.cpp)` |
| `…/autopilot/autopilot.cpp` | The descriptor (`sim`, `transport_autopilot`, `kCoreApiVersion`) and the key block (§10) |
| `…/autopilot/autopilot_law.hpp` | The law, C++23. It includes only standard headers and `icd/astra_icd.h`: no core header, no `embedded/` header |
| `…/autopilot/autopilot_peer.hpp` | The `Transport`, `BasicAutopilotPeer<LawType>`, with `using AutopilotPeer = BasicAutopilotPeer<Law>` |
| `aircraft/notional_transport/configs/cruise_{light,moderate}_turbulence_autopilot_peer.yaml` | The two tracked configs (§10) |
| `tests/plugins/autopilot_peer_variant/{CMakeLists.txt, autopilot_peer_variant.cpp}` | The four controls (§9) |
| `tests/autopilot_peer/CMakeLists.txt` | Two executables, registered with `gtest_discover_tests` |
| `tests/autopilot_peer/autopilot_peer_equivalence_test.cpp` | R5. It links `astra_autopilot_lib` and includes `autopilot_law.hpp`; no `astra_core` |
| `tests/autopilot_peer/autopilot_peer_test.cpp` | The `Transport` against the autopilot, the config refusals, and the sanitizer and zero-allocation smoke. It links `astra_core` and `astra_autopilot_lib` (`astra_add_plugin_test`) |
| `tests/CMakeLists.txt` | `add_subdirectory(autopilot_peer)` |
| `tests/python/test_autopilot_peer.py` | R6 to R9, Q8, the flags, and independence |
| `tests/python/lockstep_child.py` | Additive only: `run` gains `--trim config` and `--log`; a new scenario `inprocess` |

**The law** (`autopilot_law.hpp`, namespace `astra::aircraft_notional_transport_autopilot`) is a line-for-line
transcription of `embedded/autopilot/autopilot.cpp:14-85` and `pid.hpp:12-59`, in that text's expression order
(ADR-0029's Consequences). Its constants are its own:
- the twelve gains, as `inline constexpr double kAltP = 1.54e-4;` and so on, with the same literal text as
  `gains.hpp:35-46`;
- the five `Range`s, `kModeFlags` and `kMaxIntegrationDt` (`limits.hpp:23-38`);
- `kPi = 3.14159265358979323846` and `kTwoPi = 2.0 * kPi` (`autopilot.cpp:14-15`).

`Setpoints`, `Trims`, `PidGains`, `Range` and `Pid` (with `update`, `clamp`, `inside` and `distance`) are as in
`pid.hpp`. One struct is new:

```cpp
// The gains a law runs with. The shipped peer uses only the defaults (the constants above);
// tests/plugins/autopilot_peer_variant's float_pitch_rate control is the only other user.
struct Gains {
  double alt_p = kAltP, alt_i = kAltI, alt_d = kAltD, pitch_theta = kPitchTheta,
         pitch_rate = kPitchRate, speed_p = kSpeedP, speed_i = kSpeedI, heading_p = kHeadingP,
         heading_i = kHeadingI, bank_p = kBankP, roll_rate = kRollRate, yaw_rate = kYawRate;
};
```

The law's update, registered here because §9's replay and the review compare it with `autopilot.cpp:40-85`:

```cpp
template <class PidType = Pid>
class BasicLaw {
 public:
  BasicLaw(const Setpoints& setpoints, const Trims& trims, const Gains& gains = Gains{})
      : setpoints_(setpoints), trims_(trims), gains_(gains),
        altitude_(PidGains{gains.alt_p, gains.alt_i, gains.alt_d}),
        speed_(PidGains{gains.speed_p, gains.speed_i, 0.0}),
        heading_(PidGains{gains.heading_p, gains.heading_i, 0.0}),
        last_elevator_(trims.elevator_rad), last_throttle_(trims.throttle) {}

  void update(const astra_sim_to_sw_v1& in, double now, astra_sw_to_sim_v1& out) noexcept {
    std::memset(&out, 0, sizeof out);
    out.magic = ASTRA_ICD_MAGIC; out.version = ASTRA_ICD_VERSION; out.frame = in.frame;
    out.mode_flags = kModeFlags;
    double dt = have_previous_ ? now - previous_now_ : 0.0;
    previous_now_ = now;
    have_previous_ = true;
    if (!std::isfinite(dt) || dt <= 0.0 || dt > kMaxIntegrationDt) { dt = 0.0; }
    ++handled_;
    if (usable(in)) {  // autopilot.cpp:28-38: WIND_INVALID, then nine read fields finite, by std::all_of
      const double elevator_offset = trims_.elevator_rad +
          gains_.pitch_theta * (in.theta - trims_.theta_rad) + gains_.pitch_rate * in.q;
      last_elevator_ = altitude_.update(setpoints_.altitude_m - in.altitude, -in.vertical_speed, dt,
                                        elevator_offset, -1.0, kElevatorLimit);
      last_throttle_ = speed_.update(setpoints_.airspeed_mps - in.tas, 0.0, dt, trims_.throttle, 1.0,
                                     kThrottleLimit);
      const double bank_command = heading_.update(wrap_pi(setpoints_.heading_rad - in.psi), 0.0, dt,
                                                  0.0, 1.0, kBankCommandLimit);
      last_aileron_ = Pid::clamp(gains_.bank_p * (bank_command - in.phi) - gains_.roll_rate * in.p,
                                 kAileronLimit);
      last_rudder_ = Pid::clamp(gains_.yaw_rate * in.r, kRudderLimit);
    } else {
      ++held_;
    }
    out.elevator = last_elevator_; out.aileron = last_aileron_; out.rudder = last_rudder_;
    out.throttle[0] = last_throttle_; out.throttle[1] = last_throttle_;
  }
  // handled(), held(), usable(in), and test accessors: the three integrals and the last bank command.
};
using Law = BasicLaw<>;
```

- `wrap_pi(x)` is `std::remainder(x, kTwoPi)`.
- `Pid::update` is `pid.hpp:28-41` verbatim.
- The only differences from the port's text are these. Each multiplies by the same double, in the same position:
  - the namespace;
  - `noexcept`;
  - `gains_.x` in place of the constexpr `kX`;
  - the constructor's `PidGains` built from `gains`.

**The peer** (`autopilot_peer.hpp`) has `nesc_control`'s shape (ADR-0029 Decision 1): in-process, no clock, no
allocation per frame, and no write handle.
- `init` reads `transport.peer.setpoints.{altitude, airspeed, heading}` and
  `transport.peer.trim.{elevator, throttle, theta}` with `Config::number`.
  - It applies `cli.cpp:121-133`'s rule to each key: finite; airspeed above 0; elevator in ±0.3; throttle in
    [0, 1].
  - A refusal is a `ConfigError` naming the key.
  - It then constructs `LawType{setpoints, trims}` in a `std::optional`, which does not allocate.
- `publish(request, seq)` calls `law_->update(request, request.t, reply_)`. The HAL's `now` is the request's `t`
  (`hal_shm.cpp:198`, `:229`; `main.cpp:59`), so the peer passes `request.t`.
  - It records `seq`, and copies `handled()` and `held()` into its signals.
  - The reply is zeroed first, inside `update`, as the port does (ADR-0022 Determinism).
- `open` returns the sentinel 0xFFFFFFFF (ADR-0024). `wait_ack` returns the pending `seq`, and `read_reply`
  copies the reply. `icd_version` is 1, and `name()` is "autopilot". `reset()` rebuilds the law.
- Signals, all `transport.*` and so excluded from §6's column comparison by construction:
  - `transport.peer.handled` and `transport.peer.held` (unit 1);
  - `transport.peer.{altitude_setpoint (m), airspeed_setpoint (m/s), heading_setpoint (rad),
    elevator_trim (rad), throttle_trim (1), theta_trim (rad)}`, the parsed parameters that §7 reads.

**The variant** (`tests/plugins/autopilot_peer_variant`, descriptor `test`, `autopilot_peer_variant`). It
includes only the shipped `autopilot_peer.hpp`, and never links or loads the shipped `.so`.
- It reads `transport.peer.test_defect`.
- At `init` it builds the matching `BasicAutopilotPeer<DefectLaw>`, the only allocation, at init, and forwards
  every `Transport` call to it.
- An unknown name is a `ConfigError` naming `transport.peer.test_defect`.
- `tests/plugins` is skipped by `config_keys.py`, so the variant needs no key block.

**What the shipped plugin contains** (amendment 2):
- The shipped translation unit, `autopilot.cpp`, instantiates only `BasicAutopilotPeer<Law>`, that is
  `BasicLaw<Pid>` with `Gains{}`. No other instantiation is reachable from the shipped `.so`.
- The defect types exist only in the variant's translation unit, in its anonymous namespace.
- §3's `nm -C` check asserts that the shipped `.so` names none of `FloatElevatorTrimLaw`, `OutputFirstPid`,
  `FloatPitchRateLaw` or `ModeWordZeroLaw`.
- `ConstantsArePinnedToTheAutopilots` (§8) pins `Gains{}` to the named constants.

Each defect law is one change, written only in the variant:

| `test_defect` | Defect law | The change |
|---|---|---|
| `float_elevator_trim` | `FloatElevatorTrimLaw : public Law` | Its constructor passes `Trims{static_cast<double>(static_cast<float>(t.elevator_rad)), t.throttle, t.theta_rad}` |
| `output_before_integral` | `BasicLaw<OutputFirstPid>` | `OutputFirstPid` is `pid.hpp`'s `Pid` with one slip: it takes `clamp(before, range)` as the output before the `dt > 0` integral update, and returns that |
| `float_pitch_rate` | `FloatPitchRateLaw : public Law` | Its constructor passes `Gains` with `pitch_rate = static_cast<double>(static_cast<float>(kPitchRate))` |
| `mode_word_zero` | `ModeWordZeroLaw : public Law` | Its `update` calls `Law::update`, then sets `out.mode_flags = 0` |

## 3. The reference, and its independence (ADR-0034 Decision 2)

- **The reference** is M4's accepted `embedded/autopilot`: `astra_autopilot` from `build/python`'s tree, which
  M4's own tests run (`test_lockstep.py:229-236`).
  - It runs over the shm transport through `astra.lockstep.lockstep_session`, in a `lockstep_child.py`
    process.
  - This is the place for every lockstep run in pytest (`lockstep_child.py:1-13`).
- **The path under test** is the shipped peer, run in-process through `astra._core.Simulation` in a
  `lockstep_child.py inprocess` process.
- **Independence:**
  - **Source.** The peer is C++23 under `aircraft/`, and the port is C++17 under `embedded/`. The peer is
    written from the port's text. Neither includes the other.
    - pytest reruns the peer's and the variant's compile lines from `build/python/compile_commands.json` with
      `-MM`.
    - It asserts that no dependency's real path lies under `<root>/embedded/`.
  - **Link.** `nm -C` of the built peer `.so` and of the variant `.so` shows no `astra_autopilot::` symbol. The
    shipped `.so` also names none of the four defect types (amendment 2).
  - **Constants.** They are the peer's own, pinned bitwise to `gains.hpp`'s and `limits.hpp`'s by a test (§8),
    never by an include. `kPi` is pinned to `std::numbers::pi`.
  - **Where the two meet.** Only R5's and the `Transport` test's executables compile both. They are comparators,
    not on R6's compared path.
- **The bound** is bitwise, on every object in §6 (ADR-0034 Decision 4's first kind). Doubles compare by their
  IEEE bits (a `uint64` view or `float.hex()`), so −0.0 differs from +0.0.

## 4. Shared inputs, and what each cannot see

| Shared input | What it cannot see | Mitigation |
|---|---|---|
| The config text: the scenario, divisor 10, the command map and limits, and Q8's six law parameters | A wrong value in the text, since both sides read it | The drift test pins the peer configs to M4's accepted configs (§10). Q8's test pins the six parameters (§7). M4's `test_trim_matches_config_initial_state` links the trim literals to `astra.trim` to 1e-12. |
| ICD v1's structs (`icd/astra_icd.h`), compiled into both | A layout defect common to both | ADR-0006's static asserts; `test_lockstep.py`'s hand-counted offsets |
| The sim side: core, the physics plugins, the exchange stage, the recorder and the logger, which run in both runs | Any sim-side defect, which is common to both runs | Out of this oracle's scope: M4's R1 to R12b, oracle (a) and the existing suites |
| The platform: clang-18, libstdc++, glibc's libm (`std::remainder`), SSE2 doubles, the floating-point environment | A defect common to both sides, such as a miscompile both share | `std::remainder` is exact by IEEE definition. §5's flag and target checks. A guard in each sim process asserts round-to-nearest and no FTZ or DAZ (§6). The autopilot process keeps the ABI's initial MXCSR, since nothing in `embedded/` changes it. |
| `build/python`: one autopilot binary and one plugin directory | A stale binary | One build and one worktree state, with `git_describe` equal in both logs (lessons 2026-10-04 and 2026-10-08) |

**Blind spots, disclosed rather than claimed:**
- A defect in M4's law itself is shared by construction, since the peer is its port. This oracle proves
  equivalence, not correctness. M4's R2 bands and `test_autopilot_design.py` cover correctness.
- **The scenario's envelope.** M4's runs never saturate, never hold, and meet no dt edge after the first frame's
  dt = 0 (M4 PR 6's trial; `7c7725d` evidence: elevator 0.024 to 0.029 rad, throttle 0.31 to 0.63, aileron
  within ±0.011 rad, `held 0`). So R6 exercises only the linear, unclamped path. Saturation, anti-windup, holds,
  non-finite fields and the other dt edges are R5's, unit-level, not R6's.

## 5. The floating-point flags (R7), stated before the first comparison

`test_autopilot_peer.py::test_autopilot_peer_flags_*` reads `compile_commands.json` and asserts equality. The
translation units:
- the peer, `aircraft/notional_transport/sim/transport/autopilot/autopilot.cpp`;
- the port, `embedded/autopilot/autopilot.cpp` (with `pid.hpp` inline);
- the controls, `tests/plugins/autopilot_peer_variant/autopilot_peer_variant.cpp`;
- R5's comparator, `tests/autopilot_peer/autopilot_peer_equivalence_test.cpp`;
- the `Transport` test, `tests/autopilot_peer/autopilot_peer_test.cpp`.

The builds:
- every preset the gate builds: `build/{debug,release,static-plugins,asan}`, plus `build/python`, which pytest
  runs on. In `just check` these exist before pytest, and a missing one fails the test, never skips it;
- the embedded-standalone build, for the port's unit only. It is configured into `tmp_path` for `clang++-18` and
  `g++`, with the recipe's arguments read from the justfile plus `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (decision 7).

**The floating-point flag set** of a unit is every token that is:
- any `-O*`, `-march=*`, `-mtune=*`, `-mfpmath=*`, or `-m*` naming fma, avx or sse;
- `-ffp-contract=*`, `-ffp-model=*`, `-fdenormal-fp-math=*` or `-fexcess-precision=*`;
- `-ffast-math`, `-Ofast`, `-mdaz-ftz`, `-fveclib=*` or `-mrecip*` (the last two from amendment 3);
- or one of `-fassociative-math`, `-freciprocal-math`, `-fno-signed-zeros`, `-fno-trapping-math`,
  `-ffinite-math-only`, `-fno-math-errno`, `-funsafe-math-optimizations`, `-fapprox-func` and
  `-frounding-math`, together with each one's opposite form.

That is the M7b plan's list, plus the clang-18 spellings that also govern it.

**What the test asserts:**
1. Within each build, every listed unit has the same set, and that set is the expected one:
   - {} in debug, asan and static-plugins (CMake Debug's `-g`, no optimization flag);
   - {`-O3`} in release and `build/python`;
   - {`-O3`} in both standalone configurations.

   A new floating-point flag anywhere therefore fails the test.
2. **Residual differences**, for the peer's and the variant's units against the port's. Every other token,
   leaving out `-I`, `-isystem`, `-D`, `-o`, `-c` and the source path, may differ only within a named list:
   `-std=c++17` against `-std=c++23`, `-fvisibility=hidden` and `-fvisibility-inlines-hidden`. The test lists
   the `-D` values without asserting them. The two test units, which are comparators, get checks 1 and 3 only.
3. **The target check.** For each build's compiler, `<cxx> <that unit's -std and floating-point set> -dM -E -x c++
   /dev/null` shows `__FMA__` and `__FMA4__` undefined, `__SSE2_MATH__` defined and `__FLT_EVAL_METHOD__` 0. So
   `-ffp-contract=on` (clang's default) has no fused instruction to form, and doubles are not evaluated in
   extended precision.

**Expected, from the M7b plan's reading at `8e525c5` and this plan-mode read of the main checkout's stale
`build/{asan,python}`:**
- the port's unit is `-O3 -DNDEBUG -std=c++17 -fPIC` plus the shared warnings in `build/python`, and `-g` with
  the sanitizer flags in asan;
- a plugin's unit adds `-fvisibility=hidden -fvisibility-inlines-hidden` under `-std=c++23`;
- a test's unit carries `-fPIE`.

The test writes the per-unit table to its report directory. A trimmed copy is committed as
`docs/reviews/evidence/<head>-fp-flags.log` and stated in the PR body before the first comparison (§14, step 6).

**A hit stops the first comparison and goes to Joe.** A hit is any unexpected flag, any residual outside the
list, or any FMA macro.

## 6. The equivalence (R6): runs, objects and exclusions

**The runs, per level** (light and moderate, seed 42, 100,000 frames, the setpoints 10,000 m, 230 m/s and 0 rad):
- **shm:** `lockstep_child.py run --config aircraft/notional_transport/configs/cruise_<level>_turbulence.yaml
  --trim config --log`.
  - The config is `ls.load_config(...)` of M4's file, unchanged, with the added log block.
  - `--trim config` builds the `AutopilotTrim` from that config's own `initial_controls.controls.{elevator,
    throttle}` and `initial.state.euler_nb[1]` values, in place of `trim_for` (Q8).
  - `autopilot_argv` passes each with `repr` (`lockstep.py:359-387`). The setpoints are `lockstep_child.py`'s
    `SETPOINTS`.
- **in-process:** `lockstep_child.py inprocess --config
  aircraft/notional_transport/configs/cruise_<level>_turbulence_autopilot_peer.yaml --log`.
  - The config is `astra._yaml_route.load` of the file, the reading `lockstep_session` gets, plus the same log
    block.
  - It is built with `_core.Config.from_dict` and `_core.Simulation.from_config`. It runs `run()`, then
    `flush_log()` and `close_log()`.
- **The same added log block:** `log: {path: <run dir>/run.h5}`, which differs only in the directory. The stored
  config omits `log` (`simulation.cpp:497`).
- **Both runs build their `Config` with `Config.from_dict`,** the route `lockstep_session` takes
  (`lockstep.py:672`). The stored config text is yaml-cpp's emission of the same doubles on both sides
  (`_core.cpp:177-182`, `config.cpp:361-379`), so it compares like with like.
- **Both runs use one build** (`build/python`'s `_core`, plugin directory and `astra_autopilot`) and **one
  worktree state,** back to back in one pytest session on a clean tree (lessons 2026-10-04 and 2026-10-08).
- **The floating-point environment guard.** Each child, before it runs, asserts:
  - round-to-nearest, with `fegetround()` read through `ctypes`;
  - no flush-to-zero and no denormals-are-zero: a subnormal product and a subnormal sum, computed at run time,
    keep their exact values.
- **The determinism repeat:** a second in-process light run. Its recorder bytes equal the first run's.

**The objects, each bitwise** (Decision 8's, then this plan's additions):

| # | Object | How compared |
|---|---|---|
| O1 | `complete = 1`, no `finalized_by`, state "complete" | both logs (`astra.log.load`) |
| O2 | `frames_run` = 100,000 and no halt | `/summary` attributes; each run's `halt` is None |
| O3 | The exchange count | `transport.exchanges` = 10,000 and `transport.missed` = 0 read from each sim at the end; recorder counts (10,000, 10,000, 100,001) |
| O4 | The request, reply and state streams | **The whole recorder blob, byte for byte** (header and all three streams) |
| O5 | Every signal name, unit and rate group | Per divisor, the ordered (name, unit) list with `transport.*` removed; the same set of divisors |
| O6 | Every state and physics column | Every non-`transport.*` column of every rate group, every row, as `uint64` bits; each group's `frame` column |
| O7 | `/final_state` | `v0`'s 13 doubles as bits, and its `frame` and `after_halt` |
| O8 | The stored config | Each log's `config` attribute, read with PyYAML's `BaseLoader` so every scalar is its text, with `transport` deleted; the two trees are equal with key order |
| O9 | `/plugins` | Exactly one transport row in each (shm's, the peer's). Every other row equal in every column (path, category, name, `core_api_version`, `built_with`, `sha256`), in order. Every physics plugin's SHA-256 is therefore equal |
| O10 | The peer's two counts | The autopilot's exit line `handled 10000 held 0` (`PeerSummary`) against `transport.peer.handled` = 10,000 and `transport.peer.held` = 0 |
| O11 | Root attributes (this plan) | `log_format_version`, `git_describe`, `core_version`, `core_api_version`, `dt`. `git_describe` equality is the one-build check. Its failure message says that a `-dirty` mismatch means the tree changed between runs (lesson 2026-10-08), so a whole-log difference is read first as that attribute, not as the port (amendment 5) |
| O12 | `/summary` (this plan) | Every attribute (`halt_*`, `health.*`), and the `min` and `max` of every non-`transport.*` signal, as bits |
| O13 | `/writes`, and `transport.fault` (this plan) | `/writes` empty in both; `transport.fault` = 0 in both |
| O14 | Determinism (this plan) | The light in-process repeat's recorder equals the first |

**Excluded, and only these** (Decision 8):
- the `transport` config block;
- the transport's `/plugins` row;
- every `transport.*` signal: its column, its unit, and its `min` and `max`.

O3, O10 and O13 compare the sim-owned and the peer's counts directly.

**A difference on any object is a porting defect until shown otherwise.**
- The test reports the first differing record: stream, index and field. For logs, it reports the column and row.
- The PR stops, and the cause goes to Joe. A stated floating-point cause goes to him as such (ADR-0034
  Decision 8).

## 7. Q8's one trim result, and parse equality

The six law parameters each have one text, written once in the shared config:
- the trims are the config's committed `initial_controls` and `euler_nb[1]` literals;
- the setpoints are `10000.0`, `230.0` and `0.0`.

The autopilot gets each as `repr(float(text))`, through `strtod` in `cli.cpp:74-86`. The peer gets it as
`transport.peer.*` text, through `Config::number`. `test_autopilot_peer_law_parameters_parse_to_the_same_doubles`
runs per level and asserts four things:
1. **Text.** In the peer config:
   - `peer.trim.elevator` and `peer.trim.throttle` are, character for character, the config's
     `vehicle.initial_controls.controls` texts;
   - `peer.trim.theta` is `initial.state.euler_nb[1]`'s text;
   - those texts equal M4's config's (`BaseLoader`).

   The setpoint texts parse to `lockstep_child.SETPOINTS`'s values.
2. **The autopilot's parse.** glibc's `strtod`, called through `ctypes` (the function `cli.cpp` calls), on
   `repr(float(text))`, gives `float(text)` bit for bit (`.hex()`).
3. **The peer's parse, on both routes.** The six `transport.peer.*` parameter signals equal `float(text)` bit for
   bit after `Simulation.from_config` on two configs:
   - `Config.from_file(<peer config>)`, the route `astra run` and `test_config_keys.py` take;
   - `Config.from_dict(_yaml_route.load(...))`, the route R6 takes.
4. **The rules.** The same values pass `cli.cpp`'s rules (§8's `Transport` test checks each refusal).

Plan mode checked that `repr(float(t)) == t` for all six texts, so the shm run's argv carries the committed text
itself.

## 8. Unit-level equivalence (R5): ctest, all four presets

`astra_autopilot_peer_equivalence_tests` links `astra_autopilot_lib`, `tests/support/alloc_counter.cpp` and
GoogleTest, and includes `autopilot_law.hpp`. It does not link `astra_core`.
- **What it runs.** It drives `astra_autopilot::Autopilot` and the peer's `Law` with identical frame sequences and
  identical `now` (the frame's `t`).
- **What it asserts after every frame:** the replies are byte-equal (`memcmp` of the 64-byte struct), and
  `handled()` and `held()` are equal.
- **Optimization levels.** The debug, asan and static-plugins presets are -O0, and release is -O3, so both levels
  are covered.

Suite `AutopilotPeerEquivalence`:
1. `ConstantsArePinnedToTheAutopilots`:
   - the twelve gains, the five ranges' `lo` and `hi`, `kModeFlags` and `kMaxIntegrationDt`, each bitwise
     (`std::bit_cast<std::uint64_t>`) against `gains.hpp` and `limits.hpp`;
   - `kPi` against `std::numbers::pi`;
   - `Gains{}` against the named constants.
2. Hand-built sequences, one `TEST` each, from the trim of `autopilot_test.cpp:36-40` unless stated:
   - `FirstFrameAndTheTrimBiases`: a usable first frame; an unusable first frame (replies the trims, 0, 0);
   - `HeldFramesAndRecovery`: held frames leave the loop state alone, and recovery integrates one period
     (`autopilot.cpp:47-55`);
   - `WindInvalidHoldsWithFiniteFields`;
   - `EachReadFieldNonFinite`: each of the nine read fields at NaN, +Inf and −Inf. Non-finite fields that are not
     read (beta, alpha, mach, cas, lat, lon, ax to az) do not hold;
   - `StatusBitsThatTouchNoReadField`: ATMOSPHERE_INVALID, CAS_SUPERSONIC, ACCEL_INVALID and
     LATLON_NOT_MODELED do not hold;
   - `SaturationOfEachOutput`: elevator ±0.3, throttle 0 and 1, aileron and rudder ±0.35, and the bank command
     ±0.35;
   - `AntiWindupAtAndAcrossEachLimit`, for each of the three PIDs at each limit:
     - an unclamped command beyond the limit that would wind further (the integral holds);
     - a command exactly at the limit (inclusive, so it integrates), with a trim at ±0.3, 0 or 1;
     - a step that lands outside but nearer (it integrates);
     - an error sign change (it unwinds at once);
   - `DtEdges`: the first frame, a repeated `t`, a backward `t`, a gap of 1.5 s, exactly 1.0 s (which
     integrates), a NaN and an Inf `t` and the frame after each, and a subnormal positive dt;
   - `HeadingWrap`: ψ through ±π and ±3π, setpoints at 3.0 and −π, and `remainder`'s ties;
   - `SignedZeros`: every field at −0.0, and at +0.0.
3. `SeededSequences`: seeds 1 to 8, 50,000 frames each, from `std::mt19937_64(seed)` with the in-house uniform
   u = (x >> 11)·2⁻⁵³ (AGENTS.md's randomness rule).
   - **Per seed:** setpoints with altitude in [0, 15,000] m, airspeed in [50, 300] m/s and heading in [−π, π];
     trims with elevator in [−0.3, 0.3], throttle in [0, 1] and θ in [−0.5, 0.5]. One seed in four puts a trim
     exactly on a limit.
   - **Per frame:**
     - Δt is 0.01 with probability 0.85; otherwise 0 (0.03), −0.01 (0.03), 1.5 (0.02), 1.0 (0.02),
       u·2 s (0.04) or NaN (0.01);
     - the read fields are drawn wide enough to saturate every output. Altitude error is ±5,000 m at a scale
       drawn per 1,000-frame segment from {1, 100, 5,000}; tas error ±60 m/s; ψ in [−4π, 4π]; φ and θ in ±1;
       p, q and r in ±2; vertical speed ±200;
     - WIND_INVALID with probability 0.05, and every other status bit at random;
     - a non-finite value (NaN, +Inf or −Inf) in one random read field with probability 0.03, and in one
       unread field with probability 0.05.
   - **Coverage, asserted over the eight seeds:** at least one hold of each kind; each of the four reply
     channels exactly at each of its limits; the bank command at ±0.35; for each PID, at least one refused
     integral step and one unwinding step (from the peer law's test accessors); and every Δt kind.
   - The constants carry a comment citing this section (ADR-0034 Decision 6).
4. `LawUpdateAllocatesNothing`: `alloc_counter` over 10,000 updates.

`astra_autopilot_peer_tests` (it links `astra_core`, `astra_autopilot_lib` and `alloc_counter`; it is an
`astra_add_plugin_test`):
- `AutopilotPeerTransport.RepliesEqualTheAutopilotsBitwise`: the shipped `AutopilotPeer` is constructed from its
  header and initialized from a transport block built from text. It is driven through `open`, `publish`,
  `wait_ack` and `read_reply` on seed 1's sequence, against `Autopilot::update(in, in.t, out)`.
  - The replies are byte-equal.
  - `wait_ack` gives `kObserved` with the published `seq`.
  - The `handled` and `held` signals equal the autopilot's counts.
- `AutopilotPeerTransport.RefusesWhatTheCliRefusesNamingTheKey`. For each of the six keys, the probes are a
  missing key, NaN, ±Inf, and the out-of-range values (airspeed 0 and −1; elevator ±0.31; throttle −0.1 and
  1.1).
  - Each probe the peer refuses with a `ConfigError` naming the key is a value `parse_arguments` also refuses.
  - The in-range probes pass both.
  - Probes written as `strtod` overflow or underflow text are outside the list. `Config::number` and `strtod`'s
    errno differ there, and no committed literal is such text.
- `AutopilotPeerSmoke.ShippedPeerFliesWithoutAllocating`: the tracked light peer config, 2,000 frames by
  `step()`.
  - Zero allocations over the steps, and no halt.
  - `transport.exchanges` = 200, `handled` = 200 and `held` = 0.
- `AutopilotPeerSmoke.EachVariantDefectFliesWithoutAllocating`: the same config with `transport.plugin` replaced
  and `test_defect` added, by exact-once text substitution, for each of the four defects. 2,000 frames, zero
  allocations, no halt.
- `AutopilotPeerSmoke.AnUnknownDefectIsAConfigErrorNamingTheKey`.

The expected ctest count is 18: thirteen `AutopilotPeerEquivalence`, two `AutopilotPeerTransport` and three
`AutopilotPeerSmoke`. The `-N` count confirms it before the run.

## 9. The four porting controls (R8, amendment 2, N-03)

**How each runs.** Each control runs in-process with the variant (`lockstep_child.py inprocess --defect <name>`,
recorder only), at both levels. It is compared with that level's shm run (§6), the reference.

**Notation.** Exchange k is frame 10k. The recorder's state j is the start of frame j. Request k is packed from
state 10k. Reply k first acts on state 10k + 1.

**"First differing record at exchange k\*, fields S"** is registered to mean exactly this:
- every request at exchanges up to and including k\* is equal;
- every state at frames up to and including 10k\* is equal;
- every reply before k\* is equal;
- reply k\* differs in exactly the fields S, and every other reply field there is equal.

Nothing after k\* is registered, apart from the bands and control 4's whole-run registration.

**The precondition, N-03's exact replay,** runs in pytest before each control's comparison, per level, on the shm
run's recorded requests 0 and 1. It is plain Python doubles, written in `autopilot.cpp`'s and `pid.hpp`'s operation
order: the PID `update` with `inside`, `distance` and `clamp`, `math.remainder`, and the shipped and the
defective variant of each expression. In order:
1. Both requests are usable: WIND_INVALID clear, and the nine read fields finite.
2. The replay's shipped expressions reproduce the shm run's recorded replies 0 and 1 bitwise (decision 3). The
   integrals are exactly 0.0 before exchange 1, because exchange 0 has dt = 0.
3. The replay's defective expressions give the registered pattern below.

A failed precondition stops the PR and goes to Joe, before the comparison runs.

**Recorded before any control run** (amendment 1). The replay uses only the shm reference run, which ADR-0034
Decision 3 allows before the first run.
- §14's step 7a runs, at both levels:
  - the two shm runs;
  - the replay, alone: `pytest -k 'autopilot_peer and precondition'`, which executes neither the peer nor the
    variant.
- Its results are written into the table below as literals, dated, and committed.
- The test then holds the same literals, each with a comment citing this section, and asserts that the replay
  reproduces them. A mismatch is a failed precondition.
- The variant's control runs, the first run of that path, start only after that commit (§14, step 7b).

| Level | Control 1: elevator at frame 0, shipped against defect | Control 2: S at frame 10 | Control 3: request 0's q; elevator at frame 0 equal; elevator at frame 10, shipped against defect | Control 4: the shm `mode_flags` |
|---|---|---|---|---|
| light | (written at step 7a, dated) | | | |
| moderate | | | | |

| # | Planted (variant) | k\*, S | Why it is certain | The replay's precondition |
|---|---|---|---|---|
| 1 | `float_elevator_trim` | 0, {elevator} | Only the elevator offset reads `trims.elevator_rad` (and `last_elevator_`'s initial value, which differs too if frame 0 were held). The float error is −4.798e-10 rad (plan mode, §11), added unsaturated to an elevator near 0.027 rad, whose ulp is 3.5e-18 | The replay's elevators differ at frame 0 |
| 2 | `output_before_integral` | 1, S (decision 1; registered as the literal in the table above, amendment 1) | At exchange 0, dt = 0 (no previous frame), so no integral moves and both orders evaluate the same expression. At exchange 1, dt = t(10) − t(0) is in (0, 1], so each PID's candidate is taken. The rudder has no PID | S, the replay's set of differing fields at frame 10, contains `throttle`. Predicted S = {elevator, aileron, throttle} |
| 3 | `float_pitch_rate` | 1, {elevator} | At frame 0, q = +0.0 exactly: the IC's ω_b is 0, and `icd_pack.hpp:90` packs ω_b(1). So both products are +0.0. Only the elevator reads `kPitchRate`. The float error is 3.576e-10 | Request 0's q is 0.0; the replay's elevators are equal at frame 0 and differ at frame 10 |
| 4 | `mode_word_zero` | 0, {mode_flags}, **and at every exchange** | The sim records `mode_flags` and acts on none of it (ADR-0022 Determinism; M4 Q16) | The shm run's `mode_flags` is 1 at every exchange |

**Control 4's whole-run registration** (decision 2):
- the state and request streams are equal at every frame and exchange;
- every reply differs in `mode_flags` (0 against 1) and in nothing else.

**The bands, registered as a pass for every control at both levels** (amendment 2). Over all 100,001 recorded
states:
- max |altitude − 10,000 m| ≤ 30 m;
- max |wrapped ψ| ≤ 2°;
- both computed as M4's R2 computes them, with `test_lockstep.py`'s `euler_321`, `ALTITUDE_BAND_M` and
  `HEADING_BAND_DEG`.

M4's reference margins at seed 42 were 1.33 m and 0.083° (light) and 10.4 m and 0.83° (moderate), on `7c7725d`.
They are context only, not registered.

**The nominal maxima are reported beside each control's** (amendment 4). At both levels, the in-process nominal
run's maximum altitude error and maximum wrapped heading error are computed from the same recorder states, in the
same way. They are context, not registered. The controls' band pass stays registered as written.

**Stops.** Each of these stops the PR and goes to Joe:
- a control that does not differ;
- a control that differs earlier, or at k\* in a field set other than S;
- a control that leaves a band;
- a failed precondition.

**Reported in the PR body,** per control and level: the first differing record with each field's two values; the
registered S; and both band maxima, beside the nominal run's.

## 10. The key block, the configs and R9

**The key block** (`autopilot.cpp`'s leading comment, `scripts/config_keys.py`'s format):

```
// Config keys:
//   peer.setpoints.altitude | number | m   | required | The altitude to hold (astra_autopilot's
//          --altitude-m). Finite.
//   peer.setpoints.airspeed | number | m/s | required | The true airspeed (tas) to hold
//          (--airspeed-mps). Finite and above 0.
//   peer.setpoints.heading  | number | rad | required | The heading to hold (--heading-rad); the
//          error is wrapped to [-pi, pi] by remainder. Finite.
//   peer.trim.elevator      | number | rad | required | The trim's elevator (--elevator-trim), the
//          same text as vehicle.initial_controls.controls.elevator: the elevator offset's bias, and
//          the reply until the first usable frame. Within the elevator limit, +-0.3.
//   peer.trim.throttle      | number | 1   | required | The trim's throttle (--throttle-trim), the
//          same text as vehicle.initial_controls.controls.throttle. In [0, 1].
//   peer.trim.theta         | number | rad | required | The trim's pitch (--theta-trim), the same
//          text as initial.state.euler_nb[1] (theta = alpha at gamma = 0). Finite.
```

**The configs.** Each is a copy of `cruise_<level>_turbulence.yaml` with its `transport` block replaced by this
one, and with a header saying so: Q8, the drift test, `astra run`'s refusal, and the test that flies it.

```yaml
transport:
  plugin: aircraft/notional_transport/sim/transport/autopilot
  divisor: 10
  commands: { … M4's text … }
  limits: { … M4's text … }
  peer:
    setpoints: {altitude: 10000.0, airspeed: 230.0, heading: 0.0}
    trim: {elevator: 0.026674474137989946, throttle: 0.4985979490507746, theta: 0.023586859573503434}
```

The timeouts are left out: they are the core's defaults, and an in-process `open()` ignores them, as the
`nesc_control` configs do.

**The drift test** (`test_autopilot_peer_configs_copy_m4_with_only_the_transport_block_replaced`, per level):
- Read with `BaseLoader`, the peer config equals M4's config once `transport` is removed from both.
- Its `transport` has exactly `plugin`, `divisor`, `commands`, `limits` and `peer`. The first is the peer's path,
  and the middle three equal M4's text.
- `peer` holds exactly `setpoints` and `trim`, with §7's texts.

**R9:**
- `python3 -I -B scripts/config_keys.py` regenerates the reference, and `--check` passes.
- `test_config_keys.py` reaches the plugin through both configs, and its six documented keys equal the keys
  consumed. Both configs build, so neither joins `EXPECTED_REFUSALS`.
- `test_autopilot_peer_configs_exit_65_under_astra_run` (per level): `astra run <config>` exits 65 with
  `test_cli.py:386-399`'s message naming `transport`, and writes nothing.

## 11. What was measured before the first run (ADR-0034 Decision 3)

Plan mode ran, read-only, through a scratch `python3 -I` on stdout:
- the constants' float errors:
  - `float(0.026674474137989946) − it` = −4.79785867285587e-10 (relative −1.80e-8), against the M7b plan's
    4.8e-10;
  - `float(0.184) − it` = +3.576278717609682e-10 (relative 1.94e-9), against the M7b plan's 3.58e-10;
  - the elevator's ulp near 0.0267 is 3.47e-18, and the throttle's near 0.5 is 1.11e-16;
- `repr(float(t)) == t` for the six parameter texts.

Read, not run:
- M4's recorded margins and command ranges in `docs/reviews/evidence/7c7725d-a6-reports.json`, which are the
  reference's earlier output;
- the main checkout's stale `build/{asan,python}/compile_commands.json`.

Nothing of the peer exists, so nothing of the compared output was measured. The shm reference first runs in §14's
step 7a. That is the reference alone, which ADR-0034 Decision 3 allows, and §9's literals come from it before any
control run.

## 12. Acceptance

| R | Check | Command |
|---|---|---|
| R5 | §8's 18 tests, every preset | `ctest --preset asan -R AutopilotPeer -N` (18), then `ctest --preset asan -R AutopilotPeer --no-tests=error`; the gate runs all four presets |
| R6 | §6's objects at both levels, and the determinism repeat | `.venv/bin/pytest tests/python -k 'autopilot_peer or lockstep'` (`--collect-only -q` count first) |
| R7 | §5, stated before the first comparison | the same pytest; `<head>-fp-flags.log`; the PR body |
| R8 | §9 at both levels, with the preconditions and the bands | the same pytest |
| R9 | §10 | `python3 -I -B scripts/config_keys.py --check`; `test_config_keys.py` and the 65 test, in the same pytest and in the gate |
| — | The embedded boundary unchanged | `just embedded-standalone` |
| R16 | The full gate | `./scripts/check-full.sh`: exit 0 and `check: OK`, with §1's record and the gate's delta against 1162 s, 1178 s and 1001 s in the PR body |
| — | Review | An adversarial Codex review of this plan before the first comparison, and of the diff after it, with at most two correction rounds |
| — | Before the merge | `git fetch`, and merge `origin/main` in if it moved |

Pre-registered constants in the tests carry a comment citing this plan's section (ADR-0034 Decision 6): the seeds,
counts and distributions (§8), the band limits and each control's k\* and S rule (§9), and the flag sets and
allowed residuals (§5).

## 13. Docs in this PR

- `PLAN.md`: the M7b block's PR 3 line.
- The key block, and the regenerated `docs/reference/config-keys.md`.
- This plan, the review record (`docs/reviews/m7b-pr3-autopilot-peer-review.md`), and the evidence: the gate
  summary, the flags log, and a first-run log with §9's report.
- At `/wrap`: `STATE.md`.
- Not touched:
  - `docs/architecture.md`: §16's row stays planned, and §18 is PR 5's;
  - the ADRs and the M7b plan;
  - `test_lockstep.py`.

## 14. Sequence after approval

1. `git fetch origin`. Confirm `origin/main` is `106c0cb` or later.
   - This step is confirm-only (amendment 7): Joe confirmed that the PR 2 worktree and branch are already gone.
   - `git worktree prune --dry-run`, `git branch --list 'worktree-m7b-pr2*'` and `git ls-remote --heads origin`
     record it.
2. Create the worktree `m7b-pr3-autopilot-peer` from `origin/main` (`EnterWorktree`).
3. Move this draft into the worktree as `docs/plans/m7b-pr3-autopilot-peer.md` (`mv`, before its first add), and
   fold in Joe's decisions. Commit, push, and open the draft PR (Tier 3).
4. **The Codex adversarial plan critique.** It is fresh and read-only, on PR 2's route: `task --fresh --effort
   high`, no `--write`, no `--model`, in the background. It is given ADR-0034, ADR-0029, the M7b plan's oracle
   (b) section, and this plan. Its proposed focus:
   - §6's objects against Decision 8's, so that nothing is fewer;
   - §2's law against `autopilot.cpp` and `pid.hpp`, for expression order;
   - §9's certainty arguments, and whether the Python replay matches the port's order;
   - §5's flag set and target check;
   - §3's independence argument, and §4's blind spots;
   - the hooks against ADR-0034 Decision 5;
   - whether an excluded object could hide a porting defect.

   Verify each finding against its cited lines, and fold in each supported one. A finding that would change a
   registered object, a control, the comparison set or a decided item comes to Joe before the first comparison.
5. **Build:** the plugin, the configs, the variant, the C++ tests, the pytest and the CMake; `just python`, and
   every preset.
   - Until step 4 closes, this step compiles only. Nothing executes the peer or the variant: no `AutopilotPeer`
     ctest, no pytest that runs them, and no `test_config_keys.py`.
   - What may run: `config_keys.py` (text only), `just embedded-standalone`, and the pytest flag, target and
     independence tests, which are facts about the build.
6. **State the flags** (§5) in `<head>-fp-flags.log` and the PR body, before the first comparison.
7. **The reference alone, then the first comparison.**
   - **7a, the reference** (amendment 1). Run `.venv/bin/pytest tests/python -k 'autopilot_peer and precondition'`.
     It runs the two shm runs and §9's replay, and executes neither the peer nor the variant.
     - A failed structural precondition stops the PR and goes to Joe. The structural preconditions are: usable;
       the replay reproduces the reference; S contains `throttle`; request 0's q = 0.0; and the elevator
       inequalities.
     - Write the results into §9's table as dated literals, and into the test as constants citing §9. Commit and
       push before anything in 7b runs.
   - **7b, the first comparison:** §12's ctest and pytest commands, counts first.
     - A nominal difference, a failed precondition, or a control that does not break as registered stops the PR
       and goes to Joe.
     - Write the first-run log.
8. §13's docs, then the full gate with §1's record, the evidence and the PR body.
9. `/review-cycle`: Codex reviews the diff against `--base origin/main`, with at most two correction rounds.
   Before review, run `git diff --check origin/main..HEAD`.
10. `git fetch`, and merge `origin/main` in if it moved. A code change from that merge reruns the gate.
11. `/human-review`, then `/wrap`, then stop. The merge is Joe's.

## 15. Risks and rollback

- **Bitwise may fail on the nominal runs.** The likeliest causes are a transcription slip, or a `now` or trim that
  differs. §7's test and §8's R5 isolate each before R6. A difference stops the PR (§6), and is never absorbed.
- **A floating-point cause.** It could be a flag, a target or the process environment. §5 and the guard make each
  one visible before the first comparison. Any such cause goes to Joe.
- **Control 2's S.** If the replay's S lacks `throttle`, the precondition fails. That would mean the throttle's
  integral increment rounded away at frame 10, against an estimate of about 1e-8 over an ulp of 1.1e-16. It stops
  the PR and goes to Joe.
- **Gate cost** (decision 8). The estimate is about 12 lockstep-length runs, each about 1 to 2 s with process
  start, plus four logs of about 55 MB and two standalone configures.
- **Rollback:** revert the PR. Its only shipped addition is the plugin, with its configs and the regenerated
  reference. Everything else is test code.

## 16. The Codex plan critique (§14, step 4)

To be filled in after the run: the route, the candidate and base, the thread id, the exit status and verdict, each
finding as verified, and its resolution or Joe's decision.
