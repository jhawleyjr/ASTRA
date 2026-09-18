# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M2 PR 3b (the NESC ten-case harness), worktree `m2-nesc-harness`, branch `worktree-m2-nesc-harness`,
uncommitted (34 changed paths, nothing committed/pushed, no PR opened). Spec/plan approved; ADR-0014
accepted and written. Cases 4/5's EOM/architecture finding (frozen wrench across RK4 sub-stages,
exposed by fast spin + real drag) **decided by Joe, 2026-09-18, option (a)**: accepted as a disclosed
gap, `GTEST_SKIP()`ed in `nesc_cases_test.cpp` (not deleted, no tolerance change), `PLAN.md`'s new
**PR 3c** (per-RK4-substage wrench evaluation) is the closer, not started here. Full gate
(`just check`) passed clean on all 4 presets after fixing format/ruff/pyright/clang-tidy findings the
first run surfaced (line-length, `zip(strict=)`, `modernize-use-std-numbers` false positives on
generated data, `readability-identifier-naming` on the test file's PascalCase helpers). Codex general
review: clean, no defects. Codex adversarial review: **did not complete** — hit a hard usage-limit
error mid-pass ("try again at Sep 23rd, 2026 7:30 PM"), not a finding, not a pass. Human-review packet
prepared: `docs/reviews/nesc-harness-human.md`, discloses the adversarial-review gap plainly.

## Last verified result
`just check` full gate: **`check: OK`**, exit 0, all 4 presets, 283 tests each, `100% tests passed`
(8 Nesc cases pass, 2 skipped, matching every preset); pyright 0 errors; pytest 32 passed. Codex
`review --scope working-tree`: "No actionable defects found." Codex `adversarial-review --scope
working-tree`: incomplete (usage limit), raw logs at `/tmp/codex_review.log` /
`/tmp/codex_adversarial.log` (session-local, not yet copied into retained evidence).

## Next step
**Joe**: read `docs/reviews/nesc-harness-human.md` (the packet) and decide. The one gate this session
could not close is the adversarial review (Codex quota, resumable ~2026-09-23) — everything else is
real, current evidence. Options: wait for quota and resume adversarial review before any merge
decision; decide the candidate is acceptable on general review + the two rounds of plan-stage Codex
critique already done; or something else. Not committing/pushing/opening a PR without further
instruction (none given yet).

## Tried / failed
Correction rounds: 0/2 (general review found nothing to fix; adversarial review never completed, so
no findings exist yet to fix). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
