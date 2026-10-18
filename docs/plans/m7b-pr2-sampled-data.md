# M7b PR 2: oracle (a), the exact sampled-data closed-loop check

Status: **approved by Joe, 2026-10-18, on his word**, with his amendments 1 to 12 ("Joe's decisions", below)
folded in before the plan's first commit. Nothing of PR 2 is built or run.
- §1's base gate was measured first after approval (1162 s), and its numbers went into §1 before any other PR 2
  file.
- This file was moved (`mv`, before its first add) from the main checkout's plan-mode draft into the worktree as
  `docs/plans/m7b-pr2-sampled-data.md`. Nothing stays in the main checkout.
- Nothing of the compared output runs before the Codex plan critique closes (§14, step 4).

Owner: Joe. Builder: Claude, one builder.
Tier 3: the first in-house oracle under ADR-0034, with pre-registered bounds; a test transport at the exchange seam;
numerical validation of RK4 on model-owned state.
Base: `origin/main` = `406c407` (#122), read in plan mode; ADR-0034's Status line there reads "**accepted**,
2026-10-18". `git fetch` confirms it after approval.
Binding: ADR-0004, ADR-0010, ADR-0017, ADR-0021, ADR-0022, ADR-0029 (Decisions 1 to 3), ADR-0034.
Sources: [the M7b plan](m7b-closed-loop-oracles.md): oracle (a); F, L and C; the five controls; R1 to R4; PR
table row 2; N-02; Q3 to Q6 as decided; amendments 3, 4 and 9.

## Context

M7b replaces the verification that M7 PR 11b, 12 and 15 would have drawn from the two-member NESC ensemble with
three in-house oracles. Oracle (a) is the first:
- a linear plant with model-owned state (ADR-0021) runs under discrete state feedback through the exchange
  stage (ADR-0022);
- its trajectory must match the exact sampled-data solution at every frame, to a derived roundoff bound;
- a late command, a long hold, the wrong divisor, a command switched inside the RK4 step, and a five-state
  chain must each fail.

ADR-0034's Consequences list what an oracle's plan fixes before its first run. This plan is that
pre-registration: the reference and its independence, the shared inputs, each bound with its kind and derivation,
the comparison set, and each control's named object. A nominal miss, or a control that does not fail as
registered, stops the PR and goes to Joe. No bound, window or margin moves after the first run.

## Joe's decisions (2026-10-18)

Joe approved this plan on his word, with twelve amendments. Each is folded into the section named.
1. **§11 item 1 stands; it is Joe's reading of brief item 3.** The three plant-side controls are the plant's
   `test_defect` key. The wrong divisor and the order control are the configurations the M7b plan's own rows
   define.
2. **§11 item 3 is accepted.** C's reference runs on the 2⁻⁴⁰⁰ grid, with the grid term in C's bound as §7
   states it. It is recorded as this plan's dated departure from the M7b plan's "exact recursion" wording. F and
   L stay unrounded.
3. **§11 item 4 is accepted:** C's bound in elementwise form with the running maximum, as derived in §7.
4. **§10 is accepted.** The plain-Python emulation of §5's order is not the path under test, so it is outside
   ADR-0034 Decision 3's "first run". It is never cited as evidence that the sim passed.
5. **§11 item 6: about 35 s of pytest is accepted provisionally.** It is measured and reported against §1's base
   and 1001 s, and the number comes to Joe at human review. No formula, bound, window or comparison-set change is
   made for speed.
6. **§7, F's output term.** The sim uses c6 = fl(h/6) and the reference uses h/6, and both are known rationals
   before the run. So F charges the exact |c6 − h/6|·|s_i| in place of U·c6·|s_i|, and §7 records the value. This
   replaces a first-order bound with an exact quantity and moves no other term.
7. **§5, the build check.** `Integrator::step` is a template in `integrator.hpp`, instantiated in
   `core/src/kernel/vehicle.cpp`'s translation unit, so the flags that govern the RK4 arithmetic are core's.
   - Before the first run, read `compile_commands.json` for `vehicle.cpp`'s translation unit as well as the two
     plugins'.
   - Assert that none carries `-march`, `-ffp-contract=fast`, `-ffast-math` or `-Ofast`.
   - State all three translation units' flags in the PR body.
   - Do this for every preset the gate builds, not only asan.
8. **ADR-0034 Decision 6.** Every pre-registered constant in `test_sampled_data.py` carries a comment citing its
   section of this plan, so the reviewer can compare each value with the plan's. That covers the gains, G, the
   step frame, each control's divisor, horizon, windows, thresholds and margins, the 2⁻⁴⁰⁰ grid, I, and S̄'s
   rounding rule.
9. **§14 step 4: the Codex plan critique's focus.**
   - §5's operation order, against `integrator.hpp` and the plant's derivative as written;
   - §7's F derivation, including the tie case in §10 and amendment 6;
   - the C derivation's S̄ argument;
   - §9's windows and thresholds, against the exact reference;
   - whether any control can pass because a plausible core defect is absent from the plant's emulation of it;
   - Q4's measurement leg.

   It is given ADR-0034, the M7b plan's oracle (a) section, and this plan.
10. **§1 records** whether `just venv` ran before the timed gate. It also records that the timed command was
    `rm -rf build` followed by `./scripts/check-full.sh`, from the worktree at `origin/main`, so the delta against
    1001 s compares like with like.
11. **§11 item 5 stays a dated correction recorded only in this plan.** The M7b plan's text is not edited.
12. **The approval line** reads "approved by Joe, 2026-10-18, on his word", written after his message. If that
    write were refused, as in lesson 2026-09-29, it would not be retried by another route; the work would carry
    on, and the PR body would name the refused write.

## 1. The base gate (Q3), first after approval

**Measured 2026-10-18,** in the new worktree on `origin/main` = `406c407`, before any other PR 2 file. The steps:
1. **Load check** (lessons 2026-10-02 and 2026-10-04, as in M6 PR 13's step 5):
   - `pgrep -af '^just (check|test|build)'`, `pgrep -af 'python[0-9.]* -m [p]ytest'` and `pgrep -af 'bin/[p]ytest'`;
   - exclude the check's own shell and its ancestors, then `readlink /proc/<pid>/cwd` for each remaining hit;
   - ignore the other project's `just autopilot` dispatcher;
   - `ps -C clangd -o pid,pcpu,etime` must show clangd idle;
   - record `/proc/loadavg` and `nproc` before and after.
2. `just venv` if the fresh worktree has no `.venv`. This is untimed, and the record says whether it ran
   (amendment 10).
3. `rm -rf build && ./scripts/check-full.sh`, from the worktree at `origin/main`, through the Bash tool's
   `run_in_background`, timed. It passes on exit 0 plus its `check: OK` line. #75's 1001 s was also measured from a
   fresh configure, and a new worktree may already hold a `build/` (lesson 2026-09-29), which the `rm -rf` removes.
4. Record the result here:

| Measured | Value |
|---|---|
| Timed command, and the checkout it ran in | `rm -rf build` then `./scripts/check-full.sh`, in the worktree `m7b-pr2-sampled-data` at `origin/main` = `406c407fa13f86cd294e9b962bff1fa44211f1b6`, clean tree, no `build/` before the `rm` |
| Whether `just venv` ran first (untimed) | **Yes:** the fresh worktree had no `.venv`. `just venv` ran first, untimed, exit 0. |
| Exit status and `check: OK` | exit 0; one `check: OK` line |
| Wall time, and its delta against 1001 s | **1162 s** (12:53:01 to 13:12:23 −07:00), **+161 s** against 1001 s |
| Load before and after; `nproc`; other builds | before: 1-minute load 0.30 (0.04 before `just venv`), no build process, no pytest, no clangd; after: 13.70 14.72 9.55 (the gate's own load decaying), no other process; `nproc` 32 |
| Per-preset CTest and pytest times | CTest (real): debug 110.56 s (800 tests), release 12.07 s (801), static-plugins 225.30 s (802), asan 428.92 s (803); pytest 128.56 s (2110 passed, 5 skipped) |

The full log stays outside git. Its trimmed summary is committed as
`docs/reviews/evidence/406c407-gate-summary.log`.

## 2. What is built

Code goes only under `tests/plugins/linear_plant`, `tests/plugins/state_feedback_peer`, `tests/python`, and
`tests/` CMake.
- There is no `core/`, `icd/`, `embedded/`, golden or tolerance change, and `kCoreApiVersion` stays 11.
- Both plugins use the existing `WrenchSource` and `Transport` interfaces.
- `tests/plugins` is skipped by `scripts/config_keys.py`, so the plugins need no key block.

| File | Content |
|---|---|
| `tests/plugins/linear_plant/{CMakeLists.txt, linear_plant.cpp}` | The plant (`astra_add_plugin`) |
| `tests/plugins/linear_plant/linear_plant_smoke_test.cpp` | R4's ctest smoke, declared in the same CMakeLists (§11, item 2) |
| `tests/plugins/state_feedback_peer/{CMakeLists.txt, state_feedback_peer.cpp}` | The law, a `Transport` |
| `tests/python/sampled_data_reference.py` | The reference: `fractions`, `math` and `yaml` only |
| `tests/python/test_sampled_data.py` | The harness: drives `astra._core`, applies the reference, runs the controls and guards |

**The plant** (`tests/plugins/linear_plant`, `name()` "linear_plant").
- It is a `WrenchSource`, because only `WrenchSource::bind_inputs` registers an input
  (`core/include/astra/interfaces/wrench_source.hpp:94`). Its wrench is always zero.
- It owns n states (ADR-0021), with ẋ = A x + B u. Its own state is read through `bind_state`'s stage-buffer
  pointer.
- Keys:
  - `a` (n × n) and `b` (n × m), as lists of rows;
  - `x0` (n);
  - `inputs` (m names, unit "1");
  - `test_defect` (optional: `late_command`, `long_hold` or `stage_one_previous`);
  - `test_defect_frame` (required for `long_hold` only).
- Each block is checked with `is_sequence()` before it is iterated (lesson 2026-10-13).
- It publishes `<vehicle>.linear_plant.a.<i>.<j>` and `.b.<i>.<k>` for §7's parse-identity check. The state
  signals `<vehicle>.linear_plant.state.<i>` are the vehicle's own (`core/src/kernel/vehicle.cpp:194`).
- `step(ctx)` runs every frame, before the latch (`core/src/kernel/simulation.cpp:766`, `:773`). It records:
  - the latched value it still sees, `pre_latch_[k]`;
  - `ctx.frame`;
  - `ctx.t`.
- The input that `integrate_state(t, …)` uses for input k:
  - with no defect, the latched value `*u_[k]`;
  - `late_command`: `pre_latch_[k]`, at every frame;
  - `long_hold`: `pre_latch_[k]` at `test_defect_frame` only;
  - `stage_one_previous`: `pre_latch_[k]` when `t` equals the frame's `ctx.t`, which is RK4 stage 1
    (`integrator.hpp:110`). Stages 2 to 4 get t + h/2 or t + h. At a frame with no exchange,
    `pre_latch_` equals the latched value, so the defect acts only where u changes.
- Its derivative, written now because §5 charges this order:

  ```cpp
  for (std::size_t i = 0; i < n_; ++i) {
    double acc = 0.0;
    for (std::size_t j = 0; j < n_; ++j) {
      acc += a_[(i * n_) + j] * x_[j];
    }
    for (std::size_t k = 0; k < m_; ++k) {
      acc += b_[(i * m_) + k] * input(k, t);
    }
    dq[i] = acc;
  }
  ```

**The law** (`tests/plugins/state_feedback_peer`). It is a `Transport` in `nesc_control`'s shape (ADR-0029
Decisions 1 to 3): in-process, `transport.peer` keys, a zeroed reply that echoes the frame, no clock, no
allocation per frame.
- Keys:
  - `state` (the signal prefix; it resolves `<state>.0` to `<state>.(n−1)` at `init`, a `ConfigError` naming
    the key if one is missing);
  - `divisor` (its own N) and `dt`;
  - `k` (n gains), `g`, and `reference` (the initial r);
  - `schedule[].{frame, reference}`, with frames strictly ascending, checked with `is_sequence()`.
- At `publish(request, seq)`:
  - it advances the schedule while `entry.frame <= request.frame`, so r[k] is in force from the first exchange
    at or after its frame (Decision 3);
  - it accepts the request only if magic and version are ICD v1's, `frame` equals its expected kN, and
    `t == static_cast<double>(frame) * dt_`;
  - if accepted, `acc = g_ * r; for i: acc -= k_[i] * *state_[i];`, and the reply's elevator is `acc`;
  - otherwise the elevator is NaN, so the exchange halts `kTransport` naming `astra_sw_to_sim_v1.elevator`
    (ADR-0022, unconditionally);
  - the aileron, the rudder, both throttles, `mode_flags` and `fault_flags` are 0.
- It publishes `transport.peer.{k.<i>, g, dt, divisor, reference, refused}`.

**The configs** are built in the test from text. The nominal N = 10 config:

```yaml
sim: { dt: 0.001, frames: 2001 }
vehicle:
  models:
    - plugin: tests/plugins/linear_plant
      a: [[0.0, 100.0, 0.0, 0.0], [0.0, 0.0, 100.0, 0.0], [0.0, 0.0, 0.0, 100.0], [0.0, 0.0, 0.0, 0.0]]
      b: [[0.0], [0.0], [0.0], [100.0]]
      x0: [0.0, 0.0, 0.0, 1.0]
      inputs: [plant.u]
  initial_controls: { plant: { u: 0.0 } }
physical:
  earth: plugins/physical/earth/flat
  gravity: plugins/physical/gravity/constant
  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }
initial:
  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }
transport:
  plugin: tests/plugins/state_feedback_peer
  divisor: 10
  commands: { elevator: plant.u }
  limits: { elevator: [-1000.0, 1000.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], throttle: [0.0, 1.0] }
  peer:
    state: v0.linear_plant.state
    divisor: 10
    dt: 0.001
    k: [0.0625, 0.40625, 1.0572916666666667, 1.4010416666666667]
    g: 0.0625
    reference: 0.0
    schedule: [{ frame: 1007, reference: 1.0 }]
```

The other configs differ from it as follows:
- N = 3 and N = 1: both divisor keys and the gains, from §6;
- the controls: §9;
- the order control: a 5 × 5 chain, `b` = 100·e₅, `x0` = 0, and its own gains.

The `physical` section and its rigid body exist only because a transport requires them
(`simulation.cpp:465-468`). Nothing reads them.

## 3. The reference, and its independence (amendment 4)

- **What it is.** `tests/python/sampled_data_reference.py` computes in exact rationals (`fractions.Fraction`).
  - It imports `fractions`, `math` and `yaml`, and nothing from `astra`. It links nothing.
  - A test parses its imports (`ast`) and fails on any other import.
- **Its inputs.**
  - It reads A, B, K, G and h, plus x0, N, the schedule and the limits, from the config text with PyYAML's
    `BaseLoader`, under which every scalar is a string. It then applies `float()` or `int()` itself.
  - It reads nothing from the sim. `float()` rounds as `strtod` does, so these are the doubles the sim parses,
    and §7's parse-identity check confirms it.
- **What it computes.** Φ_j = Σ_{m<n} (jhA)^m/m! and Γ_j = Σ_{m<n} (jh)^{m+1} A^m B/(m+1)!, the closed-form
  zero-order hold. Both are finite sums, since Aⁿ = 0: to m = 3 for n = 4, and to m = 4 for the order control's
  n = 5. It never integrates.
- **What it knows of the code.** Only the bound's operation order (§5), read from the code with the lines cited,
  as ADR-0034 Decision 4 requires.
- **The harness** (`test_sampled_data.py`) drives the sim through `astra._core.Simulation`, one `step()` per
  frame. It copies the plant's slice of `vehicles[0].state` after each step, and decodes the recorder with
  `astra.lockstep.parse_recording`. It hands the reference only recorded data.

## 4. Shared inputs, and what each cannot see (ADR-0034 Decision 2)

| Shared input | What it cannot see | Mitigation |
|---|---|---|
| The config text (A, B, x0, K, G, h, N, the schedule, the limits) | A wrong value in the text itself, since both sides use it | The gains are pinned to §6's rationals (`float(text) == float(rational)`). The design's poles are checked exactly. §7's parse identity sees any difference between the two readers. |
| ICD v1's layout, as `astra.lockstep`'s dtypes transcribe it | A layout defect common to `icd/astra_icd.h` and the transcription | `test_lockstep.py`'s hand-counted offset tests |
| The platform's IEEE double arithmetic | F and L assume round-to-nearest doubles with no fused or extended operation. The reference's only floating-point steps are `float()` and c6 = `float(h) / 6.0`, one correctly rounded division as the core's. | No libm call is on the path. §5 restates the build flags before the first run. |

**Other blind spots, disclosed rather than claimed:**
- **Q4's gap.** The measurement leg reads the plant's stage-buffer pointers, not the ICD packer. The packer's
  frame-start sampling is M4's R12b (`tests/python/test_lockstep.py::test_r12b_icd_matches_state`), and every
  transport packs with the same `icd_pack::pack_sim_to_sw` call (`lockstep_exchange.cpp:222`).
- m > 1 inputs, n > 4 states under a nominal check, and the other three ICD command fields are not exercised.

## 5. The operation order the bounds charge (amendment 3)

**Core's RK4** (`core/include/astra/kernel/integrator.hpp`, `case IntegratorKind::kRk4`). Each loop is
elementwise, so the plant's slice goes through exactly these operations.

| Line | Operation | Rounding |
|---|---|---|
| 109 | `half_dt = dt_ / 2.0` | exact (dt is normal) |
| 110 | k1 = f(t, y) | the plant's, below |
| 111-113 | trial = y + half_dt·k1 | one product and one sum per element |
| 114 | k2 = f(t + half_dt, trial) | the plant's |
| 115-117 | trial = y + half_dt·k2 | one product and one sum |
| 118 | k3 = f(t + half_dt, trial) | the plant's |
| 119-121 | trial = y + dt_·k3 | one product and one sum |
| 122 | k4 = f(t + dt_, trial) | the plant's |
| 124 | y += (dt_/6.0)·(((k1 + 2.0·k2) + 2.0·k3) + k4) | c6 = fl(dt/6) once; 2.0·k exact; three sums; one product; one sum |

**Around it.**
- `vehicle.cpp:524` steps from `ctx.t`. `:458` copies each stage's trial y into the stage buffer before `:478-480`
  calls `integrate_state`, so the plant's pointer reads that stage's trial state.
- `:554`'s `post_step` renormalizes only the rigid body's quaternion. `:557` copies the accepted state into the
  stage buffer.
- The peer reads that copy at the next exchange: `simulation.cpp:760-765` runs before `:766`'s `step()` and
  `:773`'s latch. The request's t is `ctx.t`, which is `static_cast<double>(frame_) * dt_`
  (`virtual_clock.hpp:28`, `icd_pack.hpp:80`).

**The plant's derivative** (§2's loop).
- For A = 100·S and B = 100·eₙ, every product by a zero entry (±0) and every sum with a zero operand is exact.
- So entry i is exactly fl(100·x_{i+1}) for i < n, and fl(100·u) for i = n. Each is one rounding.

**The peer.** It computes acc = G·r, then subtracts each K_i·x_i in turn: n + 1 product terms, summed recursively.

**The build** (amendment 7). The M7b plan's reading of `cmake/AstraBuildFlags.cmake` at `8e525c5`: no `-march`,
`-ffp-contract` or fast-math; clang-18's default x86-64 target has no FMA.
- `Integrator::step` is a template in `integrator.hpp`, instantiated in `core/src/kernel/vehicle.cpp`'s
  translation unit, so the RK4 arithmetic's flags are core's. The plant's derivative and the peer's law are the
  two plugins'.
- **Before the first run,** for every preset the gate builds (`debug`, `release`, `static-plugins` and `asan`,
  plus `build/python`, which pytest runs on), PR 2 reads `compile_commands.json` for three translation units:
  `core/src/kernel/vehicle.cpp`, `tests/plugins/linear_plant/linear_plant.cpp` and
  `tests/plugins/state_feedback_peer/state_feedback_peer.cpp`.
- **It asserts** that none carries `-march`, `-ffp-contract=fast`, `-ffast-math` or `-Ofast`. A hit stops the
  first run and goes to Joe.
- **It states** all three translation units' flags, per preset, in the PR body.
- This is a fact about the build, which ADR-0034 Decision 3 allows before the first run.

## 6. The design numbers

**The gains** come from Ackermann's formula on (Φ_N, Γ_N), with h = 1/1000 and a = b = 100 exactly.
- The characteristic polynomial of Φ_N − Γ_N K is **exactly (z − 1/2)ⁿ** (checked in plan mode).
- The configs write each gain as `repr` of its nearest double, and G = K₁ as the same text.

| Run | K, in rationals | Config text, which is the doubles the run uses |
|---|---|---|
| n = 4, N = 10 | 1/16, 13/32, 203/192, 269/192 | 0.0625, 0.40625, 1.0572916666666667, 1.4010416666666667 |
| n = 4, N = 3 | 625/81, 1625/108, 5075/432, 1345/288 | 7.716049382716049, 15.046296296296296, 11.747685185185185, 4.670138888888889 |
| n = 4, N = 1 | 625, 1625/4, 5075/48, 1345/96 | 625.0, 406.25, 105.72916666666667, 14.010416666666666 |
| order control, n = 5, N = 10 (amendment 9) | 1/32, 1/4, 107/128, 145/96, 1531/960 | 0.03125, 0.25, 0.8359375, 1.5104166666666667, 1.5947916666666666 |

- **Which gains are exact.** The dyadic ones: K₁ and K₂ at N = 10 and N = 1, and K₁ to K₃ in the order
  control. The rest are rounded.
- **What the run's loop is.** With h = the double 0.001 and these doubles, the characteristic polynomial's
  coefficients differ from (z − 1/2)ⁿ's by at most 3.5e-16 (N = 10), 4.5e-16 (N = 3), 1.5e-16 (N = 1) and
  2.5e-16 (order). The reference uses the doubles, so it is exact for the loop the sim runs. "Poles at 1/2"
  describes the design.
- **The fixed point.** Since G = K₁ and A e₁ = 0, x = r·e₁ is the loop's exact fixed point.

**The runs** (every run: 2,001 frames, so states x(0) to x(2001)).

| Setting | Value |
|---|---|
| x0, nominal and the four controls | e₄ = (0, 0, 0, 1) |
| x0, the order control | 0 |
| r | 0, then 1.0 from frame 1007 (the schedule) |
| The first exchange at or after 1007 | frame 1010 at N = 10, 1008 at N = 3, 1007 at N = 1 |
| Exchanges | 201 (N = 10), 667 (N = 3), 2,001 (N = 1) |
| Limits | elevator ±1000, aileron and rudder ±1, throttle [0, 1] |

**The reference's magnitudes** (nominal, from the reference alone; inside the limits by a factor of 1.6 at N = 1):

| N | max \|u\| | where | max \|x\| |
|---|---|---|---|
| 10 | 1.40104 | frame 0 (−K₄) | 1 |
| 3 | 7.71605 | the step, frame 1008 (G) | 2.31481 |
| 1 | 625 | the step, frame 1007 (G) | 62.5 |

## 7. The bounds and checks, each with its kind

Notation:
- U = 2⁻⁵³ and η = 2⁻¹⁰⁷⁵ (half the smallest subnormal);
- γ_m = mU/(1 − mU);
- |·| on a vector or matrix is elementwise.

A check passes iff residual ≤ bound, **compared exactly as rationals**.

**F, per frame** (kind: roundoff, by derivation).
- **The check.** For every frame j = 0 to 2000 and every component i:
  |x_sim(j+1)_i − (Φ₁ x_sim(j) + Γ₁ u_hold(j))_i| ≤ F_i(j).
  - u_hold(j) is the recorded reply of the last recorded exchange whose request frame is at or before j
    (critique C-02).
  - F checks the integration, and that each reply acts from its own exchange frame.
- **The bound.** F_i(j) is a running bound over §5's program, evaluated in exact arithmetic on the recorded
  operands x = x_sim(j) and u = u_hold(j). Each node carries its exact value v and E, a bound on
  |computed − v|:
  - a product κ·a, with κ one of 100, h/2, h or c6: B = κ·E_a, then E = B + U·(|κ v_a| + B) + η;
  - a sum a + b: B = E_a + E_b, then E = B + U·(|v_a + v_b| + B);
  - 2·a: E = 2·E_a, which is exact;
  - the output: F_i(j) = E(x′_i) + |c6 − h/6|·|s_i| (amendment 6). Here x′_i's exact value is y_i + c6·s_i,
    the RK4 combination with the sim's own c6, and s_i is the exact (k1 + 2k2 + 2k3 + k4)_i. The last term is
    exact, not a bound: the reference's target uses h/6 and the sim uses c6, and both are known rationals before
    the run.
  - **The value:** h = the double 0.001 = 1152921504606847/2⁶⁰, and c6 = `float(h) / 6.0` =
    6148914691236517/2⁶⁵ = 0.00016666666666666666. So c6 − h/6 = −1/(3·2⁶⁵) = −1/110680464442257309696
    ≈ −9.035e-21, and |c6 − h/6| ≈ 5.42e-17·c6, below U·c6.
- **Why it is a bound.** fl(z) = z + e with |e| ≤ U|z|, plus η for a product. The computed operands lie within B
  of the exact ones, so |z| ≤ |v| + B. The sim's x̂′_i − (Φ₁x + Γ₁u)_i splits exactly into
  [x̂′_i − (y_i + c6·s_i)], at most E(x′_i), and (c6 − h/6)·s_i.
  - Every constant is positive, so this forward carriage equals carrying each rounding to the output by its exact
    linear coefficient (the M7b plan's wording; lesson 2026-10-13).
  - It has no fitted constant. Each rounding is charged at U times its own result.
- **Its premise.** RK4 with u held through all four stages gives T₄(hM), where M = [[A, B], [0, 0]]. That equals
  e^{hM} when M⁵ = 0, that is, when n ≤ 4.
  - The test asserts the nominal configs' A and B are the n = 4 chain.
  - It also asserts, in exact arithmetic, that the tableau gives Φ₁ and Γ₁ for n = 4, and misses Γ₁ by
    h⁵A⁴B/120 on x₁ for n = 5 (R1).

**L, per exchange** (kind: roundoff, by derivation).
- **The check.** |u_rec[k] − (G r[k] − Σ_i K_i x_sim(kN)_i)| ≤ γ_{n+1}·(Σ_i |K_i x_sim(kN)_i| + |G r[k]|) +
  (1 + γ_{n+1})(n + 1)η.
  - This is the recursive inner-product bound of n + 1 terms, plus underflow.
  - x_sim(kN) is the state at the start of the exchange's frame.
  - r[k] is in force from the first exchange at or after 1007.
  - It is evaluated at every recorded exchange, at the frame the request carries.

**C, the closed loop, per reference exchange k with kN ≤ 2001** (kind: roundoff, by derivation).
- **The check.** |x_sim(kN) − x̃[k]| ≤ C(k) = S̄·(w̄_{k−1} + 2⁻⁴⁰¹·𝟙), per component.
- **Its terms:**
  - x̃[0] = x0, and x̃[k+1] = round to the nearest multiple of 2⁻⁴⁰⁰ of (Φ_cl x̃[k] + Γ_N G r[k]). This is the
    exact recursion carried on a grid (§11, item 3), with Φ_cl = Φ_N − Γ_N K;
  - w_j = |Γ_N|·L(j) + Σ_{i=0}^{N−1} |Φ₁^{N−1−i}|·F(jN + i), where L(j) is the bound at the run's j-th
    recorded exchange;
  - w̄_{k−1} is the componentwise maximum of w_j over j ≤ k − 1, and w̄_{−1} = 0;
  - S̄ = (Σ_{m=0}^{I−1} |Φ_cl^m|)·(I − |Φ_cl^I|)⁻¹, computed exactly before the run. I is the smallest m with
    ‖Φ_cl^m‖_∞ ≤ 1/2. S̄'s entries may be rounded upward to keep the test fast, and never downward.
- **The derivation.** The error obeys e_{k+1} = Φ_cl e_k + Γ_N l_k + d_k, with e₀ = 0, |l_k| ≤ L(k) and
  |d_k| ≤ Σ_i |Φ₁^{N−1−i}| F(kN + i).
  - So |e_k| ≤ Σ_{j<k} |Φ_cl^{k−1−j}| w_j ≤ S̄ w̄_{k−1}.
  - Σ_m |Φ_cl^m| ≤ S̄, because |Φ_cl^{qI+s}| ≤ |Φ_cl^s| |Φ_cl^I|^q.
  - The grid adds at most S̄·2⁻⁴⁰¹ per component.
  - This is the M7b plan's Σ_j ‖(Φ_N − Γ_N K)^j‖ in elementwise form, with a running maximum of the
    per-exchange injections (§11, item 4).

**The request, per exchange** (kind: bitwise).
- The k-th request's magic, its version (1), `frame` = kN, and `t` = fl(float(kN)·h), each compared with `==`.
- The k-th reply's echoed frame equals the request's.

**Parse identity** (kind: bitwise).
- Against `float(text)`: `sim.clock.dt`, the plant's published a and b, the peer's published k, g and dt, and
  x_sim(0) = x0.
- Against `int(text)`: the peer's divisor.

**Completion.**
- No halt over 2,001 frames, so every reply stayed finite and inside its limits.
- `transport.peer.refused` is 0.

**The scale of each bound,** evaluated on the reference trajectory as a pre-run estimate. The run's own bounds
are evaluated on its recorded data.

| N | max F bound | max L bound | I | S̄'s largest row sum | max C bound |
|---|---|---|---|---|---|
| 10 | 1.78e-16 | 7.78e-16 | 12 | 48.6 | 6.94e-14 |
| 3 | 7.14e-16 | 1.46e-14 | 10 | 39.8 | 3.34e-14 |
| 1 | 4.39e-14 | 1.18e-12 | 18 | 314.3 | 5.78e-13 |

## 8. The comparison set (oracle data, ADR-0034 Decision 7)

- **The runs:** nominal N = 10, 3 and 1; the four controls; the order control. Each runs 2,001 frames.
- **F:** every frame from 0 to 2000, every component. For the order control, x₁ is the registered component.
- **L and the request:** every recorded exchange.
- **C:** every k with kN ≤ 2001, every component.
- **Also:** parse identity and completion.
- **Determinism** (an M7b invariant): two runs of nominal N = 10 give byte-identical recorder bytes and
  plant-slice trajectories.

Nothing in this set may be dropped without Joe.

## 9. The negative controls

**How each is planted.** Every control runs at N = 10, with the N = 10 gains, x0 = e₄ and 2,001 frames, unless
its row says otherwise. Each emulates, at the plant's input, the observable effect of the core defect it names.

| Control | Planted as | The core defect it emulates |
|---|---|---|
| Late command | `test_defect: late_command` | a command that takes effect one frame late (also the post-step reading, Q5) |
| Long hold | `test_defect: long_hold`, `test_defect_frame: 1010` | one hold that lasts N + 1 frames, at the step's exchange |
| Wrong divisor | `transport.divisor: 11` and `peer.divisor: 11`; the gains and the reference stay N = 10's (C-02) | an exchange at the wrong divisor |
| Stage switch | `test_defect: stage_one_previous` | a command that switches after RK4 stage 1 |
| Order | the 5 × 5 chain, its own gains (§6), x0 = 0 | a plant beyond RK4's exact order |

**What each failure looks like, derived.**
- **Late command:** F's residual at frame kN is Γ₁·(u[k−1] − u[k]), where Γ₁ = (1/240000, 1/6000, 1/200, 1/10)
  for h·a = h·b = 0.1.
- **Long hold:** the same residual, at frame 1010 only.
- **Stage switch:** with Δ = u[k−1] − u[k], the residual is (h/6)BΔ + (h²/6)ABΔ + (h³/12)A²BΔ + (h⁴/24)A³BΔ.
  That is Δ/60 on x₄, Δ/600 on x₃, Δ/12000 on x₂ and Δ/240000 on x₁.
- **Order:** h⁵A⁴Bu/120 = 8.333e-8·u, on x₁ only.

**Finite and inside the limits (N-02).** Each control's exact reference keeps the run finite and inside the
limits over its divisor and horizon:

| Control | Divisor, horizon | max \|u\| | max \|x\| |
|---|---|---|---|
| Late command | 10, 2,001 frames | 1.40104 | 1.10314 |
| Long hold | 10, 2,001 frames | 1.40104 | 1 |
| Wrong divisor | 11, 2,001 frames (182 exchanges) | 1.40104 | 1 |
| Stage switch | 10, 2,001 frames | 1.40104 | 1 |
| Order | 10, 2,001 frames | 0.046875 | 1 |

**The registrations** (ADR-0034 Decision 5). A margin is residual over bound. "Predicted" is from the exact
reference, before any run.
- A registered first failure must show a margin of at least 10¹⁰.
- Every object in a registered window must show a margin of at least 10³. The order control's frames must show
  at least 10⁸.
- A registered pass is a pass.
- A control that misses any of these stops the PR and goes to Joe. That includes a failure earlier than its
  registered first failure on the same object.

**Late command**
- F fails first at frame 0 (predicted 10^14.9). It fails at every exchange frame kN for k in [0, 40] and
  [101, 135] (predicted minimum 10^12.95).
- C fails first at k = 1 (predicted 10^13.1). It fails at every k in [1, 50] and [102, 150] (predicted
  minimum 10^5.17).
- Passes:
  - F, at every frame with no exchange;
  - L, at every exchange;
  - the request, at every exchange.

**Long hold**
- F fails at frame 1010 (predicted 10^15.2), and only there.
- C fails first at k = 102 (predicted 10^11.8). It fails at every k in [102, 135] (predicted minimum 10^5.88).
- Passes:
  - F, at every frame other than 1010;
  - C, at every k up to 101;
  - L and the request, at every exchange.

**Wrong divisor**
- The request fails first at exchange 1 (frame 11 against 10), and at every exchange from 1 on.
- C fails first at k = 2 (predicted 10^12.8). It fails at every k in [2, 45] and [102, 145] (predicted minimum
  10^5.28).
- Passes:
  - F, at every frame;
  - L, at every actual exchange;
  - the request, at exchange 0;
  - C, at k ≤ 1.

**Stage switch**
- F fails first at frame 0 (predicted 10^14.1). It fails at every exchange frame kN for k in [0, 30] and
  [101, 125] (predicted minimum 10^12.48).
- C fails first at k = 1 (predicted 10^12.4). It fails at every k in [1, 45] and [102, 145] (predicted
  minimum 10^5.38).
- Passes:
  - F, at every frame with no exchange;
  - L and the request, at every exchange.

**Order** (amendment 9, critique C-03)
- F on x₁ fails first at frame 1010 (predicted about 10^315: the omitted 2.604e-9 over a bound near η, since
  x = 0 there).
- It fails at every frame from 1010 to 1029. These are the pre-registered excited frames, the holds of the two
  exchanges just after the step, where |u| is largest:
  - exchange 101 (frames 1010 to 1019): u = 1/32, omitted term 2.604e-9, predicted minimum margin 10^10.4;
  - exchange 102 (frames 1020 to 1029): u = −3/64, omitted term 3.906e-9, predicted minimum 10^9.3.
- Passes: F on x₁ at frames 0 to 1009, where x, u and the residual are exactly 0.
- Nothing else is registered ("none required elsewhere").
- Its reference is the exact one with the sums run to m = 4. Its F bound keeps the nominal form.

**Why the windows end where they do.** Each window lies inside the set where the predicted residual is at least
1e-9 in absolute terms and the predicted margin at least 10³.
- That absolute floor is four or more decades above any C bound, so the sim's roundoff cannot move a registered
  object.
- Far into the decay, a ratio stays large, because each component's bound shrinks with its own magnitude. There,
  though, the sim's tiny values can differ from the exact ones relatively. Those frames are therefore not
  registered either way.

## 10. What was measured before the first run (ADR-0034 Decision 3)

Every computation ran read-only in plan mode, through a scratch `python3 -I` on stdout, with no files written:
- the reference alone: the gains, the design's and the doubles' characteristic polynomials, the trajectories,
  §7's bound scales, and §9's predicted margins and windows;
- a plain-Python IEEE-double emulation of §5's operation order. That emulation is not ASTRA, so no output of the
  path under test was measured. Joe accepted it as outside Decision 3's "first run" (amendment 4). It was run to
  catch a wrong derivation before review, and it is **never cited as evidence that the sim passed**:
  - F's largest residual over its bound: **0.9999999999999993** at N = 10 (frames 1044 to 1049, on x₄ = −2⁻⁵),
    0.924 at N = 3 and 0.999 at N = 1. Rerun with amendment 6's exact |c6 − h/6| term, the maxima are unchanged
    (0.9999999999999993, 0.9237993080182222 and 0.9990009989564618);
  - L's: 0.122, 0.152 and 0.178;
  - C's: 0.0212, 0.0364 and 0.0506;
  - under the emulated controls, every predicted first failure and window held, and L stayed at or below 0.202.

**F is tight by construction, and that is disclosed.** At a rounding tie just above a power of two, the final
addition's error is U·2^e, while the bound charges U·|z| with |z| barely above 2^e. Hence 0.9999999999999993.
- The bound is still a bound, and no slack factor is added, because that would be a fitted constant.
- Exact rational comparison is therefore required.

## 11. Readings of the brief, and departures from the M7b plan's text

1. **"Controls are planted through the test_defect key only": Joe's reading of brief item 3** (amendment 1).
   - The three plant-side controls are the plant's `test_defect` key.
   - The wrong divisor and the order control are configurations the test builds, as the M7b plan's own rows
     define them: both divisor keys at N + 1, and a five-state chain.
   - Nothing is planted in core, the ICD or a shipped path (ADR-0034 Decision 5). The peer has no defect key.
2. **The C++ smoke's source sits beside the plant.**
   - `linear_plant_smoke_test.cpp` is declared in `tests/plugins/linear_plant/CMakeLists.txt` as an executable
     linking `tests/support/alloc_counter.cpp`, registered with `astra_add_plugin_test`, `gtest_discover_tests`,
     label `physics`.
   - This keeps it inside the named paths. The repository's usual home, a `TEST` in
     `tests/physics/no_alloc_test.cpp` with a config under `tests/configs/`, is outside them.
3. **Dated departure, 2026-10-18 (Joe, amendment 2): C's reference runs on a 2⁻⁴⁰⁰ grid, and the grid's effect
   is added to C's bound** as §7 states it. This departs from the M7b plan's "exact closed-loop recursion"
   wording.
   - Unrounded, N = 1's denominators grow by about 300 bits per exchange, to about 600,000 bits by exchange 2001.
   - In plan mode, the unrounded per-frame recursion did not finish in 120 s.
   - F and L stay unrounded, because their references start from recorded doubles every frame.
4. **C's bound is in elementwise form, with a running maximum** (§7), in place of the M7b plan's norm-sum
   wording. Joe accepted it (amendment 3). It is still a derivation fixed before the run.
5. **Dated correction, 2026-10-18, recorded only here (amendment 11).** The M7b plan's assumption line says check
   F fails if the stage buffer does not hold the accepted state at an exchange.
   - On reading the path, it is L, and then C, that fails: F uses the recorded reply, while L recomputes it from
     the recorded state.
   - F does see the per-stage copy (`vehicle.cpp:458`).
   - The M7b plan's text is not edited.
6. **Gate cost, accepted provisionally by Joe (amendment 5).**
   - pytest is estimated at about 35 s, against the M7b table's "about 3 s". Each 2,001-frame run's exact F
     bound took about 2.3 s in plan-mode timing, and C's grid recursion up to about 4.6 s at N = 1.
   - ctest is under 1 s per preset.
   - Both are measured and reported against §1's base and 1001 s (Q3), and the pytest number comes to Joe at
     human review.
   - No formula, bound, window or comparison-set change is made for speed.
7. **x0 and r are this plan's choices.** The M7b plan leaves them to config.
   - x0 = e₄ for the nominal runs and the four controls; N-02's probe used e₄.
   - x0 = 0 for the order control, so that its first failure is its first excited frame.
   - r = 1.0.

## 12. Acceptance

- **ctest:** `ctest --preset asan -R LinearPlant -N` first. Six tests are expected:
  - nominal at N = 10, and at N = 1;
  - the three plant defects, `long_hold` at frame 100 within 200 frames;
  - a peer refusal on a mismatched divisor, which halts `kTransport` and allocates nothing on the halt.

  Then `ctest --preset asan -R LinearPlant --no-tests=error` (R4). The full gate runs the tests in all four
  presets: 200 frames each, zero allocations, under ASan and UBSan in the sanitizer preset.
- **pytest:** `.venv/bin/pytest tests/python -k sampled_data --collect-only -q` for the count, then
  `.venv/bin/pytest tests/python -k sampled_data` (R1 to R3, the guards, determinism). The guards:
  - a peer divisor of 10 under a transport divisor of 11 halts `kTransport` at frame 11;
  - a peer `dt` of 0.002 halts at frame 10;
  - an unknown `test_defect`, a missing state signal, and a scalar or quoted `schedule` each raise
    `ConfigError` naming the key.
- **The full gate:** `./scripts/check-full.sh`, exit 0 and `check: OK`.
  - The trimmed summary goes in `docs/reviews/evidence/<head>-gate-summary.log`.
  - The PR body gives the gate's wall time, its delta against §1's base and against 1001 s, and the added time
    per preset.
- **Pre-registered constants in the test** (ADR-0034 Decision 6, amendment 8). Each constant in
  `test_sampled_data.py` carries a comment citing its section of this plan:
  - §6: the gains, G, x0, r and the step frame;
  - §9: each control's divisor, horizon, windows, thresholds and margins;
  - §7: the 2⁻⁴⁰⁰ grid, I's rule, S̄'s upward rounding rule, U, η and |c6 − h/6|.
- **Review:** an adversarial Codex review of this plan before the first run, and of the diff after it, with at
  most two correction rounds.
- **Before the merge:** `git fetch`, then merge `origin/main` in if it moved.

## 13. Docs in this PR

- `PLAN.md`: the M7b block's PR 2 line.
- `docs/process/validation.md:139`: the numerical section's binding-ADR list gains "ADR-0034 in-house oracles"
  after "ADR-0013 oracle construction" (PR 1's follow-up).
- At `/wrap`:
  - `PLAN.md`'s `disarm-goldens.sh` item gains PR 1's fact (#122's body): a marker in the worktree did not arm
    the guard, while the main checkout's marker did;
  - `STATE.md`.
- This plan, the review record, and the evidence.
- Not touched: `docs/architecture.md` (§16's row stays planned until PR 5), the ADRs, and the M7b plan.

## 14. Sequence after approval

1. `git fetch origin`. Create the worktree `m7b-pr2-sampled-data` from `origin/main` (`EnterWorktree`), and
   confirm `406c407` or later and ADR-0034's Status line.
2. §1's base gate, with its load check. Record it in §1 before anything else.
3. Move this draft into the worktree as `docs/plans/m7b-pr2-sampled-data.md` (`mv`, before its first add). Commit
   it with §1 filled in, push, and open the draft PR (Tier 3).
4. The Codex adversarial plan critique: fresh and read-only, on the M7b critique's route (`task --fresh --effort
   high`, no `--write`, no `--model`, in the background). It is given ADR-0034, the M7b plan's oracle (a)
   section, and this plan. Its focus (amendment 9):
   - §5's operation order, against `integrator.hpp` and the plant's derivative as written;
   - §7's F derivation, including §10's tie case and amendment 6's exact term;
   - the C derivation's S̄ argument;
   - §9's windows and thresholds, against the exact reference;
   - whether any control can pass because a plausible core defect is absent from the plant's emulation of it;
   - Q4's measurement leg.

   Verify each finding against its cited lines, and fold in each supported one. A finding that would change a
   pre-registered number, a bound, a window, the comparison set or a decided item comes to Joe before the first
   run.
5. Build: the plugins, the reference, the tests and the CMake.
   - Until step 4 closes, the build compiles only. Nothing executes the plant or the peer: no smoke and no
     pytest that steps them.
   - Tests of the reference alone may run, because they measure only the reference: the gains, the design poles,
     the tableau identity and the import scan.
   - §5's flag check (amendment 7) runs on every preset's `compile_commands.json` before step 6.
6. The first run: the focused ctest and pytest commands. A nominal miss, or a control that does not fail as
   registered, stops the PR and goes to Joe.
7. §13's docs, the full gate, the evidence, and the PR body.
8. `/review-cycle`: Codex reviews the diff against `--base origin/main`, with at most two correction rounds.
9. `git fetch`, and merge `origin/main` in if it moved. A code change from that merge reruns the gate.
10. `/human-review`, then `/wrap`, then stop. The merge is Joe's.

## 15. Risks and rollback

- **F's bound is exactly tight at ties** (§10). If the sim's arithmetic departed from §5's order, for example
  through contraction or reassociation, F could miss. That would stop the PR and go to Joe. It is never a reason
  to add slack.
- **A control's window edge.** The registered windows sit well inside the predicted sets, by margins of 10² or
  more over the registered thresholds, so a stop there would indicate a real departure.
- **Gate cost** (§11, item 6).
- **Rollback:** revert the PR. Every path is test-only.
