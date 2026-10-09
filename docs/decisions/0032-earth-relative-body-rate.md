# ADR-0032: The body rate relative to the Earth, supplied by the core to wrench sources

Status: **proposed**, 2026-10-09
Date: 2026-10-09
Owner: Joe
Related/supersedes: supersedes **one clause** of ADR-0020's Consequences, "with zero further core interface
change", for the body rate relative to the Earth only. ADR-0020's Decision is unchanged, and ADR-0020 is not
edited.

This record elaborates five others:
- ADR-0003: the Earth model's rotation rate, which it supplies to the EOM, gains a second consumer;
- ADR-0016: the new value is computed per RK4 stage, as the atmosphere sample is;
- ADR-0017: its quiet-NaN default is this field's precedent;
- ADR-0020: the value reaches a plugin through `EnvironmentSample`, with a core utility;
- ADR-0021: `wrench_b`'s signature stays fixed.

Drafted with `docs/plans/m7-f16-inventory.md` (D3, §10 item 3) and `docs/plans/m7-pr1b-adrs.md` (J3, J4 and
amendments 5, 6, 12 and 13). Governs PLAN.md's M7 PR 3b.

## Context

- **The state.** ω_b, the rigid-body block's angular rate, is relative to inertial, in body axes (ADR-0003,
  ADR-0021, architecture §6). Every reader of body rates in the tree receives it (the addendum's D3 lists them).
  `wrench_b` receives `(VehicleState, EnvironmentSample)`, and the sample carries only `wind_n` and `atmosphere`.
- **The F-16 aero.** `F16_aero.dml` names its rate inputs `bodyAngularRate_Roll` and so on, with no frame
  (`:311-330`). The rates enter only through nine damping derivatives (addendum §2).
- **Vol II D.1.17** (p.309): "In SIM 4, the zero rotational rate relative to the Earth induced no aerodynamic side
  force, rolling moment, or yawing moment." Of SIM 2, the same paragraph says "a zero inertial rate translated
  into an Earth-relative rate where much of the Earth's rotation rate appeared in body-axis yaw rate rb … The
  Earth-relative rate induced an aerodynamic side force, rolling moment, and yawing moment".
- **The members' data** (D3's computed check, case 11 at t = 0):
  - Earth-relative rates predict SIM 5's aero Y, L and N to 3e-8 relative.
  - Inertial rates predict −0.1155 lbf, −1.6046 lbf·ft and 1.5877 lbf·ft, which miss by factors of 4 to 16.
  - SIM 4's recorded values, about 1e-11 to 1e-15, fit Earth-relative rates only.
  - M7 PR 1b reran this chain as its self-check 1c: 2.5e-8 relative
    (`docs/reviews/evidence/m7-pr1b-rate-frame/floor.log`).
