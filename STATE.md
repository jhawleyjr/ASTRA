# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M2 PR 3b (NESC ten-case harness), branch `worktree-m2-nesc-harness`, **committed and pushed**
(13 commits on `main`'s `93592f16`), **draft PR** <https://github.com/jhawleyjr/ASTRA/pull/31> —
**not marked ready**. PR body is the human-review packet (`docs/reviews/nesc-harness-human.md`),
kept in sync via `gh api ... -F body=@file` (`gh pr edit` errors on this repo's GraphQL response, a
`projectCards`-deprecation issue unrelated to the edit). Spec/plan approved; ADR-0014 accepted/
written. Cases 4/5 (frozen wrench across RK4 sub-stages): decided by Joe, option (a),
`GTEST_SKIP()`ed; `PLAN.md`'s new PR 3c is the closer, with two explicit preconditions found this
session (`v_north`'s SIM 2 contamination, `force_y`'s non-discriminating band).

## Last verified result
Adversarial review **complete** (`docs/reviews/nesc-harness-review.md`): re-dispatched 2026-09-19
after Joe reported the Codex quota reset, `--base origin/main` against `4344fa2`. Verdict:
needs-attention, one finding (`force_y` in cases 4/5 is non-discriminating, not just loose —
Codex's own recomputation, independently confirmed by Claude against the committed data).
Confirmed everything else this session disclosed: `v_north`/SIM 2 contamination, the case01/case02
band-width explanation, the frozen-wrench diagnosis, and every frame/unit/sampling/exclusion claim.
**Fixed same round**: `generate_nesc_band.py`'s new `check_non_discriminating()` (ADR-0013 already
required this, never implemented before) flags exactly `case04`/`case05` `force_y` and nothing else;
no golden data changed (diagnostic-only, verified byte-identical). Full gate re-run clean after the
fix: `check: OK`, all 4 presets, 283 tests each. General review (separately, earlier): clean.

## Next step
**Joe**: read PR #31 / the packet and decide. Both required review passes are now complete and
clean (the one finding was fixed and validated same session, not left open). Nothing outstanding on
the review side; the only remaining question is Joe's own merge/next-step decision, plus whatever he
wants to do with the `tol_rel`-globally-dropped ADR recommendation (not actioned, needs his call).

## Tried / failed
Correction rounds: 1/2 (the `force_y` disclosure fix, this session — fixed and validated, not left
open). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
