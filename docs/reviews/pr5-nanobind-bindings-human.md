# Human review packet: M3 PR 5 — nanobind bindings (Simulation, Config, SignalRegistry, write seam, derivative)

PR / candidate base and head: base `origin/main` at `96c9459` (confirmed current tip, `git fetch`
this session — `main` has not advanced past this base). Head `7a0db9e`
(`worktree-m3-pr5-nanobind`), three commits over the plan: implementation `7755432`, retained
evidence `194fb4e`, Codex review record `7a0db9e`. No PR opened yet — this is a local-only packet;
opening the PR is a separate, explicitly authorized step.
Spec/plan/task: `docs/plans/pr5-nanobind-bindings.md` (approved by Joe, 2026-09-21, after one Codex
plan critique and Joe's own second review round — see that file's own "Plan critique and
decisions" section for the full record).
Risk: **Tier 3** — `core/kernel/` (`Simulation`, `Vehicle`) and health behavior, per
`docs/process/workflow.md`'s risk-tier table; PLAN.md's own PR 5 line assigns Tier 3.
Decision requested: **approve behavior and merge** (PLAN.md's PR 5 checkbox, contingent on Joe's
own decision here — not yet checked off).

## What changes and why

`astra._core` (the nanobind module) gains its first bindings for `Simulation`, `Config` (including
a from-dict path), `SignalRegistry`/`HealthMonitor`/`VirtualClock`/`Vehicle` read views, the PR 2
write seam (`sim.write_input`), and `Simulation.derivative()` — the primitive PR 6 (`astra.trim`)
and PR 7 (`astra.linearize`) are built on. Before this PR, `astra._core` bound only the plugin
loader/descriptor types, `StepContext`, `Model`, and the fixed-13 rigid-body-block state helpers;
nothing could build or drive a real simulation from Python at all.

**Core changes** (`core/`, no plugin-ABI/`kCoreApiVersion` change — verified against `git diff`,
not assumed):
- `Vehicle::integrate()`'s own per-stage derivative lambda (environment latch, fresh-atmosphere
  sampling, wrench accumulation, rigid-body and state-owner derivatives) is factored, unchanged,
  into a new private `Vehicle::evaluate_derivative_stage()` — the exact same arithmetic both the
  real per-frame path and the new probe path call, not a second implementation.
- New `Vehicle::derivative()`: evaluates that same arithmetic at an arbitrary caller-supplied
  state, without advancing the frame. Raises `Error` when no physics is configured, when a
  configured `WindModel` has never been latched by a real frame (new `environment_latched_` flag,
  set unconditionally by `set_environment()`, cleared by nothing), or when the probe's own
  domain/environment checks fault. Restores `stage_` to match the accepted `combined_` state on
  every exit path, including the raise path. Lazily copy-constructs a policy-matching,
  counter-isolated `probe_health_` (`HealthMonitor` holds no `SignalRegistry` reference — a plain
  copy carries its action policy with zero registry involvement) so a probe never leaks into the
  real `HealthMonitor`'s counters/signals or `published_wrench_`.
- New `Vehicle::combined_state()` — renamed/promoted from the former test-only
  `combined_state_for_testing()` (one accessor, not a duplicate; its one existing test call site
  updated) — and `Vehicle::state_owner_slices()`, the authoritative named-slice metadata ADR-0021
  R4 calls for (no registry-name parsing).
- New `Vehicle::k1_for_testing()`/`Integrator::k1_for_testing()` (test-only, precedented by the
  existing `stage_state_for_testing()`).