- **The stakes** (D3's estimate). An aero that damps the inertial rate, where the members damped the
  Earth-relative one, drifts case 11's roll to about 99 times its band (2.686e-5 rad) by 180 s.
- **What the core already has.**
  - The EOM forms `omega_ie_b = c_nb * earth_.omega_ie_n(vs)` on every derivative call
    (`core/src/kernel/rigid_body_6dof_dynamics.cpp`).
  - Each RK4 stage builds its own `EnvironmentSample`, refreshing the atmosphere, before every `wrench_b` and
    `integrate_state` of that stage (`Vehicle::evaluate_derivative_stage`). `Simulation.derivative`'s probe path
    runs the same function.

## Decision

1. **What the core supplies.** The body rate relative to the Earth, ω_eb = ω_b − ω_ie, in body axes.
   - **A field, `EnvironmentSample::omega_ie_b`.** It holds the Earth's rotation rate relative to inertial,
     expressed in body axes, in rad/s. It is computed as `c_nb * earth.omega_ie_n(vs_stage)`, the EOM's own
     expression on the stage's own state.
   - **An accessor, `air_data::omega_eb_b(vs, env)`** in `core/include/astra/util/air_data.hpp`. It returns
     `vs.omega_b() - env.omega_ie_b`, and readers call it. Its comment states that the rate is Earth-relative,
     not air-relative (item 6).
   - ω_ie_b is zero for `earth/flat` and for a non-rotating `earth/round`, so there the relative rate equals ω_b.
   - **Names.** They follow the EOM's subscripts (`omega_ie_b`, `omega_en_b`, `omega_nb_b`): the rate of the
     second frame relative to the first, expressed in the third. ADR-0030 allows a unit suffix on a C++
     identifier but does not require one, and `EnvironmentSample::wind_n` has none.
2. **A per-stage field.**
   - It is per stage, like the refreshed atmosphere (ADR-0016), and unlike the frame-latched `wind_n`.
   - The core sets it in each stage's environment before every `wrench_b` and `integrate_state`. That holds on
     the step path and on `Simulation.derivative`'s probe path alike.
   - Anything that builds its own `EnvironmentSample` sets it from the sim's Earth model at the evaluated state.
   - PR 3b corrects `EnvironmentSample`'s header comment, which says the sample is "held constant through that
     vehicle's wrench and EOM stages".
3. **A NaN default.** This follows `atmosphere`, and ADR-0017's Decision, under which every input entry's values
   "default-initialize to a quiet NaN, never an unspecified value: "never written" and "written non-finite" are
   deliberately the same, loudly reported state".
   - The core always sets the field on a sim's own stages, which are the only ones `check_wrench` sees. The
     default therefore reaches only code that builds its own `EnvironmentSample`: PR 7's evaluator, a binding,
     or a unit test.
   - There, a forgotten value gives the reader a NaN wrench. That fails PR 7's self-check against
     `wrench_total`, or the unit test's own assertion. It never runs as a non-rotating Earth.
   - A test of a flat or non-rotating condition sets zero explicitly.
4. **Readers.** The F-16 aero (PR 6b) and the template's aero (PR 3b).
   - The template's configs use `earth/flat`, so the template's values stay identical. A test in PR 3b shows it.
   - **Not readers.** The notional transport's aero and `tests/plugins/nesc_aero` keep the inertial ω_b.
     Migrating them is PLAN.md's unscheduled B2 item, measured before Joe decides it. Until then the tree
     carries two documented conventions.
5. **Unchanged.**
   - ω_b stays the state's inertial rate (ADR-0003, ADR-0021, architecture §6).
   - ICD v1's p, q and r stay inertial (`icd/astra_icd.h`).
   - `wrench_b`'s signature is unchanged.
   - The rate is not published as a signal.
6. **Excluded, and named: the air's rotation.** Earth-relative is not air-relative. Under a non-uniform wind (a
   shear, or turbulence's rotational part) the air mass rotates, and that rotation is not supplied. With no wind,
   or a uniform one, the two rates are equal. The NESC F-16 cases fly with no wind.
7. **The core API.** `EnvironmentSample` crosses the plugin loader boundary, which is ADR-0016's test for a
   bump. So `kCoreApiVersion` is bumped, and a plugin built against the preceding version is rejected at load.
   PR 3b states the number.

## Rejected alternatives

- **(A) Each wrench source computes the rate itself,** as ω_b − C_bn·ω_ie^n(lat), with its own Earth rate.
  - The latitude lives in the Earth model's own position representation (ADR-0003).
  - The wrench source cannot see whether the configured Earth model rotates (`earth/round` does not by default), so
    its Earth rate can disagree with the configured one. That is the kind of inconsistency PR 3's planned
    rotation-consistency check refuses between the Earth and gravity models.
  - ADR-0029's flight-software peer does compute the rate itself, because it sees only the ICD frame. That record
    states why, and the risk that remains.
- **(B2) Every aero migrates now,** the notional transport and `nesc_aero` included.
  - Every notional-transport config runs `wgs84_rotating`, so these would move: M3's frozen regression, the R1
    golden, M4's lockstep acceptance, M6's R9 batch, `test_cruise_phugoid` and `test_autopilot_design`.
  - Migrating `nesc_aero` would move M2's case 3, against the invariant that cases 1 to 10 keep their bands.
  - The size is unmeasured. It stays PLAN.md's item.
- **Inertial rates everywhere,** the status quo. They miss SIM 5's lateral aero by factors of 4 to 16 at case 11's
  trim, and the drift they cause is about 99 times case 11's roll band by 180 s.
- **An air-relative rate now.** It needs a wind model that supplies the air's rotation, and no wind plugin does.
- **A field holding the relative rate itself,** ω_b − ω_ie_b. It puts a copy of state into the environment sample.
- **An accessor with no field.** It needs the Earth model inside the wrench source, which is a new binding.
- **Publishing the rate as a signal.** Nothing reads it from a log. It is computable offline from the logged
  state and position, and a new column moves every test that pins a log's column set.
- **A zero default.** It was proposed because a non-rotating Earth is a valid physical case, as `wind_n`'s zero
  is still air, and because hand-built samples would then need no edit. It is rejected because a forgotten value
  would then run silently as a non-rotating Earth.

## Consequences and validation

- **PR 3b implements it:**
  - the field and the accessor;
  - the per-stage assignment, on both paths;
  - the header-comment correction;
  - the `kCoreApiVersion` bump and a stale-plugin rejection;
  - the template's migration.
  It lists every test that builds an `EnvironmentSample` for a reader.
- **PR 6b's aero** reads `air_data::omega_eb_b`, with D4's transfer to the CM.
- **PR 7's independent evaluator** sets `omega_ie_b` from the sim's Earth model at the sample's frame-start
  state. A forgotten value fails its self-check.
- **Trim.** Trim and `Simulation.derivative` reach the field through the same stage path, so the F-16 trim
  residual's aero rate terms (the addendum's D5) are Earth-relative.
- **Q5's sub-condition** in the M7 plan holds: the rate comes from the sample's state and environment, never
  from a peer binding.
- **`check_environment`'s scope** is unchanged. The field is the core's own value, not an environment plugin's
  output.
- **Acceptance,** per the M7 plan's PR 3b row:
  - `ctest --preset asan -R 'Simulation|PluginLoader|Aero' --no-tests=error`: the field is set on both paths; the
    template's `earth/flat` values are identical; a stale plugin is rejected;
  - `.venv/bin/pytest tests/python -k new_aircraft`;
  - M3's frozen regression and M2's NESC cases are unchanged.

## Approval

Pending Joe's decision.
