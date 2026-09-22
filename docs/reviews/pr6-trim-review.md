# Review: M3 PR 6 — astra.trim

Spec/plan: `docs/plans/pr6-trim.md` (Revision 2, approved)
Builder: Claude (Sonnet 5)
Reviewer: Codex (installed companion 1.0.6; fresh context; general review)
Scope: base `origin/main` (`cc11c30`), head `64ab385` (branch diff: `python/astra/trim.py`,
`tests/python/test_trim.py`, `pyproject.toml`, plus PLAN.md/STATE.md/plan-doc bookkeeping)
Validation: `pytest tests/python/test_trim.py` 7/7, `ruff`/`pyright` clean at head `64ab385`; full
four-preset gate last run green at `641ff4a` (before three implementation-review fixes landed in
`46c9a95`/`64ab385`, which touched no C++ and were re-validated by `pytest`/`ruff`/`pyright` only,
per Joe's own instruction not to rerun the full gate unless review found something)
Round: initial

## Finding R-001

Severity: minor
Status: verified
Location: `tests/python/test_trim.py:142-143` (at head `64ab385`)
Requirement/ADR: `docs/plans/pr6-trim.md`'s supporting-check spec ("the collapsed-quaternion helper
(`_state_from_alpha`'s `q_nb` construction) matches `Simulation.from_config`'s own real
construction ... proving the collapsed formula against the actual bound implementation")
Failure: `test_state_from_alpha_matches_the_real_quaternion_construction` never called
`trim._state_from_alpha` at all — it compared `Simulation.from_config`'s real quaternion against a
second, independently hand-typed `[cos(alpha/2), 0, sin(alpha/2), 0]` expression, not the helper's
own output. A sign/component-order/indexing regression in `_state_from_alpha` would leave this
dedicated check green. Confirmed by injecting exactly such a regression (negating the `sin(half)`
term in `_state_from_alpha`) — the test still passed before the fix.
Requested outcome: call `trim._state_from_alpha(...)` and compare its own quaternion slice against
the real binding's construction.
Builder response: fixed — `helper_state = trim._state_from_alpha(np.zeros(3), 100.0, alpha_rad)`;
compares `helper_state`'s quaternion slice to `real_q`. Re-verified the injected-regression repro
now fails as expected, then reverted the injection; full `pytest tests/python` (84/84), `ruff`,
`pyright` clean at the fix.
Verification: builder's own repro (inject regression -> red; revert -> green) before commit; pending
Codex's own fix-verification pass (see below).
History: initial (Codex general review, this round) -> fixed same round, pending reviewer
verification.

## Completion

One finding (R-001, minor, fixed by the builder, reviewer verification pending in the same
dispatch as the adversarial pass below — not yet independently confirmed by Codex at the time this
file was first written). No other findings from the general pass. Full gate not rerun for this
fix (Python-only change, no C++ touched); `pytest`/`ruff`/`pyright` rerun clean. Adversarial review
(focus: D8 residual slices, collapsed-quaternion special case, R3's wind-axis identity, determinism/
no hidden state, the earth/flat guard, the accepted control-name-hardcoding deviation, the scipy/
pyright cast workaround) dispatched separately; findings appended below once returned.
