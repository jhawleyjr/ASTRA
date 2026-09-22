# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 5** (nanobind: `Simulation`, `Config` from dict, `SignalRegistry` read views, PR 2's write
seam (`sim.write_input(name, value)`, D7), `Simulation::derivative(state)` exposing ADR-0021 R4's
named slices) is starting, worktree `m3-pr5-nanobind` off `origin/main` at `96c9459` (M3 PR 4a,
merged, PR #42). PLAN.md's PR 4a box checked off this session with the merge SHA. Not yet planned:
building the implementation plan via `/plan-feature`, Tier 3, one Codex plan critique required
(ownership/lifetime across the Python/C++ boundary, per PLAN.md's own PR 5 line) before any code.

## Last verified result
Inherited from PR 4a's own local full four-preset gate `check: OK` at head `e0edd16`
(`docs/reviews/evidence/8e5ad1a-full-gate.log`) — no code changed this session yet, so nothing new
to verify. `origin/main` at `96c9459` matches this worktree's branch head; working tree clean before
this session's PLAN.md/STATE.md edits.

## Next step
Write the PR 5 implementation plan (`/plan-feature`): inventory the existing `python/` surface and
every fixed-13-state assumption it carries (ADR-0021 replaced the fixed 13-vector with a length-N
state); map every acceptance clause in PLAN.md's PR 5 line to a named test and command; one Codex
plan critique on ownership/lifetime across the Python/C++ boundary, every finding resolved or
explained, before showing Joe the plan or writing any code.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
