# ADR-0018: `AeroModel`/`PropulsionModel` as `WrenchSource` specializations

Status: **accepted**, 2026-09-19

**Superseded in part by ADR-0020** (2026-09-20): the `compute(const Inputs&)`/`final wrench_b()`/
`final requires_environment()` design and the `AeroInputs`/`PropulsionInputs` structs are superseded.
This record's other decisions remain in force: `AeroModel`/`PropulsionModel` deriving from
`WrenchSource` (not `Model` directly); the `kCoreApiVersion` bump precedent; the per-RK4-substage
purity contract (ADR-0016); the AERORP/CG moment-transfer obligation on a concrete plugin ((d)
above); the V→0 obligation (now attached directly to `wrench_b()` per ADR-0020, in spirit unchanged).
Owner: Joe
Related/supersedes: elaborates ADR-0017 (the `bind_inputs()`/`InputRegistry` control channel this
decision's `AeroModel`/`PropulsionModel` are the first real consumers of) and ADR-0016 (the
per-RK4-substage purity contract `compute()` inherits from `wrench_b()`). Does not supersede
either. Drafted with `docs/plans/pr3-aero-propulsion.md`, revised through one Codex plan critique
(twelve findings, all accepted and folded in) and one further correction round from Joe's own
follow-up review (four changes — citations re-verified, (b)'s no-silent-NaN guarantee made
unconditional, a V→0 plugin-contract obligation added, R8a's test-only toggle dropped — none
reopening (a)/(c)/(d)/(f), so no second critique was needed) before this record.

## Context

`docs/architecture.md` §7 sketches `AeroModel`/`PropulsionModel`/`AeroInputs`/`PropulsionInputs`
with an explicit "Status: sketch, no concrete implementation exists yet... pinned by PLAN.md M3
PR 3's own ADR" disclaimer. The sketch's own prose leaves an open design question this decision
settles, not a self-contradiction to repair: the C++ code block says `AeroInputs` carries "the
control surface positions" and `PropulsionInputs` carries "throttle commands," while the same
section's later "Control inputs in v1" paragraph and ADR-0017's own Decision describe control
values arriving through `bind_inputs()`/`InputRegistry`'s `const double*` seam instead. Both can
coexist — `InputRegistry::add()`'s own stable, initialization-resolved pointer could in principle
be copied into an `Inputs` field without a per-frame allocation or lookup — so this is a genuine
choice between putting controls in `Inputs` (baking a fixed or generically-viewed control layout
into the struct) or leaving them out (a concrete plugin reads its own bound member directly, as
`nesc_aero` already does for its own config-resolved members). §7's own "Status" note defers
exactly this choice to this record.

Stevens & Lewis (*Aircraft Control and Simulation*, 3rd ed., verified directly against
`docs/refs/textbooks/`) define the aerodynamic angles and dynamic pressure this decision's
`AeroInputs` carries: §2.2, Eq. (2.2-1), p.67 (`q̄ = ½ρV_T²`); §2.3, Table 2.3-1 / Eq. (2.3-6a,b),
pp.77–78 (`tan α = W'/U'`, `sin β = V'/V_T`, `V_T = |v_rel|`); and, separately, the nondimensional-
rate hazard this decision's own V→0 obligation is about: §2.3, subsection "The Aerodynamic
Derivatives," Eq. (2.3-9a), pp.80–81 (`p̂ = Pb/(2V_T)`, a genuine `1/V_T` term, unlike the
*dimensional* force equations, Eq. (2.3-8b), built from the same coefficients, which have none).
§2.5, Eq. (2.5-3)/(2.5-4), pp.109–110, is ADR-0017's own already-cited precedent for keeping the
control vector `Ū` separate from the state vector `X` — confirmed accurate on rereading. Zipfel
(*Modeling and Simulation of Aerospace Vehicle Dynamics*, 4th ed., also verified directly) §9.2
"Subsystem Models," pp.319–320, decomposes a vehicle into separate mathematical subsystem modules
— "airframe [aerodynamics], propulsion, controls, autopilot, sensors, guidance, navigation" — the
same decomposition this decision's `AeroModel`/`PropulsionModel` split reflects, and states
plainly that at this level "controls are not modeled explicitly... actuators need not be included"
— independent corroboration for keeping control/actuator concerns out of these two classes
entirely. §10.2.1 "Aerodynamics" and §10.2.3 "Aerodynamic Controls"/"Thrust Vector Control"
(pp.406+/460+) repeat the same split one level up, matching this decision's aero-reads/actuator-
writes separation. The module-variable-bus architecture itself (one named signal, many readers)
remains ADR-0017's own already-verified citation (Fig. 9.39, App. E.2.3/E.2.5), not re-derived
here.

