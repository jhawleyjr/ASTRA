# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7: `docs/plans/m7-f16-nesc-oracle.md` (Joe's later decisions are its dated amendments), P1 and P2 inventoried
   in `docs/plans/m7-f16-inventory.md`; ADR-0028 to ADR-0033 are accepted.
2. PR 6c (the F-16's mass and propulsion from P1) is eligible, from a new worktree `m7-pr6c` off `origin/main`;
   Joe removes `m7-pr6b`. Its short plan cites `docs/plans/m7-pr6b-f16-aircraft.md` (Mass, Propulsion, decisions
   1, 2, 4), its Deviations record #113's merge after commit 4, and its engine reads `altitude_m` (ADR-0033).
   It carries the Obligations table's DML mass, 637.1595 slug (R1).
3. Waiting: PR 7 and PR 14 on PR 6c; PR 9 on PR 7; PR 13 (ADR-0031; the F-16 declares Mach ≤ 1.0) on 6c and 9.
4. On Joe's go: PR 3 (Earth/gravity rotation consistency; own plan review; bumps `kCoreApiVersion` past 11).

## Open threads
- Carried to PR plans (the M7 plan's "Obligations" table, and the addendum's §10):
  - PR 11a: the laws read the Earth-relative rate (ADR-0029 Decision 7); 13.4's base point latches from the
    frame-0 exchange; R11's and R22's fixtures (ω_ie_b subtraction, `LATLON_NOT_MODELED` refusal), R11's floor
    and saturation margin, R12's ρ, and whether the peer refuses a non-rotating Earth at init;
  - PR 11b: whether `rate_frame.py` runs on case 15 as a reported confirmation (ADR-0029 Decision 7);
  - PR 7: the latched `Vehicle::environment()` keeps `omega_ie_b` and `altitude_m` NaN, so its evaluator sets
    both from the sim's Earth model at the frame-start state; its plan decides whether to share `vehicle.cpp`'s
    file-local `earth_rate_b` (with its +0.0 canonicalization);
  - PR 9: whether the engine's geoid altitude, read against the ellipsoid, holds (the addendum's D8, ADR-0033);
  - PR 14: `mass/constant`'s key block states the S&L product-of-inertia convention (R17's reference half).
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
  It also carries the trim callers' move to `n_frame` and the notional aero's stale "identical" sentence.
- Trim (M7 PR 5): `rate_convention` is an option on every consumer, `zero_inertial` the default (Joe's (b));
  PR 9 to 12's F-16 configs and harness select `n_frame`, and each F-16 case's full lateral residual (the
  aero rate terms) is derived from the DML and brought to Joe in PR 9, 11a and 11b.
- The F-16 band headers (M7 PR 8, #106): PR 9, 11a and 11b's harness asserts only samples from
  `Band::kFirstGatedSample` on, logs the rest, wraps longitude and yaw, and never cites a zero-accepting channel.
- Lessons: a compaction disarms the oracle guard, so arm it in the turn that finishes the guarded writes
  (2026-10-09); after a failed gate, copy the full log to its per-head name before any rerun (2026-10-10).
  PLAN.md's (unscheduled) `disarm-goldens.sh` item is Joe's.
- PLAN.md's items, each Joe's call: (unscheduled) `kJ2`'s source; the review record (the PR body only, M6 PR 10's
  D3); the docs-check range (until a gate PR fixes it, run `git diff --check <base>..HEAD` before review); the
  equal-aspect canvas and the plotly.js floor; the non-scalar map key (exit 70, not 65); (M9) the ICD v2
  Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
