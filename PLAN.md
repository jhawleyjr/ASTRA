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
- [x] **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free energy/angular-momentum
      drift < 1e-9 over 1000 s, |q| within 1e-12, ballistic arc closed form, RK4 vs Euler order shown, zero
      allocations over 1000 frames; health monitor halts on injected NaN and broken quaternion, naming the source.
  - [x] PR 1 — `core/util/` frames: quaternion (Hamilton, scalar-first) ↔ DCM ↔ 3-2-1 Euler, vector rotation,
        `q_nb` kinematics; `core/kernel/` `Integrator` over the 13-vector (Euler, RK4, post-step renormalize).
        Accept: ADR-0001 sign-pinning test (human-owned values, Joe reads); RK4 error ratio ≈ 16 and Euler ≈ 2
        when dt halves on a linear ODE with a closed form; |q| within 1e-12 after post-step.
        Plan review: integrator interface → Codex (`/codex:rescue`) before code. Also commits `scripts/tokens.py`,
        the per-session token script workflow.md §Model and effort measures with, so `/retro` can run it.
  - [x] PR 2 — `Config` (yaml-cpp; typed getters, missing/unknown key errors name the path), `SignalRegistry`
        (register at init, pointers on the hot path, `v0.` namespace), `VirtualClock`, `Scheduler`, `Vehicle`,
        `Simulation` as a list of one that loads plugins named by path in the config. Accept: a config naming the
        dummy plugin runs 1000 frames with `t == frame * dt` exact and the plugin's signal visible as `v0.dummy.*`.
        Adds ctest labels `unit`/`physics`; Stop hook runs `ctest -L unit`. Also checks `scripts/tokens.py` (PR 1):
        prints one row per session and model (the Opus/Sonnet split workflow.md §Model and effort needs), and
        `--since <date>` returns rows instead of 0 against this session's own transcripts.
  - [x] PR 3 — `EarthModel` (position kinematics, Earth rate, transport rate; `plugins/physical/earth/flat`),
        `GravityModel` (`plugins/physical/gravity/constant`, pulled forward from M2), `MassPropertiesModel`
        (`plugins/physical/mass/constant`), `Eom` derivative (Stevens & Lewis body axes) in the frame loop.
        Accept: `tests/physics/` torque-free asymmetric body, |ω| ≈ 1 rad/s, g = 0: energy and |H_n| drift < 1e-9
        relative over 1000 s at 1 kHz; ballistic arc vs closed form at two attitudes (pins n→b); symmetric-top
        precession vs closed form. Plan review: EOM and kinematics → Codex before code.
  - [x] PR 4 — `HealthMonitor`: NaN/Inf in state or any wrench (plugin named), |q| drift > 1e-6 before
        renormalization; `Simulation::run` returns a typed halt reason with the source name, no throw in the
        loop; allocation counter (replaced global `operator new`) in the asan preset. Accept: test plugin
        injecting a NaN wrench at frame 500 halts naming it; q set to norm 1.1 halts naming `v0.state.q_nb`;
        zero allocations over 1000 frames. Plan review: health monitor → Codex before code.
