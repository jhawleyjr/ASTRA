# ADR-0016: Per-RK4-substage evaluation of applied wrench and atmosphere; wind stays frame-latched

Status: **accepted**, 2026-09-19
Owner: Joe
Related/supersedes: elaborates ADR-0004 (fixed-step RK4) — does not change RK4's tableau, `dt`, or
"one `Eom`"; only what state `WrenchSource::wrench_b()` and `AtmosphereModel::at()` see per call.
Also touches ADR-0012's territory (a `Vehicle`-level check reporting through `HealthMonitor` without
`HealthMonitor` holding model pointers) — same shape, not a conflict. Drafted with
`docs/plans/pr3c-substage-wrench.md`, revised through one Codex plan critique (six findings, all
fixed) and one correction round from Joe's own review (two findings, plus three more from a second,
narrower Codex critique pass, all fixed) before this record.

## Context

`Vehicle::integrate()` evaluated the applied wrench once per major frame, at the frame-start state,
and reused it across all four of `Integrator`'s RK4 sub-stage derivative calls, even though those four
calls use four different states. Gravity and the Earth-model terms inside `Eom::operator()` did not
have this problem — they were recomputed fresh from each stage's own state already. NESC check cases 4
and 5 (Vol II Tables 28–29: spheres under real drag, spinning at 10–30 deg/s) were the first cases in
the M2 harness where this mismatch produced a measurable, band-failing residual (both `GTEST_SKIP()`ed
since PR 3b, not deleted and not tolerance-widened, pending this fix).

Stevens, Lewis & Johnson (*Aircraft Control and Simulation*, 3rd ed., §3.4, verified directly against
the textbook) and Nutaro (*Building Software for Simulation*, §5.2, verified directly) both describe
the general numerical-integration principle this violated: a pure function of the ODE's own state
belongs inside the derivative, re-evaluated at every stage; a zero-order-held external input does not
need to be. Zipfel (*Modeling and Simulation of Aerospace Vehicle Dynamics*, 4th ed., Appendix E
§E.3.7, verified directly) confirms the symmetric case: a stochastic, first-order-correlated process
(his own CADAC++ architecture's Markov noise) must be advanced once per integration cycle, not once
per sub-stage, since doing otherwise would break its designed statistics — the same reason this
project's own Dryden turbulence model, and its bitwise-determinism guarantee, must stay frame-latched.

## Decision

`WrenchSource::wrench_b()` and the `AtmosphereModel::at()` sample it consumes are evaluated fresh at
each of `Integrator`'s four RK4 sub-stages, using that stage's own state — via a new derivative functor
local to `Vehicle::integrate()` that wraps `Eom` (unchanged, including its own `begin_frame()` mass
latch, still called once per frame) with a fresh per-stage wrench accumulation. `WindModel`'s
frame-latched shaping-filter output (`begin_frame`/`wind_n`) is **unchanged**: read at every stage, but
its value cannot vary within a frame by construction, since nothing advances it again until the next
`Simulation::step`.

`HealthMonitor::check_wrench` moves from a single frame-start call to exactly four per-stage calls per
contribution plus four for the total, evaluated **uniformly** every stage regardless of an earlier
stage's own result (a fault-dependent short-circuit was considered and rejected: it behaves
inconsistently between `warn`/`halt` actions, since `check_wrench` returns a falsy `HaltReason` under
`warn` regardless of whether a fault was detected) — recorded via a first-fault-wins side channel for
*naming*, surfaced after `Integrator::step()` returns; a wrench fault still leaves `Vehicle::state()`
at its pre-step value, now via an explicit revert rather than by never having called
`integrator.step()` in the first place, since `Integrator::step`'s `kRk4` case does not reassign its
caller's state until its own final line, after all four stages have run. This uniform counting is a
real, not cosmetic, change to `HealthMonitor`'s wrench-related counter semantics (frames-with-a-fault
→ stage-evaluations-with-a-fault), reflected in one updated pinned test value
(`tests/unit/kernel/simulation_halt_test.cpp`, `1500.0` → `4500.0`, derivation in the test's own
comment).

