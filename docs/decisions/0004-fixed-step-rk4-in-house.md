# ADR-0004 — Fixed-step RK4 at 1 kHz written in-house; Euler retained for tests; Odeint only in analysis tools

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Integrators"; `docs/architecture.md` §5, §8.

## Context

The scheduler runs a fixed major frame with every rate an integer divisor of it, and the lockstep transport
promises the flight software a fixed exchange rate. The integrator must honour that, allocate nothing per
step (principle 3, real-time capable), and be verifiable by a convergence-order test that checks *our* code.

## Decision

`Integrator` takes the derivative functor `f(t, y) → ẏ` over the fixed-size 13-vector (ADR-0003) and
advances exactly one major frame, default `dt = 1 ms`. Classical fixed-step RK4 is the default; forward
Euler exists so a test can show RK4 is doing its job. Both are written in-house in `core/kernel/` (about
thirty lines for RK4). A post-step hook renormalizes `q_nb`. Integrators are part of the kernel's contract
with the state vector, not plugins. Boost.Odeint (1.83, header-only, in apt) may be wrapped on the
Python-facing analysis side for adaptive previews or long-duration checks; it never enters the sim loop.

## Rejected alternatives

- **Boost.Odeint in the main loop.** Its steppers manage temporaries through resizers that are easy to get
  subtly wrong with Eigen types (allocation in the step path), it adds a template-heavy dependency between
  the kernel and the nanobind bindings, and its correctness would be trusted rather than tested here.
- **RK45 adaptive.** Variable step breaks the fixed-rate contract with the flight software and makes
  "frame N happened at t = N·dt" false.
- **Symplectic integrators.** Irrelevant for a damped aerodynamic system; their benefit is long-term
  Hamiltonian conservation.
- **500 Hz default major frame.** Halves cost, but 1 kHz keeps the 100 Hz transport divisor and the
  future sensor rates (IMU at 200–1000 Hz) as clean integer divisors with margin.

## Consequences

- M1 acceptance: RK4 vs Euler convergence order demonstrated; torque-free energy and angular momentum drift
  below 1e-9 relative over 1000 s; quaternion norm within 1e-12; zero heap allocations over 1000 frames.
- The derivative functor is the same one exposed to Python as `Simulation.derivative`; there is one EOM.
- Changing `dt` is a config field whose value is recorded in the log; every rate divisor is validated
  against it at init.
