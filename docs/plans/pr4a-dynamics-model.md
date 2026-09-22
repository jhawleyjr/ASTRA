# Implementation plan: M3 PR 4a — ADR-0021 dynamics interface and model-owned state

Spec/brief: `docs/decisions/0021-dynamics-model-owned-state.md` (**accepted**, 2026-09-20). Full
critique/revision record, all findings and fixes, already closed:
`docs/plans/m3-framework-realignment.md` — **not redone here**, per Joe's instruction. This document
is the implementation plan ADR-0021's own Acceptance line calls for ("a dedicated implementation
plan's own full acceptance table at implementation time") and the "exact location/format decided at
implementation time" items the ADR explicitly deferred.

Risk: **Tier 3** — `core/kernel/`, `core/interfaces/`, goldens under `tests/golden/**`
(`docs/process/workflow.md`'s risk table).

Status: **approved**. Revision 1's capture design was rejected (stale-SHA guard, single-preset
capture, no end-to-end golden, unresolved rigid-body signal/`Model`-ancestry questions, copyable
`StateBuffer`) and fixed in Revision 2; Revision 2 was approved by Joe conditional on Revision 3's
edits (capture guard's diff scope, frozen `cruise.yaml` fixture, `StateBuffer` move-assignment
deletion + move-test extension, explicit `stage_`-pointer statement for the rigid-body signals) —
this document is Revision 3, all of Joe's edits applied.

Plan approval: **approved by Joe, 2026-09-20**. Next: the R1/R1-Sim capture step (see below) in a
separate, `ASTRA_TOUCH_GOLDENS`-armed session Joe launches; implementation does not begin until that
step's output is committed.

## Repository findings

**Current shape.** This worktree's `HEAD` is now `9339f4a` (this session's own bookkeeping commit:
`PLAN.md`/`STATE.md` only — no `core/`/`plugins/`/`cmake/` change since `26ad594`, confirmed by
`git diff 26ad594 HEAD -- core/ plugins/ cmake/ aircraft/notional_transport/` being empty).
`26ad594` (PR #41, merged) is the
actual pre-change revision R1's golden characterizes; `9339f4a` is simply where this branch happens to
sit right now. The R1 section below is revised specifically because Revision 1 conflated the two —
see "Fix the capture guard" below.

- `Model` (`core/include/astra/interfaces/model.hpp`): four pure virtuals only
  (`init`/`reset`/`step`/`name`); no state hooks.
- `StateVector = Eigen::Vector<double, 13>` (`core/include/astra/kernel/state.hpp`), fixed layout
  `p_n(3) v_b(3) q_nb(4) omega_b(3)` (`state::kPosition` etc.).
- `VehicleState` (`core/include/astra/kernel/vehicle_state.hpp`) wraps `const StateVector&` by
  reference; every accessor slices a fixed offset.
- `Eom` (`core/include/astra/kernel/eom.hpp/.cpp`): holds `EarthModel&`, `GravityModel&`,
  `MassPropertiesModel&`; `begin_frame()` latches mass/inertia once per major frame;
  `set_applied_wrench()` is called fresh before each RK4 sub-stage
  (`core/src/kernel/vehicle.cpp:297`); `operator()(t, y) -> StateVector` is the
  `StateDerivative`-concept functor `Integrator::step` calls by value, four times per frame. **`Eom`
  is not a `Model`** — it is `Vehicle`'s own internal helper, constructed directly (not loaded via
  `PluginLoader`), with no `init()`/`reset()`/`step()`/`name()` of its own. This status carries over
  unchanged to its replacement (see "Does `RigidBody6DofDynamics` derive from `Model`?" below).
- `Integrator` (`core/include/astra/kernel/integrator.hpp`): `step()` is a template over a
  `StateDerivative` concept (`f(t,y) noexcept -> StateVector`), owns no scratch itself — every k1..k4
  and trial-state is a stack `StateVector` local. `post_step_renormalize(StateVector&)` is a free
  function called separately by `Vehicle::integrate()`.
- `Vehicle::integrate()` (`core/src/kernel/vehicle.cpp:221-328`) builds one local lambda
  (`derivative`) that wraps `Eom`: samples the stage environment (`prepare_stage_environment`),
  sums every `WrenchSource::wrench_b()` contribution, health-checks it, calls
  `eom_->set_applied_wrench(total)`, then `(*eom_)(t, y)`. `Integrator::step(derivative, ctx.t,
  state_)` runs all four stages; `state_` is `Vehicle`'s own `StateVector` member (fixed 13).
- `kCoreApiVersion = 7` (`core/include/astra/core_api.hpp.in`), last bumped for ADR-0020's
  `WrenchSource` vtable settle (M3 PR 3d). `Model`'s five new defaulted virtuals change `Model`'s own
  vtable layout, requiring the same bump mechanism (precedent: ADR-0018's 5→6, ADR-0020's 6→7).
- `SignalRegistry::add()/find()` (`core/include/astra/signals/signal_registry.hpp`) is the *existing*
  precedent for a pointer resolved once at init and read every frame thereafter — the mechanism
  ADR-0021 reuses for cross-model state reads; `add()` already throws on a duplicate name, which is
  the existing machinery the "duplicate published state name" acceptance clause needs, not new
  duplicate-detection logic.
- `HealthMonitor::check_state(const StateVector&, const StateNames&, ...)`
  (`core/include/astra/kernel/health_monitor.hpp:95`) is fixed to the 13-double rigid-body layout;
  needs generalizing per ADR-0021's own "Health/config" paragraph.
