# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7: `docs/plans/m7-f16-nesc-oracle.md` (Joe's later decisions are its dated amendments), P1 and P2 inventoried
   in `docs/plans/m7-f16-inventory.md`; ADR-0028 to ADR-0033 are accepted.
2. PR 9 (cases 11 and 12 open loop) is eligible, from a new worktree off `origin/main`; Joe removes `m7-pr7`
   and `m7-pr14`. Its aero channels come from PR 7's `tests/physics/nesc/independent_evaluator.hpp` (Q5's route).
3. Waiting: PR 13 (ADR-0031; the F-16 declares Mach ≤ 1.0) on PR 9; PR 15 (the tutorial) on PR 12.
4. Every new plugin under `plugins/` or `aircraft/` (PR 11a's `nesc_control` peer included) needs a key block
   before `just docs-check` passes, and a `just new-aircraft` copy needs `python3 -I -B scripts/config_keys.py`
   run (architecture §18 step 1); a new tracked config builds, or `test_config_keys.py` declares its refusal.
5. On Joe's go: PR 3 (Earth/gravity rotation consistency; own plan review; bumps `kCoreApiVersion` past 11).

## Open threads
- Carried to PR plans (the M7 plan's "Obligations" table, and the addendum's §10):
  - PR 11a: the laws read the Earth-relative rate (ADR-0029 Decision 7); 13.4's base point latches from the
    frame-0 exchange; R11's and R22's fixtures (ω_ie_b subtraction, `LATLON_NOT_MODELED` refusal), R11's floor
    and saturation margin, R12's ρ, and whether the peer refuses a non-rotating Earth at init;
  - PR 11b: whether `rate_frame.py` runs on case 15 as a reported confirmation (ADR-0029 Decision 7);
  - PR 9: whether the engine's geoid altitude, read against the ellipsoid, holds (the addendum's D8, ADR-0033),
    and whether its configs' throttle is checked, since the engine extrapolates outside 0..1 (6b's decision 4).
    Its harness calls the evaluator's `copy_inputs()` after each sample's `run(1)`.
- The first PR that edits `tests/physics/nesc/independent_evaluator.hpp` fixes its header comment's wrap
  artifact, in the second bullet ("is / still / that frame's") (#116's follow-up, Joe 2026-10-11).
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
  It also carries the trim callers' move to `n_frame` and the notional aero's stale "identical" sentence.
- Trim (M7 PR 5): `rate_convention` is an option on every consumer, `zero_inertial` the default (Joe's (b));
  PR 9 to 12's F-16 configs and harness select `n_frame`, and each F-16 case's full lateral residual (the
  aero rate terms) is derived from the DML and brought to Joe in PR 9, 11a and 11b.
- The F-16 band headers (M7 PR 8, #106): PR 9, 11a and 11b's harness asserts only samples from
  `Band::kFirstGatedSample` on, logs the rest, wraps longitude and yaw, and never cites a zero-accepting channel.
- Lessons: arm the oracle guard in the turn that finishes the guarded writes (2026-10-09); copy a failed gate's
  full log to its per-head name before any rerun (2026-10-10); a ctest `-R 'A\|B'` selects nothing (2026-10-11).
- PLAN.md's items, each Joe's call: (unscheduled) `kJ2`'s source; `disarm-goldens.sh`; the review record (the PR
  body only, M6 PR 10's D3); the docs-check range (until a gate PR fixes it, run `git diff --check <base>..HEAD`
  before review); CI's docs-check paths; the equal-aspect canvas and the plotly.js floor; the non-scalar map key
  (exit 70, not 65); (M9) the ICD v2 Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