## Decision

`AeroModel`/`PropulsionModel` derive from `WrenchSource` (not `Model` directly, correcting the
sketch). Each provides a concrete, `final` `wrench_b(vs, env) const noexcept override` that:
(1) computes `v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n)`; (2) assembles
`AeroInputs` (`true_airspeed_mps`, `alpha_rad`, `beta_rad`, `omega_b`, `dynamic_pressure_pa`,
`mach`) or `PropulsionInputs` (`true_airspeed_mps`, `atmosphere`) using `atan2`-based α/β forms
chosen to avoid a division-by-zero at `v_rel_b == 0` (never `asin`) — finite there by
construction, though not always exactly `0` in the IEEE-754 signed-zero branch (`atan2(+0,-0) =
π`), a disclosed, accepted cosmetic edge case, not a NaN; (3) checks every assembled `Inputs`
field with `std::isfinite`, returning an explicit all-NaN `Wrench` (every `force_b`/`moment_b`
component set to a quiet NaN, never `Wrench{}`'s own zero default) without calling `compute()` at
all if any field is non-finite — the existing `HealthMonitor::check_wrench` (ADR-0016, §15,
unmodified) then unconditionally halts, naming this plugin's own path, regardless of which fields
a concrete plugin's own math actually reads; (4) otherwise calls a new pure virtual
`[[nodiscard]] virtual Wrench compute(const AeroInputs&) const noexcept = 0;` (respectively
`PropulsionInputs`) a concrete plugin implements, matching `wrench_b()`'s own per-RK4-substage
purity contract as ADR-0017 already amends it (identical output across one frame's four
sub-stages; a bound control member's value may still differ frame-to-frame).

Neither class overrides `bind_inputs()` — a concrete plugin registers whichever control names it
actually reads, directly, the same way `nesc_aero` registers no names and `input_consumer`
registers its own. Control values never flow through `AeroInputs`/`PropulsionInputs`.

`requires_environment()` **is** overridden, and marked `final`: both classes unconditionally
return `{wind: false, atmosphere: true}`, never overridable by a concrete plugin. Wind is optional
because the base class itself, not the plugin, is what reads `env.wind_n` (step 1 above), and
`EnvironmentSample::wind_n`'s own contract makes "absent" and "zero" identical by design.
Atmosphere is mandatory, and sealed as such, because step 3's `isfinite` guard and both `Inputs`
structs' own fields always depend on a real `AtmoState`, with no meaningful default — allowing a
concrete plugin to override this back to `false` would silently reopen the fault this decision's
own guard exists to prevent, by skipping `Vehicle::init()`'s own rejection of a config missing
`physical.atmosphere`.

A concrete plugin's own `compute()` carries a separate, undelegated obligation, stated in
`AeroModel`/`PropulsionModel`'s own class-level doc comments: it must return a finite `Wrench` at
`true_airspeed_mps == 0`, since a naive port of a nondimensional stability derivative (S&L §2.3 Eq.
(2.3-9a)) carries a real `1/V_T` term the dimensional force equations built from the same
coefficients (Eq. (2.3-8b)) do not have. This decision's own framework code computes no
nondimensional rate and cannot enforce this for a plugin that does; `PLAN.md`'s PR 4 line carries
the acceptance obligation instead.

`compute()`'s returned `Wrench` must already be referenced to the CG in body axes (`Wrench`'s own
existing, unchanged contract); a `PropulsionModel` plugin whose thrust line is off-CG applies its
own `r × F` using its own config's station offset before returning, inside `compute()` — no core
or base-class code performs this transfer.

`kCoreApiVersion` bumps 5→6: both classes' vtables, via the new `compute()`, are plugin-facing and
settling for the first time in this decision — the same reasoning version 3's brand-new `WindModel`
bump already used for a first-time-real interface, not a broader "one bump per `WrenchSource`-
family PR" pattern (versions 2/3 in fact shipped together in one PR; version 4 is
`EarthModel`/`GravityModel`, a different family).