The vehicle's *published* applied-wrench signal (`v0.wrench_total.force_b_{x,y,z}` /
`v0.wrench_total.moment_b_{l,m,n}`, body axes matching ADR-0001's own conventions, newly core-owned on
`Vehicle`, replacing `tests/plugins/nesc_aero`'s own removed self-publish) is likewise taken only from
the `k1` (frame-start) evaluation, via a plain stage counter local to the derivative functor — not from
any `WrenchSource`'s own state, and not by inspecting `t` (which `wrench_b` does not receive, so no
call site could ever make that comparison). Removing `nesc_aero`'s own mutable, per-call self-publish
also resolves a new tension this decision otherwise creates: `wrench_source.hpp`'s own doc comment now
states that a conforming `WrenchSource::wrench_b()` must be callable multiple times per frame with no
hidden per-call state, which a mutable self-publish inside a `const` method would itself have
violated.

No plugin-facing interface changes: `WrenchSource::wrench_b()`'s signature is unchanged,
`EnvironmentSample` gains no field, and `kCoreApiVersion` is not bumped — the correct test for that
version is which type crosses the versioned `dlopen` loader boundary between a plugin `.so` and
`libastra_core.so`, not a blanket "vtable changes only" rule (`core_api.hpp.in`'s own bump policy names
`StepContext`, a plain struct, explicitly). `Vehicle::PhysicsSpec` gains one field
(`AtmosphereModel* atmosphere`) but is constructed only inside `libastra_core` itself
(`Simulation::from_config`/`for_testing`), never across that boundary.

A new, disclosed atmosphere-domain misattribution window — a mid-stage crossing of
`AtmosphereModel::valid_range()` now surfaces as `kNonFiniteWrench`, naming the consuming
`WrenchSource`, rather than `kOutOfDomain`, naming the atmosphere plugin, one frame sooner than
ADR-0012's own post-step check would have fired — is the same class of gap `Vehicle::check_domain`'s
own doc comment already discloses and Joe already accepted for gravity/Earth (ADR-0012's own "a domain
a single frame can jump clean across"), now newly instanced for atmosphere. Tracked as a `PLAN.md`
follow-up, not engineered around in this decision: no NESC case 1–10 crosses an atmosphere boundary
mid-run, so this decision's own acceptance evidence cannot exercise it either way.

Full technical design: `docs/plans/pr3c-substage-wrench.md`'s "Design: `Vehicle::integrate()`"
section.

## Rejected alternatives

- **Keep once-per-frame wrench, reduce `dt`.** Zipfel's own CADAC++ architecture (`newton` module:
  forces computed once per integration cycle, accuracy bought by a smaller `int_step`) — not a
  strawman, and measured directly against the actual, unmodified frozen-wrench code before being
  rejected. `altitude`/`v_down` do converge into their ADR-0015 bands at 2x cost, linearly in `dt`. But
  `v_north` and `latitude` — swept at the same three `dt` values, measured, not extrapolated — shrink
  exactly linearly with `dt` too and still miss by **over 1.2 million times** and **~1385 times** their
  own `tol_abs` respectively even at `dt=0.00025` (4x cost, the same cost this decision's own design
  carries): closing `v_north`'s miss by `dt` reduction alone would need `dt` ≈ 2×10⁻¹⁰ s, a
  ~4.85-million-fold increase in frame count for one 30 s run. (An earlier draft of this evidence
  wrongly claimed `force_x`/`force_y`/`force_z` also fail today and need ~16x `dt` reduction to close —
  a one-frame sampling-order artifact in the throwaway benchmark driver used to gather that evidence,
  not real physics; retracted. The real committed baseline shows those three channels already pass at
  `dt=0.001`, unmodified.) Rejected as treating a qualitative state-mismatch (wrench evaluated at the
  wrong state) as a quantitative resolution problem, and shown not to be a full fix at any tested or
  practically-extrapolated `dt` regardless.
- **Give `Integrator::step` an early-abort hook** (change the `StateDerivative` concept to return more
  than `StateVector`, let `Integrator::step` return a `HaltReason` and stop after the first bad stage
  instead of running all four). Rejected: this changes a stable, 30-line, heavily-tested core
  primitive's public contract for every existing and future caller (including
  `tests/unit/kernel/integrator_test.cpp`'s plain physics-only derivatives, which know nothing about
  `HealthMonitor`), to save a little compute in an already-rare fault path. The chosen "run to
  completion, then revert" design gets the identical outward guarantee (`state()` untouched on a
  fault) without touching `Integrator` at all.
- **Encode the stage-pure/frame-latched split in `EnvironmentSample`'s type**, or add a new
  `WrenchSource`/`WindModel` virtual declaring it. Rejected: the split is already fully determined by
  each model interface's own existing contract (`AtmosphereModel::at()` is declared pure over altitude
  alone; `WindModel::wind_n()` is declared a pure read of a per-frame latch), and a new virtual would
  force a `kCoreApiVersion` bump for zero behavioral benefit.
