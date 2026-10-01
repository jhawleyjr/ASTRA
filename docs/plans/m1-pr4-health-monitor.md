# M1 PR 4 — HealthMonitor, typed halt reason, allocation counter

Recovered from `~/.claude/plans/m1-pr-4-as-piped-ullman.md` on 2026-09-30. Approval: the file records none; the work merged as #21. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

`docs/architecture.md` §15 ("A sim that produces a plausible-looking trajectory from a broken setup is
worse than one that refuses to run") specifies `core/kernel/HealthMonitor`. Nothing of it exists yet.
Today `Simulation::run()` returns `void` and loops `step()` unconditionally; `Vehicle::integrate()`
explicitly discards `post_step_renormalize`'s `[[nodiscard]] bool` with a comment pointing at this PR;
`Integrator::step` deliberately does *not* renormalize so `|q|` drift is observable; and
`Eom::set_applied_wrench` exists with **no caller anywhere in the tree** — there is no wrench
accumulation stage at all. There is also no allocation counter, so the §14 rule "no heap allocation in
the per-frame path" is enforced only by review.

PR 4 closes all three: the two during-run checks §15 lists first, a halt channel out of `run()` that
does not throw, and the allocation detector. It completes the M1 row of §16.

## Design decisions this plan makes (not pinned by any ADR — the `/codex:rescue` round covered these)

1. **`WrenchSource` is introduced now.** §15's "NaN/Inf in **any wrench contribution**, with the
   offending plugin named" needs per-contribution data that does not exist. A new
   `core/include/astra/interfaces/wrench_source.hpp` adds `class WrenchSource : public Model` with
   `[[nodiscard]] virtual Wrench wrench_b(const VehicleState&) const noexcept = 0;` and an out-of-line
   destructor in `libastra_core` (the same key-function fix `Model`/`EarthModel` needed for
   `dynamic_cast` across `-fvisibility=hidden`). M3's `AeroModel`/`PropulsionModel` derive from it.
   `Vehicle::init` resolves every `vehicle.models` entry that `dynamic_cast`s to `WrenchSource*` into a
   `std::vector<WrenchSource*>` **once, at init** — same pattern already used for `EarthModel*` /
   `GravityModel*` in `simulation.cpp`. Non-wrench models are unaffected, so PR 2's dummy fixture and
   the PR 3 physics configs keep running unchanged.
2. **Halt reason is a POD carrying a `std::string_view`**, because §14 forbids `std::string`
   construction on the per-frame path. The views point at strings built once at init and owned by the
   `Vehicle` that outlives the run — Vehicle holds indices/pointers, never views built on the fly.
3. **Source naming**: state faults are named from the vehicle's own `name_`
   (`name_ + ".state.q_nb"`), **never a `"v0"` literal** — ADR-0011 forbids assuming one vehicle, and
   `v0` is only what vehicle 0 happens to be called. Wrench faults name the **plugin path** from the
   config (`tests/plugins/nan_wrench`), which is what §15's "the offending plugin named" means and
   stays unambiguous when one plugin is loaded twice.
4. **`sim.health` config carries only the keys this PR implements** (`nan`, `quaternion`), values
   `off | warn | halt`, both defaulting to `halt`. §13's example block also lists `envelope`, `rates`,
   `transport`; those arrive with M2/M3/M4 and until then are unknown keys (§15: unknown keys are
   errors). `docs/architecture.md` is authoritative and is **not** edited.
5. **`HealthMonitor` is the only place that decides warn/halt/off.** `Vehicle` and `Simulation` report
   observations and propagate the returned `HaltReason`; they contain no policy and read no config.
   See the rejected-findings note below on §14's "the scheduler decides whether to halt".
6. **On halt the clock does not advance**, so `halt.frame == sim.clock().frame()`. The wrench check
   runs *before* `set_applied_wrench`, so a NaN never reaches the EOM and the state is still frame
   N-1's. A state/quaternion halt is different: the integrator has already run, so the stored state is
   frame N's — and it is **still renormalized before the halt returns** (see decision 7).
7. **`post_step_renormalize` runs on every frame the integrator ran, halt or not.** ADR-0001 pins
   "renormalized after every integrator step"; skipping it on the halting frame would leave a non-unit
   quaternion in state, which is exactly the invariant the check exists to defend. Order is therefore
   *observe drift → renormalize → return the halt*, which still satisfies §15's "before
   renormalization" for the observation. The observed pre-renormalization drift is published as
   `health.max_q_drift`, so the value is not lost. (On a genuinely non-finite quaternion
   `post_step_renormalize` returns false and leaves `q` alone — there is nothing to normalize to, and
   the halt is the point.)

## Files

### New — core

- `core/include/astra/interfaces/wrench_source.hpp` + `core/src/interfaces/wrench_source.cpp`
  (out-of-line dtor only), per decision 1.
- `core/include/astra/kernel/health_monitor.hpp` + `core/src/kernel/health_monitor.cpp`:

  ```cpp
  enum class HealthCheck : std::uint8_t { kNone, kNonFiniteState, kNonFiniteWrench, kQuaternionNorm };
  enum class HealthAction : std::uint8_t { kOff, kWarn, kHalt };

  struct HaltReason {                 // trivially copyable; no allocation, no std::string
    HealthCheck check = HealthCheck::kNone;
    std::string_view source;          // "v0.state.q_nb" or "tests/plugins/nan_wrench"
    std::uint64_t frame = 0;
    std::uint32_t vehicle_index = 0;
    [[nodiscard]] explicit operator bool() const noexcept { return check != HealthCheck::kNone; }
  };

  class HealthMonitor {
   public:
    void init(const Config& config, SignalRegistry& registry);   // init path: may allocate/throw
    [[nodiscard]] HaltReason check_wrench(const Wrench&, std::string_view source,
                                          const StepContext&, std::uint32_t vehicle) noexcept;
    [[nodiscard]] HaltReason check_state(const StateVector&, const StateNames&,
                                         const StepContext&, std::uint32_t vehicle) noexcept;
   private:
    HealthAction nan_action_ = HealthAction::kHalt;
    HealthAction quaternion_action_ = HealthAction::kHalt;
    double nan_count_ = 0.0;          // registered as "health.nan"        (§15: each check is a signal)
    double quaternion_count_ = 0.0;   // registered as "health.quaternion"
    double max_quaternion_drift_ = 0.0;
  };
  ```

  `check_state` tests each ADR-0003 segment with `allFinite()` (naming the first non-finite one) and
  then `std::abs(q.norm() - 1.0) > kQuaternionNormTolerance` with `kQuaternionNormTolerance = 1e-6`.
  **Every detection whose action is not `kOff` increments its counter — `kHalt` as well as `kWarn`** —
  and only then is the halt returned; §15 requires each check to be a registered signal that lands in
  the log, and a halt that returned before counting would leave the signal reading zero for the event
  that stopped the run.

### Modified — core

- `core/include/astra/kernel/vehicle.hpp` / `core/src/kernel/vehicle.cpp`
  - `init()` builds `std::array<std::string, 4> state_names_` from this vehicle's own `name_`
    (`name_ + ".state.p_n"`, … — for vehicle 0 that yields `v0.state.q_nb`, but the literal never
    appears in `core/`) and, for each resolved `WrenchSource*`, stores its plugin path alongside it
    (`struct WrenchEntry { WrenchSource* model; std::string plugin_path; };` in a `std::vector`
    reserved to its final size).
  - `integrate()` becomes out-of-line and returns the halt:
    ```cpp
    [[nodiscard]] HaltReason integrate(const Integrator&, const StepContext&, HealthMonitor&) noexcept;
    ```
    Body, in order:
    1. accumulate `Wrench total` over `wrench_sources_`, health-checking each contribution **before**
       summing it; a halt here returns immediately with `state_` untouched, so the EOM never sees a
       NaN and the stored state is still frame N-1's.
    2. `eom_->set_applied_wrench(total)`; `eom_->begin_frame()`; `integrator.step(*eom_, ctx.t, state_)`.
    3. `health.check_state(...)` — the pre-renormalization observation `integrator.hpp:9-12` was
       written to allow.
    4. `post_step_renormalize(state_)` **unconditionally**, whether or not step 3 halted (decision 7:
       ADR-0001's "renormalized after every integrator step" is not suspended by a halt). Its `false`
       return is reported as `kNonFiniteState` naming the `q_nb` entry of `state_names_` when
       `check_state` did not already halt (it will not have, only if that check is configured `off`).
    5. return step 3's halt, if any.

    With no `Eom` (physics off) the wrench and state checks still run; only steps 2 and 4 are skipped.
- `core/include/astra/kernel/simulation.hpp` / `core/src/kernel/simulation.cpp`
  - `[[nodiscard]] HaltReason step() noexcept;` and `[[nodiscard]] HaltReason run(std::uint64_t) noexcept;`
    (`run()` likewise). `run` returns the first halt and stops; `step` returns before `clock_.advance()`.
    With more than one vehicle (ADR-0011: possible, just not in v1) `step` returns on the first halting
    vehicle and the rest do not step that frame — a halt stops the run, so a partial frame is correct.
  - New member `std::unique_ptr<HealthMonitor> health_`, **not** a by-value member: `from_config`
    returns by value and the monitor registers `&nan_count_` etc. into the `SignalRegistry`, so its
    address must survive the move. `health_->init(config, registry_)` runs before `registry_.freeze()`.
  - Accessors `const HealthMonitor& health() const noexcept` and a non-const
    `std::span<Vehicle> vehicles() noexcept` overload (the quaternion acceptance test injects
    `q *= 1.1` through it; `Vehicle::state()` already has a non-const overload).
- `core/CMakeLists.txt` — add `src/interfaces/wrench_source.cpp`, `src/kernel/health_monitor.cpp`
  (the source list is explicit).

### New — tests

- `tests/plugins/nan_wrench/{CMakeLists.txt,nan_wrench.cpp}` — a `WrenchSource` publishing
  `nan_wrench.force_x`; config keys `force_x` (default 0, a finite constant force applied every frame)
  and `nan_frame` (default absent = never). From `nan_frame` on it returns a NaN `force_b(0)`. Defaults
  must keep it loadable with an empty config, because `load_all_test` constructs every registered
  plugin. No `NO_LOAD_ALL` (it is a valid plugin, not one meant to be rejected).
- `tests/unit/health/health_monitor_test.cpp` (§15 names `tests/unit/health/`), added to the existing
  `astra_unit_tests` source list so no new target is needed: NaN/Inf force and moment; non-finite in
  each of the four state segments with the expected source name; `|q| = 1 + 2e-6` halts,
  `1 + 5e-7` does not; a **halting** detection increments its counter (the finding behind decision in
  the `HealthMonitor` bullet above); `warn` counts without halting; `off` does neither; a vehicle named
  something other than `v0` produces source names carrying that name; `HaltReason{}` is falsey.
- `tests/unit/kernel/simulation_halt_test.cpp` — the two end-to-end acceptance cases, using new configs
  `tests/configs/health_nan_wrench.yaml` (dt 1e-3, 1000 frames, `vehicle.models: [{plugin:
  tests/plugins/nan_wrench, nan_frame: 500}]`, the three `physical` slots) and reusing
  `physics_ballistic_level.yaml` for the quaternion case:
  - `sim.run(1000)` → truthy halt, `check == kNonFiniteWrench`, `source == "tests/plugins/nan_wrench"`,
    `frame == 500`, and `sim.clock().frame() == 500`.
  - scale `q_nb` by 1.1, `sim.step()` → halt, `check == kQuaternionNorm`, `source == "v0.state.q_nb"`
    exactly, and **`|q_nb| == 1` within 1e-12 afterwards** — the halting frame still renormalized
    (decision 7), so ADR-0001's invariant holds on the state the halt leaves behind.
  - a config with `sim.health: { nan: warn }` runs all 1000 frames and leaves `health.nan` at 500;
    the default (no `sim.health` block at all) halts, proving the default is `halt` and that an absent
    block is not an error (`docs/lessons.md`: a bare `YAML::Node{}` is Null-typed but `IsDefined()`,
    so the absent-key path is read through `Config::has` / the defaulted getters, never a raw node).
- `tests/support/alloc_counter.{hpp,cpp}` — the **full** replacement set of global `operator new` /
  `operator delete` (plain, `nothrow`, array, sized, and `align_val_t` forms; every allocating form
  bumps a counter and calls `std::malloc`, every freeing form calls `std::free`). The whole set must be
  replaced together or ASan reports `alloc-dealloc-mismatch` when its own `operator new` pairs with our
  `operator delete`. ASan's definitions are weak, so the executable's win while `malloc` still goes
  through ASan's interceptor — checking is preserved. Linked into `astra_physics_tests` **only**, so the
  unit suite is unaffected.
- `tests/physics/no_alloc_test.cpp` (§15 names `tests/physics/test_no_alloc`) + `tests/configs/no_alloc.yaml`
  (a nan_wrench source with a finite `force_x` and no `nan_frame`, so the wrench, EOM, integrator and
  health stages all run): sample the counter, `sim.run(1000)`, assert the delta is exactly zero and the
  run did not halt. **No warm-up** — a frame-0 allocation is a real finding to fix, not to paper over.
  Joins the existing `astra_physics_tests` target (adding a new executable would also require adding it
  to `astra_finalize_plugins()`'s explicit `foreach(test_target …)` list in `cmake/AstraPlugin.cmake`).

### Modified — tests

`tests/physics/{ballistic,precession,torque_free}_test.cpp` and `tests/unit/kernel/simulation_test.cpp`
call `sim.run(...)`; `[[nodiscard]]` makes each an `-Werror` failure. Each becomes
`ASSERT_FALSE(sim.run(n))` — a strengthening, not a weakening: those oracles now also assert no health
check fired. `tests/unit/CMakeLists.txt` and `tests/physics/CMakeLists.txt` gain the new sources.

## Codex plan-review round (workflow.md §Review — the health monitor is ADR-unpinned)

One `/codex:rescue` round ran against the draft above. Five findings; three folded in, two rejected.

**Accepted**

1. *A quaternion-norm halt before `post_step_renormalize` leaves a non-unit quaternion in state,
   contradicting ADR-0001/0004 and the plan's own "last good state" claim.* Correct and the sharpest
   finding. Fixed by decision 7: observe, then renormalize unconditionally, then return the halt. The
   plan's "state untouched" wording was only ever true of the wrench halt and is now stated per case.
2. *Counters increment only on `warn`, so a `halt` returns before its registered signal records the
   event.* Correct against §15 ("each a registered signal so it lands in the log"). Every non-`off`
   detection now counts before deciding.
3. *`state_names_` spelled as `v0.…` literals is wrong for any vehicle whose index is not 0.* The draft
   did say the names are built from `name_`, but showed them as literals; ADR-0011 makes that worth
   being unambiguous about. Decision 3 now says it outright.

**Rejected**

4. *"§14 says the scheduler decides whether to halt; this plan puts the decision in
   Vehicle/HealthMonitor."* §14's clause is about **model-reported step-time errors** — "reported
   through status flags in the signal registry … the scheduler decides whether to halt based on
   config" — a mechanism that does not exist yet (no model has a status flag until M3). §15 gives the
   HealthMonitor "a configured action per check", which is what this PR implements. Routing state and
   wrench checks through `Scheduler` would mean teaching a `(Model*, divisor)` list about
   `StateVector`, quaternion norms and wrenches, which is worse, not more compliant. What the finding
   is right about is that the policy must live in exactly one place — decision 5 now says so, and
   `Vehicle`/`Simulation` read no config and apply no action.
5. *"The test plan does not require asserting frame 500 or exactly `v0.state.q_nb`."* Misreading — the
   test section already enumerates `frame == 500`, `source == "tests/plugins/nan_wrench"` and
   `source == "v0.state.q_nb"` as assertions. No change.

Codex did not answer the allocation-counter question (whether a full user replacement of global
`operator new`/`delete` behaves under `-fsanitize=address,undefined -fno-sanitize-recover=all` in both
the shared and static-plugins presets, and whether the count is genuinely zero on frame 0). That stays
the one unproven part of this plan, and the `no_alloc` test is what settles it empirically; if frame 0
does allocate, the fix is to remove the allocation, not to add a warm-up.

## Verification

Single tests while iterating (from the repo root):

```
ctest --preset asan -R 'health|halt|no_alloc' --output-on-failure
```

The gate, once, after the work — output to a file, tail only:

```
just check > /tmp/claude-1000/.../check.log 2>&1; tail -40 /tmp/claude-1000/.../check.log
```

Done means the log's final line reads `check: OK` (every preset configured, built with `-Werror`, and
ran ctest; then clang-tidy, clang-format, hook cases, ruff, pyright, pytest). The three PLAN.md M1 PR 4
acceptance items map to the three named ctest cases above.

## After the code

Per `CLAUDE.md`: Codex review from Bash (`codex-companion.mjs review --base origin/main --wait`) and,
because this touches `core/kernel/`, `adversarial-review --base origin/main --wait` with focus naming
ADR-0001 (quaternion norm/renormalization), ADR-0003 (state layout), ADR-0004 (integrator/renormalize
split), ADR-0010 (frame counter as time) and §14's no-allocation rule. Fix what holds, then `/wrap`.
Physics-touching diff, so Joe reads it.
