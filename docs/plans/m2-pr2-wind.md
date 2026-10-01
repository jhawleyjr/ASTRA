# M2 PR 2 — `WindModel` + `plugins/physical/wind/uniform` + the environment latch

Recovered from `~/.claude/plans/m2-pr-2-plan-md-snuggly-journal.md` on 2026-09-30. Approval: the file records none; the work merged as #25. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

`PLAN.md`'s M2 Environment line needs wind. `docs/architecture.md` §7 has pinned a `WindModel`
signature since the design was written, §4's tree reserves `plugins/physical/wind/{uniform, dryden}`,
and `core/src/kernel/simulation.cpp:174` already promises the slot in a comment ("Atmosphere (and,
from PR 2, wind) are optional environment models"). Nothing implements it.

The real work is **the wind injection point**, which `CLAUDE.md` names explicitly as an M2 design the
ADRs do not pin and that is expensive to unwind. Wind is the first environment quantity that must
reach a consumer which is **not** the EOM, and the first that is stochastic and time-evolving. Two
further facts make the seam, not the wind vector, the thing this PR is about:

- **PR 1b left `physical.atmosphere` with no caller.** As things stand PR 3 inherits *two*
  uncommitted injection points, plus the NESC harness, plus ten check cases. This PR pays down one
  of them and fixes the shape of the other.
- **Nothing in the kernel currently carries per-vehicle environment state.** `Vehicle::PhysicsSpec`
  holds `{mass, earth, gravity}` — the EOM's own inputs — and a `WrenchSource` is a generic
  `vehicle.models` entry (`vehicle.hpp:87`) whose only init seam is `init(config, registry)` and
  whose only step seam is `wrench_b(const VehicleState&) const noexcept`. There is no path by which
  it can see wind or atmosphere at all.

### Why wind is evaluated once per frame in the environment stage

**Correction from Codex's plan-review round, verified before writing this down:** an earlier draft of
this plan argued that `Eom::operator() const` structurally *cannot* call a non-const `wind_n`,
because `wind_n` is the one non-const method in §7. That mechanism is wrong. `Eom` holds
`EarthModel&`/`GravityModel&` as reference members, and a reference member does **not** propagate
the enclosing object's constness to the referent — calling a non-const method through a reference
member from inside a `const` member function compiles fine (checked directly:
`struct Holder { Mutable& ref_; void f() const { ref_.non_const_method(); } };` compiles). So even
if `Eom` held a `WindModel&`, `Eom::operator() const` could call a non-const `wind_n` through it.
The only reason `wind_n` is unreachable from the derivative today is that `Eom` simply has no
`WindModel` member — a topological accident, not a language guarantee, and it would silently stop
protecting anything the day a future PR gave `Eom` a wind reference for some other reason.

**The real, compiler-enforced guarantee comes from making `wind_n` itself `const`,** not from the
caller's constness. Inside a `const` member function, a class's own direct data members (not
reached through a reference/pointer indirection) really are const-qualified — mutating one is a
compile error unless it is marked `mutable` (also checked directly). So `wind_n() const` genuinely
cannot advance Dryden's own RNG or filter state from within itself; the only way to break that is an
`mutable` escape hatch, which is exactly the kind of thing a reviewer is primed to catch. This also
means `wind_n` is now safe to call from multiple call sites or multiple times per frame — not merely
safe today because nothing does so.

So: **all time evolution and RNG advance happen in `step(ctx)` (non-const, called only by the
scheduler); `wind_n(ctx, vs) const noexcept` is a pure read of whatever `step` last latched**, using
`vs` only for what does not require state (a body→nav rotation, a spatial lookup). This is exactly
§2 principle 2: "Random processes … are seeded and **stepped by the scheduler**, never by a system
RNG." It also means the design does not need `Eom`'s emptiness to hold, or the RK4-stage-latching
argument at all:

- `Vehicle::integrate` (`vehicle.cpp:57-58`) takes `const VehicleState vs{state_}` **once**, at the
  frame-start state, evaluates every `WrenchSource` against it, and hands the sum to
  `Eom::set_applied_wrench` which holds it across all four RK4 stages. So there is no per-stage
  wrench evaluation to protect wind from in the first place — this PR's tests target the real hazard
  (below), not a nonexistent RK4-stage one.

The live hazard is **call count and call site**: the realization must not depend on how many
consumers read it (guaranteed by `const`, since nothing they do can mutate it), and it must be
*sampled* at frame-start state in the environment stage, before the wrench stage (a kernel wiring
choice, which this PR does test). That is what this PR builds and tests.

