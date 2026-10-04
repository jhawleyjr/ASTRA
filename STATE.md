# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 13, the batch envelope's failing-run overlays (PLAN.md's (M6) overlay item). Then, in Joe's
   order (2026-10-03): PR 14, the plotly page's size (PLAN.md's (M6) 25 MB item); then PR 10, M6's closeout
   (`docs/plans/m6-analysis-tooling.md`, the PR 10 row; Tier 0; its `/wrap` runs the docs step in **repo
   scope**).

## Open threads
- PLAN.md's (M6) review-record item: a fix's final Codex verification has no place in
  `docs/reviews/*-review.md` under the no-record-only-commit rule; it needs Joe's choice of where it goes.
- PLAN.md's (M7) core item: `Config::unknown_keys` and `from_dict`'s emitter crash on a recursive alias (M6 PR
  11's accepted residual; two reproducers in the item); a core PR at its own tier.
- PLAN.md's (M7) envelope-monitor item is unblocked (`alpha_max_rad = 0.23` from the source's CLalpha peak would
  make R9's criterion a halt); a Tier 3 PR of its own, scheduled by Joe.
- PLAN.md's (unscheduled) equal-aspect canvas item (M6 PR 12's D2b, deferred by Joe).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
