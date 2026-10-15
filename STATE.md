# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M0 to M7 are done; PLAN.md's M7 row holds the evidence, including R6 not met for 13.3's and 13.4's six aero
   channels and PR 6b's gate exception. Each next task starts from a new worktree off `origin/main`.
2. Next: the oracle planning PR, briefed by Joe, for the closed-loop oracles in place of M7 PR 11b, 12 and 15
   (PLAN.md's unscheduled items): a sampled-data check on a linear plant, the port pattern on M4's autopilot,
   and symmetry tests. PR 11b's `rate_frame.py` obligation travels with its item.
3. Then M8 planning, starting with M7's PR 13 (the (M8) envelope item; ADR-0031, the M7 plan's row 13, Q16,
   R18). It sets case 12's explicit Mach action (the F-16 declares Mach ≤ 1.0) and the NESC harness's
   warn-counter policy (N-07).
4. On Joe's go: M7's PR 3, the (M9) rotation item (its own plan review; `kCoreApiVersion` past 11; R15).
5. Every new plugin under `plugins/` or `aircraft/` needs a key block before `just docs-check` passes, and a
   `just new-aircraft` copy needs `python3 -I -B scripts/config_keys.py` run (architecture §18 step 1); a new
   tracked config builds, or `test_config_keys.py` declares its refusal.

## Open threads
- Two code comments still name M7 PR 13 for the envelope work, which moved to M8:
  `aircraft/f16/physical/aero/aero_math.hpp:86` and `aircraft/f16/tests/aero_test.cpp:756`. The next PR
  to edit either file updates it.
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
- PLAN.md's items, each Joe's call: (unscheduled) the Codex review of M7 PR 6b's `aircraft/f16` diff; `kJ2`'s
  source; `disarm-goldens.sh`; the review record (the PR body only, M6 PR 10's D3); the docs-check range (until
  a gate PR fixes it, run `git diff --check <base>..HEAD` before review); CI's docs-check paths; the equal-aspect
  canvas and the plotly.js floor; the non-scalar map key (exit 70, not 65); (M9) the ICD v2 Earth-rate item
  (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