### The four `PLAN.md` corrections this PR makes

1. **"steady-wind NESC cases within tolerance" moves to PR 3's line.** The NESC harness *is* PR 3,
   and `PLAN.md`'s PR 3 line already claims the drag `WrenchSource` and calls it "the only M2
   consumer of wind and atmosphere." Without a consumer, wind cannot move a trajectory, so the
   criterion is vacuous in PR 2.
2. **"bit-identical logs across two same-seed runs" is replaced.** HDF5 logging is M5; §16 puts
   "bit-identical log across two runs" under **M4**. PR 2 asserts instead: two independently
   constructed `Simulation`s from the same config, stepped N frames, agree **bitwise** — compared as
   `std::bit_cast<std::uint64_t>`, never `EXPECT_DOUBLE_EQ` — on the sampled `wind_n` sequence and on
   every value in a post-`freeze()` `SignalRegistry` snapshot. The registry is what the M5 logger
   will sample, so this is the closest honest analogue that compiles today, with no golden file and
   no `guard-oracle.sh` arming. The line says explicitly that the log-level criterion stays M4.
3. **The Dryden PSD criterion moves to PR 2b's line**, with the scope split below.
4. PR 3's line gains the two obligations this PR hands it (populate `EnvironmentSample::atmosphere`;
   the varying-wind model §16's M2 row implies).

### Scope: split deliberately, `uniform` now and `dryden` in PR 2b

PR 1 split mid-flight at ~1200 lines. This one is scoped down up front:

- **PR 2 (this plan):** `WindModel`, the per-vehicle environment latch and its consumer seam,
  `plugins/physical/wind/uniform`, the `physical.wind` slot, determinism and call-contract tests.
- **PR 2b:** `core/util/random.hpp` (`std::mt19937_64` + the in-house Box–Muller/uniform transforms
  ADR-0000 mandates), `plugins/physical/wind/dryden`, MIL-F-8785C scale lengths and intensities, and
  the PSD acceptance test. The RNG utility ships with Dryden, its only consumer, not unused here.

Honest cost, stated so nobody misreads a green PR 2 — sharpened by Codex's plan-review round: with
only `uniform`, the bitwise determinism assertions are near-trivial, because a constant model is
reproducible by construction and its call-count varies only with `ctx.frame`, never with seeded
stochastic state. **PR 2's determinism result is plumbing coverage — proof the latch, the call
contract, and the registry snapshot are wired correctly — not evidence that seeded stochastic
evolution is itself deterministic.** That claim is PR 2b's to make, reusing this same harness against
Dryden, where a bitwise mismatch would actually be diagnostic of a real RNG/filter bug rather than
trivially passing. This PR's own acceptance criteria (below) are worded to match: they claim the
seam, not the statistics.

**This PR does not, by itself, satisfy `docs/architecture.md` §16's M2 row.** That row requires the
NESC check cases (steady *and* varying wind) and the Dryden PSD match — both out of scope here by the
split above. M2 as a milestone remains open until PR 2b (PSD) and PR 3 (NESC harness, drag
`WrenchSource`) land; this PR's own PR line in `PLAN.md` says so explicitly rather than reading as if
ticking it closes M2.

## Design decisions

### D1 — three-way split: `step` (scheduler RNG), `begin_frame` (per-vehicle evolution), `wind_n` (pure read)

`Model::step(ctx)` is where scheduler-driven, consumer-count-independent evolution happens — the
part of §2 principle 2 that is unconditional: "Random processes … are seeded and **stepped by the
scheduler**, never by a system RNG." For PR 2b's Dryden that means the frame's white-noise draw.

**Revision, made necessary by a real gap in the split above:** `Model::step` receives only a
`StepContext` — no `VehicleState`. MIL-F-8785C's shaping-filter coefficients (scale lengths,
intensities, time constants `L/V`) are functions of true airspeed and altitude, so a filter advance
that needs them cannot happen in `step` at all — there is no path from `step` to a `VehicleState`.
Putting the whole evolution in `wind_n` (the original draft) was rejected in the const-correction
above for a different, sufficient reason (it would let evolution depend on consumer count), but even
setting that aside, `wind_n` is now `const` and genuinely cannot evolve anything.

So `WindModel` gains a third method, mirroring `Eom::begin_frame()`'s "latch what this frame's
evaluations need" role but per-vehicle rather than per-`Eom`:

