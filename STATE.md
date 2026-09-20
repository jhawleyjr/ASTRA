# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M3 PR 3 (`PLAN.md` line ~588): `AeroModel`/`PropulsionModel` as `WrenchSource` specializations.
**Plan approved by Joe, 2026-09-19**; **ADR-0018 written** (status accepted, goldens armed by Joe).
**Implemented and reviewed**: `aero_inputs.hpp`/`aero_model.hpp`, `propulsion_inputs.hpp`/
`propulsion_model.hpp` (each `wrench_b()`/`requires_environment()` `final`, an `isfinite` guard
returning an explicit all-NaN `Wrench` before `compute()` runs on any non-finite field, checking
both derived `Inputs` fields *and* the raw `AtmoState` fields consumed — see fix below);
`core_api.hpp.in` version 5→6; `docs/architecture.md` §7 corrected; `tests/python/
test_package.py`'s pinned version fixed proactively. Tests: `tests/unit/wrench/
aero_model_test.cpp`/`propulsion_model_test.cpp`, `tests/unit/kernel/
aero_propulsion_seam_test.cpp` (R8/R8-guard via `Simulation::from_config`), `tests/plugins/
aero_stub`+`propulsion_stub`, `tests/physics/no_alloc_test.cpp` extension. Worktree
`m3-pr3-aero-propulsion`, branch `worktree-m3-pr3-aero-propulsion`, based on `main`'s `c5f7633`.

**`/review-cycle` complete** (`docs/reviews/pr3-aero-propulsion-review.md`): Codex general +
adversarial review (`--scope working-tree`) both independently found R-001 (major) —
`AeroModel::wrench_b()`'s `isfinite` guard checked only *derived* `AeroInputs` fields, so an
infinite (not NaN) `speed_of_sound_m_s` gave `mach = finite/Inf = 0.0` (finite) and let `compute()`
run on corrupted data undetected. Fixed: `all_finite()` now also checks the raw
`density_kg_m3`/`speed_of_sound_m_s` it consumes; `PropulsionModel`'s own guard already checked
all four raw `AtmoState` fields directly, confirmed no equivalent gap there. Added
`AeroModel.InfiniteSpeedOfSoundWithOtherwiseFiniteValuesNeverCallsCompute` (both signs of
infinity). A third, focused Codex adversarial-review pass verified the fix: verdict "approve", no
new findings.

## Last verified result
`./scripts/check-full.sh` **post-fix**: `EXIT=0`, `check: OK` (`/tmp/check-full-pr3-fix1.log`) — 4
presets, 343/343 each, clang-tidy 0, pyright 0, pytest 38/38, no concurrent edits during this run.
(Pre-fix clean run: `/tmp/check-full-pr3-clean.log`, 342/342, also `EXIT=0` — the candidate before
R-001 existed as a known gap. An even earlier run showed green but was invalidated by concurrent
edits mid-run, per the 2026-09-15 `docs/lessons.md` entry, and was correctly not trusted.)

## Next step
`/human-review` packet, then draft PR (as a draft, not ready-to-merge), then stop for Joe's review.

## Tried / failed
Correction rounds: 1/2 used (R-001, found by two independent Codex passes, fixed, verified by a
third). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
