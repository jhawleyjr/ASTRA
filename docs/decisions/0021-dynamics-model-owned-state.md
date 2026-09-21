# ADR-0021: Core dynamics interface with model-owned continuous states (supersedes ADR-0003's fixed-layout decision)

Status: **accepted**, 2026-09-20
Owner: Joe
Related/supersedes: supersedes ADR-0003's "the state is 13 doubles in a fixed layout" decision. Does
**not** supersede ADR-0003's frame/position-representation content (Earth model still owns the meaning
of `p_n`; `q_nb` conventions per ADR-0001 unchanged) — only the fixed, core-imposed size and layout.
ADR-0003 itself already anticipated this ("Mass as state ... becomes an ADR that supersedes this
one"). Elaborates Joe's intent document §1/§2/§3(items 2,3)/§4/§5(items 1,2,4)/§6 (sequencing).

## Context

The fixed-13-state assumption is shallow. `Integrator` is already generic over a `StateDerivative`
concept and only the *type* `StateVector` is pinned. No plugin (9 shipped physical plugins, 2
test-only aero/propulsion stubs, `nesc_aero`, `nan_wrench`) indexes the state directly — every one
uses `VehicleState`'s named accessors, with one cosmetic exception. `Model` has no hook to declare a
continuous state or return its derivative.

Textbook basis: Nutaro, *Building Software for Simulation*, §5.5 — `ode_system` exposes `numVars()`,
`init(double* q)`, `der_func(const double* q, double* dq)`; `ode_solver` holds an `ode_system*` and
integrates N doubles it knows nothing about, sizing its work arrays once in its own constructor from
`numVars()` — the "sized once, generic integrator" shape this decision adopts, and (crucially for the
coupling design below) `der_func` is handed the *whole* trial state `q`, not a per-subsystem slice.
Zipfel App. E.2.3/E.2.5/E.3.1: each module "carries out the table look-up, and integrates the state
variables" and reads any input it needs from a shared named module-variable bus. Stevens & Lewis's
F-16 model carries an engine power-lag state (`POW = X(13)`) beyond rigid-body motion — a model-owned
state is not a hypothetical need.

## Decision

**A `DynamicsModel` interface** (`core/include/astra/interfaces/dynamics_model.hpp`, Nutaro's
`ode_system` ASTRA-flavored):

```cpp
class DynamicsModel {
 public:
  virtual ~DynamicsModel();
  [[nodiscard]] virtual int state_count() const noexcept = 0;
  virtual void initial_state(double* q) const noexcept = 0;
  virtual void derivative(double t, const double* q, double* dq) const noexcept = 0;
  virtual void post_step(double* q) const noexcept {}  // default: no-op
};
```

`RigidBody6DofDynamics` ships in `core/` as the one implementation, for today's 13-double
`p_n/v_b/q_nb/omega_b` block, with arithmetic that is a **byte-for-byte port** of today's
`Eom::operator()` (R1). `post_step` becomes this class's own override of what
`post_step_renormalize` does today.

**`VehicleState` retypes** its internal storage to `Eigen::Map<const Eigen::Vector<double,13>>`,
constructed from a raw pointer, so it can view any 13-double slice of a runtime-sized buffer, not
only an owning stack `StateVector`. Every accessor's name/return value (`p_n()`, `v_b()`, `q_nb()`,
`omega_b()`) is unchanged, and no plugin call site changes (plugins only ever call these accessors;
none constructs a `VehicleState` itself). `vector()`'s return type changes from `const StateVector&`
to the same Map type by value. One existing test's address-identity assertion
(`tests/unit/kernel/vehicle_state_test.cpp:21`) is rewritten to check value equality instead, since
address identity was never a contract `VehicleState`'s own header promised ("read-only, non-owning
view", not "identical object").

**Model-owned continuous states.** `Model` gains five new virtuals, each defaulted so every existing
subclass keeps compiling and behaving identically without any edit:

```cpp
[[nodiscard]] virtual int state_count() const noexcept { return 0; }
virtual void initial_state(double* q) const noexcept {}
virtual void integrate_state(double t, const VehicleState& vs, const EnvironmentSample& env,
                              double* dq) const noexcept {}
// Phase one (Vehicle::init()): Vehicle itself calls initial_state() directly against a model's own
// combined-state-buffer slice to seed it -- a model never holds that mutable pointer. This hook
// hands the model only its own READ-ONLY stage-buffer slice, to read sub-stage-exact thereafter
// from wrench_b()/integrate_state(). A model with state_count() == 0 never has this called.
// Init-time only: may throw.
virtual void bind_state(const double* own_stage_slice) {}
// Phase two (after every model's phase one has run): resolves reads of a peer model's published
// state signal(s) via registry.find(...), caching the result as a member. Init-time only: may
// throw -- a model requiring a peer signal that find() returns nullptr for must throw a typed
// config error naming it.
virtual void resolve_peers(const SignalRegistry& registry) {}
```