```cpp
// Per-frame, per-vehicle evolution hook, called once per vehicle by Simulation::step immediately
// before wind_n for that same vehicle. This is the ONLY place a WindModel may use a VehicleState to
// evolve internal state -- Model::step has no VehicleState to offer, and wind_n is const and cannot
// evolve anything. Non-const and kernel-driven only: nothing outside Simulation::step calls it, so
// evolution still does not depend on how many WrenchSources later read wind_n. Default: empty
// (wind/uniform has no state to evolve). Per-frame path (section 14): no allocation, no exceptions.
virtual void begin_frame(const StepContext& ctx, const VehicleState& vs) noexcept;
```

`Simulation::step()` samples wind **once per vehicle**, at that vehicle's frame-start state, in the
environment stage:

```cpp
environment_scheduler_.step(ctx);               // scheduler-driven evolution only (RNG draw)
for (Vehicle& vehicle : vehicles_) {
  EnvironmentSample sample;                     // wind zero / atmosphere all-NaN by default
  if (wind_ != nullptr) {
    const VehicleState vs{vehicle.state()};
    wind_->begin_frame(ctx, vs);                // per-vehicle evolution: uses this frame's V, h
    sample.wind_n = wind_->wind_n(ctx, vs);      // pure read of what begin_frame just latched
  }
  // PR 3 populates sample.atmosphere here, from atmosphere_->at(earth_->altitude(...)).
  vehicle.set_environment(sample);
  vehicle.step(ctx);
  if (const HaltReason halt = vehicle.integrate(integrator_, ctx, *health_); halt) { return halt; }
}
```

Sampling at the top of the existing loop (rather than in a separate pass) keeps the diff to a few
lines and is still frame-start-correct: a vehicle's own `state_` is untouched until its own
`integrate` call.

`wind_n` gains **`const noexcept`** over §7's plain signature — a deviation from §7 larger than PR
1b's `noexcept`-only strengthening of `AtmosphereModel::at()`, so it is called out explicitly here
rather than folded silently into the header. `noexcept` is required by §14 and by
`Simulation::step() noexcept`. `const` is the correction above: it is what actually prevents a
`WindModel` from mutating itself out of `wind_n`, where non-const gave no such guarantee — and
`begin_frame` is where the mutation this design still needs now has an explicit, kernel-owned home
instead of leaking back into `wind_n`. §7 gains all three methods, with a comment explaining why.

**Stated explicitly, as directed, rather than left for PR 2b to discover:** PR 2b's Dryden is
intended to use the vehicle's **real, per-frame true airspeed and altitude** via `begin_frame` — that
is the entire reason this hook takes a `VehicleState` rather than reading a configured nominal `V`/`h`
from its own config. This has a direct consequence for PR 2b's PSD acceptance test, recorded now so
it lands on PR 2b's `PLAN.md` line rather than being decided informally mid-PR: **MIL-F-8785C's PSD
is a stationary-process spectrum**, meaningful only at fixed `V` and `h`. Since real flight state
varies, the PSD test must drive `begin_frame`/`wind_n` with a **synthetic, held-constant**
`VehicleState` (fixed `v_b`, fixed `p_n`) across the test's sample count — never a real trajectory —
to get a stationary realization to test against the closed-form spectrum. `PLAN.md`'s PR 2b line
says this.

**This also sharpens D4's shared-model hazard into something concrete rather than abstract.** A
non-shareable model's `begin_frame` would be called once per vehicle, each with a *different*
`VehicleState` — for a model whose evolution is keyed to `V`/`h`, that is not a subtle statistical
concern but a filter visibly fed two different airspeeds in the same frame. `begin_frame`'s existence
is why `shareable_across_vehicles()` defaults to `false` and is enforced, not merely documented.

### D2 — the latch shape is decided now: `EnvironmentSample` carries wind **and** atmosphere

Per the explicit choice to not leave this implicit, and because PR 3 is already carrying the harness
and ten cases:

```cpp
// core/include/astra/kernel/environment_sample.hpp
// One vehicle's environment for one major frame, sampled by Simulation::step in the environment
// stage (docs/architecture.md §5) and held constant through that vehicle's wrench and EOM stages --
// the same latching discipline Eom::begin_frame and Eom::set_applied_wrench already use.
struct EnvironmentSample {
  // Velocity of the air mass w.r.t. the Earth, in n (NED). Zero when no physical.wind is configured:
  // still air is the correct default, and a WrenchSource that cannot accept it declares
  // EnvironmentRequirements::wind and is rejected at init instead (wrench_source.hpp).
  Eigen::Vector3d wind_n = Eigen::Vector3d::Zero();
  // All-NaN by default (AtmoState's own designed default). Populated from PR 3, when the drag
  // WrenchSource becomes the first consumer; a WrenchSource that needs it declares
  // EnvironmentRequirements::atmosphere, so the NaN is never silently read.
  AtmoState atmosphere{};
};
```

