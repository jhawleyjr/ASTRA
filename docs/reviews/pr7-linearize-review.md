# Review: M3 PR 7 — `astra.linearize`

Spec/plan: `docs/plans/pr7-linearize.md` (Revision 3, D11–D18)
Builder: Claude (Sonnet 5)
Reviewer: Codex (installed companion 1.0.6; fresh context; general + adversarial)
Scope: base `53064e0` (PR 6 merge), head `a155c9f` at review time (candidate `009715a` plus
evidence/plan-note commits); `python/bindings/_core.cpp`, `python/astra/linearize.py`,
`tests/python/test_linearize.py`
Validation: full gate green at `009715a` (`docs/reviews/evidence/009715a-full-gate.log` — four
presets 387/387, hooks 144/144, pyright 0, pytest 97 passed + 1 expected `xfail`); fix batch
re-validated at `0fa5e85` (full gate green, pytest 98 passed + 1 expected `xfail`, log below)
Round: initial

## Finding R-001

Severity: major
Status: verified
Location: `python/astra/linearize.py` (pre-fix `linearize()`, `u0` construction and the B-matrix
loop, base `53064e0`..`a155c9f`)
Requirement/ADR: D11 (`docs/plans/pr7-linearize.md`) — `u` must be read from
`Simulation.input_names()`'s own registration order, not assumed
Failure: `u0` and each `B` column's step were built assuming a fixed position order
`[elevator, aileron, rudder, throttle]` matched `sim.input_names()`'s own runtime order. A
`vehicle.models` config listing the turbofan plugins before the aero plugin (Codex's own
reproduction: reordering only existing models) makes `input_names()` return throttle first, so
`_evaluate()` silently wrote the trim elevator value to the throttle input and vice versa —
trim's own residual stayed `< 1e-16` but the reordered config's linearization residual was
`~1.33`, computing `A`/`B` about the wrong operating point with no error raised.
Requested outcome: map `u0` and each control step to `Simulation.input_names()` by name, not
position; reject an unsupported/unexpected set of registered names rather than silently
misaligning.
Builder response: fixed, `0fa5e85`. `linearize()` now builds `control_trim_values`/
`control_steps` dicts keyed by dotted control name, validates
`set(input_names) == set(_CONTROL_NAMES)` before use, and constructs `u0`/each `B` column via
`input_names[j]` lookups. `control_step()`'s own signature changed from `index: int` to
`name: str` to match; the R6 column-isolation test (`test_input_registry_reads_trim_values_after_
any_column`) updated to call it by name. Verified directly (not only by the existing cruise.yaml
test, which happens to already list aero before the turbofans): built a copy of `cruise.yaml`
with `vehicle.models` reordered turbofans-first and confirmed `input_names() ==
["controls.throttle", "controls.elevator", "controls.aileron", "controls.rudder"]`, `u0` matches
that order, and the trim-point residual is `8.33e-17` (previously reproduced at `~1.33` before
the fix) — this ad hoc reordered-config check is not itself part of the committed test suite
(cruise.yaml's own real order is what `test_input_names_returns_the_four_registered_control_
names`, R7, already pins), recorded here as the fix's own verification evidence.
Verification: Codex (fresh context, `adversarial-review --base 53064e0`, base..head
`53064e0`..`3210dd1`), verdict **approve**: "trim values and B-column steps follow registered
names; unsupported control sets are rejected, and the registry guarantees uniqueness."
History: round 1 (initial review), found; round 1 (this same session), fixed; round 1
fix-verification, confirmed fixed.

## Finding R-002

Severity: medium
Status: verified
Location: `tests/python/test_linearize.py:307-333` (pre-fix
`test_phugoid_matches_the_approximation`, base `53064e0`..`a155c9f`)
Requirement/ADR: D13/D18 (`docs/plans/pr7-linearize.md`) — the phugoid damping bound (0.1 absolute)
is a real, independent acceptance requirement (R4b), not conditional on the frequency check
Failure: `@pytest.mark.xfail(strict=True)` covered the whole test function with no `raises=`
restriction. Two consequences: (1) `strict=True` rejects an unexpected *pass* but still accepts
*any* exception as the expected failure, so an unrelated bug (e.g. a `TypeError` from a typo)
would also read as the known, expected miss; (2) the `omega_n` assertion always raised first
(the known, disclosed miss), so the `zeta` assertion on the next line never executed at all —
phugoid damping was not actually being validated despite D13's own tolerance existing for it.
Requested outcome: validate damping in an unmarked test that always runs; restrict the `xfail` to
the specific, known frequency-tolerance assertion, with `raises=` set narrowly enough that an
unrelated failure surfaces as a real error instead of a silent expected-failure.
Builder response: fixed, `0fa5e85`. Split into `test_phugoid_zeta_matches_the_approximation`
(unmarked, always runs; numerical `zeta = 0.0520` vs the closed form's exact `0.0` — passes, `0.052`
absolute, inside the `0.1` bound) and `test_phugoid_omega_n_matches_the_approximation`
(`xfail(strict=True, raises=AssertionError)`, scoped to the one known-miss assertion — any
non-`AssertionError` failure, or an unexpected pass, now surfaces instead of being absorbed).
Verification: Codex (fresh context, `adversarial-review --base 53064e0`, base..head
`53064e0`..`3210dd1`), verdict **approve**: "damping is checked independently without xfail; the
frequency-only test retains strict=True and restricts expected failures to AssertionError."
History: round 1 (adversarial review), found; round 1 (this same session), fixed; round 1
fix-verification, confirmed fixed.

## Completion

Two findings (R-001 major, R-002 medium), both fixed this session (`0fa5e85`) and both confirmed
fixed by Codex's own fix-verification pass (fresh context, base `53064e0`..head `3210dd1`,
verdict **approve**, "New findings: none" — one correction round, 1/2 of the bounded budget).
No other findings from either original pass: the general review's own six focus areas beyond
R-001, and the adversarial review's own five focus areas beyond R-002 (Euler-angle permutation,
D16 sign discipline, D18's independent re-derivation, `LinearizeResult.simulation` ownership/
lifetime) came back clean — "No substantive defect found in the other five focus areas"
(adversarial), no further findings listed (general). Full gate re-run green after the fix batch
(`0fa5e85`): four presets 387/387, pyright 0, `pytest tests/python/` 98 passed + 1 expected
`xfail`. Zero open/critical/major findings remain.
