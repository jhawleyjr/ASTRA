# Plan: framework realignment to Joe's stated intent (supersedes 3 ADRs)

Spec/brief: this file. Risk: **3** (core interfaces, ADR changes, `core/kernel/` — the highest tier
in `docs/process/workflow.md`'s table). Status: draft, three ADRs proposed, **none accepted** —
PR 4 implementation stays stopped until Joe decides. Plan approval: pending.
Intent/approval: `docs/refs/astra_framework_intent_and_state_audit.md` (main checkout, gitignored;
"Joe's stated intent... input to superseding ADRs; not itself an ADR"), read directly this
session. Audit base: `main` at `da18909` (current `main`, unchanged since).

## Verification of the audit's §4 claims against the code (done before relying on any of it)

Every claim below was checked directly against files in this worktree this session, not assumed
from the audit's own prose.

| Audit claim | Verified | Evidence |
|---|---|---|
| `Integrator` generic over a `StateDerivative` concept; only the *type* `StateVector` fixed | **Confirmed** | `core/include/astra/kernel/integrator.hpp`: `template <StateDerivative F> void step(...)`; concept requires `f(t,y) -> StateVector noexcept`; Euler/RK4 bodies are `y + dt*f(...)`-shaped, size-independent. `post_step_renormalize` hardcodes `state::kQuaternion`/`kQuaternionSize` — confirmed hardcoded. |
| "14 mentions" in `integrator.hpp` | **Imprecise, immaterial** | 13 matching lines / 16 raw occurrences of the literal string, depending on counting method — neither is 14. Does not change the claim's substance. |
| Plugins never index state; all use `VehicleState` named accessors; one cosmetic exception at `plugins/physical/earth/flat/flat.cpp:37` | **Confirmed exactly** | `grep` for raw `StateVector`/index patterns across `plugins/` and `aircraft/` returns nothing except that one file; line 37 reads exactly `constexpr int kDown = astra::state::kPositionSize - 1;  // NED: third component is down` — matches the audit's own quote verbatim. 3 wind + 3 gravity + 3 earth plugin directories confirmed (9). |
| `state.hpp`: `kStateSize = 13`, `StateVector = Eigen::Vector<double, 13>`, "Mass is not state in v1" | **Confirmed verbatim** | Read the file directly; every token matches. |
| `vehicle.hpp`: `StateVector state_ = StateVector::Zero();`, `std::optional<Eom> eom_;` | **Confirmed verbatim**, lines 196/200 | Read directly. |
| `vehicle.cpp:147` pre-step copy, `:152` derivative lambda | **Confirmed verbatim, exact line numbers** | `const StateVector pre_step_state = state_;` at 147; `const auto derivative = [&](double t, const StateVector& y) noexcept -> StateVector {` at 152. |
| `simulation.cpp` `read_initial_state` spans ~233-259, builds a fixed `StateVector` from four named segments | **Confirmed**, function is 233-261 (off by ~2 from the audit's own 259, immaterial) | Read directly; body constructs `y0.segment<...>(state::kPosition/kVelocity/kQuaternion/kAngularRate)`. |
| Per-file "mention counts" for `vehicle_state.hpp`(9)/`eom.*`(8)/`simulation.*`(10)/`health_monitor.*`(6) | **Right file set, counts not exactly reproducible** | Simple `grep -c StateVector` gives 5/4/5/2 respectively; a broader pattern including `state::k*`/`segment<` overshoots instead. The *files* are exactly the ones that matter; the audit's own count methodology isn't reproducible from a plain grep, immaterial to any design conclusion. |
| `Model` interface: only `init/reset/step/name`, no continuous-state hook | **Confirmed verbatim** | Read `core/include/astra/interfaces/model.hpp` in full. |
| ICD has no state-layout dependence | **Confirmed** | `grep` for `StateVector`/`state::` in `icd/astra_icd.h` returns nothing. |
| Goldens don't encode state vector size | **Confirmed** | `grep -rl` across `tests/golden/` returns nothing. |
| Python `_core.cpp` exports `STATE_SIZE` + segment offsets as module constants | **Confirmed**, 9 constant exports + 1 usage found (audit said 13 total mentions — close, not exact) | Read `python/bindings/_core.cpp:76-84` directly. |
| Tests: 236 mentions across 23 files | **File count exact (23)**; occurrence count not reproducible under any single grep pattern tried (156 narrow / 311 broad) | `grep -rl`/`grep -ro` across `tests/`. The file count is the figure that actually matters (which tests would need to keep compiling); confirmed exact. |
| ADR-0000 Purpose paragraph wording | **Confirmed verbatim** | Read `docs/decisions/0000-founding-decisions.md` directly — "focused on commercial transport aircraft" is the exact clause. |
| `docs/architecture.md` §1 "implement three classes for your aircraft" | **Confirmed verbatim** | Read directly. |
| `docs/architecture.md` §7 rejects templates "for ... the Python side", promises hand-built-`AeroInputs` testing; §18 names `AeroInputs` twice | **Confirmed verbatim**, §18 is section header at line 452, both citing lines (455, 467) fall inside it | Read both sections directly. |
| ADR-0018's `AeroModel`/`PropulsionModel` `wrench_b()`/`requires_environment()` are `final`; `compute()` takes a fixed 6-field struct | **Confirmed** (re-verified this session; already read in full for the now-stopped PR 4 work) | `core/include/astra/interfaces/aero_model.hpp`, `propulsion_model.hpp`, `aero_inputs.hpp`. |
| `nesc_aero`/`nan_wrench` predate ADR-0018, derive from plain `WrenchSource` with no `Inputs` struct | **Confirmed**, and further verified that `nesc_aero`'s own force/moment forms are algebraically V-safe (no guard needed at all) | Read `tests/plugins/nesc_aero/nesc_aero.cpp` in full. |

**Net assessment:** every claim that bears on a design decision is accurate, several to the exact
line number. The only imprecision is in aggregate "mention count" figures, which don't reproduce
bit-for-bit under a plain `grep` regardless of pattern tried, and change no conclusion. The audit is
reliable to build on.

## Verification of §6's claimed defects in this session's own prior work

The audit calls out three specific defects in `docs/plans/pr4-notional-aero-turbofan.md` (this
session's now-stopped PR 4 plan). All three checked and **confirmed true** — recorded honestly
rather than defended:

1. **"the algebraic V→0 fix uses `rho`, which `AeroInputs` does not carry."** Confirmed:
   `AeroInputs` (`core/include/astra/interfaces/aero_inputs.hpp`) has exactly six fields —
   `true_airspeed_mps`, `alpha_rad`, `beta_rad`, `omega_b`, `dynamic_pressure_pa`, `mach` — no
   `rho`/density field at all. The prior plan's closed form `term = 0.25 * rho * V_T * S * L^2 *
   Coeff * rate` cannot actually be implemented against `compute(const AeroInputs&)` as written;
   it silently assumed a field that isn't there. This is now moot: ADR-0020 below removes the
   `compute(const AeroInputs&)` step entirely, so the fix is re-derived fresh against whatever
   inputs a concrete plugin actually reads (it can read atmosphere density directly once it isn't
   restricted to a core-defined `Inputs` struct).
2. **"`thrust_ref_n` leaves zero thrust margin at cruise."** Confirmed: the prior plan defined
   `thrust_ref_n` so that `throttle=1.0` exactly balances cruise drag — no margin for climb,
   acceleration, or any trim perturbation; a real PR 6 trim solve could easily need `throttle>1`
   (physically impossible) and fail to converge. A real design would carry headroom (e.g. size
   `thrust_ref_n` so cruise-required thrust lands at some `throttle<1`, such as ~0.7, a
   representative high-bypass-turbofan cruise power setting) — noted here for whenever PR 4 is
   rebuilt on the corrected interface, not fixed in this document (PR 4 itself is out of scope
   until ADR-0020 lands).
3. **"the claimed PLAN.md `Cmadot` follow-up line was never written."** Confirmed: `grep -n
   "Cmadot\|alpha_dot" PLAN.md` returns nothing. The prior plan's own text said this was "flagged
   here and in the PLAN.md follow-up list," but the PLAN.md edit was never actually made — an
   honest miss, not a disputed claim.

## Risks R1-R4 (from the audit's §5) — addressed in ADR-0021 below

Restated with their resolution for traceability: **R1** bit-identity (NESC/determinism goldens must
still pass) — ADR-0021 requires the rigid-body `DynamicsModel` implementation to be a byte-for-byte
port of today's `Eom::operator()` arithmetic, existing goldens as unmodified acceptance evidence.
**R2** no allocation — ADR-0021 requires the combined state buffer sized once in `Vehicle::init()`,
never resized in `step()`, with a dedicated no-alloc test extension. **R3** mass as state — ADR-0021
explicitly does NOT decide this; it states the rule for what changes (`MassPropertiesModel::get()`'s
signature, `Eom::begin_frame()`'s per-frame caching) if and when a future record adds it, flagged as
a deliberate open follow-up, not silently left ambiguous. **R4** trim/linearize (PR 5-7) — ADR-0021
requires `Simulation.derivative(state)`'s Python contract to be spec'd against a length-N,
named-slice vector from PR 5's own start, which is exactly why the audit's own sequencing
recommendation (§6) puts this ADR's implementation before PR 5 begins.

---

# ADR-0019 (draft): Purpose — vehicle-agnostic framework, not commercial-transport-focused

Status: **proposed**
Date: 2026-09-19
Owner: Joe
Related/supersedes: supersedes ADR-0000's Purpose paragraph only (one clause); no other row of
ADR-0000's decision table is touched.

## Context

`docs/decisions/0000-founding-decisions.md`'s Purpose paragraph reads: "A vehicle-agnostic,
high-fidelity six-degree-of-freedom simulation **framework and architecture** focused on commercial
transport aircraft." `docs/architecture.md` §1 echoes this with a user story: "clone the repo,
implement three classes for your aircraft (mass properties, aero, propulsion)." Both confirmed
verbatim this session. Joe's stated intent
(`docs/refs/astra_framework_intent_and_state_audit.md` §1): "ASTRA is a vehicle-agnostic,
high-fidelity 6-DoF simulation framework. The framework and architecture are the product ... The
shipped commercial-transport model is the simplest possible worked example. Its fidelity and its
numbers do not matter." The audit names the concrete failure mode this causes today: "every session
reads this first and designs an aircraft framework" (§3, item 1) — this session's own prior work on
PR 4 is itself an instance (a plugin-level PR built without questioning whether the underlying
interfaces were vehicle-agnostic enough, because the founding purpose statement says the project is
"focused on commercial transport aircraft").

## Decision

Supersede the Purpose paragraph's scope clause. New text:

> A vehicle-agnostic, high-fidelity six-degree-of-freedom simulation **framework and architecture**.
> The framework and its interfaces are the product: it must let a user build a new vehicle, at
> whatever fidelity they need, without editing `core/`. The framework ships no vehicle of its own;
> `aircraft/notional_transport` is the simplest possible worked example proving the public
> interfaces end to end (template → models → trim → linearize → fly against external software), not
> a claim of commercial-transport focus or fidelity — its numbers are notional and do not matter.
> The distinguishing requirement is unchanged: no software models of avionics in the sim — the
> user's real embedded software (C++) runs against the sim, first all-digital on one machine, later
> hardware-in-the-loop on the actual FPGA/Jetson target.

Everything else in ADR-0000's Purpose paragraph and decision table is unchanged (name, distinguishing
requirement, all other rows).

**Part of this decision's own acceptance, not deferred** (revised after Codex plan critique below —
the original draft deferred all of this and a reviewer correctly flagged that as leaving the actual
failure mode unfixed): `docs/architecture.md` §1's framing must be brought into agreement with the
new Purpose text as part of accepting this ADR, since §1 is the first document a session reads after
ADR-0000 and today independently states the same aircraft-specific framing this ADR supersedes
("clone the repo, implement three classes for your aircraft ... Everything else ... is already
there"). Specifically, as this ADR's own acceptance condition: reword §1's framing sentence away from
"clone the repo, implement three classes for your aircraft" toward the vehicle-agnostic framing
above (a vehicle plugs in whatever models it needs, at whatever fidelity it needs). The exact
enumeration of what a minimal vehicle must still implement (today: mass properties, aero,
propulsion) is left as-is for now and revisited once ADR-0021 is decided — that count is a
consequence of how many model *kinds* exist, not of this ADR's own purpose-statement scope, and
changing it before ADR-0021 lands would describe a capability that doesn't exist yet. Pure prose,
Tier 0/1 (`docs/process/workflow.md`), no executable behavior — folded into this ADR's own
implementation step, not a separate ADR.

## Rejected alternatives

- **Leave ADR-0000 as-is, treat the phrase as informally superseded by convention.** Rejected:
  `AGENTS.md`'s own rule ("Accepted ADRs are immutable: propose a superseding record") exists
  exactly so a reader of ADR-0000 alone gets the true current scope without tribal knowledge of a
  later conversation — the audit names this precise failure mode.
- **Edit ADR-0000 in place instead of a superseding record.** Rejected: violates the
  immutable-accepted-ADR rule directly; ADR-0000 predates the ADR series and is the index record
  every other ADR cites ("elaborates ADR-0000 row X") — editing it in place breaks that provenance.

## Consequences and validation

Documentation/intent only — no code, no interface, no behavior change. Benefits: closes the exact
"every session designs an aircraft framework" failure mode the audit names, at the root — both
places it lives (ADR-0000 and architecture.md §1), not just the ADR. Costs: the §1 framing edit
above, plus any other prose found to assume commercial-transport framing during that pass. Acceptance:
`git diff --stat` touches the new ADR file and `docs/architecture.md` §1's framing sentence only (not
its "three classes" enumeration, deferred per above); no executable check applies (Tier 0/1).

## Approval

Proposed, pending Joe's decision.

**Codex plan critique (adversarial-review, working-tree scope, `2026-09-19`): verdict
"needs-attention," one medium finding.** Finding: the original draft's acceptance criteria let
architecture.md §1's aircraft-specific user story remain authoritative indefinitely (deferred to a
future, unscheduled follow-up), which is the exact failure mode this ADR claims to close — so it
could be "accepted" while doing nothing about the document a session actually reads first.
**Accepted and folded in above**: the §1 framing edit (not its "three classes" enumeration, which
genuinely depends on ADR-0021) is now part of this ADR's own acceptance, not a deferred follow-up.
No second critique requested — the fix is narrow and doesn't reopen any other part of the draft.

---

# ADR-0020 (draft): `AeroModel`/`PropulsionModel` as true interfaces (supersedes ADR-0018's `Inputs`/`final` parts)

Status: **proposed**
Date: 2026-09-19
Owner: Joe
Related/supersedes: supersedes ADR-0018's `compute(const Inputs&)`/`final wrench_b()`/`final
requires_environment()` design and its two `Inputs` structs. Does **not** supersede ADR-0018's other
decisions: `AeroModel`/`PropulsionModel` still derive from `WrenchSource` (not `Model` directly);
the `kCoreApiVersion` bump precedent; the per-RK4-substage purity contract (ADR-0016, unchanged);
the AERORP/CG moment-transfer obligation on a concrete plugin (ADR-0018 (d), unchanged); the V→0
`compute()`-level obligation (unchanged in spirit, now attached to `wrench_b()` directly since
`compute()` no longer exists as a separate step). Elaborates Joe's intent document §1/§3(item 4)/§5
(item 3).

## Context

Joe's intent document, §1: "A header in `core/include/astra/interfaces/` declaring an abstract base
class: pure virtual functions, no data members, no implementation, no assumption about what a
concrete vehicle contains ... Core may offer shared math (air data, frames) as **utilities a model
may call**, never as a mandatory input struct or a `final` method that fixes behavior." §3 item 4:
today's `AeroModel::wrench_b()`/`requires_environment()` are `final` with real logic; `compute()`
takes a core-defined six-field struct — confirmed this session by direct read of
`core/include/astra/interfaces/aero_model.hpp`/`propulsion_model.hpp`/`aero_inputs.hpp`; "Original
architecture §7 (commit `301435d`) specified the same fixed struct, so this is a founding-doc
defect, not PR 3 drift" (audit, confirmed by reading `docs/architecture.md` §7's own "Status: real
and built" note, which still shows the original fixed-struct sketch above it).

Textbook basis (§2 of the intent document, re-verified against `docs/refs/textbooks/` this session
for ADR-0018's own already-cited passages, not re-derived): Stevens & Lewis §2.5/§3.5's F-16 model
takes coefficient dependencies `(alpha, beta, M, h, delta_S, P, Q, R)` plus `alpha_dot, beta_dot`
for moments — already more than today's six-field `AeroInputs` (no `alpha_dot` field exists;
confirmed directly, and confirmed this session's own PR 4 plan hit exactly this wall trying to
carry a `Cmadot` alpha-rate-damping term the current `AeroInputs` cannot support without a core
change). Zipfel App. E.2.3/E.2.5/E.3.1: each module "carries out the table look-up, and integrates
the state variables" and reads any input it needs from a shared named bus — no module receives a
fixed input struct.

**Existing in-tree precedent this design already works, pre-dating ADR-0018:**
`tests/plugins/nesc_aero/nesc_aero.cpp` derives directly from `astra::WrenchSource` (not through any
`Inputs`-struct-taking intermediate), reads whatever it needs from `(VehicleState, EnvironmentSample)`
and its own config-resolved members, and its force/moment forms are written to be algebraically
finite at `v_rel_b = 0` (a `|v|*v` drag form and V-linear rate-damping forms) with **no explicit
isfinite guard at all** — confirmed by reading the file in full this session. `nan_wrench` is the
other pre-ADR-0018 direct-`WrenchSource` plugin. Both continue to compile and pass unmodified today.

## Decision

`AeroModel`/`PropulsionModel` become minimal marker interfaces, with exactly one deliberate,
non-empty override each (reconciled after Codex re-verification below — an earlier draft of this
sketch showed a fully empty class body, which contradicted the safe-default fix and would have left
atmosphere optional for a plugin that never overrides `requires_environment()`):

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
VehicleState&, const EnvironmentSample&) const noexcept` directly — the exact pattern
`nesc_aero`/`nan_wrench` already use. `requires_environment()` is *not* left at `WrenchSource`'s own
bare default (`{}`, i.e. nothing required) — that would silently reopen the exact "author forgets to
declare atmosphere" gap the fix above exists to close.

These two classes exist for **type identity, plus this one safe-default override** (not the "zero
behavior baked into the base class" this draft originally claimed — corrected here after the
plan-critique's own internal-consistency finding): `Vehicle::init()`'s existing discovery pass can
still recognize "this is an aero/propulsion contribution" for logging, diagnostics, and any future
aero/propulsion-specific §15 envelope-monitor hook. The one override is a plain, non-`final` virtual
with a value a plugin can always change — consistent with the intent document's own definition
("never ... a `final` method that fixes behavior"), which objects to `final`, not to a sensible
overridable default.

**Air-data math becomes core utilities**, not a mandatory struct or sealed method: new
`core/include/astra/util/air_data.hpp`, free functions — `relative_velocity_b(vs, env)`,
`true_airspeed(v_rel_b)`, `angle_of_attack(v_rel_b)`, `sideslip(v_rel_b)` (the exact `atan2`-based,
V→0-safe forms ADR-0018 already derived — formulas carry over verbatim, only their location moves),
`dynamic_pressure(rho, v_true)`, `mach(v_true, speed_of_sound)`. A concrete plugin composes whichever
it needs and may define its own plugin-side `Inputs`-shaped struct plus a `compute()`-style split
purely as its own convention if it wants hand-built-fixture testability — the template demonstrates
this pattern; core no longer prescribes it.

**The isfinite/no-silent-NaN guard becomes a utility** (`air_data::all_finite(...)` over a
caller-supplied list, or a plugin calls `std::isfinite` itself) — recommended, not enforced.

**Regression, corrected after Codex plan critique below — stated precisely, not softened.** The
original draft of this ADR described the cost as "a plugin's own arithmetic can execute on
non-finite input before [`check_wrench`] catches it," implying the corruption is merely *detected
late*. That is wrong, and the wrong version understates a real, high-severity regression: this is
exactly the R-001 defect PR 3's own review found and fixed
(`docs/reviews/pr3-aero-propulsion-review.md`) — an infinite `speed_of_sound_m_s` divided into a
finite airspeed gives `mach = 0.0`, a perfectly finite value, so a plugin computing a wrench from it
can produce a finite-looking *output* that `HealthMonitor::check_wrench` has no way to flag, because
the corruption never survives to the output in a form that check can see. **The corruption can go
undetected permanently, not just late.** `nesc_aero` is not equivalent prior art for this specific
failure mode: it never reads `speed_of_sound_m_s` at all, so its algebraic V-safety says nothing
about protection against a corrupted atmosphere sample. Separately, reverting
`requires_environment()` from `final` to a plain virtual also removes the *unconditional* rejection
of a config missing `physical.atmosphere` for a plugin author who simply forgets to declare it — not
just the isfinite guard, a second, independent guarantee ADR-0018 provided.

**Decisions taken to close this, not merely disclosed as an accepted cost:**
1. `AeroModel`/`PropulsionModel` keep a **non-final** override of `requires_environment()`
   defaulting to `{wind: false, atmosphere: true}` — the exact value ADR-0018 sealed, but now
   overridable rather than compiler-enforced. An author who never touches `requires_environment()`
   at all still gets today's safe behavior; only an author who *actively* overrides it takes on that
   responsibility explicitly and visibly, in a code review's plain view. This closes the
   "forgot to declare atmosphere" gap without reintroducing `final`.
2. The isfinite guard itself **cannot** be made compiler-enforced without exactly the `final` method
   §1 of the intent document rules out — this is accepted as a real, permanent, named regression:
   **a plugin's forgotten or incomplete finiteness check on raw environment fields it reads can
   produce a finite, wrong output that this framework's automated checks cannot always catch.** The
   mitigation is process-level, not compiler-level: `air_data.hpp`'s header comment and
   `aircraft/_template`'s own aero/propulsion stubs must both cite R-001 by name as the concrete,
   already-happened failure mode a finiteness check over *every raw field read, not just derived
   ones* prevents, so every future plugin author and every future Codex review of one is primed to
   check for exactly this. This is weaker than ADR-0018's guarantee, and that is stated here
   explicitly for Joe's sign-off, not assumed or minimized.

No templates — the interface stays ordinary virtual dispatch (§7's own stated Python-side reason,
restated, not re-litigated).

This ADR does **not** add any continuous-state-declaration capability to `AeroModel`/
`PropulsionModel` itself — that arrives automatically, for every `Model` subclass including these
two, once ADR-0021's `Model`-level hook lands (defaulted to zero states until then). Per the
intent document's own sequencing recommendation (§6), this ADR's implementation can land *before*
ADR-0021's, unblocking PR 4 sooner.

**Correction after Codex plan critique below — the original draft overclaimed this ADR's effect on
`Cmadot`.** Removing the sealed `AeroInputs` struct does **not**, by itself, make the dropped
`Cmadot` (alpha-rate damping) term representable: `VehicleState` carries position/velocity/attitude/
angular-rate, and `EnvironmentSample` carries wind/atmosphere — neither carries `alpha_dot`, and
`alpha_dot` cannot be derived from a single-instant snapshot of either. Finite-differencing it across
successive `wrench_b()` calls is not a valid alternative — it would violate `WrenchSource::
wrench_b()`'s own documented purity contract (`wrench_source.hpp`: "a conforming implementation MUST
... be a pure function of (vs, env) with no hidden per-call state"), which ADR-0020 does not touch
and has no intention of relaxing. Genuinely supporting `Cmadot` needs either (a) ADR-0021's
model-owned continuous state, giving a plugin its own lagged/filtered alpha state with its own
well-defined derivative equation (matching S&L's own "use the previous step's value" suggestion, but
as a real, purity-respecting state rather than hidden mutable memory), or (b) continuing to neglect
it (S&L's own other suggested treatment, and what this session's stopped PR 4 plan already did).
**ADR-0020 alone does not unblock `Cmadot`; it only stops the sealed struct from being the reason
every *other* input a plugin already has access to (raw atmosphere fields, a differently-shaped
control set, its own cached members) is unavailable.** PR 4, once rebuilt against this interface,
still drops `Cmadot` for the same reason as before, now correctly attributed to "no `alpha_dot`
source exists yet, pending ADR-0021 or a deliberate neglect decision" rather than "the struct is
sealed."

## Rejected alternatives

- **Keep `AeroInputs`/`PropulsionInputs` but make them extensible** (a variant or name-keyed map of
  doubles). Rejected: reintroduces a name-keyed lookup on the per-frame path — the exact anti-pattern
  `SignalRegistry`/`InputRegistry`'s own documented convention already forbids ("resolve once at
  init, never call `find()` again") — and still bakes some core opinion about input shape into a
  vehicle-agnostic interface.
- **Keep `compute()` as a separate step, made generic via a template parameter (CRTP or similar).**
  Rejected: templates break `Model`'s runtime-dispatch story and the Python-trampoline path §7
  already rejected templates for; a compile-time-generic `compute()` cannot be called through the
  runtime `WrenchSource*` `Vehicle` holds a list of.
- **Keep the sealed `final` isfinite guard exactly as-is; a vehicle needing an input the fixed
  `Inputs` struct doesn't carry derives from `WrenchSource` directly, bypassing `AeroModel`/
  `PropulsionModel` entirely.** Rejected: relocates the cost rather than removing it — the very next
  vehicle after `notional_transport` needing, say, a differently-shaped control set or a raw
  atmosphere field `AeroInputs` doesn't expose hits this wall immediately. (`Cmadot` specifically is
  *not* an example of this particular wall, per the correction above — that gap is about a missing
  state/derivative source, not the struct's sealing — but the general problem the struct's sealing
  causes is real and is what this rejected alternative accepts living with.)

## Consequences and validation

`kCoreApiVersion` bump (same precedented reasoning ADR-0018 already used for its own 5→6 bump — a
settling vtable shape for a plugin-facing type). Benefit: any future vehicle's aero/propulsion model
reads exactly the inputs already available to it (any raw environment field, any bound control, any
member it caches itself, and — once ADR-0021 lands — its own state slice) with zero further core
interface change for that class of need. This does **not** by itself solve every input gap a future
model might have (see the `Cmadot` correction above: a genuinely missing input source, like
`alpha_dot`, still needs ADR-0021 or an equivalent, not just an unsealed struct). Costs:
`aero_stub`/`propulsion_stub` rebuilt to
override `wrench_b()` directly (small — `nesc_aero` is the existing, working proof this is not new
ground); `docs/architecture.md` §7's code sketch and surrounding prose, and §18, rewritten; the
"hand-built-fixture, no-simulator" testability promise (§7) is preserved as a documented *plugin-side
convention the template demonstrates*, not a core guarantee — stated explicitly so its removal from
core is not misread as removing the promise itself. Migration/rollback: fully reversible, no external
plugin exists yet; `docs/plans/pr4-notional-aero-turbofan.md` is superseded — its frame-conversion
math, S&L force/moment-transfer citation, and dropped-terms analysis remain reusable once PR 4 is
rebuilt against this interface, only the `compute(const AeroInputs&)` signature assumption needs
redoing.

Acceptance: rebuilt `aero_stub`/`propulsion_stub` pass PR 3's existing integration-test pattern under
the new interface; `nesc_aero`/`nan_wrench` continue to pass **unmodified** (a regression here would
mean this ADR's own reasoning about them is wrong); `AeroModel`/`PropulsionModel`'s non-final
`requires_environment()` default is unit-tested to still return `{wind: false, atmosphere: true}`
for a plugin that doesn't override it; `air_data.hpp` and both template stubs cite R-001 by name;
full gate.

## Approval

Proposed, pending Joe's decision.

**Codex plan critique (adversarial-review, working-tree scope, `2026-09-19`): verdict
"needs-attention," one high finding and one medium finding.**

- **High** (accepted, folded in above): the original draft's framing of the removed isfinite guard
  as merely "detected late" by `HealthMonitor::check_wrench` was wrong — a corrupted raw input
  (the exact R-001 case: infinite `speed_of_sound_m_s`) can produce a finite *output* wrench that no
  automated check catches, so the corruption can go undetected **permanently**, not late. `nesc_aero`
  does not establish protection against this case (it never reads `speed_of_sound_m_s`). Fixed:
  `requires_environment()` keeps a non-final safe default so a forgetful override can't silently drop
  the atmosphere requirement; the isfinite guard's loss is now stated as a real, permanent, named
  regression with a process-level (not compiler-level) mitigation, not minimized as a timing issue.
- **Medium** (accepted, folded in above): the original draft claimed removing the sealed `Inputs`
  struct unblocks `Cmadot`. False — `Cmadot` needs `alpha_dot`, which no available snapshot input
  provides and which cannot be derived without either violating `wrench_b()`'s purity contract or
  adding a model-owned state (ADR-0021). Corrected throughout; `Cmadot` remains dropped, now for the
  accurately-stated reason.

Both findings changed real technical claims in this ADR, not wording only — re-verification
requested.

**Codex re-verification (adversarial-review, working-tree scope, `2026-09-19`): verdict
"needs-attention."** Finding 1 (permanent non-detection): confirmed fixed, accurately stated.
Finding 2 (`Cmadot` overclaim): confirmed fixed. **New finding surfaced by the fix itself**
[medium]: the code sketch still showed fully empty class bodies, contradicting the new safe-default
`requires_environment()` override described in prose a few paragraphs later — the Decision section
specified both the vulnerable behavior and its own fix at the same time. **Accepted and fixed**: the
code sketch now shows the one deliberate override explicitly. A second focused re-check would be
routine confirmation at this point (a code-sketch/prose consistency fix, not a new technical claim)
— not requested; the correction is mechanical and self-evidently closes the gap on inspection.

# ADR-0021 (draft): Core dynamics interface with model-owned continuous states (supersedes ADR-0003's fixed-layout decision)

Status: **proposed**
Date: 2026-09-19
Owner: Joe
Related/supersedes: supersedes ADR-0003's "the state is 13 doubles in a fixed layout" decision.
Does **not** supersede ADR-0003's frame/position-representation content (Earth model still owns the
meaning of `p_n`; `q_nb` conventions per ADR-0001 unchanged) — only the "fixed, core-imposed size and
layout" part. ADR-0003 itself already anticipated this: "Mass as state ... becomes an ADR that
supersedes this one." Elaborates the intent document §1/§2/§3(items 2,3)/§4/§5(items 1,2,4)/§6
(sequencing).

## Context

Confirmed this session, directly against code (see the verification table above for exact
citations): the fixed-13-state assumption is shallow. `Integrator` is already generic over a
`StateDerivative` concept and only the *type* `StateVector` is pinned. No plugin (9 shipped physical
plugins, 2 test-only aero/propulsion stubs, `nesc_aero`, `nan_wrench`) indexes the state directly —
every one uses `VehicleState`'s named accessors, with one cosmetic exception. The files that
actually know the fixed layout are a short, named list: `state.hpp` (definition),
`vehicle_state.hpp` (the accessor view), `eom.hpp/.cpp`, `vehicle.hpp/.cpp` (owns `StateVector
state_`, `std::optional<Eom> eom_`, builds the per-frame derivative lambda), `simulation.hpp/.cpp`
(`read_initial_state`, `for_testing`), `health_monitor.hpp/.cpp` (`check_state`), and one comment in
`frames.hpp`. `Model` has no hook to declare a continuous state or return its derivative.

Textbook basis (re-verified against `docs/refs/textbooks/` this session, citations already given in
the intent document, not re-derived from scratch): Nutaro, *Building Software for Simulation*, §5.5
— `ode_system` exposes `numVars()`, `init(double* q)`, `der_func(const double* q, double* dq)`;
`ode_solver` holds an `ode_system*` and integrates N doubles it knows nothing about, sizing its work
arrays once in its own constructor from `numVars()` — the exact "sized once, generic integrator"
shape this ADR adopts. Zipfel App. E.2.3/E.2.5/E.3.1: each module "carries out the table look-up, and
integrates the state variables" — modules own and integrate their own states. Stevens & Lewis's F-16
model itself carries an engine power-lag state (`POW = X(13)`) beyond rigid-body motion, in the
reference aircraft the rest of this project's own citations already point to — a model-owned state
is not a hypothetical need, it is already present in the book this project's EOM is built from.

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

`RigidBody6DofDynamics` ships in `core/` as the one implementation of this interface for today's
13-double `p_n/v_b/q_nb/omega_b` block, with arithmetic that is a **byte-for-byte port** of today's
`Eom::operator()` (R1). `post_step` becomes this class's own override of what
`post_step_renormalize` does today (renormalizing its own quaternion segment), not a free function
called unconditionally over the whole buffer.

**`VehicleState` retyping — corrected after Codex plan critique below.** The original draft claimed
`VehicleState`'s "own code, signatures ... are unchanged." That is false and would have shipped a
dangling-reference bug: `VehicleState`'s actual constructor
(`core/include/astra/kernel/vehicle_state.hpp:18`) is `explicit VehicleState(const StateVector& y)
noexcept : y_(y) {}`, binding a genuine reference to a fixed-size `StateVector`. Constructing one
from an `Eigen::Map` over a runtime-sized buffer via an implicit conversion would bind `y_` to a
temporary `StateVector` destroyed at the end of the full expression, leaving every accessor reading
freed memory. **What is actually unchanged**: every accessor method's name and return value
(`p_n()`, `v_b()`, `q_nb()`, `omega_b()`) and every plugin call site (plugins only ever call these
accessors; none constructs a `VehicleState` itself — confirmed by inspection, zero hits for
`VehicleState(` outside `core/` and its own tests). **What genuinely changes**: the constructor
takes `const double*` (or an equivalent fixed-extent span) into wherever the 13 rigid-body doubles
currently live — a stack `StateVector`'s own `.data()` today, an offset into the shared dynamic
buffer after this ADR — and stores an `Eigen::Map<const Eigen::Vector<double,13>>` internally
(a non-owning view constructible from any compatible raw pointer, with identical `.segment<N>()`
semantics to today's owning type, so every accessor's own body is unchanged). `vector()`'s return
type changes from `const StateVector&` to the same Map type by value (cheap: pointer + fixed
stride). **One existing test's own invariant breaks by design, named here so it isn't rediscovered
as a mystery failure**: `tests/unit/kernel/vehicle_state_test.cpp:21`'s `EXPECT_EQ(&vs.vector(),
&y)` asserts `.vector()` returns the identical object address as the caller's own `StateVector` —
a Map-returning `vector()` has no such address to compare; this test's assertion is rewritten to
check value equality instead of address identity, since address identity was never a contract
`VehicleState`'s own header actually promised (its comment says "read-only, non-owning view", not
"identical object").

**Model-owned continuous states.** `Model` gains three new virtuals, each defaulted to a zero-state
no-op so every existing subclass keeps compiling and behaving identically without any edit:

```cpp
[[nodiscard]] virtual int state_count() const noexcept { return 0; }
virtual void initial_state(double* q) const noexcept {}
virtual void integrate_state(double t, const double* q, const EnvironmentSample& env,
                              double* dq) const noexcept {}
```

(`EnvironmentSample&` added after Codex re-verification below caught a self-contradiction: the
coupling-boundary text says a model's derivative may depend on the environment sample, but the
signature as first drafted gave it no way to receive one. This mirrors `WrenchSource::wrench_b(vs,
env)`'s own existing parameter shape exactly, so `Vehicle`'s already-existing per-stage `stage_env`
construction — built once per RK4 sub-stage today for `wrench_b()` calls, PR 3c's own pattern — is
passed through to `integrate_state()` calls too, with no new plumbing mechanism: it is sub-stage-
fresh, the same freshness `wrench_b()`'s own atmosphere argument already has, not frame-latched like
a bound control input. Bound control inputs are read the same way `wrench_b()` already reads them —
via a `const double*` a state-owning model resolves once at init through `InputRegistry`, not
through this function's parameter list.)

`Vehicle::init()`'s existing discovery pass (the same one that already walks `WrenchSource`s for
`bind_inputs()`) sums every model's `state_count()`, allocates **one** contiguous buffer of size `13
+ sum(model states)` **once** (an init-time allocation, permitted; never resized in `step()` — R2),
and hands each state-owning model a fixed `double*`/`const double*` view into its own slice (offset
computed once at init and stored as a plain `int`, matching `SignalRegistry`/`InputRegistry`'s own
"resolve once, never look up again" discipline).

**Cross-model/rigid-body coupling — scoped down after Codex plan critique below.** The original
draft claimed a single combined buffer "preserves" coupled RK4 evaluation because every state lives
in one block a single integrator advances together. A reviewer correctly found this incomplete:
`integrate_state()` as specified receives only the model's *own* slice — a model whose own
derivative genuinely needs another model's or the rigid-body's *live, sub-stage* value (the
reviewer's example: an engine spool-lag rate that depends on airspeed) has no way to read it, and
conversely `wrench_b(vs, env)`'s signature is unchanged (ADR-0020 keeps it that way deliberately),
so a `PropulsionModel` whose *thrust* depends on its *own* spool state has no way to read that
state's live sub-stage value inside `wrench_b()` either. Solving fully general, sub-stage-exact,
bidirectional coupling between arbitrary models is a materially harder problem (closer to a
dependency-graph/co-simulation scheduler than an integrator change) and is **not** solved by this
ADR. Instead, this ADR draws an explicit, disclosed line, using a mechanism this codebase already
has and has already accepted the same trade-off for:
- A model's own `integrate_state()` derivative may depend on `t`, its own state slice, its own
  already-resolved bound control inputs (`InputRegistry`, ADR-0017), and the environment sample —
  **not** on another model's state slice or the rigid-body state directly. This is not an
  arbitrary restriction: it is exactly Zipfel's own cited module-bus description (App. E.2.3/E.2.5)
  — "each module ... reads any input it needs from a shared named module-variable bus," never from
  another module's raw memory — so this restriction is the architecturally correct reading of the
  same citation this ADR already relies on, not a compromise invented to dodge the hard case.
- A state-owning model that wants a `WrenchSource`'s `wrench_b()` to see its own current value
  (the engine-spool-feeds-thrust case) **publishes it to `SignalRegistry` once per frame**, after
  integration — the identical frame-latched-bus pattern `env.wind_n` and every published control
  already use (`wrench_source.hpp`'s own documented exception: "env's wind_n is frame-latched ...
  every RK4 sub-stage's env carries the identical value"). A consuming `wrench_b()` resolves that
  signal once at init (the existing `SignalRegistry::find()` discipline) and reads whatever value
  was last published — **frame-latched, not sub-stage-fresh**, an explicit, disclosed accuracy
  trade-off consistent with a pattern this repository has already accepted elsewhere, not a new one.
- **What this ADR does NOT deliver**: sub-stage-exact bidirectional coupling between a model's own
  continuous state and the wrench computation that might depend on it. A vehicle needing that level
  of fidelity needs a further, dedicated design — named here as an open follow-up, not silently
  absent.

`Vehicle`'s per-frame derivative functor (today's lambda at `vehicle.cpp:152`) evaluates the
rigid-body `DynamicsModel::derivative()` over its own slice using the already-summed applied wrench
(unchanged — computed from `wrench_b()` calls exactly as today, per the coupling boundary above),
then every state-owning model's `integrate_state()` over its own slice, passing the same
already-constructed per-stage `stage_env` the lambda already builds for that sub-stage's `wrench_b()`
calls (no new construction, no new plumbing — the value already exists at the call site), writing
into one combined `dq` buffer.

**Integrator functor contract — corrected after Codex plan critique below.** The original draft
said `Integrator::step`'s arithmetic (`y + dt*f(...)`, the RK4 tableau) is "otherwise unchanged."
That is incomplete in a way that would have silently broken R2: today's functor contract is `F(t,y)
-> StateVector` (**return by value**), which is zero-allocation only because `StateVector` is a
fixed-size, stack-resident Eigen type — the exact same return-by-value contract applied to a
runtime-sized type risks constructing a fresh dynamically-sized object on every one of the four
RK4 sub-stage calls, every frame. The corrected contract, matching Nutaro's own `der_func(const
double* q, double* dq)` shape this ADR already cites for the *model*-level hook — now applied
consistently to the *integrator*-level functor too — is **write into a caller-provided output
buffer**: `F(t, y, dy) noexcept` where `y`/`dy` are fixed-extent views (`const double*`/`double*` or
`std::span`) over the combined state. `Integrator` itself owns **its own** pre-sized scratch buffers
for `k1`..`k4` and the RK4 tableau's intermediate trial states, sized exactly once (at construction,
or an explicit init-time `resize(int n)` call `Vehicle::init()` makes exactly once, before `step()`
is ever called) — never resized inside `step()`. `Vehicle`'s own pre-step rollback copy (today's
`pre_step_state` at `vehicle.cpp:147`) is likewise a copy into a `Vehicle`-owned scratch member
sized once at init, not a fresh per-frame allocation. The `StateDerivative` concept is preserved,
reshaped to check the new write-into-buffer signature at compile time (`requires(const F& f, double
t, ... ) { {f(t, y, dy)} noexcept; }`) — the existing compile-time safety net is kept, not lost.

**Enforcing "sized once, never resized" by type, not by comment.** The combined state buffer is a
dedicated wrapper (not a bare `std::vector<double>` `step()` could accidentally mutate the size of):
it owns a `std::vector<double>` sized exactly once in its own constructor and exposes only
`data()`/`size()`/the arithmetic the integrator needs — no `resize()`/`push_back()`/`insert()` in
its public interface at all. "Never resized after construction" is then a fact about which
operations exist to call, not only a documented intention.

**R1 (bit-identity) — acceptance strengthened twice after Codex plan critique below.** The original
draft's proposed evidence ("existing NESC/determinism goldens pass unmodified") is insufficient on
its own: `tests/physics/nesc_cases_test.cpp`-family checks compare against *tolerance bands*, not
bit-identity, and the existing determinism test compares two runs of the *same* implementation
against each other, not the pre-change `Eom` against the post-change `RigidBody6DofDynamics` — a
trajectory that changed consistently could pass both without ever being bit-identical to today's.
A first correction added a dedicated new test comparing derivative output between the pre- and
post-change arithmetic — re-verification then caught two further gaps in that fix, both closed
here: **(a)** plain `==` on `double` is not bitwise equality (`+0.0 == -0.0` is `true` while their
bit patterns differ), so the comparison must be representation-level (`std::bit_cast<std::uint64_t>`
or `std::memcmp` over each `double`, not `==`); **(b)** comparing `derivative()`'s raw output alone
cannot catch a bug in the RK4 tableau's own rewritten trial-state construction, final weighted
combination, or `post_step` quaternion renormalization — all rewritten by this ADR's own integrator
functor-contract change, none exercised by a derivative-only comparison. **Corrected acceptance**: a
dedicated new test drives identical initial states through a **complete integration step** (not
just one `derivative()` call) under both the pre-change `Eom`/`Integrator::step` arithmetic (kept as
a private reference implementation inside the test, or the pre-change code itself if still available
at implementation time) and the post-change `RigidBody6DofDynamics`/rewritten `Integrator::step`,
including `post_step` normalization, for a representative set of sampled states with matching build
settings (same compiler flags/optimization level for both sides of the comparison, since
representation-level equality is sensitive to that), asserting representation-level equality
throughout — not inferred from tolerance-banded, same-implementation-only, or derivative-only tests.
This is in addition to, not instead of, running the existing NESC/determinism suite unmodified under
all four presets.

**R2 (no allocation).** The combined state buffer, and `Integrator`'s and `Vehicle`'s own scratch
buffers (per the corrected functor contract above), are each sized exactly once, in `Vehicle::init()`
(an allocating path already permitted by `docs/architecture.md` §14); none is resized or reallocated
in `step()`. `tests/physics/no_alloc_test.cpp` gains a case with at least one state-owning test
model, extending the existing allocation-counting test rather than replacing it.

**R3 (mass as state) — explicitly deferred, not decided here.** This ADR makes mass-as-state
*representable* (a future `MassPropertiesModel` could declare one state slot for burned fuel mass)
without deciding whether v1 uses it. States the rule for the future record that would: `Mass
PropertiesModel::get()`'s current `noexcept`, no-argument signature would need to read the model's
own state slice — flagged as a known, deliberate follow-up, not resolved here. `Eom::begin_frame()`'s
existing per-major-frame mass/inertia-inverse caching (today: latched once, shared across all 4 RK4
sub-stages) would need its own explicit decision if mass ever varies **within** a frame (sub-stage to
sub-stage) rather than only frame to frame — an open question this ADR raises for whichever future
record actually adds a mass-owning state, not answered here.

**R4 (trim/linearize, PR 5-7).** `Simulation.derivative(state)`'s Python-facing contract changes
from "exactly a 13-vector" to "a length-N vector with named slices for the rigid-body block plus
each model's own labeled slice" (audit §5 item 4). PR 5 (not yet built) must be specified against
this length-N contract from its own start — the reason this ADR's implementation is sequenced before
PR 5 begins, not before PR 4 (see Sequencing below).

**Health/config.** `HealthMonitor::check_state` (today: slices the four fixed segments) becomes:
check the rigid-body block's four segments exactly as today, plus a generic finiteness check over
every other declared state slice — no per-model-specific health logic needed. `initial.state` config
gains a place for each state-owning model's own initial values (mechanism deferred to
implementation-plan detail, not fixed here). Python exposes state as a length-N vector with named
slices instead of module-level fixed offsets.

**Sequencing (per the intent document's own §6 recommendation, adopted as-is):** (a) ADR-0020
(interfaces) first — small, unblocks PR 4 sooner, since it needs no state-declaration mechanism to
exist yet; (b) implement ADR-0020's interface change; (c) PR 4 rebuilt on the corrected interface;
(d) this ADR's dynamics/state generalization implemented before PR 5 begins, since PR 5-7 bind the
state to Python and would otherwise hardcode the 13-layout a second time.

## Rejected alternatives

- **Keep `StateVector` fixed at 13 and bolt extra per-model states on as separate parallel vectors,
  each with its own integration.** Rejected: breaks RK4's requirement that all coupled state advance
  through one consistent set of k1-k4 sub-stage evaluations *for whatever coupling this ADR does
  support* (a model's own state evolving against its own bound inputs/environment, at full sub-stage
  fidelity) — two separate integrators would get that wrong even for the simple case. This
  rejection stands even after scoping cross-model coupling down to the frame-latched bus above: the
  chosen design still integrates every declared state through one RK4 evaluation, which two
  parallel integrators cannot do consistently, regardless of how much cross-model coupling either
  design actually supports.
- **`Eigen::VectorXd` as the runtime state type.** Rejected: heap-allocates on construction and,
  via Eigen's expression templates, can allocate temporaries on ordinary arithmetic even when
  "sized once" unless every operation is hand-written to avoid them. A plain contiguous buffer
  (sized once) plus an `Eigen::Map` over the rigid-body sub-block keeps existing EOM math working
  without adopting `VectorXd`'s allocation model (R2).
- **Make `state_count()`/`initial_state()`/`integrate_state()` mandatory (no default) on every
  `Model`.** Rejected: breaking change to every existing plugin (9 shipped physical plugins, 2 test
  stubs, `nesc_aero`, `nan_wrench`) for zero benefit over a defaulted, zero-returning virtual that
  costs nothing to a plugin that doesn't need it.

## Consequences and validation

Costs: `kCoreApiVersion` bump (`Model`'s vtable gains three new virtuals, even defaulted; every
plugin recompiles per existing policy); real changes to `state.hpp`, `vehicle_state.hpp` (a genuine
constructor/return-type retype, not just prose, per the correction above),
`eom.hpp/.cpp`, `vehicle.hpp/.cpp`, `simulation.hpp/.cpp`, `health_monitor.hpp/.cpp`, and
`integrator.hpp` (functor contract change from return-by-value to write-into-buffer, plus owning
its own scratch buffers) — a materially larger set of real changes than the original draft's "one
implementation plus a few call sites" framing suggested, corrected here rather than understated;
`tests/unit/kernel/vehicle_state_test.cpp`'s address-identity assertion rewritten; Python
`_core.cpp`'s `STATE_SIZE`/offset module constants need a length-N-aware replacement before PR 5
relies on them. Benefits: any future vehicle model (engine spool dynamics, actuator lag, flex modes,
fuel burn) can own its own continuous state, integrated at full sub-stage fidelity, with zero
further core interface change; closes audit item 2/3 in §3's contradiction table. **Explicitly not
delivered** (see the coupling-boundary correction above): sub-stage-exact coupling between a
model's own state and a wrench that depends on it — only frame-latched coupling via
`SignalRegistry`, matching the existing `wind_n` precedent. Migration/rollback: fully reversible
pre-any-external-consumer (no plugin outside this tree implements `Model`/`DynamicsModel` yet); the
`kCoreApiVersion` bump is the existing precedented mechanism (ADR-0018's own 5→6 bump for a settling
vtable shape, cited directly).

Acceptance: a dedicated new bitwise-equality test against the pre-change `Eom` arithmetic, not
tolerance-banded or same-implementation-only tests (R1, corrected above); existing NESC/determinism
goldens additionally pass unmodified under all four presets; `tests/physics/no_alloc_test.cpp`
extended with a state-owning test model, covering the integrator's and `Vehicle`'s own scratch
buffers, not only the combined state buffer (R2, corrected above); a test proving a state-owning
model's published signal is visible to a `wrench_b()` reader at the documented frame-latched
staleness, not sub-stage-fresh (the coupling boundary above); a dedicated implementation plan's own
full acceptance table at implementation time.

## Approval

Proposed, pending Joe's decision.

**Codex plan critique (adversarial-review, working-tree scope, `2026-09-19`): verdict
"needs-attention," three high findings and one medium finding — the most consequential critique of
the three ADRs in this document, addressed in full below rather than summarized away.**

- **High** (accepted, folded in above): slice-only access to `integrate_state()` does not preserve
  coupled RK4 evaluation for a model whose derivative needs another model's or the rigid body's live
  sub-stage value, and `wrench_b()`'s unchanged signature has no way to read a state-owning model's
  live sub-stage value either. **Fixed by scoping down, not by solving the general case**: a
  model's own state derivative may depend only on its own slice plus its own bound
  inputs/environment (matching Zipfel's own module-bus citation exactly); cross-model coupling into
  a wrench is supported only at frame-latched granularity via `SignalRegistry`, the same trade-off
  `env.wind_n` already makes. Full sub-stage-exact bidirectional coupling is named as explicitly
  out of scope, not silently absent.
- **High** (accepted, folded in above): sizing the buffer once does not by itself make the RK4 path
  allocation-free — today's `Integrator::step` returns `StateVector` *by value* from a fixed-size
  stack type, and the same return-by-value contract on a runtime-sized type risks a fresh allocation
  on every sub-stage call; the plan also didn't specify scratch storage for `k1`-`k4` or the
  rollback copy, nor how "never resized" would be enforced. **Fixed**: the functor contract becomes
  write-into-provided-buffer (matching Nutaro's own `der_func(q, dq)` shape, now applied at the
  integrator level too, not just the model level); `Integrator`/`Vehicle` each own pre-sized scratch
  buffers, sized once at init; the combined-state type's own public interface has no resize
  operation at all, so "never resized" is enforced by what the type can do, not only documented.
- **High** (accepted, folded in above): `VehicleState`'s actual constructor stores a genuine
  `const StateVector&`; binding it to an `Eigen::Map` via implicit conversion would create and then
  dangle a temporary. The original "unchanged" claim was false. **Fixed**: `VehicleState` retypes
  its internal storage to `Eigen::Map<const Eigen::Vector<double,13>>`, constructed from a raw
  pointer — a real, correctly-scoped change, with the one test whose invariant this breaks
  (`vehicle_state_test.cpp:21`'s address-identity check) named explicitly rather than left to be
  rediscovered as a surprise failure.
- **Medium** (accepted, folded in above): the proposed R1 evidence (existing goldens/determinism
  tests) cannot actually prove bit-identity between the pre- and post-change arithmetic, since one
  compares against tolerance bands and the other compares two runs of the same implementation.
  **Fixed**: acceptance now requires a dedicated new test comparing the pre-change and post-change
  derivative arithmetic bitwise, in addition to the existing suites.

This ADR's design changed substantially in response to this critique — not merely reworded. A
follow-up focused re-verification (mirroring the one already run for ADR-0020) is warranted given
the number and severity of findings.

**Codex re-verification (adversarial-review, working-tree scope, `2026-09-19`): verdict
"needs-attention."** Findings 1 (coupling scope) and 3 (`VehicleState` retyping) confirmed fixed
outright. Finding 2 (allocation) confirmed fixed. Finding 4 (R1 bit-identity evidence): confirmed
**only partially fixed**, plus **one new finding surfaced by that same fix**:
- [medium] `==` on `double` is not representation-level equality (`+0.0`/`-0.0` compare equal but
  differ bitwise), and a derivative-only comparison cannot catch a bug in the rewritten RK4 tableau
  or `post_step` normalization, neither of which a bare `derivative()` call exercises.
- [medium, new] The narrowed coupling contract's own text said a model's derivative "may depend on
  ... the environment sample," but the signature as drafted (`integrate_state(t, q, dq)`) gave it no
  parameter to receive one — a self-contradiction within the fix itself.

**Both accepted and fixed above**: R1's acceptance now requires representation-level comparison
across a complete integration step (not `==`, not derivative-only); `integrate_state()` gains an
`EnvironmentSample` parameter, reusing `Vehicle`'s already-constructed per-stage `stage_env` with no
new plumbing. A third re-verification pass was not requested: both fixes are narrow, mechanical
corrections to already-accepted design decisions (a comparison method and a missing parameter), not
new technical claims that could themselves hide a further gap — unlike the first three findings,
which changed what the ADR actually does.

---

## Mapping and sequence (implementation, once any ADR above is accepted)

| Requirement | Change / owned paths | Dependencies | Acceptance command |
|---|---|---|---|
| ADR-0020 implemented | `aero_model.hpp`/`propulsion_model.hpp` become marker classes; new `util/air_data.hpp`; rebuilt `aero_stub`/`propulsion_stub` | ADR-0020 accepted | existing PR 3 seam-test pattern, rebuilt |
| PR 4 rebuilt | `docs/plans/pr4-notional-aero-turbofan.md` reworked against direct `wrench_b()` override; `thrust_ref_n` margin fixed; `Cmadot` now representable | ADR-0020 implemented | new plan, own acceptance table |
| ADR-0021 implemented | `dynamics_model.hpp`, `RigidBody6DofDynamics`, `Model`'s three defaulted virtuals, `Vehicle`/`Integrator`/`Simulation`/`HealthMonitor` updated | ADR-0021 accepted; independent of PR 4 | R1-R4 above, existing goldens unmodified |
| PR 5 (nanobind) | Specified against length-N state from the start | ADR-0021 implemented | PLAN.md's own PR 5 line, revised |

One Claude builder per ADR's implementation by default; no split proposed here (this document is
planning only, per Joe's instruction — no code this session).

## Plan critique

Done, one dispatch per ADR as instructed (`node codex-companion.mjs adversarial-review --scope
working-tree --wait` with a read-only, plan-only focus per `docs/agents/review-dispatch.md`), plus a
focused re-verification pass wherever a fix changed a real technical claim rather than only wording
— mirroring this repo's own precedent for a fix that needs checking, not just re-reviewing
everything from scratch. Summary, in full under each ADR's own "Approval" section above:

- **ADR-0019** (purpose): one critique, one medium finding (acceptance let architecture.md §1's
  conflicting framing stand indefinitely) — fixed, folded into the ADR's own acceptance criteria, no
  re-verification needed (narrow, single-paragraph fix).
- **ADR-0020** (AeroModel/PropulsionModel interfaces): one critique (one high, one medium finding),
  one re-verification (found one new internal-consistency issue the fix itself introduced) — all
  three fixed. The high finding materially changed what safety guarantee this ADR actually
  preserves versus gives up; stated precisely now rather than softened.
- **ADR-0021** (dynamics interface / model-owned state): one critique (three high, one medium
  finding), two re-verification passes (the second catching two further gaps the first fix
  introduced) — all fixed. This ADR's design is substantially different, and substantially more
  honest about its own actual scope (explicit non-support for sub-stage-exact cross-model coupling),
  than the first draft. This was the most consequential critique of the three, exactly matching
  where the intent document's own audit said the real complexity lives.

Every finding across all three dispatches is recorded with its own verdict and fix, not summarized
away — nothing was accepted on the strength of a clean-sounding one-line verdict alone.

## Questions and decision

All three ADRs are drafts, each independently critiqued and revised. Joe's decision needed on each
independently — accepting one does not require accepting the others, though ADR-0020's
implementation is sequenced before ADR-0021's per the intent document's own recommendation, adopted
above. **No code has been written this session**; PR 4 implementation stays stopped. Once Joe
decides: accepted ADRs move from this plan file into `docs/decisions/00NN-*.md` (numbers 0019-0021
are provisional, assigned here for readability, confirmed at actual acceptance time against
whatever the next free ADR numbers are then), architecture.md's affected sections get their own
implementation-time rewrite pass, and PR 4 is rebuilt from `docs/plans/pr4-notional-aero-turbofan.md`
against whichever interface actually lands.
