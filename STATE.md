# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 6 (Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze` and `astra mc rerun`) or
   PR 9 (the frame-time benchmark), per `docs/plans/m6-analysis-tooling.md`. Its `/wrap` runs the docs step in
   branch scope.

## Open threads
- PR 8 (the report) inherits C-12's report part (`docs/plans/m6-pr5-plots.md` D7): matplotlib's counter ids
  and glyph ids repeat across SVG files, so SVGs inlined into one report need namespacing.
- PR 8 also inherits PLAN.md's (M6) open item on `tests/python/resource_refs.py` (an approved set).
- Any later plot PR: PLAN.md's (M6) plot open items (ground-track aspect, legends over data, the TAS/CAS
  panel, HTML size).
- `"attribution": false` (`.claude/settings.json`) was set mid-session, so no session has started under it
  yet: check the first commit of the next session for a co-author trailer
  (`git log -1 --format='%(trailers:key=Co-authored-by)'`). `no-main.sh` refuses one either way.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
