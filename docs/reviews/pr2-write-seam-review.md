# Review: M3 PR 2 — control-input write seam (ADR-0017)

Spec/plan: `docs/decisions/0017-control-input-write-seam.md` (accepted); `docs/plans/
pr2-write-seam.md`'s Design section and Acceptance table (R1–R13).
Builder: Claude (Sonnet 5)
Reviewer: Codex (general review + adversarial review, `codex-companion` 1.0.6, fresh context)
Scope: uncommitted working-tree diff against `main` at `829df62` (M3 PR 1, #37), including new
files under `core/include/astra/signals/input_registry.hpp`, `core/src/signals/input_registry.cpp`,
`tests/plugins/input_consumer/`, `tests/unit/signals/input_registry_test.cpp`, `tests/unit/kernel/
simulation_input_seam_test.cpp`, `tests/configs/no_alloc_write_seam.yaml`, plus modifications to
`core/include/astra/{interfaces/wrench_source.hpp,kernel/{health_monitor.hpp,vehicle.hpp},
core_api.hpp.in}`, `core/src/{interfaces/wrench_source.cpp,kernel/{health_monitor.cpp,simulation.cpp,
vehicle.cpp}}`, `tests/{physics/no_alloc_test.cpp,python/test_package.py,unit/health/
health_monitor_test.cpp}`, `docs/architecture.md` §5/§7/§9/§13/§15.
Validation: `./scripts/check-fast.sh` (306 unit tests) green before dispatch; `./scripts/
check-full.sh` (`just check`, all four presets, clang-tidy, pyright, pytest) run twice — first run
caught a stale pinned Python constant (`test_core_api_version_and_toolchain_string_come_from_
libastra_core` asserted `== 4`, now `== 5`, fixed and re-verified green: `check: OK`, 38 pytest
passed) — before Codex dispatch. `./scripts/check-fast.sh` rerun green (306/306) after R-001's fix
below.
Round: initial (general review found no findings; adversarial review found R-001)

## Finding R-001

Severity: major
Status: verified
Location: `core/src/kernel/simulation.cpp`'s `apply_initial_controls` (pre-fix: enumerated names via
`config.unknown_keys()`)
Requirement/ADR: ADR-0017's `vehicle.initial_controls` config-literal application; general
`Config`-reuse correctness (no ADR pins this, but it is a load-bearing assumption of the fix)
Failure: `Config`'s `read_paths` bookkeeping lives in a `shared_ptr<Document>`
(`core/src/config/config.cpp`'s `struct Document`), shared by every copy of one `Config` object, not
reset between calls. The original `apply_initial_controls` enumerated which `vehicle.
initial_controls` leaves to apply by diffing `config.unknown_keys()` against that shared read-state.
A second `Simulation::from_config` built from the *same* `Config` object (e.g. a future Monte Carlo
driver reusing one loaded document, PLAN.md M5) would find every declared input's literal already
marked read by the first construction's own `config.number()` calls, so the second construction's
loop would find nothing to apply and throw `first_unwritten_or_non_finite`'s `ConfigError` instead
of building a working `Simulation` — a real, reachable regression for any caller that constructs
more than one `Simulation` from one `Config`, not merely a theoretical one.
Requested outcome: applying `vehicle.initial_controls` must not depend on `Config`'s own read-state,
so the same `Config` object can build any number of `Simulation`s.
Builder response: fixed. Added `InputRegistry::names()` (`core/include/astra/signals/
input_registry.hpp`, `core/src/signals/input_registry.cpp`) — an init-time enumeration of every
registered entry's own dotted name, independent of any `Config`. `apply_initial_controls` now
iterates `vehicle.inputs().names()` and looks each one up via ordinary `Config::has()`/`::number()`
calls under the `vehicle.initial_controls` subtree — neither of which depends on prior read-state —
instead of diffing `unknown_keys()`. This also matches the ADR's own Design section more closely
than the original draft did ("one call per declared input: `vehicle_config.at("initial_controls").
number(name)`").
Verification: rebuilt and reran the full unit suite (306/306 passed, including the seam's existing
R2–R11 tests, unaffected by the enumeration-mechanism change since the values read are identical).
Added a regression test, `SimulationInputSeam.ApplyingInitialControlsDoesNotDependOnTheConfigsOwnReadState`
(`tests/unit/kernel/simulation_input_seam_test.cpp`): builds two `Simulation`s from the same `Config`
object and asserts both correctly apply the literal. Codex (adversarial, same working-tree diff,
inline verification within the adversarial pass rather than a separate fix-verification dispatch):
"The Config-reuse fix is sound; ABI rejection, deque-backed pointer stability, latch ordering,
allocation-free frame operations, and input fault attribution match ADR-0017" — verdict "approve",
"No material findings."

## Completion

Zero critical findings. One major (R-001), fixed and Codex-verified in the same adversarial pass
(verdict "approve", no material findings against the fix or the rest of the candidate). None
rejected or deferred. Adversarial focus explicitly covered: the `kCoreApiVersion` 4→5 ABI bump and
`bad_version`'s continued rejection; the per-frame path (`InputRegistry::latch()`/`write()`,
`InputHandle::write()`, `Simulation::step`'s `latch_vehicle_inputs` helper) for allocation/exception/
blocking; pointer stability across the `Vehicle` move (`InputRegistry`'s `std::deque<Entry>` storage
against `sim.vehicles_.push_back(std::move(vehicle))`); `latch()`'s ordering between
`vehicle.step(ctx)` and `vehicle.integrate(...)` for both a `step()`-time writer (same-frame,
no lag) and a `wrench_b()`-time writer (deferred to next frame); and `HealthMonitor::check_input`'s
attribution (names the input, not the consuming `WrenchSource`) plus `InputRegistry::latch()`'s
partial-commit invariant. No defect reported in any of those areas. Codex's approval is not Joe's
approval; `/human-review` and Joe's own reading of the candidate diff are the outstanding gate
(Tier 3: "Joe reads the critical diff/tests").
