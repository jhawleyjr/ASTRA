# Human review packet: PR 3c — per-RK4-substage wrench evaluation

PR / candidate base and head: base `a3d23f7` (`origin/main`'s current tip at the time review ran,
confirmed via `git merge-base HEAD origin/main` — no drift); head `d9906ea`, committed and pushed to
`worktree-m2-pr3c-substage-wrench`, draft PR [#34](https://github.com/jhawleyjr/ASTRA/pull/34)
against `main` — **not marked ready**. All validation and review evidence below ran against this
exact working-tree diff before it was committed; `git diff a3d23f7 d9906ea` is that same diff,
unchanged by the commit itself.
Spec/plan/task: `docs/plans/pr3c-substage-wrench.md` (spec + repository-aware plan + draft ADR,
approved by Joe 2026-09-19 after one Codex plan critique and one correction round). `PLAN.md`'s own
PR 3c line.
Risk: **Tier 3** — `core/kernel/` (`Vehicle::integrate()` rewritten), health behavior
(`HealthMonitor::check_wrench` counting semantics), `plugins/`-adjacent test fixtures.
Decision requested: **approve behavior and merge** (Joe squash-merges in this solo-maintainer repo).

## What changes and why

**Before**: `Vehicle::integrate()` evaluated every `WrenchSource::wrench_b()` and the atmosphere
sample it reads **once per major frame**, at the frame-start state, then reused that single wrench
across all four of `Integrator`'s RK4 sub-stage derivative calls — even though gravity and the
Earth-model terms inside `Eom::operator()` were already recomputed fresh at each sub-stage's own
state. This frozen-wrench mismatch was NESC check cases 4/5's (Vol II Tables 28/29: spheres under
real drag, spinning at 10–30 deg/s) disclosed, `GTEST_SKIP()`ed gap since PR 3b.

**After** (ADR-0016): `wrench_b()` and the atmosphere sample it reads are evaluated fresh at each of
the four RK4 sub-stages, via a new derivative functor local to `Vehicle::integrate()` that wraps the
unchanged `Eom`. Wind stays exactly as frame-latched as before (Dryden's `begin_frame`/RNG draw
still runs exactly once per frame — verified, not merely asserted, by an independent adversarial
review re-running the Dryden bitwise-determinism tests). A wrench fault still leaves `Vehicle::state()`
exactly as if the frame never ran, now via an explicit revert to a saved pre-step state rather than by
never having called `integrator.step()` (which now always runs to completion first). The vehicle's
applied-wrench signal is published core-side from the frame-start (`k1`) stage only
(`v0.wrench_total.force_b_{x,y,z}`/`moment_b_{l,m,n}`), replacing `tests/plugins/nesc_aero`'s own
removed self-publish (a mutable, per-call publish inside a now-4x/frame-called `wrench_b()` would
itself violate the new purity contract `wrench_source.hpp` documents).

No plugin-facing ABI change; `kCoreApiVersion` is not bumped (reasoning in the plan/ADR). No new
config keys. Diff summary: 13 files modified (+394/−108), 3 new files (the plan document, the ADR,
one new test config).

