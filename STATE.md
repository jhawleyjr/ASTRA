# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 1 (decision records + architecture rev 8), Tier 3 (policy), docs only. Branch `worktree-m4-pr1-adrs`
off `origin/main` `e16d26a`, in a draft PR awaiting Joe's squash-merge. ADR-0022 (exchange stage), ADR-0023
(monotonic-clock exception to ADR-0010), ADR-0024 (shm layout header/protocol, supersedes ADR-0007 in part) are
**accepted by Joe 2026-09-24** (packet `docs/reviews/m4-pr1-adrs-human.md`, Decision record approved); ADR-0007
and ADR-0010 carry "Superseded in part" banners. Plan: `docs/plans/m4-lockstep.md` (Q18, Q19 record two Codex
correction rounds). Out-of-row edits: `AGENTS.md` line 47, architecture §2 principle 1, plan rows; no hits in
hooks/skills/scripts.

## Last verified result
`python3 scripts/docs_check.py` (OK, 24 files) and `git diff --check`: exit 0 on the final tree (see PR).
Codex adversarial review plus two fix-verification passes: approve, logs in `docs/reviews/evidence/`
(`*-m4-pr1-codex-*`). No product gate (docs only, per Joe). M3's last full gate: `f3c1460-full-gate.log`.

## Next step
Joe squash-merges PR 1. Then M4 PR 2 (ICD truth packing, Tier 3, binding ADRs 0001/0002/0006; acceptance A2).
PLAN.md is untouched (duplicate §15 bullets, lines 600/605, go in PR 7).

## Open follow-up (not scheduled)
PLAN.md: `tests/golden/dynamics/capture_r1_golden.cpp` (R1/R1-Sim capture harness, M3 PR 4a) is
stale (retired `astra::Eom`/`post_step_renormalize` symbols, ADR-0021). Needs Joe's authorization —
it is under `tests/golden/**`.

## Tried / failed
Correction rounds: 2/2 (round 1: three Codex findings fixed; round 2: Joe's `peer_timeout_ms` change, plan Q19).
Failed integrations: 0/2. No further autonomous correction rounds; a new defect goes to Joe.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
