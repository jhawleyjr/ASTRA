# M7 PR 7: the aero-only channel route, Q5's independent aero evaluator (Tier 3)

Status: **approved by Joe, 2026-10-11, in plan mode**, with his amendments 1 to 6 folded in (below).
Owner: Joe. Builder: Claude, one builder. Tier: **3** (the test tree, plus one core helper move, D1).
Base: `origin/main` = `acdc6b5` (#115). It was read with `git ls-remote origin main` in plan mode and confirmed by
`git fetch` after approval. The worktree `m7-pr7` (branch `worktree-m7-pr7`) is at `acdc6b5`.
Name: the plan-mode draft (main checkout, untracked) was moved (`mv`, before its first add) to
`docs/plans/m7-pr7-aero-evaluator.md`. Nothing is left in the main checkout.
Binding: ADR-0013, 0016, 0020, 0021, 0032, 0033 (all accepted).

**The design is pinned** and this plan does not redesign it. The sources:
- the M7 plan's Q5 under (c), with its 2026-10-08 amendment and amendment 6 (`m7-f16-nesc-oracle.md:459-489`);
- PR 7's row (`:851`) and its Obligations row (`:826`);
- STATE.md's PR 7 thread.

The precedent is M2 PR 3c's R12 block (`tests/physics/nesc/nesc_cases_test.cpp:138-233`). There is no Codex plan
critique: the design is pinned (Joe).

## Joe's amendments at approval (2026-10-11)

1. **D1, the helper's home.** `earth_rate_b` stays defined in `core/src/kernel/vehicle.cpp`. It moves out of the
   anonymous namespace into namespace `astra` and is declared in `core/include/astra/kernel/vehicle.hpp`, with
   its comment moved verbatim to the declaration. `interfaces/earth_model.hpp` and `earth_model.cpp` are not
   touched.
   - Why: the interfaces headers are the plugin-facing surface, and ADR-0032 has the core supply `omega_ie_b`. A
     declaration there would invite a plugin to compute its own rate.
   - The same translation unit keeps the call site's codegen as it is, and the move is a two-file diff. The call
     site's text still does not change.
2. **D4, the exact names.** For each name in the instance's `InputRegistry::names()`, the evaluator copies the
   sim's latched `"v0." + name`, then latches. The names are `controls.elevator`, `controls.aileron`,
   `controls.rudder` and `controls.throttle`. The fixtures' sinusoids go through `Simulation::write_input` with
   the same relative names.
3. **D5 is accepted:** normwise relative 1e-12, force and moment separately. The PR body reports the observed
   maximum per fixture and per test. If any sample's miss is not exactly 0, the body names the sample and the
   cause before Joe reads it. That is a report, not a change to the criterion.
4. **Accepted:** test 3's zero-rate and zero-altitude additions, test 4, M1's corrected wording, and M2's
   `CMakeLists.txt` line. Test 3 asserts the split: a NaN `omega_ie_b` gives a non-finite aero wrench; a NaN
   `altitude_m` gives a finite aero wrench and a non-finite engine wrench.
5. **Commit 2's validation** stands, with one addition. The PR body states that B4 and B5 are the bitwise +0.0
   evidence, and names the asan ctest count for that run.
6. **Stop conditions.** Any of these stops the work and goes to Joe:
   - a miss in test 1 or 2;
   - a halt in either fixture that the amplitude or the length cannot fix;
   - any need to touch `tests/plugins`.

## Context

NESC's `aero_body*` columns are aero-only and about the CM. `v0.wrench_total.*` is aero plus engine. PR 9, 11a
and 11b need an aero-only, frame-start-stage wrench (M7 plan, "The aero channels (Q5)", `:300`). Under (c), the
harness evaluates a second instance of the aero plugin and calls `wrench_b(pre_step_state, env)`.

PR 3b and PR 3c left one thing for this PR. The latched `Vehicle::environment()` keeps `omega_ie_b` and
`altitude_m` NaN (`environment_sample.hpp:83-91, 108-114`). So the evaluator must set both from the sim's Earth
model at the frame-start state. Otherwise the F-16 aero (`air_data::omega_eb_b`) and the engine (`altitude_m`)
return NaN.

## Repository findings (read at `acdc6b5`)

- **`earth_rate_b`** is file-local in `core/src/kernel/vehicle.cpp:28-33`. It computes `c_nb * earth.omega_ie_n(vs)`
  and then `+= 0.0`, and it has one caller (`:358`). No other code or living doc names it. Only the historical
  plans do.
- **Neither `Simulation` nor `Vehicle` exposes the Earth model.** `Simulation::earth_` is private
  (`simulation.hpp:300`), and so is `Vehicle::rigid_body_`.
- **Every shipped Earth plugin is stateless.** Each has a no-op `step()`, and `init` reads only its own node:
  `flat.cpp:20-22`, `round.cpp:79-87`, `wgs84_rotating.cpp:55-57`.
- **The latched sample is the k1 stage's, except for those two fields.**
  - `sample_environment` takes `wind_n` and `atmosphere` at the frame-start state through `earth_->altitude(vs)`
    (`simulation.cpp:717-746`).
  - Every stage copies `wind_n` (`vehicle.cpp:462`).
  - k1 re-samples the atmosphere at `earth.altitude` of that same state (`:358-379`). The model, state and call
    are the same, so the values are the same.
  - After `run(1)`, `environment()` is still that frame's sample (`simulation.cpp:751-752`, the R12 argument).
- **The F-16 aero reads** `omega_eb_b`, the wind (through `relative_velocity_b`) and the density. It does not
  read `altitude_m` (`aircraft/f16/physical/aero/aero.cpp:52-77`).
- **The F-16 engine reads `altitude_m`** (`engine.cpp:64`) and `controls.throttle`. Neither plugin owns state or
  overrides `resolve_peers`, which is Q5's condition and PR 6a's finding.
- **Controls:** the `v0.controls.*` signal points at the input's latched (canonical) value
  (`input_registry.cpp:38`). `InputRegistry::names()` lists an instance's own inputs.
- **The wrench sum:** `accumulate_wrench` adds the sources in `vehicle.models` order, starting from zero
  (`vehicle.cpp:418-430`).
- **The bump rule:** `kCoreApiVersion` bumps when the plugin-facing ABI changes (`core_api.hpp.in:8`). Adding an
  exported free function does not change it.

## Decisions this plan makes

**D1. Share `earth_rate_b`: yes, as its own commit with no behavior change.**
- **The move** (as amended, amendment 1). The definition stays in `vehicle.cpp`, moved out of the anonymous
  namespace into namespace `astra`, as `astra::earth_rate_b(const EarthModel&, const VehicleState&) noexcept`.
  It is declared in `core/include/astra/kernel/vehicle.hpp`, with its comment moved verbatim to the declaration.
  This is a two-file diff, and no CMake changes. The name stays, and the text of the call site does not change.
- **Why share.** The evaluator's `omega_ie_b` is then the stage's by construction, with the +0.0 included. A
  test-local copy that dropped the +0.0 would still pass a 1e-12 relative check, because a sign of zero is
  invisible there. So the self-check cannot guard a copy's agreement with ADR-0032; only sharing guarantees it.
- **No behavior change.** The source, the translation unit and the compile flags are the same (no `-ffast-math`,
  no `-march`).
  `kCoreApiVersion` stays 11, because no vtable and no struct across the loader boundary changes.
- **Not taken:** a test-local copy, which would need no core change.

**D2. "The sim's Earth model" is a second instance of the config's `physical.earth` plugin, loaded from the same
node.** This is the R12 pattern applied to the Earth model. Given the stateless plugins above, it computes the
sim's values at any state. A `Simulation::earth()` accessor is not added: that would be a second core change,
outside the brief.

**D3. The evaluator's environment.** It starts from the latched sample (`wind_n` and `atmosphere`, as found
above). It then sets `omega_ie_b = earth_rate_b(earth, vs)` and `altitude_m = earth.altitude(vs)` at the
pre-step state. Q5's sub-condition holds by construction: the rate comes from the sample's state and environment,
never from a peer binding.