- No generic "N-double runtime buffer, sized once, no resize in its public interface" type exists
  anywhere in this tree today; ADR-0021's "dedicated wrapper" (R2) is new.
- Golden precedent: `tests/golden/nesc/generate_nesc_band.py` and
  `tests/golden/frames/adr0001_cases.hpp` are both **tolerance-band** goldens (`|a-b| <= tol`), not
  bit-exact. `tests/unit/wind/wind_determinism_test.cpp` already does bit-exact comparison
  (`std::bit_cast<std::uint64_t>`) but between **two live runs of the same build**, never against a
  frozen artifact from a different (pre-change) build — R1/R1-Sim below are the first frozen bit-exact
  goldens in this repository; there is no existing manifest convention to copy verbatim, only the
  bit-cast comparison idiom (reused directly) and the "human-owned generator, guarded by
  `guard-oracle.sh`" idiom from `generate_nesc_band.py` (partially reused).
- `docs/architecture.md` §1/§6/§7/§8/§18 are **already rewritten ahead of code** for ADR-0021 (rev 7,
  2026-09-20 — `docs/architecture.md:3,175-207,231-248,334,586`\*). Only the header's own "none yet
  implemented (PLAN.md M3 PR 3d/4a)" qualifier (`docs/architecture.md:3`) needs updating once this PR
  lands — no further architecture.md rewrite is in scope.
- `tests/unit/kernel/vehicle_state_test.cpp:21` — `EXPECT_EQ(&vs.vector(), &y);` — the
  address-identity assertion ADR-0021 requires rewriting to value equality (`VehicleState`'s
  constructor retypes from a `const StateVector&` reference to an `Eigen::Map` by value).
- `tests/physics/no_alloc_test.cpp` — existing allocation-counting harness
  (`astra_test::allocation_count()`) to extend per R2.
- Explicit (non-glob) CMake source lists: `core/CMakeLists.txt`, `tests/unit/CMakeLists.txt`,
  `tests/physics/CMakeLists.txt` — every new file must be added by name.
- `initial.state` config today parses `p_n/v_b/euler_nb/omega_b` only into the rigid-body block.
  **Decision, approved:** PR 4a ships no production state-owning model, so no YAML schema extension is
  added; this PR's state-owning test models hardcode a fixed constant in `initial_state()`.
- Python `python/astra/_core.cpp`'s `STATE_SIZE`/offset constants are explicitly PR 5 scope — no
  `python/` changes in this plan.
- `Simulation::for_testing()`'s `initial_states` parameter stays `std::span<const StateVector>`
  (rigid-body-only, 13 doubles) — no signature change.
- `Vehicle::state()` today returns `StateVector&` (an owned 13-double Eigen object). Under the
  combined buffer it becomes a view over the buffer's first 13 doubles
  (`Eigen::Map<const Eigen::Vector<double,13>>`). `tests/unit/wind/wind_determinism_test.cpp:106-109`
  reads `ya(i)`/`yb(i)` off this return value — `Eigen::Map` supports `operator()` identically, so
  this call site is expected to keep compiling unmodified; flagged as a build-verification item.

\* Line numbers are current as of `26ad594`/this session's read; re-verify against the actual diff
before citing them in a PR description.

## R1 golden capture — revised

Every point in this section responds directly to Joe's fixes. **No part of this section is performed
by this plan.** `tests/golden/**` is untouched by this plan's own diff, and
`.claude/touch-goldens`/`ASTRA_TOUCH_GOLDENS` is not armed by this plan. Joe authorizes the capture
step separately, after approving this document.

**Execution boundary, as Joe has specified it:** the capture step runs in a separate session Joe
launches himself with `ASTRA_TOUCH_GOLDENS=1`. That session's own scope is exactly this section and
nothing else — it runs `tests/golden/dynamics/capture_r1_golden.sh`, commits the harness/goldens/
frozen fixture/manifest the script produces, reports the four-preset agreement result for both
goldens (or the disagreement, per Fix 2), and stops. It does not begin any of the "Mapping and
sequence" implementation work below — that is a separate, later session, working against the now-
committed goldens.

### Fix 1: the capture guard

Revision 1's flaw: it asked the harness to refuse output "unless `git rev-parse HEAD` reads exactly
`26ad594`" — but `HEAD` is already `9339f4a` (this session's bookkeeping commit) before any capture
code exists, and *committing the harness itself* (which must be committed for its own blob hash to go
in the manifest) moves `HEAD` again. A literal-SHA-equality guard can never pass.

**Fix: a wrapper script, not a SHA-equality check.**
`tests/golden/dynamics/capture_r1_golden.sh` is the one entry point for the whole capture step
(both goldens, see Fix 3). Before building or running anything, it:

1. Requires a clean tree: `git status --porcelain` must be empty. Abort otherwise, naming the dirty
   paths.
