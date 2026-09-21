# Review: PLAN.md M3 PR 3d — implement ADR-0020

Spec/plan: `docs/plans/pr3d-adr0020-implementation.md` (approved by Joe, 2026-09-20, including his
own Q1-Q4 design decisions); `docs/decisions/0020-aero-propulsion-true-interfaces.md`.
Builder: Claude (Sonnet 5, `claude-sonnet-5`).
Reviewer: Codex (via the installed `codex` plugin, companion 1.0.6), fresh context throughout —
general review, adversarial review, and one focused re-verify.
Scope: working-tree diff at the time of each dispatch, later committed as `56af34e` on branch
`worktree-m3-pr4-notional-aero-turbofan` (parent `a6ac7ce`). All three dispatches reviewed the same
growing candidate (the general review and the adversarial review ran against the pre-fix diff; the
re-verify ran against the diff with both findings' fixes applied, immediately before commit).
Validation: fast gate (asan preset, `ctest --preset asan`) run repeatedly through the fix rounds,
340→343 tests as new tests were added, 100% passing at every rerun; full gate (`./scripts/check-full.sh`
→ `just check`) run to completion four times across the session (three failed on issues unrelated to
Codex's own findings — a clang-format pass, a clang-tidy cognitive-complexity/unchecked-optional-access
pair, and a stale Python `kCoreApiVersion` assertion — all fixed; the fourth exited `check: OK`).

## Finding R-001

Severity: minor (P2, Codex's own scale)
Status: verified
Location: `core/src/kernel/vehicle.cpp:219-225` (pre-fix line numbers, general-review dispatch)
Requirement/ADR: ADR-0020's own no-silent-NaN guarantee (`docs/decisions/0020-aero-propulsion-true-interfaces.md`,
lines 74-83 per Codex's own citation)
Failure: the original `check_environment`-call-site design inferred "was a fault detected" from
whether the resulting source-name string (`wind_plugin_path_`/`atmosphere_plugin_path_`) was
non-empty, rather than tracking detection as its own value. A caller that ever configured wind or
atmosphere but left the corresponding diagnostic path string empty (no shipped caller does this
today, but nothing in the signature enforced it) would silently skip both the report and the
wrench-stage skip, letting a plugin compute a wrench from a known-non-finite environment sample
with no halt at all.
Requested outcome: track fault detection independently of the diagnostic name's own emptiness, or
reject a missing name at initialization.
Builder response: fixed — `non_finite_environment` (later folded into `Vehicle::prepare_stage_environment`,
extracted separately for a clang-tidy cognitive-complexity finding) is now a dedicated `bool`, set
whenever the raw field predicate fires, independent of what (if any) name accompanies it. No
init-time rejection of a missing name was added; the structural fix (detection independent of
naming) is sufficient and matches Codex's own primary recommendation.
Verification: Codex, focused re-verify dispatch, current diff at the time — "non_finite_environment
controls reporting and stage skipping independently of source-name emptiness." Confirmed fixed.
History: one round; found in the general review, fixed, confirmed in the re-verify. No dispute.

## Finding R-002

Severity: major (medium, Codex's own scale — "block on missing required fault-path acceptance
tests")
Status: verified
Location: `tests/unit/kernel/aero_propulsion_seam_test.cpp:118-124` (pre-fix, adversarial-review
dispatch)
Requirement/ADR: `docs/plans/pr3d-adr0020-implementation.md`'s own R4 acceptance criteria (the
zero-`wrench_b()`-call guarantee; a fault first appearing at RK4 stage k2; the isolated R-001
infinite-speed-of-sound case) and R6 (a configured non-finite wind).
Failure: the existing end-to-end tests (`aero_propulsion_seam_test.cpp`, `simulation_halt_test.cpp`)
proved correct halt attribution (right `HealthCheck`, right source) but not the actual call-count
guarantee, a fault appearing only from k2 onward, the isolated partial-field-failure case, or a
configured non-finite wind at the `Vehicle`/`Simulation` level — `health_monitor_test.cpp`'s own
tests called `report_non_finite_environment` directly, bypassing the caller-owned predicate
entirely. Codex: "dropping individual raw-field checks or mishandling later-stage skips could pass
this suite."
Requested outcome: add instrumented `Vehicle`-level tests for k1 and k2-only environment faults, the
isolated infinite-speed-of-sound case, configured non-finite wind, and domain rejection before
sampling; assert call counts, first-fault identity, state rollback under halt, zero stage wrench
under warn, and preservation of the k1 published wrench when later stages fail.
Builder response: fixed — added `tests/unit/kernel/vehicle_environment_test.cpp` (three tests) and
three new test plugins: `tests/plugins/counting_aero` (counts its own `wrench_b()` calls via a
registered signal), `tests/plugins/flaky_atmosphere` (finite on its own first two `at()` calls each
frame — `Simulation::step`'s own frame-start pre-sample plus `Vehicle`'s own k1 sample — then an
otherwise-finite sample with an infinite `speed_of_sound_m_s` from the third call onward, isolating
both the R-001 case and a fault first appearing at k2), `tests/plugins/nan_wind` (always non-finite
`wind_n`). `ZeroWrenchSourceCallsWhenAtmosphereIsNonFiniteFromFrameStart` proves zero calls and
state rollback for a k1 fault; `WrenchSourceCalledOnceBeforeAFaultFirstAppearingAtK2` proves exactly
one call (k1 succeeds), the correct source (not misattributed to the wrench consumer), and that the
k1 published wrench survives a later stage's own fault;
`NonFiniteWindHaltsNamingTheWindPluginBeforeAnyWrenchSourceRuns` proves the configured-wind case.
Verification: Codex, focused re-verify dispatch — "The three tests assert actual wrench-call counts
and correct fault attribution. The threshold is sound: the environment scheduler resets the
counter, `Simulation::step` samples call 1, k1 samples call 2, and k2 samples call 3 — the first
infinite speed-of-sound sample. Later faulted stages skip wrench calls." Codex additionally verified
the `flaky_atmosphere` call-count threshold (tuned empirically against a real test failure this
session) against the actual call sites in `simulation.cpp`/`vehicle.cpp`, not merely that the test
now passes. Confirmed fixed.
History: one round; found in the adversarial review, fixed (with one intermediate empirical
correction to `flaky_atmosphere`'s own call-count threshold, from `<= 1` to `<= 2`, after the first
version of the new tests themselves failed against the real call sequence — see the plan/commit for
the account), confirmed in the re-verify. No dispute.

## Completion

Zero open critical/major/minor findings. Both findings above are verified fixed, not merely
fixed-unverified — the re-verify dispatch inspected the actual current diff, not only the builder's
own claim. Re-verify's own end-to-end inspection of `Vehicle::prepare_stage_environment` and both
`requires_environment()` defaults found no additional actionable defect ("New findings: none").
Scope not covered by Codex: the four-preset/clang-tidy/pyright/pytest full-gate failures fixed
during this session (clang-format, one clang-tidy cognitive-complexity finding plus one
unchecked-optional-access finding on the newly extracted `prepare_stage_environment`, one stale
Python `kCoreApiVersion` assertion) were caught by the gate itself, not by Codex, and are recorded
here for completeness, not as Codex findings. Codex's own recommendation ("approve") is not Joe's
approval; the decision itself is recorded in `docs/reviews/pr3d-adr0020-human.md`.