**Unexpected-scope check**: the diff also required (a) splitting one existing test
(`WindCallContract.BeginFrameThenWindNAreEachCalledExactlyOncePerFrame`) into two invariants, since
its old single assertion conflated `wind_n()`'s own call count (still 1x/frame, correct) with
`wrench_b()`'s (now 4x/frame, the intended change), and (b) updating one pinned counter value
(`simulation_halt_test.cpp`, `1500.0` → `4500.0`, derivation in the test's own comment) — both
disclosed in the plan document before implementation, not discovered as surprises during review.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `just check` (all four presets: debug, release, static-plugins, asan) | **check: OK**, exit 0, 2026-09-19. First attempt failed `format-check` (line-wrap violations in new comments); fixed with `clang-format-18 -i`, rebuilt clean, reran full gate clean | `/tmp/.../scratchpad/just-check2.log` (session-local; not committed) |
| Per-preset test totals | 290/290 pass × 4 presets (debug/release/static-plugins/asan); `hooks: 144 passed, 0 failed`; `pyright`: 0 errors; `pytest`: 32/32 | same log |
| NESC dry run, cases 4/5 skip-lifted (asan) | All 10 cases pass, including 4 and 5; full per-channel `max(\|diff\|)/tol_abs` table computed against the actual implementation (not estimated) — every channel, both cases, ≤ 1.0; worst is `pressure` at 0.194 | `docs/decisions/0016-per-rk4-substage-wrench-evaluation.md`'s own Consequences and Validation section has the full 21-channel × 2-case table |
| Zero-allocation, new code path (R5) | Pass: `NoAlloc.ZeroHeapAllocationsOverOneThousandFramesWithSubstageWrenchAndAtmosphere` — the only existing no-alloc fixture combining a `WrenchSource` with `physical.atmosphere` | `tests/configs/no_alloc_substage_wrench.yaml`, `tests/physics/no_alloc_test.cpp` |
| Non-`k1`-stage fault regression (R4) | Pass: `SimulationHalt.WrenchFaultInvisibleAtFrameStartStillHaltsAndRevertsState` — a fault visible only from `k2` onward still halts, names the correct plugin, and `state()` reverts exactly | `tests/unit/kernel/simulation_halt_test.cpp`; independently re-verified by adversarial review as genuinely triggering at k2, not k1 |
| Stage-1-publish proof (R12) | Pass, all ten cases, every sample: an independently-loaded `nesc_aero` instance evaluated at each sample's preserved pre-step state matches the core-published `wrench_total` signal to 1e-12 | `tests/physics/nesc/nesc_cases_test.cpp` |
| `dt`-reduction alternative | Measured (not asserted) and rejected: `v_north` still misses by >1.2 million× `tol_abs` even at 4x the frame cost this PR's own design carries | `docs/plans/pr3c-substage-wrench.md`'s Alternatives section |

CI is intentionally local-gate mode; the above is the full local gate, not a GitHub check result.
Toolchain: Ubuntu 24.04 apt, clang-18, the pinned dev group in `pyproject.toml` — unchanged by this
PR. No new dependency.

## Independent review

Builder: Claude (this session). Reviewer: Codex (companion, working-tree scope, fresh context both
passes).

- **General review**: no actionable defects. Reviewer independently ran 45 targeted ASan tests
  (simulation halts, wind contracts, NESC aero, all ten NESC cases, allocation checks) against the
  existing binaries; all passed.
- **Adversarial review**, focus per Joe's own four points: **verdict approve, no material findings**.
  (a) state-revert path after a non-`k1` fault — sound in both `Integrator` branches (RK4 and Euler);
  the new regression test genuinely triggers at `k2`, confirmed by the reviewer, not accidentally at
  `k1`. (b) health-counter semantics under `warn`/`halt` — `4500.0` is arithmetically correct against
  `health_monitor.cpp`'s actual `check_wrench` implementation; no other exact-count wrench-signal
  assertion was missed (reviewer grepped the whole `tests/` tree). (c) two-run bitwise determinism
  with Dryden — reviewer independently re-ran all three Dryden-parameterized determinism cases;
  wind stays frame-latched, all passed bit-identical. (d) ADR-0001/0002 units/frames on the new
  `wrench_total` signals — body-axis, correct SI units (`N`/`N.m`), same quantity `nesc_aero` used to
  self-publish, no sign/frame/unit change introduced by moving the publish core-side.

Zero fix rounds needed (both passes clean on first dispatch); the plan's own correction-round budget
(1/2 used, from plan review before implementation) is unaffected and remains at 1/2, not consumed
further.

## Joe's attention

**Consequential files**: `core/src/kernel/vehicle.cpp` (`Vehicle::integrate()`, fully rewritten —
the end-to-end path this candidate actually changes: environment latch → **integrator step, now
containing the per-stage wrench/atmosphere/health-check loop** → post-step, per
`docs/architecture.md` §5's updated order); `core/include/astra/kernel/vehicle.hpp` (`PhysicsSpec`
gains `atmosphere`; `Vehicle` gains `atmosphere_` and `published_wrench_`, the latter a
`unique_ptr<Wrench>` specifically to avoid a dangling `SignalRegistry` pointer across the
`Simulation::from_config`/`for_testing` move of the local `Vehicle` into `vehicles_` — this exact
hazard was caught by Codex's plan critique before implementation, not found live).

**Deleted behavior**: `tests/plugins/nesc_aero`'s own self-published `force_b_*`/`moment_b_*`
signals are gone (replaced by the core-owned `wrench_total.*` signals) — covered by R12's own proof
that the replacement is behaviorally equivalent (same value, now correctly timed).

**Assumptions worth Joe's own read**: `wrench_source.hpp`'s new documented contract (`wrench_b()`
may be called multiple times per frame and must be a pure function of its two arguments) is asserted
of `nesc_aero`/`nan_wrench`, the only two existing implementations, both verified compatible — a
*future* `WrenchSource` (M3's real `AeroModel`/`PropulsionModel`) inherits this contract but is not
itself tested by this PR.

**Disclosed, accepted risk (not fixed, not this PR's scope)**: a new atmosphere-domain
misattribution window — a mid-RK4-stage crossing of `AtmosphereModel::valid_range()` now surfaces as
`kNonFiniteWrench` (naming the consuming `WrenchSource`) rather than `kOutOfDomain` (naming the
atmosphere plugin), one frame sooner than the existing post-step check would fire. Same accepted gap
class as ADR-0012's own gravity/Earth instance; no NESC case 1–10 exercises it either way. Tracked as
a `PLAN.md` follow-up.

**Rollback**: fully reversible, single working-tree diff, no data/schema/golden migration; reverting
restores the exact prior frozen-wrench behavior and re-`GTEST_SKIP()`s cases 4/5 (the skip text is
still present in git history if ever needed again, though the intent is not to need it).

## Decision record

State: pending
Human decision/date/reference: pending — this packet, PR #34
Approved candidate/scope: pending (candidate: `d9906ea` against base `a3d23f7`)
Merge observed: not merged
Release authorization: not requested
Significant tradeoff rationale: `WrenchSource::wrench_b()` now costs 4x/frame instead of 1x
(measured in isolation: ~26 ns added per frame for one drag source — 0.0026% of the 1 ms real-time
deadline) to close a gap the measured `dt`-reduction alternative could not close at any practical
cost (`v_north` still >1.2 million× `tol_abs` at 4x cost).
Accepted risks: the atmosphere-domain-misattribution window above; owner Joe/next scenario that
exercises it; follow-up trigger is any future case whose trajectory crosses an atmosphere boundary
mid-run.
Nonblocking follow-ups: (1) `HealthMonitor`'s wrench-related counters could be deduplicated to
"once per frame" instead of "once per stage-evaluation" — considered and deliberately not done (an
action-dependent short-circuit was worse); (2) a per-`WrenchSource`-contribution `wrench_total`
signal breakdown (vs. today's vehicle-total-only) deferred until a second `WrenchSource` on one
vehicle actually exists (M3).
Architectural decisions: `docs/decisions/0016-per-rk4-substage-wrench-evaluation.md` (accepted
2026-09-19, elaborates ADR-0004).
