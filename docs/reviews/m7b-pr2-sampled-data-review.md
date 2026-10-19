# Review: M7b PR 2, oracle (a), the exact sampled-data closed-loop check

Spec/plan: [docs/plans/m7b-pr2-sampled-data.md](../plans/m7b-pr2-sampled-data.md); the
[M7b plan](../plans/m7b-closed-loop-oracles.md), oracle (a); ADR-0034.
Builder: Claude (Opus 5.5).
Reviewer: Codex, companion 1.0.6, its default model. Fresh context, read-only, two passes:
- the general `review --base origin/main`, thread `01a15148-136f-7bd3-9bc1-6d2d7b818a1c`;
- `adversarial-review --base origin/main` with the PR's focus, thread `01a15149-8ee1-7d40-8c66-9d5752539924`.

Scope: the branch diff `406c407..33aa496`, with a clean tree. The plan's own critique is in the plan's §16.
Validation: the full gate on `6eb01f7`, exit 0 and `check: OK`, 1189 s
([summary](evidence/6eb01f7-gate-summary.log)). The first run's evidence is
[1d7fb2c-sampled-data-first-run.log](evidence/1d7fb2c-sampled-data-first-run.log).
Round: initial, then fix 1 (this file's commit).

**The general review:** no actionable defects. It ran the focused pytest (25 passed) and the smoke executable in
four builds (six tests each). Leak detection was disabled in Codex's sandbox, so that is not a finding.

**The adversarial review:** verdict needs-attention, with two medium findings. Both are verified and accepted.
Codex reported that ctest was blocked by its sandbox's read-only log writes, which is a sandbox limit and not a
finding.

## Finding [R-001]

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `tests/python/test_sampled_data.py:535-541` at `33aa496`, the order control's test
Requirement/ADR: the plan's §9 and §16 (P-01, Joe 2026-10-18): every control but the wrong divisor registers §7's
full-sequence check as a pass. ADR-0034 Decision 7: narrowing a comparison counts as widening a tolerance.
Failure: the test computed `sequence_ok` but never asserted it. Codex's in-memory probe removed the final request
and reply, and the test still passed, so the final-exchange comparison was dropped for this control.
Requested outcome: assert `checks.sequence_ok` for the order control.
Builder response: fixed. The order test now asserts `checks.sequence_ok`. The other three plant controls already
asserted it, and the wrong divisor asserts its registered failure. No constant, bound, window or margin changed.
Verification: pending the Codex verification pass.
History: round 1, found by the adversarial review and fixed in the same round.
Human exception if needed: none.

## Finding [R-002]

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `tests/python/test_sampled_data.py:469-472` at `33aa496`, `_plant_control`, and the wrong-divisor and
order tests
Requirement/ADR: the plan's §8, whose comparison set runs parse identity and completion for every run; ADR-0034
Decision 7.
Failure: `_assert_parse_identity` ran only in the nominal tests, so the five control runs omitted it. Codex's
probe gave the order test a completed run with B = 99 in place of the registered 100. Parse identity would
have rejected it, but the order control's assertions passed, accepting a coefficient mismatch as the intended
truncation failure.
Requested outcome: assert parse identity for every control against its actual config text, and keep the
separate nominal reference that judges the wrong divisor.
Builder response: fixed.
- `_plant_control` asserts parse identity against the plant control's own config text.
- The wrong-divisor test does the same against its own text, divisor 11, while `evaluate` still judges it
  against nominal N = 10's reference.
- The order test asserts it against the order config.
- No constant, bound, window or margin changed.
Verification: pending the Codex verification pass.
History: round 1, found by the adversarial review and fixed in the same round.
Human exception if needed: none.

## Completion

- **Open:** no critical finding. Two major findings are fixed but not yet verified. None was rejected or
  deferred.
- **After the fix,** with the 25 tests run again: `.venv/bin/pytest tests/python -k sampled_data`, 25 passed in
  15.07 s.
- **Still to do:** the full gate on the fix commit, then Codex's focused verification of R-001 and R-002 and of
  the fix delta.
- **Unregistered, by the plan:** the order control's L and request outcomes are computed but not asserted. The
  plan's §9 registers "nothing else" for that control.
- The reviewer's recommendation is not Joe's approval.
