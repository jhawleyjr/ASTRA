# ADR-0030: Unit suffixes: never in signal names, allowed in C++ identifiers and config keys

Status: **accepted**, 2026-10-05
Date: 2026-10-04
Owner: Joe
Related/supersedes: supersedes **one bullet** of ADR-0002's Consequences: "No unit suffixes in identifiers
(`altitude`, not `altitude_m`) inside the framework; the unit lives in the registry entry and in the ICD
comment." ADR-0002 is not edited, and the rest of it stands, including its Decision ("Signal names carry no unit
suffix") and its other two Consequences bullets. Drafted with `docs/plans/m7-f16-nesc-oracle.md` (Q20) and
`docs/plans/m7-pr1a-adrs.md`. Closes PLAN.md's (M7) item on ADR-0002's wording (origin: M3 PR 3, ADR-0018).

## Context

ADR-0002's Decision says signal names carry no unit suffix, and its example is a signal-name string
(`registry.add("aero.CL", &CL_, "1")`). The superseded bullet says "identifiers", which can be read as every
C++ identifier. The tree has followed the narrower reading since M2, and accepted records have relied on it:
- **C++ identifiers.** `AtmoState`'s fields `temperature_k`, `pressure_pa`, `density_kg_m3` and
  `speed_of_sound_m_s` (architecture §7) are the precedent ADR-0020 cites. Members such as the Dryden plugin's
  `w20_m_s_` follow it.
- **Config keys.** `aircraft/notional_transport/configs/cruise.yaml` uses `wing_area_m2`, `wingspan_m` and
  `cl_alpha_per_rad`. `plugins/physical/wind/dryden` reads `w20_knots`, a non-SI suffix, and converts it at
  load with `units::knots_to_meters_per_second`. ADR-0002's third bullet allows exactly that: "a suffixed
  form, if ever added, is converted at load and stored SI". The transport's `*_timeout_ms` keys are
  milliseconds, held as integers.
- **Signal names.** None in `core/`, `plugins/` or `aircraft/` carries a unit suffix. (`wrench_total.moment_b_m`
  names the m axis, not meters.)
- **ICD fields** (`tas`, `altitude`, `lat`) carry no suffix. Their units are in the header's comments
  (ADR-0006).

A unit suffix earns its place where nothing else carries the unit:
- in a config file, a human types a bare number;
- in a C++ expression, the reader has no registry.

A signal already has its unit as registry metadata, which the log writes as an attribute. A suffix there would
duplicate that metadata, and the two could disagree.

## Decision

1. **Signal names carry no unit suffix.** This covers every name registered in the `SignalRegistry`,
   write-seam input names included (`controls.elevator`, not `controls.elevator_rad`). The unit lives in the
   registry entry (ADR-0002, unchanged).
2. **C++ identifiers may carry a unit suffix** (`density_kg_m3`, `w20_m_s_`). It is allowed, never required.
   The value is still SI inside `core/`, `plugins/` and `aircraft/` (ADR-0002).
3. **A config key may carry a suffix naming the unit of the value as written** (`wing_area_m2`,
   `w20_knots`). A key with a non-SI suffix on a physical quantity is converted at load by a named
   `core/util/units` function with a unit test, and stored in SI, as ADR-0002's third bullet says. A key
   without a suffix is SI, or a dimensionless count (`frames`, `divisor`, `seed`).
   - The one existing exception is the lockstep transport's `timeout_ms`, `handshake_timeout_ms` and
     `peer_timeout_ms`. They are wall-clock deadlines (ADR-0022 to ADR-0024), held as integer milliseconds
     (`lockstep_exchange.cpp`), and they never enter simulation state or simulation time, so they keep their
     unit as written.
   - A new physical quantity takes no such exception.
4. **ICD field names** stay governed by ADR-0006, and their units stay in the header's comments.

## Rejected alternatives

- **No unit suffix in any identifier**, the superseded bullet's literal reading. It would rename working code
  (`AtmoState`'s fields) and config keys every tracked config uses, for no gain in safety. It would also drop
  the unit from the one place a human types a bare number.
- **Unit suffixes required everywhere.** This would rename every signal and break every existing log and plot
  package, and it would duplicate the registry's unit metadata.

## Consequences and validation

- No code, config or log changes: the tree already conforms.
- One test plugin, outside ADR-0002's scope (`tests/plugins/`), publishes `counting_wind.last_altitude_m`.
  This is disclosed, and it is left as it is.
- Rule 1 binds every new signal M7 adds, ADR-0031's published envelope quantities among them.
- Architecture §6 states the rule (rev 11). PLAN.md's (M7) wording item is closed.

## Approval

Accepted by Joe, 2026-10-05.
