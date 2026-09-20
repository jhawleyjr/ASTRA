# ADR-0017: Control-input write seam — `InputRegistry`, zero-order hold, per-vehicle

Status: **accepted**, 2026-09-19
Owner: Joe
Related/supersedes: elaborates ADR-0011 (per-vehicle ownership: `InputRegistry` is owned one-per-
`Vehicle`, the same way each vehicle's `SignalRegistry` namespace already is) and ADR-0016 (the
zero-order-hold and stage-purity precedent this decision extends from wind to control inputs
generally). Bumps `kCoreApiVersion` per the existing policy `core_api.hpp.in` documents (elaborates,
does not supersede, versions 2/3/4's own bumps). Drafted with `docs/plans/pr2-write-seam.md`,
revised through two Codex plan critiques (twelve findings, then six more, all fixed) and two rounds
of Joe's own review (six findings, then five more, all fixed), plus one final fold-in (entries
default to a quiet NaN, not an unspecified value) accepted without a further review round, before
this record.

## Context

`WrenchSource::wrench_b()` reads a plugin's own state; nothing in the framework lets code outside
that plugin set one of its values by name. Three callers need to over the project's life: a config
literal (open-loop, this PR), Python trim/linearize (PR 5), and M4's transport/`ActuatorModel`
later. All three must write through one mechanism, or PR 5 and M4 each reinvent it.

Stevens & Lewis (*Aircraft Control and Simulation*, 3rd ed.) separate the control vector from the
state vector explicitly: §2.5 (pp. 109–110), Eq. (2.5-3)/(2.5-4), the control vector `Ū` is never
folded into the state vector `X`. §3.4 (p. 170) states the general justification for holding a
control input constant across an integration step: "the time step must be made small enough that
the control input can be approximated by a constant value during any interval `kT` to `(k+1)T`."
Their own F-16 example (§3.5, Fig. 3.5-2, pp. 182–183) implements the separation exactly the way
this decision does: the derivative subroutine `F(TIME,X,XD)` never takes controls as a parameter;
they arrive through `COMMON/CONTROLS/THTL,EL,AIL,RDR`, written externally by the trim routine or
driver before `F` is ever called.

Zipfel (*Modeling and Simulation of Aerospace Vehicle Dynamics*, 4th ed.) describes the same
separation at the architecture level. Fig. 9.39 (p. 364), the HYPER5 module diagram, shows
"Control," "Aerodyn," and "Propul" as separate modules connected only by named signal arrows, never
by direct calls into one another's internals. App. E.2.3 (p. 599) states the module-variable
contract this decision's read side already follows: "the modules... should be public member
functions of the vehicle classes. Their interfaces, the module-variables, would be stored in
protected data arrays." App. E.2.5 (p. 599) states the shape this decision's write side adopts for
two instances of one plugin sharing a name: "a global communication bus should enable the transfer
of data. Each vehicle should be able to publish and subscribe to any of the module-variables" — one
named slot, more than one subscriber being the ordinary case, not an edge case.

## Decision

A new class, `InputRegistry` (`core/include/astra/signals/input_registry.hpp`), is `SignalRegistry`'s
mutable counterpart, owned one per `Vehicle`. Storage is registry-owned: `add(SignalRegistry&, name,
unit)` returns a `const double*` into `InputRegistry`'s own canonical storage for `name` — a plugin
stores that pointer as its own member and dereferences it inside `wrench_b()`. On the *first* `add()`
for a given name, the same pointer is also registered read-only under the identical name in the
vehicle's `SignalRegistry` (visible as `v0.controls.elevator`, matching the existing read-side
convention). A *second* `add()` for an already-registered name — the case of one plugin
instantiated twice under one vehicle, e.g. left/right engines of one turbofan — returns the same
pointer instead of colliding: both instances see one shared value by default (a symmetric throttle
command), with per-instance naming (e.g. `controls.throttle.left`/`.right`) available whenever a
vehicle actually needs differential control; a mismatched unit under a repeated name is the actual
error case. Every entry's canonical value, pending value, and freeze()-recorded value
default-initialize to a quiet NaN, never an unspecified value: "never written" and "written
non-finite" are deliberately the same, loudly reported state to every mechanism below, never a
silent read of undefined memory.

