# ADR-0027: Run-log finalization

Status: **accepted**, 2026-09-29
Date: 2026-09-29
Owner: Joe
Related/supersedes: supersedes the **Finalization** section of ADR-0025 (HDF5 run log and replay), and only that
section. ADR-0025 is not edited, and its other sections stand. This record adds to three of them without
contradicting them: Enabling gains more refusals (item 1), Replay gains the `out` rules and one more refused
kind of log (item 7), and Determinism states that its fallback does not cover this record (item 9).
Elaborates ADR-0005 (HDF5 through the C API) and ADR-0010 (no wall time in the file). Drafted with
`docs/plans/m6-analysis-tooling.md` (Q13, Q17 to Q19) and `docs/plans/m6-pr1-adr0027.md` (Joe's D1 to D7).

## Context

ADR-0025's Finalization has `close_log()` write `/summary`, `/final_state` and `complete = 1`. The destructor
"closes best-effort". Observed at `be32cbb`:
- **A dropped run looks finished.** The destructor calls the same `close()` (`simulation.cpp:617`,
  `run_log.cpp:398`) and sets `complete = 1` whenever every write succeeds. A Python driver that raises mid-run
  leaves a consistent, complete-looking file that `astra.log.load` cannot tell apart from a finished run, and
  `astra.lockstep.Session` depends on that path.
- **A file at the final path may be unfinished.** In PR 4 review finding F1's window, the file's own close can
  fail after `complete = 1` is written to the file at `log.path` itself.
- **A killed run leaves nothing readable.** No drain flushes. `drain()` writes only full 1024-row blocks per
  rate group (`run_log.cpp:304`), and the tails, `/writes`, `/summary`, `/final_state` and the only flush are
  written at close (`:350`). A killed run leaves a file no reader can open, so `allow_incomplete` never
  recovers a real run.
- **Cleanup goes by the final name.** `_runkit`, `montecarlo` and `replay` delete by the final name, and the
  core deletes the file it created when init fails after the create (`RunLog::discard`, `run_log.cpp:410`).
- **Some mistakes are not caught.** `load` has no check that `final_state.frame` equals `frames_run`, or that
  the rate groups' signal names match `/summary`'s.

## Decision

**1. Where a log is written.** At construction, `log.path` is made absolute, or its parent directory held as a
directory descriptor (PR 2 chooses). The create, the publication rename and item 8's delete all go
through it, so a change of working directory during a run cannot redirect publication or deletion. Each of
these is a `ConfigError` naming `log.path`, raised before anything is created:
- a `log.path` that exists, a dangling symlink included (ADR-0025, unchanged);
- a `<log.path>.partial` that exists, a dangling symlink included. A stale partial belongs to the user, and
  nothing in ASTRA opens, changes or deletes it;
- a `log.path` whose name ends in `.partial`.

Otherwise the log creates `<log.path>.partial` exclusively (`H5F_ACC_EXCL`) in the same directory. It writes
the provenance, `/plugins`, each rate group's empty datasets and `rows_committed = 0` on each group, then
flushes. A missing or unwritable directory fails the create, and that is a `ConfigError` naming `log.path`.

*The run's own file.* Right after the exclusive create, the log keeps its own descriptor to the file it created.
It reopens HDF5's descriptor (`H5Fget_vfd_handle`, sec2 driver) through `/proc/self/fd`, which gives a
separate open file description. It is never a `dup()`, which would share HDF5's description and let HDF5's
file lock outlive HDF5's close, and never a second open of the name. If the reopen fails (for example no
`/proc`, `EMFILE`, or `EACCES` under a restrictive umask), init fails with a `ConfigError` naming `log.path`.
Item 8's init-failure delete then runs its identity check against HDF5's own descriptor, which is still open
at that point. The log holds this descriptor from the
create until the `Simulation` is destroyed: through a successful rename, a publication refusal, a failed close
and `discard_log()` (item 8). Only the destructor closes it, and it deletes nothing. When `from_config` fails,
the log is destroyed with it. Holding the descriptor also keeps the inode number from being reused. PR 2 tests
that `load(log.path)` succeeds while the `Simulation` is still alive, and so that the kept descriptor holds no
HDF5 file lock.

