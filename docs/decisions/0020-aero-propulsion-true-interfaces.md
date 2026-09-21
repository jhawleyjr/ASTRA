# ADR-0020: `AeroModel`/`PropulsionModel` as true interfaces (supersedes ADR-0018's `Inputs`/`final` parts)

Status: **accepted**, 2026-09-20
Owner: Joe
Related/supersedes: supersedes ADR-0018's `compute(const Inputs&)`/`final wrench_b()`/`final
requires_environment()` design and its two `Inputs` structs. Does **not** supersede ADR-0018's other
decisions: `AeroModel`/`PropulsionModel` still derive from `WrenchSource` (not `Model` directly);
the `kCoreApiVersion` bump precedent; the per-RK4-substage purity contract (ADR-0016, unchanged);
the AERORP/CG moment-transfer obligation on a concrete plugin (ADR-0018 (d), unchanged); the V→0
`wrench_b()`-level obligation (unchanged in spirit, now attached directly since `compute()` no
longer exists as a separate step). Elaborates ADR-0019 and Joe's intent document §1/§3(item 4)/§5
(item 3).

## Context

Joe's intent document, §1: "A header in `core/include/astra/interfaces/` declaring an abstract base
class: pure virtual functions, no data members, no implementation, no assumption about what a
concrete vehicle contains ... Core may offer shared math (air data, frames) as **utilities a model
may call**, never as a mandatory input struct or a `final` method that fixes behavior." Today's
`AeroModel::wrench_b()`/`requires_environment()` are `final` with real logic; `compute()` takes a
core-defined six-field `AeroInputs` struct — a founding-doc defect (original `docs/architecture.md`
§7, commit `301435d`, specified the same fixed struct), not PR 3 drift.

Textbook basis: Stevens & Lewis §2.5/§3.5's F-16 model takes coefficient dependencies `(alpha, beta,
M, h, delta_S, P, Q, R)` plus `alpha_dot, beta_dot` for moments — already more than today's six-field
`AeroInputs` (no `alpha_dot` field exists). Zipfel App. E.2.3/E.2.5/E.3.1: each module "carries out
the table look-up, and integrates the state variables" and reads any input it needs from a shared
named bus — no module receives a fixed input struct. Existing in-tree precedent this design already
works: `tests/plugins/nesc_aero/nesc_aero.cpp` derives directly from `WrenchSource` (not through any
`Inputs`-struct-taking intermediate), reads whatever it needs from `(VehicleState, EnvironmentSample)`
and its own config-resolved members, and its force/moment forms are algebraically finite at
`v_rel_b = 0` with no explicit `isfinite` guard at all.

## Decision

`AeroModel`/`PropulsionModel` become minimal marker interfaces, with exactly one deliberate,
non-empty override each:

```cpp
class AeroModel : public WrenchSource {
 public:
  // Non-final: a concrete plugin MAY override this if it genuinely needs something else. An
  // author who never touches it gets today's safe default without having to know to ask for it.
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};
class PropulsionModel : public WrenchSource {
 public:
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};
```

No override of `wrench_b()`, no `final` anywhere, no `compute()` step, no `AeroInputs`/
`PropulsionInputs` struct in core. A concrete plugin overrides `WrenchSource::wrench_b(const
VehicleState&, const EnvironmentSample&) const noexcept` directly — the pattern `nesc_aero`/
`nan_wrench` already use. `requires_environment()` is *not* left at `WrenchSource`'s own bare default
(`{}`, nothing required) — that would silently reopen the "author forgets to declare atmosphere" gap.
These two classes exist for type identity plus this one safe-default override — `Vehicle::init()`'s
existing discovery pass can still recognize "this is an aero/propulsion contribution" for logging,
diagnostics, and any future envelope-monitor hook.

**Air-data math becomes core utilities**, not a mandatory struct or sealed method: new
`core/include/astra/util/air_data.hpp`, free functions — `relative_velocity_b(vs, env)`,
`true_airspeed(v_rel_b)`, `angle_of_attack(v_rel_b)`, `sideslip(v_rel_b)` (the same `atan2`-based,
V→0-safe forms ADR-0018 already derived, relocated only), `dynamic_pressure(rho, v_true)`,
`mach(v_true, speed_of_sound)`. A concrete plugin composes whichever it needs and may define its own
plugin-side `Inputs`-shaped struct plus a `compute()`-style split purely as its own convention for
hand-built-fixture testability — the template demonstrates this pattern; core no longer prescribes
it. `air_data::all_finite(...)` is available as a recommended, not enforced, isfinite utility.

**Core-side environment finiteness check, closing the no-silent-NaN gap the removed `final` guard
used to cover — Joe's own addition, not merely disclosed as an accepted cost.** `Vehicle::integrate()`'s
per-stage derivative functor already builds `stage_env`, the `EnvironmentSample` every `wrench_b()`
call that stage will see, before its `wrench_sources_` loop runs. A new
`HealthMonitor::check_environment(const EnvironmentSample&, source names, ctx, vehicle_index)`
(mirroring `check_wrench`'s shape: per-frame path, no allocation, no exceptions, a configured
`sim.health` action) runs there, immediately after `stage_env` is built and strictly before the first
`wrench_b()` call of that stage — checking `wind_n` when wind is configured and every populated
`atmosphere` field when atmosphere is configured for NaN/Inf, naming the wind or atmosphere plugin
path on a halt. This closes `EnvironmentSample`'s own two already-disclosed gaps
(`environment_sample.hpp`'s `wind_n`/`atmosphere` comments: "HealthMonitor has no environment check
... it needs to validate EnvironmentSample as a whole") directly, confirmed unaddressed by
`Simulation::step`'s existing post-step altitude-range check (`simulation.cpp`), which validates
altitude range only, never field finiteness, and runs once per frame after `integrate()`, not per
sub-stage before `wrench_b()`. Because this check runs core-side, before any plugin's `wrench_b()`
executes, the no-silent-NaN guarantee for environment-sourced corruption (the R-001 class of defect:
an infinite `speed_of_sound_m_s` producing a finite-looking `mach`) no longer depends on any
individual plugin author remembering to check raw environment fields themselves — restored by an
explicit core-owned check, with nothing `final` anywhere in the interface. What remains, narrower
than before: a plugin's own *state*-derived or self-cached corruption is still uncatchable by any
compiler-enforced mechanism without reintroducing `final`; mitigated by process (`air_data.hpp`'s
header comment and `aircraft/_template`'s own stubs cite R-001 by name).

This decision does **not** add any continuous-state-declaration capability to `AeroModel`/
`PropulsionModel` itself — that arrives for every `Model` subclass once ADR-0021's hooks land
(defaulted to zero states until then). Removing the sealed `AeroInputs` struct does **not**, by
itself, make the dropped `Cmadot` (alpha-rate damping) term representable: no available snapshot
input provides `alpha_dot`, and deriving it by finite-differencing across `wrench_b()` calls would
violate that function's own documented purity contract. Genuinely supporting `Cmadot` needs
ADR-0021's model-owned continuous state (a lagged/filtered alpha state with its own derivative
equation) or a deliberate decision to keep neglecting it.

## Rejected alternatives

- **Keep `AeroInputs`/`PropulsionInputs` but make them extensible** (a variant or name-keyed map).
  Rejected: reintroduces a name-keyed lookup on the per-frame path, the exact anti-pattern
  `SignalRegistry`/`InputRegistry`'s own convention forbids, and still bakes core opinion about input
  shape into a vehicle-agnostic interface.
- **Keep `compute()` as a separate step, made generic via a template parameter.** Rejected: templates
  break `Model`'s runtime-dispatch story and the Python-trampoline path; a compile-time-generic
  `compute()` cannot be called through the runtime `WrenchSource*` `Vehicle` holds.
- **Keep the sealed `final` isfinite guard exactly as-is; a vehicle needing an unsupported input
  derives from `WrenchSource` directly, bypassing `AeroModel`/`PropulsionModel` entirely.** Rejected:
  relocates the cost rather than removing it — the next vehicle needing a differently-shaped control
  set or a raw atmosphere field hits this wall immediately.
- **A new `HealthMonitor` check specific to aero/propulsion inputs**, instead of a general
  `EnvironmentSample`-level check. Rejected: `EnvironmentSample`'s own disclosed-gap comment already
  calls for the general capability, not an atmosphere-specific bolt-on; the general form also covers
  every other `WrenchSource` that reads `env.atmosphere`/`env.wind_n` directly, not only aero/
  propulsion.

## Consequences and validation

`kCoreApiVersion` bump (settling vtable shape for `AeroModel`/`PropulsionModel`, same reasoning
ADR-0018 already used). Benefit: any future vehicle's aero/propulsion model reads exactly the inputs
already available to it (any raw environment field, any bound control, any member it caches itself,
and — once ADR-0021 lands — its own state slice) with zero further core interface change. Costs:
`aero_stub`/`propulsion_stub` rebuilt to override `wrench_b()` directly; `docs/architecture.md` §7's
code sketch and surrounding prose, and §18, rewritten; the "hand-built-fixture, no-simulator"
testability promise is preserved as a documented plugin-side convention, not a core guarantee; a new
`HealthMonitor::check_environment` method and its call site in `Vehicle::integrate()`, plus plumbing
for `Vehicle` to know the atmosphere/wind plugin path(s) to name on a halt. Migration/rollback: fully
reversible, no external plugin exists yet; `docs/plans/pr4-notional-aero-turbofan.md` is superseded —
its frame-conversion math, S&L citation, and dropped-terms analysis remain reusable once PR 4 is
rebuilt against this interface.

Acceptance: rebuilt `aero_stub`/`propulsion_stub` pass PR 3's existing integration-test pattern under
the new interface; `nesc_aero`/`nan_wrench` continue to pass unmodified; `AeroModel`/`PropulsionModel`'s
non-final `requires_environment()` default is unit-tested to still return
`{wind: false, atmosphere: true}` for a plugin that doesn't override it; `air_data.hpp` and both
template stubs cite R-001 by name; a new test proves a non-finite `wind_n`/`atmosphere` field is
caught by `check_environment` before any configured `wrench_b()` executes that stage, matching R-001's
own scenario; full gate.

## Approval

**Accepted by Joe, 2026-09-20**, with one addition beyond the draft: the core-side
`check_environment` finiteness check above, so the no-silent-NaN guarantee ADR-0018's `final` used to
provide survives this decision's removal of `final`. Prior to Joe's decision: one Codex plan critique
(one high finding — the removed isfinite guard's framing as merely "detected late" understated a
permanent-non-detection regression, fixed by keeping `requires_environment()`'s non-final safe
default; one medium finding — an overclaim that this decision alone unblocks `Cmadot`, corrected) and
one re-verification (one new internal-consistency finding — a code sketch/prose mismatch, fixed). Full
critique/revision record: `docs/plans/m3-framework-realignment.md`.
