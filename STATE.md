# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Case05 gravity centripetal-term fix (`PLAN.md`'s "Investigate case05's altitude/v_down anomaly",
now checked off). Worktree `m2-case05-gravity`, base `main`'s `41d2c1a` (current tip, no advance
since). **Uncommitted working-tree diff, no branch push, no PR** — not yet authorized to commit/
open a draft PR; that is the next decision, not assumed. Spec/plan `docs/plans/
nesc-case05-gravity-centripetal.md`, approved by Joe 2026-09-19 (D1: `rotating` bool config key on
`gravity/inverse_square`, mirrors `earth/round`'s own key/constant; D2: option (a), disclosed only,
with a new unchecked `PLAN.md` follow-up item for the deferred init-time consistency check).

## Last verified result
Full validation and both required review passes complete and clean, this session. `./scripts/
check-full.sh` (`just check`): `check: OK`, exit 0, run twice (first run failed `format-check` on
the new test file, fixed with `clang-format-18 -i`, reran clean) — 288/288 tests on asan, pyright 0
errors, pytest 32/32. All four presets: `physics.Nesc` 8 passed/2 skipped/0 failed each;
gravity-regression suite 47/47 each. Skip-lifted local dry run (asan, restored, `git diff` confirms
zero net change on `nesc_cases_test.cpp`): case05's altitude/v_down/temperature now at case04's own
pre-existing miss level (within ~1%, from three orders of magnitude worse); density/pressure now
pass cleanly in both cases; no channel regressed. Codex general review: no actionable defects.
Codex adversarial review (tier-3 physics focus): verdict **approve**, no material findings — caught
one accuracy overclaim in the builder's own reporting ("4 significant figures" vs. the actual ~1%
agreement), fixed same round in the plan/`PLAN.md`/`nesc-harness.md`/packet. Full record:
`docs/reviews/nesc-case05-gravity-centripetal-review.md`. Human-review packet prepared:
`docs/reviews/nesc-case05-gravity-centripetal-human.md`.

## Next step
**Joe**: read the packet and decide. If approved, the candidate still needs committing to the task
branch and a draft PR opened (not done yet — this session did not commit/push without explicit
authorization). Cases 4/5 stay `GTEST_SKIP()`ed either way (PR 3c's own separate, unstarted scope,
unaffected by this fix). D2(b) (earth/gravity rotation-consistency check at init) is a new unchecked
`PLAN.md` item, not started, needs its own plan review before code.

## Tried / failed
Correction rounds: 0/2 (no findings needed a fix round; the one reporting overclaim was corrected
inline, not a code defect). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
