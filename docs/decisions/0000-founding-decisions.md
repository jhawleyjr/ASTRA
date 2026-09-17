# ADR-0000 — Founding Decisions (2026-09-14)

Status: **accepted**. This record predates the ADR series; it is the index of decisions made in the kickoff sessions before any code existed. ADR-0001 onward are written in M0 and each cites the row here it elaborates. Repo-level details live in `docs/architecture.md`; process in `docs/process/workflow.md`.

**ASTRA — Aerospace Simulation, Test, and Research Architecture.** Code identifiers are `astra::`, `libastra_core.so`, `astra_add_plugin`, `astra_icd.h`, Python package `astra`.

## Purpose

A vehicle-agnostic, high-fidelity six-degree-of-freedom simulation **framework and architecture** focused on commercial transport aircraft. The framework ships no aircraft; a user clones it and adds their own aircraft-specific models quickly. The distinguishing requirement: **no software models of avionics in the sim** — the user's real embedded software (C++) runs against the sim, first all-digital on one machine, later hardware-in-the-loop on the actual FPGA/Jetson target.

## Decisions

| Area | Decision | Notes |
|---|---|---|
| Name | **ASTRA** | Docs written before the rename still say 6DoF in prose where they mean the concept |
| Language | C++23 core, Python bindings | Python for scripting, analysis, batch runs |
| Platform | Ubuntu 24.04 | Real-time mode: Linux PREEMPT_RT, ~1 kHz major frame, multi-rate below that |
| Toolchain | apt-only: clang-18, CMake 3.28, Eigen/HDF5/GoogleTest from apt, nanobind via pip, Python 3.12 venv, scikit-build-core, pytest, `just` | No vcpkg/Conan |
| Style | Google C++ style; `snake_case` functions/variables, `CamelCase` types, trailing-underscore members, `.hpp`/`.cpp`; clang-tidy bugprone/performance/modernize/readability minus identifier-length; Python `ruff` + `pyright` strict | |
| CI | GitHub Actions runs `just check` on every pull request on an `ubuntu-24.04` runner; branch protection requires it | From M0 |
| Use case | Both fast-as-possible all-digital AND real-time HWIL, designed in from day one | |
| Model plug-in | Code interfaces: users subclass abstract models. **Plugin architecture**: one core library, one `.so` per selectable component, all built in a single CMake invocation, loaded by path at init. Two roots: `plugins/` (shared, filed by kind: physical / hardware / sim) and `aircraft/<name>/` (one folder per aircraft, same kinds inside, copied from `_template`). No `vehicle/` kind. | Not table/config driven. Nobody ships prebuilt `.so` files. `architecture.md` §4, ADR-0009 |
| Python-defined models | Allowed in v1 via nanobind trampolines, for prototyping; refused in RealTime mode | |
| Vehicles per sim | One in v1, structured as a list of one so multi-vehicle is a v2 extension, not a rewrite | ADR-0011 |
| Time | Integer frame counter is the source of truth; `t = frame × dt` derived, never accumulated | ADR-0010 |
| Integrators | Fixed-step RK4 at 1 kHz written in-house (plus Euler for tests); Boost.Odeint allowed only in Python-side analysis tools, never in the sim loop | ADR-0004 |
| Randomness | `std::mt19937_64` with in-house Box–Muller/uniform transforms (standard distributions are not portable across libstdc++/libc++); every stochastic plugin seeded from config, seed logged | |
| Health monitoring | Pre-run, per-frame, and post-run checks in the core with warn/halt/off actions per check; defaults conservative; monitors are themselves unit-tested | `architecture.md` §15, ADR-0012 |
| Earth model | Selectable at runtime: flat (NED), round, WGS-84 rotating | Abstract Earth interface |
| Embedded SW hosting (digital) | Lockstep, separate process, shared memory. Sim owns virtual time and steps the SW; runs faster than real time. Embedded SW lives **outside** the sim source tree and is built against a HAL. | The shared-memory transport stands in for the physical link to the FPGA. Same frame/ICD layout must carry over so digital and HWIL differ only by transport |
| Embedded SW hosting (HWIL) | **The FPGA runs the embedded software** (flight control and the other LRU functions). **The Jetson is a companion computer** for the resource-intensive work an FPGA can't carry — machine learning, perception. Topology is **sim ⇄ FPGA**, with the Jetson hanging off the FPGA; the sim never talks to the Jetson directly. The sim's only HWIL boundary is the physical link to the FPGA. | Avionics buses (ARINC 429, AFDX, CAN, discretes) live between the FPGA, the Jetson, and other LRUs, out of the sim's scope; the sim's link to the FPGA is deferred |
| Systems to integrate | Everything that goes on the aircraft: FCC/FBW, FADEC, autoflight/FMS, ADIRU, actuator electronics, other LRUs | Physical models where a real unit makes no sense (e.g. IMU model, not a real IMU) |
| v1 physics scope | Core: EOM, integrators, mass properties, atmosphere, wind/turbulence, gravity, Earth models. Plus Aero and Propulsion interfaces with minimal reference implementations. | Actuators, sensors, landing gear, structural/aeroservoelastic hooks deferred until something flies |
| **v1 mission scope** | **Airborne start only.** Sim initializes at a trimmed cruise condition; no takeoff or landing, so no ground reaction model. The example "embedded software" is a **simple straight-and-level autopilot** running as its own lockstep process outside the sim tree. | Gear/ground model returns when takeoff/landing is added |
| Tooling | Trim/linearization/Monte Carlo runner; structured logging (HDF5 via C API) with Python helpers and deterministic replay; visualization bridge (FlightGear native FDM); declarative scenario + regression harness | |
| Example aircraft | Notional generic twin-engine narrow-body transport with self-consistent invented coefficients, clearly labeled notional | `aircraft/notional_transport/` |
| Distribution | Closed, non-commercial, hobby project | No licensing constraints to design around |

## Process decision

The prior 6DoF-Digital phase/task workflow is **not** copied. Pain points: too much time writing fully specified tasks before any code existed; too many hand-offs between Cowork and Claude Code sessions; brittle rules (a task pinned to an exact commit SHA refused to run after a push). The replacement is `docs/process/workflow.md`: `just check` as the single gate, Claude pushes branches and opens PRs, one Claude Code session per task, clangd and pyright LSP plugins.

## Open questions (deferred deliberately)

- The sim ⇄ FPGA physical link for HWIL (Ethernet/UDP, PCIe, or other) and its framing. Buses between the FPGA, the Jetson, and other LRUs are not the sim's concern.
- How the embedded software is expressed on the FPGA (C++ on the SoC's processor system vs. HLS vs. RTL) and whether one source tree can build both the digital-sim process and the FPGA target. The framework's stake is only that the same ICD/frame layout works in both.
- Actuator and sensor error-model interfaces
- Landing gear / ground reaction (needed when takeoff/landing enters scope)
- Structural / flexible-body hooks
- Multi-vehicle simulation (v2)
- Whether to generate `icd/` from a schema once the FPGA side exists
