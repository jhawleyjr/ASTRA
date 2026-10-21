# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M0 to M7 are done (PLAN.md's M7 row). M7b (`docs/plans/m7b-closed-loop-oracles.md`): ADR-0034 and oracles
   (a) to (c) (PRs 1 to 4) are in. Each next task starts from a new worktree off `origin/main`.
2. Next, on Joe's go: M7b PR 5, the closeout (the M7b plan's row 5). It updates architecture §16's M7b row (still
   *Planned*), names the autopilot peer in §4's tree and §10 unless #124's body records PR 3 did, checks PLAN.md's
   M7b item only if every requirement holds, and runs `/wrap`'s docs step at repo scope. It also adds PLAN.md's
   unscheduled item (Joe, 2026-10-21): "check-full.sh prints a wall time per step (each preset's build, tidy,
   pyright, the python build, each CTest run, pytest, the scans), so a gate delta can be attributed; PR 4's +94 s
   fell in untimed steps."
3. M8 planning (M7's PR 13, the envelope item: ADR-0031, the M7 plan's row 13, Q16, R18) and M7's PR 3, the (M9)
   rotation item: each on Joe's go; their order against M7b's closeout is his.
4. Every new plugin under `plugins/` or `aircraft/` needs a key block before `just docs-check` passes, and a
   `just new-aircraft` copy needs `python3 -I -B scripts/config_keys.py` run (architecture §18 step 1); a new
   tracked config builds, or `test_config_keys.py` declares its refusal.

## Open threads
- One code comment still names M7 PR 13 for the envelope work, which moved to M8:
  `aircraft/f16/tests/aero_test.cpp:756`. The next PR to edit that file updates it.
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
  tie, and the stop hook runs `unit` tests at every turn end (2026-10-18); a yardstick reads 0 on a channel its
  perturbations never move by an ulp, so it needs a perturbation or a floor at their resolution (2026-10-20).
- PLAN.md's items, each Joe's call: (unscheduled) the Codex review of M7 PR 6b's diff; `kJ2`'s source;
  `disarm-goldens.sh`; the review record; the docs-check range (run `git diff --check <base>..HEAD` before
  review); CI's docs-check paths; the equal-aspect canvas and the plotly.js floor; the non-scalar map key;
  (M9) the ICD v2 Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time benchmark maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
