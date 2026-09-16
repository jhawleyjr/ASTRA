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
        Scheduled as PR 2c under ADR-0012, not open-ended.
  - [x] PR 1b — `AtmosphereModel` + `plugins/physical/atmosphere/isa1976` (declares its valid altitude range
        `[-5000, 86000]` m geometric; §15's pre-run "altitude inside the atmosphere's range" check joins
        `Simulation::from_config` beside PR 1's `validate_p_n` calls, not `HealthMonitor::init` -- that class only
        parses `sim.health` and registers signals). `physical.atmosphere` is optional, unlike the three required
        Earth/gravity/mass slots; no position-representation check (`at(double altitude_m)` never sees `p_n`, so no
        conforming atmosphere model could assume one to mismatch). ISA-1976's eight golden table points are
        transcribed straight from NASA-TM-X-74335 (NOAA-S/T 76-1562) and checked to printed precision, separately
        from a dense computed-and-cross-checked set generated in Python
        (`tests/golden/atmosphere/generate_isa1976_table.py`) that also verifies the seven layer base pressures
        against the document's own tabulated values. Accept: ISA table points exact.
        **Known gap, disclosed not fixed** (Codex adversarial review, confirmed on re-review): PR 1's per-frame
        Earth/gravity domain-validity gap (above) now also covers `physical.atmosphere` -- the altitude check is
        init-time only, so a trajectory that leaves `[-5000, 86000]` m mid-run is not re-validated. Concretely:
        `at()` then returns an all-NaN `AtmoState`, but `HealthMonitor` checks state vectors and wrench
        contributions directly, not `AtmoState` itself, so nothing observes the NaN until (from PR 3 on) a
        `WrenchSource` reads atmosphere state into a force/moment it hands `check_wrench`. Rejected as in-scope for
        this PR for the same reasons as PR 1's gap: no test here flies near the boundary, and a correct fix is the
        same new per-frame domain-validity `HealthMonitor` capability, not a second bolt-on. Must be resolved, or
        consciously re-accepted, before PR 3's drag `WrenchSource` is the first thing that can actually read a NaN
        `AtmoState` into the EOM. Scheduled as PR 2c under ADR-0012, not open-ended.
  - [x] PR 2 — `WindModel` (§7 signature, strengthened to `const noexcept` plus `begin_frame`/
        `shareable_across_vehicles`) with `plugins/physical/wind/uniform` and the per-vehicle
        `EnvironmentSample` latch (`core/include/astra/kernel/environment_sample.hpp`) that carries wind
        (and, from PR 3, atmosphere) to a `WrenchSource` -- the wind injection point `CLAUDE.md`'s M2
        trigger list named, one `/codex:rescue` plan-review round (folded: the `EnvironmentAvailability`
        contradiction Codex's round caught; a real correction to the plan's own reasoning -- `wind_n`
        shipped `const noexcept`, not non-const, because a reference member does not propagate the
        enclosing object's constness to the referent, so `Eom::operator() const` was never the barrier
        an earlier draft claimed; `WindModel::begin_frame(ctx, vs, altitude_m)` is the per-vehicle
        evolution hook that design needed, since `Model::step` has no `VehicleState` to give a stateful
        model the true airspeed/altitude MIL-F-8785C ties its coefficients to). The post-implementation
        `adversarial-review` round caught one more: `begin_frame`'s first draft passed only `VehicleState`,
        so a stateful model reading `vs.p_n()(2)` for altitude would get opposite meanings under
        `earth/flat` (NED down) and `earth/round`/`wgs84_rotating` (geodetic altitude) -- the same
        representation trap ADR-0003/`PositionRepresentation` already prevents for gravity models, and the
        one `AtmosphereModel::at()` avoids by taking a plain `altitude_m` rather than a `VehicleState` at
        all. Fixed before merge: `begin_frame` takes an explicit Earth-derived `altitude_m`
        (`EarthModel::altitude(vs)`, computed in `Simulation::step`, which is why `Simulation` now also
        holds a non-owning `EarthModel*` alongside `WindModel*`), so no `WindModel` ever reads `vs.p_n()`
        for altitude -- this is also this PR's own **Known gap, disclosed not fixed**: it is the first
        per-frame caller of `EarthModel::altitude()` (`origin/main`'s only caller was the init-time
        `physical.atmosphere` `valid_range()` check); PR 1's/PR 1b's disclosed per-frame domain-validity
        gap (initial state validated, not every frame) was written before `altitude()` had any per-frame
        caller, and now does, with a `WindModel` (from PR 2b, `dryden`) reading that per-frame value. The
        decision PR 1b deferred is re-accepted knowingly here, not silently inherited (Cowork review,
        post-merge-ready) -- see PR 2b's line for the consequence; scheduled as PR 2c under ADR-0012,
        not open-ended. Also caught and fixed: the
        `WrenchSource` ABI change (`requires_environment()` added, `wrench_b`'s signature changed) needed
        `kCoreApiVersion` bumped 1 -> 2 (`core_api.hpp.in`) so a stale out-of-tree plugin is rejected at
        load rather than dispatched through a vtable slot it does not have, bumped again 2 -> 3 once
        `WindModel`'s own vtable settled after two further adversarial-review rounds. **Second known gap,
        disclosed not fixed** (Cowork review, post-merge-ready): `HealthMonitor` has no environment check
        (only `check_wrench`/`check_state`), so a `WindModel` returning a NaN `wind_n` reaches a
        `WrenchSource` through `EnvironmentSample` and halts naming the *consumer's* plugin path, not the
        wind plugin that produced the NaN -- contrary to §15's "with the offending plugin named", the
        same shape as `AtmoState`'s own disclosed gap (PR 1b). Disclosed in `environment_sample.hpp`'s
        `wind_n` field comment; see PR 3's line, whose drag `WrenchSource` is the first thing that can
        carry such a NaN into the EOM. `physical.wind` is optional, like
        `physical.atmosphere`; rejects a `divisor` other than 1 (`begin_frame`/`wind_n` are sampled every
        frame by `Simulation::step` regardless of the model's own scheduler divisor, and no hold contract
        exists yet for a coarser rate). `Vehicle::init` rejects a `WrenchSource` whose
        `requires_environment()` (`wrench_source.hpp`) asks for wind with none configured, naming
        `physical.wind`; rejects one asking for atmosphere unconditionally, naming `physical.atmosphere`
        -- no PR before PR 3 populates `EnvironmentSample::atmosphere`, so this is deliberately not "if
        `physical.atmosphere` is absent." `Simulation::from_config` rejects more than one vehicle paired
        with a non-shareable `WindModel` (`ADR-0011`); currently unreachable (`from_config` builds exactly
        one `Vehicle`), tested at the predicate level only. `tests/plugins/{counting_wind, wind_consumer,
        atmosphere_consumer}` make the call contract and the two rejections assertable rather than only
        claimed. Accept (this PR's own, narrower than M2's row -- see below): `begin_frame`/`wind_n` each
        called exactly once per vehicle per frame, at frame-start state, before the wrench stage; the
        sampled wind reaches a `WrenchSource` through `EnvironmentSample`; two same-config runs are
        bitwise identical (`std::bit_cast<uint64_t>`) on the sampled `wind_n` sequence and on every
        `SignalRegistry` value -- plumbing coverage for the latch, not evidence that seeded stochastic
        evolution is itself deterministic (there is no logger until M5, and §16 puts "bit-identical log
        across two runs" under M4; this is PR 2's honest substitute, not a claim on either). `just check`
        exits 0 on every preset. **Does not by itself satisfy M2's own row above**: the Dryden PSD
        criterion moves to PR 2b's line; the steady-wind NESC criterion moves to PR 3's line, since
        `WindModel` had no `WrenchSource` consumer until PR 3's drag model exists.
        **Known limitation, disclosed not fixed** (Codex adversarial review, three rounds --
        `WindModel::assumed_position_representation()` added in round 2 to close round 1's finding
        that `begin_frame`/`wind_n` handing a `WindModel` the full `VehicleState` let a plugin misread
        `p_n`'s Earth-model-dependent meaning; round 3 correctly pointed out that the declaration is
        not enforced -- a plugin can declare `kAny` while still reading `vs.p_n()` internally, and
        nothing catches the mismatch): not a gap this PR introduced --
        `GravityModel::assumed_position_representation()` (M2 PR 1) has had exactly the same property
        since it shipped, and no C++ interface in this shape can do better without either dropping
        `VehicleState` from `wind_n` below what §7 pins for its signature, or a materially heavier
        capability/audit mechanism that is its own unpinned, expensive-to-unwind design decision, not
        something to fold in reactively under review pressure. Disclosed in `wind_model.hpp`'s own
        comment on `assumed_position_representation()`. Joe's call whether this blocks the merge, same
        framing as PR 1's polar-approach gap and PR 1b's WGS-84/ISA datum gap.
        **Second Cowork re-review round, of the merged-ready branch's own fixes** (three findings,
        comment/behavior only -- no new validation added, per the same disclosure-over-new-mechanism
        posture as the two gaps above): (1) `Simulation::for_testing`'s doc comment claimed it wires
        vehicles "the same way `from_config` wires the single vehicle it ever builds," true of
        scheduling/topology and false of validation (`check_position_representation`,
        `earth`/`gravity->validate_p_n`, the atmosphere `valid_range` check, and
        `check_sampled_every_frame` are all `Config`-driven and `for_testing` takes live model
        pointers, not `Config` -- there is nothing to hang them off, and a test needing an
        otherwise-invalid state to reach `step()` is a legitimate use of the helper). Named as a
        class in the comment now, not fixed instance by instance again: this was the third such
        from_config/for_testing divergence this PR found by omission (wind's divisor guard existed
        without atmosphere's; wind was scheduled without earth/gravity; now this). (2) `frames_` was
        never set by `for_testing`, so `sim.run()` with no argument silently ran zero frames;
        fixed by adding a `frames` parameter (default 0, matching a freshly constructed `frames_`)
        rather than only documenting the no-op, since every other `for_testing` parameter already
        mirrors a `from_config` concept and leaving this one silently absent was the more surprising
        choice -- a new test (`RunWithNoArgumentStepsTheFramesPassedToForTesting`,
        `tests/unit/wind/wind_test.cpp`) proves it. (3) `Simulation::for_testing` is a public static
        on the shipped `Simulation` class in `libastra_core`, not compiled out -- accepted
        explicitly, not gated behind a test-only build or moved to a friend function outside the
        class: both would only relocate which header shows the same link-visible symbol, and
        inventing a test-only build-mode split for one static factory is exactly the kind of new
        mechanism the disclosure-over-new-design posture above already rejected for smaller gaps.
        What actually stops production code from calling it today: nothing at the compiler/linker
        level -- the guard is the name and its doc comment (`simulation.hpp`), the same posture the
        pre-existing non-const `vehicles()` overload already takes. Circumstantially, nothing
        production-facing can reach it yet either: `Simulation` has no Python binding at all
        (`python/bindings/_core.cpp` binds `PluginDescriptor`/`core_api_version` only), and no
        executable outside `tests/` links `libastra_core` and calls into `Simulation`. That
        circumstance is not load-bearing and would not survive a future Python binding or driver
        executable being added without re-litigating this -- Joe's call whether that is acceptable
        risk for a v1 test helper or worth a real gate later.
        **The re-run `adversarial-review` this round found a real bug, not a re-raise** (`high`):
        `for_testing`'s `has_physics` was computed from `gravity`/`mass_spec` alone, so a caller
        supplying both without `earth` built a `Vehicle::PhysicsSpec` holding a null `EarthModel*`,
        which `Vehicle::init` then dereferenced unconditionally (`eom_.emplace(*physics->earth,
        ...)`, `vehicle.cpp`) -- undefined behavior, confirmed by UBSan itself
        ("reference binding to null pointer") when the fix was reverted to check the new tests have
        teeth. Fixed: `earth`, `gravity`, and `mass_spec` are now required together or rejected
        (`astra::Error`, not `ConfigError` -- `for_testing` takes no `Config` to name a path in);
        supplying exactly one of `gravity`/`mass_spec` is rejected too, closing the same review's
        second observation that a partial pair silently built a no-physics `Simulation` instead of
        using what was passed. **A second adversarial-review round on this same fix caught a
        related miss, and corrected a wrong call made resolving the first round**: this PR's own
        first draft response left a non-null `wind` with a null `earth` disclosed but unenforced,
        reasoning it was the same shape as `check_position_representation`/`validate_p_n`/the
        atmosphere `valid_range` check (deliberately left unenforced above) and that enforcing it
        would break `RejectsMoreThanOneVehicleWithANonShareableWindModel`'s null-`earth` fixture.
        Both parts of that reasoning were wrong: `Simulation::step`'s wind branch dereferences
        `earth_` unconditionally whenever `wind_` is non-null (`earth_->altitude(vs)`), inside
        `step()`'s own `noexcept` -- the same crash shape as the `gravity`/`mass_spec`/`earth` bug
        just fixed, not a "plausible but domain-wrong answer" one, and reachable with a single
        vehicle, not only through the D4 guard. Fixed instead: `wind` without `earth` is now
        rejected the same way; the shareability fixture was given a real `EarthModel` (it does not
        need a null one to prove what it proves) rather than left as the reason to under-enforce.
        Four new tests across both fixes. Not a gap this PR introduced with new state, but a bug
        that existed in `for_testing` since the prior round added it, reachable only because a
        re-run adversarial review, not the original Cowork list, went looking for it -- caught
        before merge because `CLAUDE.md` requires the re-run, and caught in full only because that
        re-run was itself re-run once more against its own fix.
  - [x] PR 2b — `core/util/random.hpp` (`std::mt19937_64` seeded from config, in-house Box-Muller/uniform
        transforms per `AGENTS.md` -- `std::normal_distribution` is unspecified across libstdc++/libc++)
        and `plugins/physical/wind/dryden` (MIL-F-8785C scale lengths and intensities), built on PR 2's
        `begin_frame`/`wind_n` split: `step()` draws the frame's RNG samples (scheduler-driven, consumer-
        count-independent, §2); `begin_frame(ctx, vs, altitude_m)` advances the shaping filter using the
        vehicle's REAL per-frame true airspeed (from `vs`) and altitude (`altitude_m`, Earth-derived, never
        read from `vs.p_n()` -- PR 2's own disclosed representation fix) -- not a configured nominal --
        which is the entire reason `begin_frame` takes a `VehicleState` at all. Consequence, decided here
        rather than left to discover mid-PR: MIL-F-8785C's PSD is a stationary-process spectrum, meaningful
        only at fixed `V` and `h`, so the PSD acceptance test must drive `begin_frame`/`wind_n` with a
        synthetic, held-constant `VehicleState` across its sample count, never a real trajectory. Reuses PR
        2's bitwise determinism harness (`tests/unit/wind/wind_determinism_test.cpp`) against `dryden`, where
        a mismatch is diagnostic of a real bug rather than trivially passing (PR 2 shipped it against
        `uniform`, a constant model, as plumbing coverage only). **Inherits PR 2's disclosed per-frame
        `EarthModel::altitude()` domain-validity gap**: `altitude_m` is `dryden`'s own coefficient input,
        read every frame from a `p_n` that is not re-validated per frame (PR 1's/PR 1b's gap, re-accepted
        knowingly by PR 2, not fixed here either); scheduled as PR 2c under ADR-0012, not open-ended.
        Accept: Dryden PSD matches the
        MIL-F-8785C spec within statistical bounds at a fixed seed (docs/architecture.md §16's M2 row);
        two same-seed runs are bitwise identical on the gust sequence and the registry snapshot, and
        across `reset()`.
  - [ ] PR 2c — per-frame domain validity (ADR-0012): `EarthModel::in_domain` /
        `GravityModel::in_domain`, `validate_p_n` reimplemented as that plus input sanity,
        `HealthCheck::kOutOfDomain` under `sim.health`'s `domain` key (default halt), checked
        post-step against the state and a one-frame lookahead, plus the atmosphere `valid_range()`
        check against `EarthModel::altitude`. Closes PR 1's polar-approach gap and PR 1b's mid-run
        atmosphere-range gap; does not close PR 1b's geoid datum gap. Accept: a trajectory driven
        toward `kMaxLatitude` halts naming the Earth plugin at least one frame before any state goes
        non-finite; same for the altitude floor; a run leaving `valid_range()` mid-run halts naming
        `physical.atmosphere`; an east-west trajectory crossing longitude pi does not halt;
        `domain: off` restores current behavior; zero allocations over 1000 frames.
  - [ ] PR 3 — NESC check-case harness: cases 1–10 from YAML under `tests/physics/nesc/` (absorbing PR 1's
        hand-written case 1 into the harness); a `tests/plugins/` `WrenchSource` for the sphere's Cd·A drag and the
        brick's damping (the only M2 consumer of wind and atmosphere, air-relative velocity; steady-wind NESC cases
        within tolerance, moved from PR 2's line since PR 2 shipped no consumer). Populates
        `EnvironmentSample::atmosphere` in `Simulation::step` (`core/src/kernel/simulation.cpp`) in the same change
        that adds the `atmosphere` field back to `Vehicle::EnvironmentAvailability` (`core/include/astra/kernel/
        vehicle.hpp`) -- the two ship together, not as a flag some later PR has to remember to flip. Also decides
        the varying-wind model docs/architecture.md §16's M2 row implies ("steady **and varying** wind"): neither
        `uniform` nor `dryden` produces a commanded time-varying mean -- choose between a
        `plugins/physical/wind/table` plugin and a `tests/plugins/` one. `WrenchSource::wrench_b` needs its own
        `StepContext` latch (`step()`, mirroring `tests/plugins/nan_wrench`'s existing pattern) to hand `wind_n`
        one, since `wrench_b(vs, env)` itself carries none. **Also the first thing that can carry a NaN
        `wind_n` into the EOM** (PR 2's disclosed gap: `HealthMonitor` has no environment check, so a NaN
        reaching a `WrenchSource` via `EnvironmentSample` halts naming the consumer, not the wind plugin),
        the same way it already is for a NaN `AtmoState`. Reference trajectories
        and per-channel tolerances under `tests/golden/nesc/` are human-owned: Joe places NASA's TM-2015-218675 data
        files and reads the proposed tolerances with the guard lifted (PR 1's case 1 already shows the report itself
        publishes no fixed tolerance, only initial conditions — decide per case whether an independent integration
        or a cross-tool comparison band is the right oracle). Decide from the report whether any case needs a
        rotating spherical Earth (`rotating: true` on `round`, per §4 granularity). Accept: all ten within tolerance
        under every preset. **Also adds** the transcribed-vs-fresh geopotential consistency check PR 1b's own
        generator left unclaimed: assert `geopotential_from_geometric(Z)` matches each row's transcribed H column
        within 0.5 m, catching a geometric/geopotential column swap at generation time — the highest-risk
        transcription error in the NESC tables too (same class of mistake as `docs/lessons.md`'s 2026-09-15
        entry on NASA-TM-X-74335's dual-column Table I). **Carried from PR 1b (Codex adversarial review, disclosed not fixed):**
        `wgs84_rotating`'s `EarthModel::altitude()` returns height above the WGS-84 ellipsoid (ADR-0003's `p_n(2)`),
        not a sea-level/geoid reference `isa1976` implicitly assumes; the two differ by the geoid undulation
        (roughly -105 to +85 m), bounding the resulting density error at about 1.3% via the barometric
        approximation. Any case here using `wgs84_rotating` + `isa1976`'s drag `WrenchSource` must decide whether
        that is inside the case's own NESC tolerance; a fix needs a geoid model (EGM96/EGM2008), its own
        plan-reviewed PR, not a bolt-on. Case 1 (dragless) never exercises this.
- [ ] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise with residual < 1e-6;
      linearized modes match design values; 300 s open-loop run reproduces the golden.
- [ ] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout.
- [ ] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test.
