# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 7 (M4 closeout), Tier 0 to 1, docs only: branch `worktree-m4-pr7-closeout` off `origin/main` `0fcae33` (PR 6
#57). First candidate `a2e9215` (record `6086259`): PLAN.md M4 check-off with each clause cited to PR 6's evidence
(packet `docs/reviews/m4-pr6-lockstep-acceptance-human.md`, `docs/reviews/evidence/4e418e1-*`), PR 1 to PR 7
sub-items, duplicate §15 envelope-monitor bullet removed, open M4 follow-ups added as PLAN.md items. Correction
round 1 (Joe's review): `docs/architecture.md` stale M4 status wording (§7 reference plugins, §10 `Transport`
ownership line; Joe-authorized, status only), the moderate hold altitude added to the M4 R2 disclosure. Stage: fixes
done, A1 and Codex delta review recorded below; Joe's review next. Not pushed (stop before push, per Joe). PR 7 is
merged once `origin/main` contains it. Plan row: `docs/plans/m4-lockstep.md` PR 7 (A1).

## Last verified result
This session, first candidate `a2e9215`: A1 exit 0 both; Codex general review (`0fcae33..a2e9215`, thread
`01a0dfbe-f9f8-7fa0-83ea-ded2ec63602d`) no actionable defects (`docs/reviews/evidence/a2e9215-codex-review.log`).
Round 1 candidate: A1 and the Codex delta review (`6086259..HEAD`) are pending until recorded here.
`docs_check.py` does not scan PLAN.md, STATE.md or architecture.md; `git diff --check` covers whitespace only.
No product, test, golden, tolerance or ADR file changed, so no gate was run for this PR. M4's acceptance evidence is
prior-session (PR 6, `4e418e1`): full gate exit 0, `check: OK` (`docs/reviews/evidence/4e418e1-full-gate.log`); A6
pytest 42 passed, ctest `ClosedLoopAcceptance` 4/4. The PR 6 packet's "trace SVGs not rendered" note is superseded:
Joe rendered and checked them during his review of PR 6.

## Next step
1. Joe: review round 1, then push, open the PR and squash-merge (or send changes back).
2. After Joe's squash-merge, the next PR adds PR 7's merge commit to its PLAN.md sub-item and to the M4 entry (M3 PR
   9/10 precedent); the next task otherwise is Joe's to assign (M5 Tooling planning via `/plan-feature` from a fresh
   worktree off `origin/main`, or a PLAN.md follow-up).
3. Open follow-ups, all PLAN.md items: moderate throttle jitter from airspeed gusts; `OPTIONAL_GAIN_NAMES`/`kSideslip`
   removal; follow-ups A (PR 2 CAS comment nits), B (frames citations; Joe arms the guard), C (recorder sizing); the
   wall-clock symbol scan; Joe-owned: the stale `tests/golden/dynamics/capture_r1_golden.cpp`. The throttle SVG
   label overlap is recorded in the M4 entry as cosmetic, left as is.

## Tried / failed
Correction rounds: 1/2 (round 1: Joe's review of `6086259`); failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