`integrate_state()` takes the sub-stage `VehicleState` directly — the identical `(vs, env)` shape
`WrenchSource::wrench_b()` already has — so a model's own continuous-state derivative reads the
rigid-body block the same way any wrench computation already does (an engine spool-lag rate
depending on airspeed reads `vs.v_b()` directly, no separate plumbing). `dq` is the write target for
this call only, into whichever of `Integrator`'s own sized-once scratch buffers this RK4 stage is
filling.

**A model's own state slice, and any other model's, is readable sub-stage-exact from both
`integrate_state()` and `wrench_b()`.** `Vehicle` allocates one contiguous buffer of size
`13 + sum(model states)` once (never resized in `step()` — R2): the **combined state buffer**, the
actual `y` `Vehicle` advances frame to frame. Separately, `Vehicle` owns a second, identically-sized
**stage buffer**, also allocated once at init: at the top of the per-stage derivative functor, before
any `wrench_b()`/`integrate_state()` call for that stage runs, `Vehicle` copies that stage's own
trial state into this stage buffer. Every state-owning model resolves, once at init via
`bind_state()`, a `const double*` into its own slice of the **stage** buffer. Because the stage
buffer's address never moves after init but its contents are refreshed every sub-stage, a model
dereferences that pointer from inside either `integrate_state()` or `wrench_b()` and reads its own
current sub-stage-exact value — no `wrench_b(vs, env)` signature change (ADR-0020 keeps it fixed).
**No model ever holds a mutable pointer into the state buffer**: the combined buffer's one write path
stays exactly where it already was — `initial_state()`, called by `Vehicle` itself at init, and the
RK4 weighted combination `Integrator` performs.

**Cross-model reads use the same mechanism, through `SignalRegistry`, sub-stage-exact.** A
state-owning model additionally registers each element of its own stage-buffer slice with
`SignalRegistry::add()`, pointing at the *live* stage-buffer address, not a separately latched copy
(`SignalRegistry::add()`'s own contract requires only that the target outlive the registry, which the
stage buffer, allocated once at init, satisfies). Any other model resolving that name via
`SignalRegistry::find()` gets the identical pointer, and therefore the identical sub-stage-exact
freshness. This is distinct from `env.wind_n`'s own frame-latching, unchanged: `wind_n` is an
externally sampled environment field, latched once per frame before the wrench stage begins, never a
model-owned continuous state whose storage this decision controls.

**Two-phase init**, since a single per-model init pass cannot support mutual cross-model state
dependence (a model resolving a peer's not-yet-published signal gets `nullptr` regardless of load
order): **phase one** — for every model, in order: load, `init()`, `bind_inputs()` (ADR-0017,
unchanged), record `state_count()`, check `requires_environment()`; once every model's count is
known, allocate both buffers, then for each state-owning model call `initial_state()` (seeding its
combined-buffer slice) and `bind_state()` (handing it its own read-only stage-buffer slice) and
publish its state signals. **Phase two** — call `resolve_peers()` for **every** model (state-owning
or not, since a pure `WrenchSource` may still need a peer's state), only after phase one has finished
publishing every state-owning model's signals for every model. Two models with mutual state
dependence both resolve successfully in either load order.

**What stays out of scope: algebraic loops between model *outputs*, not state reads.** A model's own
state, once written into the stage buffer for this trial, is a known, fully-determined number —
reading it from another model's `wrench_b()`/`integrate_state()` is an ordinary dereference, not a
simultaneous-equation solve. What remains unsupported is two or more models' *outputs* (typically
wrenches) depending on each other within the same evaluation, needing a fixed-point solve —
`Vehicle`'s fixed evaluation order (copy trial state into the stage buffer; accumulate the rigid-body
wrench from every `wrench_b()`; evaluate every state-owning model's `integrate_state()`) already
prevents this class of loop from being expressible by construction. This matches Nutaro's own
`der_func(q, dq)` shape exactly: the solver hands the system function the whole trial state, not a
per-subsystem slice.

**Stage buffer invariant: outside a derivative evaluation, the stage buffer equals the accepted
state.** `Vehicle::integrate()` gains one unconditional post-step: after a wrench-fault rollback or a
completed `integrator.step()` (whichever happened this frame), and after `post_step_renormalize()`
has run, `Vehicle` copies the combined state buffer's contents into the stage buffer — a plain copy,
run unconditionally (halted frame or not), matching renormalization's own "invariant, not suspended
by a halt" framing. `Vehicle::init()` performs the identical copy once, at the end of phase one, so
the invariant already holds before frame 0's own `step()` ever runs. The per-frame order (`AGENTS.md`
/ `docs/architecture.md` §5) becomes: environment → transport exchange → vehicle wrenches → EOM →
integrator → post-step (renormalize, **then sync stage buffer to accepted state**) → logger.

