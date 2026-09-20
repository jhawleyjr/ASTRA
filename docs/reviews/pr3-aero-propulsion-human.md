# Human review packet: M3 PR 3 — `AeroModel`/`PropulsionModel` as `WrenchSource` specializations

PR / candidate base and head: not yet opened as a PR. Base `main`'s current tip, `c5f7633`
(M3 PR 2/#38, confirmed current — `git branch -vv` shows both `main` and this worktree's own
branch `worktree-m3-pr3-aero-propulsion` at that same commit). Head: entirely uncommitted working
tree in this worktree — 9 modified tracked files, 16 new files (full list below); no commits made
on this candidate yet.
Spec/plan/task: `docs/plans/pr3-aero-propulsion.md` (Revision 2, **approved by Joe, 2026-09-19**);
`docs/decisions/0018-aero-propulsion-wrench-specializations.md` (**accepted**, 2026-09-19);
`PLAN.md` line ~588 (M3 PR 3)
Risk: **Tier 3** (`core/interfaces/` addition, `kCoreApiVersion` bump, wrench-evaluation path)
Decision requested: approve behavior and merge (this candidate is implementation-complete,
reviewed, and gate-clean; no release is requested — M3 PR 3 is one PR inside an ongoing milestone)

## What changes and why

Adds `AeroModel`/`PropulsionModel`, the first two concrete `WrenchSource` specializations
(`docs/architecture.md` §7 previously sketched them with no implementation). Each provides a
`final` `wrench_b()` that: (1) subtracts wind in body axes
(`vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n)`); (2) assembles `AeroInputs`
(airspeed/α/β/body-rates/dynamic-pressure/Mach) or `PropulsionInputs` (airspeed + full atmosphere
sample) using `atan2`-based angle forms that are finite (never `0/0`) at zero airspeed; (3) checks
every consumed field — both the derived `Inputs` struct and the raw atmosphere sample — with
`std::isfinite`, returning an explicit all-NaN `Wrench` without calling `compute()` at all if
anything is non-finite; (4) otherwise calls a new pure-virtual `compute()` a concrete plugin
(PLAN.md PR 4, not yet built) implements. `requires_environment()` is also `final` on both
classes, unconditionally `{wind: false, atmosphere: true}` — no concrete plugin can override it.
Control values (elevator, throttle, etc.) never flow through `Inputs`; they arrive only through
ADR-0017's existing `bind_inputs()`/`InputRegistry` seam, exactly like any other `WrenchSource`
member.

