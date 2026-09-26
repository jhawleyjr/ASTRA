# Review: M4 PR 6, turbulence configs, `astra.lockstep`, and the M4 acceptance runs

Spec/plan: [PR 6 plan, revision 3](../plans/m4-pr6-lockstep-acceptance.md) (approved by Joe 2026-09-26), under the
[M4 plan](../plans/m4-lockstep.md).
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6), fresh context; a general review (thread `01a0df4d-f0f0-7692-983c-fb7f44f69969`)
and an adversarial review (thread `01a0df4d-efdc-7dc0-b1c5-48324b43de16`), both `--base origin/main`, read-only.
Scope: base `f4de27d` (`origin/main`), candidate `7c7725d` (product and tests), head `30611b5` (adds evidence files
only; the adversarial reviewer confirmed this). Committed branch diff; the tree was clean.
Validation: on `7c7725d`, `./scripts/check-full.sh` exit 0, `check: OK` ([log](evidence/7c7725d-full-gate.log)); A6
pytest 38 passed ([log](evidence/7c7725d-a6-pytest.log)); A6 ctest asan 4/4 ([log](evidence/7c7725d-a6-ctest-asan.log));
mutations M1 to M6 caught ([record](evidence/7c7725d-a6-mutations.md)).
Round: initial (round 0); correction round 1 (`dff89af`); correction round 2 of 2 (the last; see Completion).

## Finding [R-001]

Severity: major (reviewer: P1)
Status: fixed-unverified
Location: `python/astra/lockstep.py:566-569` (`lockstep_session`) and `_unlink_segment`, at `7c7725d`
Requirement/ADR: ADR-0024 (the driver unlinks its own uniquely named segment); M4 plan Q5
Failure: `lockstep_session` takes the segment from a caller's config dict without validating it (only `load_config`
checks the `astra_<pid>_<nonce>` pattern). `SHM_DIRECTORY / segment` with an absolute name such as `/tmp/unrelated`
is `/tmp/unrelated`, and the `finally` unlinks it even when the core has rejected the name. Verified by reading the
code: `pathlib` joining an absolute path discards the left side.
Requested outcome: validate the name before launching anything; the unlink never leaves `/dev/shm`.
Builder response: fixed. `lockstep_session` validates the segment against the plugin's pattern before any launch
(`ValueError`, nothing created); `_unlink_segment` refuses any name that does not match the pattern. New tests:
`test_session_rejects_an_unsafe_segment` (absolute, relative-with-slash and malformed names; the would-be target
file survives) and `test_unlink_segment_refuses_an_unsafe_name`.
Verification: Codex round-1 verification (adversarial, fresh, `--base 30611b5`, head `dff89af`, thread
`01a0df53-b342-7ae0-9405-c0ad4e354c62`): **holds** for the session path and the unlink. It noted that
`remove_stale_segments(directory)` takes a directory on purpose (the tests sweep a tmp directory); that is not a
defect: the sweep only unlinks names matching the pattern inside the directory it is given, and the session and
`_unlink_segment` always use `/dev/shm`. The requested outcome is read as "no session or unlink path leaves
`/dev/shm`", which holds.
History: round 0 found (general review); round 1 fixed; round 1 verified.

## Finding [R-002]

Severity: major (reviewer: high)
Status: fixed-unverified
Location: `python/astra/lockstep.py:602-605` (`reap_peer`) and `PeerProcess.stop`, at `7c7725d`
Requirement/ADR: plan C-1 (nested cleanup; every exit path reaps), ADR-0024
Failure: in `reap_peer`, an exception from `peer.wait(clean_exit_s)` skips `peer.stop()`; in `stop()`, a
`KeyboardInterrupt` during the grace wait skips SIGKILL and the reap. Verified by reading the code: both are
sequential statements with no `finally`. The outer wrapper only records the exception. A SIGTERM-ignoring peer
interrupted during cleanup is left running; an interrupted wait can leave a peer unreaped.
Requested outcome: waiting, termination, kill escalation and collection each protected; exceptions preserved until
they finish; coverage for an interrupt during cleanup with a SIGTERM-ignoring peer.
Builder response: fixed. `reap_peer` runs `stop()` in a `finally` after the clean wait. `stop()` escalates to SIGKILL
in a `finally` whenever the peer is still running after the graceful phase, however that phase ended, and the reap
after SIGKILL retries through `KeyboardInterrupt` and re-raises it once the process is collected. A cleanup
`KeyboardInterrupt`/`SystemExit` is re-raised after every cleanup step (chained to the body's exception through
`__context__`) instead of being reduced to a note. New lifecycle case `interrupt-in-cleanup`: a SIGTERM-ignoring
peer, a failed handshake, and a SIGALRM-raised `KeyboardInterrupt` landing inside `stop()`'s grace wait; asserts
`KeyboardInterrupt` propagates, the peer ends -SIGKILL and is reaped, and no segment remains.
Verification: round-1 verification (thread above): the code fix **holds** (every graceful-wait exception path
now escalates and collects; cleanup defers interrupts until its steps finish), but the test did not establish where
the interrupt landed (a timer armed before launch, from assumed 300 ms / 600 ms timing).
Round 2 response: the child now arms the interrupt from inside the grace wait itself (a wrapper around
`PeerProcess.wait` arms a 0.1 s `ITIMER_REAL` only when called by `stop` with the 1 s grace) and the SIGALRM handler
records the interrupted stack; the test asserts the stack contains `stop` at `status = self.wait(grace_s)` with
`wait` innermost, plus the propagation, `ConfigError` context, -SIGKILL and cleanup assertions. Mutation check:
with the escalation in `stop()`'s `finally` disabled, the test fails (the peer is left running and the Reaper kills
it). Pending round-2 verification.
History: round 0 found (adversarial review); round 1 fixed; round 1 verification: code holds, test placement not
proven; round 2 test fixed.

