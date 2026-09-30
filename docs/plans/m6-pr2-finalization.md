# M6 PR 2: ADR-0027 implementation (run-log finalization)

Spec: [ADR-0027](../decisions/0027-run-log-finalization.md) (accepted 2026-09-29), [M6 plan](m6-analysis-tooling.md)
PR 2 row, R16 to R19b, and "Obligations carried to PR plans" (C-01 to C-05, C-18); [PR 1 plan](m6-pr1-adr0027.md) D1 to D7.
Risk: Tier 3 (`core/` run-log finalization, byte identity, file deletion).
Status: **approved by Joe, 2026-09-29, in plan mode, with
his changes J1 to J12 folded in below.**
Base: `origin/main` `02bddfa` (#79). Branch `worktree-m6-pr2-finalization`. This PR does not touch `docs/decisions/`,
`tests/golden/**` or any tolerance file.

## Context

Today a dropped `Simulation` writes `complete = 1` through its destructor, a file at `log.path` can be unfinished
(PR 4's F1), a killed run leaves a file no reader opens (no drain flushes), and tools clean up by the final name.
ADR-0027 fixes all four: write to `<log.path>.partial`, flush and certify rows per block, publish only from
`close_log()` by a no-replace rename, mark a destructor close without publishing, and delete only through an
identity-checked `discard_log()`. This PR implements items 1 to 10 and proves them with R16 to R19b.

## Repository findings (read at `02bddfa`)

- `RunLog` (`core/src/kernel/run_log.cpp`): `read_settings` refuses an existing `log.path` by name (`:116`); the
  constructor creates at `log.path` (`:144`) and calls `discard()` (delete by name, `:410`) on a provenance
  failure; `write_block` (`:334`) appends `values` then `frame` with no flush; `close()` (`:350`) is the only
  flush; `close_best_effort` (`:398`) is `close()`, so the destructor sets `complete`.
- `Simulation` (`core/src/kernel/simulation.cpp`): `open_log_and_transport` (`:490`) calls `log_->discard()` on a
  failed handshake; `~Simulation` (`:617`) calls `close_best_effort`.
- `hdf5::File::create` (`core/src/signals/hdf5_file.cpp:958`): `H5F_ACC_EXCL`, bounds V18..V110, sec2 by default.
  `Object::set_attribute` replaces an attribute by `H5Adelete` + `H5Acreate2` (`:174`). libhdf5 1.10.10 declares
  `H5Dget_chunk_index_type` and `H5Fget_vfd_handle`; glibc 2.39 declares `renameat2`/`RENAME_NOREPLACE`.
- `astra.log` (`python/astra/log.py`): `FORMAT_VERSION = 1`; `allow_incomplete` bypasses every check; `replay`
  unlinks `out` by name on an exception (`:173`) and keeps `out` when it returns `False`.
- Cleanup by final name: `_runkit.run_logged` (`:264`), `montecarlo.run_one` (`:489`), `montecarlo._abort`
  (`:710`); the Monte Carlo kill test waits on `run_*.h5` (`tests/python/test_montecarlo.py:291`).
- `astra.lockstep.lockstep_session` never calls `close_log()`; its cleanup drops the `Simulation` (`:653`).
- No shipped model registers a divisor above 1; model entries take a `divisor` key (`simulation.cpp:72`).
- The bindings deliberately bind no `for_testing` API (`python/bindings/_core.cpp:439`, PR 5 D2).
- `kCoreApiVersion = 9` (`core/include/astra/core_api.hpp.in:51`); no file under `plugins/`, `aircraft/` or
  `tests/plugins/` includes `run_log.hpp`, `simulation.hpp` or `hdf5_file.hpp`.

## Design (ADR item → implementation)

**Item 1, where a log is written. Choice: a directory descriptor, not an absolute path.** `read_settings` checks the
name only: non-empty, a real file name (not `.`, `..` or a trailing `/`), not ending in `.partial`. The `RunLog`
constructor opens the parent directory once (`O_PATH | O_DIRECTORY | O_CLOEXEC`), then, through it and before
creating anything, refuses an existing `log.path` and an existing `<name>.partial` (`fstatat(..., AT_SYMLINK_NOFOLLOW)`,
so dangling symlinks count). All three refusals are `ConfigError` naming `log.path`. Why a descriptor: it binds the
create, the rename, the identity check and every delete to the directory inode chosen at construction, so neither a
`chdir` nor a rename or symlink swap of any ancestor can redirect them. An absolute path covers only the `chdir`.
HDF5 takes a name, not a descriptor, so the create goes through `/proc/self/fd/<dirfd>/<name>.partial`. `/proc` is
already required by the item-1 reopen, and HDF5 stores no path in the file, so the bytes are unaffected.
`hdf5::File::create` gains a display-name argument, so messages name `log.path`, not the `/proc` path.

**Kept descriptor.** Right after the create: `File::vfd_descriptor()` (new; `H5Fget_vfd_handle`, sec2), then
`open("/proc/self/fd/<n>", O_PATH | O_CLOEXEC)`. This is a separate open file description holding no lock, and it
pins the inode. `O_PATH` needs no read permission, so a restrictive umask cannot fail it. `O_CLOEXEC` keeps it out of
lockstep peers. If the reopen fails, the init delete checks identity against HDF5's own descriptor, which is still
open, and the error is a `ConfigError`. Both descriptors are RAII (`UniqueFd`) and closed only by `~RunLog`.

**Identity check and deletes.** `same_file(name)`: `fstatat(dirfd, name, AT_SYMLINK_NOFOLLOW)` against
`fstat(kept)`; the result is match, missing, or mismatch (a symlink or another inode). `checked_unlink(name)` is
the check followed by `unlinkat(dirfd, name, 0)`. It is the core's only delete, used by the init-failure path
(constructor provenance failure, the reopen failure, `open_log_and_transport`'s handshake failure) and by
`discard_log()`. `RunLog::discard()` (delete by name) is removed.

**Item 2, per-block flush.** `write_block`: `values.append`, `frame.append`, `file.flush()` (`H5F_SCOPE_LOCAL`), then
`rows_committed` overwritten in place on `/rate_<d>`. The new `Object::overwrite_attribute(name, uint64)` uses
`H5Aopen` + `H5Awrite`, with no delete/create churn in the object header and never a window where the attribute
is absent. The constructor writes `rows_committed = 0` on each group and flushes after the provenance.
`kFormatVersion = 2`.

**Item 3, `close()`.** Steps 1 to 5 as today, with the tails going through the same `write_block`. Then
`same_file(<name>.partial)`, then `renameat2(dirfd, partial, dirfd, name, RENAME_NOREPLACE)`. Every refusal
(mismatch or missing: "the .partial is no longer this run's file"; `EEXIST`; `EINVAL`/`ENOSYS`: unsupported; any
other errno) is a `LogError` naming `log.path` and the reason. It renames nothing and records the failure, so a
later call rethrows it (idempotent, no retry). An internal state tracks where the file lives (open, published,
refused, failed, discarded, destructor), and so which name `discard_log()` acts on.

**Item 4, destructor.** A new `RunLog::finalize_by_destructor(end) noexcept` runs steps 1 to 5 with the root
attribute `finalized_by = "destructor"` in place of `complete`, never renames, and after any earlier failure writes
nothing. `~Simulation` calls it for an open log. `~RunLog` closes both descriptors and deletes nothing.

**Item 8, `discard_log()`.** `Simulation::discard_log()` returns `LogDiscardResult { outcome, path, detail }`, where
`outcome` is one of `kNoLog`, `kDeleted`, `kMissing`, `kMismatch` or `kFailed` (an `unlinkat` errno), and never
throws. An open log is abandoned: handles closed, no tails, no marker. The call then does `checked_unlink` of the
current name: the `.partial` before publication, `log.path` after it. The first result is cached and returned again.
Afterwards `close_log()` throws `LogError` "discarded" and `step()` halts `kLog`. It is bound as
`Simulation.discard_log()`, with `_core.LogDiscardOutcome` and `_core.LogDiscardResult` (`.deleted` property).

**Test-only hooks (C++ only, not bound: the posture of PR 5 D2).** New `RunLog::Fault` values: `kRenameUnsupported`
(the rename step behaves as if `renameat2` failed with `EINVAL`), plus init faults `kReopen` and `kProvenance`
carried in a test-only `RunLogSettings` field for direct `RunLog` construction. A `RunLog::KillPoint` hook
(`std::raise(SIGKILL)` at a named point and occurrence) is reached through
`Simulation::inject_log_kill_for_testing` and the settings field.

**Items 6 and 7, `astra.log`.** `FORMAT_VERSION = 2`; version 1 raises `LogFormatError` naming it. `RunLog` gains
`state` (`"complete" | "destructor" | "incomplete"`), `published`, and `rows_committed` per divisor; `complete`
stays. Strict mode checks `Path(path).resolve().name.endswith(".partial")` first, raising `IncompleteLogError` "not
published by close_log()". It then refuses any state but complete, naming it, and applies ADR-0025's checks plus the
cross-checks: `final_state.frame == frames_run`; the groups' names, taken together, equal `/summary`'s, each exactly
once; `rows_committed` equals each group's rows. With `allow_incomplete`, a complete or destructor file is read whole
with the cross-checks. An incomplete file gives provenance plus the rows below `rows_committed` only, after the four
checks named per group (missing, beyond an extent, a chunk covering certified rows unallocated via h5py's
`get_chunk_info_by_coord`, frame column not `0, d, 2d, …`). `replay` loads strictly, makes `out` absolute, builds
the `Simulation` (the core applies item 1's refusals to `out`) and closes with `close_log()`. On an exception after
the build it calls `discard_log()`. **If the published `out` is not byte-identical it calls `discard_log()` and
returns `False`** (ADR item 7). This changes M5, where `out` was kept on `False`. If that `discard_log()` returns
anything but deleted, replay raises `ReplayError` naming the result and the path it left (J6), so a caller never
believes `out` was removed when it was not.

**Tools.** `_runkit.run_logged` gains `accept: Callable[[log.RunLog], None] | None`, run on the loaded log while the
`Simulation` is alive. Any exception in `from_config` leaves cleanup to the core. Any exception after it (drive,
close, load, accept) calls `sim.discard_log()` and re-raises. A result other than deleted is added as a note on the
exception, and no file is touched by name. `montecarlo.run_one` moves its `RunLengthError`/`LogHaltError` checks
into `accept`. `montecarlo._abort` deletes nothing. `_stop` SIGKILLs every live worker, so **an abort leaves a
`run_<i>.h5.partial` for every run in flight**, not only the dead worker's; strict `load` refuses each by name. A run
killed after its publication leaves a complete `run_<i>.h5`. `BatchError`/`BatchInterrupted` name the leftover
`.partial` files, and the module docstring says so (J7). `scenario.run`
and `replay` use the same rule. `lockstep_session`'s clean-exit cleanup calls `close_log()` unless the session
already called it (a `Session._log_close_called` flag, so a close the body already handled is not re-raised). A
body that raised is left to the destructor's `.partial`. Docstrings that cite "PLAN.md's finalization-marker open
item" are reworded. `run_length_failures` stays as a consumer check.

**PLAN.md.** Add the PR 2 line under M6 and remove the (M6) Finalization open item (`:313`). Its point (a),
"flag, not refuse", was superseded by Q17/D3, and the PR body says so. `docs/architecture.md` stays out of this PR.
The M6 plan's PR 10 row names only §12's status, so §9's and §12's "(M6 PR 2, *planned*)" markers become stale when
this merges; the PR body records them as a follow-up (J8).

**Behavior changes from M5, listed in the PR body (J9):** a dropped `Simulation` leaves a `.partial`, not a complete
log; `replay` deletes `out` when it returns `False`; a Monte Carlo abort leaves a `.partial` for every run in
flight, where M5 left complete logs only; `log_format_version` 2 (version 1 refused).

## Tests (R16 to R19b)

Fixture: a new test plugin, `tests/plugins/slow_rate`. It is a zero-wrench `WrenchSource`, scheduled with a model
entry `divisor: 10`, that registers `slow_rate.value` at divisor 10, set in `step()` from `ctx.frame`. Two configs
use it: cruise plus the plugin, and a minimal fast config (flat earth, constant mass, the plugin) for kill tests.
Blocks for `/rate_10` complete every 10,240 frames. C++ tests go in a new `tests/unit/kernel/run_log_finalization_test.cpp`,
with suites prefixed `RunLog` so `-R RunLog` selects them. Python tests go in `test_log.py` and the tool test
files, with names containing `log`.

| Req | How it is tested |
|---|---|
| R16 lifecycle | C++ and Python. A normal run leaves only `log.path` (complete, no `finalized_by`) and no `.partial`. A dropped `Simulation`, in a child process and in-process, leaves no `log.path` and a `.partial` with `finalized_by = "destructor"` and no `complete`. Strict `load` refuses it by name. `allow_incomplete` gives state `destructor`, `published = False`, `/summary` exposed and cross-checked. `replay` refuses it by name, and a copy renamed to `x.h5` is refused naming state "destructor". A destructor after a failed drain sets no marker (state incomplete). A `Session` closed normally leaves `log.path` complete (`lockstep_child` `log-normal`). |
| R16 kills, deterministic | C++ `EXPECT_EXIT(..., KilledBySignal(SIGKILL))` at each `KillPoint`, fixture config, then the parent inspects. After the create (before the provenance flush): the `.partial` opens or not, and neither is a failure. After the provenance flush: opens, 0 certified rows. Mid-block (between the `values` and `frame` appends) and after a block's flush but before `rows_committed`: stated-limit windows, so a typed refusal or a correct prefix. After `rows_committed` of the 1st `/rate_1` block, the 3rd, and the 1st `/rate_10` block: must open, with each group's certified rows a bitwise prefix of a clean reference run. In close, after the tails, after the final objects, after the marker, and after the identity check before the rename: no `log.path`, a refusal or a correct prefix. After the rename: `log.path` complete and no `.partial` (item 5's accepted outcome). The destructor after its marker: no `log.path`. The C++ oracle reads through `hdf5::File` independently of `astra.log`. Each killed file is read in its own child process (J4), so a reader crash inside a stated-limit window is a named failure and the file is kept for inspection. After the create, before the provenance flush, the outcome must still be typed: in Python, `load` raises `LogFormatError` or `IncompleteLogError` naming the file, or `allow_incomplete` returns provenance only with zero certified rows. |
| R16 kills, seeded | Python: a child runs the fast fixture config for 30,000 frames, then `close_log()`, printing `closing` first. Deterministic self-kills (`os.kill(getpid(), SIGKILL)`) come before the first block (frame 500) and between blocks (after 2,048 + 100 frames with `flush_log`, and after 20,485 frames). Then 12 parent kills at delays drawn from `random.Random(seed)` over the measured run, plus 4 drawn within the close window after `closing`. The delay clock starts at a `built` line the child prints after `from_config`, not at launch (J4). Each killed file is read by `load` in a separate child process, so a reader crash is a named failure with the file kept. The outcomes must not be degenerate: some kills leave certified `/rate_1` rows, and at least one lands in the close window. Every outcome must be one of: no files; a `.partial` that `load(allow_incomplete=True)` reads with each group's rows a bitwise prefix of a clean run (NumPy `uint64` views); a typed `LogFormatError`/`IncompleteLogError`; or, after the publication point, a complete `log.path` and no `.partial`. The outcome counts are printed for the evidence. |
| Item 1, chdir (J1) | C++: a relative `log.path`, `chdir` to another directory mid-run; `close_log()` publishes in the original directory and nothing appears in the new one. `discard_log()` after the same `chdir` deletes in the original directory, once before publication (the `.partial`) and once after (`log.path`). |
| Item 1, directories (J2) | C++: a missing directory fails at opening the parent, and an unwritable one at the create; each is a `ConfigError` naming `log.path` with no file created. The unwritable case is skipped when running as root (`geteuid() == 0`). |
| R16b | C++: after a publication refusal the `Simulation` is dropped, and the refused `.partial` stays byte-identical (J11). An existing `log.path`, a dangling symlink at it, a stale `.partial` (left byte-identical), a dangling `.partial` symlink and a name ending `.partial`, each a `ConfigError` naming `log.path` with no file created. A file created at `log.path` mid-run makes `close_log()` raise naming `EEXIST`; the foreign bytes are unchanged, the `.partial` is kept, and a second call raises the same. The injected `kRenameUnsupported` raises "not supported", publishes nothing and keeps the `.partial`. A failed handshake leaves neither name (extends `SimulationTransport.AFailedHandshakeLeavesNoLogFile`). Init faults `kReopen`/`kProvenance` leave neither name. **The identity check's three cases:** the `.partial` renamed aside and a second `Simulation` on the same `log.path` creating a new `.partial`; renamed away with nothing in its place; and replaced by a symlink to the moved original. Each makes `close_log()` raise "no longer this run's file", and the second run's file, the moved file and the symlink stay byte- and link-identical. The second run then publishes normally. Python: replay's `out` existing, a stale `out.partial` and an `out` ending `.partial`, each refused with the files untouched. |
| R17 | Python, `_tampered` copies (h5py `r+`). Complete files: a wrong `final_state.frame`, a renamed signal in `/rate_1/values` names, a duplicated name, and a wrong `rows_committed`, each refused naming its check. A version-1 file raises `LogFormatError`. Incomplete files, made by a kill: `rows_committed` deleted, set beyond the extent, a dataset extended over an unallocated chunk with `rows_committed` covering it, and a certified `frame` value altered, each an `IncompleteLogError` naming the group and the check. A `.partial` holding `complete = 1` (from a refused publication) reads as complete and unpublished under `allow_incomplete`; strict mode refuses it by name, including through a non-`.partial` symlink to it. |
| R18 | C++ `RunLogDrainIdentity`, for cruise and cruise plus `slow_rate` (25,000 frames, crossing `/rate_10`'s second block). Final files must be byte-identical across `run()` twice, `step()` + `flush_log()` every frame, `buffer_frames: 3` under `run()`, and `buffer_frames: 7` with irregular drains. A different seed differs (not vacuous). A halting run: cruise plus `slow_rate` plus `nan_wrench` with `nan_frame: 12345`, by `run()` and by per-frame flush. Python: `replay` of each of those logs is `True` (M5 R9), including the halting one, and M5's existing R4/R9 tests stay. `ctest --preset release -R NotionalTransportCruiseRegression`. **If any pair differs: stop and bring it to Joe (ADR-0027 item 9).** |
| R18b | Python, per tool (`_runkit` through `scenario.run`, `montecarlo.run_one`, and `replay`). Handled failure while open (drive raises): no `log.path`, no `.partial`. Collision (a monkeypatched drive or `_rerun` creates a foreign file at the final path): `close_log` raises `EEXIST`, `discard_log()` removes the owned `.partial`, and the foreign file keeps its bytes. Published then discarded: `replay` returns `False` on a tampered input and `out` is gone; if that discard does not delete (the published `out` replaced before it), replay raises `ReplayError` naming the result, and the replacement is untouched (J6); `montecarlo`'s `accept` refuses a short run after publication and `run_<i>.h5` is gone. Mismatch: the drive moves the `.partial` aside and replaces it, then raises; the result is `kMismatch` and every file is untouched, and the exception carries the note. Missing: result `kMissing`, nothing touched. `discard_log()` is idempotent. After it, `close_log()` raises "discarded" and `step()` halts `LOG`. A clean run of each tool leaves no `.partial`. The Monte Carlo kill and interrupt tests wait on `run_*.h5.partial` and assert: every run not completed keeps its `.partial` (or a complete published log), the error names each leftover `.partial`, and completed runs load strictly. A grep in the review packet shows no `unlink`/`remove` of a log path left in `python/astra/`. |
| R19b | `tests/python/test_log_cost.py`, wall clock in the test, median of 3, printed, with no speed assertion. `run()` + `close_log()` for 1 and 100,000 frames, cruise and cruise plus `slow_rate`, plus 100,000 frames of `step()` + `flush_log()` (the `Session` pattern, C-18). Each case asserts `frames_run` before its number is used. It uses only APIs on the base, so the same file measures before and after. **Opt-in (J10):** the module skips unless `ASTRA_MEASURE_LOG_COST=1` is set, and the skip reason names the variable, so the full gate and the acceptance filter skip it and the gate time recorded against 1001 s and 59.3 s is PR 2's own cost, not the benchmark's. The before and after numbers come from explicit runs with the variable set, and the PR body gives the exact command lines. |

**ADR-0027's "PR 2 confirms", with evidence (any failure: stop and bring it to Joe):**
1. Chunk index. A new `Dataset::chunk_index()` (`H5Dget_chunk_index_type`) and a test asserting
   `H5D_CHUNK_IDX_BTREE` (version-1 B-tree) for every `/rate_<d>/frame` and `values` in a real log. The test's
   output goes in the PR body.
2. The `frame` column of a halted run stepped again (C++ and Python `load`). `nan_wrench` halts, `step()` is called
   5 more times (each re-halts at the same frame), then `close_log()`. A non-finite-input halt at frame 100, then a
   finite write and 50 more frames. Both loads pass strictly, with `frame` exactly `0 … frames_run-1`, and `/rate_10`
   exactly multiples of 10.
   Evidence independent of the writer's library (J12): a byte scan of the log finds the v1 B-tree signature `TREE`
   and none of `BTHD`, `EAHD` or `FAHD` (the v2 B-tree, extensible-array and fixed-array index headers).
3. `load(log.path)` while the `Simulation` is alive: in-process after `close_log()`, and after a refused publication,
   `load(partial, allow_incomplete=True)` with the `Simulation` still alive. Made non-vacuous (J5): the test asserts
   HDF5 file locking is enabled (`HDF5_USE_FILE_LOCKING` not `FALSE`), and a one-off mutation that takes the kept
   descriptor by `dup()` of HDF5's descriptor must make the test fail. The mutation, its failing output and the
   revert are recorded in the PR body, not committed.
4. `kCoreApiVersion` unchanged (ADR-0009), **run on the final head** (J3). The header set is transitive: every
   header any plugin target includes (`plugins/`, `aircraft/`, `tests/plugins/`, `slow_rate` included), taken from
   the build's dependency files (the `.o.d` files of those targets). That set is compared with the final head's
   changed files against `origin/main`. If a plugin-reached header changed (for example `errors.hpp`, for a new
   `LogError` message or type), the PR body says so explicitly and argues whether it changes a plugin-facing
   interface. The commands and output go in the PR body.

## Joe's changes at approval (2026-09-29)

J1 the chdir test; J2 missing and unwritable directories, both failure sites; J3 confirmation 4 on the final head from
the transitive include set; J4 killed files read in child processes, typed behavior after the create, the seeded
delay clock started after `from_config`, non-degenerate outcomes; J5 confirmation 3's locking assertion and `dup()`
mutation; J6 replay raises `ReplayError` when the mismatch discard does not delete; J7 the abort leaves every
in-flight `.partial`, named in the error, docstring and PR body; J8 the stale architecture markers as a PR-body
follow-up; J9 the behavior changes listed in the PR body; J10 R19b is opt-in (`ASTRA_MEASURE_LOG_COST=1`), so the
gate measures PR 2's own cost; J11 a refused `.partial` survives the destructor byte-identical; J12 the chunk-index
byte scan.

## Obligations C-01 to C-05, C-18

| Finding | Closed by |
|---|---|
| C-01 | Per-block flush and `rows_committed` (item 2); R16 deterministic and seeded kills, with bitwise prefixes checked by two independent readers (C++ `hdf5::File`, h5py) |
| C-02 | The directory-descriptor create, the refusals, `renameat2(RENAME_NOREPLACE)` and the identity check; R16b including the three identity cases and replay's `out` |
| C-03 | `finalize_by_destructor` never renames or sets `complete`; `Session` closes with `close_log()`; R16 lifecycle; no finalization attribute at `log.path`, so R18 replay holds |
| C-04 | `discard_log()` the only delete path; `_runkit.accept`; no by-name deletes; Monte Carlo kill test waits on `.partial`; R18b |
| C-05 | R18 over every drain pattern in use, plus replay, a halting run and the divisor-10 group; stop rule, no fallback |
| C-18 | Flush only on block completion; R19b's per-frame `flush_log` case measures `Session`'s pattern |

## Sequence (one builder, commits pushed as they land; draft PR at the first)

1. Commit the plan, `tests/plugins/slow_rate` (+ `astra_add_plugin_test` wiring) and `test_log_cost.py`. Build
   (`just python`) and **capture R19b's before-numbers on this commit**:
   `ASTRA_MEASURE_LOG_COST=1 .venv/bin/pytest tests/python/test_log_cost.py -s`, plus a timed full
   `.venv/bin/pytest tests/python` (R19b skipped), the pytest before-number. Both command lines and results go in
   the PR body. Open the draft PR.
2. The `hdf5_file` additions (display name, `vfd_descriptor`, `overwrite_attribute`, `chunk_index`) and their tests.
   Run the chunk-index confirmation now.
3. Core: `RunLog`/`Simulation`/bindings and the C++ tests (R16, R16b, R18, confirmation 2). Run R18 right after
   the per-block flush lands, before building the rest on it.
4. Python: `astra.log`, `_runkit`, `montecarlo`, `scenario`, `lockstep`, and their tests (R16 seeded, R17, R18
   replay, R18b, confirmations 2 and 3).
5. PLAN.md. R19b after-numbers.

Stop and bring it to Joe: any failed confirmation, any R18 byte difference, or a needed change to `docs/decisions/`,
`tests/golden/**` or a tolerance.

## Checks and review

- The M6 plan's PR 2 acceptance commands: `ctest --preset asan -R 'RunLog|Hdf5|NoAlloc' --no-tests=error`;
  `.venv/bin/pytest tests/python -k 'log or replay or montecarlo or scenario or lockstep'`;
  `ctest --preset release -R NotionalTransportCruiseRegression --no-tests=error`. The seeded kill test is repeated
  20 times (lesson 2026-09-28) before it is trusted.
- `rm -rf build/` first (lesson 2026-09-29), then `./scripts/check-full.sh` in the background: exit 0 and
  `check: OK`, timed. Commit the trimmed `docs/reviews/evidence/<head>-gate-summary.log`, whose header states the
  fresh configure. The gate time and its pytest time go in the PR body against 1001 s and 59.3 s, and against
  step 1's timed pytest (R19b skipped in all of them, J10). The after-numbers come from the same explicit
  `ASTRA_MEASURE_LOG_COST=1` command on the final head.
- `/review-cycle`: an adversarial Codex review of `origin/main..HEAD`, focused on byte identity across drain
  patterns, the file lifecycle under failure and kill, the no-replace rename and identity check, and
  `discard_log()` as the only delete path. At most two correction rounds; the full gate is rerun after substantive
  fixes. The PR body follows `docs/templates/pr-description.md`, then `/wrap`.
- No Codex plan critique: ADR-0027 settles the design (plan-feature step 5).

## Out of scope

`docs/decisions/` and `docs/architecture.md` (PR 10); the CLI (PR 3); `fgbridge`'s `LiveBridge` (caller-owned
`Simulation`); SWMR; durability across power loss.

## Done when

R16 to R19b pass with the four confirmations evidenced. The acceptance commands and the fresh full gate exit 0.
The Codex adversarial review is complete with its findings verified. The PR body is written for Joe, who reads the
diff and tests and merges.