Envelope monitors (§15's α/β/Mach/q̄/load-factor limits) are explicitly deferred to the PR that
gives them real declared numbers (PLAN.md PR 4 or later); no hook is added here.

Field names (`alpha_rad`, `dynamic_pressure_pa`, `true_airspeed_mps`, etc.) carry unit suffixes,
following `AtmoState`'s own existing precedent in this same header family
(`temperature_k`/`pressure_pa`/`density_kg_m3`/`speed_of_sound_m_s`) rather than inventing a
bare-name convention unique to these two new structs. Whether ADR-0002's literal "no unit suffixes
in identifiers" text means signal-name strings specifically or every C++ identifier is a
pre-existing ambiguity this decision does not resolve — tracked as a `PLAN.md` follow-up.

## Rejected alternatives

- **A fixed set of named control fields on `AeroInputs`** (`elevator_rad`, `aileron_rad`,
  `rudder_rad`), or a small fixed-size array with positions assigned by convention. Rejected: both
  bake one airframe's control layout into a framework interface, against the "anything that knows
  a wing area... lives under `plugins/`/`aircraft/`, never `core/`" rule this repository states for
  itself, and against the vehicle-agnostic charter (`AGENTS.md`).
- **A non-owning view over the plugin's own already-resolved `bind_inputs()` pointers**, exposed
  through `AeroInputs` generically. Technically possible without heap allocation (`InputRegistry`'s
  own storage is stable and init-resolved), but reintroduces a name-keyed lookup inside `compute()`
  on the per-frame path — the exact pattern `SignalRegistry`/`InputRegistry`'s own documented
  convention ("resolve once at init, never call `find()` again") already avoids, for a plugin that
  has no need to re-look up a pointer it already holds as its own member.
- **Epsilon-guard `V_rel` and clamp α/β/q̄/Mach to 0 below a threshold**, instead of the `atan2`
  forms plus `isfinite` guard. Rejected: inverts the "silent NaN" failure into a silent
  *wrong-but-plausible* value — `AGENTS.md`'s "a sim that runs from a broken setup is worse than
  one that refuses" and §15's own doctrine both argue against it — and would mask a genuinely
  broken atmosphere behind a threshold that cannot distinguish "V is really zero" from "V is a NaN
  that compares false against every branch."
- **A new `HealthMonitor` check specific to aero/propulsion inputs**, checking
  `AeroInputs`/`PropulsionInputs` before `compute()` runs. Rejected as pure duplication once the
  `isfinite`-guard in `wrench_b()` already gives an unconditional guarantee through the existing
  `check_wrench` — a second `sim.health` key, signal, and failure mode for zero additional
  coverage, and would have been exactly the "atmosphere-specific bolt-on"
  `environment_sample.hpp`'s own comment warns against, ahead of the general per-frame
  `EnvironmentSample` domain-validity capability that comment calls for.
- **`AeroModel`/`PropulsionModel` provide a base `bind_inputs()` registering a fixed, conventional
  control set**, which a concrete override calls via `Base::bind_inputs(...)` first. Rejected for
  the identical reason as the fixed-control-fields option above: bakes one airframe's layout into
  a framework class.
- **No `kCoreApiVersion` bump**, reasoning that `AeroModel`/`PropulsionModel` are new types no
  existing out-of-tree plugin could already implement. Rejected: `core_api.hpp.in`'s own version-3
  precedent (`WindModel`, also introduced brand-new in the same PR that bumped the version for it)
  already establishes that the version number is a general vtable-shape-freeze marker for any
  plugin-facing interface settling for the first time, not solely a backward-compatibility gate.
- **Folding this decision into ADR-0017 as a follow-on section**, instead of a new record.
  Rejected: `AGENTS.md`'s "Accepted ADRs are immutable: propose a superseding record" means a
  follow-on section would have to be its own new record regardless; this decision is about a
  materially different design surface than ADR-0017's own scope (the write-seam mechanism itself,
  already fully decided) — the first two consumers of that seam, and an unrelated question (the
  `Inputs`-struct/`compute()` shape, the α/β singularity treatment, the open control-fields choice).
  A dedicated record states that scope honestly.

