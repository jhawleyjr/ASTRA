# Review: M6 PR 4, units (the angle pair, the bound set, fgbridge onto it)

Spec/plan: [PR plan](../plans/m6-pr4-units.md) (D1 to D6, Joe's revisions 1 to 5); [M6 plan](../plans/m6-analysis-tooling.md)
Q20, R21, C-15; [ADR-0002](../decisions/0002-si-units-and-signal-metadata.md); ADR-0009 (`kCoreApiVersion`).
Builder: Claude (Opus 5.5)
Reviewer: Codex (`gpt-6-astra`, `model_reasoning_effort = "high"` in the local Codex config; companion 1.0.6; fresh
context; adversarial review focused on the exactness claims, the binding's array semantics and fgbridge's packet
identity)
Scope: committed branch diff `origin/main` `daea24d` to `86d4568` (PR #82): code candidate `c2fc49f` plus its gate
summary; worktree clean. Paths: `core/include/astra/util/units.hpp`, `tests/unit/util/units_test.cpp`,
`python/bindings/_core.cpp`, `python/astra/fgbridge.py`, `tests/python/test_units.py`,
`tests/python/test_fgbridge_units.py`, `PLAN.md`, `docs/plans/m6-pr4-units.md`, `docs/plans/m6-analysis-tooling.md`.
Validation: on `c2fc49f`, `./scripts/check-full.sh` from a fresh configure exit 0 with `check: OK`
([summary](evidence/c2fc49f-gate-summary.log)); `ctest --preset asan -R Units --no-tests=error` 16/16;
`.venv/bin/pytest tests/python -k 'units or fgbridge'` 584 passed. Fix 1's validation is in the PR body.
Round: pass 1 (thread `01a0f4b7-2232-77e0-993c-faa389e50264`, needs-attention); fix 1 (this commit), its Codex
verification pending.

## Finding [R-001]

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `python/bindings/_core.cpp:207-214` at `c2fc49f` (`def_conversion`'s float overload)
Requirement/ADR: plan D2 and Joe's revision 4 (a size-1 int64 array, and every other non-float64, non-1-D array, is
refused with `TypeError`); ADR-0002.
Failure: the float overload used nanobind's `double` caster, which falls back to `PyFloat_AsDouble` in its
conversion pass. `pyproject.toml` allows `numpy>=2`, and numpy before 2.4 converts a size-1 array of any dtype or
shape to a float with only a `DeprecationWarning`. Reproduced by Claude with numpy 2.3.5 (the one in Codex's
runtime, put first on `PYTHONPATH`, warnings not errors): `radians_to_degrees` of `np.array([3])`,
`np.array([True])`, a size-1 float32 array, a size-1 `'>f8'` array and `np.ones((1, 1))` each returned a Python
float. The suite missed it: its warnings-as-errors setting turned the warning into the refusal, and numpy 2.5.3
refuses anyway. The plan's revision-4 note ("a numpy that only warned ... the test catches") holds only under
pytest, not in production.
Requested outcome: refuse ndim >= 1 before scalar conversion, keeping the approved scalar and 0-d behavior; test
the refusals with `DeprecationWarning` ignored.
Builder response: fixed. The float overload's argument is now `UnitsScalar`, whose caster refuses any object whose
`ndim` is not the integer 0 before delegating to nanobind's `double` caster (exact floats skip the check). The stub
still reads `float`. Tests:
- `test_other_arrays_are_refused` now ignores `DeprecationWarning` and adds size-1 bool, float32, `'>f8'` and a
  1×1 array.
- A new `_ArrayShaped` object (an `ndim` and a working `__float__`) stands in for numpy before 2.4, independent of
  the installed numpy: ndim 1, 2, `"1"` and `1.0` are refused, and ndim 0 converts like a float.
- Shown to catch the defect: with the `ndim` check deleted and the module rebuilt, `test_units.py` failed 24 cases
  on numpy 2.5.3 and 54 on numpy 2.3.5. With the fix, all 165 passed on both.
Verification: pending (fix-verification pass).
History: pass 1 open; fix 1, open to fixed-unverified.
Human exception if needed: none.

## Finding [R-002]

Severity: minor (Codex: medium)
Status: fixed-unverified (contract corrected; the requested support is rejected, pending Joe, D7)
Location: `python/bindings/_core.cpp:193-199` at `c2fc49f` (`convert_array`'s argument)
Requirement/ADR: plan, Interface, the array overload's "any strides".
Failure: a float64 field of a packed structured dtype (`[('tag', 'u1'), ('angle', 'f8')]`) is a native 1-D
float64 view with a 9-byte stride. nanobind's importer requires byte strides divisible by the item size, so
`radians_to_degrees` raises `TypeError`. Reproduced by Claude on numpy 2.5.3 and 2.3.5. It is a loud refusal, never
a wrong value: a size-1 packed field (stride unused) is accepted and converted correctly.
Requested outcome: Codex recommends supporting such views (byte strides, unaligned reads through `memcpy`), with
tests for positive and negative non-item-size strides.
Builder response: the finding is accepted: "any strides" overclaimed. The recommended support is not built.
- Joe's brief allowed each odd input to be "handled or refused", and a refusal is explicit.
- No consumer produces such a view: log columns are float64 arrays from HDF5 or pandas, whose strides are whole
  elements.
- Supporting it means bypassing nanobind's importer with a hand-written buffer-protocol reader.
The contract now reads "any stride that is a whole number of elements", in `convert_array`'s comment and in the
plan. `test_other_arrays_are_refused` pins the refusal for the packed field at stride 9 and reversed (stride -9).
`test_a_size_one_packed_field_is_accepted_because_its_stride_is_never_used` pins the accepted case. D7 asks Joe to
confirm refusal over support.
Verification: pending (fix-verification pass; the rejection of the recommended support needs the reviewer's view
and Joe's decision).
History: pass 1 open; fix 1, open to fixed-unverified (contract narrowed to refusal).
Human exception if needed: pending, D7.

## Completion

Pending fix 1's Codex verification. Pass 1 confirmed the numerical pins, found 20,000 old-versus-new packet
comparisons identical, confirmed that `tests/python/test_fgbridge.py` and `tests/golden/**` are unchanged and that
`kCoreApiVersion` correctly stays 9, and made no edits. Reviewer recommendation is not Joe's approval.
