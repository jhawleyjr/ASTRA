# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 7b, R9's planted cause in the notional transport with its negative control, closing C-08,
   per `docs/plans/m6-pr7-mc-sizing.md` D11 and its "Probe evidence". Joe's approved numbers:
   - `cm_alpha_per_rad` uniform [−0.6, +0.6], 10 s, and `max.v0.derived.alpha > 0.23`;
   - minimum effect: KS D ≥ 0.5, Spearman ρ ≥ +0.5, margin 0.25, BH at q = 0.05;
   - no inert-parameter BH assertion in the main batch; the control flags nothing.
   Sizing:
   - a 120-run pilot on its own seed (7071), sized with `astra.mc_sizing`'s ranking option (top for the main
     batch, null at 0.3 for the control);
   - the pilots deleted after sizing, then final batches of N runs on seed 707;
   - stop and ask Joe if 2 × N × 5.6 MB exceeds 4 GB (N > 357).
   R9 stays in `just check`, and its config is also checked through the PyYAML route.
2. Then PR 8, the report (`astra.report`), which renders `astra.mc_sizing.Table` among its sections. Each PR's
   `/wrap` runs the docs step in branch scope.

## Open threads
- PR 8 (the report) inherits C-12's report part (`docs/plans/m6-pr5-plots.md` D7): matplotlib's counter ids
  and glyph ids repeat across SVG files, so SVGs inlined into one report need namespacing.
- PR 8 also inherits PLAN.md's (M6) open item on `tests/python/resource_refs.py` (an approved set).
- Any later plot PR: PLAN.md's (M6) plot open items (ground-track aspect, the TAS/CAS panel, HTML size).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
