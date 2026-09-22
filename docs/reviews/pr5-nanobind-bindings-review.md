# Review: M3 PR 5 — nanobind bindings (Simulation, Config, SignalRegistry, write seam, derivative)

Spec/plan: `docs/plans/pr5-nanobind-bindings.md` (approved by Joe, 2026-09-21)
Builder: Claude (Sonnet 5)
Reviewer: Codex, `codex-companion` 1.0.6, fresh context — one general pass, one adversarial pass
Scope: base `origin/main` at `96c9459`, head `194fb4e` (candidate implementation `7755432` plus
its retained-evidence commit); committed branch diff, working tree clean at dispatch time
Validation: `docs/reviews/evidence/7755432-full-gate.log` — `check: OK`, all four presets
(debug/release/static-plugins/asan), clang-tidy clean (core + `python/bindings/`), pyright 0
errors, pytest 77/77
Round: initial

## General review

Thread `01a0c6e9-30d5-7200-8a1a-184010726cb3`. Inspected the full branch diff against
`origin/main`, `AGENTS.md`/architecture/workflow docs, `core/src/kernel/vehicle.cpp`'s
`evaluate_derivative_stage`/`derivative` methods, `python/bindings/_core.cpp`, and ran the new
Python tests directly (`tests/python/test_config.py` excluding the one file-writing case its
read-only sandbox can't execute, and the rest — 35 passed). **No actionable regressions
identified.** Did not rerun the full validation gate (not required of a read-only reviewer;
`docs/reviews/evidence/7755432-full-gate.log` already covers it).

## Adversarial review (Joe's own requested focus, four areas)

Thread `01a0c6ea-599b-7931-bd4a-8d2b5734c60c`. Focus text named the plan's own Design decisions
1/1b/1e/6/7 and acceptance rows R5a/R5b/R9 explicitly, plus the no-allocation invariant on
`Vehicle::integrate()`'s own per-frame path. Inspected `python/bindings/_core.cpp`'s
`nb::rv_policy::reference_internal` chains against the installed nanobind's own `ndarray`/type
lifetime source (`.venv/lib/python3.12/site-packages/nanobind/`), `PyHaltReason`'s construction
sites, `Vehicle::evaluate_derivative_stage`/`derivative`/`integrate`, and
`tests/unit/kernel/simulation_derivative_test.cpp`. Read-only: no builds or tests run.

**Verdict: approve.**

> "No material defects supported in the four requested areas. Ownership chains retain Simulation;
> halt sources are eagerly copied; probes isolate wrench/health storage and restore stage state
> before raising; both callers share identical derivative arithmetic and access the real k1
> scratch. The per-frame refactor adds no allocation or exception path."

## Completion

Zero findings from either pass — a valid, not invented, result: Joe's own four-area adversarial
focus is exactly the design's highest-risk surface (the ownership/lifetime and side-effect-
isolation questions the plan's own critique round already iterated on twice before implementation
began), and both passes independently confirm it holds in the actual shipped code, not only in
the plan's own reasoning. No critical/major open findings, nothing rejected, nothing deferred, no
scope not reviewed (full branch diff, both passes). Codex's recommendation is not Joe's approval;
proceeding to `/human-review` for his own decision.