**`wrench_source.hpp`'s purity-contract amendment**, required as part of this decision: immediately
after "A conforming implementation MUST therefore be a pure function of (vs, env) with no hidden
per-call state," add — "As of ADR-0021, a read-only binding into `Vehicle`'s own stage buffer (own
state slice via `bind_state()`, or a peer's via `resolve_peers()`) is part of that pure function's
actual domain, not an exception to it: the function is pure over the complete per-stage trial
snapshot `Vehicle` prepares before any call this stage — `vs`, `env`, and every resolved stage-buffer
binding together — not over `(vs, env)` alone. Every frame-latched input (`env.wind_n`, every
ADR-0017 write-seam value) remains identical across all four of one frame's calls, exactly as before
this ADR. A stage-buffer binding varies by sub-stage, by design, tracking that stage's own trial
state, but stays identical between two calls at the same sub-stage of the same frame. No call may
write through a stage-buffer binding — the same read-only-from-`wrench_b()` prohibition the ADR-0017
write-seam text already establishes for its own handles."

**Integrator functor contract** changes from return-by-value (`F(t,y) -> StateVector`) to
write-into-caller-provided-buffer (`F(t, y, dy) noexcept`, fixed-extent views over the combined
state) — matching Nutaro's own `der_func` shape, now applied at the integrator level too, not just
the model level. `Integrator` owns its own pre-sized scratch buffers for `k1`..`k4` and RK4's
intermediate trial states, sized exactly once at init, never resized inside `step()`. `Vehicle`'s own
pre-step rollback copy is likewise a copy into a `Vehicle`-owned scratch member sized once at init.
The combined state buffer is a dedicated wrapper (not a bare `std::vector<double>`): it owns a
`std::vector<double>` sized exactly once in its own constructor and exposes only `data()`/`size()`/
the arithmetic the integrator needs — no `resize()`/`push_back()`/`insert()` in its public interface
at all, so "never resized after construction" is enforced by what the type can do.

**R1 (bit-identity).** A frozen golden, captured once from the **pre-change** build against a
representative set of sampled initial states through a complete integration step (RK4, including
`post_step` renormalization), with bit patterns (`std::bit_cast<std::uint64_t>` per `double`) stored
under `tests/golden/` — never a private reference implementation or "the pre-change code itself if
still available" compiled alongside the post-change build, which cannot independently confirm what
the pre-change build actually produced. The golden ships with an accompanying manifest: the exact
pre-change revision (git SHA), the exact sampled initial-state bit patterns used, the capture harness
and its own configuration, compiler/dependency versions and target, and the compiler flags/
optimization level — all checked in alongside the golden, both requiring Joe's `ASTRA_TOUCH_GOLDENS`
authorization to create. The post-change test drives the identical samples through the post-change
`RigidBody6DofDynamics`/rewritten `Integrator::step` and asserts representation-level equality
against this golden. In addition to, not instead of, the existing NESC/determinism suite unmodified
under all four presets.

**R2 (no allocation).** The combined state buffer, and `Integrator`'s and `Vehicle`'s own scratch
buffers, are each sized exactly once, in `Vehicle::init()`; none is resized or reallocated in
`step()`. `tests/physics/no_alloc_test.cpp` gains a case with at least one state-owning test model.

**R3 (mass as state) — explicitly deferred, not decided here.** This decision makes mass-as-state
*representable* without deciding whether v1 uses it. `MassPropertiesModel::get()`'s signature and
`Eom::begin_frame()`'s per-major-frame caching would need their own explicit decision if a future
record adds a mass-owning state.

**R4 (trim/linearize, PR 5-7).** `Simulation.derivative(state)`'s Python contract changes from
"exactly a 13-vector" to "a length-N vector with named slices for the rigid-body block plus each
model's own labeled slice." PR 5 (not yet built) is specified against this contract from its own
start — why this decision's implementation is sequenced before PR 5.

**Health/config.** `HealthMonitor::check_state` becomes: check the rigid-body block's four segments
exactly as today, plus a generic finiteness check over every other declared state slice. `initial.state`
config gains a place for each state-owning model's own initial values. Python exposes state as a
length-N vector with named slices instead of module-level fixed offsets.

**Sequencing:** (a) ADR-0020 implemented first — small, unblocks PR 4 sooner; (b) PR 4 rebuilt on the
corrected interface; (c) this decision's dynamics/state generalization implemented before PR 5
begins, since PR 5-7 bind the state to Python.

## Rejected alternatives

- **Keep `StateVector` fixed at 13 and bolt extra per-model states on as separate parallel vectors,
  each with its own integration.** Rejected: breaks RK4's requirement that all coupled state advance
  through one consistent set of k1-k4 sub-stage evaluations; two separate integrators cannot do that
  consistently.