Defining both fields now costs ~4 lines and means PR 3 adds one line to the kernel instead of
redesigning the snapshot under review pressure. `wind_n` defaults to zero (still air is physically
right); `atmosphere` defaults to all-NaN (a fabricated density is not), and the requirement
mechanism below is what stops either default being read by a model that cannot accept it.

### D3 — the consumer seam: `WrenchSource` gains the sample and a declared requirement

`WrenchSource::wrench_b` is extended to receive the frame's sample. Only `tests/plugins/nan_wrench`
implements this interface today, so the change is cheap now and expensive later:

```cpp
[[nodiscard]] virtual Wrench wrench_b(const VehicleState& vs,
                                      const EnvironmentSample& env) const noexcept = 0;

// Init-path. Which optional environment slots this source cannot run without.
// Simulation::from_config rejects a config that omits a required slot with a ConfigError naming
// physical.wind / physical.atmosphere -- never a silent zero wind or a NaN density.
[[nodiscard]] virtual EnvironmentRequirements requires_environment() const noexcept;  // default: none
```

with `struct EnvironmentRequirements { bool wind = false; bool atmosphere = false; };`. This is what
makes `simulation.cpp:176`'s existing promise — "A wrench source that requires one and finds none is
an init error naming `physical.atmosphere`, never a silent rho = 0" — actually true and testable
rather than a comment. The typed seam is chosen over publishing wind as registry signals and having
consumers `find()` them, because environment signals publish **unprefixed** (`load_environment_model`
opens no `PrefixScope`), so a registry route cannot express a per-vehicle value at all once ADR-0011's
"nothing may assume exactly one vehicle" is taken seriously.

**Where the requirement is enforced.** `Vehicle::wrench_sources_` is private and is populated inside
`Vehicle::init` (`vehicle.cpp:29-32`), with no accessor — so `Simulation::from_config` cannot walk
the sources after calling `init`. Rather than add an accessor purely for a validation pass,
`Vehicle::init` gains one more parameter:

```cpp
// Which optional environment slots Simulation::step will actually populate into EnvironmentSample
// this frame. Atmosphere is deliberately absent: EnvironmentSample::atmosphere is never populated
// in M2 PR 2 (always the NaN default), so there is no config that could make an availability flag
// for it honestly true, and a flag paired with a "PR 3 must remember to flip this too" comment is
// not a mechanism -- an earlier draft had exactly that pairing, and Codex's plan-review round
// caught it as inconsistent (EnvironmentAvailability{wind != nullptr, false} always passed false
// for atmosphere regardless of what physical.atmosphere said). Vehicle::init instead rejects EVERY
// WrenchSource whose requires_environment().atmosphere is true, unconditionally -- see below. PR 3
// adds this field back in the same change that starts populating EnvironmentSample::atmosphere, so
// the two ship as one commitment instead of two that can drift apart.
struct EnvironmentAvailability { bool wind = false; };
```

The check runs immediately after the `dynamic_cast<WrenchSource*>` that discovers each source:

```cpp
const EnvironmentRequirements req = wrench_source->requires_environment();
if (req.wind && !availability.wind) {
  throw ConfigError(spec.config.origin(), "physical.wind", spec.plugin_path + " requires wind");
}
if (req.atmosphere) {
  // No PR before M2 PR 3 can populate EnvironmentSample::atmosphere (D2), so this is unconditional
  // -- not "if physical.atmosphere is absent": even a config naming physical.atmosphere cannot
  // satisfy a WrenchSource that needs it yet, because nothing connects the two before PR 3.
  throw ConfigError(spec.config.origin(), "physical.atmosphere", spec.plugin_path + " requires atmosphere");
}
```

`EnvironmentRequirements` itself keeps both fields (`{bool wind = false; bool atmosphere = false;}`)
unchanged: a `WrenchSource` still needs to be able to *declare* it wants atmosphere, for PR 3 to
satisfy later — only the kernel-side availability tracking drops the field this PR cannot make
truthful. The check is colocated with the only place the requirement is discoverable, `init` already
throws `ConfigError`/`PluginError` on this path, and no new public surface appears on `Vehicle`.

`uniform` still publishes `wind.north`/`wind.east`/`wind.down` (unit `"m/s"`, no unit suffix in the
name per ADR-0002) for the log and for the determinism snapshot — as observability, not as the seam.

### D4 — the shared-model hazard: models declare shareability, `from_config` enforces it

