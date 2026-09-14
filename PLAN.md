# PLAN — ASTRA v1 milestones

Authoritative acceptance criteria: `docs/architecture.md` §16. Edited in place; no task IDs.

- [ ] **M0 Bootstrap** — core + one dummy plugin build under all presets; loader loads by path, rejects a bad
      `core_api_version`, static-plugins build passes the same test; ICD static asserts hold under clang-18 and
      `gcc -std=c11`; hooks fire; LSPs resolve symbols; CI runs `just check` on PRs and `main` requires it;
      ADR-0001…0011 accepted.
  - [x] PR 1 — bootstrap files, ADR-0001…0011, oracle-guard hook
  - [ ] PR 2 — CMake presets, `justfile` (exports empty `PYTHONPATH`), core skeleton, plugin loader + dummy plugin,
        `icd/`, GitHub Actions (job named `check`); hook test runner over `.claude/hooks/tests/*.cases` wired into
        `just check` so "hooks fire" is verified by the gate, never by inline probes
  - [ ] PR 3 — Python package (`astra._core` via nanobind, stubs), pyright/ruff/pytest, clangd config
  - [ ] PR 4 — Stop hook (fast check), `/task` `/wrap` `/retro` skills, `check` required in the ruleset
- [ ] **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free energy/angular-momentum
      drift < 1e-9 over 1000 s, |q| within 1e-12, ballistic arc closed form, RK4 vs Euler order shown, zero
      allocations over 1000 frames; health monitor halts on injected NaN and broken quaternion, naming the source.
- [ ] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
- [ ] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise with residual < 1e-6;
      linearized modes match design values; 300 s open-loop run reproduces the golden.
- [ ] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout.
- [ ] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test.
