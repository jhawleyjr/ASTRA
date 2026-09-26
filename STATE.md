# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 7 (M4 closeout), Tier 0 to 1, docs only: branch `worktree-m4-pr7-closeout` off `origin/main` `0fcae33` (PR 6
#57). PLAN.md: M4 checked off with each clause cited to PR 6's evidence (packet
`docs/reviews/m4-pr6-lockstep-acceptance-human.md`, `docs/reviews/evidence/4e418e1-*`), PR 1 to PR 7 sub-items,
the duplicate §15 envelope-monitor bullet removed, the open M4 follow-ups added as items. Not pushed (stop before push
per Joe's assignment); Joe decides push, PR and squash-merge. PR 7 is merged once `origin/main` contains it.
Plan row: `docs/plans/m4-lockstep.md` PR 7 (A1). Stage: A1 and Codex review done; Joe's review next.

## Last verified result
This session, on candidate `a2e9215`: A1 `python3 scripts/docs_check.py` exit 0 (`docs-check: OK`; it does not scan
PLAN.md/STATE.md), `git diff --check` exit 0. Codex general review, `--base origin/main` (`0fcae33..a2e9215`, thread
`01a0dfbe-f9f8-7fa0-83ea-ded2ec63602d`): exit 0, no actionable defects, acceptance summary matches the cited evidence
(`docs/reviews/evidence/a2e9215-codex-review.log`). Later commits: this record only. No product, test, golden,
tolerance or ADR file changed, so no gate was run for this PR. M4's
acceptance evidence is prior-session (PR 6, `4e418e1`): full gate exit 0, `check: OK`
(`docs/reviews/evidence/4e418e1-full-gate.log`); A6 pytest 42 passed, ctest `ClosedLoopAcceptance` 4/4.
Correction: the PR 6 packet's "trace SVGs not rendered" note is superseded; Joe rendered and checked them during his
review of PR 6.

## Next step
1. Joe: push, open the PR and squash-merge (or send changes back).
2. After that, the next task is Joe's to assign: M5 Tooling planning (`/plan-feature`, fresh worktree off
   `origin/main`) or one of the PLAN.md follow-ups.
3. Open follow-ups, all now PLAN.md items: moderate throttle jitter from airspeed gusts (revisit with a sensor or
   actuator model); `OPTIONAL_GAIN_NAMES`/`kSideslip` removal (whoever next touches the gains); follow-ups A (PR 2
   CAS comment nits), B (frames citations; Joe arms the guard), C (recorder sizing for an absurd `sim.frames`); the
   wall-clock symbol scan; Joe-owned: the stale `tests/golden/dynamics/capture_r1_golden.cpp`. The throttle SVG label
   overlap is recorded in the M4 entry as cosmetic, left as is.
4. Noticed, not edited (architecture is Joe's): `docs/architecture.md` §7's reference-plugin list still says the
   loopback and shm transports are "not yet built".

## Tried / failed
Correction rounds: 0/2; failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
