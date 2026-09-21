# Human review packet: PLAN.md M3 PR 3d — implement ADR-0020

PR / candidate base and head: branch `worktree-m3-pr4-notional-aero-turbofan`, pushed as
**draft PR [#40](https://github.com/jhawleyjr/ASTRA/pull/40)**. This packet's own subject is the
code candidate: commit `56af34e` (parent `a6ac7ce`), plus one follow-up commit addressing Joe's own
review comments below (documentation of the domain-off/nan-off interaction and per-stage counting;
this retained full-gate rerun; this packet's own corrections; `STATE.md` trimmed) — see that
follow-up commit's own SHA in the Decision record below once made. This candidate's own base is
`da18909` (`main`'s current tip, unchanged since the session started — confirmed by
`git merge-base HEAD origin/main`). The full branch (`main..HEAD`, 11 commits before the follow-up)
also carries this session's earlier M3 framework-realignment work (ADR-0019/0020/0021 acceptance,
`docs/architecture.md`/`PLAN.md` rewrite, PR 3d's own plan) — PR #40 covers all of it, retitled to
say so; this packet's own evidence and findings are scoped to the PR 3d code candidate specifically,
not the docs-only commits preceding it (those carry no executable behavior to validate).
Spec/plan/task: `docs/plans/pr3d-adr0020-implementation.md` (approved by Joe, 2026-09-20, Q1-Q4
decided); `docs/decisions/0020-aero-propulsion-true-interfaces.md` (accepted).
Risk: **3** (`core/kernel/`, plugin-facing ABI — `docs/process/workflow.md`'s highest tier).
Decision requested: approve merging `56af34e` onto PR #40's branch (or request changes).

## What changes and why

`AeroModel`/`PropulsionModel` go from ADR-0018's sealed, `final`-`wrench_b()`/`compute()` design to
ADR-0020's minimal marker interfaces — no core-defined `Inputs` struct, no `final` anywhere except
one non-final `requires_environment()` override each. A concrete plugin overrides
`WrenchSource::wrench_b()` directly. Air-data math (relative velocity, airspeed, α/β, dynamic
pressure, Mach) becomes `core/include/astra/util/air_data.hpp`, relocated verbatim from the deleted
`wrench_b()` bodies — a move, not a rewrite (diffed by hand against `git show
644cc7c~1:core/src/interfaces/aero_model.cpp` during implementation, not merely eyeballed).

**The core-side design decision is Joe's own (Q4 of the approved plan), not a default I chose**:
removing `final` also removes the compiler-enforced isfinite guard ADR-0018 used to give. Restoring
it core-side needed a decision about a real ambiguity Codex's plan critique found — an atmosphere
sample can be non-finite either because it legitimately left its own declared `valid_range()`
(expected, not a fault) or because the model is genuinely broken within range (R-001's own case: an
infinite `speed_of_sound_m_s`) — and a naive design couldn't tell them apart without letting the
`nan` action silently override the `domain` action for the first case. Joe's decision: check the
domain **before** sampling, not after. `Vehicle` gained a new private helper,
`prepare_stage_environment()` (`core/src/kernel/vehicle.cpp`), called once per RK4 sub-stage from
`integrate()`'s own derivative functor:

1. If `atmosphere_ != nullptr` and domain checking is enabled, compare the stage's own altitude
   against `AtmosphereModel::valid_range()` — the identical NaN-safe form
   `Simulation::from_config`'s own init-time check already uses — **before** calling `at()`. Out of
   range: report `kOutOfDomain` (the existing `report_out_of_domain`, no new `HealthMonitor` method)
   naming the atmosphere plugin, and skip this stage's `wrench_sources_` loop entirely; atmosphere
   is never sampled.
2. Otherwise sample atmosphere fresh, then check `wind_n`/the sampled fields directly for NaN/Inf.
   A genuine fault reports `kNonFiniteEnvironment` via a new method,
   `HealthMonitor::report_non_finite_environment` (mirrors `report_out_of_domain`'s own shape: the
   predicate is evaluated by the caller, not this method, since a returned `HaltReason` can't itself
   distinguish "no fault" from "warn: fault detected, run continues" — the bug R-001 in the attached
   review file is about), and skips the loop the same way.

Either skip means **zero** configured `WrenchSource::wrench_b()` calls that stage — not merely a
correctly-attributed halt after the fact. This is also the actual resolution of `PLAN.md`'s own
"Atmosphere-domain-check misattribution window" item (now checked `[x]`), not a new gap: the two
failure classes are separated before either check could misattribute the other's own case.

`kCoreApiVersion` 6 → 7. `Vehicle` gains `wind_plugin_path_`/`atmosphere_plugin_path_`/
`wind_configured_`, threaded from `Simulation::from_config` and `Simulation::for_testing` (wind
only — `for_testing` has no atmosphere parameter at all today, confirmed by reading it directly).
`aero_stub`/`propulsion_stub` rebuilt to override `wrench_b()` directly.

Diff: 32 files, +903/-761 (`git diff --stat a6ac7ce..56af34e`). Two files deleted outright
(`aero_inputs.hpp`, `propulsion_inputs.hpp`); two test files deleted and replaced (their own
`wrench_b()`-assembly tests are gone with the assembly they tested;
`tests/unit/util/air_data_test.cpp` pins the relocated formulas directly). Three new, small test-only
plugins (`counting_aero`, `flaky_atmosphere`, `nan_wind`) exist solely to make the fault-path
acceptance criteria assertable — none ship outside `tests/`.

**Not in this candidate**: PR 4 itself (notional aero/turbofan numbers) and ADR-0021's own
implementation (PLAN.md PR 4a) — both remain stopped/not started, per PR 3d's own scope.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| Fast gate, asan preset (`ctest --preset asan --parallel 32`) | 100% passed, 343/343, ~100 s, 2026-09-20 (final rerun, after all fixes) | Run directly this session; log not retained to a file, rerun on request |
| Full gate (`./scripts/check-full.sh` → `just check`) | **`check: OK`**, exit 0, 2026-09-20. Four earlier runs against the pre-follow-up candidate each caught and fixed one real issue (see below); this is the retained rerun against the follow-up commit (health-monitor documentation, this evidence update, `STATE.md` trim) — `docs-check: OK (24 Markdown files checked)`; `100% tests passed, 0 tests failed out of 343` × 4 presets; `0 errors, 0 warnings, 0 informations` (pyright); `38 passed in 0.08s` (pytest) | `/tmp/claude-1000/-home-joe-repos-ASTRA--claude-worktrees-m3-pr4-notional-aero-turbofan/65ad276e-a23e-4770-98bc-f86da78fdc75/scratchpad/pr3d-followup-check-full.log` (session-local, per this repo's own established retention convention — see e.g. `docs/reviews/nesc-tol-rel-human.md`) |
| clang-tidy (asan + python presets) | Two real findings, both fixed: `readability-function-cognitive-complexity` on the original inline `integrate()` derivative functor (fixed by extracting `prepare_stage_environment`); `bugprone-unchecked-optional-access` on that new function's own `eom_->earth()` call (fixed with the same defensive `eom_.has_value()` guard `check_domain` already carries, for the identical reason) | Part of the full-gate run above |
| pytest (Python package) | One real failure, fixed: `test_core_api_version_and_toolchain_string_come_from_libastra_core` pinned the pre-bump `kCoreApiVersion == 6`; updated to `7` with the same per-bump comment history the test already carries | `tests/python/test_package.py` |
| clang-format | One real batch of violations, fixed: several new/edited files (mostly the sed-based `check_environment` → `report_non_finite_environment` rename) exceeded the 100-column line limit; `clang-format-18 -i` applied tree-wide | — |
| Codex general review | One finding (P2), fixed and verified | `docs/reviews/pr3d-adr0020-review.md`, R-001 |
| Codex adversarial review | One finding (medium/"block"), fixed and verified | `docs/reviews/pr3d-adr0020-review.md`, R-002 |
| Codex re-verify | Both findings confirmed fixed; "New findings: none" | `docs/reviews/pr3d-adr0020-review.md`, Completion |

No numerical/physics tolerance is touched by this candidate (no `tests/golden/**` file, no
`docs/decisions/**` file other than the already-accepted `0020-*.md` from the prior commit). CI
remains local-gate mode; the evidence above is the actual local full-gate result, not a GitHub
check.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex, fresh context, three dispatches (general,
adversarial, one focused re-verify) — full findings, fixes, and verification text in
`docs/reviews/pr3d-adr0020-review.md`. Zero open findings of any severity. Both fixed findings were
independently re-verified against the actual current diff, not accepted on the builder's own claim.

## Joe's attention

- **The one real design decision in this candidate is yours (Q4)** — restated above under "What
  changes and why." If you want to re-examine it, the whole derivative functor is
  `core/src/kernel/vehicle.cpp`'s `Vehicle::integrate()` plus the new `prepare_stage_environment()`
  immediately above it.
- `core/src/kernel/health_monitor.cpp`'s new `report_non_finite_environment` and
  `core/include/astra/kernel/health_monitor.hpp`'s own doc comment on it record *why* the predicate
  had to move to the caller (a real bug found and fixed mid-session, not a stylistic choice) — worth
  a read if the `report_`/`check_` naming split elsewhere in this class is unfamiliar.
- **Your own follow-up review comments, addressed in this packet's own follow-up commit**:
  (a) `report_non_finite_environment`'s own header comment
  (`core/include/astra/kernel/health_monitor.hpp`) now documents the `domain: off`/`nan: off`
  interaction explicitly — with `domain: off`, a legitimate `valid_range()` excursion falls through
  to this check instead of the domain check, reported (if `nan` isn't also off) as
  `kNonFiniteEnvironment` naming the atmosphere plugin, not `kOutOfDomain`; with both off, that
  stage's `wrench_sources_` loop is still skipped (the skip itself is never gated on either action)
  but nothing halts, counts, or signals — a silent zero-wrench stage, the same "off accepts a
  plausible-looking-wrong outcome" posture every other check in this class already has, stated
  explicitly here because the silence is otherwise easy to miss. (b) Same comment, and
  `report_out_of_domain`'s own, now document that both can fire up to four times per frame (once per
  RK4 sub-stage) under a non-halting action, sharing `check_wrench`'s own already-documented "up to
  4x/8x per frame, not deduplicated across stages" counting rule, rather than the earlier "at most
  once per vehicle per frame" claim, which this candidate's own new per-stage call site made
  inaccurate. No behavior changed for either (a) or (b) — documentation only, matching the simpler of
  the two options you offered for (b).
- `kCoreApiVersion` bump (6→7): no out-of-tree plugin exists yet, so this is a settling bump, not a
  compatibility break for anyone — same reasoning as every prior bump this class's own comment
  history records.
- `PLAN.md`'s "Atmosphere-domain-check misattribution window" item is now checked off — confirm the
  resolution reads right if you want to spot-check just one thing before merge.
- Rollback: fully reversible — this candidate is a small set of commits on a task branch, nothing
  merged yet, no schema or data migration, no plugin outside this tree implements either interface
  yet.
- Not addressed here, by design: PR 4 (notional aero/turbofan) stays stopped, now unblocked by this
  candidate but not started; ADR-0021 (PLAN.md PR 4a) is independent and untouched.

## Decision record

State: **approved**
Human decision/date/reference: **approved by Joe, 2026-09-20**, contingent on the four follow-up
items above (documentation of the domain/nan-off interaction and per-stage counting; this retained
full-gate rerun; this packet's own corrections; `STATE.md` trimmed to ≤ 40 lines) — all four
completed in this same follow-up commit.
Approved candidate/scope: PLAN.md M3 PR 3d (ADR-0020 implementation), commit `56af34e` plus this
follow-up commit, on branch `worktree-m3-pr4-notional-aero-turbofan` (draft PR #40, retitled and
marked ready per this same instruction).
Merge observed: not merged — Joe squash-merges PR #40 himself.
Release authorization: not requested

Significant tradeoff rationale: none beyond Q4 itself (already Joe's own decision, not a tradeoff
made without him).
Accepted risks: none recorded; every Codex finding was fixed, not accepted as a risk.
Nonblocking follow-ups: none opened by this candidate.
Architectural decisions: this candidate implements the already-accepted
`docs/decisions/0020-aero-propulsion-true-interfaces.md`; introduces no new ADR of its own.