- New `Simulation::write_input()` (`write()` then immediate `latch()`, raising on a non-finite
  value — ADR-0017's own explicit requirement) and `Simulation::derivative()`.

**Python changes** (`python/bindings/_core.cpp`, `python/CMakeLists.txt`): the classes above bound
via nanobind, plus `Config` (`from_file`/`from_string`/`from_dict` — a Python dict converted to a
`YAML::Node`, emitted to text, and handed to the existing `Config::from_string`; no `core/` change
for this at all), `HaltReason` (bound as a self-contained value with an eagerly-copied `source`,
never a live `string_view`), and every borrow into `Simulation`-owned storage using
`nb::rv_policy::reference_internal` at every hop. `Simulation::for_testing` and model-base
trampolines are deliberately not bound (D2, PLAN.md's own settled decision).

**Diff shape**: 20 files, +2148/-90 (excluding retained evidence log). Six new files: five Python
test modules (`tests/python/test_{config,derivative,signal_registry,simulation,write_input}.py`)
and one C++ test (`tests/unit/kernel/simulation_derivative_test.cpp`). No unexpected scope — every
touched path maps directly to a row in the plan's own "Mapping and sequence" table.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| Full gate (`just check`): format, docs-check, hooks-test, ruff, debug/release/static-plugins/asan presets, python build, link-db, clang-tidy (core + `python/bindings/`), pyright, pytest | **`check: OK`**, 2026-09-21 | `docs/reviews/evidence/7755432-full-gate.log` (3577 lines), candidate `7755432` |
| pytest | 77/77 passed (0.11s) | Same log, tail; new tests: `test_config.py` (4), `test_derivative.py` (14), `test_signal_registry.py` (5), `test_simulation.py` (10), `test_write_input.py` (3) — 36 new, 41 pre-existing, all passing |
| C++ unit suite | 351/351 (349 pre-existing + 2 new `SimulationDerivative.*`) | Same log |
| R9 bit-identity (same-`Simulation`-object, N>13 `state_owner` fixture) | `SimulationDerivative.DerivativeThenARealFrameOnTheSameSimulationAgreeOnK1ForAStateOwningModel` — pass | `tests/unit/kernel/simulation_derivative_test.cpp` |
| R2a lossless round-trip (`Config.from_dict`) | Empirically verified against the real yaml-cpp 0.8.0 (`0.8.0+dfsg-6build1`) **before** writing the acceptance test — a standalone probe against every case Joe named (full-precision double, `-0.0`/`1e-300`/`1e300`, int beyond `2**53`, bool-before-int dispatch order, strings that look like other YAML types) all round-tripped exactly under yaml-cpp's *default* emitter, no precision/quoting override needed | `docs/plans/pr5-nanobind-bindings.md`'s "What Config from a Python dict needs" section (probe program + results table); `tests/python/test_config.py::test_from_dict_round_trips_scalars_exactly` |
| `kCoreApiVersion` | Unchanged (verified via `git diff HEAD -- core/include/astra/core_api.hpp.in`, empty) | This packet, checked explicitly per Joe's own reminder not to assume |
| Golden/tolerance files | None touched; no `ASTRA_TOUCH_GOLDENS` used this PR | `git diff --stat` above |

Toolchain: clang-18, CMake 3.28+, Eigen 3.4, yaml-cpp 0.8.0, nanobind 3.0.1, Python 3.12 — all the
repo's existing pinned versions; no new dependency (yaml-cpp is already project-wide, only a new
direct link edge for `_core`). CI is local-gate mode (`GATE=local`); the evidence above is the
actual full-gate result, not a GitHub check.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (`codex-companion` 1.0.6), fresh context, two passes
against the committed branch diff (base `origin/main` `96c9459`, head `194fb4e`):

- **General review** (thread `01a0c6e9-30d5-7200-8a1a-184010726cb3`): no actionable regressions.
  Ran the new Python tests directly under its own read-only sandbox (35/36 passed — the one
  excluded test writes a temp file, which that sandbox forbids; the real full-gate run above
  already covers it).
- **Adversarial review** (thread `01a0c6ea-599b-7931-bd4a-8d2b5734c60c`), focus set by Joe himself:
  ownership/lifetime across the Python/C++ boundary, probe side-effect isolation (R5a/R5b), R9
  bit-identity, and no new allocation on `Vehicle::integrate()`'s own per-frame path. **Verdict:
  approve, no material findings** in any of the four areas.

Zero critical/major/minor findings from either pass; nothing rejected or deferred. Full record:
`docs/reviews/pr5-nanobind-bindings-review.md`.

## Joe's attention

**Consequential files, in the order the actual data path traverses them**: `core/src/kernel/
vehicle.cpp`'s `evaluate_derivative_stage()` (the extracted shared arithmetic — worth confirming
by eye that it is byte-for-byte the same body the old inline lambda had) and `derivative()` (the
new probe entry point, especially the ordering: physics/wind checks → lazy `probe_health_` →
`begin_frame()` → the shared call → the unconditional `stage_` restore → the fault check) →
`core/src/kernel/simulation.cpp`'s two new one-paragraph methods → `python/bindings/_core.cpp`'s
`Simulation` class block (the `reference_internal` policy on every accessor) and `PyHaltReason`
(the eager-copy constructor) → the new tests, which show how each requirement was actually
interpreted (`tests/unit/kernel/simulation_derivative_test.cpp` for R9;
`tests/python/test_derivative.py` for the wind-raise and side-effect-isolation behavior Design
decisions 1/1b/1e/2 specify).

**Assumptions this candidate carries forward, not re-litigated here** (all already settled in the
approved plan): `Simulation::from_config` hardcodes `IntegratorKind::kRk4` (R9's own reason for a
same-`Simulation`-object proof instead of an Euler comparison); v1 builds exactly one `Vehicle`, so
`write_input`/`derivative` bind against `vehicles_[0]` with no vehicle-index parameter.

**Known, disclosed scope boundary** (Design decision 2, not a defect): `derivative()` never
resamples wind itself — it reads whatever `Vehicle::environment_` currently holds, exactly like
the real k1 stage. A `Simulation` with wind configured that has *never* called `step()`/`run()`
raises (tested: `test_derivative_raises_on_a_never_stepped_simulation_with_wind_configured`) rather
than silently substituting still air. No `sample_environment()`-style primitive was added in this
PR, per Joe's own explicit instruction; PR 6 decides separately whether it needs one.

**No new dependency, no interface break, no changed public signature** — purely additive to the
Python surface. **Rollback**: fully reversible pre-any-external-consumer (no released wheel,
editable-only per ADR-0009; nothing else in the tree — PR 6/7 don't exist yet — depends on these
bindings), a clean `git revert` of the three candidate commits.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: Config-from-dict took the smaller of two implementation shapes
(Joe's own settled option (a) — no new `core/` type at all, dict routed through the existing
`Config::from_string` via an emitted `YAML::Node`) rather than the originally-drafted core-owned
`Value` type, on the reasoning that a second producer doesn't exist yet to justify the abstraction;
verified lossless before being finalized, not merely asserted.
Accepted risks: the never-stepped-wind-configured-`Simulation` gap above (raises loudly; not
silently wrong) — owner PR 6, trigger: if `astra.trim` needs to run under wind without an initial
`step()`.
Nonblocking follow-ups: none recorded by either review pass.
Architectural decisions: none new; implements ADR-0021 R4 and ADR-0017's own PR 5 paragraph, both
already accepted.
