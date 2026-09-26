# M4 PR 6 mutation checks on `7c7725d`

Each mutation was applied to the candidate `7c7725d` in the worktree (a scratchpad script, one exact
replacement each), the named test was run, and the file was restored with `git checkout -- <file>`.
M2 changes the autopilot, so `build/python` was rebuilt with `just python` before and after; after
the revert and rebuild, `test_r1_lockstep_health` passed again (2 passed). The tree was clean after
the last revert (`git status --short` showed only the untracked evidence files).

| ID | Mutation | Command | Result |
|---|---|---|---|
| M1 | light config `transport.limits.elevator` `[-0.3, 0.3]` to `[-0.3, 0.31]` | `ctest --preset asan -R ClosedLoopAcceptance.ConfigLimits --no-tests=error` | caught: exit 8, "Expected equality ... 0.31 ... 0.3" |
| M2 | autopilot holds on `ACCEL_INVALID` as well (frame 0 held) | `.venv/bin/pytest tests/python/test_lockstep.py -k r1_lockstep` | caught: 2 failed on `peer_summary["held"] == 0` (light and moderate) |
| M3 | R12b compares frame k's ICD with frame k+1's state | `... -k r12b` | caught: both runs fail, every field beyond 1e-12 (e.g. theta 5.6e-7, v 5.3e-4 at light) |
| M4 | jitter signaler sends signal 0 instead of SIGSTOP | `... -k r3_bit` | caught: `stops_inside_run` 0 < 20 |
| M5 | `load_config`'s `timeout_ms` override written to `handshake_timeout_ms` | `... -k "r4_wait or r4_sigkill"` | caught: 2 failed (the sim waits its 1000 ms default) |
| M6 | `lockstep_session` never stops the peer (`peer.stop` removed) | `... -k session` | caught: 6 failed (peer not reaped: bad-args, SIGKILL escalation, body exception, stopped peer, KeyboardInterrupt, retained registry) |
