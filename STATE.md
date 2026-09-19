# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M2 PR 3b (NESC ten-case harness), branch `worktree-m2-nesc-harness`, **committed and pushed**
(10 commits on `main`'s `93592f16`), **draft PR** <https://github.com/jhawleyjr/ASTRA/pull/31> —
**not marked ready**. PR body is the human-review packet (`docs/reviews/nesc-harness-human.md`),
kept in sync via `gh api ... -F body=@file` (`gh pr edit` itself errors on this repo's GraphQL
response, a `projectCards`-deprecation issue unrelated to the edit — use the raw API instead).
Spec/plan approved; ADR-0014 accepted/written. Cases 4/5 (frozen wrench across RK4 sub-stages,
exposed by fast spin + real drag): **decided by Joe, option (a)**, `GTEST_SKIP()`ed, `PLAN.md`'s new
**PR 3c** is the closer, not started here.

Two Joe-requested follow-up rounds on the finding, both docs-only, both in the plan/packet's "Known
gap"/"Joe's attention" sections: (a) `C_D=0` falsification extended to `yaw`/`pitch`/`roll` (collapse
same as `v_east`) and `force_x` (fails for an unrelated reason instead). (b) `v_north`'s own band in
cases 4/5 is entirely SIM 2's t=0 artifact, not physics — "never fails on `v_north`" was unverified
(actual drift there exceeds `v_east`'s own failing value, just inside an inflated tolerance); **PR 3c
must exclude SIM 2 from `v_north` and regenerate before that channel's pass means anything** — plus
two smaller disclosures (loose `tol_rel` on zero-crossing channels; case01/case02 band-width
variance from differing participant sets), neither a code/golden change this round.

## Last verified result
`just check`: `check: OK`, all 4 presets, 283 tests each. Codex `review`: clean. Codex
`adversarial-review`: **did not complete** (hit usage limit; Joe reports quota now reset as of this
handoff, not yet re-run). Raw logs were session-local (`/tmp/*.log`), not copied into retained
evidence.

## Next step
**Joe**: read PR #31 and decide, or ask for adversarial review to be (re)dispatched now that quota is
back — not yet run since the reset. PR stays draft until told otherwise.

## Tried / failed
Correction rounds: 0/2 (nothing to fix yet; adversarial review never completed). Failed
integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