## Consequences and validation

**Benefits.** Two new, concrete, testable abstract interfaces with no plugin-facing ambiguity left
in `docs/architecture.md`'s own sketch; zero change to any already-shipped interface; settles an
open design choice §7's own "Status" note explicitly deferred to this decision, before a concrete
plugin (PR 4) could be built against an unpinned reading of it; an unconditional no-silent-NaN
guarantee for both new consumers, closing (for them specifically) a previously-disclosed
`EnvironmentSample` gap with no new `HealthMonitor` API.

**Costs.** `kCoreApiVersion` bump (every plugin recompiles, per existing policy); two new header
pairs; `docs/architecture.md` §7 edits.

**Known gaps, disclosed not fixed.** Envelope monitors from §15 have no home yet — deferred to
PR 4 or later, tracked as a `PLAN.md` follow-up, the same posture ADR-0012/0016/0017 already took
for their own accepted, differently-shaped gaps. `EnvironmentSample`'s "not surfaced at all for a
NaN-tolerant consumer" gap is closed for `AeroModel`/`PropulsionModel` specifically but remains
open for any other `WrenchSource` that reads `env.atmosphere` directly. The V→0 nondimensional-
rate hazard is stated as a `compute()` contract obligation but not enforced by any code this
decision adds — PLAN.md's PR 4 line carries the acceptance obligation. `Simulation::for_testing()`
still cannot construct an atmosphere-requiring vehicle at all; this decision's own acceptance uses
`Simulation::from_config` instead of extending it (a separate, precedented follow-up, not part of
this decision). The ADR-0002 unit-suffix wording tension is accepted, following `AtmoState`'s
precedent, not resolved.

**Migration/rollback.** Fully reversible, single-PR change (no plugin implements
`AeroModel`/`PropulsionModel` outside this tree at the time of this decision); no data or schema
migration.

**Acceptance.** `PLAN.md`'s PR 3 line; the assigned implementation plan's own acceptance table
(`docs/plans/pr3-aero-propulsion.md`, R1–R12 plus R8-guard/R-V0) at implementation time.

## Approval

**Accepted by Joe, 2026-09-19**, after one Codex plan critique (twelve findings, all accepted and
folded in) and one further correction round from Joe's own follow-up review (four changes, none
reopening (a)/(c)/(d)/(f), confirmed in `docs/plans/pr3-aero-propulsion.md`'s "Revision 2"
section) — full record in that file. Decisions confirmed at approval: (a) option 1; (b) option 1 +
`isfinite` guard; (c) option 1 + `final` `requires_environment()`; (d) as pinned; (e) deferred, no
hook; (f) `kCoreApiVersion` 5→6 + this new record. Unit-suffixed field names accepted following
`AtmoState`'s precedent; the ADR-0002 wording ambiguity recorded as a `PLAN.md` follow-up, not
resolved here. `touch-goldens` armed by Joe for this file.