**D4. The evaluator's inputs.** Each instance has its own `SignalRegistry` and `InputRegistry`.
- Copying the inputs and evaluating are separate calls. For each name in the instance's `InputRegistry::names()`
  (`controls.elevator`, `controls.aileron`, `controls.rudder` and `controls.throttle`), the copy writes the sim's
  latched `"v0." + name`, then calls `latch()` (amendment 2).
- It throws on a missing signal (at construction) or on a non-finite latch, naming the input.
- It refuses an instance whose `state_count()` is not zero, because it cannot reproduce own state (C-07). No
  fixture exercises that refusal: no state-owning `WrenchSource` exists, and `tests/plugins` is out of scope. It
  is disclosed.

**D5. "1e-12 relative" means normwise, per sample.**
- The criterion, for the force and for the moment separately: ‖x_eval − x_total‖∞ ≤ 1e-12 · ‖x_total‖∞. Any
  non-finite value fails.
- A component that crosses zero then needs no absolute floor (R11's problem in PR 11a), and no new number enters.
- I expect exactly 0, because the functions, inputs and summation order are the same. The PR body reports the
  observed maximum per fixture and per test. If any sample's miss is not exactly 0, the body names the sample and
  the cause (amendment 3).

**D6. `nesc_cases_test.cpp` stays unchanged.** R12 checks the core's k1 publication on `nesc_aero`, which reads
neither field (ADR-0032, "Not readers"). Moving R12 onto the evaluator would change M2's harness for no gain. PR 9
reuses the new header instead.

## Mismatches with the brief (for Joe)

- **M1. The negative control's wording.** "NaN aero wrench" is true for `omega_ie_b`. With `altitude_m` left NaN,
  the F-16 aero stays finite, because it does not read the field. The engine's wrench is NaN instead, so the sum
  still fails. The test asserts exactly that split.
- **M2. One file outside `tests/physics/nesc`.** The new test source has to be listed in
  `tests/physics/CMakeLists.txt`: `astra_physics_tests`' source list and its header comment.

## Mapping and sequence

| Commit | Content | Paths |
|---|---|---|
| 1 | This plan, Status "approved by Joe, <date>" on his word; push; draft PR | `docs/plans/m7-pr7-aero-evaluator.md` |
| 2 | D1's move only (amendment 1) | `core/include/astra/kernel/vehicle.hpp`, `core/src/kernel/vehicle.cpp` |
| 3 | The evaluator and the self-check | `tests/physics/nesc/independent_evaluator.hpp` (header-only: `FrameStartEnvironment`, D2 and D3; `IndependentWrenchSource`, D4; the D5 comparison), `tests/physics/nesc/f16_evaluator_test.cpp`, `tests/physics/CMakeLists.txt` |
| 4 | Gate evidence | `docs/reviews/evidence/<head>-gate-summary.log` |

Then the Codex adversarial review (`/review-cycle`, at most 2 correction rounds), then `/human-review`. Joe reads
the evaluator's environment set-up, the self-check and commit 2.

### The self-check, pre-registered

**Fixtures.** Two inline YAML configs, in the pattern of `F16NoAlloc` (`aircraft/f16/tests/no_alloc_test.cpp:142-160`).
Both list the F-16 aero, then the engine, then the mass and `isa1976`.
- **Flat:** `earth/flat` and `gravity/constant`, with p_n = (0, 0, −3000) m.
- **Rotating:** `earth/wgs84_rotating` and `gravity/wgs84_j2`, the consistent pair PR 9 uses, with
  p_n = (0.6 rad, −2.0 rad, 3000 m). It adds `wind/uniform` (north 3 m/s, east 6 m/s, case 07's form), so the
  latched wind is exercised too.
- **Both:** v_b about (150, 3, 8) m/s, and a nonzero heading, pitch, roll and body rate. So every component of the
  Earth rate is nonzero on `wgs84_rotating`.
- **Controls.** Before every frame, `Simulation::write_input` sets all four controls, under D4's relative names,
  to sinusoids of distinct frequencies, so no two are proportional and every frame's controls differ from the
  last. The elevator, aileron and rudder stay within a few degrees, and the throttle within 0.4 to 0.8.
- **Run.** 5,000 frames, and every frame is a sample. The R12 order applies: copy the pre-step state, `run(1)`,
  evaluate the aero and the engine instances, and compare their sum with `v0.wrench_total.*`. The run must
  complete with no halt.

**Tests** (`F16Evaluator.*`, in `astra_physics_tests`, label `physics`):
1. `AeroPlusEngineEqualsWrenchTotalOnAFlatEarth`: D5 holds at every sample.
2. `AeroPlusEngineEqualsWrenchTotalOnWgs84Rotating`: D5 holds at every sample.
3. `AnUnsetEarthRateOrAltitudeFailsTheCheck`: the negative control, on the rotating fixture.
   - The premise: the latched `omega_ie_b` and `altitude_m` are NaN.
   - The split, asserted (M1, amendment 4): with `omega_ie_b` left at the latched NaN, the aero wrench is
     non-finite and the D5 comparison fails. With `altitude_m` left at the latched NaN, the aero wrench is
     finite, the engine wrench non-finite, and the comparison fails.
   - **Added, not in the brief:** a zero `omega_ie_b` (a silent non-rotating Earth) and a zero `altitude_m`
     (silent sea level) each fail too, with the measured miss reported. This shows the check resolves the
     values themselves, not only a NaN.
4. `StaleControlsFailTheCheck` (**added, not in the brief**). Evaluating a sample before copying its controls
   (that is, with the previous frame's) misses by more than 1e-12. This shows that the changing controls give the
   check teeth.

A miss of the D5 criterion in test 1 or 2 stops the work and goes to Joe (amendment 6). It never becomes a wider
number.

## Validation

- **Commit 2:**
  - `ctest --preset asan -R 'SimulationEarthRate\|NoAlloc\|Vehicle' --no-tests=error`. This covers B1 to B6 and
    NoAlloc F with their files untouched. The PR body states that B4 and B5 are the bitwise +0.0 evidence and names
    this run's asan ctest count (amendment 5).
  - `kCoreApiVersion` is still 11.
  - The diff is a verbatim move.
- **Commit 3:**
  - `ctest --preset release -R 'Nesc\|F16Evaluator' --no-tests=error`, with the `-N` count first, split by
    executable. The prediction is 26: 18 in `astra_physics_tests` (Nesc 10, NescF16Band 4, F16Evaluator 4) and
    NescAero's 8 in the unit executable.
  - `ctest --preset asan -R F16 --no-tests=error`. F16Aero, F16Engine, F16Mass and F16NoAlloc must be unchanged.
- **Final candidate:** `./scripts/check-full.sh`, which must exit 0 and print `check: OK`. The blast radius is
  pre-registered, and every item must be unchanged:
  - M2's cases 1 to 10;
  - M3's regression and the R1 golden;
  - M4's logs and M6's fixtures.

  Then `git diff --check origin/main..HEAD`.
- **Where the results go:** commands, exit codes, counts, the D5 maxima, the negative-control misses, and the
  Codex verdict and thread go in the PR body.

## Risks and rollback

- **The untrimmed F-16 could leave a domain within 5 s.** The run-completes assertion catches that. The fix would
  be the fixture's amplitude or length, never the criterion. A halt that neither fixes, or any need to touch
  `tests/plugins`, stops the work and goes to Joe (amendment 6).
- **Rollback:** revert commit 3 alone, or commits 2 and 3 together. Commit 2 stands alone.

## Decided at approval

Joe's amendments 1 to 5 settle the three questions this draft put to him: D1 shares `earth_rate_b`, in
`vehicle.hpp`/`vehicle.cpp`; D5's normwise definition stands; M1's wording and the additions in tests 3 and 4 are
accepted.

## /wrap (before Joe's merge)

- **PLAN.md:** the PR 7 line.
- **STATE.md, as main reads it after the merge:** PR 9 is eligible, PR 13 waits on PR 9, PR 3 waits on Joe's go,
  and the PR 7 thread closes.
- **Before the merge:** `git fetch`. If PR 14 has merged:
  - merge `origin/main` in;
  - rewrite STATE.md and PLAN.md for both merges;
  - rerun what the tier requires for what the merge touched: `docs-check` and `git diff --check` for Markdown
    only, the gate otherwise.