ADR-0011 makes environment models **shared across vehicles** while forbidding any assumption of
exactly one vehicle. A wind model carrying evolving internal state is therefore a live hazard: one
filter stepped once per frame, sampled by N vehicles, hands every vehicle the *same* gust — and two
aircraft kilometers apart flying bit-identical turbulence is wrong independently of any
double-advance concern, because turbulence is spatially decorrelated.

Taking the "declare single-vehicle and reject" option:

```cpp
// Whether this model's per-frame output is correct when shared by more than one vehicle (ADR-0011).
// Defaults to FALSE: refusing to run beats a plausible-looking wrong answer (§15), and a model with
// internal state that its author has not thought about must not silently produce one realization for
// every vehicle. wind/uniform overrides to true (it is stateless and position-independent);
// PR 2b's dryden leaves it false.
[[nodiscard]] virtual bool shareable_across_vehicles() const noexcept;  // default: false
```

`Simulation::from_config` rejects `vehicles_.size() > 1` with a non-shareable wind model, as a
`ConfigError` naming `physical.wind`. Stated plainly: **this guard cannot fire today** —
`from_config` builds exactly one `Vehicle`. It is written now because that is the only moment it is
cheap, and because PR 2b's Dryden is precisely the model it exists for. It is tested at the predicate
level, not through an unreachable config.

### D5 — sign and frame convention, pinned in the header

The highest-risk detail in the PR; a sign error here reads as plausible physics. `wind_n` returns the
**velocity of the air mass with respect to the Earth, expressed in n (NED), m/s** — "wind-to", not the
meteorological "wind-from". A 10 m/s wind *from* the north is `wind_n = (-10, 0, 0)`. Consumers form
air-relative velocity as `v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n)`. ADR-0001
requires it: "Any later frame (wind, stability, sensor mount) is defined relative to b or n here,
never freshly." A unit test pins the sign so the convention is oracle-backed, not comment-backed.

### D6 — `physical.wind`'s scheduler `divisor` is rejected unless 1

`resolve_model_spec` accepts a `divisor` on any slot (the map spelling `{ plugin: ..., divisor: N }`),
and the atmosphere/gravity/earth precedent passes it straight to `environment_scheduler_.add`. Doing
the same for wind would let `WindModel::step()` — the scheduler-driven RNG draw — run only every Nth
frame, while `begin_frame`/`wind_n` are called **unconditionally every frame** by `Simulation::step()`
(D1), since they are not routed through the scheduler at all. For `uniform` this is harmless (`step`
does nothing). For a stateful model it is a real rate mismatch with no defined meaning yet: does
`begin_frame` hold the last-drawn noise for N frames (a zero-order hold on the input), or is a
divisor on wind simply not a coherent idea until there is a consumer motivating the cost saving it
would buy? This PR has no such consumer and no reason to invent the answer under review pressure, so
it does not — `physical.wind` rejects any `divisor != 1`:

```cpp
const Vehicle::ModelSpec wind_spec = resolve_model_spec(physical.at("wind"));
if (wind_spec.divisor != 1) {
  const std::string path =
      wind_spec.config.path().empty() ? "divisor" : wind_spec.config.path() + ".divisor";
  throw ConfigError(wind_spec.config.origin(), path,
                    "physical.wind must run every major frame (divisor 1): begin_frame and wind_n "
                    "are sampled every frame regardless of this model's own divisor, so a coarser "
                    "step() would silently desynchronize its evolution from what is read");
}
```

matching `checked_divisor`'s own path-naming idiom (`wind_spec.config` is the `physical.wind` entry
itself, per `resolve_model_spec`). `environment_scheduler_.add(*wind, wind_spec.divisor)` still runs
afterward with the now-guaranteed-1 divisor, so `step()` executes every frame in lockstep with
`begin_frame`/`wind_n` — no rate-mismatch state exists to reason about, at the cost of not yet
supporting a slower-than-1kHz wind update. That cost is deferred to whichever future PR has a
concrete reason to pay it.

## Files

**New — core**
- `core/include/astra/interfaces/wind_model.hpp` — `begin_frame` (non-const, `noexcept`), `wind_n`
  (`const noexcept`), `shareable_across_vehicles()`, `~WindModel() override;` **declared, not
  defaulted inline**. The key function is load-bearing: plugins build `-fvisibility=hidden`, and
  without it each `.so` carries a private vtable/typeinfo, breaking the `dynamic_cast<WindModel*>`
  that `from_config` performs and UBSan's vptr check. Header comment carries D1 (the three-way
  split, why `wind_n` is `const`, and the corrected reasoning — not the rejected "`Eom`'s constness
  excludes it" argument), D4, and D5.
