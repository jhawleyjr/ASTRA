# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 1 (decision records + architecture edits), Tier 3 (policy). Docs only; no product gate (no
execution, build, test, or policy-runtime change). Plan: `docs/plans/m4-lockstep.md`. Branch
`worktree-m4-pr1-adrs` off `origin/main` `e16d26a`, **not pushed**. Review commit: ADR-0022 (exchange
stage), ADR-0023 (monotonic-clock exception to ADR-0010), ADR-0024 (shm layout header/protocol, supersedes
ADR-0007 in part), all `Status: proposed`, Approval pending; architecture §2/§5/§10/§11/§15/§16 (rev 8).
ADR-0007 and ADR-0010 are untouched until the flip commit.
Out-of-row edits (Joe's instruction): `AGENTS.md` line 47 (one clause, ADR-0023), architecture §2
principle 1, plan rows (PR 1 (b) names ADR-0024; PR 3 cites 0022, 0023; PR 4 cites 0007, 0024). Grep of
`.claude/hooks`, `.claude/skills`, `scripts/` for wall-clock-only or byte-for-byte-mirror text: no hits.

## Last verified result
`python3 scripts/docs_check.py` (OK, 24 files) and `git diff --check`: both exit 0 on the pre-commit tree.
Codex adversarial review of `d058a3a`: needs-attention, F1 to F3, fixed in round 1 (plan Q18); the packet is
`docs/reviews/m4-pr1-adrs-human.md`. M3's last
full gate is unchanged (`docs/reviews/evidence/f3c1460-full-gate.log`, `check: OK`); none ran here.

## Next step
Joe reads the packet. Then Joe re-arms `.claude/touch-goldens`; one flip commit (status/banner only) sets
ADR-0022/23/24 to accepted with Approval citing the packet's decision record and adds the "Superseded in
part" banners to ADR-0007 (to 0024) and ADR-0010 (to 0023); rerun A1; only then push and open a draft PR.
PLAN.md is untouched (duplicate §15 bullets, lines 600/605, go in PR 7).

## Open follow-up (not scheduled)
PLAN.md: `tests/golden/dynamics/capture_r1_golden.cpp` (R1/R1-Sim capture harness, M3 PR 4a) is
stale (retired `astra::Eom`/`post_step_renormalize` symbols, ADR-0021). Needs Joe's authorization —
it is under `tests/golden/**`.

## Tried / failed
Correction rounds: 1/2 (Codex adversarial review found three defects in the ADR drafts, all verified and fixed
in the round-1 commit; fix verification pending). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
