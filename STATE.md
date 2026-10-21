# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines, never appended)

## Next step
1. M0 to M7b are done (PLAN.md's M7 and M7b rows; architecture rev 17). Each next task starts from a new worktree
   off `origin/main`.
2. Next, each on Joe's go, in his order: M8 planning (M7's PR 13, the envelope item first: ADR-0031, the M7 plan's
   row 13, Q16, R18), and M7's PR 3, the (M9) rotation item.
3. A new sim-side law follows ADR-0029 Decision 8's order (architecture §18 step 5): sim-side peer first, then the
   port proven bitwise against it; an already accepted side is the reference, and every in-house oracle follows
   ADR-0034 (reference, bound fixed before the first run, negative controls).
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
- Gate time: 1294 s at M7b PR 4 (`def7d91`), +293 s against 1001 s; 94 s of PR 4's +107 s fell in untimed steps.
- Lessons: a failed gate's full log keeps its per-head name (2026-10-10); a ctest `-R 'A\|B'` selects nothing
  (2026-10-11); a roundoff bound charges each step at its own result (2026-10-13); `is_sequence()` before
  iterating (2026-10-13); an exact recursion on a double dt outgrows Fraction, a no-slack bound is attained at a
  tie, and the stop hook runs `unit` tests at every turn end (2026-10-18); a yardstick reads 0 on a channel its
  perturbations never move by an ulp, so it needs a perturbation or a floor at their resolution (2026-10-20).
- PLAN.md's items, each Joe's call: (unscheduled) per-step gate timing in `check-full.sh`; the Codex review of
  M7 PR 6b's diff; `kJ2`'s source; `disarm-goldens.sh`; the review record; the docs-check range (run `git diff
  --check <base>..HEAD` before review); CI's docs-check paths; the equal-aspect canvas and the plotly.js floor;
  the non-scalar map key; (M9) the ICD v2 Earth-rate item (ADR-0029 Decision 7); (M11) the frame-time maximum.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
