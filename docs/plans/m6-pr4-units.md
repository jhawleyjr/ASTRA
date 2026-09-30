# M6 PR 4: units (the angle pair, the bound set, fgbridge onto it)

Spec: [M6 plan](m6-analysis-tooling.md): Q20, R21, the PR 4 row, C-15 (critique table, and its
verification note in PR #78's body), "Obligations carried to PR plans" (PR 4: C-15). ADR-0002 ("every boundary
conversion is a named function in `core/util/units` with a unit test"). ADR-0009 (`kCoreApiVersion`).
Risk: **Tier 3** (`core/util`). Base: `origin/main` `daea24d` (#81). New worktree/branch `worktree-m6-pr4-units`.
Does not touch `docs/decisions/`, `tests/golden/**` (the FlightGear table keeps its own constants), tolerance
files, `core/include/astra/core_api.hpp.in`, or `tests/python/test_fgbridge.py`.
Correction made during implementation: fgbridge has ten foot fields, not eleven as first written.
Status: **approved by Joe, 2026-09-30, in plan mode, with his revisions 1 to 5 folded in below** (outcomes, not
only bytes, for packet identity; the knot pin and M6/M7; the reference packer replacing the SHA-256 pin; the size-1
int64 and `'>f8'` refusals; the scalar/array asymmetry in D2).

## Context

ADR-0002 puts every boundary conversion in `core/util/units`. PR 5 converts whole log columns for display (R1:
plotted data equals the column after conversion, exactly), and `astra.fgbridge` carries its own `FOOT`/`KNOT`.
This PR adds the angle pair, binds the set, and moves fgbridge onto it with byte-identical packets.

## Repository findings (read at `daea24d`)

- `core/include/astra/util/units.hpp`: `kMetersPerFoot = 0.3048`, `kMetersPerSecondPerKnot = 1852.0 / 3600.0`;
  to-SI multiplies, from-SI divides by the same constant (the pin). Users: `plugins/physical/wind/dryden/*`
  (feet, knots), `tests/physics/dryden_psd_test.cpp`. Tests: `tests/unit/util/units_test.cpp`, `TEST(Units, ...)`
  (ctest names `Units.*`, so `-R Units` selects them).
- No angle conversion exists in `core/`, `plugins/`, `aircraft/` or `python/astra/` (grep for `180.0`,
  `numbers::pi`, `math.degrees/radians`, `np.degrees/radians`, `deg2rad`, `rad2deg`); only tests use them.
- `python/bindings/_core.cpp`: all functions are top-level `m.def`; no submodule. The stub is one generated
  `_core.pyi` (`nanobind_add_stub` without recursion, copied next to `__init__.py`, gitignored); existing
  ndarray returns use a `new double[]` plus capsule (`simulation_derivative`). nanobind 3.0.1, numpy 2.5.3.
- nanobind's `double` caster (`load_f64`): pass 1 accepts only an exact `float`; the convert pass uses
  `PyFloat_AsDouble` (numpy scalars, `int`, 0-d arrays). numpy 2.5.3 raises `TypeError` converting an ndim ≥ 1
  array to a scalar (run: `float(np.array([1]))`), so a 1-D array can never fall through to the float overload.
- `python/astra/fgbridge.py:124-125` `FOOT`/`KNOT`; used only in `pack()` (lines 200, 216-223): ten
  `/ FOOT` and one `/ KNOT`, each a single Python float division, then `struct` packs float32. `pack` receives
  Python floats from both sources (`send_log` line 85 `float(...)`; `LiveBridge` from the registry). Every other
  float literal in the module is `0.0` (AST scan run).
- `pyproject.toml` pytest: `filterwarnings = ["error"]`. `kCoreApiVersion = 9`, twin in `test_package.py:32`.
- Architecture §12 line 422 marks this PR "*planned*"; PR 2 and PR 3 left their markers to PR 10 through the
  PLAN.md "(M6) Architecture ... planned" open item.

## Interface

`core/include/astra/util/units.hpp`, beside the foot and knot pairs (header comment extended):

```cpp
// pi/180, the SI radian per degree. std::numbers::pi is the correctly rounded pi, and this quotient is the
// correctly rounded pi/180 (0x1.1df46a2529d39p-6), checked in exact rational arithmetic (units_test.cpp).
inline constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
[[nodiscard]] constexpr double degrees_to_radians(double degrees) noexcept { return degrees * kRadiansPerDegree; }
[[nodiscard]] constexpr double radians_to_degrees(double radians) noexcept { return radians / kRadiansPerDegree; }
```

Same pin as feet/knots: one named constant, to-SI multiplies, from-SI divides, never a separately rounded
`180/pi`. Header-only; `#include <numbers>`.

**Binding (D1, D2).** Top level of `astra._core`, the six functions under their C++ names, parameter names as in
C++ (`degrees`, `radians`, `feet`, `meters`, `knots`, `meters_per_second`). Constants are **not** bound: a bound
constant invites exactly the Python-side multiply ADR-0002 forbids. Each name has two overloads, float first:

1. `(x: float) -> float`: the C++ function. Accepts what every `_core` float parameter accepts (float, `int`,
   numpy scalars, a 0-d array), converted by Python's float protocol first.
2. `(x: NDArray[float64], 1-D) -> NDArray[float64]`, argument `noconvert()`: any strides (strided, reversed,
   a column of a 2-D array, read-only), read in place; returns a **new** C-contiguous float64 array of the same
   length, element i = the C++ scalar function of element i (one template helper
   `convert_array<&astra::units::f>` looping over `in.view()`). Empty in, empty out. NaN, ±inf, -0.0,
   subnormals pass through the same IEEE operation as the scalar form.
   Refused (`TypeError`, nanobind's no-matching-overload): integer, bool, float32 or any non-float64 array; ndim ≥ 2;
   a list. No implicit dtype cast or copy, so no rounding hides in a conversion.
   Two edge cases, each tested:
   - a **size-1 int64 array** (`np.array([3])`) is refused (`TypeError`). The array overload refuses it
     (`noconvert`). The float overload's convert pass calls `PyFloat_AsDouble`, which numpy 2.5.3 answers with
     `TypeError` ("only 0-dimensional arrays can be converted", run with `-W error` and without). A numpy that only
     warned would turn it into a float, which the test catches: `filterwarnings = error` fails the call, and the
     test also asserts that no float came back.
   - a **big-endian float64 array** (`'>f8'`) is refused (`TypeError`), not byte-swapped. Read in
     `nb_ndarray.cpp`: DLPack raises `BufferError` ("only supports native byte order", run), and the buffer path
     fails on the format `'>d'` on a little-endian host (`skip_first` false, so a two-character format fails).
     The `astype` conversion path runs only in convert mode, which `noconvert` rules out. The float overload
     then raises `TypeError` for ndim 1. A caller with such an array converts it explicitly
     (`a.astype(np.float64)`).
   **Scalar and array asymmetry (D2):** the float overload accepts `int` and `bool` (converted by Python's float
   protocol, so `radians_to_degrees(True)` is `radians_to_degrees(1.0)`) and numpy integer scalars. The array
   overload refuses integer and bool arrays. The float path matches every other `_core` float parameter. The
   array path is where a silent dtype cast would hide.

PR 5 calls `_core.radians_to_degrees(frame[name].to_numpy())` (float64, possibly a strided view): no Python
multiply, no per-sample loop, and each plotted value is bitwise the scalar function's.

**fgbridge.** `FOOT`/`KNOT` removed; `pack()` calls `_core.meters_to_feet(x)` (ten fields) and
`_core.meters_per_second_to_knots(d["cas"])`; docstring's "Constants:" sentence becomes a pointer to
`core/util/units` through `astra._core` (ADR-0002). Scalar overload first, so a Python float hits pass 1.

## Exactness claims (every one run in Python floats on this machine, same single IEEE ops; C++ tests re-run them)

Oracle for constants: a 50-digit decimal π through `fractions.Fraction`, rounded once to double.
Exact:
- `std::numbers::pi` (and `math.pi`) is the correctly rounded π; `pi/180.0` is the correctly rounded π/180,
  `0x1.1df46a2529d39p-6`; `180/pi` (the forbidden reciprocal) is `0x1.ca5dc1a63c1f8p+5`.
- `radians_to_degrees(kRadiansPerDegree) == 1.0` (x/x).
- `degrees_to_radians`: 180 → π, 90 → π/2, 45 → π/4, 360 → 2π, -180 → -π, 1 → the constant.
- `radians_to_degrees`: π → 180, π/2 → 90, π/4 → 45, 2π → 360; 1.0 → `0x1.ca5dc1a63c1f8p+5` (57.29577951308232,
  the correctly rounded 180/π).
- degrees → radians → degrees is exact for **every integer |d| ≤ 14**; d = ±15 is the first failure.
- Specials: ±0.0 keeps its sign, ±inf and NaN propagate, `radians_to_degrees(1e307) == inf`,
  `degrees_to_radians(5e-324) == 0.0`.
Near only (not exact, stated as such in tests):
- `radians_to_degrees(pi/3) == 59.99999999999999` and `(pi/6) == 29.999999999999996`: within 1 ulp of 60 and 30.
- Round trips in general: over 10^6 seeded values in [-1e4, 1e4], deg → rad → deg is inexact for 7.6% and
  rad → deg → rad for 8.5%, never more than 1 ulp. Tests assert ≤ 1 ulp (`std::nextafter`), plus the feet/knots
  style `EXPECT_NEAR` list R21 asks for.
- numpy: `np.deg2rad` matched `d*c` on all 10^6; `np.rad2deg` (multiplies by 180/π) differed from `x/c` on 11.7%.
  numpy is never an oracle here.

## Tests

**C++, `tests/unit/util/units_test.cpp`** (new `TEST(Units, ...)`; existing ones unchanged):
`static_assert(kRadiansPerDegree == 0x1.1df46a2529d39p-6)` (comment names the rational-arithmetic derivation);
known values in both directions (above, `EXPECT_EQ` against `std::numbers::pi` and hex literals); the x/x
identity; the integer |d| ≤ 14 exact round-trip loop; the near cases (≤ 1 ulp, and `!= 60.0`); a
reciprocal-killing pinned value, `radians_to_degrees(1.125) == 0x1.01d4bced81d1bp+6` (the correctly rounded
1.125/c; `1.125 * (180/pi)` gives `...1cp+6`), plus one found the same way for `degrees_to_radians`; specials.
Knots get the same guard, which the suite lacks today. `kMetersPerSecondPerKnot * (1/kMetersPerSecondPerKnot)`
rounds to 1.0 (run), so an x/x identity would survive a precomputed reciprocal, and the knot round trip is
`EXPECT_NEAR`. New pin: `meters_per_second_to_knots(3.0) == 0x1.7537d830a8161p+2` (run; `3.0 * (1/k)` gives
`...62p+2`). Feet are already guarded: `MetersToFeetOfOneFootIsExactlyOne` fails under a reciprocal, because
`0.3048 * (1/0.3048) == 0.9999999999999999` (run). M7 confirms it by running.

**Deliberately wrong implementations** (applied one at a time to `units.hpp`, `ctest --preset asan -R Units`,
reverted; each must fail, the failing test names recorded in the PR body):
M1 `radians_to_degrees = r * (180/pi)` (fails: 1.125 pin, integer round trip at 3, 6, 12);
M2 `degrees_to_radians = d / (180/pi)` (fails: integer round trip at ±13, its pin);
M3 operators swapped (fails: every known value); M4 the constant defined as 180/π (swapped constant; fails the
static_assert and known values); M5 a consistent wrong-scale pair, π/200 (C-15's "inverse pair at the wrong
scale"; fails the known values, which a round-trip-only suite would not check); M6
`meters_per_second_to_knots = mps * (1/kMetersPerSecondPerKnot)` (fails: the 3.0 knot pin); M7
`meters_to_feet = m * (1/kMetersPerFoot)` (fails: `MetersToFeetOfOneFootIsExactlyOne`). Binding mutants for the Python suite: B1 the array
path multiplies by a precomputed reciprocal; B2 two names swapped; B3 the array path's `noconvert` dropped
(int arrays then accepted). Each must fail `pytest -k units`.

**Python, new `tests/python/test_units.py`** (R21 "the bound functions equal the C++ ones"). Independent oracle:
the C++ contract is one correctly rounded multiply or divide by the named constant; the test computes that in
exact rationals, `float(Fraction(x) * C)` / `float(Fraction(x) / C)`, with `C` derived in the test from the
decimal π string (angles), `Fraction(3048, 10000)` and `Fraction(1852, 3600)` rounded once, and asserts each
equals the hex literal the C++ test pins. Not numpy, not `math.pi`, not the binding. (Run: 0 mismatches of
Python's own `x*c`, `x/c` against this oracle over 200k values.)
- scalar: bitwise equality (`struct` bit patterns) over ~20k seeded values across the exponent range, all six
  functions; the known values and specials above; `int` and `np.float32`/`np.float64` scalars equal the function of
  `float(x)`; a 0-d array returns a Python float.
- array: bitwise equal to the scalar function per element for contiguous, `a[::3]`, `a[::-1]`, `m[:, 1]` of a
  C-order 2-D array, read-only; empty → shape (0,) float64; NaN/inf/-0/subnormal elements; result float64,
  C-contiguous, not sharing memory with the input; input bits unchanged.
- refusals, `TypeError` each: int64, bool, float32 arrays, a size-1 int64 array (and no float returned), a
  `'>f8'` array, a 2-D array, a list. Accepted by the float overload: `int`, `bool`, numpy integer scalars.
- no `_core` attribute equals any of the three constants (constants stay unbound).

**fgbridge, new `tests/python/test_fgbridge_units.py`** (`test_fgbridge.py` byte-unchanged, D3):
- *holds no conversion constant*: `ast` scan of `fgbridge.py`: every float literal is `0.0`; no `/` or `*` has a
  numeric-literal operand, except the zero tail's tuple repetition (`(0.0,) * 36`); no `FOOT`/`KNOT`
  attribute; no `degrees`/`radians`/`deg2rad`/`rad2deg` access.
- *dispatch*: monkeypatch `_core.meters_to_feet` and `meters_per_second_to_knots` to sentinels; the ten ft
  fields and `vcas` carry them, the rest do not change.
- *packet identity against a reference packer* (D4). The test holds its own packer, as `test_fgbridge.py`
  holds its own constants. It computes each converted field as `x / 0.3048` or `x / (1852.0 / 3600.0)` in Python
  floats, packs field by field with `struct.pack('>f', ...)`, and zeroes the accelerations while ACCEL_INVALID
  is set. It is compared with `fgbridge.pack()` over 2,000 seeded derived dicts and a fixed special set: NaN,
  ±inf, -0.0, subnormals, values that round to float32's largest finite value or overflow it (≥ 3.5e38 on a
  converted field, and on an unconverted `double` field), and ACCEL_INVALID on and off. **Outcomes are
  compared, not only bytes.** For each input, both give the same bytes or both raise the same exception type.
  `struct.pack('>f', x)` raises `OverflowError` for a finite x beyond float32 range (run: 3.5e38, 1e300), and
  `pack()` does not catch it, so those inputs go through the exception branch. A byte mismatch is reported by
  field name (the reference packs per field, and the test slices `pack()`'s bytes at the same offsets). An
  outcome mismatch names the input and both outcomes.
One-time evidence (PR body): old (`git show daea24d:python/astra/fgbridge.py`, loaded as a module) vs new
`pack()`, same outcome rule (same bytes, or the same exception type), over 10^5 seeded dicts plus the special set,
and every packet of a real round-Earth run through both `send_log` and `LiveBridge`. Required: 0 differing
outcomes. Mechanism: each field was and is one IEEE division by the same double, then the same float32 pack.

## `kCoreApiVersion` and the stub

Unchanged at 9. ADR-0009's rule (core_api.hpp.in) bumps it when the plugin-facing ABI (Model, PluginDescriptor,
StepContext, a vtable) changes; this adds an inline constexpr variable and two constexpr functions to a header,
changing no existing declaration, and dryden's calls are untouched. Evidence: no diff to `core_api.hpp.in` or
`test_package.py`; every plugin-load test passes in the gate. The regenerated `_core.pyi` has `@overload` pairs;
`just pyright` (strict on `python/astra`) runs before the full gate (lesson 2026-09-25).

## Files

- `core/include/astra/util/units.hpp`: angle constant and pair; header comment.
- `tests/unit/util/units_test.cpp`: angle tests (existing tests unchanged).
- `python/bindings/_core.cpp`: include, `convert_array` template, six overload pairs.
- `python/astra/fgbridge.py`: constants removed, `pack()` through `_core`, docstring.
- `tests/python/test_units.py`, `tests/python/test_fgbridge_units.py`: new.
- `docs/plans/m6-pr4-units.md` (this plan); `PLAN.md`: the PR 4 line, and the "(M6) Architecture ... planned"
  open item extended to §12's units bullet (D5); `docs/plans/m6-analysis-tooling.md`: PR 5's obligations row gains
  "C-15 (display-unit dispatch)" (D6).
- `docs/reviews/evidence/<head>-gate-summary.log`; `docs/reviews/m6-pr4-units-review.md` per review-cycle.

## Decisions for Joe (approving the plan approves these)

- D1: top-level names in `astra._core`; constants not bound.
- D2: the array overload takes 1-D native-byte-order float64 only, any strides, with no implicit cast, and
  refuses every other array dtype, byte order or shape. Asymmetry: the float overload accepts `int`, `bool` and
  numpy integer scalars (converted by Python's float protocol); the array overload refuses integer and bool arrays.
- D3: `tests/python/test_fgbridge.py` stays byte-unchanged; new fgbridge tests in their own file.
- D4: packet identity is checked by a reference packer in the test (its own `/ 0.3048` and
  `/ (1852.0 / 3600.0)`), comparing outcomes: the same bytes, or the same exception type. The comparison
  against `daea24d`'s `pack()` is one-time PR-body evidence, not a committed pin.
- D5: architecture §12's "(M6 PR 4, *planned*)" marker is left for PR 10, as PR 2 and PR 3 did.
- D6: C-15's display-unit dispatch half (a unit string mapped to the right function) belongs to `astra.plot`,
  so it is carried to PR 5's plan; this PR closes the independent angle values half.

## Verification and process

1. Worktree off `origin/main` `daea24d`; commit this plan; push; open the draft PR (`docs/templates/pr-description.md`).
2. Implement; `just python`; `ctest --preset asan -R Units --no-tests=error`;
   `.venv/bin/pytest tests/python -k 'units or fgbridge'`; `just pyright`.
3. Mutants M1 to M7 and B1 to B3 (each fails, reverted, `git diff` clean); the fgbridge identity sweep.
4. `rm -rf build`, then `./scripts/check-full.sh` (Bash `run_in_background`), exit 0 and `check: OK`; commit
   `docs/reviews/evidence/<head>-gate-summary.log`; results in the PR body only.
5. `/review-cycle`: adversarial Codex review of the final diff, focused on the exactness claims, the array
   semantics and fgbridge's packet identity; at most two correction rounds; rerun the gate after substantive fixes.
6. `/human-review` for the PR body (checks, mutant table, identity evidence, R21 map, D1 to D6); Joe reads diff
   and tests; `/wrap` before merge.
Rollback: revert the PR (the fgbridge packet tests and the reference-packer test guard it).
