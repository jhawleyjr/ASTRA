# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 8, the report (`astra.report`, `astra report`; `docs/plans/m6-analysis-tooling.md` R13 and
   C-11 to C-13). It renders `astra.mc_sizing.Table` among its sections. Plan it with `/plan-feature` first.
2. Then PR 10, M6's closeout (PLAN.md check-off with evidence links, architecture §12 status, open items for
   held-out scope). Each PR's `/wrap` runs the docs step in branch scope, before the final gate where it can
   (workflow.md, Verification: after a passing gate, a Markdown/evidence-only change needs only the docs checks).

## Open threads
- PR 8 (the report) inherits C-12's report part (`docs/plans/m6-pr5-plots.md` D7): matplotlib's counter ids
  and glyph ids repeat across SVG files, so SVGs inlined into one report need namespacing.
- PR 8 also inherits PLAN.md's (M6) open item on `tests/python/resource_refs.py` (an approved set).
- Any later plot PR: PLAN.md's (M6) plot open items (ground-track aspect, the TAS/CAS panel, HTML size).
- PLAN.md's (M6) PyYAML item: R9's `cruise.yaml` is covered; the other modules' configs are not.
- PLAN.md's (M7) envelope-monitor item is unblocked (`alpha_max_rad = 0.23` from the source's CLalpha peak would
  make R9's criterion a halt); a Tier 3 PR of its own, scheduled by Joe.
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
