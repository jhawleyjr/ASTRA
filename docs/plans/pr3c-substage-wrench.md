# PR 3c — per-RK4-substage wrench evaluation

Status: **approved** (Joe, 2026-09-19, after correction round 1/2 — see "Correction round 1/2" and
"Approval" below for the record; three follow-up items folded into implementation directly, no
further plan round)
Owner: Joe
Risk: **Tier 3** — touches `core/kernel/` (`Vehicle`, `Eom` usage), `plugins/physical/` consumers
(`WrenchSource`), and health behavior (`docs/process/workflow.md`'s risk-tier table names all three
explicitly). Minimum process: full relevant artifacts (this document), Joe approves spec/plan,
independent numerical/adversarial review, full gate, Joe reads the critical diff/tests.
Intent/approval: `PLAN.md`'s own PR 3c line (2026-09-19: both stated preconditions — ADR-0015's
`tol_rel` drop and SIM 2 exclusion — resolved); Joe's design brief this session (2026-09-19) sets the
per-RK4-stage-vs-per-frame classification below as the default to confirm or refute, and requires the
"reduce `dt`" alternative be evaluated with measured numbers, not asserted away.
Architecture/ADRs: `docs/architecture.md` §5 (per-frame order: "wrenches computed once, before EOM
derivative → integrator step" — the sentence this PR changes), §7 (model interfaces), §8
(integration), §14 (per-frame path rules), §15 (health monitoring). ADR-0004 (fixed-step RK4) —
**elaborated, not superseded**: RK4 itself, `dt = 1 ms`, and "one Eom" are unchanged; only *what state
`WrenchSource::wrench_b()` sees at each stage* is a new decision ADR-0004 left implicit. ADR-0012
(per-frame domain validity, the nearest precedent for a `Vehicle`/`HealthMonitor` split). ADR-0013 /
ADR-0014 / ADR-0015 (NESC oracle) — this PR's acceptance target.
System context: closes `PLAN.md` M2 PR 3b's disclosed cases 4/5 gap. The root cause is already
diagnosed and committed to source: `tests/physics/nesc/nesc_cases_test.cpp`'s own `GTEST_SKIP()`
comment on `Case04DroppedSphereRoundNonRotation` names it exactly — "the applied wrench is frozen
across one frame's four RK4 sub-stages while gravity is recomputed fresh each sub-stage." This
document's job is to fix that mismatch for `wrench_b()` (and the atmosphere sample it consumes)
without changing anything else about `Eom`, `Integrator`, or `WrenchSource`'s public signature.

## Outcome and boundaries

**Problem.** `Vehicle::integrate()` (`core/src/kernel/vehicle.cpp`) evaluates every `WrenchSource` and
sums the total wrench **once**, at the frame-start state, then hands it to `Eom::set_applied_wrench()`
before calling `integrator.step()`. `Eom::operator()` — the RK4 derivative functor — is called four
times per frame (`k1..k4`, `core/include/astra/kernel/integrator.hpp`) with four *different* states
`y`, but `applied_wrench_` never changes across those four calls: the wrench is frame-latched even
though `Eom::operator()` recomputes gravity, the Earth-rate terms, and the Coriolis-like term fresh at
every stage's own `y` (`core/src/kernel/eom.cpp`, lines 29–42). NESC cases 4 and 5 (Vol II Tables 28,
29 — spheres with real drag spinning at 10–30 deg/s) are the first check cases where this mismatch is
large enough to fail the ADR-0015 bands; both are `GTEST_SKIP()`ed today, not deleted and not
tolerance-widened, pending this PR.

**Goal.** `WrenchSource::wrench_b()` and the atmosphere sample it reads are evaluated fresh at each of
the four RK4 sub-stages, using that stage's own state, while wind stays exactly as latched once per
frame today (Dryden's PSD and bitwise-determinism tests depend on `begin_frame`/the RNG draw
happening exactly once per frame — this PR must not touch that). Cases 4 and 5's `GTEST_SKIP()` is
lifted once the full per-channel ADR-0015 band passes (or a specific residual is characterized and
separately disclosed to Joe, same discipline `PLAN.md` already used for the gravity-centripetal fix).

**Non-goals.** No change to `Integrator`, RK4's tableau, `dt`, `Eom`'s public interface, or
`WrenchSource::wrench_b()`'s signature. No change to Dryden's timing (`begin_frame`/RNG draw stays
once per frame). No attempt to fix the *other* disclosed residual named in `PLAN.md`'s PR 3c line (the
Cowork-lead, unverified zero-order-hold-on-drag account of the post-gravity-fix `altitude`/`v_down`
miss) beyond checking whether this fix happens to collapse it — that check is this PR's own acceptance
evidence, not a design goal to engineer toward.

**Invariants preserved.**
- ADR-0004: RK4 stays a pure function of `(t, y)` over the fixed 13-vector; zero heap allocation,
  zero exceptions in `step()`.
- ADR-0002/ADR-0001: no new units, no new frame conventions.
- §14/§15: per-frame path stays alloc-free and exception-free; `HealthMonitor` still names the
  offending plugin on a wrench fault, and a wrench fault still leaves `Vehicle::state()` as if the
  frame never ran (see "state() untouched" below — the *mechanism* changes, the *outward guarantee*
  does not).
- `wind_model.hpp`'s three-way split (`step` draws RNG once per frame; `begin_frame` evolves the
  shaping filter once per frame per vehicle; `wind_n` is a pure `const noexcept` read) is untouched —
  this PR reads `wind_n` the same number of times per frame it does today conceptually (its return
  value literally cannot change within a frame, since nothing calls `begin_frame` again until the
  next `Simulation::step`), just from more call sites.

**Compatibility.** No plugin-facing ABI changes — see "`kCoreApiVersion`" below. Both existing
`WrenchSource` plugins (`nesc_aero`, `nan_wrench`) happen to be pure functions of their two arguments
with no per-call state, so they behave correctly called 4x/frame instead of 1x. **Correction from
Codex's plan critique**: `wrench_source.hpp`'s current doc comment does *not* actually promise this —
it documents evaluation "at the frame's state" using "the frame-latched environment," which describes
today's one-call-per-frame reality, not a guarantee of safety under repeated per-stage calls at
different states. This PR must therefore make that guarantee **explicit**, not assume it already
existed: a doc-comment-only update to `wrench_source.hpp` (no signature change) stating that
`wrench_b()` may be called multiple times per frame, once per RK4 sub-stage, each with that stage's
own `vs`, and that a conforming implementation must be a pure function of its two arguments with no
hidden per-call state — turning `PLAN.md`'s own words ("a real ... `WrenchSource` contract change, not
a bolt-on") into a written contract instead of an implicit one. See "Signal-publishing side effect"
below for the one place that contract's *spirit* still needs restating for existing and future
implementers.

Assumptions:
- `WrenchSource::wrench_b()` implementations are a pure function of their two arguments with no
  internal state that would make repeated per-frame calls at different `y` incorrect. **Correction from
  Codex's plan critique**: this is empirically true of `nesc_aero` and `nan_wrench` today, but
  `wrench_source.hpp` does not currently document it as a required property (see "Compatibility" above)
  — this PR both relies on it and is what first writes it down as an explicit interface obligation
  (R10), not a pre-existing guarantee it merely exercises under new load.
- `AtmosphereModel::at()` and `EarthModel::altitude()` are pure, `noexcept`, allocation-free functions
  of their arguments (already documented and already true — both are called today, once per frame,
  from `Simulation::step`).
- This PR does not itself validate that a *future* stateful `WrenchSource` (one that, say, integrates
  its own internal history) would be correct under 4x-per-frame evaluation — no such model exists yet;
  a future one inherits the same "pure function" obligation §14 already states.

## Per-input classification (the core design question)

Joe's brief supplies a default rule to confirm or refute: *a pure function of the stage state belongs
inside the derivative, evaluated fresh at every RK4 stage with the stage's own `y`; anything
stochastic, stateful, or discrete-time is latched once per frame.* Checked against the actual
textbooks in `docs/refs/textbooks/` (verified directly against the PDFs this session, not taken on
faith from the cited page numbers):

- **Stevens, Lewis & Johnson, *Aircraft Control and Simulation*, 3rd ed., §3.4 "Numerical Solution of
  the State Equations."** p.170: "the time step must be made small enough that the control input can
  be approximated by a constant value during any interval `kT` to `(k+1)T`" — RK4's own derivation
  assumes a zero-order-hold *control input* held across the step, with the *state* re-evaluated fresh
  at each stage. p.177's own RK4 code listing (`function [xnew] = RK4(f, time, dt, xx, u)`) makes this
  literal: `u` is the same value passed into all four `feval(f, ..., x, u)` calls; only `x` and `time`
  change per call. Confirmed exactly as cited.
- **Nutaro, *Building Software for Simulation*, §5.2 "Networks of Continuous Systems," p.186.**
  "A continuous input `u_c` is incorporated into the state space of the model as follows. If `u_c` is
  a function of `x` alone, replace `f(x, u_c(x))` with `z(x) = f(x, u_c(x))` and solve the differential
  equation `ẋ = z(x)`." — i.e., an input that is a pure function of the state is *folded into the
  derivative itself*, not held as a separate frozen quantity; the whole point is that it then
  re-evaluates automatically at every stage a generic ODE solver takes. Confirmed exactly as cited.