The *identity check* compares the descriptor's (device, inode) with
`fstatat(dirfd, name, AT_SYMLINK_NOFOLLOW)`, where `name` is where the log currently lives: `<log.path>.partial`
before the publication point (item 3), and `log.path` after it. The check fails if the name is missing, is a
symlink, or is any other file. It guards every operation the core performs on a log file by name: the rename
and every delete.

**2. Crash-readable writing.** Each rate group still writes its own 1024-row blocks, `values` then `frame`. After
every block write of any group, on the drain path and the close path alike (tails included), the writer makes
two more HDF5 calls: it flushes the file (`H5Fflush`, local scope), then sets that group's `rows_committed`
attribute (uint64, on `/rate_<divisor>`) to the number of the group's rows written so far. A value set after a
flush names only rows already on disk, so it stays true even if HDF5 writes it to disk before the next flush.
At a clean close it equals the group's row count.

*The HDF5 call sequence does not depend on the drain pattern.* `move_rows_to_blocks` merges ring rows
oldest-first across groups, and a tie goes to the smaller divisor. A block therefore completes, and its write,
flush and `rows_committed` follow, at the same point in frame order however the rows were split into drains:
`run()`'s high-water mark, a `flush_log()` after every frame as `Session` does, or `replay`'s segments. Close
continues the same merge and then writes the tails in ascending divisor order. So the whole sequence of HDF5
calls (creates, appends, flushes, attribute writes, closes) is a function of the config, the per-group row
counts and the external writes only.

*Cost.* A group with divisor d has ceil(F/d) rows after F frames, so it makes ceil(ceil(F/d)/1024) block writes.
Each block write costs one flush and one attribute write. Add one flush after the provenance and close's flush
before the marker. A 100,000-frame run with one divisor-1 group makes 98 block writes, so 98 flushes and 98
attribute writes (the 98th is the tail at close). Today no drain flushes. Under this
record a `Session`'s per-frame drains still flush only when a block completes. PR 2 measures the cost (R19b)
against the 1001 s full gate and the 59.3 s pytest run.

**3. Explicit close.** `close_log()` does the following in order:
1. finishes the merge and writes each tail block, each with its flush and `rows_committed`;
2. writes `/writes`, `/summary` and `/final_state`;
3. closes the group handles and flushes;
4. sets the root attribute `complete = 1`, its last write;
5. closes the file;
6. runs the identity check (item 1);
7. publishes with `renameat2(RENAME_NOREPLACE)` from `<log.path>.partial` to `log.path`. The rename is atomic
   and never replaces a file. It needs glibc 2.28 or later (Ubuntu 24.04 has 2.39).

The successful rename is the *publication point*: before it, the run has published nothing, and after it the
complete log is at `log.path` and no `.partial` remains, even if the process dies before `close_log()`
returns.

A publication refusal throws a `LogError` naming `log.path` and the reason, renames nothing, touches no file,
and leaves the `.partial` in place:
- a failed identity check: "the .partial is no longer this run's file". This happens, for example, when the
  run's `.partial` was moved aside and a second run then created the same name. `RENAME_NOREPLACE` protects
  only the destination, so without this check the rename would publish the second run's open file. Stated
  residual: a replacement in the gap between the check and the rename. Linux has no compare-and-rename, so
  that gap can be shrunk but not closed;
- `EEXIST`: a file appeared at `log.path` during the run, and that file is left untouched;
- `EINVAL` or `ENOSYS`: the filesystem or kernel does not support the flag;
- any other errno, with its message.

A failed write or a failed file close in steps 1 to 5 throws a `LogError` as ADR-0025 says, with no rename.
The kept descriptor stays open after every outcome (item 1). There is never a fallback to a non-atomic rename or to link and
unlink. `close_log()` stays idempotent. After any failure, a later call raises the same error and does not
retry, and after an earlier drain failure (`kLog`) it writes nothing more. A user who clears a collision
moves the file by hand (`mv -n`). **A file at `log.path` is one that `close_log()` completed and published,
and nothing else**, apart from the stated residual of the identity check. A published log carries no finalization
attribute, so a run and its replay stay byte-identical. ADR-0025's F1 residual moves off the final path: a
file close that fails after the marker leaves only a `.partial`.

