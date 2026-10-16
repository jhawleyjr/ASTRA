# M7b Closed-loop oracles: spec, plan and PR decomposition

Status: **approved by Joe, 2026-10-15, on his word**, with his answers to Q1 to Q12 and his amendments 1 to
10. They are recorded under "Joe's decisions" and folded into the text.
- The Codex plan critique and its verification pass follow (PR 0, at the end). A finding that reopens a
  decision returns to Joe.
- Nothing is implemented, and approving this plan approves no later PR's diff.
- Name: the plan-mode draft (main checkout, untracked) was moved (`mv`, before its first add) to
  `docs/plans/m7b-closed-loop-oracles.md`. Nothing is left in the main checkout.
Owner: Joe. Builder: Claude, one builder.
Risk: Tier 3 overall. It adds a new class of oracle with pre-registered bounds, a test transport at the
exchange seam, a new transport plugin, and edits under `aircraft/f16/physical` and `aircraft/f16/sim`. This
planning PR is Tier 0 (docs only). Each PR's tier is in the decomposition table.
Intent: Joe's brief of 2026-10-15, STATE.md's next step 2. Three in-house oracles take the place of the
verification that M7 PR 11b, 12 and 15 would have drawn from the NESC closed-loop ensemble. That ensemble has
two members with loose maneuver bands, and those three PRs left M7 on 2026-10-13 ([PR 11a's
plan](m7-pr11a-control-peer.md), "Joe's decisions after the measurement", items 3 and 8).
Base: `origin/main` = `8e525c5` (#120, M7 closed out), read with `git ls-remote` in plan mode and confirmed by
`git fetch` after approval. The worktree `closed-loop-oracles` (branch `worktree-closed-loop-oracles`) is at
`8e525c5`.
Architecture and ADRs: [architecture](../architecture.md) §5 (the per-frame order), §10 and §11 (the ICD, the
transport, `embedded/`) and §16; [ADR-0001](../decisions/0001-frames-and-attitude.md),
[ADR-0002](../decisions/0002-si-units-and-signal-metadata.md),
[ADR-0004](../decisions/0004-fixed-step-rk4-in-house.md), [ADR-0006](../decisions/0006-icd-versioned-c11-structs.md),
[ADR-0007](../decisions/0007-shared-memory-lockstep-futex.md),
[ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md), [ADR-0013](../decisions/0013-nesc-check-case-oracle.md),
[ADR-0017](../decisions/0017-control-input-write-seam.md), [ADR-0021](../decisions/0021-dynamics-model-owned-state.md),
[ADR-0022](../decisions/0022-lockstep-exchange-stage.md), [ADR-0028](../decisions/0028-f16-nesc-oracle.md), and
[ADR-0029](../decisions/0029-sim-side-flight-software-peer.md) (Decisions 1, 2, 5, 7 and 8).

System context. Every oracle here runs through the existing exchange stage: environment, then the transport
exchange, then the vehicle's `step()`, the latch, the wrenches and EOM, the integrator, the post-step and the
logger (`core/src/kernel/simulation.cpp:752-778`). Each in-process peer is a `Transport` plugin, as
`LoopbackTransport` and `nesc_control` are. The block adds no core change, no `icd/` change and no
`embedded/` change.

## Outcome and boundaries

**Outcome.** A new PLAN.md block between M7 and M8, "M7b Closed-loop oracles", delivers three oracles. Each has
an independent reference, a bound pre-registered before its first run, and negative controls that must fail.
Each is gated in `just check`.
- **(a) Exact sampled-data check.** A linear plant with model-owned state runs under discrete state feedback
  through the exchange. Its trajectory matches the exact sampled-data solution at every frame, to stated
  roundoff.
- **(b) The port pattern, proven on M4's autopilot.** A sim-side, in-process C++23 peer of M4's law is bitwise
  equal to `embedded/autopilot` on ADR-0029 Decision 8's objects. The comparison runs over M4's acceptance
  scenario at both Dryden levels.
- **(c) Symmetry tests.** The F-16 flies under `F16_control`. On a non-rotating Earth, 13.3's course change and
  13.4's side step are flown as mirrored pairs. On a flat Earth, 13.3's maneuver is flown at several headings
  to show heading invariance. Each comparison has a pre-registered bound, preceded by an inventory of every
  asymmetry.

**Non-goals.**
- `F16_gnc`, cases 15 and 16, the F-16 port and the tutorial. PLAN.md's three unscheduled M7 items stay as
  written.
- ADR-0028's bands for 15 and 16 stay generated and unflown.
- Anything in M8. Sensors and ICD v2 (M9). Closed-loop Monte Carlo (M10).
- Any change to `core/`, `icd/`, `embedded/`, `tests/golden/**`, a tolerance file, or an accepted ADR's text.

**Invariants.**
- Every existing test passes unchanged, and every NESC band is unchanged.
- The shipped `nesc_control` peer and the F-16 aero behave bit for bit as today; PR 4 shows this with evidence
  (R15).
- Two runs of one config give byte-identical logs.
- The per-frame path allocates nothing.
- `embedded/` stays C++17 and links only `icd/` and `embedded/hal/`.
- No bound is fitted to ASTRA's output, and none is moved after its first run without Joe.

**Assumptions** (each is checked by the PR named; none is relied on silently):
- At an exchange, the stage buffer holds the accepted frame-start state. ADR-0021 states this invariant; PR 2's
  check F fails if it does not hold.
- glibc's `atan2`, `asin` and `sin` are exactly odd, and `cos` and `hypot` are exactly even. PR 4's parity probe
  checks this before any flight.
- clang-18 emits no fused multiply-add for the default x86-64 target. PR 3 states the flags and checks them.

## What each oracle sees that the ensemble could not

| Blind spot of the two-member ensemble | Oracle | How it is seen |
|---|---|---|
| Exchange timing, and the hold at a command step | (a) | At every frame, the plant's state equals the exact solution under a zero-order hold that starts at the exchange frame. The step's reference first acts at the first exchange at or after the step frame. |
| The divisor | (a) | Nominal runs at N = 10, 3 and 1, and a wrong-divisor control. |
| The stage a command is applied at | (a) | A command applied from the latch, through all four RK4 stages of its frame, is exact. A command that switches inside the step leaves a residual of about h/6 times the command change. |
| A porting defect smaller than any band | (b) | Bitwise equality. Ulp-sized planted defects fail it while staying inside M4's ±30 m and ±2° bands. |
| Sign and parity defects in the lateral path | (c) | A mirrored pair is compared by value (bitwise up to the sign of zero). A one-sided defect breaks it at roundoff sensitivity. |

## Repository findings (observed at `8e525c5`)

**Three findings that change the brief's literal design.** Each is a question for Joe below.
1. **ICD v1 carries only the rigid-body block, so a model-owned state cannot reach a peer through it** (Q4).
   - `icd_pack::pack_sim_to_sw` fills every field from the rigid-body `VehicleState`, the environment, the
     Earth model and the previous frame's wrench (`core/include/astra/util/icd_pack.hpp:72-140`).
   - A model-owned state is published only as `<vehicle>.<model>.state.<i>`, a live pointer into the stage
     buffer (`core/src/kernel/vehicle.cpp:194`).
   - A transport receives the `SignalRegistry` at `init`, after every vehicle has published its state
     (`core/src/kernel/lockstep_exchange.cpp:150`, `simulation.cpp:463-472`), so a test peer can resolve those
     pointers.
   - A transport also requires a `physical` section (`simulation.cpp:465-468`), so the plant's config carries a
     rigid body that nothing reads.
2. **The F-16 aero is physically asymmetric in β** (Q11).
   - Four control-derivative tables (`dclda`, `dcldr`, `dcnda`, `dcndr`) are looked up at signed β on the
     breakpoints −30° to +30° (`aircraft/f16/physical/aero/aero_math.hpp:235-238`, `aero_tables.hpp:29`).
   - Their P1 data are not even in β. Measured from the generated header in plan mode, by a read-only scratch
     script:
     - the largest |T(−β) − T(β)| per table is 0.079 for `dclda` (β = 30°, α = 20°: −0.082 against −0.003), 0.031
       for `dcldr`, 0.035 for `dcnda` and 0.044 for `dcndr`;
     - near the 13.x condition (α between 0° and 5°), |T(−10°) − T(10°)| is 0.002 to 0.004 for `dclda`, 0.001 to
       0.002 for `dcldr`, 0.006 to 0.008 for `dcnda` and 0.001 to 0.003 for `dcndr`. The tables' values there
       are −0.051, 0.015, −0.010 and −0.045.
   - Even with even data, signed-β interpolation over a mirrored bracket is not bitwise even.
     `f16_interp.hpp:30-56` computes the fraction from the bracket's lower breakpoint.
   - `absCl0` and `absCn0` are looked up at |β| with β's sign applied (`aero_math.hpp:217-223`), so they are
     bitwise odd.
3. **The shipped peer subtracts a rotating WGS-84 Earth's rate under any Earth model, and refuses a flat
   Earth** (Q10, Q12).
   - `earth_relative_rate` (`aircraft/f16/sim/transport/nesc_control/control_law.hpp:315-321`) and
     `kRefusedStatusBits` (`:123-125`); ADR-0029 Decision 7 discloses this. On a non-rotating Earth, Ω_n is
     fixed, and it is not mirror-symmetric about a meridian plane.
   - At 13.x's trim, the law then commands about 0.005° to 0.007° of aileron and 0.003° of rudder at engagement
     (PR 11a's Q4 measured 0.007° of aileron). The "level" flight before the maneuver is therefore not
     lateral-zero, and the mirror fails before any step.
   - A flat Earth sets `LATLON_NOT_MODELED`, which the peer refuses at every frame. ICD v1 carries no horizontal
     position on a flat Earth, so 13.4's cross-track cannot run there at all.

**Facts the plans rely on.**
- **The exchange.** It packs at `frame % divisor == 0`, after the environment sample and before the vehicle's
  `step()`. It validates the reply, stages the command map as pending inputs, and the same frame's `latch()`
  commits them. Every RK4 stage of that frame then reads them (ADR-0022, `simulation.cpp:752-778`).
  - `Model::step` runs before the latch, so a model's `step()` still sees the previous latched value.
  - `integrate_state` receives each stage's own t (`vehicle.cpp:479`).
- **RK4 and model-owned state.** `Integrator` advances the combined buffer, the rigid body plus each model's
  slice (ADR-0021). RK4 is in-house (ADR-0004).
- **The M4 law.**
  - Sources: `embedded/autopilot/autopilot.cpp:38-85`, `pid.hpp`, `gains.hpp` and `limits.hpp`.
  - Its state: three PID integrators, the last commands and the previous frame time.
  - dt is `now − previous_now`, where `now` is `hal_now()`, which is the request's `t` (`main.cpp:59`). A dt
    that is non-finite, not above 0, or above 1 s integrates as 0.
  - Its hold mask is `WIND_INVALID`, or any non-finite field it reads.
  - The heading error is wrapped by `std::remainder`. The mode word is 1, and the reply is zeroed first.
- **M4's scenario.**
  - The configs are `aircraft/notional_transport/configs/cruise_{light,moderate}_turbulence.yaml`: the shm
    transport, divisor 10, 100,000 frames, Dryden seed 42, and no `log` block.
  - The setpoints are 10,000 m, 230 m/s and a 0 rad heading.
  - The pytest runs take their trim from `astra.lockstep.trim_for`, which matches the config's literals to
    1e-12 (`test_trim_matches_config_initial_state`). `autopilot_argv` passes each float with `repr`
    (`python/astra/lockstep.py:174-202`, `:359-387`).
- **`astra.lockstep` accepts only `plugins/sim/transport/shm`** (`lockstep.py:277-282`). The tools refuse any
  `transport` block (ADR-0029, Q9 (b)). An in-process peer therefore runs through `astra._core.Simulation` or
  C++.
- **The build flags** (`cmake/AstraBuildFlags.cmake`; `build/python/compile_commands.json`, read in plan
  mode).
  - Both `embedded/autopilot/autopilot.cpp` and the transport plugins compile with `clang++-18 -O3 -DNDEBUG
    -fPIC`, plus the shared warning set.
  - Only `-std` (c++17 against c++23) and the plugins' `-fvisibility=hidden -fvisibility-inlines-hidden`
    differ.
  - Neither sets `-march`, `-mtune`, `-ffp-contract`, `-ffast-math` or any of its parts, or `-fno-math-errno`.
    clang-18's default x86-64 target has no FMA, so its default `-ffp-contract=on` can form no fused operation.
  - The Debug presets use `-g` with no optimization flag. The sanitizer presets add `-fsanitize=address,undefined
    -fno-sanitize-recover=all -fno-omit-frame-pointer`.
- **Test discovery.**
  - `tests/plugins/` is a plugin discovery root that `scripts/config_keys.py` skips, so test plugins need no
    key block (PR 11a's Q7).
  - `tests/plugins/nesc_control_variant` already holds the shipped peer through its header and its `Options`
    struct (`nesc_control_peer.hpp:72-80`).
  - Per-build-type registration has a precedent: the Release-only and ASan-only lockstep tests
    (`tests/lockstep/CMakeLists.txt:66`, `:91`).
- **PR 11a's guard.** `F16Control.TrackedConfigsPairThePeerWithTheRotatingEarth` fails any *tracked* config
  that pairs the shipped peer with an Earth other than `wgs84_rotating`.
- **The Earth and gravity models.**
  - `earth/round` is a sphere, non-rotating by default (`plugins/physical/earth/round/round.cpp:91`).
  - Its kinematics and transport rate (`:99-121`) and `gravity/inverse_square`'s g_n (`:114-133`) are
    sign-consistent under a reflection in a meridian plane.
- **Gate time.**
  - The recorded baseline is 1001 s (`docs/future-work.md:163`, PR #75).
  - PR 11a's CTest runs took 115, 12, 235 and 445 s in the debug, release, static-plugins and asan presets,
    and pytest took 140 s (#119's body).
  - The current gate is therefore probably above the baseline already, which is why PR 2 measures its base
    first.

## Oracle (a): the exact sampled-data check

**Plant** (`tests/plugins/linear_plant`, test-only).
- A `Model` owning n continuous states through ADR-0021's hooks, with ẋ = A x + B u.
- A, B and x0 come from config. u is m latched inputs, bound by name (ADR-0017).
- It has no wrench and no aero, mass or environment of its own. It reads only its stage-buffer slice and its
  latched inputs.
- The config still has `physical` (`earth/flat`, a constant-mass rigid body), because a transport requires it.
  Nothing reads that rigid body.

**Law** (`tests/plugins/state_feedback_peer`, test-only). It is a `Transport` in `nesc_control`'s shape under
ADR-0029: in-process, `transport.peer` keys, a schedule keyed to frames, a zeroed reply that echoes the frame,
no clock and no allocation.
- At exchange k (frame kN) it computes u[k] = −K x(kN) + G r[k]. r[k] is the reference in force from the first
  exchange at or after its schedule frame, as ADR-0029 Decision 3 does it.
- u leaves in ICD v1's `elevator` field. It passes the exchange's validation, limits, command map and latch.
- **It reads x from the plant's published stage-buffer pointers** (Q4, decided (R)), resolved at `init`. It
  still receives the ICD request, and it refuses one (a non-finite reply, so the exchange halts `kTransport`)
  whose `version` is not ICD v1's, whose `frame` is not the next kN, or whose `t` is not frame·dt.
  - N and dt are its own keys; a test asserts that its dt key equals `sim.dt`.
  - pytest asserts the same three facts on the recorder's request stream.

**The gap this leaves, in plain words** (Q4).
- **What oracle (a) checks:** the command leg over ICD v1, which is the reply, the exchange's validation, its
  limits, the command map and the latch; and the exchange's timing and divisor.
- **What it does not cross:** the measurement leg reads the plant's state pointers, not the ICD packer.
- **Where the packer is covered:** the packer's frame-start sampling is
  `tests/python/test_lockstep.py::test_r12b_icd_matches_state`, M4's R12b. At every exchange frame of runs A
  and F, it checks the packed `phi`, `theta`, `psi`, `altitude`, `lat`, `lon`, `u`, `v`, `w`, `p`, `q` and `r`
  against the same frame's recorded state to 1e-12. Its liveness half shows that a one-frame lag would fail.
- **Why that covers every transport:** the exchange packs the request with the same `icd_pack::pack_sim_to_sw`
  call for any transport (`lockstep_exchange.cpp:222`).

**The plant, and the order RK4 needs.**
- **The plant.** A chain of four integrators: A = S/τ, where S is the 4×4 upper shift, and B = e₄/τ. τ = 0.01 s,
  so 1/τ = 100 is exact in binary, and hA = 0.1·S per frame.
- **Why RK4 is exact here.** Write the held input as a state: y = (x, u) and ẏ = M y, with M = [[A, B], [0, 0]],
  a single nilpotent Jordan block of size n + 1.
  - Over one frame, with u constant through all four stages, classical RK4 gives y₁ = T₄(hM)·y₀, where
    T₄(z) = 1 + z + z²/2 + z³/6 + z⁴/24.
  - The exact flow is e^(hM) = Σ (hM)^j/j!.
  - The two are equal for every y₀ exactly when (hM)^j = 0 for j ≥ 5, that is when M⁵ = 0, that is when
    n + 1 ≤ 5.
- **The order needed.** A chain of at most four states. Four is the longest that classical RK4 integrates
  exactly, and it is the case used.
  - For n = 5, the first omitted term is (hM)⁵/120 ≈ 8.3e-8 of the state per frame at hA = 0.1. That is about
    eight decades above roundoff, so a five-state chain is the order control, and it must fail check F.
  - **The order control's own gains and reference** (amendment 9), each pre-registered in PR 2's plan:
    - its K places the sampled closed loop's five poles at z = 0.5, in rationals, as for n = 4;
    - its reference is the same exact solution with the sums run to i = 4 (A⁵ = 0), so it stays exact;
    - its F bound keeps the same derived form as the nominal one;
    - its failure, at about (hM)⁵/120 ≈ 8.3e-8 of the input's contribution to x₁ per frame, therefore sits
      about eight decades above that bound wherever the input and the states are of one order. PR 2's plan
      states that margin as a number before the first run.
  - The ratio h/τ = 0.1 is what makes that residual visible. With τ = 1 s, it would be about 8e-18, below
    roundoff.
- **The reference** (Python, `fractions.Fraction`; pytest in the gate). **It shares no code with core,
  `Integrator`, or the peer** (amendment 4).
  - pytest reads A, B, K, G and h from the config text with `float()`, never from the sim. `float()` rounds as
    `strtod` does, so these are the doubles the sim parses. Each double then enters the reference as an exact
    `Fraction`.
  - Φ_j = Σ_{i=0..n−1} (jhA)^i/i! and Γ_j = Σ_{i=0..n−1} (jh)^(i+1) A^i B/(i+1)!. These are finite sums, because
    Aⁿ = 0: they run to i = 3 for the chain of four, and to i = 4 for the order control's five. Either way they
    are exact rationals for a frame (j = 1) and for an exchange (j = N).
- **The checks**, each against a bound pre-registered in PR 2's plan before the first run:
  - **F, per frame.** x_sim(j+1) is checked against Φ₁·x_sim(j) + Γ₁·u_hold(j), where u_hold(j) is the reply of
    the exchange at floor(j/N)·N. The bound is a running roundoff bound for this RK4 on this system: each
    rounding is charged at eps·|its own result| and carried to the output by the exact linear coefficients
    (lesson 2026-10-13). No fitted constant.
    - **The operation order it charges** (amendment 3) is read from core's RK4
      (`core/include/astra/kernel/integrator.hpp`) and from the plant's `integrate_state`. PR 2's plan cites
      those lines.
    - The bound is fixed in PR 2's plan before the first run.
    - A miss on a nominal run stops PR 2 and goes to Joe. Neither the bound nor its derivation moves
      afterward.
  - **L, per exchange.** The recorded reply u[k] is checked against the exact −K·x_sim(kN) + G·r[k], with r[k]
    from Vol-II-style frame semantics: the first exchange at or after the step frame. The bound is the standard
    dot-product bound, γ_(n+1)·(Σ|K_i x_i| + |G r|).
  - **C, the closed loop.** x_sim(kN) is checked against the exact closed-loop recursion from x0,
    x[k+1] = (Φ_N − Γ_N K)·x[k] + Γ_N G r[k]. The bound accumulates F's and L's per-exchange bounds through
    Σ_j ‖(Φ_N − Γ_N K)^j‖, computed exactly before the run.
  - **The request.** Every request's `frame` is kN, and its `t` is the double frame·dt.
- **Runs.**
  - 2,001 frames at each of N = 10 (the default), 3 and 1.
  - The reference steps at frame 1005, a multiple of neither 10 nor 3.
  - The gains place the sampled closed loop's poles at z = 0.5 for each N, in rationals, before the first run.
    A stable, non-deadbeat loop keeps every negative-control run finite.
  - pytest drives `astra._core.Simulation` frame by frame and reads the plant's slice of the state vector.
  - A ctest smoke runs plant plus peer for 200 frames in all four presets (sanitizers, no allocation).

**Negative controls** (planted in the test plant through a `test_defect` key, never in core). Each emulates, at
the plant's input, the observable effect of the core defect it names. The expected magnitudes are
pre-registered in PR 2's plan.

| Control | Planting | Fails (the discriminating objects) | Does not fail |
|---|---|---|---|
| A command one frame late | The plant integrates frame j with the input latched at frame j − 1, recorded in its `step()` before the latch | F at every hold's first frame kN with u[k] ≠ u[k−1]; C from the first exchange | L |
| A hold one frame long | Only at the step's exchange frame, the plant keeps the previous input for one more frame | F at that one frame; C after it | L; F at every other frame |
| The wrong divisor | The config's `transport.divisor` is N + 1, with the gains designed for N | The requests' `frame` fields; F at the first hold boundary where N and N + 1 differ; C | L, evaluated at the actual exchange frames |
| The command applied at a stage other than the latched one | At each exchange frame, RK4 stage 1 (t = the frame start) reads the previous input, and stages 2 to 4 read the new one | F at every exchange frame where u changes (residual about h/6·B·Δu, carried through the chain); C | L |
| The order control | A five-state chain, A = S/τ (5×5), with its own pre-registered gains and its own exact reference (amendment 9) | F at every frame with nonzero u, at about (hM)⁵/120, about eight decades above F's bound | none |

"The stage a command is applied at" has two readings, and both are covered (Q5):
- **The RK4-stage reading:** the command switches inside the step. This is the fourth control.
- **The scheduler-stage reading:** the command takes effect in the post-step. That is the first control's
  reading, a command one frame late.

## Oracle (b): the port pattern on M4's autopilot

**The peer** (`aircraft/notional_transport/sim/transport/autopilot`, C++23). It is a second implementation of
M4's law, an in-process `Transport` under ADR-0029 Decision 1's location rule.
- It has the same setpoints, trims, gains, limits, hold mask, conditional-integration PID, dt rule, heading
  wrap, mode word 1 and zeroed reply.
- It keeps the port's floating-point expression order, per ADR-0029's Consequences: the same factors in the
  same order, `std::remainder`, and the same clamp and anti-windup comparisons.
- No include runs from `aircraft/` into `embedded/`.
- Its gains are its own named constants, and a test pins each one equal to `gains.hpp`'s value.
- It publishes `transport.peer.handled` and `transport.peer.held`, the counts the autopilot's exit line prints.
- Whether it ships is Q7.

**The equivalence** (ADR-0029 Decision 8's criterion, bitwise, on its objects).
- **The two runs:**
  - the shm run, through `astra.lockstep` with the real `astra_autopilot`;
  - the in-process run, the same config with the `transport` block naming the peer, through
    `astra._core.Simulation`.
- **The scenario** is M4's at both Dryden levels: light and moderate, seed 42, 100,000 frames, the setpoints
  above.
- **One trim result** feeds both runs (Q8).
- Both runs get the same added `log` block.
- **The objects, bitwise:**
  - `complete = 1`, the same `frames_run` and exchange count;
  - the recorder's request and reply streams at every exchange, and its state stream at every frame;
  - every signal name, unit and rate group, and every state and physics column;
  - `/final_state`;
  - the stored config and `/plugins`, after removing only the `transport` block and the transport's row,
    with every physics plugin's SHA-256 equal;
  - the autopilot's `handled 10000 held 0` against the peer's two counts.
- **Excluded,** and named: the `transport` block, the transport's `/plugins` row, and the `transport.*` signals.
- Both runs use one build and one worktree state (lessons 2026-10-04 and 2026-10-08, `git_describe`).

**The flags, stated before the first comparison** (the obligation from PR 12):
- the facts above, restated from PR 3's own build;
- a test that reads `compile_commands.json` for both translation units and asserts that their floating-point
  flag sets are equal. The set is every `-O`, `-march`, `-mtune`, `-m` with fma, avx or sse, `-ffp-contract`,
  `-ffast-math` and its parts, `-fno-math-errno`, `-ffinite-math-only`, `-funsafe-math-optimizations`,
  `-freciprocal-math`, `-fno-signed-zeros` and `-fno-trapping-math`.
- A difference is a porting defect until shown otherwise. If a stated floating-point cause makes bitwise
  equality infeasible, it goes to Joe.

**Unit-level equivalence** (ctest, all four presets, so -O0 is covered as well as -O3).
- Both implementations are fed identical frame sequences, hand-built and seeded. They cover held frames,
  `WIND_INVALID`, non-finite fields, saturation, anti-windup at and across each limit, and the dt edges: the
  first frame, a gap above 1 s, a repeated `t` and a backward `t`.
- Replies, `handled` and `held` must be bitwise equal.
- The test links `astra_autopilot_lib` and includes the peer's header.

**Negative controls** (`tests/plugins/autopilot_peer_variant`, holding the shipped peer through its header, as
`nesc_control_variant` does). Amendment 2 sets the rules:
- each control's first differing exchange must be certain, and is pre-registered;
- each is pre-registered to stay inside M4's ±30 m altitude and ±2° heading bands at both levels; that is what
  shows the defect is smaller than any band;
- a control that does not break equality stops the PR;
- a first break at any exchange other than its pre-registered one also stops the PR and goes to Joe.
Both results, the first differing exchange and the bands, are reported.

**Why the earlier draft's four controls were replaced.** None of them had a certain first break:
- a one-ulp gain change, a reassociated sum, or a 0.01 s literal dt is usually rounded away in the sum it
  feeds, or changes it only on some exchanges;
- the `<` written `<=` fires only on an exact tie.
Joe's two example defects:
- **"The clamp applied before the trim offset" would never fire either.** No command saturates in M4's runs
  (M4 PR 6's trial: no channel came near a limit), and inside its range the clamp is the identity, so
  `clamp(offset + y)` equals `offset + clamp(y)` bit for bit. It is not used.
- **"The PID's integrate-then-output order"** is used, as control 2.

| # | Planted defect (a porting slip) | First differing exchange, pre-registered | Why it is certain |
|---|---|---|---|
| 1 | The elevator trim stored in single precision (`static_cast<float>`) before it enters the offset | exchange 0 (frame 0), `elevator` | The trim literal is not representable in float (checked from the config text before the run). Its float error, about 1.6e-9 rad, enters the elevator additively, unsaturated, far above the output's ulp (about 3.5e-18 rad). |
| 2 | The PID returns its output computed before its integral update (output, then integrate) | exchange 1 (frame 10), `throttle` | At exchange 0, dt = 0 and nothing integrates, so nothing differs. At exchange 1, the speed loop's increment kSpeedI·e·dt exceeds half the throttle's ulp whenever \|230 − tas\| > 1e-10 m/s. The test asserts that from the shm run's recorded frame-10 request before comparing. |
| 3 | `kPitchRate` stored in single precision | exchange 1 (frame 10), `elevator` | At frame 0, q = 0 exactly (the IC's ω_b = 0), so k_q·q contributes nothing. At frame 10, the float error (about 2e-9) times q exceeds half the elevator's ulp whenever \|q\| > 1e-9 rad/s, which the test asserts from the shm run's recorded frame-10 request. |
| 4 | The mode word written 0 instead of `kModeFlags` = 1 | exchange 0 (frame 0), `mode_flags` (the reply stream only) | The field is constant and differs at every exchange. It has no physical effect, so the bands cannot see it at all. |

## Oracle (c): symmetry tests

**Geometry for the mirror pairs** (Q9). The mirror is a reflection in the meridian plane through the initial
position, with the initial course north.
- **The models:** `earth/round` with `rotating: false`, `gravity/inverse_square` with `rotating: false`, ISA-1976
  and still air.
- **The IC:** case 11's latitude (36.01916667°), longitude exactly 0, 10,013 ft, case 11's airspeed, and heading
  0. It is trimmed by `astra trim --rate-convention n_frame` on that Earth: one trim result, shared by every
  run.
- **The mirror map.** Under this reflection every quantity keeps or flips its sign: position (lat, −lon, h),
  v_b → (u, −v, w), q_nb → (w, −x, y, −z), and ω_b → (−p, q, −r). No component is permuted, so a sign-symmetric
  computation can be equal under it bit for bit.
- **The pairs.**
  - 13.3's shape: base course 0, a course step to +15° and to −15° at frame 15000, 30,001 frames.
  - 13.4's shape: base course 0, a lateral offset of +2,000 ft and −2,000 ft at frame 20000, 60,001 frames.
  - Both: altitude and EAS commands constant at the IC's values. The two runs of a pair are identical before
    the step.
- **The objects.** The exchange recorder's three streams for both runs. Each request, reply and state record of
  the "right" run equals the "left" run's record with the parity mask applied, compared by value (`==`, so
  +0.0 equals −0.0), at every frame and exchange.
  - Odd fields: lon, v, q_nb's x and z, p and r; the ICD's phi, psi, p, r, v, beta, lon and ay; the replies'
    aileron and rudder.
  - Every other field is even.

**Asymmetry inventory** (from ASTRA's realization, which R2 and R11 of the M7 plan pin to the DMLs; PR 4's
first commit adds the parity probe and the tables' figures as evidence, before any flight).

| Component | Operation, and its parity under the mirror | Expected |
|---|---|---|
| EOM, quaternion kinematics, renormalization, RK4 | Every term of each component has that component's parity; same code, same order | bitwise |
| `earth/round` (non-rotating) | lat_dot, lon_dot, the transport rate (v_e/r, −v_n/r, −v_e·tan lat/r): even, odd, axial-consistent | bitwise |
| `gravity/inverse_square` (non-rotating) | (0, 0, g(r)), plus an exact-zero centripetal term | bitwise |
| ISA-1976, engine | Functions of altitude, Mach and throttle; thrust on +x, no moment (PR 6c) | bitwise |
| Mass | Izx even (an xz product); Ixy = Iyz = 0 | bitwise |
| Air data and Euler angles | α = atan2(w, u) even; β = atan2(v, hypot(u, w)) odd; ψ, φ = atan2 odd; θ = −asin even (`frames.hpp:82`, `air_data.hpp:37-43`) | bitwise if libm is exactly odd/even (probe) |
| ICD packing | Copies and the above; vertical speed even; ay from the previous frame's wrench, odd | bitwise |
| Aero: `absCl0`, `absCn0`, cy0, cz1, damping, the MRC-to-CM transfer | Odd or even term by term (`aero_math.hpp:203-266`) | bitwise |
| **Aero: `dclda`, `dcldr`, `dcnda`, `dcndr` at signed β** | Data not even; interpolation not bitwise even | **breaks**: the figures in finding 2. At \|β\| ≈ 1°, about a tenth of the ±10° differences: about 7 % of `dcnda` and 1 % or less of the others |
| Peer: the LQR rows, saturations (±L), the interconnect, chiErr's wrap (\|x\| < π here), EAS, the cross-track (χ = 0, lon0 = 0) | Odd or even (`control_law.hpp:212-279`, `:374-379`) | bitwise |
| **Peer: Ω subtraction on a non-rotating Earth** | Ω_n fixed, not reflected | **breaks**: a spurious −6e-5 rad/s in the law's p and +4e-5 rad/s in its r at the trim; 0.005° to 0.007° of aileron and 0.003° of rudder from engagement |
| Signed zeros | The β-sign branch at β = ±0 gives +0 in both runs (the β = 0 row is 0); atan2 is never called with zero y and negative x here (heading north, u > 0, c33 > 0) | value-equal |

So a **bitwise** mirror needs four things:
- the non-rotating sphere and the meridian geometry;
- a peer that subtracts no Earth rate (Q10);
- the four β tables read at |β| (Q11);
- libm's parity, confirmed by the probe.
The variant pairs keep the bitwise (value-equal) check.

**The shipped pairs are gated too, under a modeling bound** (amendment 1). A shipped pair keeps both
asymmetries: Ω subtracted, and the signed-β tables.
- **The bound.** PR 4's plan pre-registers, for each shipped pair and each channel, a bound labeled a modeling
  bound. It is derived from the inventory's own figures (the four tables' β asymmetries and the Ω term),
  carried through a stated sensitivity estimate of the closed loop. It is fixed before the first flight.
- **A miss** stops the PR and goes to Joe. The bound is never widened.
- **Reported as well:** each shipped pair's largest asymmetry, against the inventory's estimate.
- The shipped pairs therefore check two things: that the model's known asymmetries account for its whole
  mirror defect, and that each asymmetry breaks the bitwise mirror, as a negative control.

**Heading invariance** (Q12).
- **The flight.** `earth/flat`, `gravity/constant` and ISA-1976. 13.3's shape is flown at ψ0 = 0, 0.6 rad and
  172.5°; the last crosses ±180°, so the wrap is exercised.
- **One trim result** (θ, elevator, throttle at ψ0 = 0) is reused, with only the initial quaternion's heading
  changed.
- **The peer.** It needs Q10's hook: the shipped peer refuses a flat Earth.
- **The comparison:**
  - v_b, ω_b, φ, θ, wrapped ψ − ψ0, α, β, altitude and the replies;
  - the horizontal position, rotated back by −Δψ.
- **The bound is roundoff, not bitwise.** The rotated quaternion and DCM entries are different numbers, not sign
  flips, and chiErr = β + ψ − course rounds differently. 13.4's shape cannot be flown on a flat Earth (finding
  3).

**Negative controls.**

| Control | Fails |
|---|---|
| The shipped peer's Ω on the non-rotating Earth (the shipped pair) | the bitwise mirror, from engagement |
| The shipped signed-β tables (the shipped pair) | the bitwise mirror, after the step |
| Planted: the aero variant drops β's sign on `absCl0` (clt = absCl0 for every β) | the mirror, after the step |
| Planted: the peer variant omits chiErr's wrap | heading invariance at ψ0 = 172.5° |

The shipped peer on a non-rotating Earth flies only through the variant plugin with its default `Options`. The
configs are built in the test from committed text, so PR 11a's tracked-config guard keeps holding.

## Acceptance

| ID | Requirement | Check |
|---|---|---|
| R1 | (a) The order: the plan's derivation, and the five-state control fails F | pytest; PR 2's plan |
| R2 | (a) F, L and C within their pre-registered bounds at N = 10, 3 and 1; each request's `frame` = kN and `t` = frame·dt | pytest against the `Fraction` reference |
| R3 | (a) Each of the four negative controls and the order control fails on the objects its row names, by its pre-registered margin | pytest |
| R4 | (a) The plant and peer under sanitizers, with zero allocations over their frames | ctest, four presets |
| R5 | (b) Unit-level replies, `handled` and `held` bitwise equal on hand-built and seeded sequences | ctest, four presets |
| R6 | (b) The shm and in-process runs bitwise equal on ADR-0029 Decision 8's objects, at both Dryden levels | pytest (Release) |
| R7 | (b) Both builds' floating-point flags stated before the first comparison, and asserted equal from `compile_commands.json` | pytest; PR 3's plan and body |
| R8 | (b) Each planted porting defect breaks bitwise equality first at its pre-registered exchange, with its precondition asserted from the shm run's recorder, and stays inside M4's bands at both levels (amendment 2) | pytest |
| R9 | (b) If it ships (Q7): the key block, the regenerated config-key reference, configs consuming its keys, and `astra run` exiting 65 on them | `config_keys.py --check`; pytest |
| R10 | (c) The inventory evidence and libm's parity probe (seeded arguments over the flights' ranges, plus edges; every preset) before any flight. A failed item is reclassified to roundoff and goes to Joe before any flight | ctest |
| R11 | (c) Both variant mirror pairs are equal under the parity mask by value, at every frame and exchange of all three recorder streams | ctest (release) |
| R12 | (c) Each shipped pair within its pre-registered per-channel modeling bound (amendment 1); its largest per-channel asymmetry reported against the inventory's estimate | ctest (release), PR body |
| R13 | (c) Heading invariance within Q12's pre-registered yardstick bound at each heading | ctest (release) |
| R14 | (c) Each negative control fails as its row says | ctest (release) |
| R15 | The hooks (Q10, Q11) leave shipped behavior bit-identical: before-and-after recorders of 13.3 and 13.4, and of case 11's open-loop run, byte-equal on one build; F16Aero's and F16Control's tests unchanged | evidence and ctest |
| R16 | Records and gate: each code PR's full gate (`./scripts/check-full.sh`, exit 0 and `check: OK`); its added time per preset and its whole-gate wall time against 1001 s; review per tier | PR bodies |

## Joe's decisions (2026-10-15)

The plan is approved on Joe's word. Each question below carries his answer. Where an earlier recommendation and
an answer differ, the answer governs.

**Amendments**, each folded into the section named:
1. **Oracle (c): the shipped pairs are gated, not only reported, under a pre-registered modeling bound**
   (oracle (c), R12, Q1's allowed bounds).
   - The bound is derived from the inventory's own figures through a stated sensitivity estimate, and fixed
     before the first flight.
   - A miss stops the PR and goes to Joe. It is never widened.
   - The variant pairs keep the bitwise (value-equal) check.
2. **Oracle (b): each negative control has a certain, pre-registered first differing exchange** (oracle (b),
   R8). A control that does not break equality stops the PR. The `<=` control is replaced, and so are the
   other three, for the reason stated there.
3. **Oracle (a): F's operation order is read from core's RK4 and the plant, with the lines cited,** and the
   bound is fixed before the first run. A nominal miss stops PR 2 (oracle (a)).
4. **Oracle (a): the reference shares no code with core, `Integrator` or the peer,** and reads A, B, K, G and h
   from the config text with `float()` (oracle (a)).
5. **The critique's focus gains two items** (Plan critique, items 6 and 7).
6. **A carried item:** `aircraft/f16/tests/aero_test.cpp:756` (Carried items).
7. **PR 0's scope** (PR 0).
   - It writes only the plan, the critique record, PLAN.md's block and Planning line, and architecture rev 15.
   - Its checks: `just docs-check`; `git diff --check origin/main..HEAD`; fetch and merge `origin/main` in if
     it moved.
   - Then `/wrap`, and stop.

Joe's second approval (2026-10-15) adds three more, folded in before PR 0's first commit:
8. **PR 4's scope row** reads "the shipped pairs gated under their modeling bound (R12) and reported" (PR
   decomposition).
9. **The order control has its own gains and its own exact reference,** pre-registered in PR 2's plan, and the
   plan states its failure margin above F's bound (oracle (a)).
10. **§16's M7b row ends with "and the shipped F-16 models stay within a pre-registered modeling bound"** (Q2).
    Nothing else in the row changes.
He also accepted the replacement of the four (b) controls, and the rejection of the clamp example, as
reasoned.

## Questions, each with its recommendation and Joe's answer

- **Q1. An ADR for the in-house oracle rule?** *Recommended: yes, ADR-0034, in its own docs-only PR 1 (Tier 3),
  with architecture rev 16.*
  - **Decided (Joe): yes, as recommended.** The pre-registered constants stay in the tests with their
    derivations, not under `tests/golden/`.
  - **The bounds the record allows:**
    - bitwise;
    - roundoff, by derivation;
    - a modeling bound, derived from a stated, measured asymmetry of the model (amendment 1);
    - a measured sensitivity yardstick (Q12), only where a derivation cannot reach, with the yardstick's factor
      and perturbation set fixed before any run.
  - It extends ADR-0029 Decision 8's criterion to any sim-side peer and its port, and supersedes nothing.
  - **The gap.** ADR-0013 governs an external ensemble band. ADR-0029 Decision 8 governs the bitwise port
    criterion for the NESC laws only. Neither covers an in-house reference, a bound pre-registered before the
    first run, or the requirement for negative controls. validation.md says only "never derive a tolerance
    from ASTRA's own output" and "escalate when there is no independent reference".
  - **What the record would say:**
    - the reference shares no code with the path under test;
    - the bound is derived (bitwise, or roundoff with its derivation) and fixed in the PR's plan before the
      first run, never moved afterward without Joe;
    - each oracle carries negative controls, each pre-registered to fail on a named object, and a control that
      passes stops the PR;
    - bounds and comparison sets are oracle data under AGENTS.md's boundary;
    - it extends Decision 8's criterion to any sim-side peer and its port.
  - **Alternative:** no ADR; a paragraph in validation.md's numerical section, added in PR 2.
  - **Sub-question:** do the pre-registered constants live in a guarded file under `tests/golden/`? Recommended:
    no. They stay in the test, each with its derivation, under the review rule.
- **Q2. The block's name and its §16 row.** *Recommended: "M7b Closed-loop oracles"*, the brief's name, after
  M7's lettered-PR convention. The row this PR adds (rev 15), marked planned:
  > M7b | *Planned* (PLAN.md M7b). Three in-house closed-loop oracles, each in `just check`, each with an
  > independent reference, a bound pre-registered before its first run, and negative controls that fail. A
  > linear plant with model-owned state (*ADR-0021*), under discrete state feedback through the exchange stage
  > (*ADR-0022*), matches its exact sampled-data solution at every frame to stated roundoff, and a late command,
  > a long hold, the wrong divisor or a command switched inside the step each fail. A sim-side in-process peer
  > of M4's autopilot is bitwise equal to `embedded/autopilot` over M4's acceptance scenario at both Dryden
  > levels, on *ADR-0029* Decision 8's objects, with both builds' floating-point flags stated. The F-16 under
  > `F16_control` flies 13.3's course change and 13.4's side step on a non-rotating Earth as mirrored pairs,
  > equal under the mirror where every operation is sign-symmetric, and 13.3's on a flat Earth is
  > heading-invariant to stated roundoff, and the shipped F-16 models stay within a pre-registered modeling
  > bound.

  **Decided (Joe): the name and the row as drafted,** with amendment 10's closing clause ("and the shipped F-16
  models stay within a pre-registered modeling bound"), so the row matches R12 after amendment 1.
- **Q3. Tiers and gate time.**
  - *Recommended:* the PR table's tiers.
  - (c)'s long flights (about 0.75 M F-16 frames) are registered in the release preset only, as M4's
    Release-only and ASan-only lockstep tests are. The debug, asan and static-plugins presets get 1,000-frame
    smokes of each hook.
  - Every flight in all four presets would add an estimated 200 s each to asan and static-plugins, at PR 11a's
    rate.
  - PR 2 first measures the base gate's wall time on `origin/main`, quiet machine, then each PR its own delta.
  - **Decided (Joe): as recommended.** The PR table's tiers stand. (c)'s long flights run in the release
    preset only, with 1,000-frame smokes in the other three. PR 2 measures the base gate on `origin/main`
    first, and every code PR reports its delta against 1001 s.
- **Q4. (a)'s measurement leg.** *Recommended: (R).*
  - **(R)** The peer reads x from the plant's published stage-buffer pointers at the exchange, and asserts the
    request's `frame` and `t`. The command leg is ICD v1's reply, the exchange's validation, the command map
    and the latch. The ICD packer's own same-frame timing is already M4's R12b.
  - **(K)** Realize the plant in the rigid body (flat Earth, zero gravity, unit mass, no rotation): altitude ←
    vertical speed ← a force set by u, all over ICD v1. That is a chain of only two, no A or B from config, and
    a mass model in the loop, against the brief's plant.
  - **Decided (Joe): (R).** The spec records the gap in plain words and names the test that covers the
    packer's frame-start sampling, M4's R12b (oracle (a), "The gap this leaves"). The peer still receives the
    ICD request and asserts its version, frame = kN and t = frame·dt.
- **Q5. The four controls' readings** (the table under (a)).
  - "One frame late" is read as systematic: every reply first acts at kN + 1.
  - "A hold one frame long" is read as one hold, at the step's exchange, lasting N + 1 frames.
  - "A stage other than the latched one" is read as the command switching after RK4 stage 1. A command applied
    in the post-step is the same as one frame late.
  - *Recommended as written,* with the five-state order control added.
  - **Decided (Joe): the four readings stand, and the order control is added.** The RK4-stage reading is
    accepted. The scheduler-stage defect, a command taking effect in the post-step, is control 1's reading, so
    both are covered.
- **Q6. (a)'s plant and law** as specified (the chain of four, τ = 0.01 s, poles at z = 0.5, N = 10, 3 and 1, a
  step at frame 1005, a `Fraction` reference in pytest). *Recommended as written.* PR 2's plan fixes the gains
  and the bounds' numbers before the first run.
  - **Decided (Joe): as written.** PR 2's plan fixes the gains and every bound's number before the first run.
- **Q7. Does (b)'s peer ship, or stay test-only?** *Recommended: ships,* at the brief's path.
  - It is the worked example of the pattern for §18 step 5: write the law sim-side, prove it, port it, prove
    bitwise.
  - Costs: a key block, the regenerated config-key reference, and two tracked configs, copies of M4's with the
    `transport` block replaced, with a drift test. `astra run` refuses them by design.
  - Alternative: test-only, at `tests/plugins/autopilot_peer`, with no key block and no tracked config.
  - **Decided (Joe): ships,** at the brief's path, with the key block, the regenerated reference, the two
    tracked configs and their drift test.
- **Q8. (b)'s one trim result.** *Recommended: the config's committed literals* (`initial_controls` and
  `euler_nb[1]`), written once and passed to the autopilot by `repr` and to the peer as `transport.peer.trim.*`
  text. A test asserts they parse to the same doubles. This follows ADR-0029 Decision 8, "written once into
  the shared config". M4's own runs keep `trim_for`, which matches those literals to 1e-12, not bitwise.
  - **Decided (Joe): the config's committed literals, written once,** with the parse-equality test as proposed.
- **Q9. (c)'s geometry.** *Recommended: the meridian mirror* (non-rotating sphere, longitude 0, heading north).
  - 13.x's own IC (heading 45°, longitude −75.67°) has no representation mirror: the reflection mixes north and
    east, and lon0 + Δ against lon0 − Δ rounds differently. Its pairs could be roundoff at best.
  - A rotating Earth is not symmetric about a meridian plane at all.
  - **Decided (Joe): the meridian mirror** on the non-rotating sphere, longitude 0, heading north.
- **Q10. The peer's Earth rate for (c).** *Recommended:*
  - a new `Options::earth_rate`, `kWgs84Rotating` by default (shipped behavior) or `kNone`. With `kNone`, Ω is
    not subtracted, and `LATLON_NOT_MODELED` is refused only when 13.4's offset needs position, consistent with
    ADR-0029 Decision 2's wording that the refusal applies on paths that read lat or lon;
  - selected only through `tests/plugins/nesc_control_variant`. It edits the shipped peer's header; R15 proves
    the default bit-identical.
  - Alternative: the equatorial mirror on the rotating WGS-84 Earth, heading east from latitude 0. It needs no
    hook, but its mirror permutes quaternion components and maps ψ to π − ψ, so its bound is roundoff, not
    bitwise. Heading invariance on a flat Earth would also be lost.
  - **Decided (Joe): `Options::earth_rate`,** with `kWgs84Rotating` the default, and `kNone` selected only
    through the variant plugin. With `kNone`, the flat-Earth refusal applies only on paths that read lat or
    lon. R15 proves the default bit-identical.
- **Q11. The β tables for (c).** *Recommended:*
  - a defaulted parameter of `build_up`, signed by default (shipped) or |β|, used by a new
    `tests/plugins/f16_aero_variant`. R15 shows the default bit-identical, with F16Aero's check data unchanged;
  - the shipped pair is reported.
  - Alternatives: duplicate `build_up` in the test plugin, pinned bitwise to the shipped one for β ≥ 0. Or
    symmetrize the wrench, (F + M·F·M)/2, which needs no shipped edit but makes the aero's own parity untested.
  - **Decided (Joe): the defaulted parameter of `build_up`,** signed by default, |β| through the aero variant
    plugin. R15 proves the default bit-identical, with F16Aero's check data unchanged.
  - Amendment 1 then gates the shipped pair under a modeling bound.
- **Q12. Heading invariance's bound.** *Recommended: a pre-registered yardstick.*
  - **How it works.** The base heading is flown again with each initial quaternion component moved one ulp, up
    and down. The bound at each sample is a factor F times the largest divergence among those runs. PR 4's
    plan fixes F before any run, from a stated estimate of how per-frame injection compares with an initial
    injection.
  - **Alternatives:** a fixed relative bound (for example 1e-9 of each channel's range), stated as an estimate;
    or report heading invariance and gate only the mirror.
  - In every case, the planted wrap control must fail by orders of magnitude.
  - **Decided (Joe): the yardstick, as a measured sensitivity.** F and the perturbation set are fixed in PR 4's
    plan before any run, and never adjusted afterward. The planted wrap control must fail by orders of
    magnitude.

## PR decomposition

One builder, sequential: PR 1, then 2, 3 and 4 in that order, then 5. PR 2, 3 and 4 touch disjoint paths. Each
code PR opens as a draft with its own short plan citing this one, gets Joe's plan approval and an adversarial
Codex review, and waits for Joe's merge.

| PR | Scope | Tier | Binding ADRs | Depends | Acceptance (beyond the full gate) | Gate added (estimate) |
|---|---|---|---|---|---|---|
| 0 | This planning PR: the spec and plan, the critique record, PLAN.md's M7b block and Planning line, architecture rev 15 (§16's M7b row, planned) | 0 | none | none | `just docs-check`; `git diff --check origin/main..HEAD`; the Codex plan critique and its verification pass | none |
| 1 | ADR-0034, the in-house oracle rule (Q1): the reference's independence, the four allowed bound kinds, negative controls, constants kept in the tests with their derivations; extends ADR-0029 Decision 8 to any sim-side peer and its port; supersedes nothing. Architecture rev 16 (§16's row cites it) | 3 | 0013, 0029 | Q1 | `just docs-check`; adversarial Codex review of the ADR; Joe accepts it | none |
| 2 | Oracle (a): `tests/plugins/linear_plant` and `state_feedback_peer`, the `Fraction` reference, F, L and C, the five controls, the sanitizer smoke; the base gate measured first | 3 | 0004, 0010, 0017, 0021, 0022, 0029, 0034 | PR 1 (ADR-0034 accepted by Joe) | `ctest --preset asan -R LinearPlant --no-tests=error`; `.venv/bin/pytest tests/python -k sampled_data` (R1 to R4) | pytest about 3 s; ctest under 2 s per preset |
| 3 | Oracle (b): the notional transport's autopilot peer, its key block and configs (Q7), the unit-level and lockstep equivalence, the flags check, the four porting controls | 3 | 0007, 0008, 0022, 0024, 0029, 0034 | PR 1; PR 2 (order only); Q7, Q8 | `ctest --preset asan -R AutopilotPeer --no-tests=error`; `.venv/bin/pytest tests/python -k 'autopilot_peer or lockstep'`; `just embedded-standalone`; `python3 -I -B scripts/config_keys.py --check` (R5 to R9) | pytest about 10 s; ctest under 2 s per preset |
| 4 | Oracle (c): the inventory evidence and the parity probe (stop for Joe if any item moves), the hooks (Q10, Q11) with R15's evidence, the variant plugins, the mirror pairs, the shipped pairs gated under their modeling bound (R12) and reported, heading invariance, the controls | 3 | 0001, 0004, 0020, 0021, 0028, 0029, 0034 | PR 1; PR 3 (order only); Q9 to Q12 | `ctest --preset release -R F16Symmetry --no-tests=error`; `ctest --preset asan -R 'F16Symmetry\|F16Aero\|F16Control' --no-tests=error` (R10 to R15) | release about 3 s; smokes under 10 s per preset (Q3) |
| 5 | M7b closeout: PLAN.md check-off with evidence, §16's row status, the three unscheduled M7 items' text updated to point at the proven pattern, open items for held-out scope | 0 | none | all | `just docs-check`; `git diff --check`; one Codex pass | none |

(In the table, `\|` keeps a pipe inside a cell; in a shell it is `|`, per lesson 2026-10-11. The selectors' test
names are the PR plans' to fix, and each PR takes a `-N` count before trusting one.)

## Carried items

- **PR 11b's `rate_frame.py` obligation on case 15 stays with its unscheduled item.** No oracle here flies
  `F16_gnc` or case 15, so none can take it (brief item 6).
- PLAN.md's three unscheduled M7 items stay as written in this PR. PR 5 updates their text.
- STATE.md's open threads that touch files PR 4 may edit carry into PR 4's plan:
  - the stale "M7 PR 13" comments at `aircraft/f16/physical/aero/aero_math.hpp:86` and, if PR 4 edits that
    file, `aircraft/f16/tests/aero_test.cpp:756` (amendment 6);
  - the wrap artifact in `tests/physics/nesc/independent_evaluator.hpp`, if PR 4 edits that file.

## Validation, risks and rollback

- **(a)'s bound is a derivation, not a number to tune.** If F, L or C misses on the nominal run, PR 2 stops and
  goes to Joe. The bound is never widened, and a control is never dropped.
- **(b): bitwise may fail.** That is a porting defect in the new peer until shown otherwise. M4's
  `embedded/autopilot` is the accepted artifact. A stated floating-point cause goes to Joe.
- **(c): a libm function may not be exactly odd or even.** R10 catches it before any flight, and that item
  becomes roundoff with Joe's decision. The hooks touch two shipped Tier 3 files, and R15's byte comparisons
  guard them.
- **Gate time.** PR 2 measures the base. Each PR reports its delta against 1001 s, and a large increase goes to
  Joe (Q3).
- **Rollback.** Each PR reverts on its own. PR 4's hooks revert with their tests, and PR 3's plugin with its
  configs and the reference regeneration.
- **Held out:** `F16_gnc`, cases 15 and 16, the F-16 port, the tutorial, and the shipped peer's Ω refusal on a
  non-rotating Earth (M9's ICD v2 Earth-rate item).

## Plan critique

*Pending.* One fresh, read-only Codex critique of this plan runs after Joe approves it. A verification pass
follows once its findings are folded in and Joe's decisions are recorded, as #104 had. The record goes in
`docs/plans/m7b-plan-critique.md`. The critique's focus:
1. RK4 exactness and the order claim; the reference's exactness; whether F, L and C, and each control, can fail
   by a plausible bug; Q4's measurement leg.
2. Whether (b)'s objects and flags check make bitwise equality meaningful, and whether the porting controls
   are smaller than M4's bands.
3. The inventory's completeness: any operation on the F-16 path with the wrong parity or a permutation;
   signed zeros; the trim's lateral exactness; PR 11a's tracked-config guard.
4. The hooks against ADR-0029 and PR 11a's amendment 1 (no variant code in the shipped `.cpp`).
5. Tiers, dependencies, gate estimates, the questions, and contradictions with the architecture or an
   accepted ADR.
6. Whether amendment 1's modeling-bound derivation is sound and discriminating (amendment 5).
7. Whether Q4's (R) leaves unseen any exchange-timing defect the brief named (amendment 5).

## Done when

R1 to R16 pass on their PRs, and each PR has its tier's review with results in its PR body. PR 5 then checks
M7b off in PLAN.md with evidence links. Joe's approvals and merges are separate.

## PR 0: this planning PR, after Joe's approval

PR 0 writes only what the brief allows (amendment 7): this plan, the critique record, PLAN.md's M7b block and
Planning line, and architecture rev 15. It changes no code, test, config, golden, tolerance or ADR.

1. `git fetch origin`. Create the worktree `closed-loop-oracles` (branch `worktree-closed-loop-oracles`) from
   `origin/main`, `8e525c5` or later.
2. Move this plan-mode draft from the main checkout's `docs/plans/` into the worktree's, then rename it
   `docs/plans/m7b-closed-loop-oracles.md` (`docs/agents/claude-code.md`). Nothing stays untracked in the main
   checkout.
3. Fold in Joe's answers and amendments. Write:
   - `docs/plans/m7b-plan-critique.md`, the procedure and an empty record;
   - PLAN.md's M7b block between M7 and M8, with its Planning line;
   - architecture rev 15: the status line, and §16's M7b row, planned.
   Then commit, push, and open the draft PR.
4. Run the Codex plan critique: fresh, read-only, `task --fresh --effort high`, no `--write`, no `--model`,
   through the Bash tool's `run_in_background`. Verify every finding against its cited lines. Fold in the
   supported findings, explain each rejection, and bring any reopened decision to Joe.
5. After Joe's decisions, run the verification pass on the same route. Fold in its findings.
6. `just docs-check` and `git diff --check origin/main..HEAD`. `git fetch`, and merge `origin/main` in if it
   moved. Update the PR body over REST.
7. `/wrap`, then stop. The merge is Joe's.
