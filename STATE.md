# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7 (`docs/plans/m7-f16-nesc-oracle.md`), with P1 and P2 inventoried in `docs/plans/m7-f16-inventory.md`
   (M7 PR 6a). Joe's decisions there (§10, 2026-10-08) are folded into the M7 plan as dated amendments.
2. On Joe's go, any of:
   - PR 1b: ADR-0029 (the sim-side peer) and ADR-0032 (the body rate relative to the Earth, from the core);
   - PR 2: `Config` cycle safety;
   - PR 3: Earth/gravity rotation consistency, with its own plan review;
   - PR 4: `mass/constant`'s product-of-inertia convention.
3. Then PR 3b (ADR-0032 in the core, after Joe accepts it), and PR 6b (`aircraft/f16`, after PR 1a, 3b, 4).
   PR 13 implements ADR-0031; the F-16 declares Mach ≤ 1.0.

## Open threads
- Carried to PR plans (the M7 plan's "Obligations" table, and the addendum's §10):
  - PR 1b and 11a: the laws' rate frame. Try to decide it from data: back out el, ail, rdr from the
    members' aero channels at 13.3 samples (15 if needed), evaluate the DML law with inertial and with
    Earth-relative rates, compare. If that cannot discriminate, it returns to Joe, default ICD v1's inertial rates;
  - PR 6b: the aero's MRC-to-CM transfer (1.132 ft, CM at 25 % MAC) and the DML's mass, 637.1595 slug (R1);
  - PR 11a's R11 floor and saturation-boundary margin, and where R12's ρ comes from (no atmosphere signal).
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
  It now also carries the notional transport's trim callers' move to `astra.trim`'s `n_frame` (M7 PR 5).
- Trim (M7 PR 5): `rate_convention` is an option on every consumer, `zero_inertial` the default (Joe's (b));
  PR 9 to 12's F-16 configs and harness select `n_frame`, and each F-16 case's full lateral residual (the
  aero rate terms) is derived from the DML and brought to Joe in PR 9, 11a and 11b.
- PLAN.md's (unscheduled) `kJ2` item: its source stated or its value changed, by Joe's decision.
- The F-16 band headers exist (M7 PR 8, #106): PR 9, 11a and 11b's harness asserts only samples from
  `Band::kFirstGatedSample` on, logs the earlier ones, wraps longitude as well as yaw, and never cites a
  zero-accepting channel (listed in #106's body) as evidence.
- PLAN.md's (unscheduled) review-record item: Joe chose the PR body only (M6 PR 10's D3); a Tier 2 workflow
  PR to `review-dispatch.md` and the findings template, scheduled by Joe.
- PLAN.md's (unscheduled) docs-check item: `git diff --check` reads only the uncommitted diff; until a gate PR
  fixes it, run `git diff --check <base>..HEAD` before review.
- PLAN.md's (unscheduled) equal-aspect canvas and plotly.js-floor items (M6 PR 12's D2b, PR 14's D3).
- M11: PLAN.md's (M11) open item on the frame-time benchmark's maximum (isolated CPUs, a fixed governor and a
  stated tail statistic before any maximum is compared).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
