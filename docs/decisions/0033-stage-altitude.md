# ADR-0033: The stage altitude, supplied by the core to wrench sources

Status: **accepted**, 2026-10-11
Date: 2026-10-10
Owner: Joe
Related/supersedes: supersedes **one clause** of ADR-0020's Consequences, "with zero further core interface
change", for the stage altitude only, as ADR-0032 did for the body rate relative to the Earth. ADR-0020's Decision
is unchanged, and neither ADR-0020 nor ADR-0032 is edited.

This record follows ADR-0032's pattern, and elaborates five others:
- ADR-0003: the Earth model owns the meaning of `p_n`, and its `altitude` gains a consumer outside the core;
- ADR-0016: the value is computed per RK4 stage, as the atmosphere sample is;
- ADR-0017: its quiet-NaN default follows that record's convention;
- ADR-0020: the value reaches a plugin through `EnvironmentSample`;
- ADR-0030: the identifier carries a unit suffix, which a C++ identifier may.

Drafted with `docs/plans/m7-pr3c-altitude-and-units.md`, from `docs/plans/m7-pr6b-f16-aircraft.md` decision 1.
Governs PLAN.md's M7 PR 3c.

## Context

- **The need.** The F-16 engine's thrust tables are indexed by altitude (`F16_prop.dml`), and PR 6c ports them.
  `wrench_b` receives `(VehicleState, EnvironmentSample)`, and the sample has no altitude.
- **Why not the state.** `p_n`'s meaning belongs to the Earth model (ADR-0003). It holds NED meters under
  `earth/flat` and latitude, longitude and altitude under `earth/round` and `wgs84_rotating`.
  `wind_model.hpp:12-25` names reading it in a plugin as the representation trap.
- **What the core already has.**
  - `EarthModel::altitude(vs)` is the height above the model's reference surface.
  - `Vehicle::prepare_stage_environment` already computes it at each stage's own state for the atmosphere's
    range check and `at()`, but only when an atmosphere is configured.
  - `AtmosphereModel::at` and `WindModel::begin_frame` already receive the same value as a plain `altitude_m`.
- **The rule this changes.** ADR-0032 superseded ADR-0020's "with zero further core interface change" for the
  Earth-relative rate only, and named one field. A second field needs its own record.

## Decision

1. **A field, `EnvironmentSample::altitude_m`**, in meters. It holds `EarthModel::altitude` at the stage's own
   state: the height above the configured Earth model's reference surface. That is the ellipsoid under
   `wgs84_rotating`, the sphere under `earth/round`, and −p_n[2] under `earth/flat`. It claims nothing about a
   geoid. The F-16 engine data's altitude is "Geometric altitude above reference geoid", and whether reading it
   against the ellipsoid holds is PR 9's question (the addendum's D8).
2. **Per stage.** `Vehicle::prepare_stage_environment` sets it first, with `omega_ie_b`, in every RK4 stage's
   copy, before the atmosphere's range check. It holds on the step path and on `Simulation.derivative`'s probe
   path alike. A stage whose wrenches the environment checks skip still carries it to `integrate_state`. The
   atmosphere is sampled at the same value.
3. **A NaN default** (ADR-0017's convention, as `atmosphere` and `omega_ie_b`).
   - The frame-latched sample (`Vehicle::environment()`) keeps it.
   - Code that builds its own `EnvironmentSample` sets it from the sim's Earth model at the evaluated state. A
     forgotten value gives its reader a NaN wrench, never a sea-level number.
4. **No canonicalization.** At exactly zero altitude `earth/flat` gives −0.0, and the field carries it. Nothing
   subtracts it, so no reader's result changes.
5. **Unchanged.**
   - `HealthMonitor` does not check it: it is the core's own value, not an environment plugin's output.
   - It is not published as a signal.
   - `wrench_b`'s signature is unchanged.
6. **The core API.** `EnvironmentSample` crosses the plugin loader boundary (ADR-0016's test), so
   `kCoreApiVersion` is bumped, and a plugin built against the preceding version is rejected at load. PR 3c
   states the number.

## Rejected alternatives

- **A config key naming the position representation.** No plugin can check it against the configured Earth
  model, so a mismatch reads the wrong altitude silently.
- **Reading `p_n(2)` as height.** On a flat Earth it is −altitude, which an engine table clips to its 0 ft row
  silently.
- **Inverting `atmosphere.pressure_pa`.** That gives geopotential altitude on an ISA day only, and is wrong on
  M9's non-standard days.
- **A frame-latched value.** It is wrong at three of four stages (ADR-0016), unlike the per-stage atmosphere
  that the same engine reads.
- **A zero default.** A forgotten value would run silently at sea level.

## Consequences and validation

- **PR 3c implements it:**
  - the field and its per-stage assignment, on both paths;
  - the `kCoreApiVersion` bump and the stale-plugin rejection;
  - `tests/plugins/earth_rate_recorder` extended as the field's test reader.
- **PR 6c's engine** reads it. **PR 7's independent evaluator** sets it beside `omega_ie_b`, from the sim's
  Earth model at the frame-start state.
- **Nothing on main reads it** before PR 6c, so every existing value is unchanged.
- **Acceptance,** per the M7 plan's PR 3c row:
  - `ctest --preset asan -R 'Vehicle|Simulation|Units|PluginLoader' --no-tests=error`: the field equals
    `EarthModel::altitude` at each stage's own state, bit for bit, on both paths, including skipped stages; the
    latched sample is NaN; an unset field gives its reader a NaN wrench; a stale plugin is rejected;
  - `ctest --preset asan -R NoAlloc --no-tests=error`.

## Approval

Accepted by Joe, 2026-10-11.