- `core/src/interfaces/wind_model.cpp` — key function + the two non-pure defaults, mirroring
  `core/src/interfaces/atmosphere_model.cpp` and `gravity_model.cpp` in shape.
- `core/include/astra/kernel/environment_sample.hpp` — `EnvironmentSample`,
  `EnvironmentRequirements` (D2).
- `core/CMakeLists.txt` — add the new `.cpp`, alphabetically.

**Modified — core**
- `core/include/astra/interfaces/wrench_source.hpp` (+ its `.cpp` for the new default) — D3.
- `core/include/astra/kernel/vehicle.hpp` / `core/src/kernel/vehicle.cpp` — an `EnvironmentSample`
  member, `set_environment` / `environment()`; `wrench_b` called with it in `integrate`; `init`
  gains the `EnvironmentAvailability` parameter and the `requires_environment()` check described
  above (D3's "where enforced" note).
- `core/include/astra/kernel/simulation.hpp` / `core/src/kernel/simulation.cpp` — a `WindModel*
  wind_` member; the `physical.wind` optional-slot block immediately after the atmosphere block
  (`has("wind")` → `resolve_model_spec` → **the D6 `divisor != 1` rejection** → `load_environment_model`
  → `dynamic_cast` → `PluginLoadError(path, "does not implement WindModel")` →
  `environment_scheduler_.add` → `environment_plugins_.push_back`); the per-vehicle `begin_frame`/
  `wind_n` sampling in `step()` (D1); passing `EnvironmentAvailability{wind != nullptr}` into
  `vehicle.init`; the D4 guard. No position-representation check and no `valid_range` analogue —
  `uniform` never reads `p_n`, so there is no domain to validate. Update the `:171-177` comment so
  "from PR 2, wind" is true.
- `tests/plugins/nan_wrench/nan_wrench.cpp` — the sole existing `WrenchSource` implementor;
  `wrench_b` gains the `EnvironmentSample` parameter (unused, per its existing `/*vs*/`-style
  convention). A one-line, mechanical change, called out here so it is not discovered mid-PR.

**New — plugin**
- `plugins/physical/wind/uniform/CMakeLists.txt` — `astra_add_plugin(SOURCES uniform.cpp)`. No parent
  CMake edit; `astra_discover_plugins(plugins)` globs recursively.
- `plugins/physical/wind/uniform/uniform.cpp` — single self-contained `.cpp`, anonymous namespace,
  following `plugins/physical/gravity/constant/constant.cpp`. Reads `north`/`east`/`down` (m/s,
  defaulting to 0.0 so `load_all_test` can construct and init it from an empty config), each
  finite-checked through the `keyed_path`/`checked_finite` idiom the sibling plugins duplicate — an
  unvalidated NaN here reaches every consumer with no init-time error, which is the finding Codex
  raised against `gravity/constant`. Registers the three signals; `name()` → `"wind"`; descriptor
  name `"wind_uniform"`, category `"physical"`. `step()` empty; `shareable_across_vehicles()` → true.
  Writing `seed:` on it is an unknown key and therefore a `ConfigError` (§15) — the right outcome,
  and worth a test, since "I set a seed and it was ignored" is the silent-determinism failure §15's
  guard exists to prevent.

**New — test plugins** (`tests/plugins/`, the `dummy`/`nan_wrench` pattern)
- `tests/plugins/counting_wind/` — a `WindModel` that counts `begin_frame()`, `step()`, and
  `wind_n()` calls separately and records, per call, the `ctx.frame` and the `p_n`/`v_b` it was
  handed, publishing the counts as signals. `wind_n` returns a value derived from what `begin_frame`
  last latched, so the two can be checked for ordering, not just count. Since `wind_n` is now
  `const`, its call-counting members are `mutable` — an explicit, visible exception for test
  instrumentation only (it observes calls, it does not change what `wind_n` returns), never the
  pattern a real wind model follows.
- `tests/plugins/wind_consumer/` — a `WrenchSource` declaring `requires_environment().wind = true`
  and nothing else: no Cd·A, no physics. It records the `env.wind_n` it was handed and returns a
  zero wrench. It exists to make the init-error contract and the latch assertable.
- `tests/plugins/atmosphere_consumer/` — a `WrenchSource` declaring
  `requires_environment().atmosphere = true` and nothing else. It exists to make D3's unconditional
  atmosphere rejection assertable — including against a config that *does* name
  `physical.atmosphere`, proving the rejection does not depend on that slot's presence.

**Modified — docs**
- `docs/architecture.md` §7 — `WindModel` gains `const noexcept` and the two non-pure virtuals, with
  a short comment block, exactly as §7 gained `AtmoState`/`AltitudeRange` comments in PR 1b. The
  comment says why `const` was added over the plain §7 sketch.
- `PLAN.md` — the four corrections above; tick PR 2; add the PR 2b line, which states the
  `begin_frame`-uses-real-per-frame-`V`/`h` decision (D1) and its consequence that the PSD test
  drives a synthetic held-constant `VehicleState`, not a real trajectory.
- `STATE.md`, `docs/lessons.md` — per `/wrap`.

## Tests

`tests/unit/wind/wind_test.cpp` and `tests/unit/wind/wind_determinism_test.cpp`, added to
`astra_unit_tests` in `tests/unit/CMakeLists.txt` (label `unit` comes from the existing
`gtest_discover_tests`). A trap to avoid: `isa1976_test.cpp`'s helper re-`init`s a `static` plugin on
every call, harmless only because `isa1976` registers no signals; `uniform` does, so a second `init`
against a live registry throws on duplicate names. Use a fresh `SignalRegistry` per load.

**`uniform`** — config values reach `wind_n`, attitude-independently; defaults are zero on an empty
config; a non-finite component throws `ConfigError` with `config_path() == "physical.wind.north"`;
the three signals are registered with unit `"m/s"` and track `wind_n`; **`SignConventionIsAirVelocityNotWindFrom`**
— a "wind from the north at 10 m/s" config is `north: -10.0`, and a level vehicle facing north at rest
gets `v_rel_b = (+10, 0, 0)`, a headwind (D5); `seed:` is rejected as an unknown key.

**The call contract**, with `counting_wind` + `wind_consumer` in one `Simulation`:
- **`BeginFrameThenWindNAreEachCalledExactlyOncePerVehiclePerFrame`** — after N frames, both the
  `begin_frame` and `wind_n` call counts are exactly N, and `begin_frame`'s count is always one
  ahead-or-equal within the same frame (i.e. never called after `wind_n` in a given frame). This is
  the determinism/PSD contract PR 2b depends on, and it fails under any design that evaluates
  either inside the wrench stage or per consumer.
- **`WindIsSampledAtFrameStartStateBeforeTheWrenchStage`** — the `p_n`/`v_b` `begin_frame` was
  handed equals the pre-step state, not the post-step one (under a gravity that moves it), and the
  `wind_n` call count observed by `wind_consumer` at its own call time already includes this frame's.
- **`TheSampledWindReachesTheWrenchSource`** — the `env.wind_n` `wind_consumer` recorded is bitwise
  equal to what `counting_wind` returned for that frame.
- **`AWrenchSourceRequiringWindWithNoWindSlotIsAnInitError`** — a config with `wind_consumer` in
  `vehicle.models` and no `physical.wind` throws `ConfigError` naming `physical.wind`. This is the
  contract `simulation.cpp:176` has claimed in a comment since PR 1b and nothing has asserted.
- **`AWrenchSourceRequiringAtmosphereIsAlwaysRejectedInThisMilestone`** — `atmosphere_consumer` in
  `vehicle.models` throws `ConfigError` naming `physical.atmosphere`, both with and without
  `physical.atmosphere` configured — proving D3's rejection is unconditional, not merely "slot
  absent" (the same distinction Codex's finding turned on).
- `NonShareableWindModelPredicate` — `uniform` is shareable; the default is not (D4).

**Slot wiring**, in `tests/unit/kernel/simulation_physical_config_test.cpp`:
- **`RejectsAnUnknownPhysicalSlotName` currently uses `wind: plugins/physical/wind/uniform` as its
  unknown-slot fixture** and asserts the error mentions `physical.wind`. This PR makes that slot
  real, so the test would silently stop testing what it claims. Repoint it at a genuinely unknown
  slot (`magnetic:`). Call this out in the PR body so it does not read as a weakened test.
- `AcceptsAPhysicalSpecWith`/`WithoutWind`, mirroring the atmosphere optionality pair.
- `RejectsAWindPluginThatDoesNotImplementWindModel` (`wind: plugins/physical/atmosphere/isa1976`).
- **`RejectsAWindDivisorOtherThanOne`** (D6) — `physical.wind: { plugin: .../uniform, divisor: 10 }`
  throws `ConfigError` naming `physical.wind.divisor`.

**Determinism** (`wind_determinism_test.cpp`) — the harness PR 2b reuses: build two `Simulation`s
from the same config, step N frames, assert bitwise equality via `std::bit_cast<std::uint64_t>` on
(a) the per-frame sampled `wind_n` and (b) every `Signal::value` in the frozen registry snapshot,
matched by name; plus the final `StateVector`. Written as a reusable helper, since PR 2b's Dryden
test is the same test with a different config.

`tests/configs/wind_uniform.yaml` — the end-to-end fixture; no test config currently names an
optional environment slot, so this is also the first YAML exercise of one. Flat Earth, constant
gravity, `physical.wind: { plugin: plugins/physical/wind/uniform, north: 5.0, east: -2.0, down: 0.0 }`
— the §13 map spelling from `docs/architecture.md:326`.

No file under `tests/golden/**` is added, so `guard-oracle.sh` never fires and nothing here is
human-owned.

## Not in this PR

- **Dryden, the RNG, MIL-F-8785C, and the PSD test** — PR 2b. Its `PLAN.md` line carries the PSD
  acceptance criterion.
- **Populating `EnvironmentSample::atmosphere`** — PR 3, one line in `Simulation::step()`, against a
  struct field and a requirement mechanism this PR already ships.
- **The varying-wind model** §16's M2 row implies ("steady **and varying** wind") — neither `uniform`
  nor Dryden produces a commanded time-varying mean. PR 3 decides between a
  `plugins/physical/wind/table` plugin and a `tests/plugins/` one; recorded on PR 3's line so it is a
  decision, not a discovery.
- **The per-frame domain-validity gap** disclosed in PR 1/PR 1b. Wind adds **no new instance**:
  `uniform` has no domain and divides by nothing. The existing gap is unchanged and still must be
  resolved or consciously re-accepted before PR 3's drag `WrenchSource`.
- **`core/util/units`** — ADR-0002 names it and it does not exist. `uniform` reads SI and needs none;
  PR 2b needs ft→m and kt→m/s for MIL-F-8785C and should create it there, with unit tests, rather
  than multiplying by `0.3048` inline.
- **A `physical.wind` `divisor` other than 1** (D6) — rejected outright rather than designed, since
  nothing in PR 2 needs a wind update slower than the major frame, and `begin_frame`/`wind_n` running
  every frame regardless of `step`'s own divisor has no defined hold semantics yet. Deferred to
  whichever future PR has a concrete reason to pay for that design.
- **Dryden's coefficients come from the vehicle's real per-frame `V`/`h` via `begin_frame`, not a
  configured nominal** (D1) — decided here, not left for PR 2b to discover, because it changes what
  PR 2b's PSD test must drive: a synthetic held-constant `VehicleState`, never a real trajectory.
  `PLAN.md`'s PR 2b line states this.

## Verification

One gate, run once, after the work:

```
just check
```

Per `CLAUDE.md`: launch with the Bash tool's own `run_in_background` (never a nested `&`/`nohup`/
`disown` — `docs/lessons.md`, twice), block on the real process with
`tail --pid=$(pgrep -x just) -f /dev/null`, and read the log's `EXIT=0` / `check: OK` line. Nothing is
verified before that line has been read. Do not edit tracked files while it is in flight.

