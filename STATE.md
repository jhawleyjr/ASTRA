# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M7 (`docs/plans/m7-f16-nesc-oracle.md`), with P1 and P2 inventoried in `docs/plans/m7-f16-inventory.md`.
   ADR-0028 to ADR-0032 are accepted; Joe's later decisions are dated amendments in the M7 plan.
2. On Joe's go, any of:
   - PR 2: `Config` cycle safety;
   - PR 3: Earth/gravity rotation consistency, with its own plan review (it bumps `kCoreApiVersion` past 10);
   - PR 4: `mass/constant`'s product-of-inertia convention.
3. PR 6b (`aircraft/f16`) becomes eligible once PR 3b and PR 4 are both merged. Its aero reads
   `air_data::omega_eb_b` (ADR-0032). PR 13 implements ADR-0031; the F-16 declares Mach ≤ 1.0.

## Open threads
- Carried to PR plans (the M7 plan's "Obligations" table, and the addendum's §10):
  - PR 11a: the laws read the Earth-relative rate (ADR-0029 Decision 7: the rule said "inconsistent", and Joe
    decided on the evidence), and 13.4's base point latches from the frame-0 exchange. It carries R11's and
    R22's fixtures (the ω_ie_b subtraction, the `LATLON_NOT_MODELED` refusal), R11's floor and saturation
    margin, R12's ρ (no atmosphere signal), and whether the peer refuses a non-rotating Earth at init;
  - PR 11b: whether `rate_frame.py` runs on case 15 as a reported confirmation (ADR-0029 Decision 7);
  - PR 6b: the aero's MRC-to-CM transfer (1.132 ft, CM at 25 % MAC) and the DML's mass, 637.1595 slug (R1);
  - PR 7: the latched `Vehicle::environment()` keeps `omega_ie_b` NaN, so its evaluator sets the field from the
    sim's Earth model at the frame-start state; its plan decides whether to share `vehicle.cpp`'s file-local
    `earth_rate_b` (with its +0.0 canonicalization).
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
  It also carries the trim callers' move to `n_frame` and the notional aero's stale "identical" sentence.
- Trim (M7 PR 5): `rate_convention` is an option on every consumer, `zero_inertial` the default (Joe's (b));
  PR 9 to 12's F-16 configs and harness select `n_frame`, and each F-16 case's full lateral residual (the
  aero rate terms) is derived from the DML and brought to Joe in PR 9, 11a and 11b.
- The F-16 band headers exist (M7 PR 8, #106): PR 9, 11a and 11b's harness asserts only samples from
  `Band::kFirstGatedSample` on, logs the earlier ones, wraps longitude as well as yaw, and never cites a
  zero-accepting channel (listed in #106's body) as evidence.
- The oracle guard: a compaction disarms it, so arm it right before the guarded writes, in a turn that
  finishes them (`docs/lessons.md` 2026-10-09). PLAN.md's (unscheduled) `disarm-goldens.sh` item is Joe's.
- PLAN.md's (unscheduled) items, each Joe's call: `kJ2`'s source; the review record (the PR body only, M6 PR
  10's D3); the docs-check range (until a gate PR fixes it, run `git diff --check <base>..HEAD` before
  review); the equal-aspect canvas and the plotly.js floor.
- PLAN.md's (M9) ICD v2 Earth-rate item (ADR-0029 Decision 7), and its (M11) frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
