# ADR-0026: Derived flight-condition signals from the ICD truth packer

Status: **accepted**, 2026-09-27
Date: 2026-09-26
Owner: Joe
Related/supersedes: elaborates ADR-0001 (frames, 3-2-1 Euler as derived outputs), ADR-0002 (SI, unit metadata),
ADR-0006 (ICD truth fields), ADR-0010 (frame counter), ADR-0011 (vehicle list) and ADR-0022 (exchange stage, whose
own packing is untouched). Supersedes nothing. Used by ADR-0025's row timing. Drafted with
`docs/plans/m5-tooling.md` (decision Q13, critique C2 and C3).

## Context

Euler angles, α, β, TAS, CAS, Mach, altitude, climb rate, lat/lon and specific force exist today only inside
`astra::icd_pack::pack_sim_to_sw` (`core/include/astra/util/icd_pack.hpp`, M4 PR 2, validated by R12a's oracles).
The exchange stage calls it only when a transport is configured, and only on exchange frames. M5's scenario checks
(for example "altitude within a band"), the FlightGear bridge and the run log all need these values for every
run. What `p_n` means depends on the Earth model: NED meters on a flat Earth, lat/lon/altitude on a round or
WGS-84 one. Deriving them anywhere else means re-implementing that distinction.

## Decision

**What is published.** For every vehicle of a config with physics (a `physical` block), whether or not a
`transport` block is present, the core publishes `v<i>.derived.<field>` in the `SignalRegistry`. The fields are
each `astra_sim_to_sw_v1` truth field:

`phi theta psi p q r u v w alpha beta tas cas mach altitude vertical_speed lat lon ax ay az status_flags`

plus the ground-relative NED velocity `v_n_0`, `v_n_1`, `v_n_2`. `v_n` is `frames::rotate_b_to_n(q_nb, v_b)`, the
same product the packer uses for `vertical_speed`. Units are the ICD header's comments, in SI (ADR-0002; `"1"`
for Mach and `status_flags`). `status_flags` is published as a double that holds the integer bitmask exactly. A
field made invalid by a status bit keeps the ICD's own convention: the bit is set and the value is the defined 0,
never NaN. A config without physics publishes none of these.

**How it is computed.** One **separate** call to the existing `icd_pack::pack_sim_to_sw` per vehicle per frame,
into a stage-owned `astra_sim_to_sw_v1`. The call happens at the frame-start point: right after the frame's
environment sample, before the transport exchange and before any vehicle `step()`. Its fields are then copied
into the published doubles, with no allocation.
- `core/src/kernel/lockstep_exchange.cpp` is not edited. The exchange keeps its own call, so the request bytes it
  publishes cannot change.
- No second derivation exists anywhere: not in core and not in Python.

**Timing exception (the one exception to ADR-0025's row timing).** `ax ay az` keep the ICD's previous-frame
definition: frame k−1's k1-stage `wrench_total` force over frame k−1's mass. In row k they therefore describe
t_{k−1}, and this is disclosed rather than hidden. The derived stage passes `PreviousFrame` only when this
`Simulation` completed frame k−1 itself. That is the exchange stage's own rule, because
`Vehicle::derivative()` latches a mass without integrating. Otherwise the packer sets
`ASTRA_ICD_STATUS_ACCEL_INVALID`, which is always the case at frame 0.

## Rejected alternatives

- **Derive in Python from the logged state.** This duplicates the Euler, air-data and Earth-model math outside
  the core, including the flat versus geodetic `p_n` meaning. A scenario check could then disagree with what
  the flight software saw.
- **Publish the exchange stage's own packed struct.** It exists only with a transport, and only on exchange
  frames. Sharing it would also couple the exchange's request bytes to the logger (critique C2).
- **Compute at frame end from the new state.** This breaks ADR-0025's t_k row coherence: logged α would describe
  t_{k+1} while the wrench describes t_k.
- **Redefine `ax ay az` at t_k.** That needs the frame-k wrench before the vehicle step, which is a second
  derivative evaluation, and it would disagree with the ICD field the flight software receives.

## Consequences and validation

Every physics config gains about 25 signals and one extra `pack_sim_to_sw` per vehicle per frame (two with a
transport). Tests that assert an exact signal list are updated deliberately in PR 3, never loosened. The stage
only reads state, so no trajectory changes, with or without a transport. Rollback is reverting PR 3.

Validation, plan R10 (PR 3):
- with the loopback transport, `v0.derived.*` at frame k equal the recorder's `sim_to_sw` fields for frame k
  bitwise, `ax ay az` included;
- `git diff d3e82e3 -- core/src/kernel/lockstep_exchange.cpp` is empty;
- a loopback run's serialized recorder has the same SHA-256 built at `d3e82e3` and at the PR 3 head.

Also plan R1 (the frozen M3 regression, unchanged), R3 (α and β re-derived in numpy match within 1e-12 rad) and
R5 (no allocation). This record's own check: `python3 scripts/docs_check.py`, `git diff --check` and one Codex
review.

## Approval

Accepted by Joe, 2026-09-27.
