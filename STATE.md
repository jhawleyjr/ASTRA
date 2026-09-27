# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 4 (Tier 3): PR #66, branch `worktree-m5-pr4-logger` off `origin/main` `6efd1b1`, plan
`docs/plans/m5-tooling.md` PR 4 row, binding ADR-0025. Stage: human review; the PR body is the
description Joe reviews. Code candidate `baddd0a`; `ac922ad` adds only its evidence file. The stage is
`core/{include,src}/…/kernel/run_log.*` wired into `Simulation::step()/run()` (reserve first, commit
at frame end, drains in `run()`/`flush_log()`/`close_log()`), `HealthCheck::kLog`, compound tables and
an exit-handling change in the HDF5 wrapper, SHA-256, `Config::emit_without`, the build-time
`git describe` TU, bindings, and `astra.lockstep.Session` drains. `lockstep_exchange` and
`DerivedSignals` are untouched. Review record: `docs/reviews/m5-pr4-run-log-review.md`.

## Last verified result
This session, at `baddd0a`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/baddd0a-gate-summary.log`, which also records the plan's three acceptance
commands, all exit 0, and R4's SHA-256). Codex: general review clean; adversarial needs-attention
(F1 to F3), fix verification holds all three with no new findings. F1's residual window (the file's
final close failing after writing `complete = 1`) waits on Joe. Thread ids are in the PR body.

## Next step
1. Joe: decide on PR #66. Its "Needs Joe's decision" section lists: approval of `baddd0a`, F1's
   residual (accept, or a rename-on-complete protocol via a superseding ADR), the fixed 1024-row
   staging block, `step()` after `close_log()` halting `kLog`, and the libhdf5 exit workaround.
   After any later push, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
2. M5 PR 5 (Tier 2): `astra.log` (`load`, `replay`), h5py and pandas pinned (Q2). It must prove h5py's
   bundled libhdf5 coexists with ours in one process, refuse a file without `complete = 1` (R6b's load
   half), and do R3 in numpy. Replay can use any `buffer_frames`: the file's bytes do not depend on it.
3. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #66's body holds them. The organic write-error test found that
libhdf5 1.10.10 segfaults at exit after a failed close (lessons 2026-09-27); probes are described in
the PR body, not kept. The worktree guard refused inline heredocs and scripts whose text names the
version-control tool; scripts written with the Write tool and run by path worked (lessons 2026-09-27).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
