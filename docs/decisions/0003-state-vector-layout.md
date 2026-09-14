# ADR-0003 — 13-state vector with position representation owned by the Earth model

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 rows "Earth model" and "v1 physics scope"; `docs/architecture.md` §6 State vector, §8.

## Context

The integrator (ADR-0004), the EOM, the Python `derivative` function, trim, linearization, and the logger
all touch one state vector. Its size and layout are a contract that every later milestone builds on, and
the Earth model must be selectable at runtime (flat, round, WGS-84 rotating) without the EOM changing.

## Decision

The state is 13 `double`s in a fixed layout, `Eigen::Vector<double, 13>`:

```
p_n   (3)  position of CG in the Earth model's native representation
             flat:          NED position from the origin, metres
             round/WGS-84:  geodetic latitude, longitude (rad), altitude above ellipsoid (m)
v_b   (3)  velocity of CG relative to the Earth-fixed frame, body axes (u, v, w)
q_nb  (4)  attitude quaternion per ADR-0001, scalar-first
ω_b   (3)  body angular rate relative to inertial, body axes (p, q, r)
```

The Earth model owns the meaning of `p_n` and supplies the EOM with three things: the gravity vector in n,
the Earth rotation rate expressed in n (zero for flat and round-non-rotating), and the transport rate of the
n frame from motion over a curved surface (zero for flat). Mass and inertia are **not** state in v1: they
are constant for a run, provided by the `MassPropertiesModel`; fuel burn is deferred.

## Rejected alternatives

- **ECEF position for all Earth models.** Uniform, but flat-Earth runs (M1 oracles, most unit tests) would
  carry a geodetic conversion on every step for no benefit, and the NED-metres flat case is what the
  torque-free and ballistic oracles are written against.
- **Mass as state.** Needed eventually for fuel burn, but v1 has no fuel model; adding a 14th state now
  means every oracle and golden carries a constant channel. Deferred (architecture §19); becomes an ADR
  that supersedes this one.

## Consequences

- The EOM are the standard body-axis rigid-body equations (Stevens & Lewis form) parameterized by the
  Earth interface; selecting an Earth model at runtime costs nothing in the EOM.
- Position kinematics differ per Earth model and live behind the `EarthModel` interface, not in the EOM.
- `Simulation.derivative(state, inputs)` exposes exactly this 13-vector to Python for trim and linearization.
- Layout changes require a superseding ADR and a golden regeneration decided by Joe.
