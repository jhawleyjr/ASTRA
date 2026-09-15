# PLAN — ASTRA v1 milestones

Authoritative acceptance criteria: `docs/architecture.md` §16. Edited in place; no task IDs.

- [x] **M0 Bootstrap** — core + one dummy plugin build under all presets; loader loads by path, rejects a bad
      `core_api_version`, static-plugins build passes the same test; ICD static asserts hold under clang-18 and
      `gcc -std=c11`; hooks fire; LSPs resolve symbols; CI runs `just check` on PRs and `main` requires it;
      ADR-0001…0011 accepted.
  - [x] PR 1 — bootstrap files, ADR-0001…0011, oracle-guard hook
  - [x] PR 2 — CMake presets, `justfile` (exports empty `PYTHONPATH`), core skeleton, plugin loader + dummy plugin,
        `icd/`, GitHub Actions (job named `check`); hook test runner over `.claude/hooks/tests/*.cases` wired into
        `just check` so "hooks fire" is verified by the gate, never by inline probes
  - [x] PR 3 — Python package (`astra._core` via nanobind, stubs), pyright/ruff/pytest, clangd config
  - [x] PR 4 — Stop hook (fast check), `/wrap` `/retro` skills, Codex review wired (`AGENTS.md`, `/codex:review`), `check` required in the ruleset
- [ ] **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free energy/angular-momentum
      drift < 1e-9 over 1000 s, |q| within 1e-12, ballistic arc closed form, RK4 vs Euler order shown, zero
      allocations over 1000 frames; health monitor halts on injected NaN and broken quaternion, naming the source.
  - [x] PR 1 — `core/util/` frames: quaternion (Hamilton, scalar-first) ↔ DCM ↔ 3-2-1 Euler, vector rotation,
        `q_nb` kinematics; `core/kernel/` `Integrator` over the 13-vector (Euler, RK4, post-step renormalize).
        Accept: ADR-0001 sign-pinning test (human-owned values, Joe reads); RK4 error ratio ≈ 16 and Euler ≈ 2
        when dt halves on a linear ODE with a closed form; |q| within 1e-12 after post-step.
        Plan review: integrator interface → Codex (`/codex:rescue`) before code. Also commits `scripts/tokens.py`,
        the per-session token script workflow.md §Model and effort measures with, so `/retro` can run it.
  - [ ] PR 2 — `Config` (yaml-cpp; typed getters, missing/unknown key errors name the path), `SignalRegistry`
        (register at init, pointers on the hot path, `v0.` namespace), `VirtualClock`, `Scheduler`, `Vehicle`,
        `Simulation` as a list of one that loads plugins named by path in the config. Accept: a config naming the
        dummy plugin runs 1000 frames with `t == frame * dt` exact and the plugin's signal visible as `v0.dummy.*`.
        Adds ctest labels `unit`/`physics`; Stop hook runs `ctest -L unit`. Also checks `scripts/tokens.py` (PR 1):
        prints one row per session and model (the Opus/Sonnet split workflow.md §Model and effort needs), and
        `--since <date>` returns rows instead of 0 against this session's own transcripts.
  - [ ] PR 3 — `EarthModel` (position kinematics, Earth rate, transport rate; `plugins/physical/earth/flat`),
        `GravityModel` (`plugins/physical/gravity/constant`, pulled forward from M2), `MassPropertiesModel`
        (`plugins/physical/mass/constant`), `Eom` derivative (Stevens & Lewis body axes) in the frame loop.
        Accept: `tests/physics/` torque-free asymmetric body, |ω| ≈ 1 rad/s, g = 0: energy and |H_n| drift < 1e-9
        relative over 1000 s at 1 kHz; ballistic arc vs closed form at two attitudes (pins n→b); symmetric-top
        precession vs closed form. Plan review: EOM and kinematics → Codex before code.
  - [ ] PR 4 — `HealthMonitor`: NaN/Inf in state or any wrench (plugin named), |q| drift > 1e-6 before
        renormalization; `Simulation::run` returns a typed halt reason with the source name, no throw in the
        loop; allocation counter (replaced global `operator new`) in the asan preset. Accept: test plugin
        injecting a NaN wrench at frame 500 halts naming it; q set to norm 1.1 halts naming `v0.state.q_nb`;
        zero allocations over 1000 frames. Plan review: health monitor → Codex before code.
- [ ] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
- [ ] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise with residual < 1e-6;
      linearized modes match design values; 300 s open-loop run reproduces the golden.
- [ ] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout.
- [ ] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test.
