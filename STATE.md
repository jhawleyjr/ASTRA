# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 7 (`astra.mc_sizing`, convergence, `astra mc size`, and R9's planted cause in the notional
   transport with its negative control), per `docs/plans/m6-analysis-tooling.md`. R9 ranks with
   `astra.mc_analysis`'s KS and Spearman, whose ranking order (p, then |effect|, then spec order) gives the
   strict margin it needs; its range, duration, threshold and minimum effect come back to Joe before the
   first run (Q9). Each PR's `/wrap` runs the docs step in branch scope.

## Open threads
- PR 8 (the report) inherits C-12's report part (`docs/plans/m6-pr5-plots.md` D7): matplotlib's counter ids
  and glyph ids repeat across SVG files, so SVGs inlined into one report need namespacing.
- PR 8 also inherits PLAN.md's (M6) open item on `tests/python/resource_refs.py` (an approved set).
- Any later plot PR: PLAN.md's (M6) plot open items (ground-track aspect, the TAS/CAS panel, HTML size).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
