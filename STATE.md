# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**ADR-0019/0020/0021 accepted by Joe, 2026-09-20**, moved into `docs/decisions/`, with
superseded-by notes on ADR-0000/ADR-0003/ADR-0018. `docs/architecture.md` §1/§6/§7/§18 and
`PLAN.md`'s M3 section rewritten to match. **PLAN.md M3 PR 3d (ADR-0020) implemented and
approved**: `AeroModel`/`PropulsionModel` are minimal marker interfaces;
`Vehicle::prepare_stage_environment` restores the no-silent-NaN guarantee core-side, per Joe's own
Q4 design (domain pre-check before sampling) — resolves PLAN.md's own "Atmosphere-domain-check
misattribution window" item too. `kCoreApiVersion` 6→7. Two Codex review rounds (general +
adversarial) found and fixed two real issues; one re-verify confirmed both, zero open findings.

**Joe's follow-up (documentation of domain-off/nan-off interaction, per-stage counting, full-gate
rerun to a retained log, packet fixes) is committed** (`1aad091`, `b4864b9`) and pushed. PR #40
("M3: framework realignment (ADR-0019/0020/0021) + PR 3d implementation") is retitled, pushed, and
marked **ready for review** (not draft): https://github.com/jhawleyjr/ASTRA/pull/40. Per Joe's
explicit instruction ("mark it ready, and stop"), no further action was taken on PR 3d/the
realignment ADRs this session. Joe squash-merges himself; not yet merged as of this handoff.

## Last verified result
Full gate (`./scripts/check-full.sh`): **`check: OK`**, exit 0, this session — retained log path
and exact counts (343/343 tests × 4 presets, 0 clang-tidy/pyright findings, 38/38 pytest) are in
`docs/reviews/pr3d-adr0020-human.md`'s own evidence table.

## Next step
After Joe's squash-merge of PR #40: rebuild `docs/plans/pr4-notional-aero-turbofan.md` (PLAN.md PR
4, notional aero/turbofan) against the corrected `wrench_b()`-direct interface — keep it
simplest-possible with notional numbers — and show Joe before implementing. Do not start this
before the merge is confirmed. PR 4a (ADR-0021 implementation) remains independent, not started.

## Tried / failed
Correction rounds: 1/2 used this session (PR 3d's own Codex review/fix/re-verify batch — both
findings fixed and confirmed in one round). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
