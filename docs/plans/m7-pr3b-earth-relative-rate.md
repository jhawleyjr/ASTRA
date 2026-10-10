# M7 PR 3b: ADR-0032 in the core

Status: **approved by Joe, 2026-10-09, in plan mode**, with his amendments 1 to 10 and his second review's
three corrections folded in (below).
Owner: Joe. Builder: Claude (one builder). Tier: **3** (`core/kernel/`, `aircraft/**/physical`, the plugin ABI).
Base: `origin/main` = `56b17bd` (#109, PR 1b), read with `git ls-remote` in plan mode and confirmed by `git fetch`.
After approval: `git fetch`, then worktree `m7-pr3b` (branch `worktree-m7-pr3b`) from `origin/main`, and this
draft is moved (`mv`) into the worktree as `docs/plans/m7-pr3b-earth-relative-rate.md` and committed
(`docs/agents/claude-code.md`). Nothing stays untracked in the main checkout.
Binding: ADR-0032 (accepted 2026-10-09), ADR-0001, 0003, 0016, 0017, 0020, 0021; architecture §7.
Sources: the M7 plan's row 3b; the addendum's D3 and §10 item 3; `m7-pr1b-adrs.md` J3 and amendments 5, 6, 13.

## Joe's review of the first draft (2026-10-09)

Approved in principle, with amendments 1 to 10, each folded in where it applies. Design 4's `+= 0.0`
canonicalization is accepted (amendment 1). The other amendments cover: the mutation checks (2), both of
B6's early returns (3), B1 and B2's homogeneous DCM (4), D1 moving out of the template (5), the stale
sentence in the notional transport's aero (6), the atmosphere field's comment (7), the ctest alternative
counts (8), every M6 batch fixture (9), and the identity log's path and the venv rebuild (10).

His second review (2026-10-09) made three corrections, now folded in:
1. mutation (e)'s prediction now includes D1 at W2, with W3 as "may fail";
2. a rule for a failure outside a row's predicted set;
3. B6 (i)'s identity attitude, and B6's bitwise +0.0 assertions.

## Context

The NESC members fed their F-16 aero the body rate relative to the Earth (addendum D3: Earth-relative rates
predict SIM 5's lateral aero to 3e-8; inertial rates miss by factors of 4 to 16). ADR-0032 has the core supply
it: a per-stage field `EnvironmentSample::omega_ie_b` (NaN by default) and an accessor
`air_data::omega_eb_b(vs, env)`. This PR builds that, bumps `kCoreApiVersion`, rejects a stale plugin, and
migrates the template's aero, whose `earth/flat` values must stay identical. PR 6b (the F-16 aero) depends on
it. The notional transport, `nesc_aero` and every other plugin are unchanged. Nothing changes under
`tests/golden/`, in any tolerance file or under `docs/decisions/`; if anything there seems to need a change,
the work stops and goes to Joe.

## Repository findings (read at `56b17bd`)

- **The struct.** `core/include/astra/kernel/environment_sample.hpp`: `wind_n` (zero default) and `atmosphere`
  (all-NaN `AtmoState`).
  - Its header comment (L1-3) says the sample is "held constant through that vehicle's wrench and EOM
    stages".
  - `atmosphere`'s own comment (L30-37) calls it "the same per-vehicle, frame-start latch wind_n already
    uses".
  - Both have been untrue since M2 PR 3c (ADR-0016).
  - `wrench_source.hpp:54-59` repeats the per-field account, and `model.hpp:58` calls `integrate_state`'s
    argument "the frame's EnvironmentSample".
- **The stage path.** `Vehicle::evaluate_derivative_stage` (`vehicle.cpp:418`):
  - copies the latched sample (`:426`);
  - calls `prepare_stage_environment` (`:430`);
  - then calls `accumulate_wrench`, and through it every `wrench_b` (`:387`), unless the stage is skipped;
  - then calls `integrate_state` for every state owner (`:456-458`), **whether or not the stage was
    skipped**.

  `prepare_stage_environment` (`:314`) returns early twice: on an altitude outside the atmosphere's
  `valid_range()` with the domain check on (`:346-348`), and on a non-finite wind or atmosphere (`:375`).
- **Both paths reach it.**
  - Step: `Simulation::step_frame` → `Vehicle::integrate` → `integrator_->step(lambda)` → lambda (`:493`)
    → `evaluate_derivative_stage`.
  - Probe: `Simulation::derivative` (`simulation.cpp:840`) → `Vehicle::derivative` (`vehicle.cpp:541`) →
    `evaluate_derivative_stage` (`:574`).
  - Python's `Simulation.derivative` and `astra.trim` call `Simulation::derivative` (`_core.cpp:174`).
- **The EOM** (`rigid_body_6dof_dynamics.cpp:25-33`): `c_nb = frames::dcm_from_quaternion(vs.q_nb())`,
  `omega_ie_b = c_nb * earth_.omega_ie_n(vs)`. Unchanged by this PR.
- **`dcm_from_quaternion`** (`frames.hpp:63-75`) evaluates the homogeneous form (diagonal w²+x²−y²−z²), so for
  a non-unit stage quaternion its C_nb is |q|² times a rotation.
- **The Earth models.** `earth/flat` and non-rotating `earth/round` return `Eigen::Vector3d::Zero()`;
  `wgs84_rotating` and `round` with `rotating: true` return `kOmegaIe * (cos lat, 0, -sin lat)`.
- **The latched sample** (`Vehicle::environment()`) is set by `Simulation::sample_environment`
  (`simulation.cpp:717`). Its readers:
  - `DerivedSignals::pack` and the exchange stage, both through `icd_pack::pack_sim_to_sw`
    (`icd_pack.hpp:74-125`), which reads `wind_n`, `atmosphere` and the state's inertial `vs.omega_b()` for
    p, q, r;
  - `nesc_cases_test.cpp:202`: the atmosphere fields, and R12's direct call of `nesc_aero`, which reads no
    rate from the sample;
  - `simulation_transport_test.cpp:571` and the wind tests: wind and atmosphere.

  The health checks read no sample, except `prepare_stage_environment`'s non-finite check on the stage
  copy (`wind_n`, `atmosphere`). **No reader reads a rate from any sample.**
- **Python does not bind `EnvironmentSample`**: `python/bindings/_core.cpp` and `python/astra/_core.pyi`
  have no binding of it, of `Vehicle::environment()` or of any field (grep: only plugin-path strings and the
  `NON_FINITE_ENVIRONMENT` enum value).
- **Every place that builds an `EnvironmentSample` today**, and whether it reaches a reader of the new field:
  - `Simulation::sample_environment`: the latched sample, which reaches readers only through the stage copy,
    and the core sets that copy;
  - `tests/physics/notional_transport/{aero,turbofan}_wrench_test.cpp`,
    `tests/unit/aero/aero_math_namespace_isolation_test.cpp`, `tests/unit/wrench/nesc_aero_test.cpp`,
    `tests/unit/icd/icd_pack_test.cpp` and `tests/unit/util/air_data_test.cpp`: the notional transport, the
    turbofans, `nesc_aero`, the ICD packer and `relative_velocity_b`, none of which reads it.

  **No existing site passes a sample to a reader.** The brief expected the template's tests to be one, but
  `aircraft/_template/tests/aero_test.cpp` checks only `init`'s `ConfigError`s. Every new site is listed
  under "Tests", and each sets the field explicitly.
- **The template's aero.**
  - `aircraft/_template/physical/aero/aero.cpp:96` reads `in.omega_b = vs.omega_b()`.
  - Its `aero_math.hpp` is rate-frame agnostic, and `finite()` (`:86-91`) checks `in.omega_b.allFinite()`,
    so a NaN rate returns `non_finite_wrench()`.
  - Its configs use `earth/flat`. `cruise.yaml` is zeroed and refuses to run.
  - `tests/python/test_plot_new_aircraft.py` fills a copy (`gliderx`) with the notional transport's numbers
    and flies it 1000 frames at 10 km, ψ = 0.6 rad.
- **A sentence this PR makes stale.** `aircraft/notional_transport/physical/aero/aero.cpp:4-6` says that
  file "is identical between aircraft/notional_transport and aircraft/_template (only the class name
  differs …)". After the migration it is not. Per the brief, nothing under `aircraft/notional_transport/` is
  edited; at `/wrap`, PLAN.md's B2 item records the sentence as stale until B2.
- **The API version** is 9 (`core_api.hpp.in`).
  - `tests/python/test_package.py:32` keeps a hand-kept twin of it (lesson 2026-09-25).
  - `tests/python/test_report.py:487`'s `np.uint64(9)` is a synthetic plugin-table cell in an escaping test,
    not a pin, and stays.
  - `tests/plugins/bad_version` uses `kCoreApiVersion + 1`.
- **The run log** records `git_describe` and `core_api_version` at the root. In `/plugins` it records each
  plugin's `core_api_version` and the SHA-256 of its loaded library (`run_log.cpp:110-135, 391-396`).
  `git_describe` is baked at build time by `cmake/astra_build_info.cmake` (`--always --dirty`).
- **isa1976's valid range** is 0 to 86000 m geometric (`isa1976.cpp:157-161`). `sim.health.domain` defaults
  to `halt`, so the domain check is on unless it is set to `off` (`health_monitor.cpp:26-40`,
  `health_monitor.hpp:227`).
- **The no-alloc fixtures** run 1000 frames on `earth/flat` (five) and `wgs84_rotating` (`no_alloc_domain.yaml`).
- **Build and rounding.**
  - There is no `-march` and no `-ffast-math` (`cmake/AstraBuildFlags.cmake`), so the build emits no FMA and
    folds no signed zero.
  - Nothing in the tree changes the rounding mode: no `fesetround` and no `<fenv.h>` under `core`,
    `plugins`, `aircraft`, `tests`, `embedded` or the bindings.

## Design

1. **The field** (`environment_sample.hpp`), after `atmosphere`:
   `Eigen::Vector3d omega_ie_b = Eigen::Vector3d::Constant(std::numeric_limits<double>::quiet_NaN());`
   Its comment says:
   - It is the Earth's rotation rate relative to inertial, in body axes, rad/s, set per RK4 stage by the
     core.
   - It equals `c_nb * earth.omega_ie_n(vs_stage)` in value, and it is exactly +0.0 wherever that product
     is a zero. That is why `air_data::omega_eb_b` returns `vs.omega_b()` bit for bit on a flat or
     non-rotating Earth, which is the template's identity by construction (amendment 1).
   - Readers use `air_data::omega_eb_b`.
   - It is NaN by default (ADR-0017's convention) and stays NaN in the latched sample.
   - `HealthMonitor` does not check it: it is the core's own value (ADR-0032 Consequences).
2. **The accessor** (`air_data.hpp`):
   `[[nodiscard]] inline Eigen::Vector3d omega_eb_b(const VehicleState& vs, const EnvironmentSample& env)
   noexcept { return vs.omega_b() - env.omega_ie_b; }`
   Its comment says:
   - the result is in body axes, rad/s;
   - `vs.omega_b()` is inertial (the state's own rate), and `env.omega_ie_b` is in the same body axes, so
     nothing is rotated;
   - the rate is Earth-relative, not air-relative (ADR-0032 Decision 6);
   - the result is NaN when the field was never set.
3. **The per-stage assignment.** `Vehicle::prepare_stage_environment` sets the field as its **first**
   statement after the `rigid_body_` guard, before both early returns. So a stage whose wrenches are skipped
   still hands `integrate_state` a set value. Both paths reach it (findings above). The helper is file-local
   in `vehicle.cpp`:
   ```cpp
   Eigen::Vector3d earth_rate_b(const EarthModel& earth, const VehicleState& vs) noexcept {
     const Eigen::Matrix3d c_nb = frames::dcm_from_quaternion(vs.q_nb());
     Eigen::Vector3d omega_ie_b = c_nb * earth.omega_ie_n(vs);  // the EOM's own expression
     omega_ie_b.array() += 0.0;  // -0.0 -> +0.0, every other value unchanged (design 4)
     return omega_ie_b;
   }
   ```
   It stays file-local; PR 7's plan decides whether its evaluator shares it.
   - **The latched sample keeps the NaN default.**
     - `Simulation::sample_environment` does not set the field, so `Vehicle::environment()` holds all-NaN
       `omega_ie_b`.
     - No reader of that sample reads it (findings). A future one gets a loud NaN, never a frame-start
       value mistaken for a stage's.
     - That matches ADR-0032's Consequences: PR 7's evaluator sets the field itself at the frame-start
       state.
   - **Allocation-free:** the helper uses fixed-size Eigen values on the stack and one virtual call the EOM
     already makes. The six NoAlloc fixtures run it at every stage; a seventh adds a reader on a rotating
     Earth (F).
4. **Signed zeros: the identity holds by construction** (accepted, amendment 1).
   - **The problem.** `c_nb * 0` gives −0.0 in any component whose `c_nb` row is all negative. Row 1 is
     (cθcψ, cθsψ, −sθ), all negative for ψ in the third quadrant with θ > 0. Under round-to-nearest
     (IEEE 754 §6.3), `x − (−0.0)` turns a −0.0 rate into +0.0, while `x − (+0.0)` is `x` for every double,
     −0.0 included.
   - **The fix.** Adding +0.0 turns −0.0 into +0.0 and leaves every other value, NaN included, unchanged.
   - **The chain.** On `earth/flat` and on a non-rotating `earth/round`:
     - the field is exactly +0.0 in every component;
     - therefore `omega_eb_b` returns `vs.omega_b()` bit for bit;
     - therefore the template's `Inputs` are bit-identical to before, and so are its wrench and every logged
       value.
   - The field differs in bits from the EOM's local only where a component is a zero. The EOM is not
     touched.
   - **The planted check (mutation (a)).** If removing `+= 0.0` does not make B4 fail, this prediction is
     wrong for Eigen's product as built here. The work then stops and goes to Joe.
5. **The template's migration** (`aero.cpp` only): `in.omega_b = air_data::omega_eb_b(vs, env);`.
   - **Its comment, for a copied aircraft to read:**
     - the aero damps the body rate relative to the Earth (ADR-0032), not the state's inertial ω_b;
     - on `earth/flat` the two are equal bit for bit;
     - under a rotating Earth the core supplies the Earth rate, so the aero never carries its own;
     - the notional transport and `nesc_aero` still damp the inertial rate (PLAN.md's B2 item).
   - **The file's header,** "Identical to … notional_transport … except this comment and the class name",
     gains "and the body rate it damps".
   - **`aero_math.hpp` is unchanged.** Its math is rate-frame agnostic, and its header's "identical between
     the two aircraft" stays true.
6. **The API bump: 9 → 10.**
   - `core_api.hpp.in` gains entry 10 (M7 PR 3b, ADR-0032). `EnvironmentSample`, which every `wrench_b()`
     and `integrate_state()` receives by reference, gained a field. It crosses the loader boundary (ADR-0016's
     test), so a plugin built against 9 is rejected, not handed a layout it does not know.
   - `test_package.py`'s pin and comment chain move to 10 in the same commit (lesson 2026-09-25).
   - PR 3 bumps too. Whichever lands second bumps again when it merges `main`, and neither reserves a number.
7. **Comments** (comment-only):
   - **EnvironmentSample's header (L1-3)** is rewritten:
     - `wind_n` is frame-latched;
     - `atmosphere` is refreshed per stage (ADR-0016);
     - `omega_ie_b` is set per stage (ADR-0032), and is NaN in the latched sample.
   - **`atmosphere`'s field comment (L30-37)** is corrected (amendment 7):
     - since M2 PR 3c each stage refreshes it at the stage's own altitude;
     - the latched value is only the stage copy's starting point;
     - on a stage skipped for an out-of-range altitude, the latched value is what the stage copy keeps.
   - **Other files:**
     - `wrench_source.hpp:54-59` and `model.hpp:58` name `omega_ie_b` as per stage;
     - `vehicle.hpp`'s comments on `set_environment`, `prepare_stage_environment` and
       `evaluate_derivative_stage` say what each now does with the field.

## Tests (item 7), names and values pre-registered

Each new site that builds a sample for a reader sets `omega_ie_b` explicitly, except D3, whose point is the
default.

**A. `tests/plugins/earth_rate_recorder`** (new, test-only).
- It is a `WrenchSource` that requires no environment and owns one state.
- On each `wrench_b` and each `integrate_state` call, it records into slot k (the k-th call since the last
  `step()`, four slots each):
  - `env.omega_ie_b`;
  - `air_data::omega_eb_b(vs, env)`;
  - the stage's `q_nb` and its full `p_n`.
- Slots start NaN, and it also publishes both call counts. All of these are published as signals.
- It returns a zero wrench and writes `dq[0] = 0`.
- Its mutable state follows `atmosphere_consumer`'s test-instrumentation exception.

**B. `tests/unit/kernel/simulation_earth_rate_test.cpp`** (suite `SimulationEarthRate`, which matches row 3b's
`Simulation`).

The independent computation is typed in the test:
- Ω = 7.292115e-5 rad/s, from WGS-84 (NIMA TR8350.2 Table 3.1), not `geodesy::kOmegaIe`;
- ω_ie_n = Ω(cos φ, 0, −sin φ);
- C_nb from a stated 3-2-1 Euler triple (S&L eq. 1.3-10, typed), or from a recorded stage quaternion.

**From a recorded stage quaternion, the test uses the homogeneous form** (diagonal w²+x²−y²−z²), which is
what `frames::dcm_from_quaternion` evaluates (amendment 4):
- RK4 stage quaternions are not unit length. At B1's rates, |q|²−1 is about 3e-9 at stages 2 and 3 and
  about 1.3e-8 at stage 4: (h·|ω|/2)², with h = dt/2 or dt and |ω|² = 0.0525 rad²/s².
- A form that assumes |q| = 1 would miss by about 1e-8·Ω.
- The test's comment says so, and a miss there is never a reason to change the tolerance.

**Tolerance:** per component, |Δ| ≤ 1e-13·Ω (7.3e-18 rad/s).
- That is about 1000 times the expected roundoff (about 1e-16·Ω).
- A frame or sign error is O(Ω).
- Reading the frame-start state instead of the stage's own is about 1e-4·Ω here.

**The rotating fixture:**
- models: `wgs84_rotating` with `wgs84_j2` (a pair PR 3's consistency check accepts), and `mass/constant`;
- position: lat 0.6 rad, lon −2.0 rad, alt 1000 m;
- attitude: ψ, θ, φ = (2.2, 0.3, −0.4) rad;
- v_b = (120, 5, −3) m/s;
- ω_b = (0.1, −0.05, 0.2) rad/s.

The probe states in B2 and B3 differ from this initial state, so a field computed from the combined buffer
fails them (mutation (b)).

1. `EveryStepStageCarriesCnbTimesOmegaIeNOnWgs84Rotating`: one `step()`.
   - Each of the four `wrench_b` and four `integrate_state` slots matches the independent value at that
     slot's recorded state.
   - Within a stage, both calls carry the same bits.
   - Slot 1's field differs from slot 0's, so the field is refreshed per stage.
   - After the step, `vehicles().front().environment().omega_ie_b` is all NaN.
2. `TheDerivativeProbeCarriesCnbTimesOmegaIeNOnWgs84Rotating`: `derivative()` on a fresh sim at a stated state:
   - position: lat 0.62, lon −1.9, alt 1500 m;
   - attitude: Euler (2.0, −0.2, 0.5);
   - v_b = (100, −4, 6), ω_b = (0.05, 0.08, −0.1);
   - expected: slot 0 of both kinds matches the independent value from the stated Euler triple.
3. `EarthRelativeRateIsTheRateRelativeToTheEarthInBodyAxes` (sign and frame, physically). Probes at v_b = 0,
   with the attitude and position of item 2:
   - (i) **At rest relative to the Earth:** ω_b = C_nb·ω_ie_n (the test's value). The recorded `omega_eb_b`
     is within the tolerance of zero, and the recorded field matches ω_b.
   - (ii) **Rotating relative to the Earth:** ω_b = C_nb·ω_ie_n + ω_rel, with ω_rel = (3e-5, −5e-5, 2e-5)
     rad/s, of the order of Ω so roundoff stays inside the tolerance. The recorded `omega_eb_b` matches
     ω_rel within the tolerance.

   Swapping the accessor's operands passes (i) and fails (ii) by 2|ω_rel|.
4. `IsPositiveZeroOnAFlatEarthOnBothPaths`: `earth/flat`, at ψ, θ, φ = (−2.5, 0.3, 0.0), whose C_nb row 1 is
   all negative; one `step()` and one probe.
   - Every slot's field is bitwise `0x0000000000000000` (+0.0).
   - Without `+= 0.0`, component x is −0.0 and components y and z are +0.0 (mutation (a)).
5. `IsPositiveZeroOnANonRotatingRoundEarthOnBothPaths`: the same with `earth/round` (no `rotating` key) and
   `gravity/inverse_square`.
6. `IntegrateStateSeesItOnStagesWhoseWrenchesAreSkipped`: two cases, one for each early return of
   `prepare_stage_environment` (amendment 3).
   - **(i) The non-finite return.**
     - Setup: `earth/flat` with `tests/plugins/nan_atmosphere`, at the identity attitude. With the identity
       attitude, `c_nb * 0` is +0.0 in every component even without `+= 0.0`, so mutation (a) does not
       reach this case.
     - Result: the step halts `kNonFiniteEnvironment`, and the recorder's `wrench_b` is never called
       (count 0, all slots NaN).
     - Every `integrate_state` slot holds +0.0, never NaN.
   - **(ii) The out-of-range return,** the shape of lesson 2026-09-28.
     - **Setup:**
       - models: `earth/flat`, `gravity/constant`, `atmosphere/isa1976`, with `sim.health.domain: halt`
         stated;
       - attitude: the identity;
       - start: altitude 85999.99 m, a margin of 0.01 m below `valid_range().max_m` = 86000 m;
       - climb: v_b = (0, 0, −100) m/s, a climb rate of 100 m/s.
     - **Predicted altitudes:**
       - stage 1: 85999.99 m, in range;
       - stages 2 and 3: about 86000.04 m (+100 m/s × dt/2), out of range;
       - stage 4: about 86000.09 m, out of range.
     - **Predicted result:**
       - the step halts `kOutOfDomain`, naming `plugins/physical/atmosphere/isa1976`;
       - `wrench_b` is called once (slot 0), and slots 1 to 3 stay NaN;
       - all four `integrate_state` slots hold +0.0.

   B6's +0.0 assertions are bitwise (`0x0000000000000000`), as B4's are.

**C. `tests/unit/util/air_data_test.cpp`** (suite `AirData`, which is outside row 3b's filter, so it also
runs with `-R AirData`):
1. `EarthRelativeBodyRateIsTheStateRateMinusTheEarthRateInBodyAxes`:
   - inputs: ω_b = (0.3, −0.2, 0.1) and field = (1e-4, 2e-4, −3e-4);
   - the result is bitwise `ω_b − field`, component by component;
   - this holds at the identity attitude and at a non-identity one, so nothing is rotated.
2. `EarthRelativeBodyRateKeepsANegativeZeroRateAgainstAPositiveZeroEarthRate`: with ω_b = (−0.0, −0.0, −0.0),
   - a +0.0 field gives −0.0 bits;
   - a −0.0 field gives +0.0 bits, the IEEE behavior design 4 exists for.
3. `EarthRelativeBodyRateIsNaNWhenTheEarthRateWasNeverSet`: a default sample gives all NaN.

**D. The template aero.**

**D1, this PR's migration check** (amendment 5). It lives in `tests/unit/aero/template_aero_migration_test.cpp`
(suite `TemplateAeroMigration`, which matches `Aero`), in `astra_unit_tests`. It does not travel with copies.
- **Setup:**
  - It loads `aircraft/_template/physical/aero` by path, as `aero_math_namespace_isolation_test.cpp` loads
    the notional transport.
  - It gives the plugin the notional transport's numbers (`test_plot_new_aircraft.py`'s `FILL`), with
    controls written and latched.
  - The "before" expression is the pre-migration assembly, typed in the test: `relative_velocity_b`,
    airspeed, α, β, q̄, ρ, the controls and `in.omega_b = vs.omega_b()`. It is passed to `compute_wrench`
    from `aircraft/_template/physical/aero/aero_math.hpp`.
- **Test:** `OnAFlatEarthTheWrenchIsTheInertialRateWrenchBitForBit`.
  - Conditions: the field is +0.0 and the wind is zero.
  - Each state's assertions run under its own `SCOPED_TRACE`, so the output shows which states fail.
  - All six components are compared bitwise (`std::bit_cast`, so +0.0 ≠ −0.0) at three stated states:
    - **W1, the copy's trim:**
      - v_b = (229.9360238725, 0, 5.42447469393253), Euler (0.6, 0.023586859573503434, 0);
      - ω_b = 0, ρ = 0.4128;
      - controls (0.026674474137989946, 0, 0).
    - **W2, every term nonzero:**
      - v_b = 100·(cos 0.1, sin 0.1, 0), Euler (−2.5, 0.3, 0.2);
      - ω_b = (0.05, −0.02, 0.03), ρ = 1.0;
      - controls (0.1, −0.05, 0.02).
    - **W3, signed zeros:**
      - v_b = (200, −0.0, 10), Euler (−2.5, 0.3, 0);
      - ω_b = (−0.0, −0.0, −0.0), ρ = 0.5;
      - controls 0.

**D2 and D3, the contract a copy keeps.** They live in `aircraft/_template/tests/aero_test.cpp` (suite
`AircraftAero`), and `test_new_aircraft_build.py` counts them. They compare the plugin only with itself or
with finiteness, so a copy that edits its own math still passes them while it reads `omega_eb_b`.
2. `DampsTheBodyRateRelativeToTheEarth`: at W2 with the field set to ω_b, the wrench is bitwise the wrench at
   W2 with ω_b = 0 and the field +0.0, because ω_b − ω_b is exactly +0.0.
3. `AnUnsetEarthRateGivesANonFiniteWrench`:
   - W1 with the field left at its default gives a non-finite force and moment;
   - with the field +0.0, the same state gives a finite wrench (the control).

**E. The stale plugin.** `tests/plugins/stale_api_version` is new, built `NO_LOAD_ALL`, in `bad_version`'s
shape.
- Its descriptor carries `astra::kCoreApiVersion - 1`. That stands in for a plugin built before this bump,
  and the offset survives PR 3's second bump.
- The test `PluginLoader.RejectsAPluginBuiltAgainstThePrecedingCoreApiVersion` asserts:
  - a `PluginApiVersionError`;
  - its `plugin_path()`;
  - a message naming both versions.

**F. No-alloc.** `NoAlloc.ZeroHeapAllocationsOverOneThousandFramesWithAnEarthRateReaderOnARotatingEarth`:
- It uses a new `tests/configs/no_alloc_earth_rate.yaml`: `wgs84_rotating`, `wgs84_j2`, and the recorder,
  which calls the accessor at every stage.
- The six existing NoAlloc cases pass unchanged.

## Mutation checks (amendment 2), pre-registered, each run once

**Procedure, for each mutation:**
1. Run the anchor check on the unmutated file first and see it succeed (`grep -nF`, lesson 2026-09-29).
2. Apply the mutation to the working tree.
3. Confirm it with `git diff --stat <file>`, the confirmation of record.
4. Build as its own command.
5. Run the predicted tests as a separate command (lessons 2026-09-26 and 2026-09-29).
6. Record which tests fail.
7. Revert with `git checkout -- <file>`, and confirm that `git diff --stat` is empty.

They run after the implementation commits, and before the identity run and the gate. The worktree is clean
after the last one.

Results go in the PR body. Exceptions to "report it":
- If (a) leaves B4 passing, the work stops and goes to Joe before the canonicalization or B4/B5 is kept as
  written.
- **A failure outside a row's predicted set** (amendment 2 of Joe's second review):
  - it is reported with its cause, traced to the state the test reaches;
  - if the cause is a wrong prediction, the PR body says so;
  - a test is never changed to match a prediction.
- Any other surviving mutant is a test defect:
  - it is traced to which state the test actually reaches (lesson 2026-09-28);
  - the test is fixed before review, and the fix is reported;
  - no tolerance changes.

| Mutation | Change | Predicted to fail |
|---|---|---|
| (a) | delete `omega_ie_b.array() += 0.0;` | B4 and B5 only, on component x only (−0.0); B6 runs at the identity attitude, where `c_nb * 0` is +0.0 anyway |
| (b) | compute the field from the frame-start state (the combined buffer) instead of `vs_stage` | B1 (slots 1 to 3, and slot 1 ≠ slot 0); also B2 and B3, whose probe states differ from the combined buffer |
| (c1) | move the assignment below the out-of-range return, above the non-finite check | B6 (ii); B6 (i) passes |
| (c2) | move it below the non-finite return | B6 (i) and B6 (ii) |
| (d) | restore `in.omega_b = vs.omega_b();` in the template's aero | D2 and D3; D1 passes (the field is +0.0) |
| (e) | swap the accessor's operands (`env.omega_ie_b - vs.omega_b()`) | C1, C2's first half, B3 (ii), and D1 at W2. D1 at W3 may fail; D1 at W1, B3 (i), D2 and D3 pass. See the note below |

**Note on (e).** With the operands swapped, the template aero damps `env.omega_ie_b − vs.omega_b()`.
- **At W2** that is −ω_b against D1's +0.0 field, so D1 fails at W2.
- **At W1** (ω_b = +0.0) both sides see +0.0, so W1 passes.
- **D2 and D3 pass.** ω_b − ω_b and its swap are both exactly +0.0, and NaN stays NaN.
- **At W3** the plugin sees +0.0 rates where the pre-migration assembly sees −0.0. By hand:
  - the roll rate terms sum to +0.0 either way;
  - the pitch moment is nonzero;
  - only the yaw rate term's zero changes sign;
  - adding the q̄ part and the `r_aero_b × force_b` term should turn it into +0.0 in both runs.

  That argument rests on the signs of β, of `force_b`'s zero components and of the rotated zero wind at W3,
  which depend on Eigen's evaluation order. So W3 is predicted "may fail", and its actual result is reported.

## Item 4: what "identical" compares (pre-registered)

1. **The wrench at stated states:** D1 (W1 to W3), bitwise.
2. **A run of the template copy's `cruise.yaml`, before and after, on one build with the worktree fixed.** A
   script, `docs/reviews/evidence/m7-pr3b-template-identity/identity.py`, is committed before its first run.
   - **Preconditions** (amendment 10):
     - After that commit and before the run, `just python` rebuilds the venv's `astra._core`, so its
       `git_describe` names that commit, with no `-dirty`.
     - The worktree is clean before and after (`git status --porcelain` empty).
     - The parent and each child print `astra._core.__file__` and the log's `git_describe`. These must name
       this worktree's build, and equal `git describe --always --dirty` at HEAD.
   - **The copy.** `test_new_aircraft_build.py`'s `_copy_sources` puts a throwaway copy in the session
     scratchpad, outside git. Then:
     - `just new-aircraft gliderx`, filled by `test_plot_new_aircraft.py`'s `_fill`;
     - one `release` build directory, with the six plugin targets built once.
   - **After:** the candidate's aero, as copied.
   - **Before:** in the same build directory, the copy's `aero.cpp` is replaced by
     `git show 56b17bd:aircraft/_template/physical/aero/aero.cpp`, run through `new-aircraft.sh`'s namespace
     `sed`.
     - Only the aero target is rebuilt.
     - The SHA-256 of every plugin library is taken before and after the rebuild; only the aero's may change.
   - **Each pass runs in its own child process.** In one process, a `dlopen` could keep running the earlier
     aero, while the logged SHA-256 is read from the file on disk.
   - **The passes share one config.** Both use identical config text, including one log path, and the
     driver renames the file after each pass. The config's own 1000 frames are run.
   - **Two initial conditions:** the copy's ψ = 0.6 rad, and ψ = −2.5 rad (the same θ and φ), whose C_nb
     row 1 is all negative.
   - **The comparison.** The two HDF5 files are walked with h5py.
     - The object sets must be equal.
     - Every dataset must match in dtype, shape and raw bytes, with record tables compared field by field
       and row by row.
     - Every attribute must match in raw bytes.
     - Raw bytes count +0.0 ≠ −0.0.
   - **Expected difference: exactly one,** the `sha256` cell of the `aircraft/gliderx/physical/aero` row in
     `/plugins` (ADR-0025 Q9: the library's bytes). Any other difference stops the work and goes to Joe.
   - **Planted control:** in a copy of the after-log, one float64's sign bit is flipped in a state dataset,
     and the comparison must report it.
   - **Reported, never a gate:** a third pass with a planted aero, `vs.omega_b() - (−0.0, −0.0, −0.0)`. It
     shows whether a signed zero in a rate would have reached a logged value on these runs, and either
     answer is reported as found.
   - **Where the log goes.** `identity.log` is written to the session scratchpad. It is copied into
     `docs/reviews/evidence/m7-pr3b-template-identity/` only after the "after" clean-worktree check, so both
     porcelain checks hold. It names the commit it ran on, and is then committed.
   - **When it reruns.** A later commit touching `core/`, `plugins/`, `aircraft/` or `tests/` reruns it.

## Unchanged readers (item 5)

None of these reads the field, and none of their files changes. `git diff --stat origin/main..HEAD` shows
nothing under:
- `tests/golden/`;
- `tests/physics/nesc/`;
- `tests/python/mc_planted_fixtures/`, `tests/python/mc_analysis_fixtures/`,
  `tests/python/mc_sizing_fixtures/` and `tests/python/montecarlo_fixtures/`;
- `aircraft/notional_transport/`;
- `tests/plugins/nesc_aero/`.

The commands:
- M3's frozen regression and the R1 golden: `ctest --preset asan -R
  'NotionalTransportCruiseRegression|CruiseEndToEndR1Golden' --no-tests=error`;
- M2's NESC cases 1 to 10: `ctest --preset release -R Nesc --no-tests=error`;
- M4's lockstep acceptance and every M6 batch fixture (amendment 9): `.venv/bin/pytest tests/python -k
  'lockstep or mc_ or montecarlo'`. That covers `test_lockstep`, `test_mc_planted`, `test_mc_analysis`,
  `test_mc_sizing` and `test_montecarlo`.

A value that moves is a finding for Joe, never a tolerance change.

After implementation, `grep -rn 'omega_ie_b\|omega_eb_b' core plugins aircraft tests python` must show only:
- the field, the accessor and the assignment;
- the EOM's own local;
- the template's aero;
- the recorder and the tests.

## Docs (item 8)

- **Architecture §7's paragraph** changes from "*planned*, PLAN.md M7 PR 3b" to "built in PLAN.md M7 PR 3b".
  - It names `Vehicle::prepare_stage_environment`.
  - It says the field equals `c_nb · omega_ie_n` in value and is exactly +0.0 wherever that product is a
    zero, so the template's `earth/flat` values are identical by construction (amendment 1).
  - It says the latched sample holds NaN.
  - It keeps the F-16's aero as *planned* (PR 6b).
  - It gives no version number, because PR 3 may change it.
  - No architecture revision and no §16 edit: M6's status changes kept rev 10, and M7 PRs 5 and 8 left
    §16's M7 note alone.
- **Comments:** EnvironmentSample's header, both field comments, and the related comments (design 7).
- **The template aero's comment** (design 5).
- **At `/wrap`:**
  - PLAN.md's PR 3b line.
  - In PLAN.md's unscheduled B2 item, "the F-16 and the template will damp" becomes "the template damps
    (PR 3b) and the F-16 will". The item also records `notional_transport/physical/aero/aero.cpp:4-6`'s
    "identical … only the class name differs" as stale until B2 (amendment 6).
  - STATE.md is written as main reads it after the merge: PR 6b becomes eligible once PR 3b and PR 4 are
    both merged.
- **ADR-0032 is not edited.**

## Order (item 10) and commits

1. Joe approves this plan, and the "approved" line goes in, in an edit he approves manually.
2. Fetch, create the worktree, move and rename this draft, commit, push, open the draft PR.
3. The core: field, accessor, assignment, comments, bump, the Python pin.
4. Tests A to C, E and F, and the stale and recorder plugins. `./scripts/check-fast.sh` while iterating.
5. The template's migration, D1 to D3, and the template's comment.
6. Architecture §7.
7. The mutation checks (a) to (e), each reverted, with the worktree clean afterward.
8. Commit the identity script, then `just python`, then run it, then commit its log.
9. Row 3b's checks and item 5's commands, with item 8's alternative counts (Acceptance).
10. The full gate on the final candidate, its trimmed summary committed, and `git diff --check
    origin/main..HEAD`.
11. `/review-cycle`: an adversarial Codex review of `origin/main..HEAD`, at most two correction rounds,
    each followed by the checks and gate it invalidates.
12. `/human-review` writes the PR body, and Joe reads the critical diff and tests.
13. `/wrap`.

## Acceptance (item 9)

**Before any ctest run counts** (amendment 8; `--no-tests=error` passes when any one alternative matches),
`ctest --preset <p> -N -R '<alternative>'` is recorded for every alternative, and each must be nonzero:
- `Simulation`, `PluginLoader`, `Aero` (asan);
- `NotionalTransportCruiseRegression`, `CruiseEndToEndR1Golden` (asan);
- `Nesc` (release);
- `AirData`, `NoAlloc` (asan).

**In the run output,** each new name must appear:
- `SimulationEarthRate.*` (six tests);
- `PluginLoader.RejectsAPluginBuiltAgainstThePrecedingCoreApiVersion`;
- `aircraft__template_tests.AircraftAero.DampsTheBodyRateRelativeToTheEarth` and `…AnUnsetEarthRateGivesANonFiniteWrench`;
- `TemplateAeroMigration.*`;
- `NoAlloc.*EarthRate*`;
- `AirData.EarthRelativeBodyRate*` (three tests).

**For each pytest selector,** `--collect-only -q` counts per file are recorded, and each named file
contributes.

**The commands:**
- `ctest --preset asan -R 'Simulation|PluginLoader|Aero' --no-tests=error`
- `.venv/bin/pytest tests/python -k new_aircraft`
- M3's frozen regression and M2's NESC cases unchanged (commands above), and `.venv/bin/pytest tests/python
  -k 'lockstep or mc_ or montecarlo'`
- `ctest --preset asan -R 'AirData|NoAlloc' --no-tests=error`
- the mutation table, each row's prediction met (or handled as stated)
- `identity.log`: exactly the one expected difference per initial condition, and the planted control detected
- `./scripts/check-full.sh`: exit 0 and `check: OK`, on the final candidate
- `git diff --check origin/main..HEAD`: clean

Results, exit codes and counts go in the PR body, never in a record-only commit.

## Risks and rollback

- **Folding of `+= 0.0`.** It is not folded without `nsz`, and the build has no `-ffast-math`. Mutation
  (a) and B4/B5 would show it. Design 4 assumes round-to-nearest, which nothing in the tree changes.
- **A stale plugin directory** in a developer's build fails to load with the typed error, which is the point
  of the bump.
- **PR 3 ordering.** Whichever PR merges second takes the next number in `core_api.hpp.in` and
  `test_package.py`. E's `kCoreApiVersion - 1` needs no edit.
- **Rollback:** a revert of the squash commit. The change is additive, apart from the version number.

## Plan critique

None. ADR-0032 and Joe's brief and amendments settle the design, and item 10's order has no critique step.
The adversarial review covers the build.

## Done when

- Every test above passes.
- The mutation predictions are met.
- The identity holds, with the one expected difference.
- The unchanged readers pass with no edit.
- The full gate passes on the final candidate.
- The Codex adversarial review is resolved within two rounds.
- The PR body carries the results.
- Joe reads the critical diff (`environment_sample.hpp`, `air_data.hpp`, `vehicle.cpp`, `core_api.hpp.in`,
  the template's `aero.cpp`) and the tests.

Merging is Joe's.
