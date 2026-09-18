# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow baseline and follow-up audit on `workflow-v3`; local uncommitted changes, no new PR/push.
Claude plans/builds/validates/fixes/integrates; Codex reviews/adversarially reviews; Joe approves/merges.
Added plan-feature, review-cycle and human-review skills; revised wrap; removed the obsolete retro skill.
Corrected mixed/dirty Codex review scope, startup context, bounded Stop feedback and CI outcome reporting.
Local CI and Joe's sole approval remain intentional; no product behavior, oracle or model changes.

## Last verified result
This audit: 51 hook cases passed; four CI success/failure/skipped/local summary scenarios passed.
All four active skill validators, document links, shell syntax and `git diff --check` passed.
Earlier implementation pass: Debug/Release each passed 274 CTest tests; static analysis/formatting,
ruff, pyright and 30 Python tests passed. Those product suites were not rerun for the follow-up audit.
Full gate still NOT green: LeakSanitizer failed under this execution environment at sanitizer discovery.
Codex companion scope behavior was inspected in installed source; a runtime probe hit sandbox EPERM.
No live Claude skill or independent model review is claimed by these metadata/hook/document checks.

## Next step
Run `./scripts/check-full.sh` in a normal local terminal with working LeakSanitizer; retain its log.
Exercise the Claude skills and fresh Codex review in the normal provider session before accepting the workflow.
Review/commit/open a draft PR when authorized; Joe decides merge. See docs/process/workflow.md.
After workflow work, resume PLAN.md's M2 PR 3b (NESC harness); recheck current main and its prerequisites.

## Tried / failed
Fast/full wrappers propagated CTest exit 8 from LeakSanitizer; no sanitizer flags or tests were weakened.
A read-only companion target probe could not spawn git under this sandbox; no review job was dispatched.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
