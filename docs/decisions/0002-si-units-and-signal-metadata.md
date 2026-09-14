# ADR-0002 — SI doubles everywhere, units as signal metadata, conversions only at boundaries

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Toolchain" (no mp-units) and `docs/architecture.md` §6 Units, §9.

## Context

A 6-DoF sim mixes aerodynamic tables in one unit system, ICD fields the flight software may want in feet,
FlightGear packets, and YAML written by humans. Unit bugs are classic and invisible. The question is whether
to buy compile-time dimensional safety or keep the type system out of it.

## Decision

Inside `core/`, `plugins/`, and `aircraft/` every quantity is a plain `double` in SI: metres, seconds,
kilograms, radians, newtons, pascals, kelvin. Conversions happen only at boundaries: config loading, log
column metadata, the FlightGear packet, and ICD fields if the flight software asks for non-SI. Signal names
carry no unit suffix; the `SignalRegistry` (§9) stores the unit string as metadata when a model registers a
signal, e.g. `registry.add("aero.CL", &CL_, "1")`, and the logger writes it as an HDF5 attribute.

## Rejected alternatives

- **mp-units.** Compile-time dimensional safety is attractive, but it is not in the Ubuntu 24.04 repos
  (apt-only toolchain), it complicates nanobind and Eigen interop, and clang-19 is unsupported upstream.
  Revisit only if unit bugs actually bite.
- **Imperial internally.** Aviation practice uses feet and knots at the interface, but the physics
  references, the NASA check cases, and Eigen-based EOM are all SI; converting at the boundary is cheaper
  than converting inside every equation.
- **Per-signal unit types (hand-rolled strong typedefs).** A partial mp-units with none of the tooling;
  every Eigen expression would need wrapping and unwrapping.

## Consequences

- No unit suffixes in identifiers (`altitude`, not `altitude_m`) inside the framework; the unit lives in
  the registry entry and in the ICD comment.
- Every boundary conversion is a named function in `core/util/units` with a unit test; nothing multiplies
  by `0.3048` inline.
- Config loading validates units where a YAML key admits them (`altitude: 10000` is metres; a suffixed
  form, if ever added, is converted at load and stored SI).