**4. Destructor.** When a `Simulation` is destroyed with its log still open, it runs `close_log()`'s steps 1 to 5
with two differences. Where `close_log()` sets `complete = 1`, it sets the root attribute
`finalized_by = "destructor"`, and it never renames. The tails get their flush and `rows_committed`, and
everything is flushed before the marker. In every state, the destructor then closes the kept descriptor and
deletes nothing. After any earlier failure (a failed drain, `kLog`, a failed write
during its own sequence) it writes nothing more and sets no marker. It never sets `complete`. A dropped
`Simulation` therefore leaves only `<log.path>.partial`. A destructor that never runs (interpreter exit without
collection, `abort`, a kill) leaves a killed run's file, and so does a failed one. `astra.lockstep.Session`
calls `close_log()` on its normal close, so its logs keep reaching `log.path`. Every Python tool that writes a
log closes it with `close_log()`.

**5. What a killed run leaves.** "A kill" here means the process dies while the OS survives. There is no fsync,
so power loss and an OS crash are out of scope. Where the kill lands decides what is left:

| Where the kill lands | What is left |
|---|---|
| Before the `.partial` is created | Nothing |
| After the create, before the provenance flush returns | A `.partial` that may not open; `load` raises a typed error naming the file |
| No block write in progress (the process is anywhere outside a window below) | A `.partial` that opens. Each group holds its certified rows, a prefix bitwise equal to a clean run's. Per group it loses at most the rows still in the ring (up to `buffer_frames`), the staging block (under 1024 rows), and one block whose `rows_committed` had not yet reached disk |
| During a block write, its flush or its `rows_committed` write | The stated limit, guarded by item 6 |
| During close before the publication point, or during the destructor | The stated limit, guarded by item 6, and no `log.path` |
| After the publication point, before `close_log()` returns | The complete, published log at `log.path`, and no `.partial` |
| During `discard_log()` (item 8) | An incomplete `.partial`, or nothing, and no `log.path` if the log was not yet published. After a publication refusal the `.partial` left behind is the refused one, unchanged; after publication, `log.path` is either still there or gone |

The M6 plan's R16 ("every outcome is no `log.path`") covers kills before the publication point. A kill after
it leaves exactly what a clean close leaves, and PR 2's kill tests accept that outcome.

*The stated limit.* This record does not use HDF5's single-writer/multiple-reader mode (SWMR). Without it, HDF5
orders nothing inside a flush and may write dirty metadata to disk in the middle of an append. It also rewrites
some metadata in place: inserting a chunk can split a node of the chunk index. Under the 1.8 low bound that
index is expected to be a version-1 B-tree; PR 2 confirms which index these datasets get. So a kill inside
those windows can in principle damage rows that were already certified. **"Never wrong data" holds for a kill
outside a block write, its flush, close and the destructor.** Inside those windows it is guarded by `load`'s
checks (item 6), which refuse the failures named here; wrong data inside these windows is not ruled out.
PR 2's kill tests (R16) are evidence for this, not proof.

**6. `load`.** `astra.log.load` reports one of three finalization states: *complete* (`complete = 1`),
*destructor* (`finalized_by = "destructor"`, no `complete`) or *incomplete* (neither). It also reports whether
the file is *published*, meaning its resolved name does not end in `.partial`.
- **Strict (the default).** It refuses a path whose resolved name, symlinks followed, ends in `.partial`, naming
  "not published by close_log()". This is a guard, not a proof: a hard link is not detected. It refuses any
  state other than complete, naming the state, and applies the cross-checks below.
- **`allow_incomplete=True`, complete or destructor.** It exposes everything, `/writes`, `/summary` and
  `/final_state` included, and applies the cross-checks. A `.partial` holding `complete = 1` is reported as
  complete but unpublished. It is safe to read, because everything was flushed before the marker.
