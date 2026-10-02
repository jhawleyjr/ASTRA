# M6 PR 9: frame-time benchmark (spec brief and plan)

Status: approved by Joe, 2026-10-01, with four changes folded in: the quiet-machine check in steps 5 and 6, D3's
frame counts, the two docs fixes, and this status line. Risk: Tier 2. It adds a test executable, its smoke in the gate, a `just`
recipe and docs. Nothing changes in `core/`, `plugins/`, `aircraft/`, `icd/`, goldens or tolerances.
Base: `origin/main` = `8b4852b`. Worktree `m6-pr9-benchmark`, branch `worktree-m6-pr9-benchmark`, draft PR.
Sources: [M6 plan](m6-analysis-tooling.md), with its PR 9 row, the "Benchmark" contract, Q11, R19 and R19b,
and C-19 and N-05 (their per-finding notes in PR #78's body). Binding ADRs: 0010 (the integer frame, and no
wall clock in `core/`, `plugins/` or `aircraft/`) and 0023 (the two clock sites, which this PR leaves alone).
Parallel: M6 PR 6 in `m6-pr6-mc-analysis`. This PR touches none of `astra.mc_analysis`, the `mc` subcommands
or their tests. Shared files: PLAN.md (one line), STATE.md and the `/wrap` docs.

## Context

M6 promises "measure the per-frame cost and the margin against `dt`". M11's `RealTime` mode will track that
figure over time, so it needs a trustworthy baseline now. Codex raised two problems with the planned benchmark:
- C-19: a benchmark can time halted steps that do no work.
- N-05: timing `step()` alone misses the drain and flush cost that `run()`, `flush_log()` and `close_log()`
  pay outside it.

PR 2's `tests/python/test_log_cost.py` (R19b) measured the end-to-end logged figure once, from Python, as an
opt-in test. This PR makes that figure, and the per-step cost, a C++ measurement with a smoke in the gate.

## Repository findings (observed)

- `Simulation::step()` returns a `HaltReason`, and on a halt the clock does not advance
  (`core/include/astra/kernel/simulation.hpp`). Two halts repeat on every later call without doing any work: a
  kLog halt from a full ring, and any halt in a run with a transport. Without a transport, a repeated `step()`
  runs the halted frame again. These are C-19's hazards.
- `run(N)` drains at the high-water mark. `flush_log()` drains on demand. `close_log()` publishes by
  `renameat2(RENAME_NOREPLACE)`. ADR-0027 item 8 says `discard_log()` is the only way a tool deletes a log
  (`LogDiscardOutcome::kDeleted`).
- R19b's figure is in PR #80's body: Release Python, median of 3. For cruise, `run()` + `close_log()` takes
  0.31 ms for 1 frame and 143 ms for 100,000 frames. Cruise plus `slow_rate` takes 0.29 ms and 145 ms. The
  `Session` pattern (`step()` + `flush_log()` per frame) takes 169 ms for 100,000 frames. That is about
  1.4 µs per cruise frame, so the clock-read overhead (tens of ns) matters, and the benchmark measures and
  records it.
- C++ tests build configs as text: `Config::from_string(text + "\nlog:\n  path: ...\n")`
  (`tests/unit/kernel/run_log_test.cpp:93`). `astra/signals/hdf5_file.hpp`'s `File::open_read_only` and
  `attribute_uint64` read a log back (`run_log_test.cpp:761`).
- `tests/` is outside the wall-clock scan's scope (`scripts/wallclock_scan.py`). Lockstep tests already use
  `std::chrono::steady_clock` (`tests/lockstep/shm_test_support.hpp:35`).
- The provenance strings exist: `astra::built_with()`, `git_describe()` and `core_version()`.
- No ctest invokes Python today. The precedent is `cmake -P` scripts, but `python3` is already required by the
  gate (`scripts/*.py`).
- `aircraft/_template/configs/cruise.yaml` is all zero TODOs, so it is not a meaningful benchmark. The
  turbulence configs carry a `transport` block and need the autopilot peer.
