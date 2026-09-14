# ADR-0008 — Monorepo, with `embedded/` as a separate CMake project inside it

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Embedded SW hosting (digital)" ("lives outside the sim source tree"); `docs/architecture.md` §4 end, §11.

## Context

The example autopilot must demonstrate "embedded software outside the sim": it may depend only on the ICD
and a HAL, never on `astra_core`, and must stay buildable by an older embedded toolchain. At the same time
one command should build everything for the M4 lockstep test, and the ICD and HAL must not drift from the
sim that consumes them.

## Decision

One git repository. `embedded/` is a separate CMake project (`project()` of its own, C++17 deliberately)
that the top-level build pulls in with `add_subdirectory` so `just build` produces the sim, every plugin,
and the autopilot in one invocation. `embedded/` links only `icd/` and `embedded/hal/`; a link dependency
on `astra_core` is a build error. A user's real flight software lives in its own repo and vendors or
submodules `icd/` and `embedded/hal/`; the framework's promise to that user is only that the frame layout
and the HAL calls are stable within an ICD version.

## Rejected alternatives

- **Separate repository from day one.** Enforces the boundary by distance, but the ICD would need
  versioned publishing before a single frame has crossed it, and every M4 change would touch two repos and
  two PRs. The boundary is enforced instead by the CMake link rule and a symbol check; splitting later is a
  `git subtree` operation, not a rewrite.

## Consequences

- `embedded/CMakeLists.txt` must be buildable standalone (`cmake -S embedded`) with only a C++17 compiler;
  CI builds it both ways.
- A test asserts that the autopilot binary imports no `astra_core` symbols.
- The Python driver launches the autopilot as a subprocess for M4; nothing in `core/` knows its path.