`kCoreApiVersion` bumps 5→6 (both classes' vtables settle for the first time). `docs/architecture.md`
§7 corrected to match (real interfaces, `WrenchSource`-derived, `compute()` `const noexcept`,
control fields removed from the sketch's own `Inputs`-struct prose).

**Before this PR:** no concrete `WrenchSource` specialization exists for aero/propulsion; PR 4
would have had nothing to derive from. **After:** PR 4's linear-coefficient aero and thrust-lapse
turbofan can be built directly against these two classes.

Diff shape: 9 modified tracked files (`+108/-44`), 16 new files — 4 new interface headers + 2
`.cpp`s (~230 lines), 1 new ADR, 1 new plan, 1 new review-findings doc, 5 new test files (~430
lines), 2 new stub-plugin directories, 1 new fixture YAML. No file under `tests/golden/**` or any
tolerance file touched.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `./scripts/check-fast.sh` | Pass, 320/320 unit tests | run during iteration, not separately archived |
| `./scripts/check-full.sh` (pre-fix, before R-001) | **EXIT=0**, `check: OK`; 4 presets × 342/342; clang-tidy 0; pyright 0; pytest 38/38 | `/tmp/check-full-pr3-clean.log` (candidate before the R-001 fix; superseded by the post-fix run below, kept as the "R-001 existed as a real gap" baseline) |
| `./scripts/check-full.sh` (post-fix, current candidate) | **EXIT=0**, `check: OK`; 4 presets (debug/release/static-plugins/asan) × 343/343; clang-tidy 0 findings; pyright 0 errors, 0 warnings; pytest 38/38 | `/tmp/check-full-pr3-fix1.log` — this is the evidence for the candidate as it now stands |
| `clang-format-18 --dry-run --Werror` on every new/changed C++ file | Clean | run directly during iteration |
| `clang-tidy-18 -p build/asan` on every new/changed C++ file | Clean (after fixing one `readability-magic-numbers` and one `readability-identifier-naming` finding during iteration) | run directly during iteration |
| Targeted: `AeroModel.*`/`PropulsionModel.*`/`AeroPropulsionSeam.*` | 14/14 pass | part of the full-gate runs above; also run standalone during iteration |
| Targeted: `NoAlloc.*` (all 6, including the new fixture) | 6/6 pass | part of the full-gate runs above |

An earlier `check-full.sh` run also reported green but was **discarded, not counted as evidence**:
tracked files were edited while it was still running (the exact hazard the 2026-09-15
`docs/lessons.md` entry documents — a stage can grade an edit the earlier stages never saw). Both
runs actually counted above ran with no concurrent edits.

Toolchain: Ubuntu 24.04, clang++-18, the repo's own CMake presets; no new dependency introduced.
No numerical reference/golden data is used by this PR — `AeroModel`/`PropulsionModel`'s own
correctness is pinned against independently-computed closed-form values in the new tests (e.g.
`std::atan2`/`std::hypot` computed directly in the test, not by calling the implementation), not
against an external oracle; PR 4 is where a real numerical reference (JSBSim-derived coefficients)
first applies. CI remains `GATE=local`; the evidence above is local, not a GitHub check result.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (`codex-companion` 1.0.6, fresh context each dispatch).
Full record: `docs/reviews/pr3-aero-propulsion-review.md`.

- **General review** (`--scope working-tree`) and **adversarial review** (`--scope working-tree`,
  focused on ADR-0001 frames, ADR-0002 units/field-list, ADR-0016 purity, ADR-0017 write-seam
  interaction, the isfinite guard, allocation, and the V→0 obligation) dispatched in parallel.
  **Both independently found the same defect** (R-001, major): `AeroModel::wrench_b()`'s guard
  checked only the *derived* `AeroInputs` fields, not the raw atmosphere sample — an infinite (not
  NaN) `speed_of_sound_m_s` with otherwise-finite values gave `mach = finite/Inf = 0.0` (finite by
  IEEE-754), passing the guard and letting `compute()` run on corrupted data undetected.
- Fixed same round: `all_finite()` now also checks the raw `density_kg_m3`/`speed_of_sound_m_s`;
  added a regression covering both `+Inf` and `-Inf`. `PropulsionModel`'s own guard already checked
  all four raw `AtmoState` fields directly (confirmed, not just claimed — its `PropulsionInputs`
  carries the whole `AtmoState`, not a derived value) and never had this specific gap.
- **Fix-verification pass** (focused adversarial review, naming R-001 and the fix delta): verdict
  **"approve"**; confirmed the fix, confirmed the regression test's shape, confirmed the
  `PropulsionModel` claim, found **no new findings** against ADR-0016 purity, ADR-0001 frames, or
  per-frame allocation.
- Remaining critical/major findings: **none**. Correction rounds used: **1/2**. No finding
  rejected or deferred; no human exception requested.
- Limitation disclosed by all three Codex passes: their own sandbox could not run a full rebuild or
  the full gate (a `ptrace`/LeakSanitizer restriction) — each ran the already-built test binaries
  directly instead. The full-gate evidence above is the builder's own, run in this session.

## Joe's attention

- **`requires_environment()` marked `final`, hardcoded `{wind: false, atmosphere: true}`** on both
  new classes (`core/include/astra/interfaces/aero_model.hpp`,
  `propulsion_model.hpp`) — a concrete plugin (PR 4) cannot override this. Deliberate: the base
  class, not the plugin, is what reads both `env.wind_n` and `env.atmosphere`. Worth a second look
  since it forecloses a design a future plugin might otherwise want (an aero model somehow not
  needing atmosphere is no longer expressible) — the plan's own (c) addendum argues this tradeoff
  is correct because both fields are always consumed by the base assembly step, not the plugin.
- **Control-surface/throttle values are absent from `AeroInputs`/`PropulsionInputs`**
  (`core/include/astra/interfaces/aero_inputs.hpp`, `propulsion_inputs.hpp`) — a deliberate
  correction to `docs/architecture.md` §7's own earlier sketch prose, not an oversight. Controls
  arrive only through `bind_inputs()`. If this reading of the sketch is wrong, it is a
  Tier-3-consequential interface decision to revisit before PR 4 builds against it.
- **The unconditional `isfinite` guard's exact shape** (`core/src/interfaces/aero_model.cpp:16-25`,
  `propulsion_model.cpp:16-21`) — this is the mechanism the whole PR's "no silent NaN" claim rests
  on, and it was wrong once already (R-001) before being fixed and re-verified. Worth reading
  directly rather than trusting the test names alone.
- **The V→0 obligation is stated but not enforced** (`AeroModel::compute()`'s/
  `PropulsionModel::compute()`'s own doc comments, citing S&L §2.3 Eq. (2.3-9a)'s nondimensional-
  rate hazard) — this PR's own code computes no nondimensional rate, so there is genuinely nothing
  for it to guard; PLAN.md's PR 4 line now carries the acceptance obligation instead. Confirm this
  allocation of responsibility is acceptable before PR 4 starts.
- **`Simulation::for_testing()` still cannot construct an atmosphere-requiring vehicle** — R8's
  acceptance test uses `Simulation::from_config` instead (a literal-YAML fixture, matching existing
  precedent). Extending `for_testing()` was considered and deferred as a separate follow-up, not
  bundled into this PR.
- **Two new `PLAN.md` follow-up items added** (docs-only, this PR): the ADR-0002 unit-suffix
  wording ambiguity (`alpha_rad` etc., following `AtmoState`'s own existing precedent), and §15
  envelope monitors (deferred to PR 4, no hook left in this PR).
- Rollback: fully reversible — this PR adds two new, currently-unused (no concrete plugin exists
  yet) abstract classes and bumps a version number; reverting requires no data/schema migration.
- No oracle/tolerance/golden file touched; no new dependency; no ADR edited (0018 is new, elaborates
  0016/0017 without changing either).

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending (this packet's own head, once Joe reviews it)
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: unit-suffixed field names (`alpha_rad`, `dynamic_pressure_pa`,
etc.) were accepted against ADR-0002's literal "no unit suffixes in identifiers" text, following
`AtmoState`'s own existing precedent rather than inventing a bare-name convention unique to these
two new structs — Joe accepted this explicitly at plan approval, with the wording ambiguity itself
tracked as a `PLAN.md` follow-up rather than resolved now.
Accepted risks: the `EnvironmentSample`-wide "not surfaced at all for a NaN-tolerant consumer" gap
remains open for any *other*, future `WrenchSource` that reads `env.atmosphere` directly without
this PR's own guard pattern (tracked as the pre-existing `PLAN.md` follow-up, not new); owner:
whoever builds that future `WrenchSource`, triggered when one is proposed.
Nonblocking follow-ups: (1) ADR-0002 unit-suffix wording ambiguity (`PLAN.md`); (2) §15 envelope
monitors, deferred to PR 4 (`PLAN.md`); (3) `Simulation::for_testing()`'s atmosphere-parameter gap,
a precedented but out-of-scope extension (`docs/plans/pr3-aero-propulsion.md`'s "Test
infrastructure" section).
Architectural decisions: `docs/decisions/0018-aero-propulsion-wrench-specializations.md`
(accepted, 2026-09-19) — elaborates ADR-0016/ADR-0017, supersedes neither.
