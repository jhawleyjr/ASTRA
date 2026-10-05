# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7 is planned and approved: `docs/plans/m7-f16-nesc-oracle.md` (Q1 to Q27, R1 to R23, PR 0 to 16).
   Joe places P1 and P2, the NESC F-16 DAVE-ML files from
   `https://nescacademy.nasa.gov/src/flightsim/Models/Atmospheric_models.zip`, in `docs/refs`.
2. Without them, on Joe's go:
   - PR 1a: ADR-0028, ADR-0030 and ADR-0031, with the `docs/decisions/` guard armed by Joe.
   - PR 2: `Config` cycle safety.
   - PR 3: Earth/gravity rotation consistency, with its own plan review.
   - PR 4: `mass/constant`'s product-of-inertia convention.
   - PR 5: trim, measuring every gated caller.
   - PR 8, the band headers (Joe runs the generator under `touch-goldens`), needs only PR 1a.
3. Everything else waits on PR 6a, the inventory addendum built from P1 and P2.

## Open threads
- The M7 plan's "Obligations carried to PR plans":
  - PR 11a's R11 floor and saturation-boundary margin;
  - PR 6a's archive SHA-256 and the `docs/refs/README.md` source correction (the trajectories and
    `Initial_Conditions.xlsx` come from the NESC Academy 2015 page, not NTRS).
- PLAN.md's (unscheduled) review-record item: Joe chose the PR body only (M6 PR 10's D3); a Tier 2 workflow
  PR to `review-dispatch.md` and the findings template, scheduled by Joe.
- PLAN.md's (unscheduled) docs-check item: `git diff --check` reads only the uncommitted diff; until a gate PR
  fixes it, run `git diff --check <base>..HEAD` before review.
- PLAN.md's (unscheduled) equal-aspect canvas item (M6 PR 12's D2b, deferred by Joe).
- PLAN.md's (unscheduled) plotly.js-floor item: the page's 4.8 MB floor is plotly.js inline; a partial bundle
  would be a new vendored asset, Joe's call (M6 PR 14's D3).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
