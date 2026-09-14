# ADR-0001 — Frames and attitude: FRD body, NED navigation, `q_nb` Hamilton scalar-first, 3-2-1 Euler derived

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Earth model" and the frame conventions in `docs/architecture.md` §6.
Written first because it is the decision most likely to be silently re-decided by an agent.

## Context

Every model, the EOM, the ICD frame, the FlightGear bridge, and every test tolerance depends on one shared
definition of axes and attitude. Sign errors in frame conventions look like physics (a one-frame lag, a
reversed roll response) and are the most expensive class of bug to find after the fact. The convention must
be fixed once, before any code, and pinned by a test.

## Decision

| Frame | Symbol | Definition |
|---|---|---|
| Body | b | Origin at CG; x forward, y right, z down (FRD) |
| Local navigation | n | North-East-Down, tangent to the Earth model's surface at the vehicle |
| Earth-fixed | e | ECEF for round/WGS-84 Earth; coincides with n at the origin for flat Earth |
| Inertial | i | ECI; used only by the rotating-Earth models |

Attitude is the unit quaternion `q_nb` that rotates vectors **from n to b**, Hamilton product convention,
scalar-first storage `(w, x, y, z)`, renormalized after every integrator step. Euler angles ψ, θ, φ
(yaw, pitch, roll; 3-2-1 sequence) are derived outputs only, never state. Control-surface sign conventions in
the ICD (positive elevator, aileron, rudder) are defined against these axes and documented alongside them.

## Rejected alternatives

- **ENU navigation frame.** Common in robotics and GIS, but aerospace references (Stevens & Lewis, the NASA
  check cases, FlightGear's FDM packet) are NED; adopting ENU would put a conversion inside every oracle test.
- **JPL quaternion convention.** Same numbers, opposite product order; mixing it with Eigen's Hamilton
  `Quaterniond` and with the textbook EOM invites silent sign errors.
- **Scalar-last storage `(x, y, z, w)`.** Eigen stores scalar-last internally, but the state vector, the log,
  and the ICD are read by humans and by other toolchains; scalar-first matches the aerospace literature.
  The Eigen conversion happens in one place in `core/util/`.
- **Euler angles as state.** Gimbal lock at θ = ±90° and non-linear kinematics; the quaternion is state,
  Euler angles are derived on demand for logs and the ICD.

## Consequences

- `core/util/` owns the single quaternion/DCM/Euler conversion set; no model writes its own.
- M1 ships a unit test that pins the sign of every term: known attitude → known DCM → known Euler triple,
  and a rotation of a unit vector in each axis. That test is human-owned oracle data.
- The ICD (ADR-0006) carries φ, θ, ψ, p, q, r, u, v, w in exactly these conventions.
- Any later frame (wind, stability, sensor mount) is defined relative to b or n here, never freshly.
