# M1 PR 3 — EarthModel (flat), GravityModel (constant), MassPropertiesModel (constant), Eom

Recovered from `~/.claude/plans/m1-pr-3-as-happy-melody.md` on 2026-09-30. Approval: the file records none; the work merged as #19. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

M1 PR 1 landed `core/util/frames.hpp` (quaternion/DCM/Euler, ADR-0001) and `core/kernel/integrator.hpp`
(RK4 + Euler over the 13-vector, ADR-0004). M1 PR 2 landed `Config`, `SignalRegistry`, `VirtualClock`,
`Scheduler`, `Vehicle`, `Simulation` — a frame loop that steps models and advances the clock but has no
physics in it. Both files say so in their own comments: `Simulation::step()` notes "environment / EOM /
integrator / post-step / logger join this as PR 3 and PR 4 create them", and `vehicle.hpp` notes "state
starts zeroed -- PR 3's EarthModel and EOM own giving it a meaningful initial value" (it is `Zero()`, so
`q_nb` is `(0,0,0,0)` — not even a unit quaternion).

This PR puts the rigid body in: the three physical models the EOM needs, the EOM itself, and the
integrator + post-step stages, so a config describing a rigid body actually flies it. It creates
`plugins/` for the first time (today's only plugins are test fixtures under `tests/plugins/`), and
`tests/physics/` for the first time (the `physics` ctest label is a declared convention not yet used).

It gets a `/codex:rescue` plan round because the EOM and the quaternion kinematics are ADR-unpinned:
§7 sketches `EarthModel` as a bare comment (`/* frames, rotation rate, transport rate, geodetic
conversions */`) and references a `VehicleState` type that is named in four places and defined nowhere.

## What is already pinned (do not re-decide)

- **§7 pins exact shapes**: `struct Wrench { Eigen::Vector3d force_b; Eigen::Vector3d moment_b; }`
  (about CG, body axes), `GravityModel::g_n(const VehicleState&) const`,
  `MassPropertiesModel::get() const`. All derive from `Model`.
- **ADR-0003 + §6**: the Earth model supplies the EOM exactly three things — Earth rotation rate in n,
  transport rate of n (both zero for flat), and position kinematics ("Position kinematics differ per
  Earth model and live behind the `EarthModel` interface, not in the EOM"). Gravity is §7's separate
  `GravityModel`; PLAN.md follows §7, and that resolves the §6/§7 wording overlap.
- **ADR-0003**: mass and inertia are not state; constant for a run, from `MassPropertiesModel`.
- **ADR-0011**: environment models (atmosphere, wind, gravity, Earth) are **shared across vehicles**;
  mass properties are **per-vehicle**. That decides ownership and signal namespacing.
- **ADR-0004 / §5 order**: environment → transport → vehicle models accumulate wrenches → EOM
  derivative → integrator → post-step (renormalize) → logger. EOM and integrator are therefore
  **kernel stages in `Simulation::step`**, not `Scheduler` entries.
- **`post_step_renormalize` stays separate** from `Integrator::step` on purpose, so PR 4 can observe
  |q| drift before it is erased.
- **ADR-0002 naming**: `altitude` and `mass`, never `altitude_m`/`mass_kg`.
- **§14**: per-frame path allocates nothing, throws nothing, builds no `std::string`.

## Decisions Joe made for this plan

- **Config**: typed `physical.earth` / `physical.gravity` / `physical.mass` slots only, each either a
  path string or a `{plugin: ..., ...}` map per §13. Unknown slot names are errors. `vehicle.models`
  stays exactly as it is so PR 2's dummy fixture and test do not change. Other §13 slots
  (`atmosphere`, `wind`, `aero`, `hardware.*`) arrive with their own PRs.
- **Initial conditions**: an `initial.state` block only — `p_n`, `v_b`, `omega_b` as 3-vectors and
  attitude as `euler_nb: [psi, theta, phi]` (SI per ADR-0002), converted once at init with
  `frames::quaternion_from_euler`. Any missing field is zero, so absent `euler_nb` means identity
  attitude. `initial.trim` is M3 and must be rejected as an unknown key until then. Each physics
  oracle is its own YAML, so initial conditions are data, not test code.

## Constraints found in the tree

- `.claude/hooks/guard-oracle.sh` blocks `tests/golden/**`, `docs/decisions/**`, **and any path
  containing the substring `tolerance`**. No new file may be named `tolerances.hpp`. This PR's oracles
  are analytic closed forms evaluated in the test, not tabulated data, so no guarded file is needed
  and no `touch-goldens` precondition applies. Comparison bounds are named `kXxxTolerance` constants
  inside the test sources.
- `astra_finalize_plugins()` (`cmake/AstraPlugin.cmake:152`) hardcodes
  `add_dependencies(astra_unit_tests ${targets})`. A new `astra_physics_tests` target that dlopens
  plugins gets **no** build-order dependency. Fix by extending that branch to cover both test targets
  rather than hardcoding plugin target names in `tests/physics/CMakeLists.txt`.
- `astra_discover_plugins` adds *every* directory holding a `CMakeLists.txt`, and a parent that also
  adds its children fails configure by design. So put a `CMakeLists.txt` **only at plugin leaves** —
  none at `plugins/` or `plugins/physical/`. No root `CMakeLists.txt` edit is needed;
  `astra_discover_plugins(plugins)` is already called.
- `Config` has no sequence/vector getter, but `Config::at(index)` does mark the path consumed, so
  reading a 3-vector element-by-element satisfies `unknown_keys()`. Verified in `config.cpp:226-233`.
- `SignalRegistry::add` takes only `const double*`, so a vector signal is registered component-wise
  into a member `Eigen::Vector3d`'s elements.
- `Integrator::step` requires `StateDerivative`: `{ f(t, y) } noexcept -> std::same_as<StateVector>`.
  The EOM's call operator must be `noexcept` and return `StateVector` **by value**, and every virtual
  it calls must therefore be `noexcept` too.
- Build flags include `-Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion -Werror`;
  use `static_cast` and `Eigen::Index`. `tests/.clang-tidy` already relieves magic numbers under
  `tests/`; core and plugin code needs `NOLINTBEGIN/END(readability-magic-numbers)` with a
  justification for physics literals, matching `frames.hpp`/`integrator.hpp` precedent.
- Member names must satisfy `readability-identifier-naming` (lower_case): `c_nb`, `inertia_b`, `h_n`
  — never `C_nb`/`I_b`. The one existing exception is a guarded golden header and cost Joe a
  `touch-goldens` round (`docs/lessons.md`); do not repeat it.

## The equations

Written in the general form ADR-0003 promises ("parameterized by the Earth interface; selecting an
Earth model at runtime costs nothing in the EOM"), consuming all three EarthModel outputs. Every
Earth term is exactly zero for flat Earth, so PR 3 exercises the S&L flat-Earth reduction and M2's
round/rotating models light up the same code with no EOM change.

With `c_nb = dcm_from_quaternion(q_nb)`, `m` and `inertia_b` from `MassProps`, and `applied` the
summed vehicle wrench (zero in this PR; M3's aero and propulsion sum into it):

```
omega_ie_b = c_nb * earth.omega_ie_n(vs)         // zero, flat
omega_en_b = c_nb * earth.omega_en_n(vs)         // zero, flat
omega_nb_b = omega_b - (omega_ie_b + omega_en_b) // == omega_b, flat

p_n_dot    = earth.p_n_dot(vs)                   // flat: rotate_b_to_n(q_nb, v_b)
v_b_dot    = applied.force_b / m
             + rotate_n_to_b(q_nb, gravity.g_n(vs))
             - (omega_b + omega_ie_b).cross(v_b) // flat: -omega_b x v_b, S&L
q_nb_dot   = frames::quaternion_derivative(q_nb, omega_nb_b)
omega_b_dot = inertia_b.inverse() * (applied.moment_b - omega_b.cross(inertia_b * omega_b))
```

`omega_b` is the rate relative to **inertial** (ADR-0003), which is what the gyroscopic term
`omega_b x (I omega_b)` requires, and why the attitude kinematics must subtract the Earth and
transport rates to get the rate relative to n that `q_nb` measures. Centripetal acceleration is
folded into the gravity model's `g_n` (plumb-bob gravity), which is the standard convention and what
M2's WGS-84 model will supply.

`inertia_b.inverse()` is **not** computed per derivative evaluation. `Eom` latches `MassProps` and the
inverse inertia once per major frame in a `begin_frame()` call, so the four RK4 stages share one
inversion. Mass is constant for a run today (ADR-0003); the per-frame latch is what keeps the deferred
fuel-burn case (§19) from needing an EOM change.

## Files

**New core types** (`core/include/astra/interfaces/`):
- `wrench.hpp` — `Wrench` exactly as §7 pins it.
- `mass_properties_model.hpp` — `MassProps { double mass; Eigen::Matrix3d inertia_b; }` and
  `MassPropertiesModel : Model` with `virtual MassProps get() const noexcept = 0`. No CG offset field:
  the state is the CG's position and §7's wrench is about the CG, so a CG offset is identically zero
  in v1; adding an unused field now is speculative.
- `earth_model.hpp` — `EarthModel : Model`, the unpinned interface, all `noexcept`:
  `p_n_dot(const VehicleState&) const`, `omega_ie_n(...) const`, `omega_en_n(...) const`,
  `altitude(...) const` (the last is what M2's atmosphere and inverse-square gravity need, and it is
  what makes "the Earth model owns the meaning of `p_n`" real rather than a comment).
- `gravity_model.hpp` — `GravityModel : Model` with `g_n(const VehicleState&) const noexcept`.

`noexcept` on these pull methods is a deliberate strengthening of §7's sketch: they are called from
inside `Eom::operator()`, which the `StateDerivative` concept forces to be `noexcept`, so a throwing
override would terminate anyway. `Model::step` stays non-`noexcept` — that inconsistency is PR 4's
named acceptance item, not this PR's to fix.

**New kernel** (`core/include/astra/kernel/`):
- `vehicle_state.hpp` — `VehicleState`, a non-owning read-only view over a `StateVector` with named
  accessors (`p_n()`, `v_b()`, `q_nb()`, `omega_b()`, `vector()`) returning by value so no Eigen
  expression dangles. Constructed on the stack at every derivative evaluation, including at RK4 stage
  points, which is exactly why the pull interfaces take it rather than reading the vehicle's stored
  state. §4's kernel tree already lists `VehicleState` as a kernel type.
- `eom.hpp` / `eom.cpp` — `Eom`, holding references to one earth, one gravity, one mass model plus the
  applied `Wrench`. `begin_frame()` latches mass properties; `operator()(double, const StateVector&)
  const noexcept -> StateVector` is the single derivative ADR-0004 requires ("there is one EOM").
  `set_applied_wrench(const Wrench&) noexcept` is the seam M3 fills.

**New plugins** (leaf `CMakeLists.txt` is the one-line `astra_add_plugin(SOURCES x.cpp)`; descriptor
category `"physical"`; pattern copied from `tests/plugins/dummy/dummy.cpp`):
- `plugins/physical/earth/flat/flat.cpp` — `p_n_dot = rotate_b_to_n(q_nb, v_b)`, both rates zero,
  `altitude = -p_n(2)` (NED down). Reads no config keys.
- `plugins/physical/gravity/constant/constant.cpp` — `g_n = {0, 0, g}` with `g` from config,
  defaulting to 9.80665. Publishes `gravity.g` so the value used is in the log.
- `plugins/physical/mass/constant/constant.cpp` — reads `mass` and `ixx, iyy, izz` plus optional
  `ixy, ixz, iyz` (default 0), because `Config` has no matrix getter and six named scalars are
  readable YAML. Validates at init per §15's pre-run list: mass > 0, inertia symmetric
  positive-definite (Eigen `LDLT`/eigenvalue check), all finite — each failure a `ConfigError` naming
  the offending key's path. PLAN.md places this under `plugins/physical/mass/constant`; §4's tree
  shows mass only under `aircraft/*/physical/mass/`, but §4's taxonomy rule ("mass properties are
  physics and live under `physical/`") and §7's "generic vehicle physics if any" both permit the
  shared root, and M3's notional transport will add its own alongside.

**Wiring**:
- `Simulation` gains an `Integrator`, the shared-environment `PluginLoader` results, a `Scheduler` for
  environment models (reusing the existing class so `divisor` works on env slots), and reads the new
  `physical.*` and `initial.state` config. Environment models init **without** a vehicle
  `PrefixScope`, so their signals are `gravity.g`, not `v0.gravity.g` — ADR-0011 makes them shared.
- `Vehicle` gains the mass model, an `Eom`, and an `integrate(const Integrator&, const StepContext&)`
  that calls `begin_frame()`, `integrator.step(eom_, ctx.t, state_)`, then `post_step_renormalize`.
  `post_step_renormalize`'s `[[nodiscard]]` bool is discarded with an explicit
  `static_cast<void>` and a comment naming PR 4 as the owner of routing it to a halt.
- `Simulation::step()` becomes §5's order: environment scheduler → vehicle models → per vehicle
  EOM + integrator + post-step. Its "PR 3 and PR 4 create them" comment is updated to say what is
  now real and what is still missing (transport, logger).

**Tests** (`tests/physics/`, new `astra_physics_tests` target, `LABELS physics`, `TEST_PREFIX
"physics."`; `add_subdirectory(physics)` in `tests/CMakeLists.txt`). Labeled `physics`, not `unit`, so
the Stop hook's `ctest -L unit` stays fast while `just check`'s unlabeled `ctest` still gates on them
under all four presets.
- `torque_free_test.cpp` — asymmetric `inertia_b = diag(1, 2, 3)`, `g = 0`, no wrench, 1e6 frames at
  1 kHz. **`omega_b(0) = (1, 1, 1) / sqrt(3)`** — all three components nonzero (Codex finding 3): a
  principal-axis initial condition also satisfies `|omega| ~ 1` but makes the motion constant, so it
  would pass with the Euler coupling terms completely wrong. Asserts relative drift < 1e-9 in kinetic
  energy `0.5 * omega_b' * inertia_b * omega_b` and in `|h_n|` where
  `h_n = rotate_b_to_n(q_nb, inertia_b * omega_b)`. Also asserts the **direction** of `h_n` is
  constant — free, and the real test of the quaternion kinematics, since `|h_n| == |h_b|` holds under
  any rotation whereas a wrong product order or sign error moves `h_n`'s direction.
- `ballistic_test.cpp` — `g = 9.80665`, `omega_b = 0`, no wrench, compared against
  `p_n(t) = p0 + v_n0 * t + 0.5 * g_n * t^2`. With `omega_b = 0` the attitude is constant, so `v_n` is
  exact and the closed form is exact. **Attitudes and assertions per Codex finding 2**: the second
  attitude carries **nonzero pitch** at a non-special angle (e.g. `euler_nb = (0.3, 0.4, 0.2)` rad),
  because a yaw-only attitude leaves NED gravity on the down axis unchanged and so cannot expose a
  transposed gravity rotation at all. And because transposing *both* the position kinematics and the
  gravity rotation can cancel in the composed nav-frame arc, the test **independently asserts the
  body-frame gravity** `rotate_n_to_b(q_nb, g_n)` against its hand-computed value rather than relying
  only on the trajectory. Only with both does this pin n→b.
- `precession_test.cpp` — symmetric top `inertia_b = diag(it, it, ia)`, `g = 0`. Asserts `r` constant
  and the transverse pair against the **initial-value form** (Codex finding 1):
  `p(t) = p0 * cos(lambda * t) + q0 * sin(lambda * t)`,
  `q(t) = q0 * cos(lambda * t) - p0 * sin(lambda * t)`, with `lambda = (it - ia) / it * r`.
  The plan's earlier same-sign `cos`/`sin` pair was wrong and would have validated the opposite sign:
  the EOM as written gives `p_dot = lambda * q` and `q_dot = -lambda * p`, so for `p0 = omega_t,
  q0 = 0` the solution is `p = omega_t * cos(lambda t)`, `q = -omega_t * sin(lambda t)`. Re-derived
  from Euler's equations and confirmed independently of Codex before accepting the finding.
- One YAML per oracle under `tests/configs/` (`physics_torque_free.yaml`,
  `physics_ballistic_level.yaml`, `physics_ballistic_pitched.yaml`, `physics_precession.yaml`), read
  via the existing `ASTRA_TEST_CONFIG_DIR` compile definition.
- Unit tests alongside, in the existing `astra_unit_tests`: `physical.mass` rejecting mass <= 0, a
  non-symmetric and a non-positive-definite inertia, each naming the key; `initial.trim` rejected as
  an unknown key; an unknown `physical.*` slot name rejected; `initial.state` defaulting to identity
  attitude when `euler_nb` is absent; `VehicleState` accessors against ADR-0003's layout.

## Out of scope, deliberately

- `Simulation.derivative` for Python (§8/§12) — trim and linearization are M3; no binding changes.
- Allocation counting and the NaN/|q|-drift health checks — PLAN.md gives those to PR 4, with the
  `zero allocations over 1000 frames` acceptance line.
- `AeroModel` / `PropulsionModel` / atmosphere / wind, `aircraft/` and `_template` — later milestones.
  `Eom::set_applied_wrench` is the seam left for them.

## Verification

`just check` is the gate and the definition of done. Build and test output goes to a file; only the
tail is read, and "verified" means the log's `check: OK` / `EXIT=0` line was read.

```
just check > /tmp/claude-1000/.../check.log 2>&1; echo "EXIT=$?" >> .../check.log
```

Targeted runs while iterating:

```
ctest --preset asan -R physics --output-on-failure
ctest --preset asan -L unit
```

Worth measuring early: the torque-free oracle is 1e6 RK4 steps with four virtual-dispatch derivative
evaluations each, and `just check` runs it under four presets including asan/UBSan. If it dominates
the gate's wall time, the fix is to keep the full 1000 s run (it is the acceptance criterion) and cut
per-step work, not to shorten the duration or widen the bound — widening a tolerance is a blocking
review finding.

## Review

1. `/codex:rescue` plan round now, in plan mode, EOM and kinematics focus (workflow.md §Review,
   PLAN.md PR 3). Findings that hold are folded in below; findings rejected are named with a reason.
2. After the code: from Bash, `codex-companion.mjs review --base origin/main --wait`, then
   `adversarial-review --base origin/main --wait` with focus naming ADR-0001 (signs, n→b, Hamilton
   scalar-first), ADR-0003 (13-state layout, Earth owns `p_n`), ADR-0004 (one EOM, noexcept
   derivative, no per-frame allocation), and ADR-0002 (SI, no unit suffixes).
3. This PR touches `core/kernel/`, `core/util/` and `plugins/physical/`, so Joe reads the diff.
4. `/wrap`.

## Codex plan-review findings

One `/codex:rescue` round ran in plan mode with the plan and acceptance command inlined, focused on the
EOM, the quaternion kinematics, and the three closed forms. **Three findings, all accepted and folded
into the test section above; none rejected.** All three were about the oracles being too weak to catch
the errors they exist to catch — which is the expensive kind, because such a test passes and is
believed.

1. **Symmetric-top closed form had the wrong sign.** The plan's same-sign `cos`/`sin` pair would have
   validated the *opposite* precession sign. The EOM as written gives `p_dot = lambda * q`,
   `q_dot = -lambda * p`, so the correct initial-value form is `p = p0 cos(lambda t) + q0 sin(lambda t)`,
   `q = q0 cos(lambda t) - p0 sin(lambda t)`. I re-derived this from Euler's equations before accepting
   it. This is the finding that justified the round.
2. **The ballistic test did not actually pin n→b.** A level attitude is the identity and a yaw-only
   attitude leaves NED gravity on the down axis, so neither can expose a transposed gravity rotation;
   worse, transposing both the position kinematics and the gravity rotation can cancel in the composed
   arc. Fixed by requiring nonzero pitch at a non-special angle and by asserting the body-frame gravity
   independently of the trajectory.
3. **The torque-free test left `omega_b(0)` unspecified.** A principal-axis initial condition satisfies
   `|omega| ~ 1` but yields constant motion, passing even with the Euler coupling terms wrong. Fixed by
   prescribing `(1, 1, 1) / sqrt(3)`.

Codex raised nothing against the EOM equations themselves — the velocity equation's Coriolis factor,
the `omega_nb_b` decomposition in the attitude kinematics, or the `noexcept`/no-allocation contract.
That is *weak* evidence rather than a clearance: it was asked to report only failures, so silence is
not a positive check. Those terms are all identically zero under flat Earth, so no test in this PR can
distinguish them from wrong ones; M2's round and rotating Earth models are where they are first
exercised, and that is the honest statement of what this PR verifies.