Must exit 0 on every preset (`debug`, `release`, `asan`, `static-plugins`) with `clang-tidy`,
`clang-format --dry-run --Werror`, `ruff`, `pyright`, and `pytest` clean. Iteration:
`ctest --preset asan -R Wind --output-on-failure`.

What must be true for this PR to be done:
- `wind_n` is called exactly once per vehicle per frame, at frame-start state, in the environment
  stage before the wrench stage — asserted, not argued.
- The sampled wind reaches a `WrenchSource` through `EnvironmentSample`, and a source that requires
  wind with no `physical.wind` is an init error naming the slot.
- Two same-config runs are bitwise identical on the sampled wind and the registry snapshot.
- `physical.wind` is optional; a config without it behaves exactly as before.
- §7's `WindModel` block matches the shipped header.

Before the PR: `git add -A` and commit first — Codex's `git diff` never sees untracked files
(`docs/lessons.md`), and this PR is mostly new files. Then
`node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait`,
plus `adversarial-review --base origin/main --wait` with focus naming ADR-0001 (the wind-to sign and
the n-frame), ADR-0002 (SI, unit as metadata), ADR-0003 (`p_n` meaning), ADR-0009 (plugin layout),
ADR-0011 (shared environment models, D4), §2 (scheduler-stepped randomness), and §14 (no per-frame
allocation or exceptions). This is a physics-touching diff, so Joe reads it.
