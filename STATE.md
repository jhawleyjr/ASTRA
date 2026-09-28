# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 5 (Tier 2): PR #67, branch `worktree-m5-pr5-log` off `origin/main` `3324d1e`, plan
`docs/plans/m5-tooling.md` PR 5 row, binding ADR-0025. Stage: human review; the PR body is the
description Joe reviews. Code candidate `3ff2eac`; later commits add only its evidence file and this
handoff (STATE, a lesson). Scope: `python/astra/log.py` (`load` with its `complete` and consistency
refusals, `replay` with byte identity and the transport/`LOG` refusals), `tests/python/test_log.py`,
h5py 3.16.0 and pandas 3.0.5 pinned, `Session.flush_log`'s docstring, one assertion in
`test_lockstep.py`, and PLAN.md's PR 5 line and finalization-marker open item. No `core/` change.

## Last verified result
This session, at `3ff2eac`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/3ff2eac-gate-summary.log`, which also records the acceptance command
`.venv/bin/pytest tests/python -k 'log or replay'`, exit 0, 34 passed). Codex general review of
`3324d1e..5561510`: no actionable defects (thread id in the PR body). Correction rounds 0 of 2.

## Next step
1. Joe: decide on PR #67. Its "Needs Joe's decision" section lists approval of `3ff2eac`, scheduling of
   the finalization-marker open item (Tier 3, superseding ADR), and classifying an unreadable-object
   file as `IncompleteLogError`. After any later push, rerun CI's `check` with `gh pr ready --undo`
   then `gh pr ready`.
2. M5 PR 6 (Tier 2): `astra.scenario`, `tests/scenarios/conftest.py`, the still-air config, seed
   scenarios (Q14, Q15; bands come back to Joe). It reads the run's log with `astra.log.load` and must
   assert `frames_run == duration/dt` and no unexpected halt (the PLAN.md open item).
3. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #67's body holds them. Joe's PR 4 premise, that the consistency
check backstops a `Simulation` dropped mid-run, was wrong: the destructor writes a consistent file with
`complete = 1` (disclosed and tested; open item). A real failed close leaves a file h5py opens but
cannot read (lessons 2026-09-27). The worktree guard refused compound commands; scratch scripts written
with the Write tool and run by path worked.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
