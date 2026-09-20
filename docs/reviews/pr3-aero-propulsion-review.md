# Review: M3 PR 3 — `AeroModel`/`PropulsionModel` as `WrenchSource` specializations

Spec/plan: `docs/plans/pr3-aero-propulsion.md` (Revision 2, approved by Joe 2026-09-19);
`docs/decisions/0018-aero-propulsion-wrench-specializations.md`
Builder: Claude (Sonnet 5)
Reviewer: Codex (`codex-companion` 1.0.6), fresh context — one general-review pass, one
adversarial-review pass, both dispatched in parallel, `--scope working-tree`
Scope: uncommitted working tree (no commits yet on this candidate) — every file listed under
`git status --short --untracked-files=all` at dispatch time: the two new interface header pairs
(`aero_inputs.hpp`/`aero_model.hpp`, `propulsion_inputs.hpp`/`propulsion_model.hpp`) and their
`.cpp`s, `core_api.hpp.in`'s version bump, `docs/architecture.md` §7, the ADR, the plan, and every
new/changed test file and fixture
Validation: `./scripts/check-full.sh` green before dispatch (`/tmp/check-full-pr3-clean.log`,
`EXIT=0`, 4 presets × 342/342, clang-tidy 0, pyright 0, pytest 38/38) — the clean rerun after an
earlier same-command run was invalidated by concurrent edits (not counted as evidence)
Round: initial

## Finding R-001

Severity: major
Status: verified
Location: `core/src/interfaces/aero_model.cpp:16-20` (pre-fix); the unconditional `isfinite` guard
in `AeroModel::wrench_b()`
Requirement/ADR: ADR-0018's own Decision — "checks every assembled `Inputs` field with
`std::isfinite`... regardless of which fields a concrete plugin's own math actually reads" — and
the plan's own (b) design (the unconditional no-silent-NaN guarantee)
Failure: an `AtmosphereModel` returning a finite `density_kg_m3` but an infinite (not NaN)
`speed_of_sound_m_s` produces `mach = true_airspeed_mps / speed_of_sound_m_s = finite / Inf =
0.0` — finite, by IEEE-754 — and `dynamic_pressure_pa` stays finite too (it never involves
`speed_of_sound_m_s`). The pre-fix `all_finite(const AeroInputs&)` checked only the *derived*
`AeroInputs` fields, never the raw atmosphere inputs consumed to build them, so this corrupted
sample passed the guard, `compute()` ran on data that looks valid but is not, and the resulting
finite wrench would reach `HealthMonitor::check_wrench` with nothing to catch — defeating the
"unconditional" guarantee for exactly this one raw-field corruption shape. Found independently by
both the general review and the adversarial review in the same dispatch round; confirmed by direct
reproduction (`python3 -c "print(100.0/float('inf'))"` → `0.0`, `isfinite` → `True`).
Requested outcome: the guard must check the raw atmosphere fields `AeroModel` actually consumes
(`density_kg_m3`, `speed_of_sound_m_s`), not only the values derived from them, so no combination
of finite/broken raw inputs can produce an all-finite `AeroInputs` from a genuinely corrupted
sample.
Builder response: fixed. `all_finite()` (`core/src/interfaces/aero_model.cpp`) now takes the
`AtmoState` alongside the assembled `AeroInputs` and additionally checks
`std::isfinite(atmosphere.density_kg_m3)` and `std::isfinite(atmosphere.speed_of_sound_m_s)`
directly. `PropulsionModel::wrench_b()`'s own guard was already correct on this point — it checks
all four raw `AtmoState` fields directly (`PropulsionInputs` carries the whole struct, not a
derived value), so it never had this specific gap; confirmed by rereading
`core/src/interfaces/propulsion_model.cpp`, no change needed there. Added
`AeroModel.InfiniteSpeedOfSoundWithOtherwiseFiniteValuesNeverCallsCompute`
(`tests/unit/wrench/aero_model_test.cpp`), covering both `+Inf` and `-Inf`, asserting zero
`compute()` calls and an all-non-finite `Wrench`, per the reviewers' own recommended regression
shape. Full gate rerun clean after the fix: `/tmp/check-full-pr3-fix1.log`, `EXIT=0`, 4 presets ×
343/343 (the one new test), clang-tidy 0, pyright 0, pytest 38/38 — no concurrent edits during this
run (an earlier, invalidated run of the pre-fix code is not counted, per the
2026-09-15 `docs/lessons.md` entry on editing tracked files mid-`just check`).
Verification: **verified** — focused Codex adversarial-review pass (`codex-companion` 1.0.6,
`--scope working-tree`), verdict "approve": confirmed `core/src/interfaces/aero_model.cpp` now
checks the raw `density_kg_m3`/`speed_of_sound_m_s` before `compute()`; confirmed the new
regression exercises both `+Inf` and `-Inf` and asserts zero `compute()` calls plus a non-finite
`force_b`/`moment_b`; confirmed `PropulsionModel`'s own guard already checked all four raw
`AtmoState` fields directly (the "never had this gap" claim holds — the reviewer noted it cannot
independently establish the historical timing of when that code was written, only that it is
correct now, which is the claim that matters). No new findings from inspecting the fix delta
against ADR-0016 purity, ADR-0001 frames, or per-frame allocation. Existing focused test binary:
`AeroModel.*`/`PropulsionModel.*`/`AeroPropulsionSeam.*`, 14/14, exit 0 (the reviewer's own sandbox
could not run a full rebuild/gate; the builder's own full-gate evidence below is what this review
relies on for that part).
History: round 1 (initial) — found independently by both the general and adversarial review;
fixed same round; verified by a third, focused adversarial-review pass. Correction rounds: 1/2.
Human exception if needed: none requested; no oracle/tolerance file touched.

## Completion

One major finding (R-001), fixed, tested, and independently verified; no critical findings; no
minor findings recorded by either pass beyond R-001 itself. New findings from the verification
pass's own fix-delta/callers/invariants inspection: none. Scope reviewed: the full working-tree
candidate as listed above, across three dispatches (general, adversarial, verification).
Validation gap: neither Codex pass could run the full gate itself (ptrace/LeakSanitizer
restriction in that sandbox) — all three ran the focused test binaries directly instead; the full
gate itself was run and verified by the builder, in this session, both before and after the fix
(`/tmp/check-full-pr3-clean.log` pre-fix, `/tmp/check-full-pr3-fix1.log` post-fix, both `EXIT=0`).
Review closed. Codex's "approve" is not Joe's approval; `/human-review` and Joe's own decision
remain the actual merge gate.