- [ ] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
  - [x] PR 1 — `plugins/physical/earth/{round, wgs84_rotating}` (geodetic `p_n` per ADR-0003, Earth rate, transport
        rate; initial `p_n` stays the raw vector, radians and metres, range-checked at init with a message naming
        ADR-0003 for a degree-valued latitude and a separate message for the polar singularity margin
        `core/util/geodesy.hpp`'s `kMaxLatitude` guards against); `plugins/physical/gravity/{inverse_square,
        wgs84_j2}` reading latitude and altitude from `p_n` directly (settled by Joe, not the plan-review round:
        ADR-0003 already makes `p_n` geodetic under these Earth models, so no new position-access API). Each gravity
        model declares the position representation it assumes; `Simulation::from_config` rejects a mismatch with
        `earth/flat` (ConfigError naming `physical.gravity` / `physical.earth`). Also adds
        `cppcoreguidelines-no-malloc` (with its `Allocations` option widened past the default `::malloc;::calloc` to
        include `::aligned_alloc;::posix_memalign`) to `.clang-tidy`, NOLINT pair in `tests/support/alloc_counter.cpp`,
        closing PR #21's adversarial-review finding at the gate. Accept: round non-rotating drop vs the
        inverse-square closed form; the WGS-84 rotating sphere drop (NASA/TM-2015-218675 check-case 1, "dragless
        sphere" — the report publishes exact initial conditions but, by design, no single numeric reference or fixed
        tolerance, so the oracle is an independent high-precision integration of that exact scenario, not a number
        copied from the report) within its tolerance. Plan review (one round, done): the Earth/gravity position
        contract. **Split from the plan's contingency**: the diff exceeded ~1200 lines with `AtmosphereModel` +
        `isa1976` included, so that work moved to PR 1b below; gravity stayed with the Earth models as specified.
        **Known gap, disclosed not fixed** (Codex adversarial review, three rounds — round 3 verdict stayed
        "no-ship" on this point after rounds 1–2's findings were fixed and confirmed): `validate_p_n` checks only the
        initial state; a trajectory that *continuously approaches* (without landing exactly on) the polar margin or
        an altitude floor is not re-validated per frame, and could in principle run for a while on large-but-finite,
        degraded kinematics before anything overflows to Inf/NaN or trips the quaternion-norm-drift check (verified:
        landing *exactly* on either boundary is caught — the gap is the approach). Rejected as in-scope for this PR,
        with reasons: (a) none of this PR's own tests fly anywhere near either boundary; (b) a correct fix is a new
        HealthMonitor capability (per-frame Earth/gravity domain validity), not a bolt-on — it has to say what
        "in-domain" means for every Earth model and compose correctly with the RK4 integrator's intermediate stage
        evaluations, not just the post-step state Codex's own suggested fix checks, and that is exactly the kind of
        unpinned, expensive-to-unwind design workflow.md's plan-review trigger exists for, not something to add under
        review pressure at the tail of an already-large PR; (c) it is disclosed here and in round.cpp's comment, not
        hidden. Joe's call whether this blocks the merge; close it before any milestone flies a trajectory that can
        realistically approach a pole (not M2's drop-test scenarios) — plan-reviewed like M1's EOM and health monitor.
  - [ ] PR 1b — `AtmosphereModel` + `plugins/physical/atmosphere/isa1976` (declares its valid altitude range; §15's
        pre-run "altitude inside the atmosphere's range" check joins the HealthMonitor init path); the same
        position-representation check extended to `physical.atmosphere`. Accept: ISA table points exact.
  - [ ] PR 2 — `WindModel` (§7 signature) with `plugins/physical/wind/{uniform, dryden}` (seeded `std::mt19937_64`,
        in-house transforms, seed logged). Accept: Dryden PSD matches the MIL-F-8785C spec within statistical bounds
        at a fixed seed; steady-wind NESC cases within tolerance; bit-identical logs across two same-seed runs.
  - [ ] PR 3 — NESC check-case harness: cases 1–10 from YAML under `tests/physics/nesc/` (absorbing PR 1's
        hand-written case 1 into the harness); a `tests/plugins/` `WrenchSource` for the sphere's Cd·A drag and the
        brick's damping (the only M2 consumer of wind and atmosphere, air-relative velocity). Reference trajectories
        and per-channel tolerances under `tests/golden/nesc/` are human-owned: Joe places NASA's TM-2015-218675 data
        files and reads the proposed tolerances with the guard lifted (PR 1's case 1 already shows the report itself
        publishes no fixed tolerance, only initial conditions — decide per case whether an independent integration
        or a cross-tool comparison band is the right oracle). Decide from the report whether any case needs a
        rotating spherical Earth (`rotating: true` on `round`, per §4 granularity). Accept: all ten within tolerance
        under every preset.
- [ ] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise with residual < 1e-6;
      linearized modes match design values; 300 s open-loop run reproduces the golden.
- [ ] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout.
- [ ] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test.
