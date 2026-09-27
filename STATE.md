# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 Tooling planning, docs only: PR #62, branch `worktree-m5-tooling-plan` off `origin/main` `d3e82e3`, plan
`docs/plans/m5-tooling.md`. Spec and plan approved by Joe 2026-09-26: Q1 to Q24 as recommended, with notes on
Q2 (prove two-libhdf5 coexistence in PR 5; a from-source h5py fallback returns to Joe), Q7 (a dataset-equality
fallback is recorded in ADR-0025 and every test using it), Q15/Q19 (scenario bands and the 64-run duration
return to Joe with evidence) and Q23 (PR 1 narrows AGENTS.md's wall-clock wording). The plan decomposes M5
into nine PRs, each with tier, binding ADRs and acceptance commands.

## Last verified result
This session, on the branch: `python3 scripts/docs_check.py` exit 0 and `git diff --check` exit 0 on the
plan. No build or test ran (docs only). One Codex plan critique (fresh, read-only) ran on `af3292b`; its
twelve findings were verified and all accepted; its ids and verdict are in the PR body and the plan.
Not done: the Tier 0 Codex pass on this PR's final diff; pending unless Joe waives it (the critique covered
the plan's content at `af3292b`, not the final diff). Prior-session: M4 acceptance evidence at `4e418e1`
(`docs/reviews/evidence/4e418e1-*`).

## Next step
1. PR #62: Joe reviews and merges it or waives the final-diff Codex pass first. A push after the PR opened
   needs the `gh pr ready --undo` / `gh pr ready` rerun of CI's `check` before merge.
2. M5 PR 1 (Tier 2): ADR-0025 (run log and replay, Q3 to Q12), ADR-0026 (derived signals, Q13), architecture
   rev 9 (§5, §9, §12 incl. the 62.5 Hz bridge rate, §15 summary, §16 note), AGENTS.md wall-clock wording
   narrowed to `core/`, `plugins/`, `aircraft/`. Joe launches it in a new session with the `docs/decisions/`
   guard armed. Acceptance: `python3 scripts/docs_check.py`, `git diff --check`, one Codex review.
3. PR 2 onward follow the plan's decomposition table; PR 3 and PR 4 wait for ADR-0025/0026's acceptance.
4. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #62's body holds them. Nothing failed; the main checkout's venv holds
a stale `astra._core`, so no Release timing was measured (PR 7 measures it).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
