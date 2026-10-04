# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. Next M6 task: PR 10, M6's closeout (`docs/plans/m6-analysis-tooling.md`, the PR 10 row): PLAN.md's M6
   check-off with evidence links, architecture §12's status, and open items for the held-out scope. It is
   Tier 0. Its `/wrap` runs the docs step in **repo scope**, as at every milestone closeout. Before it, Joe
   decides which of PLAN.md's other (M6) open items stay in M6: the ground-track aspect, the TAS/CAS panel, the
   plotly page's size, the PyYAML route, and the batch envelope's failing-run overlays (from M6 PR 8b's D3;
   the per-column reduction is the bounded option and would change PR 6's overlay check).

## Open threads
- PLAN.md's (M6) PyYAML item: R9's `cruise.yaml` is covered; the other modules' configs are not.
- PLAN.md's (M7) envelope-monitor item is unblocked (`alpha_max_rad = 0.23` from the source's CLalpha peak would
  make R9's criterion a halt); a Tier 3 PR of its own, scheduled by Joe.
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
