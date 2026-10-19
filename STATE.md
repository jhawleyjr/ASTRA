# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M0 to M7 are done (PLAN.md's M7 row). M7b (`docs/plans/m7b-closed-loop-oracles.md`): ADR-0034 (PR 1), oracle
   (a) (PR 2) and oracle (b) (PR 3) are in. Each next task starts from a new worktree off `origin/main`.
2. Next, on Joe's go: M7b PR 4, oracle (c), Tier 3: the F-16's mirror and heading symmetry under `F16_control`
   (the M7b plan's row 4, Q9 to Q12, R10 to R15). Its plan pre-registers what ADR-0034's Consequences list;
   PR 3's §5 is the model for its build facts, PR 3's gate (1187 s, `a459cd9`) its base, and per lesson
   2026-10-18 it says how the stop hook's `just test-unit` meets any `unit`-labeled oracle test.
3. Then PR 5 (closeout). Architecture §4's tree and §10's in-process transports do not yet name the autopilot
   peer: PR 5 adds it, unless #124's body records that PR 3 did.
4. M8 planning (M7's PR 13, the envelope item: ADR-0031, the M7 plan's row 13, Q16, R18) and M7's PR 3, the (M9)
   rotation item: each on Joe's go; their order against M7b is his.
5. Every new plugin under `plugins/` or `aircraft/` needs a key block before `just docs-check` passes, and a
   `just new-aircraft` copy needs `python3 -I -B scripts/config_keys.py` run (architecture §18 step 1); a new
   tracked config builds, or `test_config_keys.py` declares its refusal.

## Open threads
- Two code comments still name M7 PR 13 for the envelope work, which moved to M8:
  `aircraft/f16/physical/aero/aero_math.hpp:86` and `aircraft/f16/tests/aero_test.cpp:756`. The next PR to
  edit either file updates it (M7b PR 4 is the likely one).
- The F-16 engine's and `EnvironmentSample::altitude_m`'s comments still call D8 "PR 9's question"; answered
  (`docs/plans/m7-pr9-open-loop.md`, Q2: the ellipsoid holds). The next PR to edit either file updates it.
- The first PR that edits `tests/physics/nesc/independent_evaluator.hpp` fixes its wrap artifact, "is / still /
  that frame's" (#116's follow-up).
- PR 11b's `rate_frame.py` obligation on case 15 stays with its unscheduled item; no M7b oracle flies case 15.
- PLAN.md's (unscheduled) notional-transport rate-frame item (M7 PR 6a's D3 (B2)): measure before Joe decides.
- Trim: `zero_inertial` the default, the F-16 configs `n_frame`; `n_frame` rates come from `lstsq`, so exact
  zeros need explicit projection (M7b's C-01). The F-16 bands gate from `Band::kFirstGatedSample`.
- Lessons: a failed gate's full log keeps its per-head name (2026-10-10); a ctest `-R 'A\|B'` selects nothing
  (2026-10-11); a roundoff bound charges each step at its own result (2026-10-13); `is_sequence()` before
  iterating (2026-10-13); an exact recursion on a double dt outgrows Fraction, a no-slack bound is attained at a
  tie, and the stop hook runs `unit` tests at every turn end (2026-10-18).
- PLAN.md's items, each Joe's call: (unscheduled) the Codex review of M7 PR 6b's diff; `kJ2`'s source;
  `disarm-goldens.sh`; the review record; the docs-check range (run `git diff --check <base>..HEAD` before
  review); CI's docs-check paths; the equal-aspect canvas and the plotly.js floor; the non-scalar map key;
  (M9) the ICD v2 Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