- **`Eigen::VectorXd` as the runtime state type.** Rejected: heap-allocates on construction and, via
  Eigen's expression templates, can allocate temporaries on ordinary arithmetic even when "sized
  once" unless every operation is hand-written to avoid them.
- **Make the five new virtuals mandatory (no default) on every `Model`.** Rejected: breaking change to
  every existing plugin for zero benefit over a defaulted, zero-returning virtual that costs nothing
  to a plugin that doesn't need it.

## Consequences and validation

Costs: `kCoreApiVersion` bump (`Model`'s vtable gains five new virtuals, even defaulted; every plugin
recompiles); real changes to `state.hpp`, `vehicle_state.hpp` (a genuine constructor/return-type
retype), `eom.hpp/.cpp`, `vehicle.hpp/.cpp` (two-phase init, the stage buffer and its sync,
`bind_state()`/`resolve_peers()`), `simulation.hpp/.cpp`, `health_monitor.hpp/.cpp`, `integrator.hpp`
(functor contract change, owning its own scratch buffers), and `wrench_source.hpp`'s own
documentation; `tests/unit/kernel/vehicle_state_test.cpp`'s address-identity assertion rewritten;
Python `_core.cpp`'s `STATE_SIZE`/offset constants need a length-N-aware replacement before PR 5; a
new frozen golden under `tests/golden/`, requiring Joe's authorization to create. Benefits: any
future vehicle model (engine spool dynamics, actuator lag, flex modes, fuel burn) can own its own
continuous state, integrated at full sub-stage fidelity, with zero further core interface change; any
`wrench_b()`/`integrate_state()` can read any other model's or the rigid body's live sub-stage state,
not only its own. Still not delivered: an algebraic loop between two or more models' own outputs
within one evaluation. Migration/rollback: fully reversible pre-any-external-consumer; the
`kCoreApiVersion` bump is the existing precedented mechanism.

Acceptance: the frozen-golden bitwise-equality test (R1); existing NESC/determinism goldens pass
unmodified under all four presets; `no_alloc_test.cpp` extended with a state-owning test model,
covering the integrator's, `Vehicle`'s combined-state, and `Vehicle`'s stage-buffer scratch (R2); a
test proving a state-owning model's own value, read through both `bind_state()` and a peer's
`SignalRegistry`-resolved read, is identical at every RK4 sub-stage; a test proving two models with
mutual state dependence both resolve successfully in `resolve_peers()` regardless of config order; a
test proving the stage buffer equals the combined state buffer immediately after `integrate()`
returns, both on a normal and a halted frame, and at the end of `init()` before frame 0; a test
proving duplicate published state names and missing required peers throw recoverable `ConfigError`s,
not `std::terminate()`; `wrench_source.hpp`'s amended purity-contract text lands as part of this
decision's own diff; a dedicated implementation plan's own full acceptance table at implementation
time.

## Approval

**Accepted by Joe, 2026-09-20**, after substantial revision through this session. Original draft:
one Codex plan critique (three high, one medium finding — slice-only `integrate_state()` access
didn't preserve coupled RK4 evaluation; sizing the combined buffer once didn't by itself make RK4
allocation-free; `VehicleState`'s claimed-unchanged constructor would have dangled; R1's proposed
evidence couldn't prove bit-identity) and two re-verification passes (catching, in turn, a
representation-equality gap and a missing `EnvironmentSample` parameter) — all fixed, closing to
"own slice only" state access plus a frame-latched `SignalRegistry` bus for cross-model coupling.
**Reopened by Joe's own instruction** before deciding: `integrate_state()` takes the sub-stage
`VehicleState` directly; any model's state, not only its own, is readable sub-stage-exact via the
stage-buffer mechanism above, replacing the frame-latched bus. A further focused Codex re-verify
found two high findings (the stage buffer went stale outside a derivative call; init-time resolution
alone couldn't support mutual cross-model dependence) and one medium (`wrench_source.hpp`'s purity
text unamended) — fixed per Joe's follow-up instruction, alongside changing R1's evidence to a frozen
golden. A second focused re-verify against exactly those four deltas found one high finding
(`bind_state()`/`resolve_peers()` were `noexcept` but needed to throw on a config error) and one
medium (the golden's capture needed a fuller reproducibility manifest) — both fixed directly. Final
change at acceptance: `bind_state()` takes only a read-only stage-buffer pointer; `Vehicle` itself
seeds a model's combined-buffer slice via `initial_state()`, so no model ever holds a mutable pointer
into the state buffer. Full critique/revision record, all findings and fixes:
`docs/plans/m3-framework-realignment.md`. `touch-goldens` armed by Joe for this record and its R1
golden.