- The host is a Ryzen 9 9950X (32 logical CPUs) with the governor at `powersave`. The benchmark records the
  governor and does not change it.

## Decisions (approved by Joe with the plan, 2026-10-01)

- **D1. Named configs for the per-step cost:**
  - `aircraft/notional_transport/configs/cruise.yaml`: the full stack (WGS-84 rotating, J2, ISA, Dryden, aero,
    two turbofans). This is the headline figure.
  - `aircraft/notional_transport/configs/cruise_still_air.yaml`: the same without Dryden, which isolates the
    turbulence cost.
  - `tests/configs/dummy_1000.yaml`: the scheduler floor, with no physics.

  Excluded: transport configs, because a lockstep frame's cost includes the peer and belongs to M10/M11, and
  the template, because its values are zeros.
- **D2. The logged end-to-end cases (N-05) mirror R19b's five,** so the C++ figure tracks PR #80's numbers:
  - cruise, `run()` + `close_log()`, at 1 frame and at N frames;
  - cruise plus `tests/plugins/slow_rate` (divisor 10), the same two cases. The plugin is inserted as a model
    by text after the unique `"\n  models:\n"` anchor, and the benchmark refuses if the anchor is not found
    exactly once.
  - cruise, `step()` + `flush_log()` every frame, then `close_log()`, at N frames (the `Session` pattern).
