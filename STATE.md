# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**ADR-0019/0020/0021 accepted by Joe, 2026-09-20**, moved into `docs/decisions/`, with
superseded-by notes on ADR-0000/ADR-0003/ADR-0018. `docs/architecture.md` §1/§6/§7/§18 and
`PLAN.md`'s M3 section rewritten to match. **PLAN.md M3 PR 3d (ADR-0020) implemented**:
`AeroModel`/`PropulsionModel` are minimal marker interfaces; `Vehicle::prepare_stage_environment`
restores the no-silent-NaN guarantee core-side, per Joe's own Q4 design (domain pre-check before
sampling, separating a legitimate `valid_range()` excursion from a genuinely broken model) —
resolves PLAN.md's own "Atmosphere-domain-check misattribution window" item too. `kCoreApiVersion`
6→7. Two Codex review rounds (general + adversarial) found and fixed two real issues (fault
detection conflated with an optional diagnostic name's own emptiness; missing fault-path acceptance
tests); one re-verify confirmed both, zero open findings.

**Joe approved PR 3d, 2026-09-20, pending one follow-up commit** (in progress): document the
domain-off/nan-off interaction and per-stage (up to 4x/frame) counting in
`HealthMonitor::report_non_finite_environment`'s own header comment; rerun the full gate to a
retained log; fix the review packet's stale "not yet pushed" line; trim this file. After that
commit: retitle PR #40 to cover both the realignment ADRs and PR 3d, mark it ready for review, and
stop — Joe squash-merges himself.

## Last verified result
Full gate (`./scripts/check-full.sh`): **`check: OK`**, exit 0 — see
`docs/reviews/pr3d-adr0020-human.md`'s own evidence table for the retained log path and exact
counts (rerun as part of this follow-up commit; the packet cites the latest run).

## Next step
After Joe's squash-merge: rebuild `docs/plans/pr4-notional-aero-turbofan.md` (PLAN.md PR 4, notional
aero/turbofan) against the corrected `wrench_b()`-direct interface — keep it simplest-possible with
notional numbers — and show Joe before implementing. PR 4a (ADR-0021 implementation) remains
independent, not started.

## Tried / failed
Correction rounds: 1/2 used this session (PR 3d's own Codex review/fix/re-verify batch — both
findings fixed and confirmed in one round). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
