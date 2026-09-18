# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow-v3 review/fix round, branch `workflow-v3`, on top of committed `b7978e3` (base
`origin/main` `40b484b`); this round's changes committed separately, not pushed, no PR opened,
PR 3b not started. See `docs/reviews/workflow-v3-review.md` (now two rounds recorded there).
Claude plans/builds/validates/fixes/integrates; Codex reviews/adversarially reviews; Joe approves/merges.
This round: completed R-002/R-003 (round 1's fresh review), D5 (oracle-guard regex, Joe
authorized), Step 2/3 template edits (spec assumptions/system-context; plan boundary/evidence/
conflicts), and approved cleanup (3 empty placeholder READMEs removed, `docs/README.md`
de-linked accordingly, `architecture.md`'s stale skill list fixed, `scripts/tokens.py` + its
test retired per Joe: the fable→opus/sonnet migration measurement it existed for is done).

## Last verified result
Full local gate (`./scripts/check-full.sh`, log `build/validation/full.log`, gitignored), run
three times this session: (1) first D1-D4/D6-D8 pass failed at `ruff` (real `SIM102`), fixed;
(2) after R-002/R-003 fixes, green; (3) after D5/cleanup/tokens.py, a first attempt correctly
caught `tests/python/test_tokens_script.py` orphaned by an incomplete tokens.py removal —
restored, resolved with Joe, re-deleted both together, reran: all four presets 274/274,
`hooks-test` 56/56 (up from 51: D5 added 5 new guard-oracle.cases), `docs-check` OK (24 files,
down from 27 after the README deletions), ruff/pyright(0 errors)/pytest(26 passed, down from
30) clean, `EXITSTATUS=0`, `check: OK`. LeakSanitizer completes without error in this session's
actual execution environment; the earlier ptrace failure in `docs/lessons.md` describes a
different one and is left as-is. Fresh Codex review of `origin/main`..`b7978e3` found and this
round fixed: R-003 (`bash -n a b` only parses `a`); R-001/R-002 from round 1 verified/completed.

## Next step
Commit this round; dispatch fresh Codex general + adversarial review of the complete candidate
(everything since `b7978e3`), supplying R-001/R-002/R-003's dispositions for verification —
this closes fix-round 1's loop (1/2 used) if clean, or opens round 2 if not. Then `/human-review`
packet; Joe decides on further commits/push/draft PR. After acceptance, resume `PLAN.md`'s M2
PR 3b (NESC harness); recheck current `main` and prerequisites first.

## Tried / failed
Correction rounds: 1/2; failed integrations: 0/2.
This session's only failures were self-caught before commit: a `ruff` finding, and an
incomplete-removal test failure — neither reached a reviewed candidate or required a fix round.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
