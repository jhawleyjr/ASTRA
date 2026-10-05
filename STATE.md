# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M6 is checked off in PLAN.md. Next: M7 planning (F-16 and the NESC oracle), with `/plan-feature` from
   PLAN.md's M7 row and its plan questions; Joe launches it. M7 takes the (M7) open items.

## Open threads
- PLAN.md's (unscheduled) review-record item: Joe chose the PR body only (M6 PR 10's D3); a Tier 2 workflow
  PR to `review-dispatch.md` and the findings template, scheduled by Joe.
- PLAN.md's (unscheduled) docs-check item: `git diff --check` reads only the uncommitted diff; until a gate PR
  fixes it, run `git diff --check <base>..HEAD` before review.
- PLAN.md's (M7) core item: `Config::unknown_keys` and `from_dict`'s emitter crash on a recursive alias (M6 PR
  11's accepted residual; two reproducers in the item); a core PR at its own tier.
- PLAN.md's (M7) envelope-monitor item is unblocked (`alpha_max_rad = 0.23` from the source's CLalpha peak would
  make R9's criterion a halt); a Tier 3 PR of its own, scheduled by Joe.
- PLAN.md's (unscheduled) equal-aspect canvas item (M6 PR 12's D2b, deferred by Joe).
- PLAN.md's (unscheduled) plotly.js-floor item: the page's 4.8 MB floor is plotly.js inline; a partial bundle
  would be a new vendored asset, Joe's call (M6 PR 14's D3).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
