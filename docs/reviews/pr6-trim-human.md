# Human review packet: M3 PR 6 — astra.trim

PR / candidate base and head: base `origin/main` at `cc11c30` (M3 PR 5, merged, #43) — confirmed
current tip, unchanged since this worktree started; head `fd4dbd4` (worktree `worktree-m3-pr6-trim`,
not pushed, no PR opened). Working tree clean at packet time.
Spec/plan/task: `docs/plans/pr6-trim.md` (Revision 2, **approved by Joe, 2026-09-21**)
Risk: Tier 2 — feature + one new dev dependency (`scipy`, `pyyaml`), no `core/`, `plugins/physical`,
`aircraft/**/physical`, `aircraft/**/hardware`, or ICD change.
Decision requested: approve behavior and squash-merge candidate `fd4dbd4` against base `cc11c30`.

## What changes and why

Adds `astra.trim`: `trim_straight_and_level()` solves the notional transport's straight-and-level
cruise trim (elevator, throttle, alpha unknowns; `theta = alpha` at `gamma = 0`) by driving
`Simulation.derivative(state)`'s rigid-body residual to zero via `scipy.optimize.root`. Before this
PR, no Python-side trim capability existed; PR 7 (`astra.linearize`) depends on it.

Diff (`origin/main` `cc11c30` → `fd4dbd4`, 9 files, +8882/−34 — two retained full-gate logs account
for ~8100 of the insertions):
- `python/astra/trim.py` (new, 191 lines): `TrimResult` dataclass, `trim_straight_and_level()`,
  and four private helpers (`_read_yaml_dict`, `_earth_plugin_path`, `_load_still_air_config`,
  `_state_from_alpha`, `_residual6`).
- `tests/python/test_trim.py` (new, 166 lines): 7 tests — R1–R4 named in the plan, plus 3
  supporting checks (collapsed-quaternion cross-check, still-air config check, `write_input`
  guard-name check).
- `pyproject.toml`: `dev` group gains `scipy==1.18.1`, `pyyaml==6.0.3` (both confirmed the current
  PyPI release via `pip index versions` at pin time, not assumed from planning).
- `docs/plans/pr6-trim.md` (new, Revision 2): the approved spec/plan.
- `docs/reviews/pr6-trim-review.md` (new): Codex findings record.
- `PLAN.md`: PR 5 checked off with its merge SHA; PR 6/7 accept-lines' test paths corrected
  `python/tests/` → `tests/python/` (PR 5's own documented convention finding); two new one-line
  follow-up items (undeclared elevator/throttle range; bind `InputRegistry::names()` for PR 7).
- `STATE.md`: session handoff, rewritten per this session's own progression.

No `core/`, `plugins/`, or `aircraft/**` file is touched — this PR calls only PR 5's already-merged,
already-reviewed nanobind surface (`Simulation.derivative`, `Simulation.write_input`,
`Config.from_dict`).

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| R1: residual < 1e-6 at cruise | Pass — actual `max\|residual\|` ≈ 8.3e-17 | `pytest tests/python/test_trim.py::test_trim_converges_at_cruise` |
| R2: untrimmed guess fails by a wide margin | Pass — actual residual ≈ 3.43 m/s² (bound: > 1e-2) | `pytest tests/python/test_trim.py::test_untrimmed_guess_fails_by_a_wide_margin` |
| R3: physically plausible (alpha/elevator/throttle in range; `T cos(alpha) = D` cross-check) | Pass — `alpha ≈ 1.35°`, `elevator ≈ 0.0267 rad`, `throttle ≈ 0.4986`; cross-check misses by 0.31% (bound: < 5%) | `pytest tests/python/test_trim.py::test_solution_is_physically_plausible` |
| R4: determinism (bitwise-identical across two runs) | Pass — `np.array_equal` on state, exact `==` on controls | `pytest tests/python/test_trim.py::test_two_runs_from_the_same_guess_are_bitwise_identical` |
| Full four-preset gate (`just check`) | `check: OK`, 2026-09-21, ~9 min | `docs/reviews/evidence/6104206-full-gate.log` (rerun at the final reviewed candidate, after R-001's fix) — hooks 144/144, debug/release/static-plugins/asan all 387/387, tidy clean, pyright 0 errors, pytest 84/84 (77 pre-existing + 7 new) |
| Prior full-gate run (pre-review, superseded) | `check: OK` | `docs/reviews/evidence/b8c4506-full-gate.log` (retained for the record; `6104206`'s run above is the one that matters — captured after the review-found fix) |
| `ruff`/`ruff format`/`pyright` (repo-wide) | Clean at every commit in this candidate | Run inline each edit; folded into the full-gate runs above |

No golden/tolerance file is touched (`tests/golden/**` and `docs/decisions/**` untouched); the
oracle guard did not need to be lifted. GitHub's own `check` has not run — this PR is not pushed.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (installed companion 1.0.6, fresh context each pass).
Findings record: `docs/reviews/pr6-trim-review.md`.

- **General review** (base `cc11c30`, head `64ab385`): found **R-001** (minor) —
  `test_state_from_alpha_matches_the_real_quaternion_construction` never called
  `trim._state_from_alpha`; it compared the real binding's quaternion against a second, independently
  hand-typed formula, so a regression in the helper itself (confirmed reproducible by injecting one)
  would leave this dedicated check green.
- **Fixed** in `4b6f398`: the test now calls `_state_from_alpha` directly and compares its own
  quaternion slice against the real construction. Builder's own repro (inject → red; revert → green)
  before commit.
- **Adversarial review** (base `cc11c30`, head `4b6f398`, focus: D8 residual-slice correctness, the
  collapsed-quaternion special case, the R3 `T cos(alpha) = D` identity and its independent
  hand-computed cross-check, determinism/hidden-state across repeated `derivative()`/`write_input()`
  calls, the `earth/flat` position-representation guard, the accepted control-name-hardcoding
  deviation, and the scipy/pyright `cast()` workaround): **verdict approve**, R-001 independently
  reproduced and confirmed fixed, no other findings. One environment-only artifact noted (the
  reviewer's own sandbox showed unrelated tmp-dir setup errors on a broader `pytest` run — not
  reproducible in this session's own runs or the full-gate evidence above).

Remaining critical/major/minor: **zero**. No rejected or accepted-risk findings from either pass.

## Joe's attention

**Two accepted, documented deviations from your original instruction**, both because the current
Python binding surface has a real gap, not because they were convenient:

1. **Control-input names are hardcoded** (`"controls.elevator"`, `"controls.throttle"` in
   `trim.py`), against "read them from the registry." `write_input`'s existing raise-on-an-
   unregistered-name behavior is a runtime *guard* against a wrong or stale literal, not a *read* —
   `InputRegistry::names()` exists in C++ (PR 2) but isn't bound to Python yet. Documented in
   `trim.py`'s own module docstring, `docs/plans/pr6-trim.md`'s D10 row, and a new PLAN.md follow-up
   under PR 7's line (bind `InputRegistry::names()` so trim/linearize can discover names instead of
   repeating this).
2. **Elevator/throttle plausibility bounds are hardcoded in the test** (`[-0.3, 0.3]` rad, cited to
   `docs/refs/aircrafts/737.xml:201-206`; `[0, 1]`, cited to `icd/astra_icd.h:46`), against "read
   them from the plugin configs" — no such config key exists anywhere in this repository (checked
   directly in `aero.cpp`, `turbofan.cpp`, `cruise.yaml`). D10, approved as recommended: no new
   `aircraft/**/physical` config keys added (would raise this PR's own tier); PLAN.md gets a
   one-line follow-up noting this as the second control-surface property with no ICD/config home
   (after the existing positive-deflection-sense gap).

**One correction you caught in implementation review, now fixed**: the plan's first-draft R3
cross-check (`T = D + m g sin(alpha)`) double-counted the weight term already absorbed into `D`'s
own z-axis equilibrium; the implementation uses the corrected classical wind-axis identity
`T cos(alpha) = D`, which the real solve confirms to 0.31% (your own estimate: ~0.03%).

**One safety guard added in implementation review, now fixed**: `trim_straight_and_level` now
refuses (raises `ValueError` naming `physical.earth`) rather than silently mis-trimming when given
a config using `plugins/physical/earth/flat`, since `p_n[-1] = altitude_m` is only valid under a
geodetic (round/wgs84_rotating) position representation (ADR-0003). Verified by hand (a synthetic
flat-earth cruise.yaml variant raises as expected).

**No changed dependency/interface beyond the two new dev-only Python packages** (`scipy`, `pyyaml`
— neither touches `core/`, hot-path, or ABI). **Rollback**: revert the branch; no migration, no
data, no config format change for any existing consumer.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: R3's 5% cross-check bound is generous relative to the actual 0.31%
miss observed — kept at 5% deliberately (not tightened to match this one config's own coincidence)
because its purpose is catching a wrong-sign or mis-transcribed coefficient, not certifying this
config to hand-calculation precision (`docs/plans/pr6-trim.md`'s own reasoning).
Accepted risks: (1) control-input names hardcoded, no registry read (owner: PR 7's
`InputRegistry::names()` binding follow-up, PLAN.md). (2) elevator/throttle bounds hardcoded, no
config key (owner: PLAN.md's new "second undeclared control-surface property" follow-up item, not
scoped to any PR yet).
Nonblocking follow-ups: PLAN.md's two new items above.
Architectural decisions: none (no ADR introduced or depended on beyond ADR-0021/0017/0001/0002,
all pre-existing and unchanged).
