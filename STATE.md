# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 8** (trimmed `cruise.yaml`, 300 s regression trajectory, phugoid cross-check). Tier 3.
Branch `worktree-m3-pr8-cruise-regression`, base `origin/main` `553700f` (confirmed current tip at
packet time); local only, not pushed. Plan `docs/plans/pr8-cruise-regression.md` Revision 2
(approved by Joe 2026-09-22 with five conditions, all met in the packet).

**Stage: pre-capture candidate `ed10734`, reviewed; packet ready.** Packet:
`docs/reviews/pr8-cruise-regression-human.md`; findings: `docs/reviews/pr8-cruise-regression-review.md`
(R-001 and R-002, both verified; Codex final verdict approve, new findings none).
**Golden capture is unarmed; nothing is written under `tests/golden/**` until Joe arms it.**
Acceptance 2 (the frozen trajectory) is open by design.

## Last verified result
`./scripts/check-full.sh` at `ed10734`: `check: OK`, exit 0, 388/388 on each of four presets, pytest
111 passed (`docs/reviews/evidence/ed10734-full-gate.log`). Four-preset capture dry run is
byte-identical with no halt (`docs/reviews/evidence/b4c5f44-capture-dryrun.md`).

## Next step
Joe decides on the pre-capture candidate and arms the capture. Once armed: run
`tests/capture/capture_cruise_regression.sh` (commit B), add `MatchesTheFrozenTrajectoryBitwise`
(commit C), then a renewed full gate and Codex review of B and C (a new finding goes to Joe:
rounds are exhausted), then update the packet and open the draft PR if Joe authorizes it.

## Tried / failed
Correction rounds: 2/2 (R-001, R-002). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
