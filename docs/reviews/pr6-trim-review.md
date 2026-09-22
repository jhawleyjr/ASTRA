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
Verification: builder's own repro (inject regression -> red; revert -> green) before commit;
**independently reproduced and confirmed by Codex's adversarial pass** (base `cc11c30`, head
`4b6f398`): "R-001 is independently verified fixed: the test calls the actual helper, and an
in-memory quaternion sign mutation fails it."
History: initial (Codex general review) -> fixed same round (`4b6f398`) -> verified (Codex
adversarial pass, same head).

## Adversarial review (Codex, base `cc11c30`, head `4b6f398`)

Verdict: **approve**. Focus: D8 residual-slice correctness, the collapsed-quaternion special case,
the R3 wind-axis identity (`T cos(alpha) = D`) and its independent hand-computed cross-check,
determinism/hidden-state across repeated `derivative()`/`write_input()` calls, the `earth/flat`
position-representation guard, and the accepted control-name-hardcoding deviation plus the
scipy/pyright `cast()` workaround. No material findings. Specific results: D8 extracts indices
`[3,4,5,10,11,12]` correctly against `derivative(state)`'s named layout; the quaternion collapse
matches `frames.hpp`; `T cos(alpha) = D` is the correct identity; the test's independent ISA-1976
density omits the geopotential/geometric-altitude distinction, an immaterial ~0.195% error against
the check's 5% bound (noted, not a defect); repeated probe calls and two separate
`trim_straight_and_level` invocations produced bit-identical results; both YAML spellings of
`physical.earth` are rejected by the flat-Earth guard before `Simulation` construction; no
additional correctness risk in the disclosed control-name deviation or the typing casts. One
environment-only artifact reported, not a repository defect: the reviewer's own sandboxed
`pytest tests/python -q` run showed 15 setup errors from an unwritable temp directory in its
execution environment (this session's own local run, and the full four-preset gate at `641ff4a`,
both show 84/84 clean — not reproducible outside the reviewer's own sandbox).

## Completion

One finding total (R-001, minor), fixed and independently reviewer-verified. Zero findings from
either the general or the adversarial pass beyond R-001. Full gate not rerun for the R-001 fix
(Python-only change, no C++ touched) — `pytest`/`ruff`/`pyright` rerun clean at each step, and the
full four-preset gate remains green at `641ff4a` (last full run, before these Python-only fix
commits). Reviewer recommendation ("approve") is not Joe's approval; proceeding to `/human-review`.