- **D3. Run length.** "All its frames" means `sim.frames` by default. `--frames N` overrides it for every case;
  the JSON records the override and asserts the count. All three D1 configs have `sim.frames: 1000`, so a
  default run is 1000 frames each, and the Release baseline is the `--frames 100000` override (R19b's N), not
  the configs' own count. The smoke uses `--frames 1000`. Cruise runs 300 s without a halt in the frozen regression, so
  100 s is inside its known envelope.
- **D4. Timing boundaries and warm-up (C-19).**
  - **Warm-up.** Before each measured run, a separate, untimed `Simulation` of the same config runs
    `min(frames, 1000)` frames, and no halt is allowed.
  - **Per-step samples.** The measured run uses a fresh instance from frame 0. Every frame is one sample: one
    `steady_clock::now()` on each side of exactly one `step()`, into a preallocated buffer.
  - **Per-frame checks.** After each sample, outside the timed window, the benchmark checks for no halt and
    for `clock().frame() == i + 1`. So it validates the frames it timed, not a run before timing.
  - **End-to-end window.** The timer starts after `from_config` and stops after `close_log()` returns, as R19b
    did. It runs 3 repeats and records each repeat and the median.
  - **End-to-end checks, outside the timer.** No halt, `clock().frame() == N`, a published file at `log.path`
    and no `.partial`, `/summary` `frames_run == N`, and `complete == 1`. Then `discard_log()` must return
    `kDeleted`.
  - **Clock overhead.** The median of 10,000 back-to-back `now()` pairs, recorded and not subtracted.
  - The step-cost phase refuses a config with a `log` block, because a ring with no drain would kLog-halt.
- **D5. Statistics, defined and unit-tested against known samples (C-19's "aggregation and margin oracles").**
  - Mean is the arithmetic mean.
  - Median is the middle value, or the mean of the two middle values for an even count.
  - p99 is the nearest rank: the ⌈0.99 n⌉-th smallest value.
  - Max is the largest sample, and the first frame's cost is reported separately.
  - `frames_per_second` = n / Σ samples.
  - `margin_p99` = 1 − p99/dt and `margin_max` = 1 − max/dt. Both are fractions of `dt`; a negative value means
    over budget.
  - Times are in SI seconds, as doubles, with a top-level `"units"` map and no unit suffixes in names
    (ADR-0002's spirit).
- **D6. Record only after every check passes.** Samples stay in memory. The JSON is written once, at the end,
  only if every config and case passed. Exit codes:
  - 1: a halt or failed verification, with the check, source and frame on stderr, and no file written;
  - 64: a usage error;
  - 74: the output could not be written.
- **D7. JSON contents (`"schema": "astra.frame_benchmark/1"`):**
  - `build`: the build type, `built_with`, the compiler version, `git_describe` and `core_version`;
  - `host`: the CPU model from `/proc/cpuinfo`, the logical CPUs, the governor, the kernel release, and
    `/proc/loadavg` at start and at end. This is the objective "nothing else was running" evidence.
  - `clock`: its name, period and overhead;
  - `step[]`: for each config, its path, `dt`, frames, warm-up frames, the statistics, `first_frame`,
    `frames_per_second` and both margins;
  - `end_to_end[]`: for each case, the config, pattern, frames, the repeats' seconds, the median and
    `frames_per_second`.
- **D8. The smoke runs in every preset** (it asserts no speed, and ASan checks the statistics code). Its ctest
  label is `benchmark`, not `unit`, so the Stop hook skips it. The **baseline** is Release only: the JSON
  carries the build type, and the `just benchmark` recipe builds and runs the `release` preset.
- **D9. Evidence.** The baseline JSON is committed as `docs/reviews/evidence/<head>-frame-benchmark.json`,
  which is small, and summarized in the PR body. It is recorded only after step 6's quiet-machine check, and
  the PR body gives that check's output.

## Mapping and sequence

| Req | Change / owned paths | Acceptance |
|---|---|---|
| D5 statistics | `tests/benchmark/frame_stats.hpp`: `summarize(span<const double>, dt)`, nearest-rank percentile, and a JSON string escape. `tests/benchmark/frame_stats_test.cpp` (gtest, label `unit`) covers 1..100 (mean 50.5, median 50.5, p99 99, max 100), an odd count, unsorted input, a single sample, empty input refused, margins (dt = 1e-3, p99 = 2.5e-4 gives 0.75; over budget gives a negative margin), fps, and the escape of a quote, a backslash and a control character | `ctest --preset asan -R FrameStats --no-tests=error` |
| R19, C-19, N-05 | `tests/benchmark/frame_benchmark.cpp` (`astra_frame_benchmark`; `--out`, `--frames`, `--config` repeatable with the D1 set as the default). Source paths are compile definitions (`ASTRA_SOURCE_DIR`, `ASTRA_TEST_PLUGIN_DIR`), and `astra_add_plugin_test()` gives the build order. `static_assert(steady_clock::is_steady)` | the smoke below |
| R19 smoke | `tests/benchmark/check_frame_benchmark.py`, stdlib only. It runs the executable with `--frames 1000` into a fresh temporary directory and checks: every D7 field and its type; 3 `step` entries with `frames == 1000` and `warmup_frames == 1000`; 5 `end_to_end` entries with frames 1 or 1000 and 3 repeats each; `median ≤ p99 ≤ max` and `mean ≤ max` (consistency, not speed); margins equal to their definitions recomputed from the fields; build and host fields non-empty. **Negative cases:** `--config tests/configs/health_nan_wrench.yaml`, which halts at frame 500, exits 1, names the check and frame 500, and writes no file; a bad argument exits 64. Registered in `tests/benchmark/CMakeLists.txt` as ctest `FrameBenchmark.smoke` (label `benchmark`) through `find_program(python3 REQUIRED)` | `ctest --preset release -R FrameBenchmark --no-tests=error` (and in all four presets under the gate) |
| Wiring | `tests/CMakeLists.txt` (`add_subdirectory(benchmark)`); `justfile` (`benchmark out:` builds `release` and runs `--frames 100000 --out {{out}}`); `pyrightconfig.json` include adds `tests/benchmark` | full gate (`tidy`, `ruff`, `pyright`, wall-clock scan unaffected) |
| Docs | `docs/process/validation.md`: line 29's "There is no standalone benchmark" corrected, and a short "Frame-time benchmark" paragraph covering the command, the quiet-machine rule and what the smoke does not assert. `docs/architecture.md:598`, the M6 note: PR 9 built and the benchmark named; it is Joe's file, so the PR body lists the edit under "Needs Joe's decision". PLAN.md: the PR 9 line under M6. STATE.md at `/wrap` | `python3 scripts/docs_check.py` |

**Sequence (one builder):**
1. `EnterWorktree m6-pr9-benchmark`. Copy this plan to `docs/plans/m6-pr9-benchmark.md` and delete the draft
   from the main checkout.
2. First commit: the plan. Check `git log -1 --format='%(trailers:key=Co-authored-by)'`. Push, open the draft
   PR, and record the result in the PR body.
3. Build the statistics header and its test, then the executable, then the checker and its CMake wiring,
   iterating with `./scripts/check-fast.sh` and focused `ctest`. Commit and push as I go.
4. The `just` recipe, the docs and the PLAN.md line.
5. **Quiet-machine check.** Name the ASTRA build and test processes explicitly:
   `pgrep -af 'just check|check-full|check-fast|ninja|cmake --build|ctest|pytest|astra_|clang\+\+-18|clang-tidy|run-clang-tidy'`.
   A bare `clang` pattern would match clangd, which is always running and re-indexes after `rm -rf build`.
   None may be running. Then wait until clangd is idle, near 0 % CPU in `top -bn1` over a few samples. Record
   the `pgrep` output, `top -bn1 | head -20` and `/proc/loadavg`. Joe holds PR 6's gate while this gate and
   the baseline run. Then `rm -rf build && ./scripts/check-full.sh`, and commit the trimmed
   `<head>-gate-summary.log`.
6. The same check, recorded again, then `just benchmark docs/reviews/evidence/<head>-frame-benchmark.json`.
   Commit the JSON, and put the `pgrep`, `top` and load-average records in the PR body.
7. `/review-cycle` (Codex) on the final diff, then fixes and a re-gate if anything substantive changes.
8. The PR body per `docs/templates/pr-description.md` (`/human-review`), then `/wrap`. If the trailer is empty,
   `/wrap` closes STATE.md's attribution thread. If PR 6 merges first, merge `origin/main`, rewrite STATE.md
   for the combined main, and rerun the full gate.

## How C-19 and N-05 close

- **C-19 (the notes ask for logging, drain placement, warm-up, sample count, timing boundaries, frame advance,
  refusing halts, and aggregation and margin oracles).**
  - Logging is off for step timing, which refuses a `log` block. The drains run inside the end-to-end window
    (D2, D4).
  - Warm-up runs on a separate instance (D4). Every frame is a sample, and the per-frame check covers
    `!halt && frame == i + 1` for exactly the frames timed.
  - The negative smoke proves a halt at frame 500 records nothing (D6).
  - The statistics have known-sample oracles (D5).
- **N-05 (the notes ask for end-to-end timing that includes drains and close, separate from step cost, for the
  `run()`, stepped and `Session` patterns).** D2's five cases time `run()` + close and per-frame `step()` +
  `flush_log()` + close, with step cost reported separately. Construction is excluded and close is included,
  as in R19b. The `Session` pattern is the same call sequence `astra.lockstep.Session` makes without a
  transport. A plain stepped run with no flush would kLog-halt once its ring filled, so it is not a valid
  pattern.

## Validation and risks

- **Gate:** `rm -rf build && ./scripts/check-full.sh`, exit 0 and `check: OK`. The smoke adds a few seconds per
  preset.
- **Noise.** No speed is asserted anywhere. The baseline is evidence for M11, not a threshold. The governor and
  load average travel with it.
- **Text insertion of `slow_rate`** depends on cruise.yaml's layout. A changed layout fails loudly (D2), never
  silently.
- **Rollback.** Purely additive. Reverting removes the directory, the recipe and the doc lines.
- **Not in scope:** CPU pinning, `RealTime` mode, tracking the benchmark over time (M11), and transport configs
  (M10/M11).

## Plan critique

No Codex plan critique. The shape was settled by Q11 and approved with the M6 plan. The open details are
test-level, and the PR's Codex review checks them (Q23's reasoning).

## Done when

- `FrameStats` and `FrameBenchmark.smoke` pass in all presets.
- The full gate passes fresh, with exit 0 and `check: OK`.
- The Release baseline JSON is committed with the quiet-machine evidence.
- C-19 and N-05 are closed as above.
- The Codex review is done, and the PR body and the attribution-trailer result are recorded.
- Joe approves and merges.