`WrenchSource` gains one new virtual, `bind_inputs(InputRegistry&, SignalRegistry&)`, default empty
body. `Vehicle::init()`'s existing `WrenchSource`-discovery pass (already used for `requires_
environment()`) calls it once per discovered `WrenchSource`, immediately after that model's own
`Model::init()`, while the vehicle's `SignalRegistry::PrefixScope` is still open, so every name
lands correctly namespaced. `Model::init()`'s own signature is unchanged — the seam attaches only to
`WrenchSource`, because `Model::init()` is also the entry point for shared environment models
(`EarthModel`, `GravityModel`, `AtmosphereModel`, `WindModel`), loaded outside any vehicle by
`Simulation::from_config`, which have no use for a write seam and no vehicle namespace to register
one under.

`InputRegistry::write(name, value)` stages a pending value by name (throwing on an unregistered
name). `InputRegistry::find(name)` resolves a name once to a repeatable `InputHandle`, whose own
`write(double)` stages a pending value on every later call without re-resolving by name — the
write-side counterpart to the read-side `const double*` a plugin already holds, for a caller (a
future M4 `ActuatorModel`) that writes from inside its own `Model::step()`. `wrench_source.hpp`'s
doc comment is amended: a conforming `WrenchSource` may read any input through its own `const
double*`, but must never write through an `InputHandle` from inside `wrench_b()` or `reset()` — only
`step()` is a sanctioned write point. `InputRegistry`'s own entries live in a `std::deque` (not a
`std::vector`), so a `const double*`/`InputHandle` handed out at any point remains valid for the
`Vehicle`'s entire lifetime, including past the `std::move(vehicle)` `Simulation::from_config`/
`for_testing` perform.

`InputRegistry::latch()`, called once per vehicle per major frame by `Simulation::step` — after
`vehicle.step(ctx)`, before `vehicle.integrate(...)`, matching `docs/architecture.md` §5's own
canonical order ("...transport exchange if due (send truth/sensor frame, **receive commands**) →
integrator step...") — commits every entry's pending value into its canonical storage uniformly,
never short-circuiting on the first fault (the same reasoning ADR-0016 already applies to
`check_wrench`'s own per-stage evaluation). It never commits a non-finite pending value, warn or
halt alike, and returns the first non-finite entry's name (first-fault-wins). `Simulation::step`
reports that name through a new `HealthMonitor::check_input`/`HealthCheck::kNonFiniteInput` (a new
`health.input` signal, a new `sim.health` config key `"input"`, default `"halt"`), at most once per
vehicle per frame — the same calling shape `report_out_of_domain` already uses (the caller evaluates
the predicate; `HealthMonitor` holds no registry pointers of its own).

Zero-order hold across a frame's four RK4 sub-stages follows from `latch()` running exactly once per
frame: a plugin's own member never changes mid-integration, only between frames.

Sequencing at init lives in `Simulation::from_config`, not `Vehicle::init()` (whose actual signature
never receives the vehicle's own config subtree, so it cannot read `vehicle.initial_controls` at
all): apply each declared input's config literal by relative name (e.g. `controls.elevator`, no
`v0.` prefix, ordinary nested YAML under a new `vehicle.initial_controls` key, read through
`Config`'s existing dot-navigation — no new parsing code); validate every entry has a finite pending
value (`ConfigError` naming its dotted path if not — never a silent zero); call `InputRegistry::
freeze()`, which closes registration and seeds canonical storage from each entry's validated pending
value — the one unconditional, init-time commit that makes canonical storage meaningful before
frame 0 ever runs, independent of whichever per-frame `latch()` happens to run first. `Simulation::
for_testing` performs none of these three steps, matching its own existing, documented list of
`from_config`-only validations it does not mirror; its vehicles' entries stay at their NaN default
until a test's own `write()`, and a forgotten one fails loudly at that vehicle's first `latch()`,
naming the input, exactly as a genuine bad write would.

`InputRegistry::reset()` restores every entry to the value recorded when `freeze()` last ran, or to
its NaN default if `freeze()` never ran. `Vehicle::reset()` calls it alongside delegating to every
model's own `reset()`, in either order — safe because `wrench_b()` and `reset()` are both prohibited
from writing through an `InputHandle` at all, so nothing can interfere with the restore regardless
of ordering.

PR 5's `Simulation::derivative()` evaluates whatever is currently latched in each `WrenchSource`'s
own canonical storage — the only mechanism possible, since `wrench_b()` takes no `inputs` argument.
`docs/architecture.md` §8's already-documented Python-facing `derivative(state, inputs)` signature
can still be honored at the Python boundary while its internal implementation does `write()` for
each declared input, then one `latch()`, before evaluating at `state` — PR 5's own choice whether to
keep that explicit parameter or require its own caller to `write()`+`latch()` directly; `latch()` is
public and reachable independently of `Simulation::step()` either way. Whichever path PR 5 takes, it
must check `latch()`'s returned name and raise rather than continue: a non-finite write leaves the
previous, last-good value latched, so `derivative()` would evaluate at the wrong point and return a
plausible-looking but wrong residual — a silent wrong answer, worse than a loud failure, for a
numerical method whose soundness depends on evaluating exactly the point requested.

`WrenchSource` gaining a new virtual changes that interface's vtable layout; `kCoreApiVersion` bumps
4 → 5, the same reasoning as version 2's `requires_environment()` addition and version 4's own
`in_domain` addition (both new-virtual additions — the closer precedent here, not a changed-
parameter-list one). `Model::init()` itself, and every non-`WrenchSource` plugin, is unaffected.

## Rejected alternatives

- **Change `WrenchSource::wrench_b()`'s signature to take controls directly.** `PLAN.md`'s own PR 2
  line pins the external-write direction; S&L's own F-16 example (§3.5) shows it is a well-
  precedented design, not merely a constraint accepted for convenience.
- **A mandatory code-level default at `add()` time.** Reintroduces the silent-default ambiguity
  ("is `0.0` a deliberate trim value or a forgotten config?") this design exists to prevent, and
  works against M3 PR 1's own established "numbers in YAML not code" direction.
- **Merge write capability into the existing `SignalRegistry`.** `SignalRegistry` does not freeze
  its *values* — `freeze()` closes registration only, and every registered entry's pointed-to value
  keeps changing every frame. The real reason to keep the two types separate is who is allowed to
  write through the pointer: every `SignalRegistry` entry is written only by the model that
  registered it (a self-publish); this seam's entries are written by code other than the
  registering model. Merging them would erase that boundary from the type system. Matches Zipfel's
  own Table/Variable/Module split (App. E.2.3) — distinct classes for distinct concerns.
- **Add the `InputRegistry&` parameter to `Model::init()` directly.** `Simulation` owns the one
  shared `SignalRegistry`, and shared environment models call `Model::init()` outside any vehicle
  with no use for a write seam; changing the common signature would force every one of them to
  accept a parameter they cannot use.
- **Caller-owned storage, one name → one target (`add()` taking a `double* target` parameter).**
  Breaks the moment one vehicle loads the same plugin twice under different config (M3 PR 4's own
  two-engine `turbofan` shape): two different target pointers cannot both be "the" value under one
  `SignalRegistry`-visible name.
- **`N` targets per name (a broadcast copy on `write()`/`latch()`).** Duplicates what registry-owned
  shared storage already gives for free, while adding a real correctness risk shared storage
  structurally cannot have: a partially-applied `N`-way copy could let targets observably disagree
  even though they represent "the same" input.
- **`latch()` before `vehicle.step(ctx)`.** Bakes a one-frame delay into any `Model` writing through
  the seam from its own `step()` — a future `ActuatorModel`'s exact use case — the failure
  `docs/architecture.md` §5 already names: "model ordering is a classic source of one-frame delays
  that look like physics." Moved to run after `step()`, before `integrate()`, matching §5's own
  canonical order.
- **Config-literal application, validation, and `freeze()` inside `Vehicle::init()`.**
  `Vehicle::init()`'s actual signature never receives the vehicle's own config subtree, so it cannot
  read `vehicle.initial_controls` at all; `Simulation::for_testing` also calls this same
  `Vehicle::init()`, so a check placed there would incorrectly fire for `for_testing` too, which
  needs to skip it. Moved to `Simulation::from_config` instead.
- **An unspecified ("garbage") default for a never-written entry.** `Simulation::for_testing` skips
  `from_config`'s own validation by design, so a forgotten `write()` would otherwise read undefined
  memory with no backstop at all. A quiet-NaN default instead routes a forgotten write through the
  same `latch()`/`health.input` mechanism a genuine bad write already uses.

## Consequences and validation

**Benefits.** One write mechanism for config literals (this PR) and Python (PR 5), correctly
supporting a plugin instantiated more than once under one vehicle; zero change to `WrenchSource::
wrench_b()`'s or `Model::init()`'s signature; a documented, testable zero-order-hold guarantee with
no one-frame lag for a sanctioned step()-time writer; no reachable code path reads uninitialized
memory.

**Costs.** `WrenchSource` gains a new virtual (ABI break, `kCoreApiVersion` bump, every existing
plugin recompiles); one new kernel type (`InputRegistry`) plus a small handle type (`InputHandle`);
one new `HealthMonitor` check and signal; one amended `wrench_source.hpp` doc comment; one new,
additive `vehicle.initial_controls` config key.

**Known gap, disclosed not fixed.** `bind_inputs()` is attached to `WrenchSource` specifically,
because that is the only model kind this decision's own scope needs to serve. A transport writer or
`ActuatorModel` that is not itself a `WrenchSource` — a plausible shape, given `docs/architecture.md`
§7's own description of `ActuatorModel` transforming a command into an achieved position another
model then reads as its own input, not necessarily a wrench contributor itself — has no seam-
provided route to a write handle for a name it does not own. Even a writer that could reach `find()`
would depend on `vehicle.models`' load order (the owning model's `bind_inputs()` must already have
run), a dependency nothing here validates or enforces. Not exercised by this decision's own
acceptance evidence; not engineered around, the same posture ADR-0012 and ADR-0016 already took for
their own accepted, differently-shaped gaps. M4's own plan resolves this, with its own critique,
either by designing `ActuatorModel` as a `WrenchSource` or by extending the seam with a
non-`WrenchSource` binding route.

**Migration/rollback.** Fully reversible, single-PR change; no data or schema migration beyond the
additive `vehicle.initial_controls` config key.

**Acceptance.** `PLAN.md`'s PR 2 line; the assigned implementation plan's own acceptance table
(`docs/plans/pr2-write-seam.md`, R1–R13) at implementation time.

## Approval

**Accepted by Joe, 2026-09-19**, after two Codex plan critiques and two rounds of Joe's own review
(full record in `docs/plans/pr2-write-seam.md`), plus one final fold-in (the quiet-NaN default)
accepted without a further review round. `touch-goldens` armed by Joe for this file.
