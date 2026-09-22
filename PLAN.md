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
- [x] **M2 Environment** — ISA-1976, gravity (constant / inverse-square / WGS-84 J2), wind + Dryden, Earth
      (flat / round / WGS-84 rotating); NASA check cases 1–10 within NESC tolerances; ISA table points exact.
      **All ten cases pass under all four presets** (debug/release/static-plugins/asan), confirmed by PR
      3c's own `just check` run, 2026-09-19 — closing PR 3c's `dt`-independent fix (ADR-0016) to the
      frozen-wrench gap PR 3b disclosed for cases 4/5.
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
        **Closed by PR 2c** (`EarthModel::in_domain`/`GravityModel::in_domain`, checked post-step with a
        one-frame lookahead, ADR-0012).
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
        `AtmoState` into the EOM. **Closed by PR 2c** (post-step altitude-vs-`valid_range()` check in
        `Simulation::step`, ADR-0012).
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
        post-merge-ready) -- see PR 2b's line for the consequence; **closed by PR 2c** (ADR-0012). Also
        caught and fixed: the
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
        knowingly by PR 2, not fixed here either); **closed by PR 2c** (ADR-0012).
        Accept: Dryden PSD matches the
        MIL-F-8785C spec within statistical bounds at a fixed seed (docs/architecture.md §16's M2 row);
        two same-seed runs are bitwise identical on the gust sequence and the registry snapshot, and
        across `reset()`.
  - [x] PR 2c — per-frame domain validity (ADR-0012): `EarthModel::in_domain` /
        `GravityModel::in_domain` (also `gravity/wgs84_j2`, whose own `validate_p_n` was never a
        no-op -- ADR-0012's split applies to it too), `validate_p_n` reimplemented as that plus input
        sanity, `HealthCheck::kOutOfDomain` under `sim.health`'s `domain` key (default halt), checked
        post-step against the state and a one-frame lookahead, plus the atmosphere `valid_range()`
        check against `EarthModel::altitude` in `Simulation::step`. `Vehicle::integrate` also runs the
        same check pre-step on frame 0 only, since a frame's pre-step state is every later frame's
        already-checked post-step state but frame 0's own RK4 stages have no earlier check behind
        them -- closing that gap rather than leaving it open. Closes PR 1's polar-approach gap and PR
        1b's mid-run atmosphere-range gap (both re-accepted by PR 2/PR 2b, closed here); does not
        close PR 1b's geoid datum gap. **Also caught and fixed** (Codex review): `in_domain` changes
        `EarthModel`/`GravityModel`'s vtable layout, so `kCoreApiVersion` bumped 3 -> 4, the same
        reasoning as PR 2's own 1 -> 2 -> 3 (`core_api.hpp.in`). Accept: a trajectory driven toward
        `kMaxLatitude` halts naming the Earth plugin at least one frame before any state goes
        non-finite; same for the altitude floor; a run leaving `valid_range()` mid-run halts naming
        `physical.atmosphere`; an east-west trajectory crossing longitude pi does not halt; `domain:
        off` restores current behavior; zero allocations over 1000 frames.
        **Known gap, disclosed not fixed** (Codex adversarial review, this PR): `check_domain`'s
        lookahead, `p_n + dt * p_n_dot(state)`, is first-order in velocity. RK4's own stages nest
        acceleration into a position estimate at `0.5 * |a| * dt^2` -- ~4.9 um at standard gravity
        and ADR-0010's fixed `dt = 1 ms` (pinned by a unit test against the real `Eom`+`Integrator`,
        not a hand-derived duplicate, so this number moves visibly, not silently, if `dt` or the
        integrator ever changes -- it scales as `dt^2`). A state within that ~5 um band of a domain
        boundary, **and** with near-zero velocity toward it (a nonzero closing velocity is exactly
        what the lookahead's own `dt * p_n_dot` term already predicts and catches a frame early),
        can have an RK4 stage evaluate the singular math before `check_domain`'s post-step check
        ever runs. Inside that band, ADR-0012's own "halts naming the [Earth/gravity] plugin at
        least one frame before any state vector goes non-finite" is not strictly met: the run may
        instead halt one frame later, naming the generic `kNonFiniteState` check M1 PR 4 already
        has, not `kOutOfDomain`. That check remains the backstop regardless -- nothing escapes
        undetected, only the halt's timing and naming degrade inside the band. ADR-0012's own
        Rejected Alternatives names exactly this class ("if a future model has a domain a single
        frame can jump clean across, this decision is superseded, not patched"); shown here to also
        apply, within this band, to `earth/round` + `gravity/constant` today, not only a
        hypothetical future model. Not fixed here: before this PR a continuously approaching
        trajectory ran on degraded kinematics indefinitely with no per-frame check at all (PR 1's
        own gap); after it, that is caught except possibly one frame late and misnamed, in a band
        requiring both the ~5 um proximity and near-zero closing velocity together -- a strict
        improvement, and blocking on the smaller gap to avoid it would leave the larger one. A
        margin-based or acceleration-aware lookahead is an unpinned, expensive-to-unwind design
        (ADR-0012 pins the literal Euler formula) and needs its own plan-reviewed PR, not a
        redesign under review pressure here.
  - [x] PR 3a — populates `EnvironmentSample::atmosphere` in `Simulation::step` (one shared
        `earth_->altitude(vs)` call feeds both `wind_n` and `atmosphere`, independent of which slot is
        configured, never clamped outside `valid_range()`); `Vehicle::EnvironmentAvailability::atmosphere`
        (conditional rejection naming `physical.atmosphere`, replacing PR 2's unconditional one, since
        nothing before this PR connected the two); `plugins/physical/earth/round`'s `rotating: <bool>`
        (default false; the same `omega_ie_n(lat)` formula `wgs84_rotating` uses unconditionally -- case 5
        needs it, Table 6.3-1 pairing case 4 fixed / case 5 rotating with 1/R^2 to isolate Earth rotation);
        new `plugins/physical/wind/table` (piecewise-linear wind vs. altitude from a config point table,
        clamped not extrapolated outside its own range, `shareable_across_vehicles() == true` despite
        holding per-frame state since it is memoryless, `assumed_position_representation() == kAny` --
        settles docs/architecture.md §16's "steady **and varying** wind" per ADR-0013: case 8's wind is
        altitude-keyed, not time-varying, so no `WrenchSource`-side `StepContext` latch is needed); new
        `tests/plugins/nesc_aero` (`WrenchSource`: sphere drag `F_b = -C_D*0.5*rho*S*|v_rel_b|*v_rel_b`
        and brick rate damping `L/M/N = 0.25*rho*V*S*{b,cbar,b}^2*C_{lp,mq,nr}*{p,q,r}` using the
        inertial-referenced body rates `VehicleState::omega_b` already stores; both forms checked against
        NASA/TM-2015-218675 Vol II's own published values, case 7 `sim_03`'s drag at t=0 and case 3
        `sim_04`'s damping at t=10/30 s; `requires_wind` is config-driven, not unconditional, since eight
        of the ten NESC cases correctly configure no `physical.wind` at all -- caught in plan review
        before writing any config, since `Vehicle::init` calls a `WrenchSource`'s own `init()`, so it has
        its config, before querying `requires_environment()`). No `kCoreApiVersion` bump: no plugin-facing
        ABI change. **Split from this PR's own original scope** (~1750 combined lines; PR 1's own
        precedent split at ~1200): PR 3a is the consumer half `just check` proves with no goldens, no
        guard, no NESC data; PR 3b below is the harness. Codex review: clean. Adversarial review
        (ADR-0001/0002/0003/0011): one needs-attention finding, atmosphere's NaN reaching a `WrenchSource`
        misattributed to the consumer, not the atmosphere plugin -- the identical shape wind's own already-
        disclosed gap has (disclosed, not fixed, `environment_sample.hpp`/`atmosphere_model.hpp`; PR
        1/1b/2's precedent for this exact class of gap says the real fix is one new per-frame domain-
        validity `HealthMonitor` capability validating `EnvironmentSample` as a whole, not a per-field
        bolt-on). PR #29.
  - [x] PR 3b — the ten-case harness: YAML under `tests/physics/nesc/` (absorbing PR 1's hand-written
        case 1); `tests/golden/nesc/generate_nesc_band.py` (human-owned, never run by `just check`): SI
        conversion with Vol II Table 73's exact forms, 10 Hz grid by nearest-sample selection within 1e-6 s
        (`sim_05` decimated x10), per-file header-name column keying (column counts vary by SIM FILE, not
        by case -- 22/27/31/32/37/38 across the ten cases' 54 files; ADR-0013's own "cases 4 and 5 publish
        27" describes two specific participants' files, not every file in those cases), a `sim_05`
        duplicate-`feVelocity_ft_s_Z`-column assert, Vol II Tables 77/78's own exclusions (SIM 2's Euler
        angles and aero moments in every case, SIM 2's X-force in cases 4-5, SIM 3's Y-force in cases 6-8),
        plus a new t=0-only exclusion of `sim_06` (both wind cases) and `sim_03` (case 8) from every
        airspeed-derived channel -- both publish a physically-wrong zero airspeed at their own first
        sample in a wind case, which would otherwise set a case's whole tolerance band from one bad
        instant (case 8's `Fy` band alone would widen 1.7x). **Settled: ADR-0014**, elaborating ADR-0013
        with the second judgment number its formula doesn't have -- a per-channel-kind resolution floor
        (`tol_abs = max(k*max_t(spread), floor)`; 1e-12 rad, 1e-9 m, 1e-9 m/s, 1e-12 rad/s, 1e-12 N/N.m, or
        1e-12 relative for density/pressure/temperature/speed of sound) so a channel analytically zero for
        its case (case 1's latitude, every aero force in cases 1-3) does not fail on the ensemble's own
        floating-point roundoff -- checked, not merely asserted, against every degenerate channel in the
        data (the tightest genuinely non-zero band found, case 9's yaw, sits 20x above the floor).
        **Settled: the sampling fix** -- `wrench_b` is called on the PRE-step state, so a plain
        `sim.run(100)` per sample leaves force/moment signals one frame stale (up to 3.4% of the k=3 band
        on case 3's moment N); `nesc_cases_test.cpp` instead reads state, `sim.run(1)` (using the state
        just read), reads wrench, then `sim.run(99)` to the next sample (skipped after sample 300) --
        30,001 frames total, not 30,000. **Settled: case 8's `wind/table` nodes at 0 and 40,000 ft**, not
        0/30,000 -- the case starts exactly ON a 30,000 ft node (its own fall distance is 4178.5 m, not
        case 1's dragless 4389 m), so 0/40,000 keeps the flown band strictly interior and the clamp policy
        unexercised by any of the ten cases. **Settled: no geoid model needed** -- measured this session by
        inverting every sim's own ECEF to geodetic: NESC's `altitudeMsl_ft` column IS ellipsoidal height
        for all ten cases (<=6.1e-5 m difference for the eight WGS-84 cases; cases 4/5 are height above
        Table 73's own sphere, matching their spherical Earth model exactly), so `wgs84_rotating::
        altitude()` feeding `isa1976` unchanged is correct; the disclosed geoid gap in
        `atmosphere_model.hpp` stays disclosed as a general property of `wgs84_rotating`, not a blocker
        here (a future case with a real MSL reference gets its own plan-reviewed PR with a geoid model).
        Also adds the transcribed-vs-fresh geopotential consistency check PR 1b's own generator left
        unclaimed (`abs(geopotential_from_geometric(Z) - transcribed_H) <= half_ulp(0)`, i.e. 0.5 m; worst
        residual measured 0.281 m) -- the same class of mistake as `docs/lessons.md`'s 2026-09-15 entry on
        NASA-TM-X-74335's dual-column Table I. Config gotchas: `inverse_square`'s own defaults do not
        reproduce cases 4/5 (Table 73's sphere radius 6371007.1809 m and mu=3.986004418e14 give
        `g0 = mu/R^2 = 9.820228349842926`, not standard gravity); `omega_b` in the configs is inertial-
        referenced, not the Earth-relative values Tables 25-34 print; Earth rate is Table 73's
        7.292115e-5 rad/s, not the initial-conditions workbook's rounded back-conversion;
        `localGravity_ft_s2` is never a compared channel (it is pure Newtonian gravitation, not the
        apparent gravity -- with the centrifugal correction -- `wgs84_j2::g_n()` correctly computes for
        the EOM). Precondition on Joe's machine: `touch .claude/touch-goldens` before ANY file under
        `tests/golden/` or `docs/decisions/` (the new ADR-0014) can be written -- the generator itself
        lives under the guarded `tests/golden/nesc/`, so the lift must come first, not after it is
        written, and stays armed through the whole guarded window (generator, its output, the ISA
        generator's geopotential edit, ADR-0014, the case1 deletions), not four separate lifts. First
        implementation step, before any code: time `nesc_case1_test.cpp` once and multiply by ten, since
        `just check`'s wall-time budget is why the gate runs locally at all and this design's addition to
        it was estimated, not measured. Accept: all ten within tolerance under every preset.
        **In progress**: 8/10 cases pass, verified under all 4 presets; full gate clean
        (`check: OK`). Cases 4/5
        (`round`/`inverse_square`, Vol II Tables 28/29's 10-30 deg/s spin combined with real `C_D`)
        found a real EOM/architecture characteristic during implementation -- the applied wrench is
        frozen across one frame's four RK4 sub-stages while gravity is recomputed fresh each
        sub-stage, producing a measurable lateral-velocity/attitude drift NASA's own six-tool
        ensemble does not show (confirmed by isolating the mechanism: zeroing `C_D` at runtime
        collapses the drift to floating-point noise; restoring it reproduces it identically).
        **Accepted by Joe as a disclosed gap, 2026-09-18** (option (a): land 8/10 now, close the gap
        in PR 3c below): cases 4/5 are `GTEST_SKIP()`ed in `nesc_cases_test.cpp`, not deleted and not
        tolerance-widened; risk (first-order force/spin coupling this frame architecture does not
        track between wrench samples; matters more once M3 introduces a real aircraft with sustained
        coupled spin+force dynamics) recorded in the human-review packet's Decision record. A Codex
        adversarial review (2026-09-19, after re-dispatch once the quota reset) independently
        confirmed the diagnosis and every other disclosure, and found one further gap: cases 4/5's
        `force_y` band is not merely loose but genuinely non-discriminating (accepts an all-zero
        signal despite a real ~25 N peak) -- fixed with a new generator-side detection check
        ADR-0013 already required but this generator never implemented; full account in
        `docs/reviews/nesc-harness-review.md`.
- [x] Investigate `case05`'s `altitude`/`v_down` anomaly — **root cause found and fixed**
      (`docs/plans/nesc-case05-gravity-centripetal.md`, 2026-09-19): `gravity/inverse_square` never
      supplied the centripetal term `gravity_model.hpp`'s own plumb-bob contract requires ("g_n is
      plumb-bob gravity ... so the EOM carries no separate centripetal term" -- verified against
      `eom.cpp`, which indeed adds none), while `earth/round rotating: true` (case05 only) meant
      Earth's rotation should have entered gravity and did not. `gravity/wgs84_j2` already met this
      contract (cases 1-3/6-10, untouched); `gravity/inverse_square` (cases 4/5 only) did not, at
      all, regardless of rotation. Fix: a new `rotating` config key (bool, default false, mirroring
      `earth/round`'s own key and its unconditional use of `astra::geodesy::kOmegaIe`) on
      `gravity/inverse_square`; case05's config sets it `true`, case04's `false` (explicit, matching
      its `earth.rotating: false`). Verified against two independent textbook derivations, read
      directly this session (`docs/refs/`, main checkout): Stevens/Lewis/Johnson eq. 1.6-27 (p.33)
      and Groves eq. 2.133 second line (p.70), which agree with each other and with `eom.cpp`'s own
      eq. 1.7-16b match to the last sign. Confirmed by an actual dry run (skips locally lifted, asan
      preset, restored before commit), not only arithmetic: at t=30 s, case05's `altitude`/`v_down`
      diffs moved from -13.748 m / +0.801 m/s to **-0.014545 m / +0.0015042 m/s** -- matching
      case04's own pre-existing miss (-0.014685 m / +0.0015207 m/s) to within ~1% -- corrected from
      this session's own earlier "4 significant figures" overclaim per Codex adversarial review
      ("approximately, not to four significant figures"), not merely closer to three orders of
      magnitude away. `density`/`pressure` now pass cleanly in both cases (were case05-only FAILs).
      No channel regressed (`docs/plans/nesc-case05-gravity-centripetal.md`'s own R4/R8 table cites
      every channel against the prior baseline). **Supersedes, for these five channels only**,
      `docs/plans/nesc-harness.md`'s "R8 dry-run baseline for PR 3c" table -- see that file's own
      superseding note. Cases 4/5 remain `GTEST_SKIP()`ed (frozen-wrench characteristic, PR 3c's own
      scope, untouched by this fix and still present equally in both cases). D2 (an earth/gravity
      rotation-consistency check at init) deferred, tracked below.
- [ ] Earth/gravity rotation-consistency check at init — `docs/plans/nesc-case05-gravity-centripetal.md`'s
      D2(b), deferred (Joe, 2026-09-19: D2(a), disclosed only, for that task). Two independently-configured
      plugins can each pass their own init-time validation while disagreeing about whether Earth rotates:
      `gravity/inverse_square`'s new `rotating` config key (that task) must be set consistently with
      `earth/round`'s own `rotating` by hand, with nothing enforcing it (exactly the shape of that task's
      own case05 bug, restated as a residual, not eliminated). Covers **both** mismatch directions:
      `earth/round rotating: true` paired with a non-rotating `gravity/inverse_square` (today's actual
      bug), and the symmetric, currently-latent case, `gravity/wgs84_j2`'s own *unconditional* rotation
      paired with a non-rotating Earth model (no current config does this, so it is latent, not manifest).
      Needs a new `core/` interface path (an `EarthModel`/`GravityModel` accessor pair, mirroring the
      existing `assumed_position_representation()`/`position_representation()` check already in
      `Simulation::from_config`) — a real, if modest, interface change, not a `plugins/physical`-only fix;
      plan-review before code. Not started.
- [x] PR 3c — per-RK4-substage wrench evaluation, the M2 closer: recompute `WrenchSource::wrench_b()`
      (and any other per-frame environment/force input) at each of the `Integrator`'s RK4 sub-stages
      instead of once per frame, closing PR 3b's disclosed cases 4/5 gap. Needs a superseding or
      elaborating ADR to ADR-0004 (fixed-step RK4) and `docs/architecture.md` §5's per-frame order
      (wrenches computed once, before "EOM derivative → integrator step"), since this changes how
      often and with what state every existing `WrenchSource` is called -- a real `Integrator`/`Eom`/
      `WrenchSource` contract change, not a bolt-on. One independent Codex plan critique required
      before any code (`docs/process/workflow.md`'s plan-review trigger: an expensive, unpinned
      design). **Both original preconditions resolved** (ADR-0015,
      `docs/decisions/0015-nesc-tol-rel-drop.md`, 2026-09-19): `tol_rel` is dropped from the oracle
      entirely, and SIM 2 is excluded *entirely* from cases 4/5 (not per-channel -- a broader survey
      found it contaminating nearly every channel there, not only `v_north`/`force_y`), regenerated.
      PR 3c inherits a self-consistent, 3-participant band for every channel, plus a non-gating
      dry-run baseline (`docs/plans/nesc-harness.md`'s "R8 dry-run baseline for PR 3c" table): only
      the three angular rates and three moments pass cleanly against it in both cases; `latitude`/
      `temperature` fail in both, `density`/`pressure`/`force_y`/`force_z` fail in case05 only, and
      `case04`'s own `force_x` now passes where it was previously grouped with case05 as failing --
      reported as PR 3c's starting point for the frozen-wrench characteristic specifically, not
      diagnosed. **Superseded for `altitude`/`v_down`/`density`/`pressure`/`temperature` by the
      gravity fix above** (`docs/plans/nesc-case05-gravity-centripetal.md`, 2026-09-19, not
      reworded here per this file's own append-only-narrative convention): `density`/`pressure` now
      pass cleanly in both cases; `altitude`/`v_down`/`temperature` now match `case04`'s own level
      in both cases (`case04`'s numbers unchanged; the old table's `case04`/`case05` split for these
      five channels no longer holds). PR 3c's own starting point for the *frozen-wrench*
      characteristic specifically is therefore: only the three angular rates and three moments pass
      cleanly against it in both cases; `latitude`/`altitude`/`v_down`/`temperature` fail in both at
      case04's own (now shared) level; `longitude`/`v_east`/`yaw`/`pitch`/`roll` still fail, worse in
      case04 than case05; `force_x`/`force_y`/`force_z` now pass cleanly in both (were case05-only
      FAILs pre-fix) -- the full updated table is in that plan's own R4/R8 section. **Cowork lead,
      unverified**: the residual `altitude`/`v_down` miss now common to both cases (-0.0147 m /
      +0.0015 m/s at t=30 s) is quantitatively consistent with a zero-order hold on drag across each
      1 ms frame -- `dv ~ (dt/2) * a_drag(30 s) ~ 0.0005 * 3.2 = +0.0016 m/s`, right sign and order
      of magnitude. `temperature`'s FAIL (ratio ~1.49, both cases) is plausibly that same ~1.5 cm
      altitude error propagated through the ISA lapse rate. PR 3c's own acceptance should expect
      these two residuals to collapse as well, not only the lateral-velocity/attitude drift already
      named above -- not verified this session, a claim for PR 3c to check, not assume. Not started
      in the M2 PR 3b worktree; a separate task.

      **Done, 2026-09-19** (`docs/plans/pr3c-substage-wrench.md`, `docs/decisions/0016-per-rk4-
      substage-wrench-evaluation.md`, not reworded above per this file's own append-only-narrative
      convention): the Cowork-lead residual-collapse claim above is confirmed -- every one of the
      channels this PR's own starting point listed as failing now passes cleanly in both cases,
      cases 4 and 5's `GTEST_SKIP()` lifted, no golden regenerated, no tolerance widened. Full
      per-channel `max(|diff|)/tol_abs` table (all 21 channels, both cases, computed against the
      actual implementation): worst is `pressure` at 0.194; `v_north`, this PR's own starting
      point's worst channel at 4.85 million times `tol_abs` (real committed baseline,
      `docs/plans/nesc-harness.md`'s R8 table), now misses by 0.0033–0.0044 -- full table in
      ADR-0016. The "just reduce `dt`" alternative was measured, not merely argued, against the
      actual unmodified frozen-wrench code and rejected: `v_north`/`latitude` shrink linearly with
      `dt` but still miss by >1.2 million times/~1385 times `tol_abs` even at 4x cost. `just check`
      green on all four presets (debug/release/static-plugins/asan), 290/290 each; Codex general +
      adversarial review (focus: state-revert after a non-k1 fault, health-counter semantics under
      warn/halt, Dryden bitwise determinism, ADR-0001/0002 units/frames on the new
      `wrench_total` signals) both clean, verdict approve, zero fix rounds needed. Human-review
      packet: `docs/reviews/pr3c-substage-wrench-human.md`.
- [x] Atmosphere-domain-check misattribution window — PR 3c's own disclosed, accepted gap
      (`docs/plans/pr3c-substage-wrench.md`, `docs/decisions/0016-per-rk4-substage-wrench-
      evaluation.md`), the same class ADR-0012 already accepted for gravity/Earth, newly instanced
      for atmosphere: since PR 3c recomputes `AtmoState` fresh at each RK4 sub-stage's own altitude,
      a stage whose `y` drifts outside `AtmosphereModel::valid_range()` mid-integration could
      surface as `kNonFiniteWrench` (naming the consuming `WrenchSource`), one frame sooner than and
      instead of `Simulation::step`'s own post-step `kOutOfDomain` check (which would correctly name
      the atmosphere plugin). **Resolved by ADR-0020 (M3 PR 3d, 2026-09-20, Q4 of
      `docs/plans/pr3d-adr0020-implementation.md`)**, as part of implementing
      `HealthMonitor::check_environment`, not a standalone fix: `Vehicle::integrate()`'s per-stage
      derivative functor now checks the stage's own altitude against `AtmosphereModel::
      valid_range()` (the same NaN-safe form `Simulation::from_config`'s own init-time check uses)
      *before* sampling atmosphere at all -- out of range reports `kOutOfDomain` naming the
      atmosphere plugin directly, atmosphere is never sampled that stage, and `check_environment`
      (the new NaN/Inf check replacing the old per-plugin isfinite guard) therefore only ever
      observes an already-confirmed-in-range sample, never the ambiguous all-NaN value a legitimate
      excursion produces. `tests/unit/kernel/simulation_halt_test.cpp`'s
      `LeavingTheAtmosphereValidRangeMidRunHaltsNamingTheAtmospherePlugin` verified against the real
      implementation.
- [ ] `plugins/physical/mass/constant`'s `ixy`/`ixz`/`iyz` config keys have no documented sign
      convention (M3 PR 1 review): the plugin places them directly, unnegated, into the inertia
      tensor's off-diagonal entries, which only matches the standard aerospace convention
      (tensor entry = `-integral(x*z*dm)`) if the config value is already meant as that negated
      tensor entry rather than the product of inertia itself. `aircraft/_template`'s and
      `aircraft/notional_transport`'s own `physical/mass` plugins (M3 PR 1) document and use the
      product-of-inertia convention explicitly instead; the shared plugin's keys are unchanged and
      still undocumented. Not started.
- [ ] `icd/astra_icd.h`'s elevator/aileron/rudder fields have no documented positive-deflection sense
      (M3 PR 4 planning): the header comment says only "rad, positive per ADR-0001 sign conventions,"
      and ADR-0001 itself only says the definition is "documented alongside them" — nowhere actually
      states whether positive means trailing-edge-up or -down, left or right. `aircraft/notional_
      transport`'s `physical/aero` plugin (PR 4) is the first consumer that needs a definition to
      exist, and adopts JSBSim's own sense directly (elevator trailing-edge-down, positive aileron =
      right-wing-down roll, rudder trailing-edge-left — inferred from `docs/refs/aircrafts/737.xml`'s
      own `Cmde`/`Clda`/`Cndr`/`Cldr` signs, approved by Joe as the ICD convention). Pinning it for
      this one aircraft's own controls does not resolve the framework-wide gap: `icd/astra_icd.h`'s
      own comment should eventually state the physical sense directly instead of pointing at
      ADR-0001, which does not itself state it. Not started.
- [ ] Elevator/throttle deflection *range* has no declared home either (M3 PR 6 planning,
      `docs/plans/pr6-trim.md`, D10): the second control-surface property (after the
      positive-deflection-sense gap above) with no config key or ICD field to read it from. PR 6's
      own plausibility check hardcodes `elevator ∈ [-0.3, 0.3]` rad (737.xml) and `throttle ∈ [0, 1]`
      (the ICD's documented convention, not a config value) directly in its test rather than adding
      `aircraft/**/physical` config keys. Not started.
- [ ] `cmake/AstraPlugin.cmake`'s `astra_finalize_plugins()` hardcodes which test executables get a
      plugin build-order dependency (`astra_unit_tests`, `astra_physics_tests`, and — since M3
      PR 1 — `astra_notional_transport_mass_test`, `astra_template_mass_test`), by explicit design
      ("listed explicitly ... so a new test target opts in", that function's own comment). This
      means a new aircraft with its own dedicated test executable needs a framework edit
      (`cmake/AstraPlugin.cmake` itself) to build correctly from clean, contradicting §18's on-ramp
      story ("copy `_template`, edit, rebuild — nothing in the framework is edited"). Wants an
      `astra_add_plugin_test()` helper (mirroring `astra_add_plugin()`'s own one-line
      self-registration) so a new aircraft's own `CMakeLists.txt` can opt its test in without
      touching `cmake/AstraPlugin.cmake`. Not started.
- [ ] A separate tier-2 workflow PR restoring the "no recon subagents" rule's visibility and
      rationale, dropped from the standing docs by Workflow v3 (#30) (origin: commit `b07057c`/#8,
      2026-09-14; rationale in that day's `docs/lessons.md` entry — subagent cost, and fidelity: a
      Sonnet-pinned subagent hands the planner a lossy secondhand account instead of a first-hand
      read). Found during M3 PR 2 planning: a research subagent was launched mid-session before the
      rule was re-read and caught. (a) one line in `AGENTS.md`'s "Commands and boundaries" — read
      code directly, no exploration/recon subagents (cost + fidelity, `docs/lessons.md` 2026-09-14);
      the Codex review/critique route (`docs/agents/review-dispatch.md`) is the sole exception;
      (b) "directly, not via subagents" added to `.claude/skills/plan-feature/SKILL.md` step 1's
      "Inspect..." instruction. `AGENTS.md`+`CLAUDE.md` are at 77/100 lines, so (a) fits the
      existing budget. Not started.
- [ ] ADR-0002's literal "no unit suffixes in identifiers (`altitude`, not `altitude_m`)" text vs.
      `AtmoState`'s own field names (`temperature_k`, `pressure_pa`, `density_kg_m3`,
      `speed_of_sound_m_s`) and, as of M3 PR 3's own ADR-0018, `AeroInputs`/`PropulsionInputs`'s
      (`alpha_rad`, `dynamic_pressure_pa`, `true_airspeed_mps`) — whether the rule means signal-name
      strings specifically (its own example, `registry.add("aero.CL", ...)`, is a string) or every
      C++ identifier is unresolved; PR 3's own plan accepted the unit-suffix precedent rather than
      settling the wording. Not started. **`AeroInputs`/`PropulsionInputs` themselves are gone under
      ADR-0020** (2026-09-20, `docs/decisions/0020-aero-propulsion-true-interfaces.md`) — `AtmoState`'s
      own field names are ADR-0020's own cited precedent for whatever unit-suffixed fields a concrete
      plugin's own `air_data.hpp`-derived, plugin-side `Inputs`-shaped struct uses instead; the
      underlying wording question is otherwise unchanged.
- [ ] §15 envelope monitors (α, β, Mach, dynamic pressure, load-factor limits declared by the aero
      model) — deferred from M3 PR 3 (ADR-0018): no declaration hook exists yet on `AeroModel`
      because no concrete plugin has real numbers to declare before PR 4. PR 4 or a later PR adds
      the declaration virtual (a further `kCoreApiVersion` bump, precedented) and wires
      `HealthMonitor`'s own per-frame check against it. Not started.
- [ ] §15 envelope monitors (α, β, Mach, dynamic pressure, load-factor limits declared by the aero
      model) — deferred from M3 PR 3 (ADR-0018): no declaration hook exists yet on `AeroModel`
      because no concrete plugin has real numbers to declare before PR 4. PR 4 or a later PR adds
      the declaration virtual (a further `kCoreApiVersion` bump, precedented) and wires
      `HealthMonitor`'s own per-frame check against it. Not started.
- [ ] **M3 Notional transport** — aero + mass + turbofan plugins; trims at cruise (10,000 m, 230 m/s
      TAS, ADR-0002 SI) with residual < 1e-6 (norm TBD, PR 6); linearized modes match design values,
      each mode against its own tolerance; a 300 s open-loop run matches its own frozen regression
      trajectory (not an oracle), independently cross-checked against the linearized model's
      phugoid-cycle prediction. §16's M3 row is corrected to this wording by PR 0.
  - [x] PR 0 — merged as `91aa358` (#36, 2026-09-19). Neutral status markers on `docs/architecture.md`: §7/§8/§12 note the `AeroModel`/
        `AeroInputs`/`Simulation` Python bindings as not-yet-built M3 scope, without rewriting §7's
        shape yet (that lands with PR 2/3's ADR, once the control-input design is actually pinned, to
        avoid rewriting it twice); §13's typed `physical.aero`/`hardware.propulsion` example is marked
        illustrative, not the current schema (D6: `vehicle.models` is the real one; PR 1 rewrites
        §13's example once the first real config lands). §16's M3 row is corrected to read "trims at
        cruise ... linearized modes match design values (per-mode tolerance) ... a 300 s open-loop run
        matches its own frozen regression trajectory, independently cross-checked against the
        linearized model's phugoid prediction," replacing "reproduces the golden trajectory" (O5: a
        self-frozen trajectory is a regression check, not an oracle). Also adds `aircraft/**/physical`,
        `aircraft/**/hardware` to `docs/process/workflow.md`'s tier-3 row (D3). Accept: `git diff
        --stat` touches only these two docs; `just check` unaffected (no code). Binding ADRs: none.
        Tier 2 — the §16 acceptance-row and workflow.md tier-table edits are policy, not editorial;
        Joe approves the wording (recorded in this plan's own approval), and the diff itself still
        gets a Codex review pass even though no code changes and no full gate applies. Plan critique:
        no.
  - [x] PR 1 — merged as `829df62` (#37, 2026-09-19). `aircraft/_template/` and `aircraft/notional_transport/` scaffolding (`physical/
        {aero,mass}/`, `hardware/propulsion/turbofan/`, `configs/`), `just new-aircraft <name>`.
        `physical/mass` is a `MassPropertiesModel` plugin (existing interface, O1's mass/inertia only
        -- no CG: `MassProps` is `{mass, inertia_b}`, wrench is about CG per §7, engine-station offsets
        live in the turbofan plugin's own config, not in mass properties). Config schema is
        `vehicle.models` as-is (D6, zero core changes -- `station:` lives inside an entry's own config
        map); rewrites §13's example config to this real syntax, replacing the illustrative typed-slot
        sketch PR 0 flagged. Accept: static-plugins build discovers both folders with zero framework
        edits; `ctest -L unit -R notional_transport_mass` passes. Binding ADRs: ADR-0009. Tier 1.
        Plan critique: no.
  - [x] PR 2 — Write-seam primitive, the read-registry's mutable counterpart, plan/ADR accepted,
        implemented, validated, and merged as `c5f7633` (#38, 2026-09-20).
        `InputRegistry` (registry-owned storage, `SignalRegistry`'s
        mutable counterpart): a `WrenchSource` registers a writable input from a new `bind_inputs()`
        hook, getting back a `const double*` into `InputRegistry`'s own canonical storage (also
        visible read-only as `v0.controls.*`); `WrenchSource::wrench_b`'s signature is unchanged.
        Config literals (`vehicle.initial_controls`, this PR), Python (trim/linearize, PR 5), and
        later M4's transport/`ActuatorModel` all write through the same seam (`write()`/a cached
        `InputHandle`); `Simulation::step` calls `InputRegistry::latch()` once per vehicle per major
        frame, after `vehicle.step(ctx)` and before `vehicle.integrate(...)`, giving zero-order hold
        across a frame's four RK4 sub-stages (S&L §3.4) with no one-frame lag for a step()-time
        writer. Two instances of one plugin (PR 4's left/right `turbofan`) share one value by
        default (symmetric control), with per-instance naming available as an opt-in. Entries
        default to a quiet NaN, never an unspecified value: an unwritten or non-finite input halts
        (default) naming the input itself, not the consuming `WrenchSource`, via a new
        `HealthMonitor::check_input`. Cite S&L §2.5 (control vector separate from state), §3.4 (ZOH
        justification), §3.5 Fig. 3.5-2 (F-16's `COMMON/CONTROLS`, unchanged derivative signature);
        Zipfel Fig. 9.39 + App. E.2.3/E.2.5 (module-variable bus, one writer many readers). Full
        design, rejected alternatives, and the disclosed M4 gap (no seam route for a non-`WrenchSource`
        writer): `docs/decisions/0017-control-input-write-seam.md`; full review history: `docs/plans/
        pr2-write-seam.md` (two Codex plan critiques, two rounds of Joe's own review, eighteen
        findings total, all resolved) plus implementation-time Codex general + adversarial review
        (`docs/reviews/pr2-write-seam-review.md`: one major finding — `apply_initial_controls`
        depended on `Config`'s own shared read-state via `unknown_keys()`, breaking a second
        `Simulation` built from the same `Config`; fixed with `InputRegistry::names()` — fixed and
        verified, zero remaining). Accept: `v0.controls.*` visible in the registry; a value written
        mid-frame is not observed until next frame (R3); all four RK4 stages of one frame see the
        identical written value (R5); zero allocation over 1000 frames (R6); full acceptance table
        R1–R13, `docs/plans/pr2-write-seam.md`, all passing. Human review packet: `docs/reviews/
        pr2-write-seam-human.md`. Binding ADRs: ADR-0017 (accepted). Tier 3. Plan critique: done
        (twice).
  - [x] PR 3 — merged as `da18909` (#39, 2026-09-19). `AeroModel`/`PropulsionModel` as `WrenchSource` specializations: compute α/β/q̄/Mach
        from `(vs, env)`, read the model's own PR 2 control-input members, assemble `AeroInputs`/
        `PropulsionInputs`, call a pure-virtual `compute()` that is `const noexcept` (§7's non-const
        sketch is what changes, not the ADR-0016 purity rule). Hand-built-`AeroInputs` unit testing
        without a simulator is preserved. Depends on PR 2 (merged, `c5f7633`/#38, 2026-09-19
        20:55:09-07:00). Accept: a stub `AeroModel` is unit-tested with a hand-built `AeroInputs`
        alone, and separately proven end-to-end through a real `Vehicle` with PR 2's write seam
        driving it; `wrench_b()`'s own `isfinite` guard over the assembled `Inputs` (an unconditional
        no-silent-NaN guarantee, no new `HealthMonitor` API) is part of this PR's own acceptance.
        Binding ADRs: ADR-0016, ADR-0017, ADR-0018 (new). Tier 3. **Plan approved by Joe, 2026-09-19**
        (`docs/plans/pr3-aero-propulsion.md`, Revision 2). Plan critique: done (once) -- the combined
        pr2/pr3 pass floated at PR 0 time did not happen (PR 2 shipped independently); this PR's own,
        separate Codex plan critique found twelve findings, all accepted and folded into the plan,
        none reopening the interface/ownership/version decisions, so no second critique was needed.
  - [x] PR 3d — **framework realignment to Joe's stated intent** (`docs/refs/
        astra_framework_intent_and_state_audit.md`, main checkout, gitignored), superseding ADR-0000's
        "commercial transport" purpose clause, the `Inputs`/`final` parts of ADR-0018, and ADR-0003's
        fixed-13-state-layout decision. **ADR-0019/0020/0021 accepted, 2026-09-20**
        (`docs/decisions/0019-vehicle-agnostic-purpose.md`, `0020-aero-propulsion-true-interfaces.md`,
        `0021-dynamics-model-owned-state.md`; full critique/revision record:
        `docs/plans/m3-framework-realignment.md`). Implements ADR-0020 (unblocks PR 4, below):
        `AeroModel`/`PropulsionModel` become minimal marker interfaces (no `compute()`, no
        `AeroInputs`/`PropulsionInputs`, no `final` except one non-final `requires_environment()`
        override each); new `core/include/astra/util/air_data.hpp` utilities; new
        `HealthMonitor::report_non_finite_environment` plus a new per-stage domain pre-check in
        `Vehicle::prepare_stage_environment` (checks the stage's own altitude against
        `AtmosphereModel::valid_range()` before sampling, closing PLAN.md's own "Atmosphere-domain-check
        misattribution window" item too, not only ADR-0020's own scope), both skipping that stage's
        `wrench_sources_` loop entirely on a fault, closing the no-silent-NaN gap `final`'s removal
        would otherwise open; rebuilt `aero_stub`/`propulsion_stub` overriding `wrench_b()` directly;
        `kCoreApiVersion` 6 -> 7. `docs/architecture.md` §1/§6/§7/§18 already rewritten to match
        (ahead of code, same session).
        **Implemented and validated, 2026-09-20**: full gate green (`check: OK`, all four presets,
        clang-tidy, pyright, pytest); Codex general review (one P2 finding, fixed: fault detection was
        conflated with an optional diagnostic name's own emptiness) and adversarial review (one medium
        finding, fixed: added `tests/unit/kernel/vehicle_environment_test.cpp` with three new counting/
        flaky test plugins proving the actual zero-`wrench_b()`-call guarantee, a fault first appearing
        at RK4 stage k2, the isolated R-001 infinite-speed-of-sound case, and a configured non-finite
        wind); one Codex re-verify pass confirmed both fixes, no new findings. **Approved by Joe,
        2026-09-20**, pending a follow-up commit (domain-off/nan-off documentation, per-stage counting
        documentation, full-gate rerun to a retained log, packet fixes) -- follow-up committed
        (`1aad091`, `b4864b9`). PR #40 (covers this and the realignment ADRs above) merged as
        `6590b7c`, 2026-09-20 (squash merge, Joe).
        Binding ADRs: ADR-0019, ADR-0020. Tier 3. Plan critique: done
        (`docs/plans/pr3d-adr0020-implementation.md`, Codex plan critique folded in, approved by Joe
        including Q1-Q4).
  - [x] PR 4 — `aircraft/notional_transport/physical/aero` (linear-coefficient aero) and
        `hardware/propulsion/turbofan` (thrust lapse), O2/O3's numbers transcribed from
        `docs/refs/aircrafts/737.xml`'s `<aerodynamics>`/`<propulsion>` blocks (same source as O1's
        mass properties, PR 1 -- transcription, not an independently proposed set of coefficients);
        engine-station offset from CG lives in the turbofan plugin's own config. **Superseded plan**
        (`docs/plans/pr4-notional-aero-turbofan.md`, built against `compute(const AeroInputs&)`, gone
        under ADR-0020): rebuilt once PR 3d lands, against a direct `wrench_b()` override -- its
        frame-conversion math, S&L force/moment-transfer citation, and dropped-terms analysis remain
        reusable; carries forward this session's own found defects (`thrust_ref_n` sized for cruise
        headroom, not zero margin; `Cmadot`/alpha-rate damping still dropped -- no `alpha_dot` source
        exists until PR 4a/ADR-0021 lands, not a PR 4 fix). Accept:
        `tests/physics/notional_transport/` exercises the rebuilt aero/propulsion `wrench_b()`
        overrides directly at 2-3 hand-picked conditions against O2/O3's sourced values to stated
        tolerance; **and** (the V→0 obligation, now attached directly to `wrench_b()` per ADR-0020)
        returns a finite `Wrench` at `true_airspeed_mps == 0`, proven by its own test -- the
        linear-coefficient aero's stability derivatives are exactly the S&L §2.3 Eq. (2.3-9a)
        nondimensional-rate form (scaled by `1/(2V_T)`) that hazard names, and a naive port would
        carry a real division by zero at that state, unlike the dimensional force equations built
        from the same coefficients. **Implemented, validated (full gate green, four presets), two
        Codex fix-verification rounds closed (static-plugins ODR namespace collision, `docs/lessons.md`
        entry), approved by Joe.** PR #41 merged as `26ad594`, 2026-09-20 (squash merge, Joe).
        Depends on PR 3d. Binding ADRs: ADR-0001, ADR-0002, ADR-0017,
        ADR-0020. Tier 3. Plan critique: yes -- rebuilt plan against the new interface, not the old
        ADR-0018 `compute()` shape PR 2/3 pinned.
  - [x] PR 4a — implement ADR-0021: `DynamicsModel` interface + `RigidBody6DofDynamics` (byte-for-byte
        port of today's `Eom::operator()`); `Model`'s five new virtuals (`state_count`/
        `initial_state`/`integrate_state`/`bind_state`/`resolve_peers`, all defaulted); `Vehicle`'s
        two-phase init (discover/size/allocate/bind/publish, then resolve peers), combined state
        buffer, and stage buffer with its post-step/init sync invariant; `Integrator`'s
        write-into-buffer functor contract and its own owned scratch; `wrench_source.hpp`'s
        purity-contract amendment (ADR-0021's own exact text); a frozen R1 golden captured from the
        pre-change build, with its capture manifest, under `tests/golden/` (`.claude/touch-goldens`).
        Independent of PR 4 (adds no states of its own) but must land before PR 5, which binds the
        state to Python. Accept: the frozen-golden bitwise-equality test (R1); existing NESC/
        determinism goldens pass unmodified under all four presets; `no_alloc_test.cpp` extended with
        a state-owning test model (R2); a test proving sub-stage-exact cross-model state reads via
        both `bind_state()` and a peer's `SignalRegistry`-resolved read; a test proving mutual
        cross-model state dependence resolves regardless of `vehicle.models` config order; a test
        proving the stage buffer equals the combined state buffer outside any derivative call, both on
        a normal and a halted frame; a test proving duplicate published state names and missing
        required peers throw recoverable `ConfigError`s. Binding ADRs: ADR-0021. Tier 3. Plan
        critique: done (`docs/plans/m3-framework-realignment.md`). Implementation plan: done
        (`docs/plans/pr4a-dynamics-model.md`, Revision 3). **Implemented, validated (full gate
        green, four presets; Codex Tier 3, three fresh-context passes, general + adversarial: R-001/
        R-002/R-003 found and fixed, fix-verification pass confirmed "New findings: none"), approved
        by Joe.** PR #42 merged as `96c9459`, 2026-09-21 (squash merge, Joe). Depends on PR 3/3d/4.
  - [x] PR 5 — nanobind: `Simulation`, `Config` from dict, `SignalRegistry` read views, PR 2's write
        seam (`sim.write_input(name, value)`, D7), and `Simulation::derivative(state)` -- the same
        derivative functor `Vehicle::integrate` hands the integrator (fresh atmosphere at the given
        state, latched wind), not a second implementation. `Simulation::for_testing`'s M2 PR 2 deferral
        ("expose when a Python binding is added") is resolved here: **not bound**, decision recorded.
        Trim/linearize run with no wind or uniform wind only (Dryden makes the derivative
        frame-dependent). Depends on PR 2/3/4a (ADR-0021's length-N state, not the old fixed 13-vector
        -- `derivative(state)` exposes named slices for the rigid-body block plus each state-owning
        model's own slice, per ADR-0021's R4). Accept: `derivative(state)` is bitwise equal to the k1
        the real integrator computes for that state, proven via a small C++ test seam (`from_config`
        hardcodes `kRk4`; an Euler-step comparison isn't reachable without new config surface, so this
        replaces that alternative). Binding ADRs: ADR-0021 (R4). Tier 3. Plan critique: **yes**
        (ownership/lifetime across the Python/C++ boundary). D2 settled: minimal scope, no trampolines
        in M3. **Implemented, validated (full gate green, four presets, retained at
        `docs/reviews/evidence/5964c63-full-gate.log`; Codex three fresh-context passes -- general (no
        actionable regressions), adversarial on Joe's own four-area focus (ownership/lifetime across
        the Python/C++ boundary, probe side-effect isolation R5a/R5b, R9 bit-identity, no allocation on
        `Vehicle::integrate()`'s own per-frame path), and a fix-verification round on Joe's own
        human-review test addition -- all verdict approve, no material findings), approved by Joe.**
        PR #43 merged as `cc11c30`, 2026-09-21 (squash merge, Joe).
  - [ ] PR 6 — `astra.trim`: `scipy.optimize.root` writing PR 5's inputs, driving `derivative(state)`
        to zero at a flight condition. Residual norm/units (D8, Joe's pick, at this PR's planning
        time) -- S&L §3.6 uses a weighted sum-of-squares of state derivatives; §16 gives no norm, and
        the state mixes m, m/s, quaternion components, rad/s, so raw SSE isn't unit-consistent.
        Depends on PR 5. Accept: `pytest tests/python/test_trim.py` -- residual under D8's definition
        < 1e-6 at cruise. Binding ADRs: none new. Tier 2. Plan critique: no.
  - [ ] PR 7 — `astra.linearize`: central-difference A/B about trim; short period/phugoid/Dutch
        roll/roll/spiral checked as a sanity comparison against S&L §4.2's classical closed-form
        mode approximations, not an independent O4 source (supersedes O4: `docs/refs/aircrafts/
        737.xml` is a JSBSim example aircraft with Aeromatic/Roskam-estimated coefficients, not a
        validated real 737, so no independently validated source exists to check it against --
        S&L §4.2's approximations are a labeled sanity check, not an oracle). Depends on PR 6.
        Accept: `pytest tests/python/test_linearize.py` within a loose, per-mode tolerance labeled
        as approximate (tighter for short period/phugoid, where S&L's approximations are usually
        good; wider for the lateral three, where they're known to be poorer). Binding ADRs: none
        new. Tier 2/3. Plan critique: no.
  - [ ] PR 8 — Cruise config(s) (D6's `vehicle.models` schema) + 300 s open-loop run frozen as a
        regression trajectory (O5 -- not an oracle), generator-under-guard pattern from
        `tests/golden/nesc/`. Adds the actual independent check §16 (as corrected by PR 0) requires: a
        small-perturbation open-loop response compared against PR 7's linearized-model prediction
        (period/damping) over the first phugoid cycle. Depends on PR 4/6/7. Accept: `ctest --preset
        asan -R notional_transport_cruise_regression` matches the frozen trajectory under all four
        presets, guarded by `.claude/touch-goldens`; the phugoid-cycle cross-check passes its own
        stated tolerance. Binding ADRs: ADR-0004, ADR-0016, PR 2/3's ADR. Tier 3. Plan critique: no --
        needs Joe's explicit oracle sign-off same as ADR-0013/14/15.
- [ ] **M4 Lockstep** — shm transport + HAL + straight-and-level autopilot outside the sim tree; 10,000
      exchanges, zero missed; altitude ±30 m, heading ±2° under light Dryden; bit-identical logs; peer death
      halts within one timeout.
- [ ] **M5 Tooling** — HDF5 logging + replay, scenario harness over `tests/scenarios/`, Monte Carlo of 64 runs,
      FlightGear packets verified by a decoder test.