2. Requires **no diff in the paths that actually affect the captured arithmetic** between the fixed
   baseline `26ad594` and the current `HEAD`: `git diff --quiet 26ad594 -- core/ plugins/ cmake/
   CMakeLists.txt CMakePresets.json justfile aircraft/notional_transport/`. Scope is deliberately
   exactly these paths (not `icd/`, `embedded/`, `docs/`, `tests/`) — the capture harness touches
   `core/` + the flat/constant/constant plugins + the CMake/build-flag machinery that decides how they
   compile for R1, **and** `aircraft/notional_transport/` for the second (R1-Sim) golden below, since
   that golden's own bit-identity claim depends on those plugins' compiled arithmetic exactly as much
   as it depends on `core/`; a `docs/`, `PLAN.md`/`STATE.md`, or unrelated-plugin change since
   `26ad594` (exactly this worktree's actual situation right now) must not block capture, but a
   `core/`, `cmake/`, or `aircraft/notional_transport/` change must. Abort with `git diff --stat
   26ad594 -- <those paths>` on any hit, naming exactly what changed.
3. Only once both checks pass does it add and commit the capture harness files themselves (the one
   commit this step is allowed to make), then configure/build/run under all four presets (Fix 2), then
   write the golden headers + manifest (Fix 3/4), then commit those.

**Both SHAs recorded in the manifest**, not one: `baseline_revision: 26ad594` (the pre-change revision
step 2 above confirmed zero drift against, for the scoped paths) and `capture_revision: <HEAD after
step 3's own commit>` (the actual commit the harness ran from, source-identical to `26ad594` in every
path that matters, but literally at a later `HEAD` because the harness file itself and the docs
bookkeeping ahead of it exist there). The manifest states the relationship explicitly: "the captured
arithmetic is `26ad594`'s; `capture_revision` is where the harness that captured it lives."

### Fix 2: four-preset capture with a bit-identity cross-check, not a single fixed preset

Revision 1 fixed the capture build to `release` alone and left "does this reproduce across presets"
as an untested assumption the *post-change* test would be the first thing to ever check. That is
backwards: an assumption this load-bearing must be checked on the **pre-change** side too, before it
becomes the thing 34 recompiled plugins and a rewritten `Integrator` get judged against.

**Fix:** the wrapper script builds and runs the capture harness under **all four presets** (`debug`,
`release`, `asan`, `static-plugins`) against the identical, already-verified-unchanged pre-change
source. It compares all four presets' output **bit-for-bit** (`std::bit_cast<std::uint64_t>` per
double, every sample, both goldens). Two outcomes:

- **All four agree**: proceed to write the golden + manifest, recording `cross_preset_agreement:
  true` and each preset's own compiler/flag metadata (four entries, not one — see Fix 4).