- **Zipfel, *Modeling and Simulation of Aerospace Vehicle Dynamics*, 4th ed., Appendix E "CADAC++
  Architecture," §E.3.7 "Monte Carlo Capability," pp.611–612.** "MARKOV models a Markov process with
  Gaussian distribution and first-order time correlation. It has to be called every integration
  cycle... At every integration step, for each vehicle, the Markov noise function is called in
  function `execute(...)`: `vehicle_list[i]->markov_noise(sim_time, int_step, nmonte)`... Because
  Markov noise is a first-order correlation process, the current value depends on the previous value."
  Confirmed: Zipfel's own reference C++ simulation framework advances stochastic (Markov) processes
  once per **integration cycle** (i.e., once per major frame, regardless of the integrator's internal
  RK4 sub-stages), not once per sub-stage — the same rule Joe's brief states for Dryden. This appendix
  also confirms the Alternatives section's "keep once-per-frame wrench, reduce `dt`" is CADAC++'s own
  actual architecture (`newton` module's `exec()`/`integrate()` split, p.602–603): CADAC++ computes
  forces once per integration cycle and lets a smaller `int_step` be the accuracy knob, exactly the
  alternative below — not a strawman.

**Verdict: the default rule is confirmed, and the current codebase already gets three of its four
inputs right.**

| Input | Pure function of stage state? | Today | After this PR | Citation |
|---|---|---|---|---|
| Gravity (`gravity_.g_n(vs)`, inside `Eom::operator()`) | Yes — `GravityModel::g_n` takes only `VehicleState` | **Already per-stage** (unchanged by this PR) | Per-stage | Nutaro §5.2 p.186 |
| Earth rate / transport rate (`earth_.omega_ie_n`, `omega_en_n`, `p_n_dot`) | Yes | **Already per-stage** | Per-stage | Nutaro §5.2 p.186 |
| Mass properties / inertia (`Eom::begin_frame`) | Trivially yes (constant in v1 — ADR-0003), but re-evaluating it would be pure waste, not a modeling choice | Per-frame (`begin_frame`, cached) | **Unchanged** — per-frame, but because the function is *constant*, not because it is "stateful" | Deferred fuel-burn note in `eom.hpp` |
| `WrenchSource::wrench_b(vs, env)` | Yes — declared `const noexcept`, pure function of its two arguments (`wrench_source.hpp`) | **Per-frame (bug)** | **Per-stage** | Stevens/Lewis/Johnson §3.4 p.177 (state re-evaluated, not held); Nutaro §5.2 p.186 |
| `AtmosphereModel::at(altitude_m)` feeding `wrench_b`'s `env.atmosphere` | Yes — `const noexcept`, pure function of altitude | **Per-frame (bug)** | **Per-stage**, recomputed from the stage's own altitude | same as above |
| `WindModel` shaping-filter state (Dryden's `u_, v_x_, w_x_`) | **No** — a Markov/first-order-correlation process, explicitly stateful and RNG-driven (`step()` draws this frame's normal() samples; `begin_frame` advances the filter using them) | Per-frame (`begin_frame`, once per vehicle) | **Unchanged — per-frame** | Zipfel 4e App. E §E.3.7 pp.611–612 ("has to be called every integration cycle"); this is the textbook case the rule's stochastic/stateful exception describes |
| `EnvironmentSample.wind_n` as read by a `WrenchSource` | `wind_n()` is `const noexcept`, but it is a *read of the frame-latched filter output above*, not an independent pure function — the value cannot change within a frame regardless of how many times or with what `vs` it is called | Read once (folded into the frame-start `env`) | **Read up to 4x/frame, but returns the identical value every time** (see below) | wind_model.hpp's own constness argument |
| `v_rel_b = vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n)` (inside `nesc_aero`, and any future drag-like `WrenchSource`) | N/A — this is the consumer, not an input | Frame-start `v_b`/`q_nb` only | **Stage `v_b`/`q_nb`, frame-latched `wind_n`** — falls out automatically once `wrench_b` is called per-stage with a per-stage `vs` and a `wind_n` that is (correctly) the same value at every stage | Joe's brief: "v_rel uses stage velocity with frame-latched gust" |

## How `EnvironmentSample` expresses the split

**It doesn't — and shouldn't.** `EnvironmentSample` (`core/include/astra/kernel/environment_sample.hpp`)
stays exactly as it is today: two fields, `wind_n` and `atmosphere`, no new members, no type-level
marker for "this field is stage-pure" vs "this field is frame-latched." The split is expressed
**operationally**, in `Vehicle::integrate()`'s code, not structurally in the type:

- `Simulation::step()` is **unchanged**: it still builds exactly one frame-start `EnvironmentSample`
  per vehicle per frame (`wind_n` from `begin_frame`+`wind_n()`, `atmosphere` from
  `atmosphere_->at(altitude_m)` at the frame-start altitude) and hands it to `Vehicle::set_environment()`.
  This frame-start sample is **not removed or bypassed** — `tests/physics/nesc/nesc_cases_test.cpp`
  reads it directly (`sim.vehicles().front().environment()`, line 166) to check the `density`,
  `pressure`, and `temperature` channels against the NESC bands, a real, load-bearing dependency this
  plan must not break.
- `Vehicle::integrate()` gains a **second, transient, stack-local `EnvironmentSample`**, built fresh
  inside the new per-stage derivative functor (below) for each of the four RK4 calls: `.wind_n` is
  copied verbatim from the frame-latched `environment_.wind_n` (never recomputed — this is the whole
  mechanism by which "wind stays frame-latched" is enforced); `.atmosphere` is recomputed from that
  stage's own altitude via `atmosphere_->at(eom_->earth().altitude(VehicleState{y_stage}))`. This
  second sample is discarded after each stage's derivative call — never written back into
  `environment_`, never visible to `nesc_cases_test.cpp` or any other reader of
  `Vehicle::environment()`.

Rejected alternative: adding a `stage_atmosphere`/`is_stage_pure()`-style marker to `EnvironmentSample`
or a new virtual on `WrenchSource`/`WindModel` declaring which of its inputs are stage-safe. Rejected
because (a) the split is fixed and already fully determined by which *model interface* owns each
field — `AtmosphereModel::at()` is declared pure over altitude alone, `WindModel::wind_n()` is declared
a pure read of a per-frame latch — so there is nothing left for a plugin to "declare" that its own
interface doesn't already say; (b) a new virtual changes `WindModel`'s or `WrenchSource`'s vtable,
forcing a `kCoreApiVersion` bump for zero behavioral benefit; (c) this codebase's own convention
(`EnvironmentRequirements`'s two plain booleans, not a trait system) favors a small, fixed struct plus
a code comment over a type-level generalization for a two-field, well-understood split.

## Repository findings

- `Vehicle::integrate()` (`core/src/kernel/vehicle.cpp:93-143`) is the one function this PR rewrites.
  Today: pre-step domain check (frame 0 only) → sum `wrench_b()` over `wrench_sources_` once, at
  `vs = VehicleState{state_}` (frame-start) → per-contribution and total `health.check_wrench()` →
  `eom_->set_applied_wrench(total)` → `eom_->begin_frame()` → `integrator.step(*eom_, ctx.t, state_)`
  → post-step `check_state`/domain check/renormalize.
- `Integrator::step`'s `kRk4` case (`core/include/astra/kernel/integrator.hpp:77-88`) calls its
  functor `f` exactly four times: `f(t, y)`, `f(t+dt/2, y+dt/2·k1)`, `f(t+dt/2, y+dt/2·k2)`,
  `f(t+dt, y+dt·k3)`. **`y` (the caller's `state_`) is not reassigned until the single line at the end
  of the case** (`y = y + dt/6·(k1+2k2+2k3+k4)`) — meaning `state_` is provably unchanged for the
  entire duration of the four `f` calls, a fact this plan's revert mechanism (below) depends on.
- `Eom` (`core/include/astra/kernel/eom.hpp`) needs **zero changes**. `Eom::operator()` already
  recomputes every gravity/Earth term fresh from its `y` argument; it only reads `applied_wrench_`
  (set via `set_applied_wrench`), and nothing stops calling `set_applied_wrench` again immediately
  before each of the four `(*eom_)(t, y)` calls from inside a wrapping functor — which is exactly this
  plan's mechanism.
- `WrenchSource`, `AtmosphereModel`, `WindModel`, `EarthModel`, `GravityModel`, `HealthMonitor`: **zero
  interface changes**. `Vehicle::PhysicsSpec` (`core/include/astra/kernel/vehicle.hpp:47-53` — public,
  but constructed only by core-internal call sites today; never crosses the plugin `dlopen` boundary in
  practice, see "`kCoreApiVersion`" below for the precise, corrected claim) gains one new field:
  `AtmosphereModel* atmosphere = nullptr;` — both call sites that construct one
  (`Simulation::from_config`, `core/src/kernel/simulation.cpp:338`, and `Simulation::for_testing`,
  `simulation.cpp:468`) need updating to pass their already-held `atmosphere`/`nullptr`.
- `Vehicle` gains one new private member, `AtmosphereModel* atmosphere_ = nullptr;`, set in `init()`
  from `physics->atmosphere`. No new `EarthModel*` member is needed — `eom_->earth()` (already public,
  `const`, `noexcept`) supplies the altitude call.
- `Vehicle` gains a second new private member, `std::unique_ptr<Wrench> published_wrench_;`
  (heap-allocated in `init()`, not by value — the same reason `Simulation::health_` is a `unique_ptr`
  and not a by-value member: a plain `Wrench` member would dangle the moment `Simulation::from_config`/
  `for_testing` moves the local `Vehicle` into `vehicles_`), plus six new `SignalRegistry` entries
  registered in `init()` (see "Signal-publishing side effect," resolved, below) — resolves Finding 2 /
  Q2 from Joe's review of this document's first draft.
- `nesc_cases_test.cpp`'s dependence on `Vehicle::environment()` for `density`/`pressure`/`temperature`
  (line 166-170) means `Simulation::step`'s existing frame-start `atmosphere_->at()` call **must not be
  removed** — see "How `EnvironmentSample` expresses the split" above. This was checked directly, not
  assumed; it is the one place this plan's design would have silently regressed observability if
  unverified.
- `tests/configs/no_alloc.yaml` and `no_alloc_domain.yaml` load a `WrenchSource` (`nan_wrench`) but
  configure no `physical.atmosphere`; `no_alloc_dryden.yaml` configures wind but not atmosphere either.
  **None of the three existing no-alloc fixtures exercises the new code path** (per-stage atmosphere
  recompute feeding a `WrenchSource`) — a new fixture is required (see Acceptance, R5).
- `python/bindings/_core.cpp` exposes no `Simulation.derivative()` binding today (checked directly:
  no `derivative` symbol in the file) — ADR-0004's "the derivative functor is the same one exposed to
  Python" is aspirational for a future M3 trim/linearize path, not a current dependency. Since `Eom`
  itself is unchanged by this PR, that aspiration remains exactly as true or false as it was before
  this PR.

## Design: `Vehicle::integrate()`

```
HaltReason Vehicle::integrate(...) {
  // unchanged: frame-0 pre-step domain check

  const StateVector pre_step_state = state_;   // trivial 13-double stack copy, no allocation
  HaltReason wrench_fault;                     // local; first fault wins, read after integrator.step()
  int stage = 0;                               // local; 0 == k1 (frame-start state) -- resolves Q2/R12

  const auto derivative = [&](double t, const StateVector& y) noexcept -> StateVector {
    const VehicleState vs_stage{y};
    EnvironmentSample stage_env = environment_;             // copies the frame-latched wind_n verbatim
    if (atmosphere_ != nullptr) {
      stage_env.atmosphere = atmosphere_->at(eom_->earth().altitude(vs_stage));  // fresh per stage
    }
    Wrench total;
    for (const WrenchEntry& entry : wrench_sources_) {
      const Wrench contribution = entry.model->wrench_b(vs_stage, stage_env);
      if (const HaltReason halt = health.check_wrench(contribution, entry.plugin_path, ctx, index_);
          halt && !wrench_fault) {
        wrench_fault = halt;   // first fault wins; keep evaluating so total/logging stay well-defined
      }
      total.force_b += contribution.force_b;
      total.moment_b += contribution.moment_b;
    }
    if (const HaltReason halt = health.check_wrench(total, wrench_total_name_, ctx, index_);
        halt && !wrench_fault) {
      wrench_fault = halt;
    }
    if (stage == 0) {
      *published_wrench_ = total;  // CORE-owned, registered signal (resolves Q2/Finding 2) -- the
    }                               // frame-start (k1) total only, never k2..k4's; no WrenchSource
    ++stage;                        // signature change, no t==ctx.t comparison (wrench_b takes no t)
    eom_->set_applied_wrench(total);
    return (*eom_)(t, y);
  };

  eom_->begin_frame();   // UNCHANGED, still once per frame -- Codex's plan critique caught this
                          // missing from an earlier draft of this pseudocode: mass/inertia caching
                          // (core/src/kernel/eom.cpp) must still run before the first derivative call,
                          // or Eom::operator() divides by MassProps::mass's zero-initialized default.
  integrator.step(derivative, ctx.t, state_);   // always runs all four stages, every check fires every
                                                 // stage -- no early skip; see the disclosed counter
                                                 // change below for why "always evaluate uniformly" was
                                                 // chosen over a fault-dependent short-circuit

  if (wrench_fault) {
    state_ = pre_step_state;    // explicit revert: state() is untouched, same outward guarantee as today
    return wrench_fault;
  }

  // unchanged from here: check_state, post-step domain check, unconditional renormalize
}
```

This satisfies every one of Joe's named coverage points:

- **Wrench call count: exactly 4x, not 5x.** The derivative's first invocation (`k1`) is called with
  `y == state_` — the frame-start state — which is *identical* to what today's separate pre-check
  computes. Deleting today's separate pre-check block and letting `k1`'s in-lambda evaluation serve
  double duty (physics **and** the health gate) means `wrench_b()` is called exactly once per RK4 stage,
  four times total, not five. (`AtmosphereModel::at()` is a different count — see next bullet.)
- **`AtmosphereModel::at()` call count: 5, not 4** — one **unchanged**, existing call in
  `Simulation::step()` for `Vehicle::environment()`'s observability/test contract (see Repository
  findings), plus 4 new calls inside the derivative lambda. `at()` is a cheap ISA-1976 polynomial
  evaluation, not the dominant cost here — see the measured cost table below for why this redundancy
  was accepted rather than engineered away by a fragile "reuse `k1`'s value" special case.
- **`HealthMonitor::check_wrench` placement — corrected after Codex's plan critique.** Moved from a
  single frame-start call (before `integrator.step`) to **exactly** 4 calls per contribution + 4 calls
  for the total, called *uniformly, every stage, regardless of whether an earlier stage this same frame
  already faulted* — deliberately **not** short-circuited once a fault is found, because a
  short-circuit gated on `check_wrench`'s returned `HaltReason` only actually skips anything under a
  `halt`-configured action (`HealthMonitor::check_wrench` returns a falsy `HaltReason` under `warn`
  regardless of whether a fault was detected, so a short-circuit keyed off it would silently do nothing
  under `warn` while changing behavior under `halt` — an action-dependent counting inconsistency worse
  than a uniform rule). All calls happen from *inside* the derivative lambda, which cannot itself
  return a `HaltReason` (the `StateDerivative` concept fixes the return type to `StateVector`); a new
  local `HaltReason wrench_fault` (captured by reference, "first fault wins") is the side-channel that
  lets `Vehicle::integrate()` learn about a mid-integration fault *after* `integrator.step()` returns
  (used only under a `halt`-configured action, where it drives the revert below — under `warn` it is
  never set, matching today's "warn never stops the run" contract exactly).

  **This is a real, not cosmetic, counting change, and it breaks one existing pinned assertion**
  (found by Codex's plan critique, not by this plan's original drafting — my initial claim that "nobody
  currently asserts an exact count" was wrong). `tests/unit/kernel/simulation_halt_test.cpp`'s
  `WarnActionRunsAllFramesAndCountsEveryNanDetection` (line 151-175) pins `health.nan == 1500.0` for a
  fixture where `nan_wrench` corrupts every frame from 500 onward under a `warn` action: today, that is
  1 contribution-check + 1 total-check + 1 (unaffected) state-check per corrupted frame, × 500 frames =
  1500. Under this PR's uniform per-stage design, the *wrench* checks alone become 4 (contribution) + 4
  (total) per corrupted frame (the fault is present at every one of the four stage states, since
  `nan_wrench`'s trigger is `ctx.frame`-based, true at every stage call within an affected frame),
  giving `(4 + 4 + 1) × 500 = 4500`. **This test's pinned value must change to `4500.0` as an explicit,
  justified part of this PR's diff** — not loosened, not silently left broken; the exact multiplier is
  derived above, not asserted. The unit-level `tests/unit/health/health_monitor_test.cpp` assertions
  (`EXPECT_EQ(*nan_count, 1.0)` etc.) call `HealthMonitor::check_wrench` directly, not through
  `Vehicle`/`Simulation`, and are **unaffected** — they test `HealthMonitor` in isolation against a
  call count the test itself controls. R1 (below) is revised accordingly: existing tests are green
  **except** this one named, justified, single-line update — a genuinely new acceptance item (see R9),
  not something "unmodified" could honestly claim.
- **"state() untouched" invariant.** Previously true *by construction* (the wrench was checked before
  `integrator.step` was ever called, so a halt meant `integrator.step` never ran). Now true *by
  explicit revert*: `integrator.step()` always runs to completion (RK4 has no early-abort hook — see
  Alternatives for why adding one was rejected), and `pre_step_state` is written back over whatever
  `integrator.step` computed if a mid-stage fault was recorded. The **outward guarantee is identical**
  (`Vehicle::state()` reads the same before/after a wrench halt as it does today); the **mechanism**
  changes from "never touched" to "touched, then restored," which is why `vehicle.hpp`'s own doc
  comment on `integrate()` (currently "a halt here leaves state() untouched — the EOM never sees a bad
  wrench") needs a one-line update: the EOM *does* now see a bad wrench transiently, inside a step
  whose result is discarded.
- **Zero-allocation guarantee.** `pre_step_state` is a stack copy of a fixed 13-double `Eigen::Vector`
  (no heap). The `derivative` lambda captures `this`, `&health`, `ctx`, and `&wrench_fault` — all by
  reference or trivially-copyable value — and is passed to `Integrator::step` as a template parameter
  (`const F&`), never type-erased through `std::function`, so no heap allocation is possible at the
  language level. `stage_env` is a stack copy of `EnvironmentSample` (two `Eigen::Vector3d` + one
  4-double `AtmoState`, all fixed-size). This needs a **new** acceptance test, since none of the three
  existing no-alloc fixtures combines a `WrenchSource` with `physical.atmosphere` configured (see R5).
- **Signal-publishing side effect — resolved by Joe's decision (Finding 2, correction round 1/2), not
  merely disclosed.** An earlier draft of this document treated this as a hypothetical risk for R2/R3
  to observe, with a proposed fallback ("publish only from `k1`, detectable via `t == ctx.t`") that
  Joe's review found **unimplementable**: `WrenchSource::wrench_b(vs, env)` takes no `t` at all, so no
  call site could ever make that comparison. Worse, the risk was never hypothetical: the same
  finite-difference math that retracted the force-channel dt-sweep rows above (`dF/dt` from the golden
  band's own samples 298-300, ×`dt`) shows that publishing `k4`'s evaluation (effectively one frame
  *ahead* of the sampled pre-step state, at `t+dt` on `y+dt·k3`) would fail R2 on the force/moment
  channels by ~10x (`force_x`), ~17x (`force_y`), ~3x (`force_z`) once this PR's other changes are in
  place — a real, predictable regression, not a maybe. Joe's decision: **the published wrench is the
  stage-1 (frame-start-state) value, produced core-side**, not by any `WrenchSource`'s own
  self-publish. Concretely (see the pseudocode above, `stage`/`published_wrench_`):
  - **Corrected by the second Codex critique pass**: a plain `Wrench published_wrench_;` member on
    `Vehicle` would **dangle**. `Simulation::from_config`/`for_testing` build a local `Vehicle`, call
    `init()` (where a signal registration would happen), and only *then* `std::move` it into
    `vehicles_` (`simulation.cpp:340,378,459`) — the identical hazard `HealthMonitor`'s own existing
    `unique_ptr<HealthMonitor> health_` member is already there to avoid ("the monitor's address must
    survive every move this `Simulation` goes through — a by-value member would dangle those pointers
    on the first move," `simulation.hpp`'s own comment on `health_`). `Vehicle` gains a
    `std::unique_ptr<Wrench> published_wrench_;` instead, allocated once in `init()` (the init path may
    allocate — §14), with the registry pointers pointing at `*published_wrench_`'s `force_b`/`moment_b`
    components — a stable heap address surviving every later move of the owning `Vehicle`, the same
    reason every plugin's own heap-owned `Model` already has this property for free. Registers six new
    `SignalRegistry` entries in `init()` — **named with the frame marker, per Joe's decision, matching
    the names being replaced and ADR-0001's own conventions**: `"<name_>.wrench_total.force_b_x/y/z"`
    and `"<name_>.wrench_total.moment_b_l/m/n"` (units N / N·m; body axes, roll/pitch/yaw moment
    components, not a bare `x/y/z` that could be read as an inertial or navigation frame). The name
    deliberately reuses
    `wrench_total_name_`'s own root (already used as a *label*, not a signal, for the health-check
    total) — one concept, two uses, not two concepts.
  - The derivative lambda writes `*published_wrench_ = total` (through the stable heap pointer) only
    when the local `stage` counter (a plain `int`, captured by reference, no allocation) is `0` — i.e.,
    only on the very first of the four calls `Integrator::step` makes, which is always `f(t, y)` with
    `y == state_`, the frame-start state. No inspection of `t` is needed or attempted.
  - **`nesc_aero.cpp` is edited to remove its own self-publish entirely**: the `mutable force_b_`/
    `moment_b_` members, their `registry.add(...)` calls in `init()`, and the two assignment lines at
    the end of `wrench_b()` all go away. This is not merely tidying up a now-redundant duplicate: it
    also **directly resolves** the tension Joe's review raised against R10's new purity contract (a
    mutable self-publish inside a `const` method is exactly the "hidden per-call state" that contract
    now forbids) — after this edit, `nesc_aero::wrench_b()` has no mutable state at all, genuinely pure,
    no contract tension to explain away. `nesc_aero.cpp` is a test-support plugin owned by this repo
    (`tests/plugins/`), not a third-party plugin — editing its implementation is in scope; no
    `WrenchSource` signature or vtable changes either way.
  - `nesc_cases_test.cpp`'s `resolve_aero_signals`/`AeroSignals` (currently reading
    `"v0.nesc_aero.force_b_x"` etc.) is updated to read `"v0.wrench_total.force_b_x"` etc. instead. The
    *comparison itself* (against the same ADR-0015 golden bands, at the same pre-step-sampled point)
    is unchanged — only which signal supplies the actual value changes, and it now supplies the
    *correct* one instead of a stale, artifact-prone plugin-owned one.
  - **Scope decision**: only the vehicle *total* is published core-side, not a per-`WrenchSource`
    breakdown — every case this PR's acceptance covers has exactly one `WrenchSource`, so total and
    contribution are identical; a per-contribution signal would be pure speculative abstraction ahead
    of M3's actual need (this codebase's own convention: don't build for a hypothetical multi-source
    vehicle before one exists). A future PR adding a second `WrenchSource` to one vehicle can add
    per-contribution signals then, against a real need.
- **Atmosphere-domain-check misattribution window (new, found by Codex's plan critique).** Today,
  because atmosphere is frame-latched, a trajectory whose *frame-start* altitude is inside
  `AtmosphereModel::valid_range()` always gets a real (non-NaN) atmosphere sample fed to every
  `WrenchSource` all frame — a mid-frame excursion out of range is only ever caught by
  `Simulation::step()`'s dedicated **post-step** domain check (ADR-0012), which halts as
  `kOutOfDomain`, correctly naming the atmosphere plugin. After this PR, the per-stage atmosphere
  recompute means a stage whose *own* `y` has drifted out of range mid-integration can produce an
  all-NaN `AtmoState` (`atmosphere_model.hpp`'s own documented "returns all-NaN outside
  `valid_range()`" contract) that a `WrenchSource` reads into a NaN/Inf wrench — now caught by this
  PR's own per-stage `check_wrench`, which halts as `kNonFiniteWrench`, naming the *consuming*
  `WrenchSource`, not the atmosphere plugin, and potentially one frame *earlier* than ADR-0012's own
  post-step check would have fired. This is the **same class of gap** `Vehicle::check_domain`'s own
  doc comment already discloses and Joe already accepted for gravity/Earth (`vehicle.hpp`: "a state
  within that band of a boundary... can have an RK4 stage evaluate the singular math before this
  check ever runs" — ADR-0012's own "Rejected Alternatives" names this class, "a domain a single frame
  can jump clean across"), now newly extended to atmosphere by this PR, where it did not exist before
  (atmosphere was never stage-evaluated until now). Not a defect this PR must fix — same backstop
  applies (a non-finite wrench still halts, just named differently and possibly one frame sooner) —
  but a **new instance of an already-accepted gap class**, not a pre-existing one, and must be
  disclosed to Joe as such rather than silently inherited. None of NESC cases 4/5/1–10 cross an
  atmosphere boundary mid-run, so this PR's own acceptance evidence (R2/R3) cannot exercise it either
  way — it is a live gap for the *next* scenario that does, tracked as a `PLAN.md` follow-up, the same
  way ADR-0012's own gravity/Earth instance already is.

## `kCoreApiVersion`: no bump (corrected reasoning after Codex's plan critique)

**Original reasoning in this plan's first draft was overstated and partly wrong**, per Codex: the
bump-policy comment in `core_api.hpp.in` names "`Model`, `PluginDescriptor`, `StepContext`" —
`StepContext` is a **plain data struct**, not a vtable class, so "`kCoreApiVersion` only bumps for
vtable changes" is not actually this project's stated policy; it was my own incorrect generalization
from the four prior bumps' shared shape (all four happened to be vtable changes), not from the policy
comment's actual words. The correct distinction is **which types cross the versioned boundary** — the
`dlopen`/loader handshake between a plugin `.so` and `libastra_core.so` (`docs/architecture.md` §4) —
not vtable-vs-plain-data in general. `StepContext` crosses it (every `Model::step(const StepContext&)`
call does); `Vehicle::PhysicsSpec` does not, **today**, because nothing outside
`Simulation::from_config`/`Simulation::for_testing` — both compiled inside `libastra_core` itself, in
the same translation unit family as `PhysicsSpec`'s own definition — ever constructs one, and no
`Model`-derived plugin ever receives one across the loader boundary. `Vehicle::PhysicsSpec` and
`Vehicle::init(...)` *are* technically public C++ API (`vehicle.hpp:47,82`), so this is an empirical
fact about current usage, not a structural guarantee the type system enforces — Codex found no
existing plugin or test constructing one externally, but a future one technically could. Given that:
- `WrenchSource::wrench_b()`'s signature is byte-for-byte unchanged, and `EnvironmentSample` gains no
  field (this PR's stage-local copy is of the *existing* type, not a new one) — neither crosses the
  boundary differently than today.
- `Vehicle::PhysicsSpec::atmosphere` (the one new field) does not cross the boundary *today*, matching
  the precedent that `PLAN.md`'s M2 PR 3a already added `EnvironmentSample::atmosphere` — a type that
  genuinely *does* cross the boundary, in every `WrenchSource::wrench_b()` call — without a version
  bump, on the same "single CMake invocation, no stale plugin binaries in practice" reasoning
  `docs/architecture.md` §4 states.

**Recommendation: still no bump**, on the corrected, narrower reasoning above (nothing this PR touches
crosses the loader boundary in a way that changed), plus one concrete follow-up this PR should make:
add a one-line comment on `Vehicle::PhysicsSpec` itself stating it is constructed only by
`Simulation::from_config`/`for_testing` and is not part of the versioned plugin ABI despite being
public — turning the "empirical fact" into a checkable, written invariant instead of one review has to
rediscover.

## Alternative: keep once-per-frame wrench, reduce `dt`

Not a strawman — this is Zipfel's own CADAC++ architecture (verified above, §E.3.7 pp.611–612 and
p.602–603's `newton` module: forces computed once per integration cycle, accuracy bought by a smaller
`int_step`, not by re-evaluating forces inside the RK4 sub-stages).

**Corrected after Cowork review (correction round 1/2), reproduced independently against the actual
repository before accepting.** This document's first draft's dt-sweep included `force_y`/`force_z`
rows claiming those channels *fail* today (e.g. `force_y` missing by ~15.8x `tol_abs`) and used that
to argue `dt`-reduction would need an impractical ~16x cost to close them. **That was wrong — a
sampling-order artifact in the throwaway sweep driver, not real physics.** The driver read the force
signal *after* running all N frames to completion, which yields whatever `nesc_aero` last wrote during
frame N's own execution — computed from frame N's *own frame-start* state, i.e. one frame (one `dt`)
*before* t=30s, not at t=30s. `nesc_cases_test.cpp`'s own header comment names exactly this hazard and
exists specifically to avoid it (its read-state/`run(1)`/read-wrench ordering); the throwaway driver
didn't follow it for the force channels (it did, correctly, for `altitude`/`v_down`, which come from
`state()` directly and have no such one-frame offset).

**Verified independently against the actual golden file**
(`tests/golden/nesc/case04_dropped_sphere_round_non_rotation_band.hpp`): a 3-point backward difference
of `kForceY`'s own reference samples 298/299/300 (t=29.8/29.9/30.0 s) gives `dF_y/dt ≈ −16.09 N/s`;
of `kForceZ`'s, `dF_z/dt ≈ +4.09 N/s` — both recomputed directly from the checked-in header, not taken
on faith. Multiplied by one `dt=0.001` frame: `∓0.01609 N` / `±0.00409 N` — matching the sweep's
reported "misses" (`+0.015174`, `−0.004040`) almost exactly, and shrinking by half each time `dt`
halved purely *because a frame is `dt` long*, not because of any wrench-evaluation-timing physics.
**The `force_y`/`force_z`/`force_x` rows are retracted, not corrected in place** — dropped from the
table below rather than re-measured, since the real, committed evidence already exists and is
stronger:

**The committed R8 dry-run baseline** (`docs/plans/nesc-harness.md`, lines 534-554, `max(|diff|)/tol_abs`
per channel through the real harness, `dt=0.001`, the same frozen-wrench code, `PLAN.md`'s own account
of which channels the later gravity-centripetal fix did and did not move) is the actual state channel
evidence — **`force_x`/`force_y`/`force_z` PASS today** (case04 ratios 0.798/0.935/0.418; case05 now
matches per `PLAN.md`'s post-gravity-fix account, "force_x/force_y/force_z now pass cleanly in both
cases"), directly contradicting this document's original, artifact-based claim that they fail. The
channels that genuinely do fail, by ratios the force-channel argument never needed:

| Channel | case04 ratio (`diff`/`tol_abs`) | case05 ratio | Source |
|---|---|---|---|
| `v_north` | **4.850×10⁶** | 4.851×10⁶ | `nesc-harness.md:539` (case04 unaffected by the gravity fix, `rotating: false`; case05 not independently re-checked for `v_north` post-fix, but `latitude`/`v_north` were not among the channels the gravity fix's own cross-check named as improved — see caveat below) |
| `latitude` | 5539.05 | 5539.17 | `nesc-harness.md:536` (post-fix per `PLAN.md`'s own account: "latitude ... fail[s] in both at case04's own (now shared) level" — already near-identical to case04 above, consistent with that) |
| `longitude` | 2768.39 | **833.8** | case04: `nesc-harness.md:537`; case05: **corrected, second Codex critique caught the first correction reusing the pre-fix number** — `docs/plans/nesc-case05-gravity-centripetal.md:164`'s own dry-run cross-check reports case05's post-gravity-fix `longitude` ratio as 833.8, not the pre-fix 1740.1 |
| `v_east` | 2.424×10⁶ | **761.5** | case04: `nesc-harness.md:540`; case05: `nesc-case05-gravity-centripetal.md:164` post-fix (was 1287.0 pre-fix) |
| `yaw` | 7.32 | **3.44** | case04: `nesc-harness.md:542`; case05: `nesc-case05-gravity-centripetal.md:164` post-fix (was 4.45 pre-fix) |
| `pitch` | 20.67 | **8.42** | case04: `nesc-harness.md:543`; case05: `nesc-case05-gravity-centripetal.md:164` post-fix (was 10.17 pre-fix) |
| `roll` | 17.27 | **6.84** | case04: `nesc-harness.md:544`; case05: `nesc-case05-gravity-centripetal.md:164` post-fix (was 9.80 pre-fix) |

**Corrected once already** (second Codex critique, this same correction round): the first revision of
this table reused `nesc-harness.md`'s own case05 numbers for `longitude`/`v_east`/`yaw`/`pitch`/`roll`
verbatim, which are the *pre*-gravity-fix values — stale for the actual current code. The real
post-fix numbers (verified directly against `nesc-case05-gravity-centripetal.md:164`'s own committed
dry-run table) are lower — the gravity fix improved these channels too, alongside the five
(`altitude`/`v_down`/`density`/`pressure`/`temperature`) `PLAN.md` explicitly names — but every one of
them still fails, and `force_x`/`force_y`/`force_z` are independently confirmed passing in case05 by
that same table ("0 failures logged" vs. the pre-fix 221.6/192.0/273.9 FAILs), so this document's claim
that force channels pass in both cases is unaffected by this correction — only the specific
`longitude`/`v_east`/`yaw`/`pitch`/`roll` case05 numbers needed fixing. `case04`'s own numbers are
unaffected either way (`rotating: false`, untouched by that fix).

**`altitude`/`v_down` are retained, measured directly against the actual, unmodified frozen-wrench
code** (release build, no sanitizer; config copies in scratch with only `sim.dt`/`sim.frames` edited
to hold total duration at 30 s; `dt=0.001` reproduced independently — it matches the harness's own
ratio for these two channels, 1.50/1.31, to within rounding, cross-checked directly). Golden
`reference[300]`/`tol_abs` parsed directly from the same `tests/golden/nesc/case0{4,5}_..._band.hpp`
files `nesc_cases_test.cpp` itself reads (t=30s = sample 300):

| case | dt | frames | wall_s | ms/frame | channel | actual | reference | diff | tol_abs | ratio | in band? |
|---|---|---|---|---|---|---|---|---|---|---|---|
| case04 | 0.001 | 30000 | 0.01225 | 0.000408 | altitude | 4947.288552 | 4947.303237 | −0.014685 | 0.00982118 | 1.50 | **N** |
| case04 | 0.001 | 30000 | 0.01225 | 0.000408 | v_down | 264.294958 | 264.293437 | +0.001521 | 0.00115949 | 1.31 | **N** |
| case04 | 0.001 | 30000 | 0.01225 | 0.000408 | latitude | −5.539046e-9 | ~0 | −5.539046e-9 | 1e-12 | 5539.0 | **N** |
| case04 | 0.001 | 30000 | 0.01225 | 0.000408 | v_north | −4.850395e-3 | ~0 | −4.850395e-3 | 1e-9 | 4.850×10⁶ | **N** |
| case04 | 0.0005 | 60000 | 0.02425 | 0.000404 | altitude | 4947.295292 | 4947.303237 | −0.007945 | 0.00982118 | 0.81 | Y |
| case04 | 0.0005 | 60000 | 0.02425 | 0.000404 | v_down | 264.294272 | 264.293437 | +0.000835 | 0.00115949 | 0.72 | Y |
| case04 | 0.0005 | 60000 | 0.02425 | 0.000404 | latitude | −2.769526e-9 | ~0 | −2.769526e-9 | 1e-12 | 2769.5 | **N** |
| case04 | 0.0005 | 60000 | 0.02425 | 0.000404 | v_north | −2.425150e-3 | ~0 | −2.425150e-3 | 1e-9 | 2.425×10⁶ | **N** |
| case04 | 0.00025 | 120000 | 0.04838 | 0.000403 | altitude | 4947.298662 | 4947.303237 | −0.004575 | 0.00982118 | 0.47 | Y |
| case04 | 0.00025 | 120000 | 0.04838 | 0.000403 | v_down | 264.293929 | 264.293437 | +0.000492 | 0.00115949 | 0.42 | Y |
| case04 | 0.00025 | 120000 | 0.04838 | 0.000403 | latitude | −1.384763e-9 | ~0 | −1.384763e-9 | 1e-12 | 1384.8 | **N** |
| case04 | 0.00025 | 120000 | 0.04838 | 0.000403 | v_north | −1.212563e-3 | ~0 | −1.212563e-3 | 1e-9 | 1.213×10⁶ | **N** |
| case05 | 0.001 | 30000 | 0.01358 | 0.000453 | altitude | 4961.028649 | 4961.043194 | −0.014545 | 0.00976080 | 1.49 | **N** |
| case05 | 0.001 | 30000 | 0.01358 | 0.000453 | v_down | 263.494921 | 263.493416 | +0.001504 | 0.00115316 | 1.30 | **N** |
| case05 | 0.001 | 30000 | 0.01358 | 0.000453 | latitude | −5.499721e-9 | ~0 | −5.499721e-9 | 1e-12 | 5499.7 | **N** |
| case05 | 0.001 | 30000 | 0.01358 | 0.000453 | v_north | −4.816013e-3 | ~0 | −4.816013e-3 | 1e-9 | 4.816×10⁶ | **N** |
| case05 | 0.0005 | 60000 | 0.02724 | 0.000454 | altitude | 4961.035323 | 4961.043194 | −0.007871 | 0.00976080 | 0.81 | Y |
| case05 | 0.0005 | 60000 | 0.02724 | 0.000454 | v_down | 263.494242 | 263.493416 | +0.000826 | 0.00115316 | 0.72 | Y |
| case05 | 0.0005 | 60000 | 0.02724 | 0.000454 | latitude | −2.749863e-9 | ~0 | −2.749863e-9 | 1e-12 | 2749.9 | **N** |
| case05 | 0.0005 | 60000 | 0.02724 | 0.000454 | v_north | −2.407960e-3 | ~0 | −2.407960e-3 | 1e-9 | 2.408×10⁶ | **N** |
| case05 | 0.00025 | 120000 | 0.05405 | 0.000450 | altitude | 4961.038660 | 4961.043194 | −0.004535 | 0.00976080 | 0.46 | Y |
| case05 | 0.00025 | 120000 | 0.05405 | 0.000450 | v_down | 263.493904 | 263.493416 | +0.000487 | 0.00115316 | 0.42 | Y |
| case05 | 0.00025 | 120000 | 0.05405 | 0.000450 | latitude | −1.374932e-9 | ~0 | −1.374932e-9 | 1e-12 | 1374.9 | **N** |
| case05 | 0.00025 | 120000 | 0.05405 | 0.000450 | v_north | −1.203968e-3 | ~0 | −1.203968e-3 | 1e-9 | 1.204×10⁶ | **N** |

**`v_north`/`latitude` rows added per Joe's request — measured, not extrapolated** (an earlier draft
of this section extrapolated `v_north`/`latitude`'s scaling from a single `dt=0.001` data point;
Joe's own review pointed out these are state channels the existing sweep driver samples validly, the
same way `altitude`/`v_down` already are, so the correct fix was to actually measure them, not merely
label the estimate). `latitude`'s reference/tol_abs are `~1.5e-21`/`1e-12`; `v_north`'s are
`~-3e-15`/`1e-9` (both cases, both effectively zero relative to the diffs above) — read directly from
`tests/golden/nesc/case0{4,5}_..._band.hpp`. Wall time scales linearly with frame count
(~0.0004–0.00045 ms/frame, regardless of `dt`: pure per-frame cost, no fixed overhead).
`altitude`/`v_down`'s residual shrinks **linearly** with `dt` (entering band between `dt=0.001` and
`0.0005` — **2x cost**, both cases), consistent with a first-order modeling-mismatch error, not RK4's
own 4th-order truncation error. **`v_north`/`latitude` shrink exactly linearly with `dt` too — each
ratio halves precisely each time `dt` halves** (case04 `v_north`: 4.850×10⁶ → 2.425×10⁶ → 1.213×10⁶;
`latitude`: 5539.0 → 2769.5 → 1384.8; case05 tracks identically) — but **even at `dt=0.00025`, 4x
today's frame cost, `v_north` is still failing by over 1.2 million times its own `tol_abs`**, measured,
not projected. Continuing the same exact, now-measured linear trend to close it would need
`dt` ≈ 0.001 / 4.85×10⁶ ≈ 2×10⁻¹⁰ s (roughly 200 picoseconds) — a ~4.85-million-fold increase in frame
count for a single 30 s run, meaningless in practice, not merely expensive. `latitude`'s ~5539x miss
extrapolates the same way to needing ~5539x smaller `dt`, confirmed by the same measured, linear
trend, not asserted from one point.

**Measured cost of 4x `wrench_b()` calls** (isolated microbenchmark, `tests/plugins/nesc_aero`, release
build, no sanitizer, no integration/health overhead — pure call cost):

| run | 30,000 calls (today, 1x/frame/30s) | 120,000 calls (this PR, 4x/frame/30s) | delta (4x−1x) | delta / 30s frame-budget (30e9 ns) | delta / single 1ms frame |
|---|---|---|---|---|---|
| 1 | 8.640 ns/call | 8.607 ns/call | 773,700 ns | 2.579e-5 | +25.79 ns (0.00258%) |
| 2 | 8.541 ns/call | 8.598 ns/call | 775,519 ns | 2.585e-5 | +25.85 ns (0.00258%) |

~8.6 ns/call, stable run to run. Going from 1x to 4x evaluations of this `WrenchSource` costs an extra
~25.8 ns per 1 ms frame. Expressed two ways, both honestly, per Codex's plan critique catching a
**1000x arithmetic error in this document's earlier draft** (it compared 25.8 ns against a
mistakenly-computed "~400,000 ns/frame," when the sweep table's own `ms/frame` column — 0.000408 ms —
is 408 ns/frame, not 400,000 ns; the two numbers were three orders of magnitude apart, not the same
unit read twice):
- Against the **hard 1 ms real-time-mode deadline** ADR-0004/§10 actually care about: 25.8 ns is
  **0.0026%** of that budget — genuinely negligible, and the comparison this project's own real-time
  language is about.
- Against **this specific toy scenario's own currently-measured total frame cost** (~408–450 ns/frame,
  from the table above — one drag `WrenchSource`, no logging, no transport, no sensors): 25.8 ns is
  **~6%** of that — not negligible in relative terms, though this scenario's own total cost is
  unusually low (a real M3+ vehicle carrying aero/mass/propulsion/sensor models per frame will have a
  much larger denominator, shrinking this ratio). **This microbenchmark also measures only
  `wrench_b()`'s own call cost in isolation** — it does not include the added per-stage
  `AtmosphereModel::at()` calls, the extra `HealthMonitor::check_wrench` calls, or the `EnvironmentSample`
  stage copies this PR's design also adds every stage; all three are individually cheap (pure functions,
  a few `isfinite()` checks, a POD copy respectively) but were not measured together, so the *true*
  total per-frame overhead of this PR's mechanism is somewhat higher than 25.8 ns and is not yet
  precisely known — the first real measurement of it is this PR's own R8 full-gate run against the
  actual implementation, not this plan's spike. This is an acceptable limit for a planning document
  (which reasons about a design, not the artifact of having already built it) but is recorded here so
  it is not mistaken for a completed measurement.

**Rejected because**, now with corrected, real, measured evidence in hand: reducing `dt` treats a
*qualitative* mismatch (wrench evaluated at the wrong state) as a *quantitative* one (not enough
resolution). For two of the checked channels (`altitude`, `v_down`) that framing happens to work, at
an affordable 2x cost. `force_x`/`force_y`/`force_z` were never actually failing — that claim in an
earlier draft was a sweep-tool artifact, retracted above — so they offer no evidence either way.
`v_north`/`latitude`, swept at all three `dt` values (measured, not extrapolated), still miss by over
**1.2 million times** and **1385 times** their own `tol_abs` respectively even at `dt=0.00025` — 4x
today's frame cost, the same cost this PR's own design carries — and the remaining committed-baseline
channels (`v_east`, `longitude`, `yaw`, `pitch`, `roll`) miss by ~7x to 2.4 million times at `dt=0.001`.
The alternative **does not actually close PLAN.md's PR 3c gap**, and does so by a margin no plausible
engineering tradeoff could close, let alone one cheaper than the per-stage design's own measured
marginal overhead (0.0026% of the real-time deadline; ~6% of this toy scenario's unusually low current
per-frame cost, not yet fully measured end-to-end). It also does not fix the *root cause* the skip
comment names, which would resurface at any spin rate high enough relative to whatever smaller `dt`
was chosen — this PR's fix is `dt`-independent by construction, the alternative's is not.

## Acceptance

| ID | Observable requirement | Executable check |
|---|---|---|
| R1 | `Vehicle::integrate()`'s new derivative functor compiles and satisfies `StateDerivative` (`noexcept`, returns `StateVector`); existing `tests/unit/kernel/*` and `tests/unit/health/*` pass **except the one named, justified update in R9** | `just build asan`; `ctest --preset asan -L unit` |
| R2 | Cases 4 and 5's `GTEST_SKIP()` removed; every channel passes its existing ADR-0015 band (no oracle change) | `ctest --preset asan -R 'physics.Nesc.Case0[45]'` |
| R3 | Every other NESC case (1–3, 6–10) stays green — this PR must not regress a case that passes today | `ctest --preset asan -R physics.Nesc` |
| R4 | A wrench fault introduced only at a non-`k1` RK4 stage (not visible at frame-start) still halts, names the correct plugin, and leaves `state()` at its pre-step value | New test: extend `tests/plugins/nan_wrench` with an optional "fault starting from the Nth `wrench_b()` call within a frame" trigger (a `mutable` per-frame call counter reset in `step()`, the same `mutable`-for-test-instrumentation pattern `nesc_aero` already uses) — proves detection at `k2`/`k3`/`k4` is real, not just `k1`'s pre-existing coverage relocated |
| R5 | Zero heap allocations over 1000 frames with a `WrenchSource` **and** `physical.atmosphere` both configured (the new code path none of the three existing no-alloc fixtures exercises) | New `tests/configs/no_alloc_substage_wrench.yaml` (round or flat Earth + `physical.atmosphere: isa1976` + `nesc_aero` or `nan_wrench`); new `TEST(NoAlloc, ...)` in `tests/physics/no_alloc_test.cpp` |
| R6 | `nesc_cases_test.cpp` reads the new core-owned `v0.wrench_total.force_b_x/y/z`/`moment_b_l/m/n` signals (not `nesc_aero`'s own, now-removed self-publish) and every force/moment channel still passes its existing ADR-0015 band (see "Signal-publishing side effect," resolved) | `ctest --preset asan -R 'physics.Nesc.Case0[45]'` (same command as R2 — this is the same acceptance run, not a separate one) |
| R7 | `kCoreApiVersion` unchanged; every existing plugin loads and every existing test that checks plugin-version rejection (`bad_version` fixture) still passes | `ctest --preset asan -R Plugin` (or the equivalent loader test label) |
| R8 | Full gate, once, on the final candidate | `./scripts/check-full.sh` (`just check`): `check: OK`, exit 0 |
| R9 | **New, from Codex's plan critique.** `tests/unit/kernel/simulation_halt_test.cpp`'s `WarnActionRunsAllFramesAndCountsEveryNanDetection` updated from `EXPECT_EQ(*nan_count, 1500.0)` to `EXPECT_EQ(*nan_count, 4500.0)`, with the derivation (`(4 contribution + 4 total + 1 state) × 500 corrupted frames`) recorded in the test's own comment, not just this document; a repo-wide grep for other exact-count assertions on a wrench-related health counter (`health.nan`, or any other signal a `WrenchSource`-triggered fault increments) confirming none were missed | `grep -rn "health\.nan\|nan_count" tests/` reviewed by hand before claiming R1/R8 green; the one match's new value verified against a real run, not just arithmetic |
| R10 | `wrench_source.hpp`'s doc comment updated to state `wrench_b()` may be called multiple times per frame (once per RK4 sub-stage) and must be a pure function of `(vs, env)` — the explicit contract Codex's plan critique found this design relies on but the interface never actually promised | Reviewed as part of the diff; no executable check beyond compilation (a doc-comment-only change) |
| R11 | The atmosphere-domain-misattribution window (new disclosed risk, see Design) is recorded as a `PLAN.md` follow-up item alongside ADR-0012's own existing gravity/Earth instance of the same gap class, not silently dropped | Reviewed as part of the diff; `PLAN.md` updated |
| R13 | **New, folded in per Joe's approval.** `docs/specs/nesc-harness.md` (lines ~141, ~197) updated in the same diff: both currently name the now-removed `nesc_aero.force_b_{x,y,z}`/`nesc_aero.moment_b_{l,m,n}` self-published signals, and line ~141 explicitly asserts "no plugin gains new registered signals for this task" — false once this PR lands. Both updated to name the new core-owned `v0.wrench_total.force_b_{x,y,z}`/`moment_b_{l,m,n}` signals instead | Reviewed as part of the diff; `grep -rn "nesc_aero\.force_b\|nesc_aero\.moment_b" docs/` clean afterward except historical/narrative references (e.g. this plan document's own account of what was removed) |
| R12 | **New, from Joe's decision on Finding 2; the capture detail below corrected by the second Codex critique pass.** The published `v0.wrench_total.force_b_x/y/z`/`moment_b_l/m/n` signal genuinely reflects `k1` (the frame-start state), not any other stage — i.e. this specific regression (publishing a non-`k1` stage) is caught by a test, not merely assumed fixed by code review | New assertion in `nesc_cases_test.cpp`'s existing per-sample loop. **Correction**: the loop's existing `y` (`nesc_cases_test.cpp:130`) is a `const StateVector&` alias into the live `Vehicle::state()`, mutated in place by `sim.run(1)` (line 154) — reusing it *after* that call, as an earlier draft of this row assumed, would compare against the post-step state, not the pre-step one `wrench_b` actually saw. The test must take an explicit copy — `const StateVector pre_step_copy = y;` — *before* calling `sim.run(1)`, and build the independent comparison's `VehicleState`/`EnvironmentSample` from that copy, not from `y` after the fact. With that fixed: independently construct a fresh, separately-loaded `tests/plugins/nesc_aero` instance (same config; the same standalone-plugin-loading idiom `tests/unit/wrench/nesc_aero_test.cpp` already demonstrates) and call its `wrench_b(pre_step_vs, pre_step_env)` directly against the preserved pre-step copy; assert it matches the sim's own published `wrench_total` signal (read *after* `sim.run(1)`, same as the existing force/moment checks) to within ~1e-12 (floating-point round-off only, not a real discrepancy) |

Performance/allocation bounds: R5 above. No new dependency, no config schema change (no new YAML
keys) — this PR is purely an internal evaluation-timing fix, plus the two disclosed doc-comment/test
updates R9–R10 above.

## Questions and decisions

| # | Question | Consequence | Owner | Answer |
|---|---|---|---|---|
| Q1 | Should `HealthMonitor`'s wrench-related counters be deduplicated to "once per frame" instead of "once per stage-evaluation," given Codex's plan critique showed this is **not** cosmetic — it breaks a pinned test (`simulation_halt_test.cpp:174`, R9)? | Deduplication (e.g., a short-circuit) turned out to behave inconsistently between `warn`/`halt` actions (see Design's corrected `check_wrench` placement bullet) and was rejected for that reason, not chosen; the alternative is accepting and documenting the new count, updating the one affected pinned value | Joe | **Resolved during this plan's own critique**: uniform per-stage counting (no dedup), pinned value updated to `4500.0` with derivation shown (R9) — recommend accepting this as the simpler, action-consistent design |
| Q2 | Does `nesc_aero`'s registry publish need to change (publish only `k1`'s value) as part of *this* PR, or only if R6 actually observes a regression? | Not hypothetical: the same finite-difference math used to retract the force-channel dt-sweep rows shows publishing `k4` would fail R2 by ~10x/17x/3x on force_x/y/z. The originally-proposed fallback ("detectable via `t == ctx.t`") is unimplementable — `wrench_b` takes no `t` | Joe | **Resolved.** The published wrench is the stage-1 value, produced core-side via a `stage` counter local to the derivative lambda (not inside `wrench_b`); `nesc_aero` stops self-publishing entirely; new signals `v0.wrench_total.*`; `nesc_cases_test.cpp` reads them instead; R12 added. No `WrenchSource` signature change, no `kCoreApiVersion` bump |
| Q3 | Does the Alternatives section's measured numbers change the recommendation once available? | Measured, then corrected twice: a 1000x arithmetic error and a table/prose contradiction (Codex's plan critique), then a sampling-order artifact that had invented a `force_y`/`force_z` failure that doesn't exist (Cowork review, correction round 1/2) | Joe | **Conclusion accepted, evidence corrected.** `force_x`/`force_y`/`force_z` pass today — that claim is retracted. The real evidence (`v_north` missing by 4.85 million times `tol_abs`, `latitude` by ~5539x, both via the committed R8 baseline, plus `altitude`/`v_down`'s own directly-measured linear-in-`dt` convergence) rejects the alternative more decisively than the retracted claim did |
| Q4 | Is the new atmosphere-domain-misattribution window (Design section, found by Codex's plan critique) acceptable as a disclosed gap, matching ADR-0012's own accepted precedent for the identical gravity/Earth shape, or does it need to be engineered around before this PR ships? | Engineering around it (e.g., clamping per-stage atmosphere recompute to the frame-start value when the stage altitude falls outside `valid_range()`) is possible but adds real complexity for a window none of this PR's own acceptance evidence (NESC cases 1–10) can exercise either way | Joe | Pending — default recommendation: disclose only, matching ADR-0012's own precedent, tracked as a `PLAN.md` follow-up (R11) |

## Draft ADR — elaborates ADR-0004

*(Kept with this plan per `docs/templates/adr.md`'s own convention — "Keep a draft with the feature
plan" — not yet a numbered file under `docs/decisions/`; would become ADR-0016 on acceptance.)*

---

### ADR-0016 (draft): Per-RK4-substage evaluation of applied wrench and atmosphere; wind stays frame-latched

Status: proposed
Date: 2026-09-19
Owner: Joe
Related/supersedes: elaborates ADR-0004 (fixed-step RK4) — does not change RK4's tableau, `dt`, or
"one `Eom`"; only what state `WrenchSource::wrench_b()` and `AtmosphereModel::at()` see per call.
Also touches ADR-0012's territory (a `Vehicle`-level check reporting through `HealthMonitor` without
`HealthMonitor` holding model pointers) — same shape, not a conflict.

#### Context

`Vehicle::integrate()` evaluates the applied wrench once per major frame, at the frame-start state,
and reuses it across all four of `Integrator`'s RK4 sub-stage derivative calls, even though those four
calls use four different states. Gravity and the Earth-model terms inside `Eom::operator()` do not
have this problem — they are recomputed fresh from each stage's own state already. NESC check cases 4
and 5 (Vol II Tables 28–29: spheres under real drag, spinning at 10–30 deg/s) are the first cases in
the M2 harness where this mismatch produces a measurable, band-failing residual (`nesc_cases_test.cpp`'s
own `GTEST_SKIP()` comment). Stevens/Lewis/Johnson (3e, §3.4) and Nutaro (§5.2) both describe the
general numerical-integration principle this violates: a pure function of the ODE's own state belongs
inside the derivative, re-evaluated at every stage; a zero-order-held external input does not need to
be, and a stochastic/first-order-correlated process (Zipfel 4e App. E, §E.3.7) must not be, since doing
so would break its designed statistics and this project's own bitwise-determinism guarantee.

#### Decision

`WrenchSource::wrench_b()` and the `AtmosphereModel::at()` sample it consumes are evaluated fresh at
each of `Integrator`'s four RK4 sub-stages, using that stage's own state — via a new derivative functor
local to `Vehicle::integrate()` that wraps `Eom` (unchanged, including its own `begin_frame()` mass
latch, still called once per frame) with a fresh per-stage wrench accumulation. `WindModel`'s
frame-latched shaping-filter output (`begin_frame`/`wind_n`) is **unchanged**: read at every stage, but
its value cannot vary within a frame by construction, since nothing advances it again until the next
`Simulation::step`. `HealthMonitor::check_wrench` moves from a single frame-start call to exactly four
per-stage calls per contribution plus four for the total, evaluated **uniformly** every stage
regardless of an earlier stage's own result (a fault-dependent short-circuit was considered and
rejected — it behaves inconsistently between `warn`/`halt` actions, see the plan's own Design section)
— recorded via a first-fault-wins side channel for *naming*, surfaced after `Integrator::step()`
returns; a wrench fault still leaves `Vehicle::state()` at its pre-step value, now via an explicit
revert rather than by never having called `integrator.step()` in the first place. This uniform counting
is a real, not cosmetic, change to one existing pinned test value (`simulation_halt_test.cpp`, updated
`1500.0` → `4500.0` with its derivation shown). The vehicle's *published* applied-wrench signal
(`v0.wrench_total.*`, newly core-owned, replacing `nesc_aero`'s own removed self-publish) is likewise
taken only from the `k1` (frame-start) evaluation, via a plain stage counter local to the derivative
functor — not from any `WrenchSource`'s own state, and not by inspecting `t` (which `wrench_b` does not
receive). No plugin-facing interface changes; `kCoreApiVersion` is not bumped, on the corrected
reasoning (which type crosses the versioned loader boundary today, not a blanket vtable-only rule) in
the plan's own section above. A new, disclosed atmosphere-domain misattribution window (same class as
ADR-0012's own accepted gravity/Earth gap, now newly extended to atmosphere) is tracked as a `PLAN.md`
follow-up, not engineered around in this PR. Full technical design: this document's "Design:
`Vehicle::integrate()`" section above.

#### Rejected alternatives

- **Keep once-per-frame wrench, reduce `dt`.** Zipfel's own CADAC++ architecture; not fixed by this
  ADR's problem statement — see the plan's "Alternative" section above for the measured cost/residual
  comparison (corrected once for a sampling-order artifact that had wrongly claimed `force_y`/`force_z`
  fail today, and once to replace an extrapolated `v_north`/`latitude` estimate with a real
  three-point sweep: both channels shrink exactly linearly with `dt`, and `v_north` still misses by
  over 1.2 million times its own `tol_abs` even at 4x cost, measured, not projected). Rejected as
  treating a qualitative state-mismatch as a quantitative resolution problem.
- **Give `Integrator::step` an early-abort hook** (change the `StateDerivative` concept to return
  more than `StateVector`, let `Integrator::step` return a `HaltReason` and stop after the first bad
  stage instead of running all four). Rejected: this changes a stable, 30-line, heavily-tested core
  primitive's public contract for every existing and future caller (including
  `tests/unit/kernel/integrator_test.cpp`'s plain physics-only derivatives, which know nothing about
  `HealthMonitor`), to save a little compute in an already-rare fault path. The chosen "run to
  completion, then revert" design gets the identical outward guarantee (`state()` untouched on a
  fault) without touching `Integrator` at all.
- **Encode the stage-pure/frame-latched split in `EnvironmentSample`'s type**, or add a new
  `WrenchSource`/`WindModel` virtual declaring it. Rejected — see "How `EnvironmentSample` expresses
  the split" above: the split is already fully determined by each model interface's own existing
  contract, and a new virtual would force a `kCoreApiVersion` bump for zero behavioral benefit.
- **Move the per-stage wrench-accumulation logic into `Eom` itself** (give `Eom` a reference to the
  vehicle's `WrenchSource` list and environment models). Rejected: `Eom`'s entire value (ADR-0004:
  "there is exactly one `Eom`", exposed unchanged to Python) is that it is a small, pure, physics-only
  functor with no knowledge of plugins, health checks, or per-vehicle bookkeeping. Threading
  `WrenchSource`s and `HealthMonitor` through `Eom` would entangle two concerns ADR-0004 and this
  codebase's layering (`core/kernel/` vs `core/interfaces/`) keep deliberately separate, for a design
  that is exactly as achievable — and no cheaper — as a wrapping functor local to `Vehicle::integrate()`.

#### Consequences and validation

Benefits: fixes the diagnosed root cause of NESC cases 4/5's disclosed gap, `dt`-independently;
zero plugin-facing interface changes; zero new dependencies. Costs: `WrenchSource::wrench_b()` called
4x/frame instead of 1x (measured in isolation: ~26 ns/frame for one drag source, ~6% of this toy
scenario's own unusually low per-frame cost, 0.0026% of the 1 ms real-time deadline; true end-to-end
cost including the added atmosphere/health-check work not yet measured — see plan's Alternatives
section); `AtmosphereModel::at()` called 5x/frame instead of 1x; `HealthMonitor`'s wrench-related
counters change from frames-with-a-fault to stage-evaluations-with-a-fault, a **real, not cosmetic**,
change requiring one pinned test value updated with a shown derivation (R9); `Vehicle::PhysicsSpec`
gains one field. Risks: the signal-publishing side effect named in the plan (R6) — must be observed,
not assumed, by this PR's own acceptance run; a new, disclosed atmosphere-domain-misattribution window
(R11), the same accepted gap class ADR-0012 already carries for gravity/Earth, now newly instanced for
atmosphere. Migration/rollback: fully reversible, single-PR change with no data/schema migration.
Acceptance commands: this plan's Acceptance table (R1–R11) above.

#### Approval

**Plan approved by Joe, 2026-09-19**, after correction round 1/2 (both Joe's own two findings and the
resulting second Codex critique pass's three further findings, all fixed — see "Correction round 1/2"
above). Q1/Q4 accepted as recorded; Q2 resolved (stage-1, core-side publish); Q3's conclusion accepted,
evidence corrected twice (once for the force-channel sampling artifact, once for the stale post-fix
case05 numbers) and once strengthened with a real, measured `v_north`/`latitude` sweep replacing an
earlier extrapolation. Three further items folded directly into implementation, not requiring another
plan round: (1) name the new core signals `wrench_total.force_b_{x,y,z}`/`moment_b_{l,m,n}`, matching
the names being replaced and ADR-0001's conventions; (2) update `docs/specs/nesc-harness.md`'s own
references to the removed `nesc_aero` self-published signals in the same diff (R13); (3) measure
`v_north`/`latitude` at the three swept `dt` values rather than extrapolate (done above — confirmed
linear, `v_north` still failing by >1.2 million times `tol_abs` even at 4x cost).

**ADR-0016 itself is not written as a numbered `docs/decisions/` record yet**: per Joe's explicit
sequencing, implementation happens first, then cases 4/5 run skip-lifted and the full per-channel
`max|diff|/tol_abs` table is reported *before* anything else — `touch-goldens` stays unarmed and this
draft ADR stays a draft until that dry run shows the gap actually closes. If any channel still fails,
the sequencing calls for stopping and reporting, not widening, regenerating, or re-skipping to force a
green result.

#### Plan critique — resolved

One independent, read-only Codex plan critique was obtained on this document (fresh context,
`docs/agents/review-dispatch.md`'s plan-critique procedure) before this document was shown to Joe.
Six failed assumptions were found and are fixed above, all before any code was written:

| # | Finding | Resolution |
|---|---|---|
| 1 | Uniform per-stage `check_wrench` calls break `simulation_halt_test.cpp:174`'s pinned `health.nan == 1500.0` — the plan's original claim that "nobody asserts an exact count" was wrong | Value corrected to `4500.0` with derivation shown; new acceptance item R9 |
| 2 | The pseudocode's literal replacement omitted `eom_->begin_frame()`, which would divide by a zero-initialized mass on the very first derivative call | Restored, called once per frame as today, immediately before `integrator.step()` |
| 3 | Per-stage atmosphere recompute opens a new atmosphere-domain misattribution window (a mid-stage boundary crossing now surfaces as `kNonFiniteWrench`, naming the consumer, instead of `kOutOfDomain`, naming the atmosphere plugin) that the original draft did not disclose | Disclosed as a new instance of ADR-0012's own already-accepted gap class; tracked as a `PLAN.md` follow-up (R11), not engineered around |
| 4 | "Every conforming `WrenchSource` must already tolerate 4x/frame calls" overstated `wrench_source.hpp`'s actual documented contract | Reframed as a contract this PR makes explicit via a doc-comment update (R10), not one that already existed |
| 5 | "`kCoreApiVersion` only bumps for vtable changes" is not the policy's actual wording (`StepContext`, a plain struct, is named explicitly); `Vehicle::PhysicsSpec`'s "never crosses a plugin boundary" claim is an empirical fact about current usage, not a structural guarantee | Reasoning corrected to "which type crosses the versioned loader boundary today"; recommendation (no bump) unchanged but now honestly grounded; a checkable doc-comment on `PhysicsSpec` added to the plan |
| 6 | A 1000x unit-conversion error (408 ns misread as 400,000 ns) made the wrench-call cost look 1000x more negligible than it measures, and one table row's prose contradicted its own data (case05 `force_z` at `dt=0.00025`) | Both corrected; the honest comparison is ~6% of this toy scenario's own (unusually low) per-frame cost, not ~0.003%, alongside the still-valid 0.0026%-of-real-time-deadline figure |

No failed assumption was found in: RK4's deferred mutation of `y` (the revert mechanism's own
precondition), the first-fault-wins retention logic, the fixed-size/no-heap-allocation reasoning, the
"exactly 4x, not 5x" `wrench_b()` call count, or retaining the frame-start `EnvironmentSample` for
`Vehicle::environment()`'s existing observability contract. Codex found no evidence that any of the
three rejected structural alternatives (early-abort `Integrator` hook; moving wrench logic into `Eom`;
a type-level stage-purity marker) is clearly superior to the chosen design.

## Correction round 1/2 (Joe/Cowork review, post-Codex-critique)

Two findings, both reproduced independently against the actual repository before being accepted (per
`docs/process/workflow.md`'s review-fix discipline — `Correction rounds: 1/2`):

| # | Finding | Reproduction | Resolution |
|---|---|---|---|
| 1 | The dt-sweep's `force_y`/`force_z`/`force_x` "misses" were a one-frame sampling-order artifact in the throwaway driver, not real physics — the committed R8 baseline shows these channels *pass* today | Independently recomputed the golden band's own 3-point backward difference at samples 298-300 (`force_y`: −16.09 N/s; `force_z`: +4.09 N/s) and matched it against the sweep's reported diffs to within finite-difference error; cross-checked `altitude`/`v_down`'s ratios (1.50/1.31) against `nesc-harness.md`'s own R8 table and confirmed exact agreement | Force-channel rows retracted (dropped, not re-measured); `altitude`/`v_down` rows kept (independently confirmed valid); Alternatives section and ADR rewritten to use the committed R8 baseline's real failing channels (`v_north` 4.85M×, `latitude` 5539×, etc.) instead — a *stronger*, correct rejection of the `dt`-reduction alternative |
| 2 | Q2 was not hypothetical (finite-difference math shows publishing `k4` would fail R2 by ~10x/17x/3x on force channels), and the plan's own proposed fallback (`t == ctx.t`) cannot be implemented (`wrench_b` takes no `t`) | Same finite-difference method as Finding 1, applied prospectively to the `k4`-vs-`k1` gap instead of the sweep's own frame-offset | Joe's decision: publish the vehicle's stage-1 total wrench core-side, via a plain stage counter local to the derivative functor; `nesc_aero` stops self-publishing (also resolving the R10 purity-contract tension against its own `mutable` publish); `nesc_cases_test.cpp` reads the new core-owned signal; new acceptance item R12 proves it is genuinely stage-1, not merely reviewed as such |

Q1/Q4 accepted as previously answered; Q3's conclusion accepted, its evidence corrected per Finding 1.
No golden file, ADR file, or code was touched — this round is plan-document-only, per the standing
oracle-guard fence (`ASTRA_TOUCH_GOLDENS` not armed this session).

**Second Codex critique pass** (narrow, scoped to only the two sections above, per Joe's own
instruction to re-run it before reporting) found three further, all now fixed in this same round:

| # | Finding | Fix |
|---|---|---|
| 3 | `Vehicle::published_wrench_` as a plain `Wrench` member would dangle: `Simulation::from_config`/`for_testing` move the local `Vehicle` into `vehicles_` *after* `init()` registers signal pointers into it (`simulation.cpp:340,378,459`) — the identical hazard `HealthMonitor::health_`'s existing `unique_ptr` is already there to avoid | Changed to `std::unique_ptr<Wrench> published_wrench_`, heap-allocated in `init()`, matching `health_`'s own established pattern |
| 4 | R12's design assumed the existing per-sample loop's `y` (a `const StateVector&` alias, not a copy) still reflects the pre-step state after `sim.run(1)` mutates it in place — it doesn't | R12 now specifies an explicit `const StateVector pre_step_copy = y;` taken *before* `sim.run(1)`, used to build the independent comparison instead of the (by-then-mutated) `y` |
| 5 | The first correction's replacement table reused `nesc-harness.md`'s **pre**-gravity-fix case05 numbers for `longitude`/`v_east`/`yaw`/`pitch`/`roll`, overstating case05's current misses | Replaced with the real post-fix numbers from `nesc-case05-gravity-centripetal.md:164`'s own committed dry-run table (833.8/761.5/3.44/8.42/6.84) — still all failing, just not by the stale, larger pre-fix margins; conclusion unchanged |

Both backward-difference values from Finding 1 (−16.09346 N/s, +4.09425 N/s) were independently
reproduced by Codex too. No further consumer of the removed `nesc_aero.force_b_*`/`moment_b_*` signal
names was found outside `nesc_cases_test.cpp`.

## Done when

- R1–R11 all pass, evidence retained (exact commands, exit codes, per-channel band numbers for cases
  4/5 — not "a trace matched").
- ~~One independent Codex plan critique obtained on this document~~ **Done** (per `PLAN.md`'s own PR 3c
  line: "One independent Codex plan critique required before any code" —
  `docs/process/workflow.md`'s plan-critique trigger for an expensive, unpinned design). Six findings,
  all resolved in this document — see "Plan critique — resolved" under the draft ADR above.
- Joe's explicit spec/plan approval recorded, including a decision on Q1/Q2/Q3/Q4 above.
- ADR-0016 (draft above) either accepted as a numbered `docs/decisions/` record or revised per Joe's
  decision.
- Full independent review (general + adversarial, given this touches `core/kernel/` and health
  behavior) per the standard `/review-cycle` bounded fix loop, before human-review packet.

## Plan critique

**Complete.** One independent, read-only Codex plan critique dispatched fresh (`/codex:rescue --fresh
--wait`, per `docs/agents/review-dispatch.md`'s procedure) against this document plus the repository/
ADR context it cites. Six failed assumptions found and verified against the actual repository; all six
resolved directly in this document (see "Plan critique — resolved" under the draft ADR above) before
this document was shown to Joe, per the workflow's "before Joe sees the plan" ordering. No findings
were rejected without action; none required abandoning the design.