## Finding [R-003]

Severity: major (reviewer: medium)
Status: fixed-unverified
Location: `tests/python/test_lockstep.py:868-877` (`test_r4b_sim_death`) and `:975-985`
(`test_driver_death_after_handshake`), at `7c7725d`
Requirement/ADR: M4 plan R4b; plan C-6 (a live peer at the kill)
Failure: the pre-kill liveness check reads `PeerProcess.returncode`, Popen's cached value, which stays `None` after
the autopilot exits until someone waits. If the sim stalled after `READY` long enough for the autopilot to time out,
pytest would kill the sim, `wait()` would stamp `exited_ns` when it first observed the already-readable pidfd, and the
test would pass with a near-zero interval and status 2. The driver-death test opened the orphan's pidfd only after the
kill. Verified by reading the code.
Requested outcome: prove the peer is running immediately before the kill in both tests (pidfd, not a cached value);
open the orphan's pidfd before the kill; a stalled-sim case proving a pre-kill timeout cannot pass.
Builder response: fixed. One helper, `_kill_while_peer_runs(host, pidfd)`, polls the peer's pidfd with a zero
timeout immediately before taking `t_kill` and killing, and fails with `PeerExitedBeforeKill` if the peer had already
exited. Both death tests hold a pidfd from before the kill (R4b: `PeerProcess` holds it from launch; driver death:
opened as soon as the child reports the pid, while the child is alive and has not reaped it). New test
`test_death_check_rejects_a_peer_that_exited_first`: a sim-only child that stalls after `READY` for longer than
`peer_timeout_ms`, so the autopilot times out first; the helper must raise.
Verification: round-1 verification (thread above): **not closed**. (a) The 250 ms observation-time floor was
unsound both ways: a descheduled sim can make a correct peer fail it, and a peer exiting between the poll and the
kill can still satisfy it if pytest observes the pidfd late. (b) The stalled-sim control called `autopilot.wait()`
first, which reaps the peer and fills `returncode`, so a cached-status check would have passed it too.
Round 2 response: (a) the floor is removed; liveness across the kill is now proved by **stopping the peer**: the
helper `_kill_sim_with_peer_stopped` polls the peer's pidfd (exited: refuse), sends SIGSTOP through the pidfd, waits
until `/proc/<pid>/stat` shows `T` (a zombie or a readable pidfd: refuse), kills the sim host, asserts the host died
by that SIGKILL (-9), then sends SIGCONT. A stopped process cannot exit by itself, so the peer is alive at the kill;
its deadline began at its last reply, before the stop, so kill-to-exit <= `peer_timeout_ms` + allowance still bounds
the requirement (an observation, never earlier than the exit). The stopped peer receives no orphaned-group
SIGHUP/SIGCONT: the sim host is its own session, so its death does not newly orphan the peer's group (kernel
`kill_orphaned_pgrp` needs the exiting parent in the same session as a parent in another group). (b) The control now
waits for the peer's pidfd to become readable **without reaping**, asserts `returncode is None` (the stale cached
status), requires `PeerExitedBeforeKillError`, and checks the host was not killed. Mutation check: with the
liveness proof replaced by "trust the cached status", the control fails (`DID NOT RAISE`). Pending round-2
verification.
History: round 0 found (adversarial review); round 1 fixed; round 1 verification: not closed (floor unsound, control
not live); round 2 fixed.

## Completion

Round 0: three findings (R-001 general; R-002, R-003 adversarial), each checked against the code and accepted; none
rejected. The adversarial reviewer's other targets (the subreaper's reported-pid-only reaping, the READY barrier's
counter check, the notify-pipe ownership, R4 timing, R3 determinism, R12b, the limits and trim and copy checks)
produced no finding. Round 1 verification: R-001 verified; R-002 code verified, test placement not; R-003 not
closed; "New findings: none beyond these verification gaps". Round 2 (the last autonomous correction round) addresses
the R-002 and R-003 gaps in tests only (`lockstep.py` unchanged from `dff89af`); pending round-2 verification. A third
round is not permitted: any remaining gap goes to Joe.

Process note: the full gate started on `dff89af` was still running when the round-2 test edits were made, so its
pytest stage graded a mixed tree; that log is kept as `evidence/dff89af-full-gate-CONTAMINATED.log` and is **not**
evidence for any candidate. The round-2 candidate gets its own gate with no edits during it.
