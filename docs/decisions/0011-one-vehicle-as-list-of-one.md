# ADR-0011 — One vehicle per simulation in v1, structured as a list of one

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Vehicles per sim"; `docs/architecture.md` §7 Vehicle.

## Context

v1 flies one aircraft. Multi-vehicle simulation (formation, targets, wake encounters) is a v2 idea Joe
wants to keep possible without a rewrite of the kernel, the signal namespace, or the logger.

## Decision

`Simulation` holds a list of `Vehicle`s whose length is one in v1. Each `Vehicle` owns its own state vector
(ADR-0003), its own model composition (one mass-properties model, lists of aero and propulsion models whose
wrenches are summed), and its own signal namespace (`v0.aero.CL`). Environment models (atmosphere, wind,
gravity, Earth) are shared across vehicles. Nothing in v1 may assume exactly one vehicle except the CLI's
convenience defaults (which pick `v0`).

## Rejected alternatives

- **A single hard-wired vehicle.** Simplest today; turns v2 multi-vehicle into a rewrite of `Simulation`,
  the scheduler order, the registry namespace, and every log reader.
- **Full multi-vehicle now.** Would force decisions (inter-vehicle interaction, per-vehicle transports,
  scheduling fairness) that v1 has no test for; premature.

## Consequences

- Signal names are namespaced by vehicle index from M1 onward; the logger and `astra.log` read that
  structure.
- The scheduler order (environment → transport → vehicle models → EOM → integrator → post-step → logger)
  is written per vehicle inside the frame loop.
- Adding a second vehicle in v2 is a config list of length two plus an ADR that supersedes this one.
