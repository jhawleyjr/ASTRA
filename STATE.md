# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 6 (turbulence configs, `astra.lockstep`, M4 acceptance R1 to R4b, R12b), Tier 3: **approved by Joe 2026-09-26**
on candidate `4e418e1` (product and tests), branch `worktree-m4-pr6-lockstep-acceptance` off `origin/main` `f4de27d`;
later commits are documents only. Pushed as a draft PR whose body is the packet (verified with `gh pr view --json body`);
Joe squash-merges. PR 6 is merged once `origin/main` contains it. Plan: `docs/plans/m4-pr6-lockstep-acceptance.md`.
Packet (approved): `docs/reviews/m4-pr6-lockstep-acceptance-human.md`. Findings: `docs/reviews/m4-pr6-lockstep-acceptance-review.md`.

## Last verified result
This session, on `4e418e1` with no edits during the gate: `./scripts/check-full.sh` exit 0, `check: OK` (debug 558,
release 559, static-plugins 558, asan 559; pyright 0 errors; pytest 177; `docs/reviews/evidence/4e418e1-full-gate.log`).
A6 separately, both exit 0: `.venv/bin/pytest tests/python/test_lockstep.py` 42 passed; `ctest --preset asan -R
ClosedLoopAcceptance --no-tests=error` 4/4 (full length 58.6 s). R2 seed 42: light 1.33 m / 0.083 deg, moderate
10.41 m / 0.828 deg; hold peer also inside both bands (moderate heading 1.97 deg); R2d ratios 0.18 / 0.14. Codex
round-2 verification (thread `01a0df61-8094-7690-b094-c1594d502d23`): approve, R-001 to R-003 closed, new findings
none. The `dff89af` gate log is marked CONTAMINATED (edits during its pytest stage): not evidence.

## Next step
1. After Joe's squash-merge is observed on `origin/main`: M4 PR 7 from a fresh worktree off it (PLAN.md M4 check-off,
   remove the duplicate §15 envelope-monitor bullet, follow-ups, STATE).
2. Nonblocking follow-ups (Joe, 2026-09-26): moderate throttle jitter from airspeed gusts through the speed loop (up
   to 0.019 per exchange, no limit reached), revisit when a sensor or actuator model lands; the "trim 0.4986" label
   overlaps the trace in `docs/reviews/evidence/7c7725d-throttle-moderate.svg` (cosmetic, file left as is).
3. Carried: remove `OPTIONAL_GAIN_NAMES`/`kSideslip` from `tests/python/autopilot_design.py` (whoever next touches the
   gains); follow-ups A (PR 2 CAS nits), B (frames citations; Joe arms the guard), C (recorder sizing), the wall-clock
   symbol scan. PLAN.md untouched (PR 7). Joe-owned: the stale `tests/golden/dynamics/capture_r1_golden.cpp`.

## Tried / failed
Correction rounds: 2/2 (Codex R-001 to R-003 on `7c7725d`; round 1 `dff89af`; round 2 `4e418e1`, tests only); failed
integrations: 0/2. The budget is exhausted: any new defect goes to Joe.
Packet disclosures: the R2 bands do not separate hold from the autopilot at seed 42 (R2d is the only gate that does);
trace SVGs not rendered in-session (headless Firefox could not write); timing tests share the machine with other sessions.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
