# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 6 (turbulence configs, `astra.lockstep`, M4 acceptance R1 to R4b, R12b), Tier 3: **at Joe's human review**.
Candidate `4e418e1` (product and tests) on branch `worktree-m4-pr6-lockstep-acceptance` off `origin/main` `f4de27d`;
later commits (head `2da6abc`) are evidence and records only. **Not pushed, no PR** (stop before push, as instructed).
Plan: `docs/plans/m4-pr6-lockstep-acceptance.md` (revision 3, approved by Joe 2026-09-26). Packet:
`docs/reviews/m4-pr6-lockstep-acceptance-human.md` (decision pending). Findings: `docs/reviews/m4-pr6-lockstep-acceptance-review.md`.

## Last verified result
This session, on `4e418e1` with no edits during the gate: `./scripts/check-full.sh` exit 0, `check: OK` (debug 558,
release 559, static-plugins 558, asan 559; pyright 0 errors; pytest 177; `docs/reviews/evidence/4e418e1-full-gate.log`).
A6 separately, both exit 0: `.venv/bin/pytest tests/python/test_lockstep.py` 42 passed; `ctest --preset asan -R
ClosedLoopAcceptance --no-tests=error` 4/4 (full length 58.6 s). R2 seed 42: light 1.33 m / 0.083 deg, moderate
10.41 m / 0.828 deg; hold peer also inside both bands (moderate heading 1.97 deg); R2d ratios 0.18 / 0.14. Codex
round-2 verification (thread `01a0df61-8094-7690-b094-c1594d502d23`): approve, R-001 to R-003 closed, new findings
none. The `dff89af` gate log is marked CONTAMINATED (edits during its pytest stage): not evidence.

## Next step
1. Joe reviews the packet and decides on `4e418e1`. If approved: Joe (or Claude, if authorized) pushes the branch and
   opens a draft PR with the packet as body; Joe squash-merges. PR 6 is merged once `origin/main` contains it.
2. Then M4 PR 7 from a fresh worktree off it: PLAN.md M4 check-off, remove the duplicate §15 envelope-monitor bullet,
   follow-ups, STATE.
3. Nonblocking follow-up (owner: whoever next touches the gains): remove `OPTIONAL_GAIN_NAMES`/`kSideslip` support from
   `tests/python/autopilot_design.py`.
4. Follow-ups A (PR 2 CAS comment nits), B (frames citations; Joe arms the guard), C (recorder sizing for an absurd
   `sim.frames`), and the wall-clock symbol scan remain as in the PR 5 handoff.
PLAN.md untouched (M4 check-off is PR 7). Joe-owned: the stale `tests/golden/dynamics/capture_r1_golden.cpp`.

## Tried / failed
Correction rounds: 2/2 (Codex R-001 to R-003 on `7c7725d`; round 1 `dff89af`; round 2 `4e418e1`, tests only); failed
integrations: 0/2. The budget is exhausted: any new defect goes to Joe.
Packet disclosures: the R2 bands do not separate hold from the autopilot at seed 42 (R2d is the only gate that does);
trace SVGs not rendered in-session (headless Firefox could not write); timing tests share the machine with other sessions.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
