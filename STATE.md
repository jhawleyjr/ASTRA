# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 Lockstep: spec/plan approved by Joe with recorded decisions (2026-09-24); no implementation started.
Tier 3 (transport/concurrency, `icd/`, health behavior). Artifact: `docs/plans/m4-lockstep.md` (spec
R1-R12b, PR 1-7 decomposition, decisions, one Codex plan critique, ten findings accepted). Branch
`worktree-m4-lockstep-plan` off `origin/main` `827077b`; the plan is in a draft PR awaiting Joe's
squash-merge (Tier 0: plan + STATE only, no product gate).

## Last verified result
M3 (unchanged; these planning sessions ran no build or test). PR 10's full gate:
`./scripts/check-full.sh` at `f3c1460` after `rm -rf build`: `check: OK`, exit 0, 389/389 × four presets,
pytest 115 passed (`docs/reviews/evidence/f3c1460-full-gate.log`). This session's checks:
`python3 scripts/docs_check.py` and `git diff --check` (results in the handoff message).

## Next step
PR 1 (ADRs + architecture edits, incl. the ADR-0007 superseding amendment, the ADR-0010 clock
exception, and §16's M4 row naming both turbulence levels): needs Joe to authorize `docs/decisions/**`
at launch (`touch-goldens` marker or `ASTRA_TOUCH_GOLDENS=1` on the launch line). All plan questions are
decided; Q7 is settled (light 0.15 m/s / 15 kt, moderate 1.5 m/s / 30 kt, both gate). PLAN.md is untouched (duplicate §15 bullets, lines 600/605, go in PR 7).

## Open follow-up (not scheduled)
PLAN.md: `tests/golden/dynamics/capture_r1_golden.cpp` (R1/R1-Sim capture harness, M3 PR 4a) is
stale (retired `astra::Eom`/`post_step_renormalize` symbols, ADR-0021). Needs Joe's authorization —
it is under `tests/golden/**`.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
