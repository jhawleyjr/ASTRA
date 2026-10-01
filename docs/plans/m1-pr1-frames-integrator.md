# M1 PR 1 — frames, Integrator, tokens.py (plus: plan review becomes automatic)

Recovered from `~/.claude/plans/two-things-in-this-reactive-lake.md` on 2026-09-30. Approval: the file records none; the work merged as #15. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

Two things ship in one PR.

**1. Process.** Plan review for unpinned designs is currently written as a manual step Joe performs
(`CLAUDE.md` review bullet, `docs/process/workflow.md` §Review "Plan review, from M1", the `STATE.md`
"Plan review:" sentence). It becomes automatic and mine: for a plan touching a design the ADRs do not
pin (EOM, kinematics, integrator interface, health monitor), I run one `/codex:rescue` round *inside
plan mode, before the plan is shown to Joe*, fold in the findings that hold, note the rejected ones and
why, then exit plan mode. Plans touching only pinned designs skip it. No critique subagents, and not on
every plan — a critique on every plan is the cost failure mode in the other direction.

**2. The task.** M1 PR 1 as written in `STATE.md` "Next step" and `PLAN.md` M1 PR 1: `core/util/`
frames (quaternion ↔ DCM ↔ 3-2-1 Euler, rotation, `q_nb` kinematics, ADR-0001), `core/kernel/`
`Integrator` over `StateVector` (Euler + RK4, post-step renormalize, no allocation in `step`,
ADR-0003/0004), and `scripts/tokens.py`. `tests/golden/frames/adr0001_cases.hpp` (PR #12, human-owned)
is the oracle: the frames test is judged against it and neither it nor the clang-tidy header filter moves.

Acceptance: `just check` exits 0 and `ctest --preset asan -R 'frames|integrator'` passes with
(a) every ADR-0001 case within `kTolerance` (1e-12), (b) RK4 error ratio 16 ± 0.5 and Euler 2 ± 0.1 when
dt halves on a linear ODE with a closed form, (c) |q| within 1e-12 after post-step.

## Part 1 — the three process files

`AGENTS.md` (80) + `CLAUDE.md` (20) = exactly the 100-line cap in workflow.md §"Files that carry state",
so the CLAUDE.md bullet must stay five lines; the long form of the rule lives in workflow.md.

- **`CLAUDE.md`**, replace the review bullet with (5 lines, same width as its neighbors):
  `/codex:review` and `/codex:adversarial-review` unchanged; "never the built-in `/code-review`; never
  the Codex stop-time gate; never a critique subagent. A plan deciding a design the ADRs do not pin (EOM,
  kinematics, integrator interface, health monitor) gets one `/codex:rescue` round in plan mode before
  Joe sees it, plan and acceptance command inlined (workflow.md §Review); other plans skip it."
- **`docs/process/workflow.md` §Review**, rewrite the "Plan review, from M1" paragraph: same trigger
  (unpinned + expensive to unwind; in M1 the EOM and quaternion kinematics, the integrator interface, the
  health monitor — not mass properties, flat Earth, or test scaffolding), but done by Claude inside plan
  mode before Joe sees the plan; `/codex:rescue` with **the plan text and the acceptance command inlined**
  (Codex cannot read the plan file) and "state what must be true for this plan to satisfy the acceptance
  command and ADR-XXXX…; check each; report only what fails"; one round; findings that hold are folded in,
  rejected ones named in the plan with the reason, so Joe reads the plan and the critique together;
  pinned-only plans skip it; still never a critique subagent. Rest of §Review unchanged.
- **`STATE.md`**, the "Plan review:" sentence in Next step → the integrator-interface plan gets its
  `/codex:rescue` round inside plan mode, automatically, before the plan is shown. (≤ 40 lines holds;
  `/wrap` rewrites the file at the end of the session anyway.)
- `PLAN.md`'s "Plan review: … → Codex before code" lines in PR 3 and PR 4 stay: they say when, not who,
  and are still true. Not touched.

## Part 2 — the code

### `core/include/astra/util/frames.hpp` (header-only, `namespace astra::frames`)

Every formula is typed from the closed forms in the oracle header (Stevens & Lewis 1.3-11, 1.8-15/16/18),
not from a library, so the test judges the code against the convention. Eigen fixed-size types only, so
no allocation; `[[nodiscard]]`, `noexcept`. Euler triples are ordered **(ψ, θ, φ)**, as the oracle's
`euler_nb` is; `q_nb` is `Eigen::Vector4d` scalar-first `(w, x, y, z)`.

- `quaternion_from_euler`, `dcm_from_euler`, `dcm_from_quaternion`, `euler_from_dcm` (ψ = atan2(C12,C11),
  θ = −asin(clamped C13), φ = atan2(C23,C33)), `euler_from_quaternion` (via the DCM, one path).
- `quaternion_product` (Hamilton, scalar-first), `quaternion_derivative(q_nb, ω_b)` = ½ q_nb ⊗ (0, ω_b),
  `normalize_quaternion`.
- `rotate_n_to_b(q_nb, v_n)` / `rotate_b_to_n` by the quaternion sandwich, *not* by building the DCM, so
  the golden vectors pin a second independent path; a test asserts the two agree.
- `to_eigen_quaternion` / `from_eigen_quaternion` — ADR-0001's "the Eigen conversion happens in one place
  in `core/util/`". A test pins the trap: `to_eigen_quaternion(q_nb).toRotationMatrix()` is C_nbᵀ.
- Deliberately **not** included: `quaternion_from_dcm` (Shepperd). No consumer in M1 and the oracle pins
  no dcm→q data, so its only test would be a round trip against our own code.

### `core/include/astra/kernel/integrator.hpp` (header-only)

- `inline constexpr double kDefaultDt = 1e-3;` — ADR-0004's default major frame, in seconds, named once
  so PR 2's `Config` defaults to it rather than re-deciding it.
- `enum class IntegratorKind : std::uint8_t { kEuler, kRk4 };`
- `concept StateDerivative` for `f(t, y) → ẏ`: the call must return exactly `StateVector` (not an Eigen
  expression, which would dangle) **and be `noexcept`**, which is how §14's "the per-frame path never
  throws" gets checked by the compiler instead of asserted in prose. Test functors are `noexcept` lambdas.
- `explicit Integrator(IntegratorKind kind = IntegratorKind::kRk4, double dt = kDefaultDt)`, throwing
  `astra::Error` on dt ≤ 0 (init path may throw); `kind()`, `dt()` accessors.
- `template <StateDerivative F> void step(const F& f, double t, StateVector& y) const noexcept` advances
  exactly one major frame — Euler `y += dt*f(t,y)`, classical RK4 with stages at t, t+dt/2, t+dt/2, t+dt —
  with stages in `StateVector` locals (13 doubles, stack), so nothing allocates. `t` comes from the caller
  as `frame * dt` (ADR-0010); it is never accumulated inside the integrator.
- **`step` does not renormalize.** The post-step is a separate free function
  `void post_step_renormalize(StateVector& y) noexcept` (over `frames::normalize_quaternion` on
  `y.segment<4>(state::kQuaternion)`), because architecture §5's frame order and ADR-0004 make the
  renormalize a post-step stage, and PR 4's health check must see `|q|` drift **before** it is erased.
- Header-only because the per-frame path must inline and because it adds no sources to `astra_core`.

### Tests

`ctest --preset asan -R 'frames|integrator'` must match, and `gtest_discover_tests` names tests
`Suite.Test`, so the two files get their own executables with a lowercase `TEST_PREFIX` rather than a
lowercase GoogleTest suite name (which would put the naming under clang-tidy's macro-expansion behavior):

- `tests/unit/util/frames_test.cpp` → `astra_frames_test`, `TEST_PREFIX frames.`, with
  `target_include_directories(... "${ASTRA_SOURCE_DIR}/tests")` so it includes
  `"golden/frames/adr0001_cases.hpp"`. Over all eight cases: quaternion from Euler, DCM from Euler, DCM
  from quaternion, Euler round trip (θ only where `euler_roundtrip` is false), `n_axes_in_b` /
  `b_axes_in_n` by rotation, `qdot_nb`; all `|a − b| <= kTolerance`.
- `tests/unit/kernel/integrator_test.cpp` → `astra_integrator_test`, `TEST_PREFIX integrator.`:
  - (b) the linear ODE ṗ = v, v̇ = −k p in the first `p_n`/`v_b` slot pair, k = 1 s⁻², p(0) = 1, v(0) = 0,
    every other derivative component zero, q = identity and ω_b = 0; closed form p = cos t, v = −sin t.
    Global error at T = 1 s with dt = 0.05 s and dt = 0.025 s → ratio 16 ± 0.5 (RK4), 2 ± 0.1 (Euler).
    RK4's error there is ~1e-7, eight orders above double roundoff, and dt is small enough that Euler's
    ratio is within a few percent of 2. If the measured ratios sit near a band edge, the fix is a smaller
    dt pair, never a widened band — the bands are the acceptance criterion.
  - (c) constant-rate rotation driven by `frames::quaternion_derivative`, 1000 steps at dt = 1 ms:
    `| |q| − 1 | <= 1e-12` after `post_step_renormalize` on every step, and the attitude within a loose
    RK4-order tolerance of the closed-form axis-angle solution.
  - that `step` alone leaves an off-norm quaternion off-norm (|q| = 1.1 in, still 1.1 out) and
    `post_step_renormalize` is what fixes it — the property PR 4's "drift > 1e-6 before renormalization"
    health check depends on.
- The existing `astra_unit_tests` target is untouched.

### `scripts/tokens.py` (+ `tests/python/test_tokens_script.py`)

The per-session token script workflow.md §"Model and effort" measures with and `/retro` step 4 runs:
stdlib only, reads `~/.claude/projects/*ASTRA*/**/*.jsonl` (paths overridable on the command line, with a
`--since` filter), aggregates per session from each assistant message's `message.usage` — output tokens,
cache reads, cache creation, input tokens — plus the model(s) seen, and prints a table with a total row.
Ruff-clean at line-length 100 (pyright does not cover `scripts/`). The pytest writes a synthetic
transcript into `tmp_path` and checks the totals, so the gate verifies the script rather than trusting it.

## Verification

1. `just check > $SCRATCH/check.log 2>&1; echo EXIT=$?` — read the `EXIT=` / `check: OK` line, tail only.
2. `ctest --preset asan -R 'frames|integrator' > $SCRATCH/accept.log 2>&1` — every ADR-0001 case, the two
   order ratios, the norm bound; tail only.
3. `wc -l AGENTS.md CLAUDE.md STATE.md` — 100-line and 40-line caps hold.
4. Review: `/codex:review --base origin/main --wait`, then `/codex:adversarial-review --base origin/main`
   with ADR-0001/0003/0004 as focus; then push the branch, open the PR, `/wrap`.

If clang-tidy flags the human-owned oracle header once it is included by a test, that is a stop-and-ask
for Joe: the filter is not loosened and the header is not edited.

## Codex plan review (the new rule, applied to the integrator interface)

One `/codex:rescue` round on the integrator-interface part, with the draft and the acceptance command
inlined. Four findings; all four changed the plan above:

1. **In-step renormalization erases the drift PR 4's health check must see.** Held, and architecture §5
   already puts the renormalize in the post-step stage — the draft had it inside `step`. Fixed: `step`
   only advances; `post_step_renormalize(StateVector&)` is separate, and a test pins that `step` leaves an
   off-norm quaternion off-norm.
2. **Name concrete constants for the order test.** Held in substance (adopted: k = 1, p(0) = 1, v(0) = 0,
   T = 1 s, dt = 0.05 → 0.025, all other components zero). Rejected in one detail: Codex read the draft's
   `y'' = −k y` as `y = −k y` and called it "neither an ODE nor a harmonic oscillator"; it is the harmonic
   oscillator, now written as the first-order pair ṗ = v, v̇ = −k p so there is nothing to misread.
3. **`noexcept` on a `step` that calls an arbitrary functor makes an unverified guarantee.** Held. The
   `StateDerivative` concept now requires the derivative call itself to be `noexcept`, so §14's "the
   per-frame path never throws" is a compile error instead of a terminate at runtime.
4. **No default `dt = 1 ms` anywhere.** Held: ADR-0004 names 1 ms as the default and the draft made every
   caller supply it. Added `kDefaultDt = 1e-3` plus defaulted constructor arguments (RK4 default kind).

Nothing was raised about the frames half, the `t`-as-`double` interface (ADR-0010 keeps `frame` the source
of truth at the scheduler, which is where the counter lives), or the templated-functor shape.