- **Move the per-stage wrench-accumulation logic into `Eom` itself** (give `Eom` a reference to the
  vehicle's `WrenchSource` list and environment models). Rejected: `Eom`'s entire value (ADR-0004:
  "there is exactly one `Eom`", exposed unchanged to Python) is that it is a small, pure, physics-only
  functor with no knowledge of plugins, health checks, or per-vehicle bookkeeping. Threading
  `WrenchSource`s and `HealthMonitor` through `Eom` would entangle two concerns this codebase's
  layering (`core/kernel/` vs `core/interfaces/`) keeps deliberately separate, for a design exactly as
  achievable — and no cheaper — than a wrapping functor local to `Vehicle::integrate()`.

## Consequences and validation

**Benefits.** Fixes the diagnosed root cause of NESC cases 4/5's disclosed frozen-wrench gap,
`dt`-independently; zero plugin-facing interface changes; zero new dependencies.

**Costs.** `WrenchSource::wrench_b()` called 4x/frame instead of 1x (measured in isolation: ~26 ns
added per frame for one drag source — 0.0026% of the 1 ms real-time deadline, ~6% of one unusually
cheap toy scenario's own current per-frame cost); `AtmosphereModel::at()` called 5x/frame instead of
1x; `HealthMonitor`'s wrench-related counters change from frames-with-a-fault to
stage-evaluations-with-a-fault; `Vehicle::PhysicsSpec` gains one field; `Vehicle` gains two new private
members (`atmosphere_`, `published_wrench_`).

**Risks.** The atmosphere-domain-misattribution window above (tracked as a `PLAN.md` follow-up, not
exercised by any current case). Migration/rollback: fully reversible, single-PR change, no data or
schema migration.

**Validation — the dry run this decision was gated on** (`asan` preset, `ctest`, cases 4 and 5's
`GTEST_SKIP()` lifted, no golden regenerated, no tolerance widened): full per-channel
`max(|diff|)/tol_abs` across all 301 samples (t = 0…30 s) of each case, computed against the actual
implementation, not estimated:

| Channel | case04 | case05 | | Channel | case04 | case05 |
|---|---|---|---|---|---|---|
| latitude | 0.0000 | 0.0000 | | rate_r | 0.1111 | 0.1111 |
| longitude | 0.0000 | 0.1220 | | force_x | 0.1344 | 0.1343 |
| altitude | 0.1228 | 0.1228 | | force_y | 0.1292 | 0.1292 |
| v_north | 0.0033 | 0.0044 | | force_z | 0.1431 | 0.1430 |
| v_east | 0.0042 | 0.1273 | | moment_l | 0.0000 | 0.0000 |
| v_down | 0.1288 | 0.1287 | | moment_m | 0.0000 | 0.0000 |
| yaw | 0.1137 | 0.0112 | | moment_n | 0.0000 | 0.0000 |
| pitch | 0.1135 | 0.0295 | | density | 0.1228 | 0.1228 |
| roll | 0.1139 | 0.0238 | | pressure | 0.1937 | 0.1936 |
| rate_p | 0.0836 | 0.1111 | | temperature | 0.1228 | 0.1228 |
| rate_q | 0.1111 | 0.1111 | | | | |

Every channel, both cases, passes (ratio ≤ 1.0); worst is `pressure` at 0.194, well inside its band —
no channel near the boundary. `v_north`, previously missing by 4.85 million times `tol_abs` under the
frozen-wrench code, now misses by 0.0033–0.0044. Full test suite (`asan` preset): 290/290 pass,
including all ten NESC cases, the new zero-allocation fixture for the per-stage atmosphere+wrench code
path, the new non-`k1`-stage-fault regression test, and the new `wrench_total` stage-1-publish proof —
`git status` confirms no file under `tests/golden/` or elsewhere in the oracle changed.

Acceptance commands: `docs/plans/pr3c-substage-wrench.md`'s own Acceptance table (R1–R13).

## Approval

**Accepted by Joe, 2026-09-19**, after the plan's own correction round 1/2 (Joe's two findings plus a
second Codex critique pass's three further findings, all fixed) and this dry run's full per-channel
table, reported and reviewed before this record was written. `touch-goldens` armed by Joe for this one
file only; nothing under `tests/golden/` was written.