- **`allow_incomplete=True`, incomplete.** It exposes the provenance and each group's certified rows only, the
  rows below `rows_committed`. It ignores any `/writes`, `/summary` or `/final_state`, and it raises a typed
  error naming the group when:
  - `rows_committed` is missing;
  - `rows_committed` exceeds either dataset's extent;
  - the certified `frame` column is not exactly 0, d, 2d, …;
  - a chunk covering certified rows of `frame` or `values` is unallocated (h5py's chunk information).

*Cross-checks* for both finalized states, each refusal naming its check:
- ADR-0025's structural checks, unchanged;
- `final_state.frame == frames_run`;
- the rate groups' signal names, taken together, equal `/summary`'s names, each exactly once;
- each group's `rows_committed` equals its row count.

A finalized log's `frame` column is expected to be exactly 0, d, 2d, … below `frames_run`. That includes a
halted run that is stepped again after its halt; PR 2 confirms it.

**7. Replay.** `astra.log.replay` refuses a log whose state is not complete, naming the state it found. A
destructor or incomplete log is not a replayable record. Replay loads its input strictly, so an unpublished
`.partial`, even one holding `complete = 1`, is refused, naming "not published by close_log()". This is in
addition to ADR-0025's transport and `kLog` refusals. `out` follows item 1's rules: it is made absolute, or its
parent directory held as a directory descriptor (PR 2 chooses), and an existing `out`, an existing
`<out>.partial` and a name ending in `.partial` are each refused. Replay closes with `close_log()`, which
publishes to `out`. If the new file does not meet the determinism criterion, replay deletes the `out` it
published through `discard_log()` (item 8).

**8. Who deletes what.**
The core deletes a log file in exactly two cases, and each delete is an `unlinkat` through the directory that
runs item 1's identity check first. On a failed check it deletes nothing. `close_log()` and the destructor
delete nothing. A stale `.partial` refused under item 1 was not created by this run and is never touched.
Stated residual: the gap between the check and the `unlinkat`, the same as item 3's gap before the rename.
- **A failing init.** The `.partial` that the current `from_config` call created exclusively is deleted only
  while that same call is failing, never after it returns. Today there are two such sites, and any future
  init step after the create follows the same rule:
  - the `RunLog` constructor's provenance write failure;
  - the transport `open()` failure in `Simulation::open_log_and_transport`.
- **`Simulation::discard_log()`**, bound to Python and off the per-frame path. It is the only way a tool
  deletes a log file. It works in every state:
  - while the log is open, it stops writing, closes the file without setting `complete` or `finalized_by`,
    and unlinks the `.partial`;
  - after a publication refusal or a failed close, it unlinks the `.partial`;
  - after publication, it unlinks `log.path`.

  On a failed identity check or a missing name it deletes nothing and returns a result saying so. It is
  idempotent: a second call deletes nothing and returns the same result. Afterwards `close_log()` raises
  `LogError` "discarded", and `step()` halts `kLog`, as with any closed log. `discard_log()` is how a tool
  meets R18b.
- **A tool** (`montecarlo`, `scenario`, `replay`, through `_runkit`) deletes only its own run's log file, only
  when it handles a failure, and only by calling `discard_log()` while its `Simulation` is alive. When the
  result reports a mismatch or a missing name, the tool leaves every file alone. Once the `Simulation` is gone,
  a tool deletes nothing. A tool never deletes a log file by name, never a file whose construction was refused
  (an existing `log.path`, a stale `.partial`), and never a foreign file that collided at `log.path`
  (`EEXIST`) or that replaced its own. After a clean run, or an error it handles while its `Simulation` is
  alive, it leaves no `.partial` of its own.

**9. Determinism.** ADR-0025's criterion is unchanged: two runs of one config give byte-identical files at their
final paths, and so does a run and its replay. By item 2 this holds for every drain pattern in use.
ADR-0025's conditional fallback (dataset-and-attribute equality) was a libhdf5 capability fallback for its own
PR 2. It does not cover this record's changes. If PR 2's R18 shows a drain pattern or replay that changes the
bytes, the choice comes back to Joe, and any relaxation needs a record that supersedes this item.

**10. Format version.** `log_format_version` becomes 2, because `rows_committed`, `finalized_by` and the
publication rule change what a file means. `load` reads version 2 and refuses version 1 with a
`LogFormatError` naming the version. No `.h5` file is committed, and M5's logs predate this record.

## Rejected alternatives

- **The destructor renames too** (Q17's alternative): a dropped run's log stays at `log.path`, but a file there
  no longer proves an explicit close, and a finalization attribute would enter logs whose replay closes
  differently.
- **Flushing at every drain:** the flush points would depend on the drain pattern, which puts byte identity at
  risk, and a `Session` would flush every frame.
- **No flush (today):** a killed run's file does not open.
- **Two flushes per block (`values`, flush, `frame`, flush), or a flush both before and after
  `rows_committed`:** twice the cost, for what one flush plus a lagging `rows_committed` already certifies.
- **SWMR:** it would make the guarantee absolute, but it needs the 1.10 format as the low bound, allows no new
  objects once it starts (`/writes`, `/summary` and `/final_state` are created at close), and would need the
  byte-identity story redone.
- **Link then unlink to publish:** not atomic. A kill or a failed unlink between the two calls leaves both names,
  with a published file before `close_log()` returns (M6 plan N-02).
- **A non-atomic fallback on a filesystem without `RENAME_NOREPLACE`:** it would break the one guarantee
  publication exists for.
- **Judging a `.partial` by its content alone:** a `.partial` can hold `complete = 1` after a refused
  publication or a failed file close. Only the final name carries the guarantee.
- **The core never deleting, even during a failing init:** every failed handshake would leave a stale `.partial`
  that the next run refuses.
- **Overwriting or deleting a stale `.partial`:** it may be the only record of an earlier run.
- **Flagging, not refusing, a destructor-finalized log** (the M5 open item's first wording): a strict reader
  would then accept a run nobody closed.
- **Trusting the `.partial` name at publication and deletion:** `RENAME_NOREPLACE` protects only the
  destination. If the run's `.partial` is replaced during the run, the rename would publish another file, and
  a delete would remove it (Codex review of this record, F1).
- **Taking the kept descriptor by opening the name a second time:** that could open a file that already
  replaced the one HDF5 created.
- **A `dup()` of HDF5's descriptor:** it shares HDF5's open file description, so HDF5's file lock could outlive
  HDF5's close, and `load(log.path)` could fail while the `Simulation` is alive.
- **Closing the kept descriptor at the rename or at a refusal:** a tool that handles an `EEXIST` refusal could
  then no longer delete its own `.partial` safely (Codex review of this record, N1).
- **Tools deleting log files by name:** they could delete a file that replaced their own.

## Consequences and validation

Additive behind an absent `log` block: a config without one behaves as before, and M3's frozen regression is
untouched. Costs:
- one flush and one attribute write per block, plus one flush after the provenance;
- a rename at close;
- a third finalization state in `load`;
- `log_format_version` 2;
- one descriptor held per logged `Simulation`;
- a new bound call, `Simulation::discard_log()`;
- every Python tool moving to `close_log()`, and to `discard_log()` for cleanup.

`kCoreApiVersion` is expected not to change, because no plugin-facing interface changes: `close_log()`'s
signature stays the same, and `discard_log()` is a new `Simulation` method that plugins never call. PR 2
confirms this under ADR-0009's check. Rollback is reverting PR 2. This record then has no implementation
and ADR-0025's Finalization describes the code again.

Validation is the M6 plan's, in PR 2:
- R16 (the lifecycle, the destructor's file, `load` and `replay` refusals, kill tests with a divisor-10 fixture,
  a kill before the first block, between blocks and at seeded points including close);
- R16b (an existing `log.path`, a stale `.partial` left byte-identical, a collision at publication, an injected
  unsupported `RENAME_NOREPLACE`, replay's `out`, a failed handshake leaving neither name). It also covers
  the identity check. Three cases each refuse and leave every file untouched: the `.partial` moved aside with a
  second run creating the same name, moved away with nothing in its place, and replaced by a symlink to the
  original;
- R17 (the cross-checks on hand-edited files);
- R18 (byte identity from `run()`, a per-frame `flush_log()`, a small `buffer_frames` and `replay`, for the
  notional transport and the divisor-10 fixture, a halting run included);
- R18b (one cleanup rule through `_runkit`, `montecarlo`, `scenario` and `replay`, with the Monte Carlo kill
  test waiting on the `.partial` name). Its collision case checks that `discard_log()` removes the owned
  `.partial` and leaves the foreign `log.path` untouched. Two more cases are added: a published log that is then
  discarded, and a mismatch that deletes nothing and reports it;
- R19b (the flush cost, measured before and after).

PR 2 also confirms two claims: the chunk index type (item 5) and the `frame` column claim for halted runs
(item 6). It tests that `load(log.path)` succeeds while the `Simulation` is still alive, which shows the kept
descriptor holds no HDF5 file lock (item 1). R16's kill
tests accept a published log for a kill after the publication point (item 5).

This record's own check: `python3 scripts/docs_check.py`, `git diff --check`, an adversarial Codex review and
Joe's acceptance. Not covered: durability across power loss or an OS crash, readers other than h5py, and
filesystems without `RENAME_NOREPLACE` (they refuse to publish).

## Approval

Accepted by Joe, 2026-09-29.