- **Any disagree**: the script **stops, writes nothing to `tests/golden/**`, and prints which
  presets disagree, on which golden, which sample, and which double** — this is exactly the "stop
  and tell me if they disagree" instruction. A genuine cross-preset floating-point non-determinism in
  the pre-change build is a finding for Joe, not something this plan resolves unilaterally (e.g. by
  picking one preset's output as canonical).

### Fix 3: a second golden, captured in the same step — full `Simulation` on real `cruise.yaml`

Added to the same `capture_r1_golden.sh` invocation, under the identical guard (Fix 1) and identical
four-preset cross-check (Fix 2):

- **Fixture, frozen at capture time (Joe's fix — the golden must not depend on the shipped config
  staying byte-identical forever).** The capture step copies
  `aircraft/notional_transport/configs/cruise.yaml` (`sim.dt = 0.001`, `sim.frames = 1000`) verbatim
  into `tests/golden/dynamics/cruise_end_to_end_r1_fixture.yaml` and captures against **that frozen
  copy**, not the live shipped path. Its exact git blob hash (of the frozen copy, taken at the moment
  it's committed in step 3) is recorded in the manifest. This exercises the full
  `Simulation::from_config` → `Vehicle::init()` → four-RK4-stage `wrench_b()` accumulation →
  `Eom`/post-step pipeline end to end (real `wgs84_rotating`/`wgs84_j2`/`isa1976`/`dryden` environment
  models, real `notional_transport` aero/turbofan `WrenchSource`s, `dryden`'s seeded — `seed: 42` —
  RNG, so still fully deterministic), a materially stronger regression check than R1's isolated
  `Eom`-level golden: it is the thing that actually proves the combined-buffer/two-phase-init/stage-
  buffer refactor reproduces a real run bit-for-bit, not only the raw rigid-body arithmetic in
  isolation. Freezing the fixture means a later PR that edits the shipped `cruise.yaml` (PR 6's trim,
  PR 8's own frozen 300 s run) cannot silently invalidate this golden out from under it — the golden
  and the exact config it was captured against travel together, permanently, under `tests/golden/**`.
- **Harness**: a second `main()` (or a second mode of the same harness binary) that calls
  `Simulation::from_config(Config::from_file(".../cruise_end_to_end_r1_fixture.yaml"), plugin_dir);
  const HaltReason halt = sim.run();` and — Joe's own words — records **state bit patterns after N
  frames**: the final rigid-body state (`sim.vehicles().front().state()`, 13 doubles) as
  `std::bit_cast<std::uint64_t>`, plus `halt` itself (expected falsy) so a future regression that
  starts halting this config is caught as loudly as a numeric drift would be.
- **Output**: `tests/golden/dynamics/cruise_end_to_end_r1_golden.hpp` (13 `uint64_t` bit patterns +
  the frame count + the expected non-halt) plus the frozen `cruise_end_to_end_r1_fixture.yaml` itself,
  recorded in the same manifest as R1's own golden
  (`tests/golden/dynamics/rigid_body_6dof_r1_manifest.md`, extended to cover both artifacts rather
  than a second manifest file — one capture step, one authorization, one manifest).

**Matching post-change test**: `tests/physics/dynamics/cruise_end_to_end_r1_golden_test.cpp` — loads
`tests/golden/dynamics/cruise_end_to_end_r1_fixture.yaml` (the frozen copy, never
`aircraft/notional_transport/configs/cruise.yaml` directly) through post-change `Simulation`,
`sim.run()`, and asserts representation-level equality against the golden's recorded final state, plus
`EXPECT_FALSE(halt)`. Added to `tests/physics/CMakeLists.txt`, so it runs under all four presets
automatically as part of `just check`, the same way every existing physics test does — no
special-casing needed on the post-change side (Fix 2's four-preset discipline lives entirely in the
*capture* step; the post-change comparison test simply runs wherever the physics-test suite already
runs). Because the test reads the frozen fixture, not the shipped config, this test's own pass/fail is
permanently decoupled from whatever `cruise.yaml` looks like by the time PR 6/8 land.

### Fix 4: manifest, extended

Recorded per ADR-0021's "at minimum" list, now covering both goldens and all four presets:

- `baseline_revision: 26ad594`, `capture_revision: <actual HEAD at capture>` (Fix 1).
- `cross_preset_agreement: true` for each golden, or the step never completes (Fix 2).
- The exact sampled initial-state bit patterns (R1) — the actual values, not a description.
- The capture harness's own path(s) and exact git blob hash(es), committed as the one commit step 3
  (Fix 1) performs.
- The frozen `cruise_end_to_end_r1_fixture.yaml`'s own exact git blob hash (Fix 3), recorded alongside
  the harness's, so the manifest names exactly which config bytes R1-Sim's result corresponds to.
- Per preset (four entries): `clang --version`, Eigen's version string, `__GLIBCXX__`, target triple,
  the preset's own `CMAKE_BUILD_TYPE`/sanitizer/static-plugins flags — read programmatically by the
  harness/script and printed, not hand-typed.

### The post-change R1 (Eom-level) test — unchanged from Revision 1, restated for completeness

`tests/physics/dynamics/rigid_body_r1_golden_test.cpp`: reads the checked-in golden header,
reconstructs each sample via `std::bit_cast<double>`, drives it through post-change
`RigidBody6DofDynamics` + the rewritten `Integrator::step` with the identical fixed wrench, asserts
representation-level equality per double, per sample. Written and expected to fail before the capture
step runs, so the comparison shape is pinned in review before any golden bytes exist.

## Design decisions this plan fixes

### Does `RigidBody6DofDynamics` derive from `Model`? — **No.**

ADR-0021 fixes two *separate* interfaces: `DynamicsModel` (`state_count`/`initial_state`/`derivative`/
`post_step`, raw-pointer shaped) and `Model`'s five new virtuals (`state_count`/`initial_state`/
`integrate_state`/`bind_state`/`resolve_peers`, `VehicleState`/`EnvironmentSample`-shaped) — similar
names, different signatures, different purposes. `RigidBody6DofDynamics` implements only
`DynamicsModel`. It is **not** a `Model` subclass: like `Eom` today, it is constructed directly by
`Vehicle` (not loaded through `PluginLoader`), has no `init()`/`reset()`/`step()`/`name()`, and is held
as `std::optional<RigidBody6DofDynamics>` exactly where `std::optional<Eom> eom_` lives today. It does
**not** go through the generic per-`Model` state-discovery loop described below — `Vehicle` special-
cases it, always occupying `combined_[0..13)` whenever `physics != nullptr`. `DynamicsModel::
initial_state()` is a pure virtual (unlike `Model`'s own defaulted version), so `RigidBody6DofDynamics`
must implement it: it writes all 13 doubles to `0.0`, matching today's `StateVector state_ =
StateVector::Zero()` default exactly. `Vehicle` still calls it uniformly (no bypass), and then —
whenever `initial.state`/`for_testing`'s span provides a real value — overwrites `combined_[0..13)`
from that, the same "zero, then the caller's explicit value" two-step the current member-initializer-
plus-explicit-assignment already performs today, just retargeted to the combined buffer.

Every genuine `Model`-derived plugin (the vehicle's own `mass` model, every `vehicle.models` entry —
`WrenchSource` or not) *does* go through the generic loop: any of them may declare `state_count() > 0`
and gets discovered, seeded, bound, and published the same way. Environment models `Vehicle` only
holds a raw, shared pointer to (`earth_`/`gravity_`/`atmosphere_`/wind) are out of scope for
state-ownership this PR — ADR-0011 makes state per-vehicle, and a *shared* model owning per-vehicle
state is ambiguous (whose buffer would it live in, under two vehicles?); nothing in this PR or any
planned PR exercises it, so this plan does not design for it.

### Resolved: the rigid-body signal question

**Decision: give it meaningful names, published unconditionally.** `Vehicle` registers the rigid-body
block's own 13 scalars with `SignalRegistry::add()` whenever `physics != nullptr` — mirroring exactly
how `wrench_total`'s 6 components are already unconditionally registered today
(`core/src/kernel/vehicle.cpp:86-92`) — under `"<name_>.state.p_n_0"`, `"...p_n_1"`, `"...p_n_2"`,
`"...v_b_0"`..`"...v_b_2"`, `"...q_nb_w"`, `"...q_nb_x"`, `"...q_nb_y"`, `"...q_nb_z"`,
`"...omega_b_0"`..`"...omega_b_2"`. Indices, not axis letters, for `p_n`/`v_b`/`omega_b` because
`p_n`'s own meaning is Earth-model-dependent (NED metres for flat Earth; lat/lon/alt for round/
WGS-84 — ADR-0003) — a generic `_x`/`_y`/`_z` suffix would misrepresent round-Earth `p_n`; unit
metadata for `p_n_*` is therefore left empty (Earth-model-dependent, the same "position representation
is owned by the Earth model" reasoning ADR-0003 already establishes), while `v_b_*` gets `"m/s"`,
`q_nb_*` gets `""` (dimensionless), `omega_b_*` gets `"rad/s"`. This is a genuinely new set of signals
— today these 13 doubles are reachable only through `Vehicle::state()`, never through the registry.

**These 13 signals point into `stage_`, not `combined_`, and not a separately latched copy (Joe's
fix, stated explicitly).** `registry.add()`'s `const double*` argument for each of the 13 is
`&stage_.data()[i]` — the same live, sub-stage-refreshed address every other state-owning model's own
published slice gets (ADR-0021: "pointing at the live stage-buffer address, not a separately latched
copy"), **not** the `published_wrench_`-style pattern of latching a value once at the k1 sub-stage and
publishing that copy. A reader resolving one of these 13 names via `SignalRegistry::find()` mid-frame
sees that RK4 sub-stage's own trial value, refreshed four times a frame, exactly like `bind_state()`'s
own contract for every other model's state — consistent, not a second convention for the rigid body.

**Effect on logger output, stated explicitly.** No logger exists yet (M5); the registry is exactly
what the future logger samples (`wind_determinism_test.cpp`'s own header comment). Effect: once M5's
logger exists, it picks up these 13 rigid-body signals automatically, with no M5-side special-casing
— arguably closes a gap M5 would otherwise have had to add itself. Testable side effect *now*, not
deferred: `WindDeterminism.TwoRunsAreBitIdenticalOnEveryRegisteredSignal`
(`tests/unit/wind/wind_determinism_test.cpp:83`) iterates `registry().signals()` — its signal *count*
grows by 13 for every physics-configured fixture it runs (both fixtures in that test are), but the
test compares two live runs of the *same* build against each other, so both sides gain the identical
13 signals identically and the test keeps passing unmodified — flagged here so the count change isn't
mistaken for a regression if noticed during review.

### `StateBuffer` — non-copyable, move-constructible only, with an explicit element-copy method

`core/include/astra/kernel/state_buffer.hpp`: owns one `std::vector<double>` sized exactly once in its
constructor; exposes `data()` (mutable and const) and `size()` only — no `resize()`/`push_back()`/
`insert()` anywhere in its public interface.

- **Copy constructor and copy-assignment operator are explicitly `= delete`d.** Nothing in this design
  needs to copy a `StateBuffer` as a whole object — the one place that looked like it needed to
  (`stage_ = combined_` in Revision 1's own prose) is not actually a copy-assignment at all, see below.
- **Move constructor stays available (`= default`); move-assignment operator is explicitly
  `= delete`d (Joe's fix).** The constructor is required because `Vehicle` itself is moved into
  `Simulation::vehicles_` after `init()` runs (`core/src/kernel/simulation.cpp`'s existing pattern,
  unchanged by this PR) — that move happens **after** `bind_state()`/`SignalRegistry::add()` have
  already resolved pointers into `Vehicle`'s `stage_` (mirroring `published_wrench_`'s own existing
  "moved after pointers into it are live" comment). `std::vector<double>`'s move *constructor* never
  reallocates or relocates the underlying heap block — it transfers ownership of the same allocation
  into brand-new, not-yet-pointed-into storage — so `StateBuffer::data()`'s address is identical before
  and after that move. Move-*assignment* is a different, needless risk: it would overwrite an
  **already-bound** `StateBuffer`'s own storage with another's, which (unlike construction into fresh
  storage) could leave every pointer previously resolved into the assigned-to object dangling into
  now-orphaned memory — and nothing in this design ever needs to move-assign one `StateBuffer` over an
  existing one (`Vehicle`'s own move into `vehicles_` uses move *construction*, the same way `std::
  vector::push_back`/reallocation always move-constructs into new slots, never move-assigns over
  existing ones). Deleting it removes a bug class this design has no use for, rather than leaving it
  available and merely unused.
- **The post-step/init sync is specified as an element copy, not a copy-assignment (Joe's fix).**
  `StateBuffer::copy_from(const StateBuffer& other) noexcept` — `std::copy_n(other.data(), size(),
  data())`, asserting `other.size() == size()` (a programming-error `assert`, both sides always sized
  identically by construction — this is never a resize). This is deliberately **not** `operator=`:
  the C++ standard does not guarantee `std::vector::operator=` reuses existing capacity even when
  sizes already match (an implementation is permitted to reallocate), and reallocating `stage_`'s own
  backing store would silently invalidate every `bind_state()`/`SignalRegistry::add()` pointer already
  resolved into it — exactly the bug class Joe is asking this method to make structurally impossible,
  not merely undocumented. `Vehicle::integrate()`'s post-step sync becomes `stage_.copy_from(combined_)`
  (both branches — wrench-fault rollback and completed step); `Vehicle::init()` performs the identical
  call once at the end of phase one.
- **`Integrator`'s own k1..k4/trial-state scratch stays plain `std::vector<double>`, not
  `StateBuffer`.** No external code ever takes a pointer into `Integrator`'s private scratch (unlike
  `Vehicle`'s `combined_`/`stage_`, which `bind_state()` and `SignalRegistry` do), so the extra
  copy-safety `StateBuffer` exists for isn't needed there — using the plain type avoids implying a
  guarantee (cross-model pointer stability) that scratch buffer was never meant to offer.

**Pointer-stability tests, extended (Joe's fix):** `tests/unit/kernel/state_buffer_test.cpp` —
`StateBuffer, DataPointerIsStableAcrossMoveConstruction`: construct, capture `data()`'s address,
move-construct into a new instance, assert the new object's `data()` returns the **identical address**
(and that `StateBuffer` has no move-assignment operator to test, only a move constructor). **Extended
to cover a real `Vehicle` move, not only the isolated type**: `tests/unit/kernel/
vehicle_state_coupling_test.cpp` gains `PointerIntoStageBufferSurvivesTheVehicleMoveIntoSimulation` —
build a `Simulation` via `Simulation::for_testing`/`from_config` with a `state_owner` model configured,
capture the `const double*` a peer's `resolve_peers()` resolved (or the pointer `SignalRegistry::find()`
returns for the state-owning model's own published signal) **before** `Simulation`'s own construction
returns, then confirm — after the `Vehicle` has actually been moved into `vehicles_` internally — that
pointer still reads the correct, live value across a `step()`. This is the literal mechanism
`Vehicle`'s own move-into-`vehicles_` depends on, tested end to end through the real construction path,
not only through the isolated `StateBuffer` unit test above (which would otherwise be the only thing
standing between a future refactor and every coupling test failing mysteriously instead of this one
test failing legibly).

### `Vehicle`'s two-phase init and cross-model state-signal naming

Phase one — for every entry in `vehicle.models` (plus the vehicle's own `mass` model, exactly as
today): `load()`, `init()`, `bind_inputs()` (unchanged), record `state_count()`, check
`requires_environment()` (unchanged). Once every count is known, construct `combined_`/`stage_` once
(size `13 + sum(state_count())`); seed `combined_[0..13)` from the caller's initial state (unchanged
data flow, new storage) and call `RigidBody6DofDynamics::initial_state()` first (see above) so the
zero-then-overwrite order matches today exactly. For every **`Model`-derived** state-owning entry
(`state_count() > 0`; the rigid body is handled separately, above, not through this loop): call
`initial_state()` into its `combined_` slice, `bind_state()` with its `stage_` slice pointer, and —
on `Vehicle`'s own behalf, since neither `bind_state()` nor `resolve_peers()` carries a
`SignalRegistry&` a model could use itself — register each element with `SignalRegistry::add()`,
named `"<vehicle_name>.<model.name()>.state.<i>"` where `i` is **local to that model** (`0..
state_count()-1`), unit left empty.

**This local (not globally offset) indexing is deliberate, not an oversight**: two instances of one
state-owning plugin type, neither configured to disambiguate its own `name()`, collide on their very
first signal name (`"v0.<same_name>.state.0"` for both) — `SignalRegistry::add()`'s existing
duplicate-name throw catches this at init, surfaced as a `ConfigError`, never a silent overwrite or
`std::terminate()`. This is the realistic scenario the "duplicate published state name" acceptance
clause protects against (the same posture ADR-0017 already puts on plugin authors for `bind_inputs()`
— e.g. `turbofan`'s own `station: left`/`station: right` config key disambiguates two instances' own
published names; a state-owning plugin needing more than one instance per vehicle must do the same for
`name()`), not an artificial test-only trick layered on top of an otherwise-collision-proof scheme.

Phase two — `resolve_peers()` for every model, state-owning or not, only after every model's phase one
(including publishing) has finished — satisfies mutual-dependence regardless of `vehicle.models` order.

### `HealthMonitor::check_state` generalization — unchanged from Revision 1

Gains an overload taking the full combined buffer plus the existing four rigid-body `StateNames` and a
`std::span<const std::string_view>` of one name per state-owning model beyond the rigid body
(per-model, not per-scalar — consistent with `check_wrench`'s own per-plugin naming): checks the four
rigid-body segments exactly as today, then a generic finiteness check over each remaining model's
slice, naming that model on a halt.

## The new test-only plugin: `tests/plugins/state_owner`

One configurable `Model` (not a `WrenchSource`), `state_count() == 1`, reused via different YAML
fixtures across every coupling/duplicate/missing-peer/pointer-stability-adjacent test below (matching
this repo's existing frugal-plugin-count style — `counting_aero`, `nan_wrench` are each reused across
several tests via config, not duplicated per case). Config keys:
- `instance` (optional string): folded into what `name()` returns (`"state_owner"` with no `instance`
  configured, `"state_owner_<instance>"` otherwise) — the disambiguation mechanism the duplicate-name
  design above assumes plugin authors use, mirroring `turbofan`'s own `station` key.
- `resolve_peer_name` (optional string): if present, `resolve_peers()` calls
  `registry.find(resolve_peer_name)`; throws `ConfigError` naming it if `find()` returns `nullptr`.
- `initial_value` (optional double, default `0.0`): what `initial_state()` writes.

## Mapping and sequence

One Claude builder (this session), sequential — no independent packages.

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance command |
|---|---|---|---|
| R1 capture guard + four-preset cross-check (separately authorized) | `tests/golden/dynamics/capture_r1_golden.sh` + `capture_r1_golden.cpp` | Clean tree + no diff in `core/`,`plugins/`,`cmake/`,top-level build files vs. `26ad594`; builds/runs under all four presets, halts on disagreement | Manual run per the R1 section; not part of `just check` |
| R1-Sim capture (same step) | Second capture mode in the same script; freezes `cruise.yaml` into `tests/golden/dynamics/cruise_end_to_end_r1_fixture.yaml` | Same guard (now also `aircraft/notional_transport/`) + four-preset discipline | Same manual run |
| R1 (Eom-level) bit-identity test | `tests/physics/dynamics/rigid_body_r1_golden_test.cpp` | Reads the committed golden; post-change `RigidBody6DofDynamics`+`Integrator` | `ctest --preset asan -R RigidBodyR1Golden` (all four presets via full gate) |
| R1-Sim (end-to-end) bit-identity test | `tests/physics/dynamics/cruise_end_to_end_r1_golden_test.cpp` | Real `cruise.yaml`, post-change `Simulation` | `ctest --preset asan -R CruiseEndToEndR1Golden` (all four presets via full gate) |
| `DynamicsModel` interface | new `core/include/astra/interfaces/dynamics_model.hpp` + `.cpp` | ADR-0021's exact declared shape | Compiles; no standalone test |
| `RigidBody6DofDynamics` (retires `Eom`; not a `Model`) | new `core/include/astra/kernel/rigid_body_6dof_dynamics.hpp/.cpp`; delete `eom.hpp/.cpp` | Byte-for-byte port (R1); `Vehicle` holds it directly, not via `PluginLoader` | R1/R1-Sim golden tests; existing `tests/physics/{ballistic,torque_free,precession,round_earth_drop}_test.cpp` and `Nesc.*` pass unmodified |
| `Model`'s five new virtuals | `core/include/astra/interfaces/model.hpp` | All defaulted; every existing `Model` subclass keeps compiling with zero edits (34 plugins, listed below) | Full-tree build under all four presets |
| `kCoreApiVersion` 7→8 | `core/include/astra/core_api.hpp.in` | Precedented mechanism | `tests/plugins/bad_version` still rejected — existing test, unmodified |
| `wrench_source.hpp` purity-contract amendment | `core/include/astra/interfaces/wrench_source.hpp` | ADR-0021's exact text | Diff review only |
| `VehicleState` retype | `core/include/astra/kernel/vehicle_state.hpp` | `Eigen::Map` ctor from a raw pointer | `vehicle_state_test.cpp` — address-identity line replaced with value-equality |
| `StateBuffer` type | new `core/include/astra/kernel/state_buffer.hpp` | Non-copyable, move-constructible only (no move-assignment), `copy_from()` element copy, no resize in public interface | `tests/unit/kernel/state_buffer_test.cpp` (new) — sizing, `copy_from` correctness, `DataPointerIsStableAcrossMoveConstruction`; `vehicle_state_coupling_test.cpp`'s `PointerIntoStageBufferSurvivesTheVehicleMoveIntoSimulation` |
| `Integrator` write-into-buffer contract | `core/include/astra/kernel/integrator.hpp` | `Integrator(state_count, kind, dt)`; owns k1..k4/trial scratch (plain `std::vector<double>`), sized once | `tests/unit/kernel/integrator_test.cpp` rewritten — same closed-form assertions, new functor shape |
| `Vehicle` two-phase init + stage buffer + rigid-body signal publish | `core/include/astra/kernel/vehicle.hpp/.cpp` | `combined_`/`stage_` `StateBuffer`s; `RigidBody6DofDynamics` special-cased at offset 0; 13 meaningful signals always published when physics configured | New `tests/unit/kernel/vehicle_state_coupling_test.cpp` cases below; `WindDeterminism.*` unmodified (signal count grows, comparison still passes) |
| Sub-stage-exact cross-model read (own + peer) | `state_owner` + a peer configured with `resolve_peer_name` | `bind_state()` + `SignalRegistry::find()` | `vehicle_state_coupling_test.cpp` — `StateOwnerAndPeerReadTheIdenticalSubStageValueAtEveryStage` |
| Mutual cross-model dependence, order-independent | two `state_owner` instances (`instance: a`/`instance: b`), each resolving the other, both `vehicle.models` orders | Phase two runs for every model only after every model's phase one | `vehicle_state_coupling_test.cpp` — `MutualStateDependenceResolvesRegardlessOfConfigOrder` (parameterized) |
| Stage buffer == combined buffer outside a derivative call | `state_owner` fixture | `copy_from()` sync (normal frame, wrench-fault-rolled-back frame, end of `init()`) | `vehicle_state_coupling_test.cpp` — `StageBufferMatchesCombinedBufferAfterInit`, `...AfterANormalFrame`, `...AfterAHaltedFrame` |
| Duplicate published state name | two `state_owner` instances, **neither** given a distinguishing `instance` | Local per-model index naming collides by design (see above) | `vehicle_state_coupling_test.cpp` — `DuplicateStateSignalNameThrowsConfigError` |
| Missing required peer | `state_owner` with `resolve_peer_name` pointing at a name that doesn't exist | `resolve_peers()` throws `ConfigError` naming it | `vehicle_state_coupling_test.cpp` — `MissingRequiredPeerThrowsConfigErrorNamingIt` |
| R2 (no allocation) | `tests/physics/no_alloc_test.cpp` extended | `state_owner` wired into a new `no_alloc_state_owner.yaml` fixture | New `TEST(NoAlloc, ZeroHeapAllocationsOverOneThousandFramesWithAStateOwningModel)`; `ctest --preset asan -R NoAlloc` |
| `HealthMonitor::check_state` generalization | `core/include/astra/kernel/health_monitor.hpp/.cpp` | Rigid-body four segments unchanged; generic finiteness over remaining slices, named per model | `tests/unit/health/health_monitor_test.cpp` — new case: a `state_owner` slice going non-finite halts naming it |
| `Simulation`/`HealthMonitor` call-site updates | `core/src/kernel/simulation.cpp` | `check_state` call site passes generalized arguments | Covered by existing `SimulationHalt.*`/`Nesc.*`/`NoAlloc.*` passing unmodified |
| `docs/architecture.md` status line | `docs/architecture.md:3` | Drop "none yet implemented ... M3 PR 3d/4a" | Diff review only |
| `PLAN.md`/`STATE.md` | Checked off with merge SHA once merged (not this plan's own action) | — | — |

Existing tests confirmed to need **no** source edit, only recompilation against the ABI bump:
`tests/unit/kernel/{scheduler,clock,simulation,simulation_halt,simulation_input_seam,
simulation_physical_config,notional_transport_cruise_config,aero_propulsion_seam,
vehicle_environment}_test.cpp`, `tests/unit/health/health_monitor_test.cpp` (existing cases),
`tests/unit/wind/*`, `tests/physics/{dryden_psd,notional_transport/*}_test.cpp`.

## Every plugin and test stub affected by the new `Model` virtuals and the `kCoreApiVersion` bump

All 34 recompile because of the ABI bump; **none needs a source edit** — verified by grep before
writing this plan: none of the 34 currently references `state_count`/`initial_state`/
`integrate_state`/`bind_state`/`resolve_peers`.

**Shipped physical plugins (11):** `plugins/physical/atmosphere/isa1976`,
`plugins/physical/earth/{flat,round,wgs84_rotating}`,
`plugins/physical/gravity/{constant,inverse_square,wgs84_j2}`, `plugins/physical/mass/constant`,
`plugins/physical/wind/{dryden,table,uniform}`.

**Shipped aircraft-specific plugins (6):** `aircraft/notional_transport/physical/{aero,mass}`,
`aircraft/notional_transport/hardware/propulsion/turbofan`, `aircraft/_template/physical/{aero,mass}`,
`aircraft/_template/hardware/propulsion/turbofan`.

**Existing test-only plugins (17), unmodified, recompile only:** `tests/plugins/{aero_math_odr_check,
aero_stub, atmosphere_consumer, bad_toolchain, bad_version, counting_aero, counting_wind, dummy,
flaky_atmosphere, geodetic_wind, input_consumer, nan_atmosphere, nan_wind, nan_wrench, nesc_aero,
propulsion_stub, wind_consumer}`. `bad_version`/`bad_toolchain` deliberately keep failing their own
rejection checks across the bump — their existing tests are the regression check that the bump didn't
loosen rejection.

**New test-only plugin added by this PR (1):** `tests/plugins/state_owner` (see above).

Total: 34 existing + 1 new = 35 `Model`-hierarchy binaries rebuilt by this PR.

## Validation and risks

- **Fast**: `./scripts/check-fast.sh` during iteration.
- **Full**: `./scripts/check-full.sh` once on the final candidate, retained evidence log.
- **Independent oracle**: R1/R1-Sim's frozen goldens (captured, cross-preset-verified build, see
  above) are the oracle for byte-for-byte arithmetic equality — now checked for cross-preset agreement
  at *capture* time as well as post-change comparison time, closing Revision 1's own single-preset gap.
  The existing NESC ensemble-band goldens and ballistic/torque-free/precession/round-earth-drop
  closed-form tests remain the oracle for physical correctness.
- **Compatibility**: no plugin outside this tree implements `Model`/`DynamicsModel` — the ABI bump
  breaks nothing that exists. Fully reversible.
- **Scope exclusions, explicit**: R3 (mass-as-state) and R4 (Python `derivative()` length-N contract,
  PR 5) are both out of scope. `initial.state`'s YAML schema extension for a state-owning model's own
  initial value stays deferred (approved).
- **Human-owned files touched**: `tests/golden/dynamics/{capture_r1_golden.sh, capture_r1_golden.cpp,
  rigid_body_6dof_r1_golden.hpp, cruise_end_to_end_r1_golden.hpp, rigid_body_6dof_r1_manifest.md}` —
  all under `tests/golden/**`, requiring Joe's `ASTRA_TOUCH_GOLDENS` authorization for the capture step
  specifically. No other `docs/decisions/**` or tolerance-file edit in this plan.

## Plan critique and decisions

Codex plan critique **already done** for ADR-0021's own design — not repeated here. This document's
own implementation-time decisions (the `state_owner` test-plugin design, rigid-body signal naming and
its logger effect, `RigidBody6DofDynamics`'s non-`Model` status, `StateBuffer`'s copy/move contract,
the capture guard/manifest mechanics) were reviewed and revised twice already (Revision 1 → 2 → 3, this
document, approved) directly by Joe, not by a Codex round — the implementation itself still gets
Codex's ordinary review per Tier 3 (see "Done when").

## Done when

- R1/R1-Sim capture step run (separately authorized) and its output committed, including a recorded
  `cross_preset_agreement: true` for both goldens (or a disclosed, Joe-reviewed finding if any preset
  disagreed and the step halted).
- Every row of the Mapping table above implemented and its named acceptance command passing.
- `./scripts/check-full.sh` green, evidence retained.
- **Both** a Codex general review round and a Codex adversarial review round (per Tier 3 —
  `docs/process/workflow.md`: "Physics diffs also need adversarial review focused on ADRs, units/
  signs, domain validity, allocation, and determinism" — this diff is `core/kernel/`-wide, changes
  allocation ownership (`Integrator`, `StateBuffer`) and determinism-sensitive code (the RK4 port,
  the golden comparison itself), so both rounds apply, not general review alone), findings verified
  and closed within the bounded fix-loop budget.
- `docs/architecture.md`'s status line updated; `PLAN.md`'s PR 4a box left for Joe to check at merge
  time, same convention as PR 4.
- Human packet prepared (`/human-review`) before Joe's merge decision.
