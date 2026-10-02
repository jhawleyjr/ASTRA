# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 6 (Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze` and `astra mc rerun`), in
   worktree `m6-pr6-mc-analysis`. Then PR 7 (sizing and the planted cause) and PR 8 (the report), per
   `docs/plans/m6-analysis-tooling.md`, then PR 10's closeout. Each `/wrap` runs the docs step in branch scope.

## Open threads
- PR 8 (the report) inherits C-12's report part (`docs/plans/m6-pr5-plots.md` D7): matplotlib's counter ids
  and glyph ids repeat across SVG files, so SVGs inlined into one report need namespacing.
- PR 8 also inherits PLAN.md's (M6) open item on `tests/python/resource_refs.py` (an approved set).
- Any later plot PR: PLAN.md's (M6) plot open items (ground-track aspect, legends over data, the TAS/CAS
  panel, HTML size).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
