# PR 3 — `AeroModel`/`PropulsionModel` as `WrenchSource` specializations

Status: **approved** (Joe, 2026-09-19) — one Codex plan critique complete (twelve findings, all
accepted and folded in), plus one further revision from Joe's own follow-up review (citations
re-verified, (b)'s no-silent-NaN guarantee made unconditional, a V→0 plugin-contract obligation
added, R8a dropped); no second critique needed (see "Revision 2" section — none of the four
changes reopen (a)/(c)/(d)/(f))
Spec/brief: this file (small enough to keep spec + plan together, per `plan-feature/SKILL.md` step 3)
Risk: **Tier 3** (`aircraft/**/physical`/`hardware` boundary per `docs/process/workflow.md`'s risk
table; also a `core/interfaces/` addition and a `kCoreApiVersion` bump)
Owner: Joe
Plan approval: **Approved by Joe, 2026-09-19.** Decisions: (a) option 1; (b) option 1 +
`isfinite` guard; (c) option 1 + `requires_environment()` `final` returning `{wind: false,
atmosphere: true}`; (d) as pinned (no open question); (e) deferred, no hook; (f) `kCoreApiVersion`
5→6 + new ADR-0018. Unit-suffixed field names (`alpha_rad` etc.) accepted, following `AtmoState`'s
existing precedent — the ADR-0002 wording ambiguity is recorded as a `PLAN.md` follow-up, not
resolved by this PR. Authorized: `docs/decisions/0018-*.md` written from this plan's draft ADR,
status "accepted", as part of this PR's implementation. No `tests/golden/**` or tolerance-file
changes authorized. Correction budget for implementation: the normal two autonomous review-fix
rounds (`docs/process/workflow.md`), 0 used so far — **corrected**: an earlier handoff misread
Joe's "Correction budget 0/2" as zero rounds allowed at all; it is the `n/2`-used notation
`docs/process/workflow.md` already defines, not a cap of zero.
Intent/approval: `PLAN.md` line ~588 (M3 PR 3); user instruction 2026-09-19 resuming M3 after
PR 2 (#38) merged as `c5f7633`
Architecture/ADRs: `docs/architecture.md` §5, §7 (sketch + "Status" note), §9, §13, §15;
ADR-0001 (frames), ADR-0002 (units), ADR-0003 (state), ADR-0016 (`wrench_b()` per-stage purity),
ADR-0017 (write seam, `bind_inputs()`); draft ADR-0018 below
System context: `AeroModel`/`PropulsionModel` are the first two concrete `WrenchSource`
specializations (M2 PR 3's `nesc_aero` test plugin implements `WrenchSource` directly and is not
touched by this PR). They sit in the existing per-frame wrench-accumulation stage
(`docs/architecture.md` §5): `Vehicle::integrate()`'s derivative functor calls every discovered
`WrenchSource::wrench_b(vs, env)` at each of the four RK4 sub-stages (ADR-0016) and sums the
result before the EOM. This PR adds no new scheduler stage and touches no scheduling code — it
only gives two new `Model` subclasses a concrete `wrench_b()` implementation that reads
`(vs, env)` and a physical-quantities-only view of it (`AeroInputs`/`PropulsionInputs`), then
calls a new pure-virtual `compute()` a concrete plugin (PR 4) implements. Controls arrive through
ADR-0017's already-built seam (`InputRegistry`/`bind_inputs()`), never through the `Inputs` structs
— see (a) below for the design choice this settles.

## Outcome and boundaries

**Problem.** `docs/architecture.md` §7 sketches `AeroModel`/`PropulsionModel`/`AeroInputs`/
`PropulsionInputs` but states plainly: "no concrete implementation exists yet... pinned by
PLAN.md M3 PR 3's own ADR." Nothing today turns a `VehicleState`+`EnvironmentSample` into the
airspeed/α/β/q̄/Mach quantities an aero or propulsion model actually wants, and no concrete class
exists for PR 4's linear-coefficient aero or thrust-lapse turbofan to derive from.

**Outcome.** Two new abstract classes, `AeroModel` and `PropulsionModel`, both deriving from
`WrenchSource` (not `Model` directly — correcting the sketch, see (a)/finding below), each
providing a concrete, `final`, per-frame-path-legal `wrench_b(vs, env)` that assembles a plain
`AeroInputs`/`PropulsionInputs` struct and forwards to a new pure-virtual `compute()` the concrete
plugin implements. `compute()` is `const noexcept` (PLAN.md's own line), matching `wrench_b()`'s
existing purity contract and ADR-0016's per-RK4-substage evaluation. No plugin exists yet: this
PR is the interface plus a stub-based test, matching PR 2's "hand-built `AeroInputs`... without a
simulator" precedent. `aircraft/notional_transport`'s real linear-coefficient aero and turbofan
(PLAN.md PR 4) derive from these next.

**Non-goals.** No concrete aero/propulsion plugin (PR 4). No trim/linearize (PR 6/7). No envelope
monitors — deferred, see (e). No change to `WrenchSource`, `wrench_b()`'s signature, `Wrench`,
`EnvironmentSample`, `InputRegistry`, or the scheduler order. No touch to `tests/golden/**` or any
tolerance file — nothing here needs `touch-goldens`; flagged up front per `AGENTS.md`'s oracle
boundary, and re-confirmed in Acceptance below (every check here is a new unit-level test on a
stub, not a golden regeneration).

**Invariants preserved.** ADR-0016's per-call purity contract as ADR-0017 already amends it, not
a stricter "depends only on its own arguments" claim: `wrench_b()`, and now `compute()`, must
return the identical value across all four of one frame's RK4 sub-stages, but a bound control
member's value *may* differ frame-to-frame (`wrench_source.hpp`'s own ADR-0017 amendment) — that
is a value read from outside `wrench_b()`'s `(vs, env)` arguments by design, not a violation of
purity, since ADR-0017 already carved out exactly this exception. (Codex plan critique F5 —
verified: the plan's first draft claimed a stricter "arguments only" invariant that ADR-0017's own
amendment contradicts; corrected here, wording only, no design change.) ADR-0001 frames (body FRD,
`q_nb` n→b); ADR-0002 SI everywhere in the new structs, though see (a)/(f)'s naming note below for
a real tension between ADR-0002's literal "no unit suffixes in identifiers" text and the field
names proposed here; ADR-0017's control-input channel is the only write path a plugin may read
from for a control value (this PR adds no second one).

Assumptions: `VehicleState`/`EnvironmentSample`/`InputRegistry`/`WrenchSource::bind_inputs()`
already deliver everything `wrench_b()` needs (confirmed by reading `vehicle_state.hpp`,
`environment_sample.hpp`, `wrench_source.hpp`, `input_registry.hpp` this session — no new kernel
type is needed). `Vehicle::init()`'s existing `WrenchSource`-discovery pass already calls
`requires_environment()` and `bind_inputs()` for any `WrenchSource`, `AeroModel`/`PropulsionModel`
included, with no change needed to `vehicle.hpp`/`vehicle.cpp`. This PR does not itself validate
that a concrete plugin's `compute()` is actually pure/const/noexcept in the way ADR-0016 requires
— that is the same "true by contract, enforced by review" discipline `wrench_b()`'s own doc
comment already relies on, not a new runtime check.

## Contract and failure behavior

**`AeroInputs`** (SI, ADR-0002; all frames body axes unless noted):

| Field | Unit | Meaning |
|---|---|---|
| `true_airspeed_mps` | m/s | `\|V_rel_b\|`, air-relative (wind-corrected) speed |
| `alpha_rad` | rad | angle of attack, `atan2(w_rel, u_rel)` |
| `beta_rad` | rad | sideslip, `atan2(v_rel, hypot(u_rel, w_rel))` — see (b) for why this form, not `asin` |
| `omega_b` | rad/s | `Eigen::Vector3d` (p, q, r), straight `vs.omega_b()` — inertial-referenced, no wind term (`nesc_aero.cpp`'s own comment already states this distinction) |
| `dynamic_pressure_pa` | Pa | `0.5 * rho * true_airspeed_mps^2` |
| `mach` | 1 | `true_airspeed_mps / speed_of_sound_m_s` |

**`PropulsionInputs`:**

| Field | Unit | Meaning |
|---|---|---|
| `true_airspeed_mps` | m/s | identical formula to `AeroInputs`, recomputed independently (separate `WrenchSource`, separate `wrench_b()` call) |
| `atmosphere` | — | `AtmoState` passthrough (density for thrust lapse now; pressure/temperature reserved for a later, more detailed engine model — not read by PR 4's simple lapse model, kept for the struct to not need a breaking change then) |

**Where wind is subtracted.** Inside each of `AeroModel::wrench_b()`/`PropulsionModel::wrench_b()`
(the new, shared, `final`-override assembly step both classes provide) — never inside a concrete
plugin's `compute()`, and never twice. Formula, matching the one existing precedent
(`tests/plugins/nesc_aero/nesc_aero.cpp:77`, and `wind_model.hpp`'s own documented convention):

```cpp
const Eigen::Vector3d v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n);
```

**Control values are not carried in `AeroInputs`/`PropulsionInputs`.** See (a)/(c) below: a
concrete plugin reads its own control(s) from the `const double*` its own `bind_inputs()` override
already holds, exactly like any other `WrenchSource` reads any other member. `compute()` takes
only the physics-derived `Inputs` struct.

**Failure/edge behavior (this PR's own decision, see (b); revised twice — Codex plan critique, then
Joe's own follow-up):** `true_airspeed_mps == 0` is a legitimate physical state (still air, or a
vehicle momentarily at rest relative to the air mass), not a fault — `alpha_rad`/`beta_rad` use
`atan2`-based forms that are well-defined (**finite, never NaN/Inf**) at zero magnitude, matching
`nesc_aero.cpp`'s own stated design virtue ("this form has no 0/0 at v_rel_b = 0"). "Well-defined"
does not mean "always exactly `0`," though: IEEE-754 `atan2`'s signed-zero branch gives
`atan2(+0,-0) = π` (confirmed locally: `python3 -c "import math; print(math.atan2(0.0,-0.0))"` →
`π`), so a bit-exact negative-zero component can give `alpha_rad`/`beta_rad` a sign-discontinuous
but still finite value instead of `0` at an otherwise-identical still-air state — a disclosed,
**not fixed by this PR**, cosmetic discontinuity, never a NaN.

**The no-silent-NaN guarantee is unconditional.** A NaN `AtmoState` can arise from either cause
`environment_sample.hpp`'s own comment names (the domain-misattribution window, or a genuinely
broken in-range `AtmosphereModel`) and would otherwise propagate into `dynamic_pressure_pa`/`mach`.
Rather than let a concrete plugin's own arithmetic decide whether that NaN ever becomes visible,
each base class's `wrench_b()` checks every assembled `Inputs` field with `std::isfinite` **before
calling `compute()` at all**; if any field is non-finite, it returns an explicit all-NaN `Wrench`
(every `force_b`/`moment_b` component set to a quiet NaN — never `Wrench{}`'s own zero default,
which would itself be a silent wrong answer) without ever invoking `compute()`. The existing
`HealthMonitor::check_wrench` (ADR-0016, §15, unmodified) then always halts, naming this plugin's
own path — no new `HealthMonitor` check, no new signal, no API change. This closes, for these two
specific consumers, the gap `environment_sample.hpp`'s own comment discloses as open ("not
surfaced at all for a NaN-tolerant consumer") — see (b) for the full design and R5 for the test
that proves it against a plugin that deliberately ignores the corrupted field.

**"No atmosphere" is not a runtime case at all:** `requires_environment()` is `final` on both new
classes (see (c)'s addendum), unconditionally returning `{wind: false, atmosphere: true}` — a
concrete plugin cannot override this. `Vehicle::init()`'s existing rejection (`ConfigError` naming
`physical.atmosphere`) therefore fires before frame 0 for any vehicle missing it. The one exception
is `Simulation::for_testing()`, which has no atmosphere parameter at all (`simulation.cpp`:
`PhysicsSpec.atmosphere` is hardcoded `nullptr`) and so cannot load either class regardless — R8's
acceptance approach below uses `Simulation::from_config` alone for this reason (see the "Test
infrastructure" section for the options considered).

**V→0 is a separate, plugin-owned obligation this PR does not itself enforce.** The `isfinite`
guard above protects against a non-finite *input*; it says nothing about a concrete plugin's own
`compute()` producing a non-finite *output* at legitimate, finite inputs — the real hazard being
S&L's `1/(2V_T)`-scaled nondimensional rate/damping derivatives (§2.3 Eq. (2.3-9a)), which a naive
port could carry into a literal division by zero at `true_airspeed_mps == 0`, unlike the
*dimensional* force equations (§2.3 Eq. (2.3-8b)) built from the same coefficients, which have no
such singularity. `AeroModel`/`PropulsionModel`'s own class-level doc comments state this as a
contract obligation on `compute()`, the same way `wrench_b()`'s own doc comment already states its
per-RK4-substage purity contract — see (b) for the full account and the Follow-up section for the
named acceptance obligation this places on PLAN.md's own PR 4 line.

## Acceptance

| ID | Observable requirement | Executable check and expected result |
|---|---|---|
| R1 | `AeroModel`/`PropulsionModel` compile as abstract `WrenchSource` subclasses; a stub subclass implementing only `compute()`/`init()`/`reset()`/`step()`/`name()` compiles and links, registered in `tests/unit/CMakeLists.txt` (Codex F11: that file enumerates sources explicitly — no auto-discovery — so the new test binaries need their own entries, not just a matching `ctest -R` filter) | `ctest -R AeroModel` / `-R PropulsionModel` (new, named to actually match `gtest_discover_tests`-generated test names, not a source-file guess), `just build` |
| R2 | `wrench_b()` correctly assembles `AeroInputs` from a hand-built `VehicleState`/`EnvironmentSample`, matching independently hand-computed α/β/q̄/Mach/`true_airspeed_mps`/**`omega_b`** (Codex F10: the first draft's row omitted body-rate passthrough), still air (no wind configured) | new unit test, values computed independently in the test, not copied from the implementation |
| R3 | Same as R2, with `env.wind_n` nonzero and `vs.q_nb()` a non-identity rotation — proves the wind subtraction happens once, in body axes, before α/β/q̄ | new unit test |
| R4 | At `vs.v_b() == 0`, `env.wind_n == 0` (`true_airspeed_mps == 0`, both components exact `+0.0`): `compute()` receives finite `alpha_rad == 0`, `beta_rad == 0`, `dynamic_pressure_pa == 0`, `mach == 0`. **Separately** (Codex F4, verified): with a bit-exact `-0.0` component forced into `u_rel`/`w_rel`/`v_rel`, assert the result is still finite (no NaN/Inf) but do **not** assert it equals exactly `0` — pins the documented signed-zero discontinuity as a known, accepted, finite-valued edge case rather than silently leaving it unspecified | new unit test, per (b), two sub-cases |
| R5 | **Rewritten (Joe's own follow-up, strengthening past Codex F3's fix):** a stub `AeroModel` whose `compute()` explicitly *ignores* `mach` (the hard case — proves the guard, not the plugin, does the catching) fed a deliberately-NaN `AtmoState` (either cause `environment_sample.hpp` discloses, not only the domain-misattribution one): assert `compute()` is never called (an instrumented call counter stays `0`) and `wrench_b()` itself returns an explicit all-NaN `Wrench`; `HealthMonitor::check_wrench` on that `Wrench` reports `HealthCheck::kNonFiniteWrench` naming the stub's own plugin path. This is now an **unconditional** guarantee, not scope-limited to a plugin sensitive to the corrupted field | new unit test — the "a health check nobody has seen fire is not known to work" (§15) proof, now for the harder, plugin-indifferent case |
| R6 | `PropulsionInputs.true_airspeed_mps` matches the identical formula/value as `AeroInputs`'s own field for the same `(vs, env)`, computed by two independent `wrench_b()` calls; **and** (Codex F10) `PropulsionInputs.atmosphere` is a bitwise-equal passthrough of `env.atmosphere` (all four `AtmoState` fields, not just density) | new unit test |
| R7 | Hand-built-`Inputs` unit testing works with **no `VehicleState`/`EnvironmentSample`/`wrench_b()` involved at all** — a stub's `compute()` called directly against a by-hand `AeroInputs`/`PropulsionInputs` literal (§7's/PLAN.md's actual promise). **Corrected (Codex F8):** the first draft pointed at R2–R6, which all still construct a `VehicleState`/`EnvironmentSample` and go through `wrench_b()` — that exercises the assembly step, not the "no simulator" promise itself; this is now its own, separate test | new unit test, no `VehicleState`/`EnvironmentSample`/`Vehicle`/`Simulation` symbol referenced |
| R8 | **Revised (Joe's own follow-up drops the test-only `requires_environment()` toggle R8a relied on — now architecturally impossible anyway, since `requires_environment()` is `final` and hardcoded per (c)'s addendum): a single row, via `Simulation::from_config` only.** The real, unconditional `requires_environment().atmosphere = true` shape, end-to-end **with PR 2's write seam actually driving a registered control into `compute()`'s output** (PLAN.md's own PR 3 line, not satisfied by a default-empty `bind_inputs()`): a stub registers one control via `bind_inputs()`, a config literal or direct `write()`+`latch()` sets it, and the resulting `Wrench` reflects it. Built via `Simulation::from_config` with a literal test-fixture YAML declaring `physical.atmosphere` (Codex F1, confirmed: `Simulation::for_testing` has **no** atmosphere parameter at all — `PhysicsSpec.atmosphere` is hardcoded `nullptr` — so it cannot exercise this class regardless of any toggle; `tests/unit/kernel/simulation_input_seam_test.cpp:32`'s own literal-YAML `from_config` helper is the existing, confirmed precedent for this fixture shape). This one row now also proves `wrench_b()`'s reachability from `Vehicle::integrate()`'s per-RK4-substage call — R8a's own original purpose — as a byproduct of the real path, so nothing is lost by dropping it | new integration test |
| R8-guard | The unconditional `isfinite`-guard (b) also applies on this end-to-end path, not only in R5's unit test: with `physical.atmosphere` configured but a forced-NaN sample at one frame (or a stub `AtmosphereModel` returning NaN), `Vehicle::integrate()` halts via `check_wrench` naming the aero/propulsion plugin, never producing a plausible-looking trajectory | extends the R8 integration test, or a close sibling of it |
| R9 | Zero heap allocation across 1000 frames with a stub `AeroModel`+`PropulsionModel` loaded (§14), **including the `isfinite`-guard branch itself** (`std::isfinite` and constructing the all-NaN `Wrench` sentinel are both allocation-free) | extends the existing `tests/physics/test_no_alloc` fixture (R6 in PR 2's own table is the precedent for adding a fixture here) |
| R-V0 | **New (task instruction 3): the V→0 plugin-contract obligation this PR states but does not itself enforce.** Not this PR's own acceptance — `AeroModel`/`PropulsionModel` compute no nondimensional rate and have nothing of their own to test here. Recorded as a **named acceptance obligation on PLAN.md's own PR 4 line** instead (Follow-up, below): PR 4's linear-coefficient aero must prove its own `compute()` returns a finite `Wrench` at `true_airspeed_mps == 0`, specifically exercising whatever nondimensional-rate term its stability derivatives use | PLAN.md PR 4's own acceptance table, not this PR's |
| R10 | `kCoreApiVersion` bumped 5→6; a plugin descriptor built against version 5 is rejected loading against the new binary (same shape as the existing `bad_version` test, PR 2's R12) | `ctest -R Plugin` |
| R11 | `docs/architecture.md` §7's C++ sketch and prose corrected: `compute()` marked `const noexcept`; `AeroModel`/`PropulsionModel` shown deriving from `WrenchSource`; "and the control surface positions"/"throttle commands" reworded to point at the existing `bind_inputs()`/`InputRegistry` control channel (an open design choice this ADR settles, not a self-contradiction being repaired — see (a)'s corrected framing); "Status" note updated to say the interface is now real, plugin(s) pending PR 4. **Corrected verification (Codex F11):** `git diff --stat` shows *that* something changed, not *that* the new prose is technically correct — actually re-reading the edited §7 section against this plan's own Contract fields is the real check | manual re-read of the edited §7 section against this plan's Contract table, plus `git diff --stat` for scope |
| R12 | Full gate green | `./scripts/check-full.sh` |

No golden file, tolerance file, or `docs/decisions/000X-*.md` acceptance record is touched by
implementation; ADR-0018 itself (drafted below) is `docs/decisions/`, which **does** require Joe's
authorization to write for real — flagged, not assumed, per `AGENTS.md`'s oracle-adjacent-file
boundary (an ADR is not `tests/golden/**`, but the same "ask before touching" instinct applies to
any file under `docs/decisions/`, and the task instruction says so explicitly: draft only, ask
before any `docs/decisions/` write).

## Design questions (a)–(f)

Each below: options, recommendation, and why — citing repository precedent, then the assigned
texts where they bear. **Citation note, corrected from this plan's first draft:** the first draft
found no Stevens & Lewis/Zipfel file under this worktree's own `docs/refs/` and flagged every S&L/
Zipfel citation as unverified. That was wrong about the repository, not just imprecise —
`docs/refs/` is gitignored, so a fresh worktree's own copy is empty even though the **main
checkout** (`~/repos/ASTRA/docs/refs/textbooks/`, `docs/process/workflow.md`'s documented
location) has both books in full, plus several more (2026-09-20 `docs/lessons.md` entry records
this miss). Every S&L/Zipfel citation below has now been read and page-checked directly against
those files this session (S&L 3rd ed., the same edition ADR-0016/0017 cite; Zipfel 4th ed.,
likewise), meeting the same bar ADR-0016/0017's own citations were held to. Two of the first
draft's section-title guesses were wrong and are corrected throughout this section: S&L §2.4 is
**Static Analysis** (trim/stiffness analysis about one axis — nothing to do with wind axes,
dynamic pressure, or aerodynamic angles, which live in §2.2/§2.3 instead, below), and S&L §3.5 is
**Aircraft Models for Simulation** (not trim/linearization — those are §3.6 "Steady-State Flight"
and §3.7 "Numerical Linearization"); Zipfel's aerodynamic/propulsion subsystem-module content is
organized under **§9.2/§10.2, both titled "Subsystem Models,"** for the five-DoF and six-DoF
simulation chapters respectively, not a blanket "Ch. 9–10" reference.

### (a) `AeroInputs`/`PropulsionInputs` fields, units, frames; where wind is subtracted

**Open design question, not a self-contradiction (corrected framing — Codex plan critique F7,
verified):** the first draft of this plan called `docs/architecture.md` §7's control-field
language and its "Control inputs in v1" paragraph "inconsistent with each other." On rereading
both against ADR-0017's own Decision section, that overstates it: §7's sketch (`AeroInputs` carries
"the control surface positions") describes what a `compute()` call *could* take, while "Control
inputs in v1" and ADR-0017 describe the storage/write/latch seam a control value flows through —
different layers that **can coexist**: nothing stops a base `wrench_b()` from dereferencing a
plugin's own already-`bind_inputs()`-resolved `const double*` and copying that value into an
`AeroInputs` field before calling `compute()`, since `InputRegistry::add()`'s own contract already
gives a stable, initialization-resolved pointer good for the vehicle's lifetime (no per-frame
allocation or name lookup required to do this). This is a genuine, undecided design choice §7's
sketch left open (its own "Status" note says as much: "pinned by PLAN.md M3 PR 3's own ADR"), not
architecture.md contradicting itself — corrected here and in the draft ADR's Context below.

**Options.**
1. *Controls stay out of `Inputs`; a concrete plugin reads its own bound `const double*` directly
   in `compute()`* (matching S&L's own F-16 example ADR-0017 already cites: `F(TIME,X,XD)` takes no
   controls parameter at all — they arrive through `COMMON/CONTROLS`, a channel outside the
   derivative call's own arguments).
2. *A fixed set of named control fields on `AeroInputs`* (`elevator_rad`, `aileron_rad`,
   `rudder_rad`) sized for a conventional transport, populated by the base `wrench_b()` from three
   hardcoded `bind_inputs()`-registered names.
3. *A small fixed-size array* (`std::array<double, kMaxControls>`) with positions assigned by
   convention.
4. *A non-owning view over the plugin's own already-resolved `bind_inputs()` pointers*, exposed
   through `AeroInputs` generically (e.g. a bounded span of name/value pairs) so `compute()` can
   look a control up by name without the base class needing a fixed field list. Technically
   possible without heap allocation, since `InputRegistry`'s own storage is already stable and
   resolved at init (Codex's own point, correctly raised against the first draft's "impossible
   without a heap map" claim).

**Recommendation: option 1**, on engineering merits rather than the stronger "only option
consistent"/"impossible otherwise" claim the first draft made (both retracted per the correction
above). Option 2/3 still bake one airframe's control layout into a framework interface, against
the "anything that knows a wing area... lives under `plugins/`/`aircraft/`, never `core/`" rule
this repository states for itself. Option 4 is real but reintroduces a name-keyed lookup inside
`compute()` on the per-frame path — the exact pattern `SignalRegistry`/`InputRegistry`'s own
documented convention ("resolve once at init, the per-frame path holds the pointer and never calls
find() again," `input_registry.hpp`) already established is worth avoiding, and a plugin that
already resolved its own `const double*` at `bind_inputs()` time has no need to re-look it up by
name inside `compute()` at all — it can hold that pointer as its own member exactly like
`nesc_aero` holds `cd_`, and read it directly wherever its own force/moment math needs it, with no
`Inputs`-struct involvement either way. Option 1 is simplest and matches this existing,
already-working pattern; `docs/architecture.md` §7's own sketch prose is updated (R11) to describe
this choice, not to repair a claimed defect.

**`true_airspeed_mps`/`alpha_rad`/`beta_rad`/`omega_b`/`dynamic_pressure_pa`/`mach` fields, units,
wind subtraction:** as tabulated in Contract and failure behavior above; not independently
optional — no alternative form was found in this repository (`nesc_aero.cpp` is the only existing
consumer of `env.wind_n`/`vs.q_nb()` together, and it uses exactly this formula for its own drag
`v_rel_b`, cited above). **Citations corrected this revision, page-checked against
`~/repos/ASTRA/docs/refs/textbooks/` directly (see the citation note above):**
- Dynamic pressure: S&L §2.2 "Basic Aerodynamics," Eq. (2.2-1), p.67: `q̄ = ½ρV_T²`.
- `α`, `β`, `V_T`: S&L §2.3 "Aircraft Forces and Moments," Table 2.3-1 / Eq. (2.3-6a,b), pp.77–78:
  `tan α = W'/U'`, `sin β = V'/V_T`, `V_T = |v_rel|` — the canonical form is `sin β = V'/V_T` (an
  `asin`), not the `atan2(v_rel, hypot(u_rel,w_rel))` form (b) below actually uses; that
  reformulation is this plan's own numerical choice to avoid the `asin` form's `0/0` at `V_T = 0`,
  not something S&L present or discuss at this page.
- The nondimensional-rate hazard driving (3) below: S&L §2.3, subsection "The Aerodynamic
  Derivatives," Eq. (2.3-9a), pp.80–81: `ΔC = C()(α,β,M,h,δ_s,T_c) × k/(2V_T) × rate`, with
  `p̂ = Pb/(2V_T)` named "a dimensionless roll rate" — a genuine `1/V_T` term, unlike the
  *dimensional* force/moment equations, Eq. (2.3-8b), same section: `D = q̄SC_D` etc., finite and
  `→0` at `V_T = 0` since `q̄` itself is.
- Control vector kept separate from state: S&L §2.5 "The Nonlinear Aircraft Model," Eq.
  (2.5-3)/(2.5-4), pp.109–110 — `X` (state, no controls) and `Ū = [δ_t,δ_e,δ_a,δ_r,W_N,W_E,W_D]ᵀ`
  (controls, plus wind) are two separate vectors; confirmed accurate on rereading, unchanged from
  the first draft.
- Zipfel §9.2 "Subsystem Models," pp.319–320 (not "Ch. 9" generally): explicitly decomposes a
  vehicle into separate mathematical subsystem modules — "airframe [aerodynamics], propulsion,
  controls, autopilot, sensors, guidance, navigation" — the same decomposition this PR's own
  `AeroModel`/`PropulsionModel` split reflects; its own Eq. (9.28) combines thrust `F`, dynamic
  pressure `q`, and `α` in one specific-force equation, the same three-quantity relationship
  `AeroInputs`/`PropulsionInputs` carry separately. The same page also states plainly that, at this
  level of simulation, "controls are not modeled explicitly... actuators need not be included" —
  independent corroboration for (c)'s choice to keep control/actuator concerns out of
  `AeroModel`/`PropulsionModel` entirely. Zipfel §10.2.1 "Aerodynamics" and §10.2.3 "Aerodynamic
  Controls"/"Thrust Vector Control" (six-DoF chapter, pp.406+/460+) repeat the same split one level
  up: aerodynamic force/moment modeling and control-surface/actuator modeling are separate
  subsections, matching this plan's own aero-reads/actuator-writes separation (c). The
  module-variable-bus architecture itself (one named signal, many readers) remains ADR-0017's own
  already-verified citation (Fig. 9.39, App. E.2.3/E.2.5) — not re-derived here. **Field naming vs. ADR-0002
(Codex plan critique F12, verified):** ADR-0002's own "Consequences" section states "No unit
suffixes in identifiers (`altitude`, not `altitude_m`) inside the framework" — the proposed
`alpha_rad`/`dynamic_pressure_pa`/`true_airspeed_mps` field names directly carry unit suffixes,
against that literal text. `AtmoState` (`atmosphere_model.hpp`: `temperature_k`, `pressure_pa`,
`density_kg_m3`, `speed_of_sound_m_s`) — the one existing sibling struct in the same
interfaces-family header set — already departs from that same text the identical way; this plan
follows that closest existing precedent for consistency within `core/interfaces/` rather than
inventing a bare-name convention no other physical-quantity struct in this codebase actually uses.
Flagged, not resolved: whether ADR-0002's text means signal-name strings specifically (its own
example, `registry.add("aero.CL", ...)`, is a string) or literally every C++ identifier is a
pre-existing ambiguity this PR did not create and should not unilaterally settle differently from
`AtmoState`. Left for Joe's plan approval to accept explicitly, or to redirect.

### (b) V→0, zero density, no atmosphere: never a silent NaN into `wrench_b`

**Revised this session (Joe's own follow-up review, after the Codex critique below): the
no-silent-NaN guarantee is now unconditional, not scoped to "whenever the concrete plugin's own
math is sensitive to the corrupted field."** The first revision (post-Codex-F3) correctly
identified that `check_wrench` alone only catches a corrupted `AtmoState` when the concrete
plugin's own arithmetic actually reads the bad field — a plugin that ignores `mach`, say, would
silently produce a finite wrench from a NaN atmosphere sample, unobserved. Rather than accept that
narrower guarantee (as the previous revision did) or add a new `HealthMonitor` check (rejected
below, option 3), the base class itself now closes the gap directly, with no new API:

**Options.**
1. *Formulate the singular quantities to avoid a division-by-zero at the boundary, by construction*
   (`atan2`-based α and β rather than `asin(v/V)`, both finite at `V_rel = 0`, matching
   `nesc_aero.cpp`'s own stated design virtue for its drag form, "no 0/0 at v_rel_b = 0"), **and**
   have each base class's `wrench_b()` check every assembled `Inputs` field with `std::isfinite`
   before calling `compute()` at all; if any field is non-finite, return an explicit all-NaN
   `Wrench` (every `force_b`/`moment_b` component set to a quiet NaN — **not** `Wrench{}`'s own
   zero-initialized default, which would itself be the silent-wrong-answer failure mode this design
   exists to prevent) without calling `compute()`. The existing `HealthMonitor::check_wrench`
   (ADR-0016, §15, unmodified) then halts, naming this plugin's own path, unconditionally — no new
   `HealthMonitor` check, no new signal, no `kCoreApiVersion`-relevant API change: `wrench_b()`'s
   own signature and `Wrench`'s own shape are both already exactly what ADR-0016/ADR-0017 pin.
2. *Epsilon-guard `V_rel` and clamp α/β/q̄/Mach to 0 below a threshold.* Rejected: this is exactly
   the "silent NaN" failure mode inverted into a silent *wrong-but-plausible* value instead —
   `AGENTS.md`'s closing line ("a sim that runs from a broken setup is worse than one that
   refuses") and §15's own doctrine both argue against manufacturing a plausible-looking number to
   avoid an honest one; it would also mask a genuinely broken atmosphere behind a threshold check
   that cannot distinguish "V is really zero" from "V is a NaN that happens to compare false
   against every branch."
3. *A new `HealthMonitor` check specific to aero/propulsion inputs* (a `kNonFiniteAeroInputs` or
   similar, checking `AeroInputs`/`PropulsionInputs` themselves before `compute()` runs).
   **Rejected, now on a cleaner basis than before:** option 1's `isfinite`-guard already gives an
   unconditional guarantee through the existing `check_wrench` mechanism, so a second, dedicated
   check would be pure duplication — a second `sim.health` key, a second signal, and a second
   failure mode to test, for zero additional coverage. (It would also still have been the
   "atmosphere-specific bolt-on" `environment_sample.hpp`'s own comment warns against, ahead of the
   general per-frame `EnvironmentSample` domain-validity capability that comment calls for — but
   that is now a secondary reason, since option 1 makes it moot for this PR's own two consumers.)

**Recommendation: option 1, unconditional.** `AeroModel::wrench_b()`/`PropulsionModel::wrench_b()`
never call `compute()` with a non-finite `Inputs` field, of any kind, for any reason — a genuinely
broken atmosphere sample (either cause `environment_sample.hpp` discloses: the domain-
misattribution window, or an in-range but broken `AtmosphereModel`) is caught the same way
regardless of which fields a given concrete plugin happens to read. This closes, for these two
specific consumers, the exact class of gap `environment_sample.hpp`'s own comment discloses as
open ("not surfaced at all for a NaN-tolerant consumer") — not by adding a new check, but by never
letting a NaN input reach a concrete plugin's math in the first place. The general
`EnvironmentSample`-wide gap (any *other*, non-`AeroModel`/`PropulsionModel` `WrenchSource` that
might read `env.atmosphere` directly) remains open and is not this PR's to close — tracked
unchanged as the existing `PLAN.md` follow-up. "No atmosphere" is still handled at init, not here,
by unconditionally declaring `requires_environment().atmosphere = true` on both new classes (now
`final` — see (c)'s addendum below) — never a runtime NaN-vs-init-error ambiguity.

**V→0 is a separate, plugin-owned contract this PR's own framework code does not enforce.** The
`isfinite`-guard above only protects against a non-finite *input*; it says nothing about whether a
concrete plugin's own `compute()` can produce a non-finite *output* at legitimate, finite inputs.
The real hazard at `true_airspeed_mps = 0` is not this PR's own α/β assembly (finite by
construction) but a **concrete aero plugin's own** stability-derivative math: S&L's nondimensional
damping/control derivatives are scaled by `1/(2V_T)` (S&L §2.3 Eq. (2.3-9a): `p̂ = Pb/(2V_T)`, and
the acceleration derivatives Eq. (2.3-10a,b)) — a genuine division by `true_airspeed_mps` that a
naive port of a textbook stability-derivative model would carry directly into a real
divide-by-zero at `V_T = 0`, even though the *dimensional* force/moment equations built from those
same coefficients (S&L Eq. (2.3-8b): `D = q̄SC_D`, etc.) have no such singularity — `q̄ = ½ρV_T²`
and its downstream forces are finite and `→0` at `V_T = 0` by construction. **This PR's framework
code does not compute or guard nondimensional rates at all** — `AeroInputs.omega_b` carries the
plain dimensional `p, q, r` (rad/s), never `pb/2V`/`qc̄/2V`/`rb/2V` — so this hazard is entirely
inside whichever concrete plugin (PLAN.md PR 4's linear-coefficient aero) chooses to nondimensionalize
a rate internally. Stated as an explicit contract obligation on the concrete plugin, not solved by
this PR: **`AeroModel::compute()`'s own class-level doc comment states that a conforming
implementation must return a finite `Wrench` at `true_airspeed_mps == 0`, the same way
`wrench_b()`'s own doc comment already states the per-RK4-substage purity contract** — a plugin
that nondimensionalizes a rate by dividing by `true_airspeed_mps` (or `2×true_airspeed_mps`)
directly needs its own guard (e.g. multiplying by the dimensional rate and coefficient product
first, then dividing, so the `q̄`-like cancellation `nesc_aero.cpp`'s own drag form already
demonstrates applies) — an obligation on PR 4's own plan and acceptance table (see Follow-up),
named here so it is not silently assumed away. The equivalent doc-comment obligation and
acceptance line is added to `PropulsionModel` too, since a thrust-lapse or ram-effect model can
carry the identical `1/V_T` hazard in an intake-recovery or ram-drag term.

Acceptance: R4 proves the framework's own `isfinite`-guard assembly step is non-NaN by construction
at `V_rel = 0` (finite α/β, pinning rather than hiding the IEEE-754 signed-zero discontinuity); R5
(rewritten, see below) proves the unconditional guard actually blocks `compute()` and returns an
all-NaN `Wrench`, using a stub that deliberately *ignores* the corrupted field — the case that
would have slipped through the earlier, conditional design.

### (c) Who owns `bind_inputs` — base class or plugin; what "`ActuatorModel` slots in later" requires

**Options.**
1. *The concrete plugin owns `bind_inputs()` entirely* — `AeroModel`/`PropulsionModel` do not
   override `WrenchSource::bind_inputs()` at all (its existing default-empty body already suffices
   for both, exactly as it does for `nesc_aero`/`nan_wrench` today); PR 4's own `linear_aero`/
   `turbofan` register whichever names they actually read (`controls.elevator`, `controls.throttle`,
   etc.) directly.
2. *`AeroModel`/`PropulsionModel` provide a base `bind_inputs()` registering a fixed, conventional
   set* (elevator/aileron/rudder; throttle), which a concrete plugin's own override calls via
   `Base::bind_inputs(...)` first, then adds any of its own.

**Recommendation: option 1**, for the identical reason (a)'s option 1 was chosen: a fixed
conventional set bakes one airframe's control layout into a framework class, which the "vehicle-
agnostic" charter and "anything that knows a ... thrust curve ... lives under `plugins/`/
`aircraft/`" rule both rule out. `AeroModel`/`PropulsionModel` add no override of `bind_inputs()`
at all — it keeps `WrenchSource`'s own default-empty, non-`final` body, so a concrete plugin
overrides it directly, exactly as `nesc_aero`/`nan_wrench` do today.

**Addendum, answering the second half of this question directly (Joe's own follow-up review):
`requires_environment()` is marked `final` on both `AeroModel` and `PropulsionModel`, and the base
classes own both flags unconditionally — `{wind: false, atmosphere: true}`, always, with no
concrete-plugin override possible.** This is deliberately asymmetric with `bind_inputs()` above,
and for a specific reason: **the base class is what reads `env.wind_n`**, not the concrete plugin
— the wind-subtraction step (`v_rel_b = vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n)`) lives
entirely inside each base class's own `wrench_b()` override (a), so no concrete plugin ever touches
`env.wind_n` directly, and none could meaningfully declare a *different* wind requirement than the
base already has. Wind is `false` (optional), never a per-plugin toggle like `nesc_aero`'s own
`requires_wind_` member, because `EnvironmentSample::wind_n`'s own contract makes "absent" and
"zero" identical by design ("still air is the correct default") — the base class's formula is
already correct with no configured wind at all, so nothing about it ever needs `physical.wind` to
exist. Atmosphere is `true` (mandatory), unconditionally, because (b)'s `isfinite`-guard and both
`Inputs` structs' own fields always depend on a real `AtmoState` — there is no finite, meaningful
default for density or speed of sound the way zero is meaningful for wind. Marking the method
`final` (not just giving it this default, which a subclass could still override) is itself part of
closing (b)'s own gap: if a future concrete plugin could override `requires_environment()` and
return `atmosphere: false`, its own base-class `wrench_b()` would still unconditionally dereference
`env.atmosphere` every frame — reintroducing, by a different route, exactly the class of
init-time-checkable-but-silently-skipped fault (b)'s design is built to prevent, this time by
skipping the `Vehicle::init()`-time rejection that `requires_environment().atmosphere = true`
otherwise guarantees. `final` makes that override impossible to write, not just inadvisable.

**What "`ActuatorModel` slots in later" requires:** nothing new from this PR either way. ADR-0017
already discloses the relevant gap verbatim ("a transport writer or `ActuatorModel` that is not
itself a `WrenchSource`... has no seam-provided route to a write handle for a name it does not
own"). Since `AeroModel`/`PropulsionModel` read their own controls through a plain `const double*`
obtained from the *plugin's own* `bind_inputs()`-time `add()` call — identical in kind to any other
`WrenchSource`'s own bound member — whatever eventually *writes* that shared slot (a config
literal today, Python in PR 5, or a future `ActuatorModel` that either becomes a `WrenchSource`
itself or gets a new, separately-designed binding route) is completely orthogonal to how
`AeroModel`/`PropulsionModel` read it. This PR neither closes nor widens that disclosed gap.

### (d) Output axes and moment reference; where propulsion's r×F transfer happens

Not an open design choice — already pinned by the existing, unmodified `Wrench` struct
(`wrench.hpp`: "About the CG, body axes") and `WrenchSource::wrench_b()`'s own contract
("This model's contribution to the vehicle's applied wrench (about the CG, body axes)"). Both new
classes' `compute()` must therefore return a `Wrench` already referenced to the CG in body axes —
`AeroModel::wrench_b()`/`PropulsionModel::wrench_b()` apply no further transform to `compute()`'s
return value.

**Where the r×F transfer happens for an off-CG-mounted engine:** entirely inside the *concrete*
`PropulsionModel` plugin's own `compute()` (PR 4's turbofan, not this PR). `PLAN.md`'s own PR 1
line already commits to this: "no CG: `MassProps` is `{mass, inertia_b}`... engine-station offsets
live in the turbofan plugin's own config, not in mass properties." `PropulsionModel`'s own
class-level doc comment (this PR) states the contract explicitly — mirroring `wrench_source.hpp`'s
own "about the CG, body axes" comment on `Wrench` — so a future reader of the abstract interface
sees the requirement without having to find PR 1's plan line: *"`compute()` must return a wrench
already transferred to the CG; a plugin whose thrust line does not pass through the CG applies its
own `r × F` using its own config's station offset before returning."* No code in this PR performs
an r×F transfer — there is no concrete engine yet to have an offset.

### (e) §15 envelope monitors: this PR or deferred, and the hook left

**Options.**
1. *Defer entirely; no hook added in this PR.* When PR 4's linear-coefficient aero has real
   declared numbers (α/β/Mach/q̄/load-factor limits), add a new `AeroModel`-level virtual then
   (e.g. `virtual Envelope declared_envelope() const noexcept`), bump `kCoreApiVersion` again at
   that point.
2. *Add an empty/default-returning envelope-declaration virtual now,* so PR 4 only overrides it
   rather than adding a new one.

**Recommendation: option 1.** §15 itself ties this to declared numbers that do not exist before
PR 4 ("the notional transport declares its own"); `AGENTS.md`'s "don't design for hypothetical
future requirements... no half-finished implementations" directly names option 2's shape. The cost
of deferring is one more `kCoreApiVersion` bump in PR 4 — cheap and precedented, though **the
precedent is narrower than the first draft of this plan claimed (Codex F6, verified against
`core_api.hpp.in`'s own comments)**: it is not "one bump per accepted-ADR PR touching a
`WrenchSource`-family vtable" — versions 2 and 3 both landed in the *same* PR (M2 PR 2), and
version 4 bumped for `EarthModel`/`GravityModel`, not the `WrenchSource` family at all. The actual,
narrower and still-sufficient precedent: this project bumps `kCoreApiVersion` whenever *any*
plugin-facing vtable's shape changes or newly settles, as often as that happens within a milestone,
not confined to one interface family — which still supports paying the cost again in PR 4 without
needing the stronger, incorrect claim. **The hook left by this PR: none.** `AeroModel`/
`PropulsionModel` ship with exactly the members this PR's own scope needs; §16 M3's row and this
PR's `PLAN.md` line are the record that envelope monitors remain open, tracked as a `PLAN.md`
follow-up item (below), not a silent gap.

### (f) `kCoreApiVersion`; new ADR vs. follow-on to 0017

**Options.**
1. *Bump 5→6; new ADR (ADR-0018), "elaborates" ADR-0017 without superseding it* (same
   `Related/supersedes` convention ADR-0016 already uses against ADR-0004).
2. *Bump 5→6; a short follow-on section appended to ADR-0017 itself.*
3. *No bump* (reasoning: `AeroModel`/`PropulsionModel` are new types, so no existing out-of-tree
   plugin can be broken by them).

**Recommendation: option 1.** Rejecting option 3 first: `core_api.hpp.in`'s own version-3 bump
precedent is directly on point — `WindModel` was *also* introduced brand-new in the same PR that
bumped the version for it, "so the version this PR ships with names `WindModel`'s one, final
layout, not an intermediate shape that existed only for a few commits inside this same unmerged
branch." The version number is a general vtable-shape-freeze marker for any plugin-facing
interface, not solely a backward-compatibility gate for previously-shipped ones; `AeroModel`/
`PropulsionModel`'s own vtables (via `compute()`) are settling for the first time in this PR,
across at least one Codex plan critique and (per Tier 3) Joe's own review — exactly the situation
version 3 already established the precedent for. Between options 1/2: `AGENTS.md` says "Accepted
ADRs are immutable: propose a superseding record" — ADR-0017 is accepted and cannot be edited
in place regardless; a "follow-on section" would have to be its own new record anyway, making
option 2 not actually different from option 1 except in scope-of-claim. This PR's decision is
about a materially different design surface than ADR-0017's own scope (the write-seam *mechanism*
itself, already fully decided) — it is about the *first two consumers* of that seam and an
unrelated question (the `Inputs`-struct/`compute()` shape, the α/β singularity treatment, and the
open control-fields design choice §7's own "Status" note left for this ADR). A dedicated ADR-0018
states that scope honestly; folding it into ADR-0017 would misrepresent an already-accepted,
already-implemented decision as still open.

### Test infrastructure: `Simulation::for_testing()`'s atmosphere gap (task instruction 4)

`Simulation::for_testing()` has no `AtmosphereModel*` parameter (Codex F1, confirmed:
`PhysicsSpec.atmosphere` is hardcoded `nullptr`), and — after (c)'s addendum makes
`requires_environment()` `final` and hardcoded on both new classes — there is no longer any way
for a stub to work around that gap by declaring atmosphere optional, either. R8a's original,
now-dropped design tried to route around the gap with a test-only toggle; this PR does not carry
that workaround forward.

**Options.**
1. *Rely on R8b (`Simulation::from_config`) alone for end-to-end proof.* No kernel change. R8b
   already proves everything R8a was meant to (`wrench_b()` reachable from `Vehicle::integrate()`'s
   per-RK4-substage call) as a byproduct of proving the real, atmosphere-required,
   write-seam-driven path — R8a added an isolated-variable harness, not additional coverage.
2. *Add a real, additive `AtmosphereModel* atmosphere = nullptr` parameter to
   `Simulation::for_testing()`*, mirroring the existing `WindModel*`/`EarthModel*`/`GravityModel*`
   optional live-pointer parameters it already takes; `PhysicsSpec.atmosphere` and
   `EnvironmentAvailability.atmosphere` would be derived from it instead of hardcoded. Backward
   compatible (new parameter defaults to `nullptr`, every existing call site unchanged). Would
   generally benefit any future `for_testing()`-based test needing atmosphere, not just this PR's
   own R8.

**Recommendation: option 1 for this PR.** It fully satisfies PLAN.md's own PR 3 acceptance line
with zero kernel-code change, keeping this PR's own diff to the two new interface header pairs
plus tests plus the `kCoreApiVersion` bump and the §7 doc edit — matching the "small,
self-contained interface addition" framing this plan already commits to elsewhere. **Option 2 is a
legitimate, precedented follow-up** (it slots into `for_testing()`'s existing optional-live-pointer
pattern exactly), but it is a `core/src/kernel/simulation.cpp`/`simulation.hpp` change, outside
this PR's own stated boundaries, and no test in this PR's own acceptance table actually needs it
once R8a is dropped — proposing it as part of PR 3 would be scope creep for a benefit only a
*future* PR would cash in. Recorded as a `PLAN.md` follow-up (below) rather than folded into this
PR's own Mapping/sequence.

## Repository findings

- `core/include/astra/interfaces/wrench_source.hpp` — `WrenchSource` already has everything this
  PR's two new classes need to inherit: `bind_inputs()` (default empty, ADR-0017), `wrench_b()`
  (pure, to be given a concrete override), `requires_environment()` (default `{false, false}`, to
  be overridden). No change to this file.
- `core/include/astra/kernel/vehicle.hpp`/`vehicle.cpp` — `Vehicle::init()`'s existing
  `WrenchSource`-discovery pass (confirmed by reading the header's own doc comment) already calls
  `requires_environment()`/`bind_inputs()` for *any* `WrenchSource`; no change needed for
  `AeroModel`/`PropulsionModel` to be discovered correctly — they are ordinary `vehicle.models`
  entries (§13: "there is no separate typed `physical.aero`/`hardware.propulsion` slot").
- `core/include/astra/kernel/environment_sample.hpp` — `EnvironmentSample::wind_n`/`atmosphere`
  already carry everything needed; both fields' own doc comments already disclose the
  domain-misattribution/NaN-propagation gap this PR's (b) answer deliberately relies on rather than
  re-fixes.
- `core/include/astra/kernel/vehicle_state.hpp` — `v_b()`, `q_nb()`, `omega_b()` accessors exist
  exactly as needed; no change.
- `core/include/astra/util/frames.hpp:132` — `rotate_n_to_b(q_nb, v_n)` exists and is the exact
  function `nesc_aero.cpp`/`wind_model.hpp` already use for this identical wind-subtraction
  formula; reused, not reinvented.
- `tests/plugins/nesc_aero/nesc_aero.cpp` — the one existing precedent for a real `WrenchSource`
  reading wind+atmosphere+body-rate state; its header comments are the direct source for (b)'s
  "no 0/0 by construction" design choice and (a)'s wind-subtraction formula. Not modified by this
  PR (it implements `WrenchSource` directly, not `AeroModel`, and stays that way — it is a NESC
  test fixture, not aero/propulsion).
- `core/include/astra/core_api.hpp.in` — version is 5 (PR 2/ADR-0017); this PR bumps to 6, per (f).
  Verified this session: versions 2 and 3 both landed in one PR (M2 PR 2), and version 4 is
  `EarthModel`/`GravityModel`, not `WrenchSource`-family — corrects an overclaimed "one bump per
  `WrenchSource`-family PR" pattern this plan's first draft asserted (Codex plan critique F6).
- `core/src/kernel/simulation.cpp` (`Simulation::for_testing`, confirmed by reading it directly
  this session, ~line 457) — takes no `AtmosphereModel*` parameter at all; `PhysicsSpec.atmosphere`
  is hardcoded `nullptr` for every `for_testing`-built vehicle, and `EnvironmentAvailability` is
  built from `wind != nullptr` alone. Since `Vehicle::init()` (`vehicle.cpp:45`) rejects a
  `requires_environment().atmosphere` request when `availability.atmosphere` is false, **no
  `for_testing`-based vehicle can ever load an `AeroModel`/`PropulsionModel` under this plan's own
  unconditional, `final` atmosphere requirement (c).** This PR's own acceptance (R8, corrected)
  works around it by using `Simulation::from_config` alone for its one end-to-end test, per the
  options+recommendation above — it does not change `for_testing()` itself, which is out of this
  PR's scope (Codex plan critique F1, confirmed; option 2 above is the precedented but
  out-of-scope alternative).
- `tests/unit/kernel/simulation_input_seam_test.cpp:32` — existing precedent for a literal-YAML
  `Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR)` test fixture, reused for R8b.
- `tests/unit/CMakeLists.txt` — enumerates test sources explicitly; the new test files need their
  own entries, not just a `ctest -R` filter (Codex plan critique F11, confirmed by reading the
  file's own structure).
- `aircraft/notional_transport/physical/mass/`, `aircraft/_template/physical/mass/` — PR 1's
  existing scaffolding; no `physical/aero`/`hardware/propulsion` directories exist yet in either
  aircraft folder (confirmed: `find aircraft -type f` lists only `mass/` and `configs/`). PR 4
  creates them; this PR touches neither.
- `docs/architecture.md` §7 — the open design choice (a) settles is left open in this file's own
  prose, not introduced by this plan; R11 pins it.
- No `tests/golden/**` file, no file under `docs/decisions/` yet exists for this PR's scope; both
  are out of scope for implementation (ADR-0018 stays a draft here until Joe authorizes writing it
  for real, matching the task's own "ask before touch-goldens" instruction, applied here to the
  adjacent "ask before writing to `docs/decisions/`" boundary).

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance |
|---|---|---|---|
| R1, R11 | New `core/include/astra/interfaces/aero_inputs.hpp` (`AeroInputs` struct, mirroring `wrench.hpp`'s split of struct from interface), `aero_model.hpp` (`AeroModel : public WrenchSource`); same pair for `propulsion_inputs.hpp`/`propulsion_model.hpp`; **new entries in `tests/unit/CMakeLists.txt`** for the new test binaries (Codex F11: that file enumerates sources explicitly, `gtest_discover_tests` does not find an unregistered `.cpp`) | Derives from existing `WrenchSource`; no change to that file | `ctest -R AeroModel`/`PropulsionModel` (named to match discovered test names) |
| R2–R4, R6, R7, R9 | New `tests/unit/interfaces/aero_model_test.cpp`, `propulsion_model_test.cpp` (stub subclasses; R2–R4/R6 hand-build `VehicleState`/`EnvironmentSample`, R7 hand-builds `AeroInputs`/`PropulsionInputs` directly with no `VehicleState`/`EnvironmentSample` at all) | Reuses `tests/plugins/nan_wrench`'s stub-plugin pattern for structure, not its content | ctest, `test_no_alloc` extension |
| R5 | Extends `tests/unit/health/health_monitor_test.cpp` or a new focused test alongside it; stub's `compute()` has an instrumented call counter and deliberately never reads `mach` | `HealthMonitor::check_wrench` unchanged, reused; `wrench_b()`'s own new `isfinite`-guard is what's under test | ctest |
| R8, R8-guard | New `tests/plugins/aero_stub`/`propulsion_stub` (no `requires_environment()` override needed or possible — `final`, per (c)), registering one control via `bind_inputs()`; new `Simulation::from_config`-based integration test with a literal test-fixture YAML declaring `physical.atmosphere` and the stub's `vehicle.initial_controls` entry | Reuses `tests/unit/kernel/simulation_input_seam_test.cpp:32`'s own literal-YAML `from_config` helper pattern (confirmed existing precedent); no `for_testing`-based row (dropped, see (4) below) | ctest |
| R10 | `core/include/astra/core_api.hpp.in`: version 5→6, new numbered comment following the existing five | Existing `bad_version` plugin-rejection test extended, not replaced | `ctest -R Plugin` |
| R11 | `docs/architecture.md` §7 edits only | — | manual re-read against this plan's Contract table, `git diff --stat` for scope |
| R12 | — | — | `./scripts/check-full.sh` |

One Claude builder; no package split (this PR is a small, self-contained interface addition — the
same "no split until interfaces are stable" reasoning PR 2 itself used).

## Validation and risks

Fast gate (`scripts/check-fast.sh`) during iteration; full gate (`scripts/check-full.sh`) before
review, per `AGENTS.md`. No behavior regression risk for any existing plugin: `WrenchSource`,
`Wrench`, `EnvironmentSample`, `InputRegistry` are all unchanged; the only cross-cutting change is
`kCoreApiVersion` 5→6, whose own regression risk is fully covered by the existing `bad_version`
plugin-rejection test pattern (R10), already proven correct for versions 2 through 5. No new
dependency. No migration: additive-only (`aero_model.hpp`/`propulsion_model.hpp`/`*_inputs.hpp` are
new files; nothing existing is deleted or renamed). Numerical risk is confined to the α/β/q̄/Mach
formulas themselves, independently checkable by hand in R2/R3/R4 without any golden file. Main
open risk: the (a) mismatch fix (R11) is a `docs/architecture.md` edit under Tier-3 scope by virtue
of touching an `aircraft/**/physical` boundary's own governing document — recorded here, not
silently done, per the task's own instruction to resolve rather than paper over it.

## Draft ADR — new record (would become ADR-0018 on acceptance)

Not written to `docs/decisions/` — draft only, per the task's own instruction and `AGENTS.md`'s
oracle-adjacent-file boundary. Joe's authorization required before the real file is created.

### ADR-0018 (draft): `AeroModel`/`PropulsionModel` as `WrenchSource` specializations

Status: proposed
Date: 2026-09-19 (drafted; not yet dated as accepted)
Owner: Joe
Related/supersedes: elaborates ADR-0017 (the `bind_inputs()`/`InputRegistry` control channel this
decision's `AeroModel`/`PropulsionModel` are the first real consumers of) and ADR-0016 (the
per-RK4-substage purity contract `compute()` inherits from `wrench_b()`). Does not supersede
either. Drafted with `docs/plans/pr3-aero-propulsion.md`; one Codex plan critique complete
(twelve findings, all accepted and folded in — see that file's own "Plan critique" section), plus
one further revision from Joe's own follow-up review (see that file's "Revision 2" section) — no
second critique needed.

#### Context

`docs/architecture.md` §7 sketches `AeroModel`/`PropulsionModel`/`AeroInputs`/`PropulsionInputs`
with an explicit "Status: sketch, no concrete implementation exists yet... pinned by PLAN.md M3
PR 3's own ADR" disclaimer. The sketch's own prose leaves an open design question this decision
settles, not a self-contradiction to repair (corrected from this plan's first draft, Codex plan
critique F7): the C++ code block says `AeroInputs` carries "the control surface positions" and
`PropulsionInputs` carries "throttle commands," while the same section's later "Control inputs in
v1" paragraph and ADR-0017's own Decision describe control values arriving through
`bind_inputs()`/`InputRegistry`'s `const double*` seam. Both can coexist — `InputRegistry::add()`'s
own stable, initialization-resolved pointer could in principle be copied into an `Inputs` field
without a per-frame allocation or lookup — so this is a genuine choice between putting controls in
`Inputs` (baking a fixed or generically-viewed control layout into the struct) or leaving them out
(a concrete plugin reads its own bound member directly, as `nesc_aero` already does for its own
config-resolved members). §7's own "Status" note defers exactly this choice to this PR's ADR.

#### Decision

`AeroModel`/`PropulsionModel` derive from `WrenchSource` (not `Model` directly, correcting the
sketch). Each provides a concrete, `final` `wrench_b(vs, env) const noexcept override` that: (1)
computes `v_rel_b = vs.v_b() - frames::rotate_n_to_b(vs.q_nb(), env.wind_n)`; (2) assembles
`AeroInputs`/`PropulsionInputs` (fields per this plan's Contract section) using `atan2`-based α/β
forms chosen to avoid a division-by-zero at `v_rel_b == 0` (never `asin`) — finite there by
construction, though not always exactly `0` in the IEEE-754 signed-zero branch (`atan2(+0,-0) =
π`), a disclosed, accepted cosmetic edge case, not a NaN; (3) checks every assembled `Inputs`
field with `std::isfinite`, returning an explicit all-NaN `Wrench` (never `Wrench{}`'s own zero
default) without calling `compute()` at all if any field is non-finite — the existing
`HealthMonitor::check_wrench` (ADR-0016, §15, unmodified) then unconditionally halts, naming this
plugin's own path, regardless of which fields a concrete plugin's own math actually reads; (4)
otherwise calls a new pure virtual `[[nodiscard]] virtual Wrench compute(const AeroInputs&) const
noexcept = 0;` (respectively `PropulsionInputs`) a concrete plugin implements, matching
`wrench_b()`'s own per-RK4-substage purity contract as ADR-0017 already amends it (identical output
across one frame's four sub-stages; a bound control member's value may still differ frame-to-
frame). Neither class overrides `bind_inputs()` — a concrete plugin registers whichever control
names it actually reads, directly, the same way `nesc_aero` registers no names and `input_consumer`
registers its own. `requires_environment()` **is** overridden, and marked `final`: both classes
unconditionally return `{wind: false, atmosphere: true}`, never overridable by a concrete plugin —
wind is optional because the base class itself, not the plugin, is what reads `env.wind_n` (step 1
above), and `EnvironmentSample::wind_n`'s own contract makes "absent" and "zero" identical by
design; atmosphere is mandatory, and sealed as such, because step 3's `isfinite` guard and both
`Inputs` structs' own fields always depend on a real `AtmoState`, with no meaningful default. A
concrete plugin's own `compute()` carries a separate, undelegated obligation, stated in
`AeroModel`/`PropulsionModel`'s own class-level doc comments: it must return a finite `Wrench` at
`true_airspeed_mps == 0`, since a naive port of a nondimensional stability derivative (S&L §2.3 Eq.
(2.3-9a): `p̂ = Pb/(2V_T)`) carries a real `1/V_T` term the *dimensional* force equations built from
the same coefficients (Eq. (2.3-8b)) do not have — this decision's own framework code computes no
nondimensional rate and cannot enforce this for a plugin that does; PLAN.md's PR 4 line carries the
acceptance obligation instead. `compute()`'s returned
`Wrench` must already be referenced to the CG in body axes (`Wrench`'s own existing contract,
unchanged); a `PropulsionModel` plugin whose thrust line is off-CG applies its own `r × F` using
its own config's station offset before returning, inside `compute()` — no core or base-class code
performs this transfer. `kCoreApiVersion` bumps 5→6: both classes' vtables, via the new
`compute()`, are plugin-facing and settling for the first time in this PR — the same reasoning
version 3's brand-new `WindModel` bump already used, not the broader "one bump per `WrenchSource`-
family PR" pattern this plan's first draft incorrectly generalized from it (versions 2/3 shipped in
one PR; version 4 is `EarthModel`/`GravityModel`, a different family). Envelope monitors (§15) are
explicitly deferred to the PR that gives them real declared numbers (PLAN.md PR 4 or later); no
hook is added here.

#### Rejected alternatives

See Design questions (a)–(f) above in the accompanying plan — each option list and rejection
reason is reproduced there in full and not duplicated here to keep this draft to one screen
before its own review.

#### Consequences and validation

**Benefits.** Two new, concrete, testable abstract interfaces with no plugin-facing ambiguity left
in `docs/architecture.md`'s own sketch; zero change to any already-shipped interface; settles an
open design choice §7's own "Status" note explicitly deferred to this ADR, before a concrete
plugin (PR 4) could be built against an unpinned reading of it.

**Costs.** `kCoreApiVersion` bump (every plugin recompiles, per existing policy); two new header
pairs; `docs/architecture.md` §7 edits (R11).

**Known gaps, disclosed not fixed.** Envelope monitors from §15 ("α, β, Mach, dynamic pressure,
load factor... the notional transport declares its own") have no home yet — deferred to PR 4 or
later, tracked as a `PLAN.md` follow-up, the same posture ADR-0012/0016/0017 already took for
their own accepted, differently-shaped gaps. `EnvironmentSample`'s "not surfaced at all for a
NaN-tolerant consumer" gap is closed for `AeroModel`/`PropulsionModel` specifically (the
`isfinite`-guard above) but remains open for any other `WrenchSource` that reads `env.atmosphere`
directly. The V→0 nondimensional-rate hazard (S&L §2.3 Eq. (2.3-9a)) is stated as a `compute()`
contract obligation but not enforced by any code this decision adds — PLAN.md's PR 4 line carries
the acceptance obligation. `Simulation::for_testing()` still cannot construct an
atmosphere-requiring vehicle at all; this decision's own acceptance uses `Simulation::from_config`
instead rather than extending it.

**Migration/rollback.** Fully reversible, single-PR change (assuming no plugin yet implements
`AeroModel`/`PropulsionModel` outside this tree, true today); no data or schema migration.

**Acceptance.** This plan's own Acceptance table, R1–R12, at implementation time.

#### Approval

Pending. One Codex plan critique complete (below, twelve findings, all accepted and folded into
this plan). Still requires: Joe's plan approval (Tier 3), then implementation, full gate,
independent review, and Joe's own reading of the critical diff/tests before this record is written
for real.

## Questions and decisions

| Question | Consequence | Owner | Answer/reference |
|---|---|---|---|
| (a) fields/units/frames/wind | Interface shape for all of PR 4+ | Joe | Recommended: option 1 (controls excluded from `Inputs`), pending approval |
| (b) V→0/zero-density/no-atmosphere | Whether a real fault can hide as a plausible zero | Joe | Recommended: option 1 (`atan2` forms + existing `check_wrench` backstop), pending approval |
| (c) `bind_inputs` ownership | Framework vs. plugin boundary | Joe | Recommended: option 1 (plugin-owned, no base override), pending approval |
| (d) output axes/r×F | Already pinned by `Wrench`'s existing contract | — | Not an open question; documented, not decided |
| (e) envelope monitors | Whether PR 3 or PR 4 carries this scope | Joe | Recommended: option 1 (deferred, no hook), pending approval |
| (f) `kCoreApiVersion`/ADR | Whether a new record is warranted | Joe | Recommended: option 1 (bump to 6; new ADR-0018), pending approval |
| ADR-0002 field naming | `alpha_rad`/`dynamic_pressure_pa`/etc. carry unit suffixes ADR-0002's literal text disallows; `AtmoState` already sets this precedent | Joe | Recommended: follow `AtmoState`'s existing precedent, pending explicit acceptance (Codex plan critique F12) |
| `for_testing()` atmosphere param (4) | Whether to extend the kernel test factory now or defer | Joe | Recommended: defer (rely on R8b alone); Option B (extend `for_testing()`) proposed as a separate, later follow-up, not this PR's scope |

## Follow-up (not part of this PR's diff)

- Envelope monitors (§15's α/β/Mach/q̄/load-factor limits) — deferred to PLAN.md PR 4 or a later
  PR, per (e). Add a line to `PLAN.md`'s own unchecked-items list once this plan is approved.
- The already-disclosed ADR-0017 gap (a non-`WrenchSource` `ActuatorModel` has no seam-provided
  write route) is unaffected by this PR either way (c) — remains M4 scope.
- The already-disclosed `EnvironmentSample`-wide "not surfaced at all for a NaN-tolerant consumer"
  gap (`environment_sample.hpp`'s own comment) is now **closed for this PR's own two consumers**
  by (b)'s unconditional `isfinite`-guard, but remains open in general for any future, differently-
  shaped `WrenchSource` that reads `env.atmosphere` directly without going through
  `AeroModel`/`PropulsionModel` — tracked as the existing `PLAN.md` follow-up item, not a new one.
- **New (task instruction 3): PLAN.md's PR 4 line gets a named acceptance obligation** — the
  concrete linear-coefficient aero (and any propulsion model using a ram/intake term) must prove
  its own `compute()` returns a finite `Wrench` at `true_airspeed_mps == 0`, per (b)'s V→0
  plugin-contract addendum. Not testable by this PR (no concrete plugin exists yet); recorded here
  so PR 4's own plan does not silently assume the hazard away.
- `Simulation::for_testing()`'s atmosphere-parameter gap (Codex plan critique F1) is worked around
  by this PR's own R8 (`Simulation::from_config` alone), not fixed in `for_testing()` itself. Option
  2 of the "Test infrastructure" section above (an additive `AtmosphereModel*` parameter, mirroring
  the existing `WindModel*`/`EarthModel*`/`GravityModel*` ones) is a legitimate, precedented,
  **separate** follow-up task for whenever a future PR actually needs a `for_testing()`-based
  atmosphere-dependent test — not bundled into this PR's own scope.
- ADR-0002's literal "no unit suffixes in identifiers" text vs. `AtmoState`'s (and now this PR's)
  actual field naming is a pre-existing, repo-wide tension this PR did not create and does not
  resolve — worth its own small documentation clarification at some point (whether the rule means
  signal-name strings only), not blocking this PR.

## Done when

Requirements R1–R12 (plus R8-guard and R-V0, added by Revision 2) proved by their own acceptance
commands; one Codex plan critique recorded below with findings resolved, plus Revision 2's four
further changes (confirmed not to reopen (a)/(c)/(d)/(f), no second critique needed); Joe's plan
approval (Tier 3); then, at implementation time: full gate green, independent Codex review
(general + adversarial), human review packet (`/human-review`), and Joe's own reading of the new
interface headers and their tests before merge. Implementation completion is distinct from Joe's
approval/merge.

## Plan critique

Dispatched via `docs/agents/review-dispatch.md`'s plan-critique route: `codex:codex-rescue`
subagent, `task --fresh --wait`, explicit read-only instruction (no `--write`, no file edits, no
implementation). Scope: this plan file plus the grounding files listed in the dispatch prompt
(architecture.md §5/§7/§9/§13/§15; ADR-0016/0017; `wrench_source.hpp`, `wrench.hpp`,
`environment_sample.hpp`, `vehicle_state.hpp`, `vehicle.hpp`, `input_registry.hpp`,
`core_api.hpp.in`, `frames.hpp`; `nesc_aero.cpp`; `PLAN.md`'s PR 3 line). Confirmed clean run: no
files modified, no builds/tests run. All twelve findings below were independently verified against
the repository this session (not accepted on Codex's word alone), per `AGENTS.md`'s "treat
findings as claims to verify."

| ID | Severity | Finding | Verified? | Disposition |
|---|---|---|---|---|
| F1 | Major | `Simulation::for_testing()` has no atmosphere parameter (`PhysicsSpec.atmosphere` hardcoded `nullptr`); R8's original single-row `for_testing`-based harness cannot load an atmosphere-requiring `AeroModel`/`PropulsionModel` at all | **Confirmed** — read `simulation.cpp` `for_testing()` directly (`PhysicsSpec` construction, `EnvironmentAvailability{wind != nullptr}`) and `vehicle.cpp:45`'s rejection | **Accepted, later superseded by Joe's own follow-up review.** First fix: R8 split into R8a (`for_testing`, test-toggleable requirement)/R8b (`from_config`, real requirement). Second revision (task instruction 4): R8a dropped entirely — `requires_environment()` is now `final` on both new classes (c), making any test-only toggle architecturally impossible, not just unneeded — leaving R8 as a single `from_config`-only row; extending `for_testing()` itself considered and deferred as a separate, precedented follow-up (see "Test infrastructure" section) |
| F2 | Minor | The "citation honesty" note claimed `nesc_aero.cpp` corroborates the α/β/Mach formulas; it only corroborates the wind-subtraction/drag-vector form, not any angle or Mach computation | **Confirmed** — reread `nesc_aero.cpp:72–97`; it computes no angle and no Mach | **Accepted.** Citation-honesty note's wording narrowed above to claim only what `nesc_aero.cpp`/`wind_model.hpp` actually corroborate (the wind-subtraction formula), not the angle/Mach forms |
| F3 | Major | Non-finite atmosphere fields are not guaranteed to reach `check_wrench` — it only inspects the returned `Wrench`; a concrete plugin ignoring the corrupted field produces a finite wrench, unobserved. Also: the NaN cause is not limited to the domain-misattribution window; a genuinely broken in-range `AtmosphereModel` is a second, independently-disclosed cause | **Confirmed** — reread `health_monitor.cpp:77` (`check_wrench` inspects only `force`/`moment`) and `environment_sample.hpp:39–47` (both causes named, "not surfaced at all" language) | **Accepted, later superseded by Joe's own follow-up review.** First fix stated the guarantee as conditional on plugin sensitivity. Second revision (task instruction 2) closes the gap unconditionally instead: `wrench_b()` itself `isfinite`-checks every assembled `Inputs` field and returns an explicit all-NaN `Wrench` without calling `compute()` at all when any field is non-finite — the existing `check_wrench` then always fires, regardless of which fields a concrete plugin reads. R5 rewritten to prove the harder case (a stub that ignores the corrupted field) rather than one that reads it |
| F4 | Major | `atan2`-based α/β are finite at `V_rel = 0` but not always exactly `0` — IEEE-754 signed-zero gives `atan2(+0,-0) = π` | **Confirmed locally** — `python3 -c "import math; print(math.atan2(0.0,-0.0))"` → `3.141592653589793` | **Accepted.** Contract, (b), R4, draft ADR Decision corrected: claim narrowed from "evaluates to 0" to "finite, never NaN/Inf; not always exactly 0 in the signed-zero branch," a disclosed cosmetic gap, not a NaN hazard. R4 split into two sub-cases (exact `+0.0`, and a forced `-0.0`) |
| F5 | Major | "Invariants preserved" claimed `wrench_b()`/`compute()` "depend only on their own arguments," which ADR-0017's own amendment (control values may change frame-to-frame via a bound member, outside `(vs, env)`) contradicts | **Confirmed** — reread `wrench_source.hpp:40–49`'s own ADR-0017 amendment comment | **Accepted.** Wording corrected to state the purity contract exactly as ADR-0017 already amends it, not a stricter argument-only claim. Wording-only; no design change |
| F6 | Minor | Version-bump precedent overclaimed as "one bump per accepted-ADR PR touching a `WrenchSource`-family vtable: v2, v3, v4, v5" | **Confirmed** — reread `core_api.hpp.in`'s own comments: v2/v3 shipped in one PR (M2 PR 2); v4 is `EarthModel`/`GravityModel` | **Accepted.** (e), draft ADR Decision, and Repository findings corrected to the narrower, still-sufficient precedent (any plugin-facing vtable settling, not confined to one family or one-per-PR); the underlying recommendation (bump 5→6) is unchanged — only the historical claim was wrong |
| F7 | Major | The claimed §7 "internal inconsistency" (controls in `Inputs` vs. the `bind_inputs()` seam) is really an open design choice the two layers can coexist under; the "impossible without a heap map" framing was too strong given `InputRegistry`'s own stable, init-resolved storage | **Confirmed** — reread architecture.md §7's own two paragraphs and ADR-0017's Decision section side by side; they describe different layers, not a contradiction | **Accepted.** (a), draft ADR Context, and R11 all reframed from "fix a mismatch" to "settle an open choice," with a fourth option (a non-owning view) added and correctly credited to Codex's counter-argument; recommendation (option 1) unchanged on its own engineering merits (avoids per-frame name lookup) |
| F8 | Major | R7 ("hand-built-`Inputs` testing... no simulator") pointed at R2–R6, which all still construct `VehicleState`/`EnvironmentSample` and call `wrench_b()` — not the same as calling `compute()` directly on a by-hand `Inputs` literal | **Confirmed** — reread the plan's own R2–R6 wording; none omits `VehicleState`/`EnvironmentSample`/`wrench_b()` | **Accepted.** R7 rewritten as its own, separate test with no `VehicleState`/`EnvironmentSample`/`Vehicle`/`Simulation` symbol referenced |
| F9 | Major | R8's stub used a default-empty `bind_inputs()`, so it could not prove PLAN.md's own PR 3 requirement ("end-to-end... with PR 2's write seam driving it") | **Confirmed** — reread PLAN.md line ~593–594 and the plan's own original R8 wording | **Accepted.** Folded into what is now R8's own single row: the stub registers a real control via `bind_inputs()`, and the test writes/latches a value and checks it reaches `compute()`'s output |
| F10 | Major | Several stated contract fields have no acceptance assertion: `omega_b` passthrough (R2), full `PropulsionInputs.atmosphere` passthrough beyond density (R6), and no row for atmosphere-missing rejection | **Confirmed** — reread the plan's own Contract table against the original R2/R6 wording | **Accepted.** R2 now asserts `omega_b`; R6 now asserts full `AtmoState` passthrough. Atmosphere-missing rejection is already implied by the existing, unmodified `Vehicle::init()` behavior (`vehicle.cpp:45`, not new code this PR adds) — noted in Contract's failure-behavior text rather than given its own new acceptance row, since it is existing, already-tested framework behavior this PR's classes merely opt into by declaring the requirement, not a new code path |
| F11 | Minor | `tests/unit/CMakeLists.txt` enumerates sources explicitly; new test files need registration, and `git diff --stat` cannot verify R11's prose correctness | **Confirmed** — read `tests/unit/CMakeLists.txt`'s own structure (explicit source list, no globbing) | **Accepted.** Mapping/sequence and R1/R11 both corrected to name the required `CMakeLists.txt` registration and to require an actual re-read of the edited §7 prose, not just a diff stat |
| F12 | Minor | Proposed field names (`alpha_rad`, `dynamic_pressure_pa`, etc.) carry unit suffixes against ADR-0002's literal "no unit suffixes in identifiers" text | **Confirmed** — reread `docs/decisions/0002-si-units-and-signal-metadata.md`'s own Consequences section | **Accepted, with existing-precedent context.** Noted in (a), Questions and decisions, and Follow-up: `AtmoState` already sets this same precedent; this plan follows it for consistency rather than inventing a bare-name convention unique to these two new structs, and flags the ADR-0002-vs-practice tension as pre-existing and unresolved by this PR, for Joe's explicit acceptance rather than silent adoption |

No finding was rejected outright; all twelve changed the plan's wording, acceptance table, or
recommendation-framing (F1/F8/F9 materially — R8's approach and R7 both had to change; the rest are
corrected claims/wording within an unchanged recommendation). No finding required reopening (c) or
(d), which Codex found no defect in.

## Revision 2 (Joe's own follow-up review, post-critique)

Four further changes, requested directly by Joe after reading the critiqued plan above — distinct
from the Codex critique table, which recorded only what Codex found:

1. **Citations re-verified against the main checkout's `docs/refs/textbooks/`** (this session's own
   worktree copy is empty because `docs/refs/` is gitignored — a `docs/lessons.md` miss, now
   recorded). Every S&L/Zipfel citation in (a)'s "Citations corrected this revision" block and the
   top-of-section citation note was re-checked against the actual pages; two section-title guesses
   (S&L §2.4, §3.5) were wrong and are corrected.
2. **(b)'s no-silent-NaN guarantee is now unconditional**, not scoped to "whenever the concrete
   plugin's own math is sensitive to the corrupted field" (superseding the Codex-F3 fix, itself
   already an improvement on the first draft's overclaim). `wrench_b()` itself `isfinite`-guards
   the assembled `Inputs` and returns an all-NaN `Wrench` without calling `compute()` when any
   field is non-finite — no new `HealthMonitor` API. R5 rewritten accordingly.
3. **A named V→0 contract obligation added for concrete plugin code** (this PR's own framework
   code computes no nondimensional rate and has nothing to guard): `AeroModel`/`PropulsionModel`'s
   own doc comments now state that a conforming `compute()` must return a finite `Wrench` at
   `true_airspeed_mps == 0`, citing S&L §2.3 Eq. (2.3-9a)'s `1/(2V_T)` nondimensional-rate hazard as
   the mechanism a naive port would carry into a real division by zero — recorded as a named
   acceptance obligation on `PLAN.md`'s own PR 4 line, not solved here.
4. **R8a's test-only `requires_environment()` toggle dropped.** Folded together with (c)'s own new
   addendum (`requires_environment()` marked `final`, hardcoding `{wind: false, atmosphere: true}`
   on both new classes, since the base class — not the concrete plugin — is what reads both
   `env.wind_n` and `env.atmosphere`): R8a's toggle is now architecturally impossible, not merely
   unneeded. R8 is a single row via `Simulation::from_config`; extending `Simulation::for_testing()`
   itself is proposed as options+recommendation (deferred, precedented follow-up), not done here.

**None of the four reopen (a), (c), (d), or (f) as decided:** (1) is citation accuracy only, no
design change. (2) is confined to (b)'s own scope — no interface, field, or ownership change; (c)'s
own recommendation (plugin owns `bind_inputs()`) is unchanged. (3) adds a doc-comment obligation
inside (b)'s own topic and a `PLAN.md` line for PR 4; it does not touch `AeroInputs`/
`PropulsionInputs`'s field list (a), `bind_inputs()` ownership (c), output axes/r×F (d), or
`kCoreApiVersion`/the ADR relationship (f) — (c) gains a new, compatible addendum
(`requires_environment()` ownership) rather than a changed recommendation. (4) is test
infrastructure only. Per the task's own instruction, **no second Codex plan critique is
dispatched** for this revision.
