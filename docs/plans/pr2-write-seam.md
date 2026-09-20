# PR 2 — Control-input write seam

Status: **approved by Joe**, following two Codex plan critiques (twelve findings, then six more, all
resolved; see "Plan critique round 1/2 — resolved" below), Joe's own review rounds 1 and 2 (six,
then five further findings, all resolved; see "Joe review round 1"/"Joe review round 2" below), and
one final fold-in (entries default-initialize to quiet NaN, not an unspecified value — (a)/Design/
R10/ADR) requiring no further review round. Q1–Q3 and (h) decided — see "Questions and decisions."
ADR-0017 accepted and written clean at `docs/decisions/0017-control-input-write-seam.md`.
Owner: Joe
Risk: **Tier 3** — touches `core/kernel/` (`Vehicle`, `Simulation::step`), the plugin-facing ABI
(`WrenchSource`'s vtable — see (g)'s correction below), and health behavior (`docs/process/
workflow.md`'s risk-tier table names all three explicitly). Minimum process: full relevant
artifacts (this document), Joe approves spec/plan, independent adversarial review, full gate, Joe
reads the critical diff/tests.
Intent/approval: `PLAN.md`'s own PR 2 line (direction already chosen by Joe: `WrenchSource::wrench_b`'s
signature does not change; control values are plugin-internal members written through a named-input
write seam, zero-order hold across a frame's four RK4 sub-stages). Joe's design brief this session
(2026-09-19) sets items (a)–(g) below as the scope this document must resolve, with options and a
recommendation each, before one Codex plan critique and before Joe sees it. **Planning only — no
code, tests, or existing docs touched this session.**
Architecture/ADRs: `docs/architecture.md` §5 (frame order — this PR adds one call inside it, does
not reorder it), §7 (Model interfaces, the "Control inputs in v1" paragraph — `ActuatorModel` slots
in later "without changing `AeroInputs`", which is the M4 boundary this PR must not reach across),
§9 (`SignalRegistry` — the read side this PR's write side mirrors), §13 (config schema — `vehicle.
models` as the only per-model config surface today), §14/§15 (per-frame rules, health monitoring).
ADR-0011 (one `Vehicle` as list-of-one; per-vehicle signal namespace). ADR-0016 (per-RK4-substage
wrench evaluation — the zero-order-hold precedent for `WindModel::begin_frame`/`wind_n`, and the
purity contract `WrenchSource::wrench_b` already documents). Draft ADR-0017 below.
System context: A `WrenchSource` (M3 PR 3's `AeroModel`/`PropulsionModel`) needs a way to receive an
externally driven value — elevator deflection, throttle — that is not a plain code constant.
`SignalRegistry` (§9) only supports a model *publishing* a value it already owns; nothing today lets
something outside the model *set* one. This PR builds that write side so PR 3's aero/propulsion
plugins, PR 5's Python trim/linearize, and M4's transport (later, an `ActuatorModel`) can all set such
values through one seam, once per major frame, before the wrench stage, with the same zero-order
hold discipline `WindModel::begin_frame`/`wind_n` already established for wind (ADR-0016).

## Outcome and boundaries

**Problem.** Nothing in the current interfaces lets code outside a `WrenchSource` set a value that
model's `wrench_b()` will read. A control surface deflection or throttle command is exactly this
shape (Stevens & Lewis, 3rd ed., §2.5, p.109–110, Eq. (2.5-3)/(2.5-4): the control vector `Ū` is
*separate from* the state vector `X` — never folded into it, never state), and three different
callers need to set it over the project's life: a config literal today (open-loop, this PR), Python
trim/linearize tomorrow (PR 5), and M4's transport/`ActuatorModel` later. All three must write
through the same mechanism, or PR 5 and M4 each reinvent it.

**Goal.** A named, writable-`double` seam — this document calls the type `InputRegistry`, `Signal
Registry`'s mutable counterpart — that a `WrenchSource` registers from a new `bind_inputs()` hook
(`elevator_ = inputs.add(registry, "controls.elevator", "rad")`, `elevator_` a `const double*` into
registry-owned storage, matching `SignalRegistry::add`'s own established call shape and (h)'s
correction below) and that anything holding a reference to it can set, once per major frame, before
that vehicle's wrench/EOM/integrator stage runs. `WrenchSource::wrench_b()`'s signature is unchanged:
the plugin reads through its own `const double*` (`*elevator_`) exactly as it reads any other of its
own fields today; the seam's job is only to make that value externally settable, safely and namedly.
Zero-order hold across the frame's four RK4
sub-stages follows for free from `wrench_b()`'s own existing purity contract (`wrench_source.hpp`:
a pure function of `(vs, env)` "with no hidden per-call state") plus one new invariant this PR adds:
the member the seam writes changes **at most once per major frame**, at a fixed point before the
first RK4 stage runs — the identical shape `WindModel::begin_frame`/`wind_n` already uses for wind
(ADR-0016), and the shape S&L's own §3.4 (p.170) states as the general justification: "the time step
must be made small enough that the control input can be approximated by a constant value during any
interval `kT` to `(k+1)T`."

**Non-goals.** No `AeroModel`/`PropulsionModel` implementation (PR 3). No Python binding (PR 5's own
`sim.write_input(name, value)`, D7 in `PLAN.md`). No transport/`ActuatorModel` (M4). No change to
`WrenchSource::wrench_b()`'s signature, `Integrator`, or `dt`. This PR builds the primitive and wires
its one required call site into `Simulation::step`; it does not build a control surface.

**Invariants preserved.**
- The *spirit* of ADR-0016's purity contract — no dependence on call count or order *within* a
  frame. **Correction from Codex's plan critique**: `wrench_source.hpp:28`'s doc comment, as
  literally written today, promises more than that ("may depend only on the arguments given, never
  on how many times or in what order it has previously been called *this frame or any other*") —
  identical `(vs, env)` producing a different result after a seam write between frames genuinely
  contradicts that literal text. This PR does not get to claim the existing contract already
  permits the seam; it must **amend** `wrench_source.hpp`'s doc comment to carve out control-seam
  members explicitly, the same way ADR-0016's own PR had to write down `wrench_b`'s multi-call
  contract for the first time rather than assume it already existed. **Fold-in from Joe's pre-
  second-critique review**: the amendment must also state the other direction explicitly — a
  conforming `WrenchSource::wrench_b()` may *read* any input through its own `const double*`, but
  must **never write** through an `InputHandle` from inside `wrench_b()`. A production `WrenchSource`
  writing its own or another model's input from within its own wrench evaluation would make one
  RK4 stage's output depend on a hidden mutation no caller asked for, exactly the class of hidden
  per-call state the existing contract already forbids in spirit, just not yet stated for this
  specific direction. `tests/plugins/input_consumer` (R3's own fixture) is a **labeled, test-only
  exception** to this rule, doing exactly what a real `WrenchSource` must not — the identical shape
  `tests/plugins/nan_wrench`'s own `mutable call_count_` already uses ("the 'test instrumentation
  only' exception `nan_wrench`'s own header comment documents for a `const wrench_b()`," confirmed
  directly this session). See Design below and R3's own acceptance row for where this label lives.
  **Extended, second Codex critique (finding 2)**: the same prohibition applies to `Model::reset()`
  — a conforming `WrenchSource`'s `reset()` must also never write through a cached `InputHandle`;
  `step()` is the one sanctioned write point (R4's `ActuatorModel` case). Found necessary because a
  plugin caching a *writable* `InputHandle` (not just the read-side `const double*`) could otherwise
  let its own `reset()` silently interfere with `InputRegistry::reset()`'s own restore — see (b) and
  the Design section's own corrected account.
- §14: per-frame path stays allocation-free and exception-free. The seam's per-frame commit
  (`latch()` below) is `noexcept`, touches only plain `double`s already allocated at init, and does
  no string work.
- ADR-0011: the seam is per-`Vehicle`, not global — see (f) below.
- §15's doctrine ("a sim that runs from a broken setup is worse than one that refuses") governs (a)
  below.

**Compatibility.** `WrenchSource` gains a new virtual method (`bind_inputs`, see Design below) — a
plugin-facing ABI break. `Model::init()`'s own signature is **not** changed (see Design and (g):
Codex's plan critique caught that `Simulation` — not `Vehicle` — owns the one shared `SignalRegistry`
(`simulation.hpp:150`), and shared environment models call `Model::init()` directly from
`Simulation::from_config` with no vehicle namespace open at all (`simulation.cpp:71`'s
`load_environment_model`) — so changing `Model::init()`'s own signature would force every
`EarthModel`/`GravityModel`/`AtmosphereModel`/`WindModel`/`MassPropertiesModel` plugin to also accept
an `InputRegistry&` parameter it has no use for; the seam belongs on `WrenchSource` only). See
"`kCoreApiVersion`" below (recommendation: bump).

Assumptions:
- A `WrenchSource` implementation that uses this seam satisfies `wrench_source.hpp`'s *amended*
  purity contract (see "Invariants preserved" above — this PR must write that amendment, not assume
  it pre-exists): no dependence on call count/order *within* a frame; the canonical value a plugin's
  `const double*` reads (h) may differ *between* frames. A canonical value set once per frame, read
  many times per frame through a stable pointer, is exactly the shape `WindModel`'s own `wind_n()`
  already relies on under its own, separately-documented contract.
- This PR does not itself validate an `AeroModel`/`PropulsionModel` — none exists yet (PR 3). The
  acceptance evidence below uses a small test-fixture plugin (`tests/plugins/input_consumer`,
  mirroring the existing `wind_consumer`/`atmosphere_consumer`/`nan_wrench` fixtures), not a real
  aero model.
- Nothing outside `libastra_core` calls `InputRegistry`'s internals directly today; Python's own
  access (PR 5) is a future nanobind nanobind boundary this PR does not build, only leaves room for.

## Design questions (a)–(h)

Each below: options considered, recommendation, and why — citing precedent already in this
repository and the assigned textbooks where they bear on the choice.

### (a) An input registered but never written

**Options.**
1. *Init error, no code-level default.* `InputRegistry::add()` (whatever its exact parameter list —
   settled later in this document by (h)'s storage-ownership question) takes no default value.
   Something throws `ConfigError` naming the input's dotted path if, by the time the check runs,
   nothing has written it. **Correction, second Codex critique (finding 3)**: the original wording
   here said "`Vehicle::init()` throws" — wrong. `Vehicle::init()`'s actual signature (confirmed
   directly, `vehicle.hpp:96`) takes a `ModelSpec` list, never the vehicle's own config subtree, so
   it has no access to `vehicle.initial_controls` (e) at all and cannot apply or validate config
   literals itself. The check (and the config-literal application it depends on) must live in
   `Simulation::from_config`, called after `vehicle.init()` returns — see the Design section's own
   corrected account, and R10's own precedent that `Simulation::for_testing` already skips several
   `from_config`-only validations by design (its own documented list, `simulation.hpp`), which this
   check now correctly joins.
2. *Mandatory code-level default.* `add()` takes a mandatory `default_value` argument (mirroring
   `Config::number(key, default_value)`'s own two-arity precedent, `config.hpp`); a never-written
   input simply keeps that value forever. Never silently zero, because the plugin author must
   choose a value explicitly at the call site — but the chosen value is baked into the plugin's own
   source, not the vehicle's config.

**Recommendation: option 1** — init error, no code-level default. `AGENTS.md`'s own closing line —
"a sim that runs from a broken setup is worse than one that refuses" — and `docs/architecture.md`
§15's identical doctrine both argue for refusing rather than running on an unexamined default. More
concretely, `STATE.md`'s own record of M3 PR 1's scaffolding review names "numbers in YAML not code"
as a direction Joe corrected toward during that PR — a code-level default (option 2) is exactly the
kind of number this project has already decided belongs in a vehicle's config, not a plugin's
source. Option 1 also removes the real ambiguity option 2 would leave: whether `0.0` at some future
review is "a deliberately trimmed value" or "nobody configured this" is not decidable from the
value alone; option 1 makes "nobody configured this" a `ConfigError` at init, never a number anyone
has to interpret. A plugin's own `AeroInputs`-style standalone unit test (§7: "a user's model can be
unit-tested with hand-built inputs and no simulator") is unaffected — that test builds its inputs by
hand and never goes through `Vehicle::init()`/`InputRegistry` at all (PR 3's own line: "Hand-built-
`AeroInputs` unit testing without a simulator is preserved").

**Fold-in, approved before ADR write-up: every entry's storage (canonical, pending, and the
freeze()-time recorded value) initializes to quiet NaN, not an unspecified value.** `Simulation::
from_config`'s own init check (option 1 above) catches a never-written input before the run starts.
But `Simulation::for_testing` skips that check by design (R10) — if a `for_testing`-based test
simply forgets to `write()` one of its declared inputs, there is no init-time backstop at all. A
quiet-NaN default closes that gap without a second check: a forgotten `write()` leaves `pending`
at NaN, which the very first `latch()` (non-finite, per (c)) refuses to commit and reports through
`HealthMonitor::check_input`, naming that specific input — the same loud, named failure a runtime
NaN write already produces, not a silent read of undefined memory. NaN is therefore doing two jobs
with one value: "never written" and "written badly" are deliberately indistinguishable to `latch()`,
because both deserve the identical loud response.

**Rejected: option 2.** Reintroduces the silent-default ambiguity the PLAN.md line's own "never a
silent zero" language is written to prevent, and works against M3 PR 1's own established direction.

### (b) `reset()` semantics for written values

`Model::reset()` is an existing pure virtual (`model.hpp`) with no current kernel caller (verified:
no call site in `core/src/` or `core/include/` today) — a forward-looking hook every plugin already
implements (`plugins/physical/wind/dryden/dryden.cpp`'s own `reset()` reseeds its RNG and zeros every
mutable member, including several also registered as read signals — the established precedent for
"return to a construction-equivalent baseline").

Given (a)'s resolution removes any code-level default, a plugin has no value of its own to reset a
written input *to* — the only value with any standing is whatever config supplied at init. That
value is not private to the plugin; it is bookkeeping the seam itself holds.

**Recommendation.** `InputRegistry` records each entry's init-established value (the config literal
that satisfied (a)'s init check) and exposes `InputRegistry::reset() noexcept`, restoring every
entry's canonical *and* pending value to that recorded value. `Vehicle` gains a `reset()` method
(new; nothing calls it yet, matching `Model::reset()`'s own current status) that calls `inputs_.
reset()` and delegates to every `Model`'s own `reset()` in scheduler order, in either order. **Why
ordering doesn't matter, corrected a second time — second Codex critique (finding 2)**: round 2
claimed a `const double*` read-side member alone made a plugin's `reset()` unable to touch the seam
— true for the *read* pointer, but it overlooked that a plugin may *also* cache a **writable**
`InputHandle` (R3/R4 both rely on exactly this). Nothing previously stopped a plugin's own `reset()`
from calling `.write()` on such a cached handle, which *would* make the two orderings genuinely
different (see the Design section's own corrected account). Fixed with an explicit prohibition
instead of relying on the read-only pointer alone: a conforming `WrenchSource`'s `reset()`, like its
`wrench_b()`, must never write through a cached `InputHandle` — only `step()` is a sanctioned write
point (R4's `ActuatorModel` case), and `InputRegistry::reset()` alone owns restoring the seam's own
state. With that stated, ordering between `inputs_.reset()` and the per-model `reset()` pass is
genuinely irrelevant. Kernel-owned state gets kernel-owned reset semantics, the same layering
`Vehicle::state_`/`SignalRegistry` already use, rather than asking each plugin to remember a value it
no longer holds. This PR defines and unit-tests the contract; wiring an end-to-end reset flow (M5's
Monte Carlo driver) is out of scope here.

**Rejected: plugin-owned reset** (each plugin's own `reset()` resets its own bound members, matching
`dryden.cpp`'s pattern literally). Rejected: after (a), the "right" value is a config fact the
plugin does not hold a reference to; after (h), the plugin's own *read-side* member is a `const
double*` it has no write access to at all — and the write-side `InputHandle` a plugin might
separately cache is now explicitly prohibited from being used this way. Not buildable as written,
and not permitted even via the one channel that technically could.

### (c) `HealthMonitor`: a non-finite written input halts naming that input

**Design.** A new `HealthCheck::kNonFiniteInput` enum value (`health_monitor.hpp`'s existing enum:
`kNone, kNonFiniteState, kNonFiniteWrench, kQuaternionNorm, kOutOfDomain` gains a fifth). A new
`HealthMonitor::check_input(std::string_view source, const StepContext&, std::uint32_t vehicle_index)
noexcept` mirrors `report_out_of_domain`'s existing shape exactly: the *predicate* (a non-finite
pending value) is evaluated by the caller — `InputRegistry::latch()` itself, which is where the value
first becomes observable — and `HealthMonitor` only turns an already-decided fault into the
configured action, count, and signal (`report_out_of_domain`'s own doc comment: "the caller has
already decided the predicate failed... this call only turns that into the configured action, the
count, and the signal, exactly like `apply()` does for the other three checks"). A new signal
`health.input` and a new `sim.health` config key `"input"` (default `"halt"`, matching every other
check's conservative default).

**Why at `latch()`, not at `write()`.** `write()` may be called from three different callers over the
project's life (config-literal application at init, Python's `sim.write_input` between frames, M4's
raw shared-memory transport later) with different validation costs available to each; `latch()` is
the one place every writer's value is guaranteed to pass through before `wrench_b()` ever sees it,
so it is the single point that can name *the input*, not whichever `WrenchSource` happens to consume
it. On `warn` or `halt` alike, `latch()` does **not** copy a non-finite pending value into canonical
storage — the value every plugin holding this entry's `const double*` reads keeps its last
successfully latched value (h: the pointer is shared, registry-owned storage, not a per-plugin
member — the value it reads through *is* the canonical value). **Correction from Codex's plan
critique**: this is a **new** invariant this PR introduces for `InputRegistry` specifically, not an
existing precedent it merely follows. `HealthMonitor::apply()` (`health_monitor.cpp:39–50`) returns
an empty, falsy `HaltReason` under `warn` unconditionally — `Vehicle::integrate()`'s own wrench-fault
revert (`vehicle.cpp:178–184`, `state_ = pre_step_state`) only fires when `wrench_fault` is truthy,
which never happens under `warn` (`check_wrench` always returns falsy there too). So a `warn`-mode
wrench fault today runs on with the bad wrench, uncorrected — ADR-0016's revert protects only a
*halting* fault's state, not a warned one. `InputRegistry::latch()`'s "never commit a bad pending
value, warn or halt" behavior has no existing analog; it is simply the right invariant for *this*
seam (a control input has an obvious "last good value" to fall back to in a way a wrench total does
not), stated and tested on its own terms.

**Gap found by Codex's plan critique, fixed here.** As originally drafted, (a)'s init check
(`first_unwritten()`) only asked "did *anything* call `write()`," not "was the written value
finite" — a first config-literal write of `.nan` would satisfy that check, leave the entry with no
real "last good value" at all (the plugin's own member still holds whatever its in-class default
happens to be, not a value this seam ever validated), and `InputRegistry::reset()` (b) would then
copy that same recorded-but-invalid value back on every future reset. This contradicts
`docs/architecture.md` §15's own pre-run doctrine ("no NaN/Inf anywhere in state or parameters").
Fixed: the init-time check (renamed `first_unwritten_or_non_finite()`) validates finiteness too,
throwing the same `ConfigError` shape as (a)'s unwritten case when a config literal itself is
non-finite — an init-time failure, not a runtime `health.input` one, consistent with §15's
"pre-run (init), all halt by default" checks catching a bad *parameter*, and the runtime
`check_input` catching a bad *value written during the run* (Python, M4) instead.

**Partial-latch behavior on a runtime fault — stated explicitly per Joe's review round 1 (finding
6).** `latch()` processes **every** registered entry uniformly in one call, never short-circuiting
on the first non-finite one — the identical reasoning ADR-0016 already applies to `check_wrench`'s
own per-stage evaluation ("a fault-dependent short-circuit was considered and rejected: it behaves
inconsistently between `warn`/`halt` actions"). Concretely: every entry whose pending value is
finite is committed to its canonical storage in this same `latch()` call, regardless of whether some
*other*, unrelated entry was non-finite — one bad input never withholds every other input's
legitimate write that frame. `latch()` returns only the **first** non-finite entry's name
(first-fault-wins, the same pattern `Vehicle::integrate()`'s own `wrench_fault` already uses), and
`Simulation::step` calls `HealthMonitor::check_input` **at most once per vehicle per frame** with
that name — matching `report_out_of_domain`'s own call shape (at most one call per vehicle per
frame), not `check_wrench`'s (up to several calls per frame): `latch()` itself is the one place per
frame this check needs to run, not once per entry. Under `warn`, every finite entry this frame is
still committed normally; the one non-finite entry's canonical value is left untouched (its last
good value), and the run continues.

### (d) Naming: every writable input also readable as `v0.*`; how the two registries' `add()` relate

**Recommendation, updated for (h)'s registry-owned-storage decision.** `InputRegistry::add(registry,
name, unit)` takes the vehicle's `SignalRegistry&` as an explicit **parameter**, not a stored member
(see the correction below), returns a `const double*` (no `target` parameter — (h)), and — **only on
the first `add()` call for a given name** — forwards that same pointer into the read registry's own
`add(name, pointer, unit)`, visible under `"v0.controls.elevator"` automatically. A second `add()`
call for an already-registered name (e.g. PR 4's second `turbofan` instance, (h)) does not call the
read registry's `add()` again (which would itself throw a duplicate-name `Error`,
`signal_registry.cpp:25–30`, confirmed directly) — it returns the existing pointer, after checking
the requested `unit` matches the first registration's (a mismatched unit under one name is a real
bug, reported as an `Error` naming both callers' units).

**Correction from Codex's plan critique.** The original draft had `InputRegistry` store a
`SignalRegistry&` member, constructed against "this vehicle's own `SignalRegistry`" — that phrase
describes something that does not exist. `SignalRegistry` is a single, by-value member of
`Simulation` (`simulation.hpp:150`), shared by every vehicle and every environment model; ADR-0011
grants each `Vehicle` its own signal *namespace* (via `PrefixScope`), never its own registry
*object*. A stored reference would also outlive its safe lifetime incorrectly: `Simulation::
from_config`/`for_testing` move each `Vehicle` into `vehicles_` after `init()` runs, and `Simulation`
itself is a returned-by-value, movable object — a persisted `SignalRegistry&` member is exactly the
dangling-reference hazard `Vehicle::published_wrench_` was already changed to a `unique_ptr` to avoid
(ADR-0016's own plan critique, `vehicle.hpp`). Fixed by not storing the reference at all: `add()`
receives `SignalRegistry&` transiently, for the duration of one call, matching how `Model::init()`
itself already receives it fresh each time rather than caching it.

**Correction from Joe's review round 1 (finding 1).** The original draft assumed `bind_inputs()`
runs inside `Vehicle::init()`'s existing `SignalRegistry::PrefixScope` — false. Read directly,
`vehicle.cpp:21–24`: the scope is opened in a nested block that closes immediately after `loaded.
model->init(spec.config, registry);` returns, *before* the `dynamic_cast<WrenchSource*>` check
(currently line ~30) that would discover the model and could call `bind_inputs()` on it. Calling
`bind_inputs()` at that later point, as originally drafted, would register every write-seam name
**unprefixed** (`"controls.elevator"`, not `"v0.controls.elevator"`) — silently breaking the
`v0.*`-visibility acceptance criterion for every vehicle, not just a multi-vehicle one. Fixed: the
`WrenchSource` discovery (the `dynamic_cast`) moves *inside* the same nested block, immediately after
`init()`, so `bind_inputs()` runs while the `PrefixScope` is still open:

```cpp
{
  const SignalRegistry::PrefixScope scope(registry, name_);
  loaded.model->init(spec.config, registry);
  if (auto* const wrench_source = dynamic_cast<WrenchSource*>(loaded.model.get());
      wrench_source != nullptr) {
    wrench_source->bind_inputs(inputs_, registry);   // still inside scope -- correctly prefixed
  }
}
scheduler_.add(*loaded.model, spec.divisor);
if (auto* const wrench_source = dynamic_cast<WrenchSource*>(loaded.model.get());
    wrench_source != nullptr) {
  // requires_environment() check, unchanged -- doesn't touch the registry's prefix, so its
  // existing position (outside the scope) is unaffected.
}
```

The existing `requires_environment()` check stays where it is (it never calls `registry.add()`, so
its position relative to the scope was never load-bearing); only `bind_inputs()`'s new call needed
to move inside.

**Rejected: two independent `add()` calls** (the plugin author calls both `registry.add(...)` and
`inputs.add(...)` with the same name). Rejected: doubles the boilerplate and creates a real risk of
the two silently drifting (different pointer, different unit string) with no mechanism to catch it —
and would not, by itself, have avoided (h)'s duplicate-name problem either.

### (e) Who may write and when — the ordering rule

**Options.**
1. *`latch()` before `vehicle.step(ctx)`* (the original draft). Simple, but — **found by Joe's
   review round 1** — bakes a one-frame delay into any `Model` that writes through the seam from its
   own `step()`: a future M4 `ActuatorModel` computing this frame's achieved surface position inside
   `step()` would have that write land in `pending`, committed only by *next* frame's `latch()` —
   `docs/architecture.md` §5's own words are the exact warning this design would walk into: "model
   ordering is a classic source of one-frame delays that look like physics."
2. *`latch()` after `vehicle.step(ctx)`, before `vehicle.integrate(...)`.* A write issued from
   *within* this frame's `vehicle.step(ctx)` (any scheduled `Model::step()`, including a future
   `ActuatorModel`) is committed by *this same frame's* `latch()`, which now runs strictly after.

**Recommendation: option 2.** It matches §5's own already-documented canonical order more closely
than option 1 does: "...transport exchange if due (send truth/sensor frame, **receive commands**) →
integrator step..." places command-receipt strictly before wrench evaluation, exactly where a
step()-time writer's commit now lands. Concretely, in `core/src/kernel/simulation.cpp`'s existing
per-vehicle loop, one new call is inserted between the existing `vehicle.step(ctx);` (line 522) and
`vehicle.integrate(...)` (line 523) — not before `vehicle.step(ctx)` as originally drafted.

This single rule covers all three writer kinds without this PR building the latter two:
- **Config literals (this PR).** Applied via `write()` once, at the end of `Vehicle::init()`
  (equivalent to (a)'s "config literal satisfied it" check) — already in `pending` before frame 0's
  first `latch()` runs (which, under option 2, still runs before `vehicle.integrate()` on frame 0
  too), so no special-casing is needed; `latch()` just copies whatever the last `write()` set.
- **Python (PR 5).** `sim.write_input(name, value)` calls `write()` at any point between two `sim.
  step()` C++ calls. Because Python and C++ run strictly synchronously here (§14: "the scheduler is
  single-threaded... models are never called concurrently"), and because frame *N*'s own `latch()`
  has already run (and already committed) by the time `sim.step()` returns for frame *N*, a write
  issued after that return always lands in `pending` too late for frame *N* and is committed only by
  frame *N+1*'s own `latch()` — "not observed until next frame" holds by construction here.
- **A step()-time writer (this PR's own R4 test fixture, `input_consumer`, simulating a future M4
  `ActuatorModel`).** Writes through a resolved `InputHandle` (see "Design" below) from inside its
  own `Model::step()`. Under option 2, that write is committed by *this same frame's* `latch()`,
  observed by every one of this frame's four `wrench_b()` calls — no one-frame lag, exactly the
  property option 1 lacked.
- **M4's transport, or a real `ActuatorModel` (later) — ordering position only, not a built or
  verified route.** *Softened per Joe's review round 2 (finding 5), which corrects an overclaim
  here*: a writer positioned at the "transport exchange" point §5 already names, ahead of `vehicle.
  step(ctx)`, would still be ahead of this frame's `latch()` under option 2, so *would* still commit
  this same frame *if it can reach `write()`/an `InputHandle` at all*. This PR does not build that
  writer, and — see the Design section's "Known gap: no seam route for a non-`WrenchSource` writer"
  — does not actually verify it *can* reach one: `bind_inputs()` is `WrenchSource`-only (g), and a
  transport/`ActuatorModel` implementation is not necessarily a `WrenchSource` itself. The ordering
  *position* is available; whether M4's own writer can occupy it is an open question for M4's own
  plan, not something this PR's design has built or proven.

### (f) ADR-0011: inputs are per-vehicle

`InputRegistry` is owned by `Vehicle` — one instance per vehicle. It is never a `Simulation`-level or
global object (unlike `SignalRegistry`, which stays `Simulation`-owned and shared — see (d)'s
correction above: this PR does not change that). PR 5's `sim.write_input(name, value)` would resolve
to `vehicles_[0]`'s own `InputRegistry`. **Correction from Codex's plan critique**: ADR-0011's text
grants its "v1 convenience picks `v0`" carve-out explicitly to "the CLI's convenience defaults"
(`0011-one-vehicle-as-list-of-one.md:15`) — extending that carve-out to `sim.write_input()` is a
**new** exception this PR (or, more precisely, PR 5, which actually builds that binding) must record
explicitly, not one ADR-0011 already grants. Acceptance includes a regression test (via `Simulation::
for_testing`, which already supports more than one vehicle) proving two vehicles get independent
`InputRegistry` instances: writing one vehicle's elevator must not affect the other's.

### (g) `kCoreApiVersion`: bump

**Recommendation: yes, bump** (4 → 5) — **redesigned after Codex's plan critique** (see the
correction under (d) and "Compatibility" above): the seam attaches to `WrenchSource`, not to the
common `Model::init()` every plugin (including shared environment models) implements.

**Corrected mechanism.** `WrenchSource` gains one new virtual method, `bind_inputs(InputRegistry&,
SignalRegistry&)`, with a default empty body — a `WrenchSource` that uses no writable input (e.g.
today's `nesc_aero`/`nan_wrench`) need not override it. `Vehicle::init()` already runs a dedicated
pass identifying every loaded model that is a `WrenchSource` (`vehicle.hpp`'s existing `WrenchEntry`
discovery, used today for `requires_environment()`); this PR's new call happens in that same pass,
once per `WrenchSource`, after that model's own `Model::init()` has already run. `Model::init()`'s
own signature is **unchanged** — no `EarthModel`/`GravityModel`/`AtmosphereModel`/`WindModel`/
`MassPropertiesModel` plugin is affected.

**Why this still bumps `kCoreApiVersion`.** Adding a new virtual method to `WrenchSource` changes
its vtable layout — an out-of-tree plugin built against version 4 has no slot for `bind_inputs` and
must be rejected at load, not dispatched into undefined behavior.

**Correction from Codex's plan critique on the precedent itself.** The original draft cited
versions 2 *and* 4 as both being about "changing an existing pure virtual's parameter list" — that
overstates version 4: `core_api.hpp.in:18–21`'s own comment says version 4 bumped because
`EarthModel`/`GravityModel` each gained a **new non-pure virtual** (`in_domain`), not because an
existing method's parameters changed. That is, in fact, the *closer* precedent for this PR's
corrected design (`bind_inputs` is also a new, non-mandatory-override virtual added to an existing
interface) — version 2's *other* half (`wrench_b`'s signature changing) is the precedent for a
parameter-list change specifically, which this corrected design no longer needs at all.

**Rejected alternative: merge write capability into the existing `SignalRegistry`** (no new type,
no bump). **Reasoning corrected from Codex's plan critique**: the original rejection claimed
`SignalRegistry` is "frozen after init and never mutated again" — false. `SignalRegistry::freeze()`
(`signal_registry.hpp:39–40`) closes *registration* (further `add()` calls) only; every registered
entry's pointed-to `double` keeps changing every frame for the run's entire duration (that is the
entire point of a *published* signal — `aero.CL`'s live value is read fresh on every log sample).
The real distinction is about *who* is allowed to write through the pointer, not whether the value
changes: every existing `SignalRegistry` entry is written **only by the model that registered it**
(a self-publish); this seam's entries are written by code **other than** the registering model
(config, Python, M4's transport). Merging the two into one class would make that "who may write
this" boundary invisible in the type system — a future maintainer reading `SignalRegistry::add()`
would have no way to tell, from the class alone, whether a given entry is self-published-only or
externally writable. Two small, single-purpose types keep that distinction structural. This also
still matches Zipfel's own CADAC++ Table/Variable/Module split (App. E.2.3, p.599: distinct classes
for distinct concerns).

### (h) Two instances of one plugin under one vehicle registering the same name

**Added after Joe's review round 1** — not resolved by any of (a)–(g) above, and a real problem: M3
PR 4's own line loads `hardware/propulsion/turbofan` twice under one vehicle (`station: left`,
`station: right`, `docs/architecture.md` §13's config example). If both instances declare a write-
seam input the natural way a plugin author would name it — `"controls.throttle"`, with no reason
either instance's own code would know to disambiguate — the original `add(registry, name, target,
unit)` design (one caller-owned `target` pointer per name) would throw `SignalRegistry`'s existing
duplicate-name `Error` on the second instance's `bind_inputs()` call
(`signal_registry.cpp:25–30`, confirmed directly).

**Options.**
1. *Per-instance names.* Each plugin instance disambiguates its own registered name using its own
   config (e.g. `"controls.throttle." + station`), giving two genuinely independent inputs
   (`controls.throttle.left`/`controls.throttle.right`). A writer wanting differential thrust
   addresses each separately; a writer wanting symmetric thrust must write both.
2. *`N` targets per name.* `add()` accepts the same name more than once, and `write()`/`latch()`
   copies one committed value into every target registered under that name (a broadcast).
3. *Registry-owned storage, plugins hold `const double*`.* Invert ownership: `InputRegistry` itself
   owns one canonical `double` per unique name (not a pointer into the caller's own member); `add()`
   returns a `const double*` into that storage for the plugin to read from (`wrench_b()` dereferences
   it), rather than taking a `double* target` parameter. A second `add()` call under an
   already-registered name (matching unit) is not a duplicate — it returns the *same* `const double*`
   to the second subscriber; the underlying `SignalRegistry::add()` forwarding happens only once, on
   the first `add()` for a given name (avoiding that duplicate too).

**Recommendation: option 3.** This is exactly the shape both cited textbook precedents already use.
S&L's own `COMMON/CONTROLS/THTL,EL,AIL,RDR` (§3.5, Fig. 3.5-2) is literally shared Fortran storage —
any code reading `THTL` reads the *same* memory, not a per-reader copy; there is no Fortran
equivalent of "duplicate declaration" for two subroutines that both read the same common block.
Zipfel's "global communication bus" (App. E.2.5, p.599: "each vehicle should be able to publish and
subscribe to any of the module-variables") is the identical shape: a module-variable is one named
slot; more than one module *subscribing* to it is the ordinary case, not an edge case needing special
handling. Option 3 also makes both left/right turbofans see one throttle command *by construction*
(symmetric control, the simplest sensible default for a first thrust-lapse model), while leaving
option 1's per-instance naming fully available to PR 4 later if it decides it needs differential
thrust — that is PR 4's own modeling decision, not one PR 2 should make for it; option 3's primitive
supports either choice without forcing it now.

**Rejected: option 1 as the default.** Forces every plugin author to invent and thread a
disambiguating suffix through every multiply-instanced plugin's own config, for a collision this
PR's own primitive can avoid structurally instead. Still available as an opt-in (a plugin can always
choose a name that already includes its own station/instance identifier) — this PR does not remove
that option, only stops requiring it.

**Rejected: option 2.** A separate `N`-copies-per-name mechanism duplicates what option 3's shared
storage already gives for free (one canonical value, many readers), while adding a real correctness
risk option 3 does not have: if `latch()`'s copy-into-`N`-targets loop were ever partially applied
(e.g. interrupted between copies, or a future refactor got the loop wrong), the `N` targets could
observably disagree even though they are supposed to represent "the same" input — impossible under
option 3, where there is only ever one storage location to begin with.

**Consequence for (d).** `InputRegistry::add()`'s signature changes: `add(registry, name, unit) ->
const double*`, no `double* target` parameter. See the redesigned "Design" section below.

## Repository findings

**Observed facts below are from a direct, first-hand read of this repository this session** (`Read`/
`Grep`/`Bash` against the actual files, cited by path/symbol/line throughout this document) — not
from a subagent. An earlier `Agent` fork launched mid-session for a "codebase interfaces survey" was
stopped (`TaskStop`) before it returned any result, once `docs/process/workflow.md`'s "Model and
effort" section ("No recon subagents for this repository") was re-read and the conflict caught; its
output was discarded and none of it informs any claim in this document, including `Simulation::step`'s
per-frame order, `SignalRegistry`'s `add`/`freeze`/`PrefixScope` shape, `HealthMonitor`'s check kinds,
and `kCoreApiVersion`'s bump history, all re-derived directly below.

- `core/include/astra/signals/signal_registry.hpp` — the read-side precedent this PR's `InputRegistry`
  mirrors directly: `add(name, value_ptr, unit, divisor)` (init-time, may throw on empty name/null
  value/zero divisor/duplicate/frozen), `find(name)` (init-time-or-test use only — "the per-frame
  path holds the pointer it already resolved and never calls this"), `freeze()`, and the RAII
  `PrefixScope` that namespaces every `add()` call while alive.
- `core/include/astra/interfaces/wrench_source.hpp` — `wrench_b(const VehicleState&, const
  EnvironmentSample&) const noexcept`, unchanged by this PR. Its doc comment (line 28) states a
  *stronger* purity contract than this PR's design actually satisfies as written today ("may depend
  only on the arguments given, never on how many times or in what order it has previously been
  called this frame or any other") — see "Invariants preserved" above: this PR must amend that
  comment, not merely cite it as already permitting the seam.
- `core/include/astra/interfaces/wind_model.hpp` and ADR-0016 — the direct architectural precedent
  for this PR's `latch()`: `WindModel::begin_frame(ctx, vs, altitude_m)` (non-const, kernel-driven,
  called once per vehicle per frame by `Simulation::step`) evolves state; `wind_n(ctx, vs) const
  noexcept` is a pure read of whatever `begin_frame` last latched. This PR's `write()`/`latch()`
  split is the identical shape: `write()` stages a pending value (any writer, any time), `latch()`
  (kernel-driven, once per vehicle per frame) commits it into the canonical storage `wrench_b()`
  reads through its own `const double*` (h).
- `core/include/astra/kernel/health_monitor.hpp` / `health_monitor.cpp` — `HealthCheck` enum,
  `HaltReason` struct. **Correction from Codex's plan critique**: only `report_out_of_domain`
  evaluates a caller-supplied predicate (`report_out_of_domain`'s own doc comment, confirmed against
  `health_monitor.cpp`); `check_wrench` and `check_state` evaluate their own predicates internally
  (`wrench.force_b.allFinite()`, `p_n.allFinite()`, `health_monitor.cpp:75–95`) — the original
  "all three share this shape" claim was wrong. (c)'s new `check_input` correctly mirrors
  `report_out_of_domain` specifically, which is the one existing method whose shape it actually
  matches; only the supporting prose here overstated the generalization, not the design choice.
- `core/src/kernel/simulation.cpp:492–554` (`Simulation::step`) — the actual per-frame order as
  coded: `environment_scheduler_.step(ctx)` → per vehicle: sample wind/atmosphere → `vehicle.
  set_environment(sample)` → `vehicle.step(ctx)` → `vehicle.integrate(...)` (wrench × 4 RK4 stages +
  EOM + integrator + post-step) → ADR-0012's post-step domain check → `clock_.advance()`. This PR's
  one new call lands between `set_environment` and `vehicle.step`.
- `core/include/astra/kernel/vehicle.hpp` — `Vehicle::init()`'s existing `ConfigError`-on-unmet-
  `requires_environment()` check (the precedent (a)'s init error reuses), and its existing dedicated
  discovery pass over every loaded model to find the `WrenchSource`s (used today for `requires_
  environment()`) — the pass (g)'s new `bind_inputs()` call joins, not a change to `Model::init()`
  itself. `Model::reset()`'s current, uncalled status (the precedent (b) builds on).
- `core/include/astra/kernel/simulation.hpp:150` (`SignalRegistry registry_;`, a plain by-value
  member of `Simulation`) and `core/src/kernel/simulation.cpp:71`'s `load_environment_model`
  (`loaded.model->init(spec.config, registry)`, called with **no** `PrefixScope` open, "earth and
  gravity are shared across vehicles... their signals publish unprefixed") — the two facts,
  confirmed directly, behind (d)'s and (g)'s corrections: `Vehicle` does not own a `SignalRegistry`,
  and `Model::init()` is called for shared, non-`WrenchSource` models entirely outside any vehicle.
- `core/include/astra/core_api.hpp.in` — `kCoreApiVersion = 4` and its documented bump history,
  cited directly in (g).
- `core/include/astra/config/config.hpp` — `number(key)`/`number(key, default_value)`'s two-arity
  precedent, considered and rejected as a model for (a) (see "Rejected: option 2" above).
- `docs/architecture.md` §13's config example (`aircraft/notional_transport/configs/cruise.yaml`) —
  no existing `vehicle.initial_controls`-shaped schema; this PR's config-literal writer needs a small,
  new-but-unsurprising schema addition (see Design below), not a new config subsystem.
- Test-fixture convention: `tests/plugins/{wind_consumer,atmosphere_consumer,nan_wrench,
  counting_wind}` — small, purpose-built `Model`/`WrenchSource` fixtures used only by unit tests.
  This PR proposes one more in the same shape: `tests/plugins/input_consumer`. The exact
  instrumentation precedent, read directly (`tests/plugins/nan_wrench/nan_wrench.cpp`): `mutable
  std::uint32_t call_count_`, incremented inside `wrench_b() const noexcept`, reset to `0` once per
  frame inside `step()` (called by `Vehicle::step()` before `Vehicle::integrate()` ever calls
  `wrench_b()`) — the "test instrumentation only" exception `nan_wrench`'s own header comment
  documents for a `const wrench_b()`. **Correction from Codex's plan critique**: the plan originally
  cited "`nesc_aero`'s existing mutable-instrumentation pattern," but `nesc_aero`'s own mutable
  self-publish was **removed** by ADR-0016 (`0016-per-rk4-substage-wrench-evaluation.md:57`,
  "Removing `nesc_aero`'s own mutable, per-call self-publish...") — that citation was stale, checking
  a fact true only of an earlier draft of a different, now-merged PR's plan, not of the current
  tree. `nan_wrench`'s pattern above, read directly this round, is the real, current precedent. Also
  corrected: `wrench_b()` is called exactly **four** times per frame (once per RK4 sub-stage); a
  `HealthMonitor` check consumes the wrench `wrench_b()` already returned (`vehicle.cpp:142–158`) and
  does not call `wrench_b()` again, so there is no "four or five" — always four.
- `tests/physics/no_alloc_test.cpp` + `tests/configs/no_alloc_*.yaml` — the established pattern for
  the "zero allocation over 1000 frames" acceptance criterion: one dedicated config, one dedicated
  `TEST(NoAlloc, ...)`.
- `tests/unit/signals/signal_registry_test.cpp` — the established pattern for a new `tests/unit/
  signals/input_registry_test.cpp`.

## Design: `InputRegistry`

**Redesigned after Codex's plan critique and Joe's review round 1** (see (d), (g), and (h) above):
`InputRegistry` stores no `SignalRegistry&`; the seam attaches to `WrenchSource` via one new
virtual, not to `Model::init()`; storage is registry-owned, not caller-owned (h); `find()`/
`InputHandle` are fully defined, not deferred (Joe's finding 6).

```cpp
// core/include/astra/signals/input_registry.hpp — SignalRegistry's mutable counterpart.
namespace astra {

// A resolved, repeatable-write handle into one InputRegistry entry -- mirrors SignalRegistry::
// find()'s own "resolve once at init, the per-frame path holds the pointer and never calls find()
// again" contract, but for writing. Resolved once (typically during bind_inputs()) and called
// repeatedly later -- from inside wrench_b() itself (R3's own test, proving a mid-integrate write
// is deferred to next frame) or from a model's own step() (R4's own test, simulating a future M4
// ActuatorModel, proving a step()-time write is NOT deferred) -- since neither wrench_b() nor
// Model::step()'s signature carries an InputRegistry reference of its own; see (e). Trivially
// copyable, no allocation, no std::string (same shape as HaltReason).
class InputHandle {
 public:
  [[nodiscard]] explicit operator bool() const noexcept { return pending_ != nullptr; }
  // Stages this entry's pending value -- noexcept, since resolution (name lookup, the only
  // failure mode) already happened in find(). const: mutates *pending_ (the pointee), not
  // pending_ itself, so this is callable from inside a const method -- specifically
  // WrenchSource::wrench_b() const noexcept, which R3's own test calls it from (a plugin's cached
  // InputHandle member is accessed as const from within a const method; write() being const is
  // what makes that compile). Per-frame path: no allocation, no exceptions.
  void write(double value) const noexcept;

 private:
  double* pending_ = nullptr;  // into the owning InputRegistry's own stable storage -- see below.
  friend class InputRegistry;
};

class InputRegistry {
 public:
  // No constructor parameters -- owned one-per-Vehicle (f), default-constructed alongside it.

  // Registers name as writable (h: registry-owned storage) and returns a const double* into this
  // registry's own canonical storage for name -- the plugin stores this pointer as its own member
  // and dereferences it inside wrench_b(). Forwards to registry.add(name, pointer, unit) (d), but
  // ONLY on the first add() call for a given name -- registry is a parameter, not a stored
  // reference (Codex's plan critique: Simulation, not Vehicle, owns the one shared SignalRegistry,
  // itself a movable, returned-by-value object). A second add() for an already-registered name (h:
  // two instances of one plugin) returns the SAME pointer without a second registry.add() call
  // (which would itself throw a duplicate-name Error); throws Error if the requested unit does not
  // match the first registration's. No default_value parameter (see (a)): the entry has no
  // meaningful value until something write()s it -- see "Why the canonical value is never read
  // uninitialized" below. Init-time only: may throw.
  [[nodiscard]] const double* add(SignalRegistry& registry, std::string_view name,
                                  std::string_view unit);

  // Stages value as name's pending value; has no effect on the canonical value until the next
  // latch() (see (e)). Throws Error naming name if not registered. Not itself on the per-frame
  // path -- called by config-literal application at init, or (PR 5) Python between sim.step() calls.
  void write(std::string_view name, double value);

  // Resolves name to a stable InputHandle for a caller that will write repeatedly without
  // re-resolving by name each time -- see InputHandle above. Returns a handle that converts to
  // false if name is not registered. Init-time (or equivalent early-hook) use only; the per-frame
  // path holds the handle it already resolved and never calls find() again.
  [[nodiscard]] InputHandle find(std::string_view name) noexcept;

  // Vehicle::init()'s own check for (a)/(c): the dotted name of the first registered entry with no
  // value yet, or whose only written value is non-finite, or empty if every entry has a real,
  // finite value. Called once, after every model's init()/bind_inputs() and every config-literal
  // write() has run, before registry freeze. Inspects each entry's PENDING value, never its
  // canonical value -- see below for why that distinction matters.
  [[nodiscard]] std::string_view first_unwritten_or_non_finite() const noexcept;

  // Copies every entry's pending value into its canonical storage -- the single per-vehicle-per-
  // frame commit point (e). Processes every entry uniformly, never short-circuiting (see (c)'s
  // "partial-latch behavior" -- ADR-0016's own precedent for check_wrench). Returns the name of the
  // FIRST entry whose pending value was non-finite, or empty if none; that one entry's canonical
  // value is left at its last good value, every OTHER entry's finite pending value is still
  // committed this same call, warn or halt alike (a new invariant -- see (c)). Per-frame path: no
  // allocation, no exceptions.
  [[nodiscard]] std::string_view latch() noexcept;

  // (b): restores every entry's canonical and pending value to the value recorded when freeze()
  // last ran, i.e. the config-literal value each entry held at that point -- or, for a vehicle
  // built via Simulation::for_testing, which never calls freeze() at all, back to that field's own
  // NaN default: an entry that was never frozen has no recorded "good" value to restore, so
  // reset() correctly returns it to the same "unwritten" NaN state it started in, not to whatever a
  // test happened to write() before calling reset(). Called by Vehicle::reset() alongside every
  // model's own reset(); relative order does not matter given the reset()/InputHandle prohibition
  // below -- see "Vehicle::reset() ordering" below.
  void reset() noexcept;

  // Closes registration (matching SignalRegistry::freeze()'s own shape) AND seeds every entry's
  // canonical storage from its already-validated pending value, right here, at init time. This is
  // the ONE unconditional commit that establishes a real "last good value" before Simulation::
  // from_config ever returns, independent of whichever per-frame latch() happens to run first --
  // see "Why the canonical value is never read uninitialized" below. Simulation::for_testing never
  // calls this (R10); its vehicles' entries stay at their NaN default until a test's own write()
  // -- and, per (a)'s fold-in, a forgotten write() then fails loudly at the first latch(), naming
  // the input, rather than reading undefined memory.
  void freeze() noexcept;

 private:
  // Entry: name, unit, canonical double, pending double, "value recorded at freeze()" double.
  // Every one of these four fields default-initializes to a quiet NaN, not an unspecified value
  // (fold-in, approved before ADR write-up) -- "never written" and "written non-finite" are
  // deliberately the same, loudly-reported state to latch(), never silent undefined memory. Held
  // in a std::deque<Entry> (or an equivalent per-element-stable container), NOT a
  // std::vector<Entry> -- see "Why entries survive a Vehicle move" below. No SignalRegistry&
  // member.
};

}  // namespace astra
```

```cpp
// core/include/astra/interfaces/wrench_source.hpp -- one new virtual, default no-op.
class WrenchSource : public Model {
 public:
  // wrench_b(), requires_environment() unchanged in signature. Doc comment amended (see "Invariants
  // preserved"): may read any input through its own const double*; MUST NEVER write through an
  // InputHandle from inside wrench_b() OR reset(). tests/plugins/input_consumer (R3's fixture) is
  // the one documented, test-only exception, from wrench_b() only -- mirroring nan_wrench's own
  // mutable call_count_ exception. step() is the one sanctioned write point (R4's own case).

  // Called once per WrenchSource, by Vehicle::init()'s existing WrenchSource-discovery pass (the
  // same pass that already checks requires_environment() today), after this model's own
  // Model::init() has already run, and still inside the vehicle's SignalRegistry::PrefixScope --
  // see (d)'s finding-1 correction for exactly where. Registers this model's writable inputs, e.g.
  // elevator_ = inputs.add(registry, "controls.elevator", "rad"); (h: elevator_ is now a
  // `const double*`, not a `double`). Default: does nothing -- a WrenchSource with no writable
  // input (nesc_aero, nan_wrench) need not override this. Init-time only: may throw. Model::init()'s
  // own signature is unchanged.
  virtual void bind_inputs(InputRegistry& inputs, SignalRegistry& registry);
};
```

`Vehicle` gains one `InputRegistry inputs_` member. Its existing `WrenchSource`-discovery pass calls
`wrench_source->bind_inputs(inputs_, registry)` for each discovered `WrenchSource`, still inside the
`PrefixScope` (d). `Vehicle::init()`'s job ends there for the seam — it registers entries, nothing
more.

**Corrected — second Codex critique (finding 3): the validation/seeding sequence below does not
run inside `Vehicle::init()`.** The original text here said it did; that is impossible given `Vehicle
::init()`'s actual signature (`vehicle.hpp:96`, confirmed directly), which takes a `ModelSpec` list
and never the vehicle's own config subtree — it has no way to read `vehicle.initial_controls` (e) at
all, and `Simulation::for_testing` calls this exact same `Vehicle::init()` in both its branches
(`simulation.cpp:469–478`), so any check placed inside it would incorrectly also fire for
`for_testing`, contradicting R10's own test design (which needs `for_testing` to return with
unwritten inputs so the test can `write()` them itself afterward). Fixed: `Simulation::from_config`
— which does hold the full config — applies each declared input's `vehicle.initial_controls` literal
(via `write()`) after `vehicle.init()` returns, then calls `inputs_.first_unwritten_or_non_finite()`
(throwing `ConfigError` naming any entry that still fails it), then calls `inputs_.freeze()` (which
also seeds canonical storage — see below), all before moving the vehicle into `vehicles_`.
`Simulation::for_testing` calls none of these three steps, matching its own existing, documented
list of `from_config`-only validations it skips (R10's own account) — a `for_testing`-based test
supplies every declared input's value itself, by calling `write()` directly, and relies on that
vehicle's first `latch()` (inside its first `sim.step()`) to seed canonical storage the ordinary way,
since `freeze()`'s own one-time seed never ran for it either.

`Simulation::step` calls `inputs_.latch()` at the point (e) names (after `vehicle.step(ctx)`, before
`vehicle.integrate(...)`).

**Known gap, disclosed not fixed — Joe's review round 2 (finding 5).** `bind_inputs()` is a
`WrenchSource`-only virtual (g), given to whichever model *owns* (registers) a writable input. It is
**not** a general route for an arbitrary `Model` to obtain a *write* handle into an input some other
model owns. A future M4 `ActuatorModel` that is itself a `WrenchSource` owning its own inputs is
fine; one that is **not** a `WrenchSource` — plausible, given architecture.md §7's own description
("`ActuatorModel` slots in between later without changing `AeroInputs`," i.e. it transforms a
command into an achieved position `AeroModel` then reads as *its own* `controls.elevator` input, not
necessarily a wrench contributor in its own right) — has **no seam-provided way to reach `find()`/
an `InputHandle` for a name it does not itself own** under this PR's design. Even setting that aside,
a *second*, independent problem exists: `find()` only succeeds once the *owning* model's own
`bind_inputs()` has already run, and nothing here validates or enforces `vehicle.models`' load order
— a config that happens to list a hypothetical writer before the model owning the name it wants to
write would silently get an invalid (`false`-converting) `InputHandle` back, discovered only were
someone to check it. Both are the same class of gap ADR-0012/ADR-0016 already carry elsewhere in
this codebase for a different reason each (a domain a single frame can jump across; an atmosphere-
domain misattribution window) — disclosed, not engineered around, because M4 does not exist yet and
this PR's own acceptance evidence (a `WrenchSource`-only test fixture) cannot exercise either shape
either way. Tracked as an M4-scope open question, not solved here; see the Draft ADR's own
"Consequences and validation" for the same disclosure recorded against the accepted record.

**Why the canonical value is never read uninitialized — corrected, second Codex critique (finding
1), tightened by the NaN-sentinel fold-in.** Every entry's canonical, pending, and freeze()-recorded
storage default-initializes to a quiet NaN, never an unspecified value (the fold-in above). "Never
read uninitialized" is therefore true by construction from the moment an entry is created — the real
question this paragraph answers is why a plugin reading canonical storage never silently gets a
*meaningless-but-defined* NaN either, in the ordinary case.

**The original reasoning here was wrong.** It claimed (e)'s ordering rule alone was enough: `latch()`
running before `vehicle.integrate()` on frame 0 would commit the config literal's validated value
before the first `wrench_b()` call. That overlooks something R4 itself establishes as sanctioned
behavior: a step()-time writer (a future `ActuatorModel`, or R4's own fixture) can write through a
cached `InputHandle` *during* `vehicle.step(ctx)` — which, under (e)'s ordering, runs **before**
`latch()`, on every frame including frame 0. If that write overwrites `pending` with a non-finite
value before frame 0's own `latch()` ever fires, `latch()` correctly refuses to commit it (c) — but
under `warn`, the run continues into `vehicle.integrate()` anyway, and canonical storage — before
the fix below — had never been committed to *at all* yet, config literal or otherwise: `wrench_b()`
would read a value nobody had validated for this run, indistinguishable from a stale NaN.

**Fixed**: `Simulation::from_config` calls `InputRegistry::freeze()` once per vehicle, after
applying that vehicle's config literals and after `first_unwritten_or_non_finite()` passes, seeding
every entry's canonical storage directly from its already-validated pending value — before that
vehicle is moved into `vehicles_`. This is a *separate, one-time, init-time commit*, not the
ordinary per-frame `latch()`, so canonical storage already holds a real, config-validated value the
moment the `Simulation` exists, regardless of what any step()-time writer does on frame 0 or any
later frame. A step()-time writer feeding `latch()` a non-finite pending value on any frame
thereafter falls back to that genuine last-good value (the `freeze()`-time seed, or whatever the
most recent successful `latch()` committed since), not to an unvalidated one.

`Simulation::for_testing` never calls `freeze()` (R10) — its vehicles' entries keep their NaN
default until a test's own `write()`. A test that forgets one does not read undefined memory (the
NaN default already prevents that) and does not silently proceed on a meaningless zero either: the
first `latch()` finds that entry's `pending` still NaN, refuses to commit it, and reports through
`HealthMonitor::check_input`, naming the specific input — the identical, loud failure a genuine
runtime NaN write already produces.

**Why entries survive a Vehicle move — Joe's review round 1 (finding 5), last sentence corrected in
round 2 (finding 4).** A test (or PR 5's Python binding, or M4's transport) needs a stable `const
double*`/`InputHandle` that remains valid after `Simulation::from_config`/`for_testing` moves the
local `Vehicle` into `vehicles_` (`simulation.cpp:379`/`478`, confirmed directly) — the same class of
hazard that already made `Vehicle::published_wrench_` a `unique_ptr<Wrench>` rather than a plain
`Wrench` member. `InputRegistry` does **not** need the same `unique_ptr` treatment, for a different
reason: storing entries in a `std::deque<Entry>` (or `std::vector<std::unique_ptr<Entry>>`) gives
every entry its own, separately-allocated storage whose address is untouched by either (i) the
*owning* `InputRegistry`'s own address changing (a `Vehicle` move relocates `inputs_` itself, but
not the heap-allocated chunks a `deque` already references indirectly) or (ii) a *later* `add()`/
`bind_inputs()` call for a different input growing the container (a `deque` never invalidates
existing element addresses on growth, unlike a `std::vector<Entry>` stored inline, which would).

**Correction — Joe's review round 2 (finding 4).** The previous draft's closing sentence here said
the opposite of what the reasoning above actually supports, and directly contradicted R4's own test
design (which requires exactly the thing that sentence said not to do). Corrected: a `const double*`
(from `add()`) or an `InputHandle` (from `find()`) is a pointer into the *entry's own* stable storage
(inside the `deque`), not into `InputRegistry` itself — it is safe to obtain **and cache** during
`bind_inputs()`/`init()`, and every plugin using this seam is *expected* to do exactly that (R4's own
fixture caches an `InputHandle` during `bind_inputs()` and calls it later from `wrench_b()`/`step()`
— see the next section). What is **not** safe to cache across a `Vehicle` move is a raw `InputRegistry&`
(or `Vehicle&`) itself — a reference to the *container object*, whose own address does change when
its owning `Vehicle` moves. The distinction is "pointer into stable per-element storage" (safe, and
the whole point of this design) versus "reference to the moving registry/vehicle object itself"
(unsafe) — not "anything obtained during `bind_inputs()`" versus "anything obtained later," which is
what the retracted sentence wrongly implied.

**`Vehicle::reset()` ordering — round 2's "no longer matters" claim was incomplete; fixed with a new
prohibition, second Codex critique (finding 2).** An earlier draft (itself a fix for Codex's *first*
plan critique) reasoned that `Vehicle::reset()` must call every model's own `reset()` *before*
`inputs_.reset()`, to prevent a plugin's own `reset()` — following `dryden.cpp:151`'s "zero every
mutable member" precedent — from silently overwriting the value `inputs_.reset()` had just restored.
Round 2 declared this obsolete, reasoning that a `const double*` read-side member gives a plugin's
`reset()` no mechanism to write to it. **That reasoning only considered the read side.** It missed
that R3/R4 already establish a plugin may *also* cache a **writable** `InputHandle` (from `find()`),
and nothing stated anywhere prohibited a plugin's own `reset()` from calling `.write()` on it — if
`inputs_.reset()` runs first, a plugin's `reset()` could restage a new `pending` value right after,
which the *next* `latch()` would commit, silently discarding the restore; reversing the order instead
discards *that* write. The two orders are **not** equivalent after all.

**Fixed with an explicit prohibition, not an ordering rule.** Extending the same rule Joe's own
pre-second-critique review already required for `wrench_b()` (see "Invariants preserved"): a
conforming `WrenchSource`'s `reset()` must also **never** write through a cached `InputHandle` —
`InputRegistry::reset()` alone owns restoring the seam's own state; a plugin's `reset()` restores
only its own *unrelated* internal state (`dryden.cpp`'s RNG/filter precedent, unaffected). `step()`
remains the **one** sanctioned write point (R4's `ActuatorModel` use case); `wrench_b()` (R3's own
labeled test-only exception aside) and `reset()` are not. With that prohibition stated, `Vehicle::
reset()`'s two possible orderings (`inputs_.reset()` before or after the per-model `reset()` pass)
are genuinely equivalent again — this time because nothing is permitted to touch the seam from
`reset()` at all, not because the read pointer alone was assumed to make it impossible.

**Config-literal writer schema.** The original draft's example, `vehicle: { controls: { controls.
elevator: 0.0 } }`, used a literal dotted string (`controls.elevator`) as one YAML map key.
`Config`'s actual path resolution (`config.cpp`'s `split()` and `child()`) treats **every** `.` in a
key as a separate nesting level, the same way `physical.mass.ixx` is read via three literal nested
YAML levels, never a flat string containing dots — so the original example would not parse as
intended at all. Fixed, using `Config`'s existing dot-navigation directly rather than inventing
anything: one new key, `vehicle.initial_controls`, whose own nested structure mirrors each
write-seam name's dot-segments as ordinary nested YAML (exactly like `physical.mass`'s keys already
work):

```yaml
vehicle:
  models: [...]
  initial_controls:
    controls:
      elevator: 0.0   # the WrenchSource that registered "controls.elevator" via bind_inputs()
```

Applying it is one call per declared input: `vehicle_config.at("initial_controls").number(name)`
(where `name` is the write-seam's own full dotted name, e.g. `"controls.elevator"`) — `Config`'s
existing multi-level navigation handles the rest with no new parsing code.

**PR 5's `Simulation::derivative()` — Joe's review round 1 (finding 4), corrected in round 2
(finding 2) after (h) made the original "two viable paths" framing wrong.** `docs/architecture.md`
§8 already documents `Simulation.derivative(state, inputs)` (M3 scope, not yet built, exact
signature settled by PR 5) evaluating a wrench/EOM derivative directly at an arbitrary `state`,
without calling `Simulation::step()` — trim/linearize's finite-difference perturbations must not
advance the frame counter or re-run the full per-frame pipeline.

**Correction.** The previous draft offered two "equally viable" paths, one of which — "`derivative
(state, inputs)`'s own `inputs` argument is applied directly for that one call, bypassing the seam's
registry entirely" — is **not actually possible** given (h)'s own registry-owned-storage design:
`wrench_b(const VehicleState&, const EnvironmentSample&)` takes no `inputs` argument at all and
reads *only* through the `const double*` `bind_inputs()` gave the plugin, which points at
`InputRegistry`'s own canonical storage. There is no code path by which a `derivative()`-supplied
`inputs` value could reach `wrench_b()` except by first being written into that same canonical
storage — i.e., through `write()` then `latch()`, the ordinary seam. **The one actual rule, stated
for the ADR:** `derivative()` evaluates whatever is *currently latched* in each `WrenchSource`'s own
canonical storage — nothing else is possible. `docs/architecture.md` §8's already-documented Python-
facing `derivative(state, inputs)` signature can still be *honored at the Python boundary* (PR 5's
binding still accepts an `inputs` dict) while its internal implementation does `write()` for each
declared input, then one `latch()`, before evaluating the wrench/EOM at `state` — PR 5's own choice
whether to keep that explicit parameter (implemented this way) or instead require its own caller to
`write()`+`latch()` directly. Either way, this PR's `latch()` must be public and reachable outside
`Simulation::step()` — already true of this design, so PR 5 needs no ADR change either way.

**Whether that `latch()` runs the non-finite check — the other half of Joe's finding 2, corrected
before the second Codex critique (fold-in, not yet dispatched at the time of the original text).**
`InputRegistry::latch()` itself never performs `HealthMonitor` reporting — reporting is always a
*caller* responsibility, the identical shape `report_out_of_domain` already uses (`HealthMonitor`
holds no registry/model pointers of its own, by design — see "Repository findings").
`Simulation::step()` (this PR's own scope) calls `HealthMonitor::check_input` every frame using
`latch()`'s returned name.

**Correction — the previous text's "never silently corrupt" claim was wrong for PR 5's own use
case.** `latch()`'s core safety property (never commit a non-finite pending value to canonical
storage) protects the *wrench*, but not what PR 5 actually needs protected: when a perturbed value
is non-finite, `latch()` leaves the *previous, last-good* value latched — meaning `derivative()`
would evaluate at the **wrong point** (the old state, not the requested perturbation) and still
return a perfectly finite, *plausible-looking* residual/Jacobian entry. Unlike a NaN, which fails
loudly, this is a silent, wrong answer — exactly the failure mode this project's own doctrine
(§15: "a sim that produces a plausible-looking trajectory from a broken setup is worse than one
that refuses to run") exists to prevent, now instanced for trim/linearize instead of a frame. A
skipped `HealthMonitor` report is therefore not merely "unlogged" for PR 5's own use — it is
**required**, not optional, for this specific caller: **PR 5's internal `write()`+`latch()`
sequence must check `latch()`'s returned name and raise (a Python exception, not a silent
continuation) whenever it is non-empty**, rather than proceeding to evaluate `derivative()` at a
point that silently differs from the one requested. This is a stronger requirement than `Simulation
::step()`'s own optional `HealthMonitor`-mediated reporting (which may reasonably choose `warn` and
continue the physics run) — trim/linearize has no equivalent "continue is fine" option, since its
entire numerical method is unsound the moment the evaluated point differs from the requested one.

## `kCoreApiVersion`

Bumped 4 → 5 per (g)'s recommendation. `core_api.hpp.in`'s comment gains one line: "5 (M3 PR 2):
`WrenchSource` gained a new virtual, `bind_inputs`, changing that interface's vtable layout — an
out-of-tree plugin built against version 4 must be rejected, not dispatched through a slot it does
not have (same reasoning as version 2's `requires_environment()` addition and version 4's own
`in_domain` addition). `Model::init()`'s own signature is unchanged; every non-`WrenchSource` plugin
is unaffected."

## Acceptance

| ID | Observable requirement | Executable check |
|---|---|---|
| R1 | `InputRegistry::add`/`write`/`latch`/`reset`/`first_unwritten_or_non_finite` behave per the Design section, including every documented error case (unit mismatch on a repeated `add()` for one name, unregistered name passed to `write()`, empty name) and the finite/non-finite init check | New `tests/unit/signals/input_registry_test.cpp`. **Corrected, second Codex critique (finding 4)**: the original list wrongly included "duplicate name" as an error case — under (h)'s get-or-create semantics (confirmed against R11), a second `add()` for an already-registered name with a *matching* unit is explicitly valid, not an error; only a *mismatched* unit under a repeated name is |
| R2 | `v0.controls.*` (or whatever name a test fixture registers) is visible in the vehicle's `SignalRegistry` after `Vehicle::init()`, from one `inputs.add(registry, ...)` call, matching a plugin's read-side name exactly | `tests/plugins/input_consumer` loaded through `Simulation::from_config`; assert `sim.signals()` (or the registry accessor test fixtures already use) contains the name |
| R3 | A value written *mid-frame, from inside a wrench evaluation itself* (RK4 stage 2 of 4) is not observed by that *same frame's* remaining stages (3, 4); it is observed only from the *next* frame's stages onward — the actual acceptance evidence for "a mid-frame write is not seen until the next frame" | **Redesigned a second time — Joe's review round 2 (finding 1): the round-1 version (write between two complete `sim.step()` calls) is vacuous.** It would pass identically even with *no* pending/canonical split at all — a naive "`write()` mutates canonical directly, no staging" design also shows the old value during a frame that already finished and the new value starting the next `sim.step()` call, since nothing reads the input *between* two such calls either way. That test proved only "writes between frames apply at the next frame," true of any design, not the specific pending/`latch()` mechanism this PR builds. **Real test, explicitly labeled a test-only exception to the amended `wrench_source.hpp` contract** (a conforming `WrenchSource` must never write through an `InputHandle` from inside `wrench_b()` — see "Invariants preserved" above; `tests/plugins/nan_wrench`'s own `mutable call_count_` is the precedent for a documented, narrow exception, confirmed directly): `input_consumer` caches an `InputHandle` (via `find()`, during `bind_inputs()`) and a `const double*` (via `add()`) for its own input, plus a `mutable std::uint32_t call_count_` reset to `0` in `step()` (`nan_wrench`'s own exact pattern). Inside `wrench_b() const noexcept`: increment `call_count_`; on the call where `call_count_ == 2` (RK4 stage `k2`), call the cached handle's `write(new_value)` (a `const`-qualified `InputHandle::write()`, since mutation happens through the pointed-to `double`, not through `wrench_b`'s own `this`) — a real `WrenchSource` must never do this; `input_consumer`'s own header comment says so explicitly, same as `nan_wrench`'s; always read the *canonical* value (`*const_ptr_`) and record it. Run one frame: assert all four recorded values (`k1`..`k4`, including `k3`/`k4`, *after* the stage-`k2` write) equal the *old* value — the write only staged `pending`, and `latch()` does not run again until the next frame boundary. Run a second frame: assert all four of *that* frame's recorded values equal the *new* one — `latch()` (running once, after `vehicle.step()`, before `vehicle.integrate()`, per (e)) committed it at the start of that frame, before `k1`. A naive no-staging design fails this test (stage `k3` would already show the new value); this PR's design passes it |
| R4 | A value written *from within a model's own `step()`* (simulating a future M4 `ActuatorModel`) is observed by *this same frame's* `wrench_b()` calls, not delayed a frame — the complementary case to R3, together pinning down exactly where (e) places `latch()` | `input_consumer` calls its own cached `InputHandle.write(...)` from inside its `Model::step()` override (called by `Vehicle::step()`, strictly before `vehicle.integrate()`); assert `wrench_b()`'s four calls *later in that same frame* already observe the new value. Without (e)'s fix (`latch()` before `step()`, the original placement), this test would fail — it is the acceptance evidence the ordering decision itself needs. R3 proves the pending/`latch()` split exists and matters; R4 proves `latch()` sits in the right place relative to `step()`/`integrate()` |
| R5 | All four RK4 stages of one frame see the identical written value | Extend R3's own recorded-values vector (already built for R3's own assertions): assert all **four** (not "four or five" — a `HealthMonitor` check consumes the wrench `wrench_b()` already returned, never calling it again, `vehicle.cpp:142–158`) entries within one frame are bitwise equal |
| R6 | Zero heap allocations over 1000 frames with a write-seam input configured, `latch()` running every frame regardless of whether `write()` was called that frame | New `tests/configs/no_alloc_write_seam.yaml`; new `TEST(NoAlloc, ...)` in `tests/physics/no_alloc_test.cpp`, matching the existing three fixtures' shape. **Wording fix**: this PR's only implemented writer (a config literal) writes once, at init — the allocation-free property under test is `latch()`'s own per-frame commit, which runs every frame unconditionally, not a repeated `write()` call |
| R7 | A vehicle loaded via `Simulation::from_config` (not `for_testing` — see the Design section's corrected account of where this check actually runs) whose config declares a `WrenchSource` with a registered-but-never-written, or non-finite, input fails at init, naming the input's dotted path, per (a)/(c)'s gap fix | New test: `input_consumer` variant that registers a second input with no matching `vehicle.initial_controls` entry (and a separate case with a `.nan` literal), loaded through `Simulation::from_config`; assert `ConfigError` naming it in both cases |
| R8 | A non-finite `write()`'d value halts (default) naming the input, not the consuming `WrenchSource`; on `warn`, every *other* finite entry still commits that frame and the bad entry's canonical value keeps its last successfully latched value (a new invariant this seam introduces — see (c)'s correction and "partial-latch behavior," not an existing rollback precedent) | New test in `tests/unit/health/` (mirroring the existing NaN/broken-quaternion/dead-peer/overrun fixture pattern, §15: "a health check nobody has seen fire is not known to work"), per (c), with two declared inputs — one non-finite, one finite — proving the finite one still commits |
| R9 | `InputRegistry::reset()` restores every entry to its config-literal init value, not to whatever was last committed | New test: `write()` a different value, then **`latch()`** (missing from the original description — found by the second Codex critique, finding 5: `write()` alone only changes `pending`, never `canonical`; without an intervening `latch()`, `canonical` never actually changes, and the test would trivially pass even with a no-op `reset()`), assert canonical now holds the different value, call `Vehicle::reset()`, assert canonical reverts to the config-literal init value |
| R10 | Two vehicles (`Simulation::for_testing`) get independent `InputRegistry` instances; writing one does not affect the other | **Gap found by Codex's plan critique, fixed here**: `Simulation::for_testing` does not mirror `from_config`'s config-driven validation by design (its own documented list of deliberately-skipped checks, `simulation.hpp`) — (a)/(c)'s init check and `freeze()`'s canonical-storage seeding both join that list. A `for_testing`-based test must call `write()` explicitly for every declared input on each vehicle before that vehicle's first `step()`, rather than relying on config-literal auto-application (which `for_testing` never runs) or on `freeze()` (which `for_testing` never calls either — every entry's canonical/pending storage stays at its NaN default, per (a)'s fold-in, until that test's own `write()`). New test in `tests/unit/kernel/simulation_test.cpp` or a new fixture, per (f), doing exactly that; a companion case proves a *forgotten* `write()` fails loudly at the vehicle's first `latch()` (naming the input via `health.input`), not by reading undefined memory |
| R11 | Two instances of the same `WrenchSource` plugin under one vehicle, registering the same name, do not collide; both read the same shared value, per (h) | New test: load `input_consumer` twice under one vehicle (mirroring PR 4's own left/right `turbofan` shape), both calling `bind_inputs()` with the identical name; assert no `Error`, both instances' `const double*` are equal, and a single `write()` under that name is observed by both |
| R12 | `kCoreApiVersion` bumped to 5; every existing plugin recompiled against the new `WrenchSource` vtable still loads (only `WrenchSource`'s layout changes; `Model::init()`'s signature is unchanged — no other plugin kind is affected); `bad_version`'s existing rejection test still passes | `ctest --preset asan -R Plugin` (loader test label) |
| R13 | Full gate, once, on the final candidate | `./scripts/check-full.sh` (`just check`): `check: OK`, exit 0 |

Performance/allocation bounds: R6. New config schema: `vehicle.initial_controls` (Design section) — a new
key, not a redefinition of an existing one; no golden/tolerance file touched (no `docs/decisions/`
file is written this session either — the draft ADR below stays a draft until Joe arms `touch-
goldens` and gives the go-ahead, per this session's own instruction).

## Questions and decisions

| # | Question | Consequence | Owner | Answer |
|---|---|---|---|---|
| Q1 | Does (a)'s init-error choice ever block a legitimate "this input is optional, sensible to leave at some fixed value" use case (e.g., a landing-gear-position flag irrelevant at cruise)? | If yes, option 1 as written would force every such flag into every vehicle's config even when it never varies | Joe | **Accepted as written.** A plugin that truly wants an always-fixed value keeps it a plain code constant instead of declaring it through this seam. |
| Q2 | Should `vehicle.initial_controls`' key naming allow a plugin's own relative name (`controls.elevator`) or require the full `v0.controls.elevator` form, given `physical.mass`'s existing keys are read without a `v0.` prefix? | Affects config ergonomics and whether the schema generalizes cleanly to M4's per-vehicle transport config later | Joe | **Decided: relative names (no `v0.` prefix)** — matches `physical.mass`'s own existing precedent. |
| Q3 | Is `InputRegistry::find()`'s resolved-handle path (for a future M4 transport writer) worth designing further now, or left exactly as sketched? | M4 is a separate milestone with its own plan; over-designing it here risks pinning a shape M4's own plan critique would have to unwind | Joe | **Closed: `find()`/`InputHandle` are built in this PR** (needed for R3/R4's own acceptance evidence, not deferred). Whether a non-`WrenchSource` writer (M4's own transport/`ActuatorModel`) can reach either is the disclosed, unresolved M4-scope gap (Design section, ADR); M4's own plan resolves it, with its own critique. |
| (h) | Two instances of one plugin registering the same name: symmetric shared value, or per-instance names? | Determines whether PR 4's left/right `turbofan` see one throttle command or two independent ones by default | Joe | **Accepted: symmetric shared throttle is the default** (option 3, registry-owned storage). PR 4 may opt into per-station names later if it needs differential thrust — this PR's primitive supports either, PR 4 decides which it needs. |

**Approved by Joe, following both Codex plan critiques and both of Joe's own review rounds above**,
subject to the one fold-in recorded in (a)/Design/R10/the ADR (entries default-initialize to quiet
NaN, not an unspecified value) — no further review round required for that fold-in.

## Follow-up (not part of this PR's diff)

Recorded here per Joe's instruction this session; to be added as a new, unassigned `PLAN.md`
follow-up line the next time that file is touched — not filed against `PLAN.md` in this
planning-only session.

**A separate tier-2 workflow PR** restoring the "no recon subagents" rule's visibility and
rationale, dropped from the standing docs by Workflow v3 (#30) (origin: commit `b07057c`/#8,
2026-09-14; rationale in that day's `docs/lessons.md` entry — subagent cost, and fidelity: a
Sonnet-pinned subagent hands the planner a lossy secondhand account instead of a first-hand read):
(a) one line in `AGENTS.md`'s "Commands and boundaries" — read code directly, no exploration/recon
subagents (cost + fidelity, `docs/lessons.md` 2026-09-14); the Codex review/critique route
(`docs/agents/review-dispatch.md`) is the sole exception; (b) "directly, not via subagents" added to
`.claude/skills/plan-feature/SKILL.md` step 1's "Inspect..." instruction. `AGENTS.md`+`CLAUDE.md`
are at 77/100 lines today, so (a) fits the existing 100-line budget.

## Draft ADR — new record (would become ADR-0017 on acceptance)

*(Kept with this plan per `docs/templates/adr.md`'s own convention — "Keep a draft with the feature
plan" — not written to `docs/decisions/` this session per this session's own instruction: Joe arms
`touch-goldens` and gives the go-ahead before that file is created.)*

---

### ADR-0017 (draft): Control-input write seam — `InputRegistry`, zero-order hold, per-vehicle

Status: proposed
Date: 2026-09-19
Owner: Joe
Related/supersedes: elaborates ADR-0011 (per-vehicle ownership) and ADR-0016 (the zero-order-hold and
purity precedent this decision extends from wind to control inputs generally); bumps `kCoreApiVersion`
per the existing policy `core_api.hpp.in` documents (elaborates, does not supersede, versions 2/3/4's
own bumps).

#### Context

`WrenchSource::wrench_b()` reads a plugin's own members; nothing today lets code outside that plugin
set one of them by name. Three callers over the project's life need to (a config literal, open-loop,
this PR; Python trim/linearize, PR 5; M4's transport/`ActuatorModel`, later), and all three must use
one mechanism or PR 5 and M4 each reinvent it. Stevens & Lewis (3rd ed., §2.5 pp.109–110) separate the
control vector `Ū` from the state vector `X` explicitly (Eq. 2.5-3/2.5-4); their own F-16 example
(§3.5, Fig. 3.5-2, pp.182–183) implements this as a Fortran `COMMON/CONTROLS/THTL,EL,AIL,RDR` block
written externally by the trim routine/driver, never as a parameter to the derivative subroutine
`F(TIME,X,XD)` itself — the same shape this decision adopts: `wrench_b(vs, env)`'s signature stays
unchanged, and the control values arrive through an external write, not through `wrench_b`'s own
argument list. Zipfel (4th ed., Fig. 9.39 p.364; App. E.2.3/E.2.5 p.599) describes the same separation
at the architecture level: CADAC++'s modules ("Control," "Aerodyn," "Propul," ...) communicate only
through named "module-variables" on "a global communication bus," never by directly calling into one
another's internals.

#### Decision

A new class, `InputRegistry` (`core/include/astra/signals/input_registry.hpp`), is `SignalRegistry`'s
mutable counterpart, with registry-owned storage (h): a plugin registers a writable input in a new
`WrenchSource::bind_inputs()` virtual (default no-op; called by `Vehicle::init()`'s existing
`WrenchSource`-discovery pass, still inside the vehicle's `SignalRegistry::PrefixScope`, after that
model's own `Model::init()` — see (d)'s finding-1 correction for exactly where), e.g. `elevator_ =
inputs.add(registry, "controls.elevator", "rad")`, where `elevator_` is a `const double*` into
`InputRegistry`'s own storage, not a plugin-owned member. `add()` also registers that same pointer
read-only under the identical name in the vehicle's `SignalRegistry` (visible as `v0.controls.
elevator`) — but only on the *first* `add()` call for a given name; a second `WrenchSource` instance
registering the same name (h: two engines of one turbofan plugin) gets back the same pointer, no
duplicate-name error either side. `Model::init()`'s own signature is unchanged; `InputRegistry`
stores no `SignalRegistry&` (the registry is `Simulation`-owned and shared, not `Vehicle`-owned —
ADR-0011 grants each vehicle a signal *namespace*, not a registry *object*). `InputRegistry::write()` (by name) stages a pending value directly. `InputRegistry::find()`
**corrected, second Codex critique (finding 6): `find()` itself stages nothing — it only resolves a
name to a repeatable `InputHandle`; that handle's own `write(double)` stages the pending value later,
from a caller that holds the handle** (`step()`, sanctioned; `wrench_b()`, R3's own labeled test-only
exception; whether a non-`WrenchSource` M4 writer can reach `find()` at all is a disclosed,
unresolved gap — see "Consequences and validation" below).
`InputRegistry::latch()`, called once per vehicle per major frame by `Simulation::step` — after
`vehicle.step(ctx)`, before `vehicle.integrate(...)` (e: moved from an earlier before-`step()`
placement, which would have baked a one-frame delay into any step()-time writer, exactly the failure
mode §5 already warns about) — commits every entry's pending value into its canonical storage,
uniformly (never short-circuiting on the first fault, ADR-0016's own precedent), never committing a
non-finite one (warn or halt alike — a new invariant this seam introduces). Zero-order hold across a
frame's four RK4 sub-stages follows from `latch()` running exactly once per frame, plus an amendment
this PR makes to `wrench_source.hpp`'s doc comment permitting a control-seam value to change
*between* frames (its existing text, unchanged, already forbids any dependence on call count/order
*within* one). `InputRegistry` is owned one-per-`Vehicle` (ADR-0011), its entries stored in a
`std::deque` (not a `std::vector`) so a returned `const double*`/`InputHandle` survives both further
registry growth and the `Vehicle` move `Simulation::from_config`/`for_testing` performs. Every
entry's canonical, pending, and freeze()-recorded storage default-initializes to a quiet NaN, never
an unspecified value — "never written" and "written non-finite" are deliberately the same, loudly
reported state. `Simulation::from_config` applies each declared input's `vehicle.initial_controls`
literal, then validates every entry has a finite pending value (`ConfigError` naming its dotted path
if not — never a silent zero), then calls `InputRegistry::freeze()`, which both closes registration
and seeds canonical storage from each entry's validated pending value — the one unconditional,
init-time commit that guarantees canonical storage is never read uninitialized, independent of
which per-frame `latch()` runs first. `Simulation::for_testing` performs none of these three steps,
matching its own existing, documented list of `from_config`-only validations it does not mirror —
its entries stay at their NaN default until a test's own `write()`; a forgotten one fails loudly at
that vehicle's first `latch()`, naming the input, rather than reading undefined memory. A
non-finite value written *during* the run is caught at `latch()` (first-fault-wins) and reported
through a new `HealthMonitor::check_input`/`HealthCheck::kNonFiniteInput`, naming the input, not the
consuming `WrenchSource`. `InputRegistry::reset()` restores every entry to the value recorded when
`freeze()` last ran — or, for an entry that was never frozen (`for_testing`), back to its NaN
default. `Vehicle::reset()` calls it alongside delegating to every model's own `reset()`, in either
order — ordering between the
two is not safety-critical, because a conforming `WrenchSource`'s `reset()`, like its `wrench_b()`,
is prohibited from writing through a cached `InputHandle` at all (only `step()` is a sanctioned write
point); the read-side `const double*` alone was not sufficient reasoning for this (a plugin may
separately cache a *writable* handle too — see "Invariants preserved"). `latch()` is public, reachable
independently of `Simulation::step()`; PR 5's `derivative(state)` evaluates whatever is *currently
latched* in each `WrenchSource`'s own canonical storage — the only mechanism possible, since
`wrench_b()` takes no `inputs` argument — so a PR-5-internal `write()`-then-`latch()` sequence,
called before each finite-difference evaluation, needs no ADR change here, provided PR 5 checks
`latch()`'s returned name and raises rather than continuing: `latch()` never commits a non-finite
value, but silently evaluating `derivative()` at the previous, last-good point instead of the
requested one is a wrong, plausible-looking answer, not a safe one, for a numerical method whose
soundness depends on evaluating exactly the requested point. `WrenchSource` gains one
new virtual (`bind_inputs`), an ABI break; `kCoreApiVersion` bumps 4 → 5. `Model::init()` itself, and
every non-`WrenchSource` plugin, is unaffected.

#### Rejected alternatives

- **Change `WrenchSource::wrench_b()`'s signature to take controls directly.** Rejected: `PLAN.md`'s
  own PR 2 line pins this — Joe's direction was decided before this document, and S&L's own F-16
  example (§3.5) demonstrates the alternative (external write, unchanged derivative signature) is a
  well-precedented design, not merely a constraint accepted for convenience.
- **A mandatory code-level default at `add()` time (option 2 under (a)).** Rejected: reintroduces
  the silent-zero ambiguity the design is meant to prevent, and works against M3 PR 1's own
  established "numbers in YAML not code" direction.
- **Merge write capability into the existing `SignalRegistry` (no new type, no `kCoreApiVersion`
  bump).** Rejected — reasoning corrected by Codex's plan critique: not because `SignalRegistry` is
  "frozen after init" (its `freeze()` closes registration only; every entry's pointed-to value keeps
  changing every frame — `signal_registry.hpp:39–40`), but because merging the two would erase the
  real distinction between them: every existing `SignalRegistry` entry is written only by the model
  that registered it (a self-publish), while this seam's entries are written by code *other than*
  the registering model. Two small, single-purpose types keep that "who may write this" boundary
  structural, matching Zipfel's own module/module-variable separation (App. E.2.3, p.599).
- **Add the `InputRegistry&` parameter to `Model::init()` directly** (an earlier draft of this
  decision). Rejected — found by Codex's plan critique: `Simulation`, not `Vehicle`, owns the one
  shared `SignalRegistry` (`simulation.hpp:150`), and shared environment models (`EarthModel`,
  `GravityModel`, `AtmosphereModel`, `WindModel`) call `Model::init()` directly from `Simulation::
  from_config` (`simulation.cpp:71`), outside any vehicle and with no use for a writable-input seam.
  Changing the common `Model::init()` signature would force every one of those plugins to accept a
  parameter they cannot use. Attaching `bind_inputs` to `WrenchSource` specifically avoids this.
- **Caller-owned storage, one name → one target** (an earlier draft; `add()` took a `double* target`).
  Rejected — found by Joe's review round 1 (h): breaks the moment one vehicle loads the same plugin
  twice under different config (M3 PR 4's own two-engine `turbofan` shape), since two different
  target pointers cannot both be "the" value under one `SignalRegistry`-visible name. Registry-owned
  storage (S&L's `COMMON/CONTROLS`, Zipfel's bus — both already "one shared slot, many readers") lets
  a second `WrenchSource` instance subscribe to an existing name instead of colliding with it.
- **`latch()` before `vehicle.step(ctx)`** (an earlier draft's ordering). Rejected — found by Joe's
  review round 1 (e): bakes a one-frame delay into any `Model` writing through the seam from its own
  `step()` (a future M4 `ActuatorModel`), the exact failure `docs/architecture.md` §5 already names
  ("model ordering is a classic source of one-frame delays that look like physics"). Moved to run
  after `step()`, before `integrate()`, matching §5's own canonical order ("...receive commands) →
  integrator step...").

#### Consequences and validation

Benefits: one write mechanism for config literals (this PR) and Python (PR 5) that also correctly
supports a plugin loaded more than once under one vehicle (h); zero change to `WrenchSource::
wrench_b()`'s signature or `Model::init()`'s signature; a documented, testable zero-order-hold
guarantee with no one-frame lag for a step()-time writer. Costs: `WrenchSource` gains a new virtual
(ABI break, `kCoreApiVersion` bump, every existing plugin recompiles); one new kernel type plus a
small handle type (`InputHandle`); one new `HealthMonitor` check and signal; one amended doc comment
(`wrench_source.hpp`'s purity contract).

**Risk, disclosed not fixed — Joe's review round 2 (finding 5).** `bind_inputs()` is attached to
`WrenchSource` specifically (g), because that is the only model kind this PR's own scope needs to
serve. This leaves an open question for M4's own plan: a transport writer or `ActuatorModel` that is
*not itself* a `WrenchSource` has no seam-provided route to obtain a write handle for a name another
model owns, and even a writer that *could* reach `find()` would still depend on `vehicle.models`'
load order (the owning model's `bind_inputs()` must already have run) — a dependency nothing in this
design validates or enforces. Not exercised by this PR's own acceptance evidence (a `WrenchSource`
-only test fixture); not engineered around here, the same posture ADR-0012 and ADR-0016 already took
for their own accepted, differently-shaped gaps. M4's own plan, when it starts, either designs
`ActuatorModel` as a `WrenchSource` (sidestepping this entirely) or extends the seam with a
non-`WrenchSource` binding route — a decision for that plan's own critique, not this one.

Migration/rollback: fully reversible, single-PR change; no data or schema migration beyond the new,
additive `vehicle.initial_controls` config key. Acceptance commands: this plan's Acceptance table
(R1–R13) above.

#### Approval

**Accepted by Joe**, following both Codex plan-critique tables below, both "Joe review round 1"/
"Joe review round 2" sections further down this document, and one final fold-in (entries default-
initialize to quiet NaN — see (a)/Design/R10 above) that needed no further review round. Q1–Q3 and
(h) decided per "Questions and decisions." Written clean, with no correction-history narrative, at
`docs/decisions/0017-control-input-write-seam.md`.

#### Plan critique round 1 — resolved

One independent, read-only Codex plan critique was obtained on this document (fresh context,
`docs/agents/review-dispatch.md`'s plan-critique procedure — `node codex-companion.mjs task
--prompt-file ...`, no `--write`, foreground) before this document was shown to Joe. Twelve findings
were returned; each was independently reproduced against the actual repository (not accepted on
Codex's word alone) before being fixed above:

| # | Finding | Verified how | Resolution |
|---|---|---|---|
| 1 | `wrench_source.hpp:28`'s purity contract, as literally written, does not already permit a control member changing between frames | Re-read the exact comment text | "Invariants preserved"/Assumptions reworded: this PR *amends* the doc comment, does not merely cite an existing one |
| 2 | `Vehicle` does not own/construct a `SignalRegistry`; it is a by-value `Simulation` member (`simulation.hpp:150`) | Read `simulation.hpp:140–160` directly | (d)/Design: `InputRegistry` stores no `SignalRegistry&`; `add()` takes it as a parameter |
| 3 | Shared environment models call `Model::init()` outside any vehicle, with no vehicle namespace (`simulation.cpp:71`'s `load_environment_model`) | Read `simulation.cpp:55–90` directly | (g)/Design: the seam attaches to a new `WrenchSource::bind_inputs()` virtual, not to `Model::init()` |
| 4 | The `vehicle.controls.controls.elevator`-shaped literal dotted YAML key does not parse the way the plan claimed (`Config`'s `split()`/`child()` treat every `.` as a nesting level) | Read `config.cpp:70–100,240–260` directly | Design: schema fixed to `vehicle.initial_controls` with ordinary nested YAML, using `Config`'s existing navigation, no literal dotted keys |
| 5 | A first non-finite config write satisfies "was it written" without ever establishing a real last-good value; `reset()` would then restore the bad value | Traced the logic by hand against the original `first_unwritten()`/`reset()` design | (a)/(c)/Design: renamed `first_unwritten_or_non_finite()`, validating finiteness at init too |
| 6 | Resetting the registry *before* each plugin's own `reset()` lets a plugin overwrite the just-restored value, per `dryden.cpp`'s own "zero every mutable member" precedent | Read `dryden.cpp:151–160` and `model.hpp:36` directly | Design: `Vehicle::reset()` order reversed — every model's own `reset()` first, `inputs_.reset()` last |
| 7 | ADR-0011's CLI carve-out is textually scoped to "the CLI's convenience defaults," not to a future `sim.write_input()` | Re-read `0011-one-vehicle-as-list-of-one.md:15` directly | (f) reworded: this is a **new** exception PR 5 must record, not one ADR-0011 already grants |
| 8 | R3's original test ("write between two `sim.step()` calls") cannot exercise a mid-frame write, since each `sim.step()` runs a full frame to completion | Read `simulation.cpp:492` and traced `Vehicle::step()`/`integrate()`'s call order | Acceptance R3 redesigned: write from inside a fixture's own `Model::step()`, which runs after `latch()` but before `integrate()` |
| 9 | The cited `nesc_aero` "mutable instrumentation" precedent was removed by ADR-0016; wrench evaluation is exactly 4x/frame, not "4 or 5" | Read `tests/plugins/nan_wrench/nan_wrench.cpp` and `0016-per-rk4-substage-wrench-evaluation.md:57` directly; read `vehicle.cpp:130–172` for the call count | "Repository findings"/R4 corrected: cites `nan_wrench`'s real, current `mutable call_count_` pattern; exactly four calls |
| 10 | R5 overstated "written every frame" (only a config literal, written once, is implemented); `for_testing` has no control-literal setup hook and skips config-driven validation by design | Read `no_alloc_test.cpp:83`/`simulation.hpp`'s `for_testing` docs directly | R5 wording fixed (tests `latch()`'s own per-frame cost, not a repeated `write()`); R9 notes `for_testing` must `write()` explicitly, matching its own documented list of skipped checks |
| 11 | Only `report_out_of_domain` receives a caller-evaluated predicate; `check_wrench`/`check_state` evaluate their own (`health_monitor.cpp:75–95`). The "warn keeps last good value" claim wrongly cited ADR-0016's wrench-fault revert, which never fires under `warn` (`apply()`'s own falsy return, `vehicle.cpp:178–184`) | Read `health_monitor.cpp:39–95` and `vehicle.cpp:130–190` directly | "Repository findings" bullet scoped correctly to `report_out_of_domain`; (c) reworded: `latch()`'s never-commit-a-bad-value behavior is a **new** invariant, not an existing precedent |
| 12 | Version 4's bump was for **new** non-pure virtuals (`in_domain`), not a changed parameter list; the `SignalRegistry`-merge rejection wrongly claimed values freeze at `freeze()` | Read `core_api.hpp.in:18–22` and `signal_registry.hpp:39–40` directly | (g) rewritten: version 4 is now the *closer* precedent for the corrected `bind_inputs`-as-new-virtual design; the merge-rejection reasoning replaced with the self-write-vs-external-write distinction |

No finding was rejected; all twelve reflected a real gap or inaccuracy in the original draft, most of
them structural (findings 2, 3, and 6 in particular would have blocked implementation entirely, not
just needed a wording fix). No code, test, or `docs/decisions/` file was touched during this
correction pass — plan-document-only, per this session's own instruction (`touch-goldens` not armed).

#### Plan critique round 2 — resolved

A second independent, read-only Codex plan critique was dispatched (same procedure as round 1),
scoped explicitly to the sections redesigned across rounds 1–2: the "Design: `InputRegistry`"
section in full, design questions (e) and (h), the Acceptance table (R1–R13), and the Draft ADR
(Context/Decision/Rejected alternatives/Consequences — excluding its own correction-history tables).
Six findings returned; each independently reproduced against the actual repository before being
fixed above:

| # | Finding | Verified how | Resolution |
|---|---|---|---|
| 1 | The "canonical value is never read uninitialized" guarantee does not actually hold: R4 permits a frame-0 `Model::step()` write before that frame's own `latch()`; if it is non-finite under `health.input: warn`, `latch()` correctly refuses to commit it, but canonical storage was never committed to *at all* yet — `wrench_b()` reads genuinely uninitialized memory, not a stale "last good value" | Re-read R4's own test design against the original "why never read uninitialized" reasoning, confirmed the gap by tracing frame-0's exact call order | `InputRegistry::freeze()` redefined to seed canonical storage from validated pending values, once, unconditionally, at init time — independent of which per-frame `latch()` happens to run first. "Why the canonical value is never read uninitialized" and the ADR both rewritten |
| 2 | Reset ordering is not actually made irrelevant by the read-side `const double*` alone: a plugin may separately cache a *writable* `InputHandle` (R3/R4 both rely on this), and nothing prohibited a plugin's own `reset()` from writing through it, which would make the two `Vehicle::reset()` orderings genuinely different again | Checked `InputHandle`'s own contract and `Model::reset()`'s signature (`model.hpp:36`) against the "ordering doesn't matter" claim — found nothing preventing a `reset()`-time `.write()` call | Extended the wrench_b()-write prohibition (already added this round per Joe's own fold-in) to also cover `reset()` — only `step()` is a sanctioned write point. With that prohibition stated, ordering is genuinely irrelevant again. (b), the Design section, and "Invariants preserved" all updated |
| 3 | R10 contradicts the Design's own initialization contract: R10 requires `Simulation::for_testing` to return with unwritten inputs so a test can `write()` them afterward, but the Design section placed the init check *inside* `Vehicle::init()`, which `for_testing` calls too (`simulation.cpp:469–478`) — both cannot hold | Confirmed `Vehicle::init()`'s actual signature (`vehicle.hpp:96`) takes no vehicle-level config subtree at all, so it cannot read `vehicle.initial_controls` or apply/validate config literals; confirmed `for_testing` calls the identical `Vehicle::init()` | Config-literal application, the init check, and `InputRegistry::freeze()` all moved out of `Vehicle::init()` into `Simulation::from_config` (called after `vehicle.init()` returns) — matching `for_testing`'s own already-established pattern of skipping `from_config`-only validations. (a), the Design section, R7, and the ADR all updated |
| 4 | R1 still lists "duplicate name" as an `InputRegistry` error case, contradicting (h)'s own get-or-create semantics (confirmed valid, not an error, by R11) | Cross-checked R1's error-case list against R11's own description and (h)'s recommendation | R1 corrected: a repeated `add()` for one name with a matching unit is valid; only a unit mismatch is the actual error case |
| 5 | R9's test (`write()` a different value, `Vehicle::reset()`, assert canonical reverts) does not demonstrate canonical restoration at all — `write()` alone only changes `pending`, never `canonical`, so the assertion would trivially pass even with a no-op `reset()` | Traced `write()`'s own documented effect ("has no effect on the canonical value until the next `latch()`") against R9's test steps, found the missing step | R9 corrected: adds the missing `latch()` call between `write()` and `Vehicle::reset()`, so canonical genuinely changes before being reset |
| 6 | The ADR's Decision paragraph says `write()` and `find()` "both stage a pending value" — wrong: `find()` only resolves a name to a handle and takes no value argument; staging happens through the returned handle's own `write(double)` | Re-read `find()`'s own defined signature against the ADR's claim | ADR wording corrected: `write()` stages directly; `find()` resolves a handle whose own `write()` stages later |

No finding was rejected. Findings 1 and 3 were structural (an uninitialized-memory read reachable
under a sanctioned code path; a design that could not have compiled/passed its own acceptance
evidence as written). Finding 2 reopened a question round 2 had incorrectly closed. No code, test,
or `docs/decisions/` file was touched — plan-document-only, `touch-goldens` not armed.

## Joe review round 1

Six findings from Joe's own read of the post-Codex-critique document, kept separate from the Codex
table above per Joe's own instruction. Each independently reproduced against the actual repository
before being fixed (per `docs/process/workflow.md`'s review-fix discipline — `Correction rounds: n/2`
convention, though this is a plan-document round, not an implementation one):

| # | Finding | Reproduction | Resolution |
|---|---|---|---|
| 1 | `bind_inputs()` would register unprefixed — `vehicle.cpp:21–24`'s `SignalRegistry::PrefixScope` closes before the `WrenchSource` discovery pass that would call it | Read `vehicle.cpp:1–40` directly: the `PrefixScope` lives in a nested block closing immediately after `loaded.model->init(...)`, before the `dynamic_cast<WrenchSource*>` check | (d): the `dynamic_cast`/`bind_inputs()` call moves *inside* the same nested block, so it runs while the scope is still open; the existing `requires_environment()` check (which never touches the registry) stays where it is |
| 2 | One name → one target breaks PR 4: `cruise.yaml` loads `turbofan` twice (`station: left`/`right`); both would `add("controls.throttle", ...)` → `SignalRegistry`'s existing duplicate-name `Error` | Read `signal_registry.cpp:25–30` directly (the exact throw); read `docs/architecture.md` §13's cruise.yaml example (both commented `turbofan` entries) | New design question (h): registry-owned storage, `add()` returns `const double*` (no `target` param), get-or-create semantics — recommended against S&L's `COMMON/CONTROLS` and Zipfel's bus, both literally "one shared slot, many readers" |
| 3 | `latch()`-before-`vehicle.step()` bakes a one-frame delay into any `Model` writing from its own `step()` (a future M4 `ActuatorModel`) — exactly what §5 warns against | Re-read `docs/architecture.md` §5's own line: "model ordering is a classic source of one-frame delays that look like physics" | (e) rewritten: `latch()` moves to run *after* `vehicle.step(ctx)`, *before* `vehicle.integrate(...)` — a step()-time write is committed the same frame; new acceptance row R4 proves it |
| 4 | `Simulation::derivative(state)` (PR 5) never calls `Simulation::step()`, so written inputs would never latch for trim/linearize unless the ADR states a rule | Read `docs/architecture.md` §8: `Simulation.derivative(state, inputs)` is already documented as taking `inputs` explicitly | Design section states both compatible paths (explicit `inputs` argument, or a public `latch()` PR 5 calls itself) and commits `latch()` to being public — PR 5 needs no ADR change either way |
| 5 | R3's fixture needs an `InputRegistry&`/handle that survives past `init()`, but a naively-cached reference would dangle once `Vehicle` is moved into `vehicles_` | Read `simulation.cpp:379` and `simulation.cpp:478` directly (`sim.vehicles_.push_back(std::move(vehicle))` in both `from_config` and `for_testing`) | Design section's new "why entries survive a Vehicle move" explains `InputRegistry`'s internal `std::deque<Entry>` (not `std::vector<Entry>`) gives every entry independently stable storage under both container growth and a `Vehicle` move — no `unique_ptr<InputRegistry>` wrapper needed, unlike `published_wrench_` |
| 6 | Partial-latch behavior on a runtime fault was never stated; `InputHandle`/`find()` were deferred as a stub despite R3/R4 now needing them | Re-examined the (then-)current draft's `find()` comment ("not built by this PR") against what R3/R4's own test mechanism actually requires | (c) states uniform, non-short-circuited `latch()` (ADR-0016's own precedent) with first-fault-wins reporting; `InputHandle`/`find()` fully defined in the Design section (not dropped — needed for R4's own acceptance evidence) |

All six reflected real problems, three of them (1, 2, 3) structural enough to have produced a design
that either silently mis-namespaced every write-seam signal, threw at load for PR 4's own two-engine
config, or quietly baked in the exact one-frame-delay bug §5 already warns about. No code, test, or
`docs/decisions/` file was touched during this round — plan-document-only, `touch-goldens` not armed.

## Joe review round 2

Five further findings from Joe's own read of the post-round-1 document, kept in its own section per
Joe's own instruction. Each independently reproduced before being fixed:

| # | Finding | Reproduction | Resolution |
|---|---|---|---|
| 1 | R3 (round 1's version: `write()` between two `sim.step()` calls) is vacuous under the corrected latch-after-step ordering — it would pass identically even with no pending/canonical split at all, proving nothing specific to this design | Traced through what a naive "`write()` mutates canonical directly" design would do under R3's old test steps: identical observable result | R3 replaced with the real evidence: `input_consumer` writes via its own cached `InputHandle` from *inside* `wrench_b()` at RK4 stage `k2` (`nan_wrench`-style `mutable call_count_` instrumentation, read directly); asserts stages `k3`/`k4` *that same frame* still see the old value, the *next* frame's stages see the new one. `InputHandle::write()` made `const` so it is callable from `wrench_b() const noexcept` |
| 2 | `derivative()` path (i) ("apply `inputs` directly, bypassing the seam") is impossible: `wrench_b()` takes no `inputs` argument and reads only through the registry-owned `const double*` (h) — values can only arrive via `write()`→`latch()` | Re-examined `wrench_b`'s actual signature (`const VehicleState&, const EnvironmentSample&`, no third argument) against the two-paths claim | ADR's "PR 5's `Simulation::derivative()`" paragraph rewritten: one rule stated (`derivative()` evaluates whatever is currently latched), §8's Python-facing `inputs` parameter can still be honored by PR 5 internally doing `write()`+`latch()`. Also stated whether that internal `latch()` runs the non-finite check: no — `HealthMonitor` reporting is always a caller choice, matching `report_out_of_domain`'s own shape; `latch()`'s core safety property (never commit a bad value) holds regardless |
| 3 | Stale after (h): a plugin's input-bound member is a `const double*`, so its own `reset()` cannot write to it at all — the entire "ordering prevents a plugin from stomping the restored value" rationale (from the Codex-critique-era fix) no longer applies, and the document had two near-duplicate copies of both the "`Vehicle::reset()` ordering" and "Config-literal writer schema" blocks | Diffed the two occurrences of each block directly in the file (an artifact of how round 1's edits were applied) | Duplicates removed; (b), the Design section, and R9's second test variant all rewritten to state ordering is no longer safety-critical (the type system already prevents the hazard); the now-impossible "plugin `reset()` touches its own input member" sub-test dropped from R9 |
| 4 | The "why entries survive a Vehicle move" paragraph's closing sentence said the *opposite* of what its own reasoning supports, and directly contradicted R4 (which requires caching an `InputHandle` during `bind_inputs()`) | Re-read the sentence against R4's own test design, found a direct contradiction | Corrected: a pointer into an entry's own stable (`deque`) storage is safe to cache anytime, including during `bind_inputs()` — that is the point of the design. What is unsafe is caching a raw `InputRegistry&`/`Vehicle&` itself, whose own address moves. The retracted sentence had these backwards |
| 5 | Only `WrenchSource` gets `bind_inputs()` (g), so a non-`WrenchSource` writer (a plausible shape for M4's `ActuatorModel`) has no seam-provided route to a write handle at all; and even a writer that could reach `find()` would depend on `vehicle.models`' load order, unvalidated | Checked `bind_inputs()`'s actual attachment point (`WrenchSource` only) against architecture.md §7's own description of `ActuatorModel` (transforms a command into an achieved position another model reads — not necessarily a wrench contributor itself) | Design section gains a "Known gap, disclosed not fixed" note (matching ADR-0012/ADR-0016's own precedent for a disclosed, not-engineered-around gap); the ADR's own "Consequences and validation" carries the same disclosure; (e)'s M4-transport bullet reworded from "already accommodates it" to "ordering position only, not a built or verified route" |

Findings 1, 3, and 5 were the substantive ones (a vacuous acceptance test, an obsolete-but-still-stated
safety rationale with real duplication, and an undisclosed gap in what the primitive actually
covers); 2 and 4 were precision fixes to claims this document itself made about its own design. No
code, test, or `docs/decisions/` file was touched during this round — plan-document-only,
`touch-goldens` not armed. Per Joe's own instruction, no further action taken past this point this
turn — Joe decides whether a second Codex plan critique runs against the redesigned sections next.

## Done when

- ~~One independent Codex plan critique obtained on this document~~ **Done** — twelve findings, all
  verified against the actual repository and resolved directly in this document (see "Plan critique
  round 1 — resolved" above).
- ~~Joe's own review round 1~~ **Done** — six further findings (three structural: unprefixed
  registration, the PR 4 duplicate-name collision, the one-frame-delay ordering bug), all
  independently reproduced and resolved (see "Joe review round 1" above).
- ~~Joe's own review round 2~~ **Done** — five further findings (a vacuous acceptance test, an
  impossible `derivative()` path, an obsolete safety rationale plus real section duplication, a
  self-contradicting move-safety claim, and an undisclosed M4-scope gap), all independently
  reproduced and resolved (see "Joe review round 2" above).
- ~~A fold-in pass before the second Codex critique~~ **Done** — the `wrench_b()`-write prohibition
  and the `derivative()`/non-finite-check correction, both from Joe's own review, folded in.
- ~~A second Codex plan critique, scoped to Design/(e)/(h)/Acceptance/ADR~~ **Done** — six findings,
  two of them structural (an uninitialized-canonical-storage gap reachable via R4's own sanctioned
  code path; a design that could not have passed its own R10 acceptance evidence as written), all
  independently reproduced and resolved (see "Plan critique round 2 — resolved" above).
- ~~One final fold-in (entries default to quiet NaN, no further review round)~~ **Done** — see (a)/
  Design/R10/ADR above.
- ~~Joe's explicit spec/plan approval~~ **Done** — recorded above, with a decision on Q1–Q3 and (h)
  (see "Questions and decisions").
- ~~ADR-0017 accepted~~ **Done** — written clean, no correction-history narrative, at
  `docs/decisions/0017-control-input-write-seam.md`.
- R1–R13 to be proved and full independent review (general + adversarial, given this touches
  `core/kernel/` and the plugin ABI) completed at implementation time — not part of this
  planning-only session.

## Plan critique

**Two Codex plan critiques complete**, both `node codex-companion.mjs task --prompt-file <focus>`,
no `--write`, foreground, `docs/agents/review-dispatch.md`'s plan-critique procedure.

**Round 1** ran against the document as it stood after this session's first full draft. Twelve
failed assumptions found; all twelve independently reproduced against the actual repository (not
accepted on Codex's assertion alone — each has a "Verified how" entry naming the exact file/lines
read) and resolved — see "Plan critique round 1 — resolved" above.

**Between the two rounds**, two rounds of Joe's own review found eleven more findings — several
(the `PrefixScope` timing bug, the PR 4 duplicate-name collision, the one-frame-delay ordering bug,
R3's vacuous acceptance test, the obsolete reset-ordering rationale with real duplicated text, the
undisclosed M4 gap) structural enough that the design round 1 reviewed no longer matched: `bind_
inputs()`'s attachment point moved, `add()`'s ownership model inverted (h), `latch()`'s position in
`Simulation::step` moved a second time (e), R3's test mechanism changed. Plus one fold-in pass (the
`wrench_b()`-write prohibition and the `derivative()`/non-finite-check correction).

**Round 2**, scoped explicitly to the sections that had actually changed (Design, (e), (h), the
Acceptance table, the Draft ADR — not a full re-review), found six more failed assumptions, all
independently reproduced and resolved — see "Plan critique round 2 — resolved" above. Two were
structural: canonical storage could be read uninitialized via a code path (R4's own step()-time
write) this same design sanctions, and the design as written could not have passed its own R10
acceptance evidence (a contradiction between where the init check ran and what `Simulation::
for_testing` needed to skip). Both are now fixed. **No further Codex critique is scheduled; this
document awaits Joe's decision next.**
