# ADR-0006 — ICD as versioned C11 structs with additive `_vN` evolution

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 rows "Embedded SW hosting (digital)" and "(HWIL)"; `docs/architecture.md` §10.

## Context

The frame layout is shared between the sim (clang-18, C++23), the flight software (an FPGA-side toolchain
that may be years older), and any companion computer. Digital and HWIL runs must differ only by transport,
so the layout must be toolchain-neutral, self-describing, and never change under a peer that has not been
rebuilt.

## Decision

`icd/astra_icd.h` is plain C11 that includes only `<stdint.h>`. It defines `ASTRA_ICD_VERSION`,
`ASTRA_ICD_MAGIC` (`0x41535452`, "ASTR"), `astra_sim_to_sw_v1` (200 bytes) and `astra_sw_to_sim_v1`
(64 bytes) exactly as written in architecture §10: fixed-width types, 8-byte natural alignment, magic and
version in every frame, sizes pinned by `_Static_assert`. A layout is **never edited**. Growing the ICD
(sensor-model outputs, more surfaces, discretes) adds a `_v2` struct and bumps the version; the sim can
speak the version the other side reports. The `icd/` directory has no dependencies and is what a user's
real flight software vendors or submodules alongside `embedded/hal/`.

## Rejected alternatives

- **Generated headers from a schema (e.g. a YAML/IDL source).** Attractive once two consumers exist and
  drift is a real risk, but v1 has one C header and one consumer written by us; a generator is ceremony
  now. Revisit at v2 when the FPGA side exists (architecture §19).
- **protobuf / flatbuffers.** Variable-length encoding, a runtime library on the embedded side, and no
  fixed memory layout for a shared-memory or FPGA link. The ICD must be a struct you can `memcpy`.

## Consequences

- M0 acceptance: the static asserts hold under both clang-18 (C++23) and `gcc -std=c11`; a test target
  compiles the header both ways in every `just check`.
- Every transport (ADR-0007) and the HAL exchange these structs by value; the health monitor checks magic
  and version on every received frame and at handshake.
- Field conventions (angles in rad, positive surface directions) follow ADR-0001 and ADR-0002; the header
  comments state units because the C side has no signal registry.
