# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7: `docs/plans/m7-f16-nesc-oracle.md` (Joe's later decisions are its dated amendments), P1 and P2 inventoried
   in `docs/plans/m7-f16-inventory.md`; ADR-0028 to ADR-0033 are accepted.
2. Next, each from a new worktree off `origin/main`:
   - the M7 closeout PR (Tier 0): rewrite architecture §16's M7 row to the delivered scope (cases 11 to 13.4
     against the ensemble; 15 and 16, the port and the tutorial unscheduled), with §10's and §11's markers as set;
   - a planning PR, briefed by Joe, for the closed-loop oracles that replace M7 PR 11b, 12 and 15 (PLAN.md's
     unscheduled items): a sampled-data check on a linear plant, the port pattern on M4's autopilot, symmetry tests.
3. PR 13 (ADR-0031; the F-16 declares Mach ≤ 1.0) stays eligible as a framework feature: it sets case 12's
   config's explicit Mach action and the NESC harness's warn-counter policy (the M7 plan's Q16, N-07).
4. Every new plugin under `plugins/` or `aircraft/` needs a key block before `just docs-check` passes, and a
   `just new-aircraft` copy needs `python3 -I -B scripts/config_keys.py` run (architecture §18 step 1); a new
   tracked config builds, or `test_config_keys.py` declares its refusal.
5. On Joe's go: PR 3 (Earth/gravity rotation consistency; own plan review; `kCoreApiVersion` past 11). M7 PR 11a's
   Q4: `nesc_control` cannot see the Earth model and refuses none (Joe); its configs pair it with `wgs84_rotating`,
   which `F16Control.TrackedConfigsPairThePeerWithTheRotatingEarth` enforces.

## Open threads
- M7 PR 11a: R6 not met for 13.3's and 13.4's six aero channels (PLAN.md's line); Q11 amended (`peer.trim.theta`).
  PR 11b's `rate_frame.py` obligation travels with its unscheduled PLAN.md item.
- The F-16 engine's and `EnvironmentSample::altitude_m`'s comments still call D8 "PR 9's question"; answered
  (`docs/plans/m7-pr9-open-loop.md`, Q2: the ellipsoid holds). The next PR to edit either file updates it.
- The first PR that edits `tests/physics/nesc/independent_evaluator.hpp` fixes its wrap artifact, "is / still /
  that frame's" (#116's follow-up).
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
  It also carries the trim callers' move to `n_frame` and the notional aero's stale "identical" sentence.
- Trim (M7 PR 5): `zero_inertial` the default, the F-16 configs `n_frame`. The F-16 bands (M7 PR 8): `f16_flight.hpp`
  gates from `Band::kFirstGatedSample`; never cite a zero-accepting channel.
- Lessons: copy a failed gate's full log to its per-head name before any rerun (2026-10-10); a ctest `-R 'A\|B'`
  selects nothing (2026-10-11); a cross-implementation roundoff bound charges each step at its own result
  (2026-10-13); check `Config::is_sequence()` before iterating a block (2026-10-13).
- PLAN.md's items, each Joe's call: (unscheduled) `kJ2`'s source; `disarm-goldens.sh`; the review record (the PR
  body only, M6 PR 10's D3); the docs-check range (until a gate PR fixes it, run `git diff --check <base>..HEAD`
  before review); CI's docs-check paths; the equal-aspect canvas and the plotly.js floor; the non-scalar map key
  (exit 70, not 65); (M9) the ICD v2 Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
