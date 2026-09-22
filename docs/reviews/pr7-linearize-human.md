# Human review packet: M3 PR 7 — astra.linearize

PR / candidate base and head: base `origin/main` at `53064e0` (M3 PR 6, merged, #44) — confirmed
current tip via `git fetch`, unchanged since this worktree started; implementation candidate
(last code/test commit) `fec3d67`, head `f0c4519` (plan/review/packet/STATE documentation commits
on top, each itself a real deliverable Joe reviews below, not mechanical bookkeeping). Working
tree clean at packet time.
Spec/plan/task: `docs/plans/pr7-linearize.md` (Revision 4, **approved by Joe, 2026-09-22**)
Risk: Tier 2 — feature, no `core/kernel/`, `plugins/physical`, `aircraft/**/physical`,
`aircraft/**/hardware`, or ICD change. One `python/bindings/_core.cpp` addition
(`Simulation.input_names()`) is a pure-copy read over an already-existing C++ method, no
`kCoreApiVersion` bump.
Decision requested: approve behavior and squash-merge candidate `f0c4519` against base `53064e0`.

## What changes and why

Adds `astra.linearize`: central-differences `A`/`B` (9x9, 9x4) about `astra.trim.trim_straight_
and_level`'s cruise solution, in the Euler-angle state form `x = [u, v, w, p, q, r, phi, theta,
psi]`/`u = [elevator, aileron, rudder, throttle]` (S&L eq. 1.8-16 for the quaternion reconstruction,
eq. 1.4-4 for the Euler-rate rows), and reports the five classical modes (short period, phugoid,
Dutch roll, roll, spiral) so PR 8 has a linear model to cross-check its own open-loop run against.
Includes PR 6's own follow-up: `Simulation.input_names()`, binding `InputRegistry::names()` so
control names are read from the registry rather than hardcoded (`astra.trim` itself is
unmodified — a separate PLAN.md follow-up tracks that retrofit).

Diff (`origin/main` `53064e0` → `f0c4519`, 11 files, +9902/−29 — the two retained full-gate logs
account for ~8117 of the insertions):
- `python/bindings/_core.cpp` (+23/−1): one new free function (`simulation_input_names`) plus one
  `.def("input_names", ...)` line, mirroring `simulation_derivative`'s own shape — no header/core
  change.
- `python/astra/linearize.py` (new, 415 lines): `LinearizeResult`/`OscillatoryMode`/`RealMode`/
  `Modes` dataclasses, `linearize()`, `modes()`, `longitudinal_block()`/`lateral_block()`,
  `state_step()`/`control_step()`, and private helpers (`_quaternion_from_euler`, `_euler_rate`,
  `_state_from_x`, `_evaluate`, `_column`, mode-classification internals). Unchanged by R-003
  (test/docs-only fix).
- `tests/python/test_linearize.py` (new, 14 tests): R1–R7 named in the plan (R4 split into 5 mode
  tests, one of which — phugoid — is itself split into two, zeta and omega_n, D18/R-002 below),
  plus 3 supporting checks (quaternion-port cross-check, wings-level kinematic identity,
  `input_names()` registration order). All 14 pass unmarked; no `xfail` remains (D18 Revision 4/
  R-003).
- `PLAN.md`: PR 6 checked off with its merge SHA `53064e0`; one new follow-up (`astra.trim`'s own
  control names not retrofitted to `input_names()`); PR 8's own line gets a follow-up (its phugoid
  cross-check should use this PR's numerical `A`, because that's the model being validated, not
  because the closed form is unreliable — D18 below).
- `docs/plans/pr7-linearize.md` (new, Revision 4): the approved spec/plan, including the three
  in-session revisions below.
- `docs/reviews/pr7-linearize-review.md` (new): findings record (R-001/R-002 Codex, R-003 Joe's
  own packet review; all three fixed and fix-verified).
- `docs/lessons.md`: two new append-only entries — the CG-vs-AERORP coefficient-reference gotcha,
  and its own correction (a fixed coefficient doesn't validate the formula chosen around it; check
  a textbook simplification's stated assumption against the real numbers before accepting a miss).
- `STATE.md`: session handoff, rewritten per this session's own progression.

No `core/`, `plugins/`, or `aircraft/**` file is touched.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| R1: halving every step changes `A`/`B` by < 1e-3 relative (Frobenius) | Pass — `A` changed `5.29e-9`, `B` changed `4.42e-13` (bound: < 1e-3) | `pytest tests/python/test_linearize.py::test_halving_every_step_changes_a_and_b_by_less_than_the_bound` |
| R2: longitudinal/lateral blocks don't cross-couple | Pass — cross-block entries < 1e-3 relative to their own block's norm | `pytest tests/python/test_linearize.py::test_longitudinal_and_lateral_blocks_do_not_cross_couple` |
| R3: trim point's own derivative < 1e-6 before differencing | Pass — actual `max\|f0\|` ≈ 8.33e-17 | `pytest tests/python/test_linearize.py::test_the_trim_points_own_derivative_is_near_zero` |
| R4a: short period vs S&L eq. 4.2-11/4.2-12 | Pass — `omega_n` 1.000 vs 0.998 rad/s (0.2%, bound 15%); `zeta` 0.547 vs 0.545 (0.003 abs, bound 0.1) | `pytest tests/python/test_linearize.py::test_short_period_matches_the_approximation` |
| R4b: phugoid `zeta` vs S&L eq. 4.2-18 | Pass — 0.052 vs 0.0 (0.052 abs, bound 0.1) | `pytest tests/python/test_linearize.py::test_phugoid_zeta_matches_the_approximation` |
| R4b: phugoid `omega_n` vs S&L eq. 4.2-16 | Pass — 0.0502 vs 0.0507 rad/s (0.9%, bound 15%; not eq. 4.2-17b, D18 Revision 4 below) | `pytest tests/python/test_linearize.py::test_phugoid_omega_n_matches_the_approximation` |
| R4c: Dutch roll vs S&L eq. 4.2-30/4.2-31 | Pass — `omega_n` 1.881 vs 1.828 rad/s (2.9%, bound 30%); `zeta` 0.109 vs 0.118 (0.008 abs, bound 0.15) | `pytest tests/python/test_linearize.py::test_dutch_roll_matches_the_approximation` |
| R4d: roll vs S&L eq. 4.2-35 | Pass — signed `sigma` −1.145 vs −1.129 /s (1.4%, bound 30%, sign agrees) | `pytest tests/python/test_linearize.py::test_roll_matches_the_approximation` |
| R4e: spiral vs S&L eq. 4.2-36 | Pass — signed `sigma` −0.01072 vs −0.01269 /s (18.4%, bound 30%, sign agrees) | `pytest tests/python/test_linearize.py::test_spiral_matches_the_approximation` |
| R5: two `linearize()` calls give bitwise-identical `A`/`B` | Pass — `np.array_equal` on both matrices | `pytest tests/python/test_linearize.py::test_two_calls_are_bitwise_identical` |
| R6: column isolation, checked after every individual column | Pass — registry equals `u0` after each of 9 state + 4 control `_column` calls | `pytest tests/python/test_linearize.py::test_input_registry_reads_trim_values_after_any_column` |
| R7: `input_names()` returns the four registered names, in order | Pass — `["controls.elevator", "controls.aileron", "controls.rudder", "controls.throttle"]` | `pytest tests/python/test_linearize.py::test_input_names_returns_the_four_registered_control_names` |
| `pytest tests/python/` (full suite, at head `f0c4519`) | 99 passed, 0 `xfail` in 0.37s | Rerun this session |
| `ruff check .` / `ruff format --check .` / `pyright` (repo-wide, at head `f0c4519`) | Clean | Rerun this session |
| Full four-preset gate (`just check`), final candidate | `check: OK` — hooks 144/144, debug/release/static-plugins/asan all 387/387, tidy clean, pyright 0 errors, pytest 99 passed, 0 `xfail` | `docs/reviews/evidence/fec3d67-full-gate.log` (post-R-003 fix; the packet's own head `f0c4519` adds only doc/plan bookkeeping after this commit, no code/test change) |
| Full four-preset gate, earlier candidate (superseded) | `check: OK` — 97 passed + 1 expected `xfail` (pre-R-001/R-002 fix); `0fa5e85`'s own re-run (98 passed + 1 expected `xfail`, pre-R-003) confirmed the same shape but wasn't separately retained | `docs/reviews/evidence/009715a-full-gate.log` (retained for the record only) |

No golden/tolerance file is touched (`tests/golden/**` and `docs/decisions/**` untouched); the
oracle guard did not need to be lifted. GitHub's own `check` has not run — this PR is not pushed.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (installed companion 1.0.6, fresh context each pass).
Findings record: `docs/reviews/pr7-linearize-review.md`.

- **General review** (base `53064e0`, head `a155c9f`): found **R-001** (major) — `u0`/`B`'s control
  columns assumed a fixed position order `[elevator, aileron, rudder, throttle]` matched
  `Simulation.input_names()`'s own runtime registration order. Reordering only existing
  `vehicle.models` entries (turbofans before the aero model) reproduced a trim residual under
  1e-16 but a linearization residual of ~1.33 — silently computing `A`/`B` about the wrong
  operating point, exactly the class of bug binding `input_names()` was meant to prevent.
- **Adversarial review** (same base/head, focus: Euler-angle permutation, column isolation,
  D16 sign discipline, D18's independent re-derivation, the phugoid `xfail`, `LinearizeResult.
  simulation` ownership/lifetime): found **R-002** (medium) — the phugoid `xfail(strict=True)`
  covered both the frequency and damping assertions with no `raises=` restriction, so the damping
  assertion never ran (the frequency assertion always raised first) and any unrelated exception
  would have read as the known, expected miss. No defect found in the other five focus areas.
- **Fixed** in `0fa5e85`: R-001 — `u0`/each `B` column now built by mapping control name to trim
  value/step via `Simulation.input_names()`'s own runtime order, with an explicit rejection of an
  unexpected name set; verified directly against a reordered copy of `cruise.yaml`
  (`input_names() == ["controls.throttle", "controls.elevator", "controls.aileron",
  "controls.rudder"]`, residual now 8.33e-17). R-002 — split into an unmarked
  `test_phugoid_zeta_matches_the_approximation` (always runs, passes) and a narrowly-`xfail`'d
  `test_phugoid_omega_n_matches_the_approximation` (`strict=True, raises=AssertionError`).
- **Fix-verification, round 1** (fresh context, same base, head `3210dd1`): **verdict approve** —
  "R-001: fix holds... R-002: fix holds... New findings: none."
- **R-003** (Joe's own packet review, not Codex's — see Joe's attention below): the phugoid
  `xfail` from R-002 was itself covering a real, fixable formula defect. Fixed in `fec3d67`.
- **Fix-verification, round 2/2** (fresh context, base `53064e0`, head `492b990`): **verdict
  approve** — "S&L third edition, p. 262, eq. 4.2-16 reduces with CL_V = Cm_V = 0 to omega_n^2 =
  (2g/c)(2CL_e Cm_alpha)/(Cm_q CL_alpha + 4m Cm_alpha/(rho S c)), matching the implementation's
  signs and factors... New findings: none." The correction-round budget (2/2) is fully consumed.

Remaining critical/major/minor: **zero** across all three findings. No rejected or accepted-risk
findings from any pass.

## Joe's attention

**D18 — the closed-form comparison's coefficients had to be corrected mid-session, on your own
direction, not merely tolerance-tuned.** Running R4 against the real `cruise.yaml` numbers first
found three of five modes outside D13's own bound (short period 21.7% vs 15%, phugoid 16.7% vs
15%, spiral 223% vs 30%). I verified this wasn't a bug in `astra.linearize` itself (R2/R3 pass
tightly; a hand-derived check of the numerical `dM/dw` against the moment-transfer physics below
matched to four significant figures) before flagging it. Root cause: `aero_math.hpp`'s own
`moment_b = moment_native_b + r_aero_b.cross(force_b)` transfers its tabulated `Cm_alpha`/
`Cl_beta`/`Cn_beta` from AERORP to the CG, while S&L Sec. 4.2's dimensional derivatives are
CG-referenced throughout — D13's original coefficient list named the raw AERORP values. Per your
own direction (Revision 3), the test re-derives the CG-referenced coefficients from first
principles (`tests/python/test_linearize.py::_cg_referenced_coefficients` — never calling
`aero_math.hpp` or `astra.linearize.modes()`/`derivative()` for its own reference values), applied
to every affected derivative (not only the ones that were failing), with every S&L equation number
and tolerance left unchanged. This fixed short period and spiral outright. **Phugoid needed a
second correction, which you caught in your own packet review**: my first pass accepted phugoid
`omega_n` as an expected, `xfail`'d miss on the reasoning that eq. 4.2-17b
(`omega_n = (g/V_T)*sqrt(2)`) carries no stability derivative for the CG-referenced correction to
move. That reasoning stopped one step short — eq. 4.2-17b is itself a further simplification of
eq. **4.2-16**, valid only when S&L's own dominance condition holds in 4.2-16's denominator; the
raw `Cm_alpha` (`-0.6`) satisfied it, the CG-referenced one (`-0.315`) does not (the neglected term
is ~40% of the retained one). Using eq. 4.2-16 itself — the formula S&L's own text says "will be
used to calculate the phugoid frequency" — with `CL_V = Cm_V = 0` (exact for this model, no
Mach/airspeed dependence in the aero or turbofan coefficients) closes the miss to 0.9%. **The
`xfail` is removed; all five modes now pass unmarked**, correction round 2/2. Full derivation,
sign conventions, and the before/after table for all five modes (both revisions) are in
`docs/plans/pr7-linearize.md`'s own D18 section.

**PLAN.md's PR 8 follow-up, reworded**: PR 8's own phugoid cross-check should still use this PR's
numerical `A` — because that's the model PR 8 itself validates, not because the closed form is
unreliable for this aircraft (it works fine once the right equation and CG-referenced coefficient
are used).

**One citation correction, resolved without escalation** (not a question, a repository finding):
D11 cited S&L §4.1 for the Euler-angle linearization form; reading the actual book (main checkout)
found §4.1 is historical introduction prose with no equations. The real sources — eq. 1.8-16
(quaternion from Euler, already used by `frames.hpp`), eq. 1.4-4 (the Euler kinematical equations),
and §3.7 "Numerical Linearization" for the central-difference method itself — are cited throughout
the implementation instead. Changes no tolerance, scope, or design.

**No changed dependency.** `python/bindings/_core.cpp`'s one new binding has no `kCoreApiVersion`
bump (no ABI/vtable change). **Rollback**: revert the branch; no migration, no data, no config
format change for any existing consumer — `python/astra/trim.py` is unmodified.

## Decision record

State: approved
Human decision/date/reference: **Joe approved, 2026-09-22, in session**
Approved candidate/scope: implementation `fec3d67` (head `693d9fe`, documentation/handoff commits
on top) against base `origin/main` at `53064e0`
Merge observed: not merged (Joe's own squash-merge, pending GitHub's `check` going green on the PR)
Release authorization: not requested

Significant tradeoff rationale: D18's CG-referenced correction is scoped to exactly the three
derivatives (`Cm_alpha`, `Cl_beta`, `Cn_beta`) that the moment-transfer term actually affects — rate
derivatives (`Cm_q`, `Cl_p`, `Cl_r`, `Cn_r`) are provably untouched (this aero model's force
equations carry no `p`/`q`/`r` dependence) and were left as the raw config coefficients, not
"corrected" for symmetry's own sake.
Accepted risks: none from D18 — the phugoid closed-form cross-check passes unmarked as of Revision 4;
the earlier accepted-miss risk from Revision 3 no longer applies.
Nonblocking follow-ups: PLAN.md's `astra.trim`-control-names-not-retrofitted line; PLAN.md's PR 8
phugoid-cross-check line (both above).
Architectural decisions: none (no ADR introduced or depended on beyond ADR-0021/0017/0001/0002,
all pre-existing and unchanged).
