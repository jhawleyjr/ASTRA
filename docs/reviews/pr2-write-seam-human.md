# Human review packet: M3 PR 2 — control-input write seam

PR / candidate base and head: `main` at `829df62` (M3 PR 1, #37); committed and pushed candidate
`de4815e` on branch `worktree-m3-pr2-write-seam`, PR
<https://github.com/jhawleyjr/ASTRA/pull/38> (draft, opened this session) against `main`'s
`829df62`. `main` has not advanced since this candidate's base was taken. The docs-only planning
commit (`PLAN.md`, `docs/plans/pr2-write-seam.md`, `docs/decisions/0017-control-input-write-seam.md`)
landed on this branch as `3f4ca7e` before implementation started this session.
Spec/plan/task: `docs/decisions/0017-control-input-write-seam.md` (accepted, immutable) and `docs/
plans/pr2-write-seam.md`'s Design section and Acceptance table (R1–R13), both already reviewed and
approved across two Codex plan critiques and two of your own review rounds before this session.
Risk: **Tier 3** per `docs/plans/pr2-write-seam.md` — touches `core/kernel/` (`Vehicle`,
`Simulation::step`), the plugin-facing ABI (`WrenchSource`'s vtable, `kCoreApiVersion` 4→5), and
health behavior. Binding ADR: ADR-0017 (accepted).
Decision requested: approve behavior and authorize commit + draft PR (this session does not push or
open a PR without that authorization).

## What changes and why

Builds the write-seam primitive `docs/plans/pr2-write-seam.md` designed: a plugin (`WrenchSource`)
registers a named, externally-writable input from a new `bind_inputs()` hook; a config literal
(`vehicle.initial_controls`, this PR), Python (PR 5, not built here), and M4's transport later all
write through the same seam, once per major frame, with zero-order hold across a frame's four RK4
sub-stages. Before this PR nothing let code outside a `WrenchSource` set a value that model's
`wrench_b()` reads; `wrench_b()`'s own signature is unchanged.

New: `InputRegistry`/`InputHandle` (`core/include/astra/signals/input_registry.hpp`, `core/src/
signals/input_registry.cpp`) — `SignalRegistry`'s mutable counterpart, registry-owned storage in a
`std::deque` (so a `const double*`/`InputHandle` a plugin caches survives the `Vehicle` move
`Simulation::from_config`/`for_testing` perform), quiet-NaN defaults, get-or-create semantics for two
instances of one plugin sharing a name. `WrenchSource::bind_inputs()` (new virtual, default no-op) —
`kCoreApiVersion` bumps 4→5 (ABI break: every plugin recompiles). `Vehicle` gains an `inputs_`
member, a public `inputs()` accessor, and a new `reset()` method (nothing calls it yet, matching
`Model::reset()`'s own current status). `Simulation::from_config` applies `vehicle.initial_controls`'s
literals, validates every declared input is written and finite, then freezes the registry, all before
the vehicle moves into `vehicles_`. `Simulation::step` calls `latch()` once per vehicle per frame,
after `vehicle.step(ctx)` and before `vehicle.integrate(...)` — a `step()`-time writer (a future M4
`ActuatorModel`) is observed the same frame, never a frame late. `HealthMonitor::check_input`/
`kNonFiniteInput`/`health.input` (new, mirrors `report_out_of_domain`'s shape) names the input, not
the consuming `WrenchSource`, on a non-finite write; every other declared input still commits that
frame. `wrench_source.hpp`'s purity-comment amended: a control-seam member may change *between*
frames, but a conforming `wrench_b()`/`reset()` must never *write* through an `InputHandle` —
`tests/plugins/input_consumer` is the one labeled, test-only exception, and `step()` is the one
sanctioned write point. `docs/architecture.md` §5/§7/§9/§13/§15 updated to match.

Diff stat (tracked files, excluding this packet and its review record): 15 files changed, 352
insertions, 48 deletions — `core/CMakeLists.txt`, `core/include/astra/{core_api.hpp.in,interfaces/
wrench_source.hpp,kernel/{health_monitor.hpp,vehicle.hpp}}`, `core/src/{interfaces/wrench_source.cpp,
kernel/{health_monitor.cpp,simulation.cpp,vehicle.cpp}}`, `docs/architecture.md`, `tests/{physics/
no_alloc_test.cpp,python/test_package.py,unit/{CMakeLists.txt,health/health_monitor_test.cpp}}`,
`STATE.md`. Untracked (new): `core/include/astra/signals/input_registry.hpp`, `core/src/signals/
input_registry.cpp`, `tests/configs/no_alloc_write_seam.yaml`, `tests/plugins/input_consumer/`
(fixture plugin), `tests/unit/kernel/simulation_input_seam_test.cpp`, `tests/unit/signals/
input_registry_test.cpp`, and this packet plus `docs/reviews/pr2-write-seam-review.md`. No scope
outside what the ADR/plan authorized; the one addition beyond the original plan draft is
`InputRegistry::names()` (see Independent review's R-001).

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `./scripts/check-fast.sh` (initial candidate) | pass, 305/305 unit tests | ASan preset, `ctest -L unit` |
| `ctest --preset asan -R NoAlloc` | pass, 5/5 (R6: zero allocations over 1000 frames with a write-seam input configured) | `tests/configs/no_alloc_write_seam.yaml` |
| `ctest --preset asan -R Plugin` | pass, 19/19 (`AllPlugins` load-all including `input_consumer`; `bad_version` still rejected against the bumped version 5) | R12 |
| `./scripts/check-full.sh` (`just check`), run 1 | **failed** at `pytest`: `test_core_api_version_and_toolchain_string_come_from_libastra_core` pinned `== 4` | fixed to `== 5` (`tests/python/test_package.py`) — every other step (4 presets × 327 tests, clang-tidy, pyright) already green in that same run |
| `./scripts/check-full.sh` (`just check`), run 2 | pass, `check: OK`, exit 0 | 4 presets × 327 tests, clang-tidy 0 warnings on touched files, pyright 0 errors, pytest 38/38 |
| `./scripts/check-fast.sh` (post Codex fix) | pass, 306/306 unit tests | includes the new Config-reuse regression test (R-001) |
| `clang-format-18 --dry-run --Werror` on every touched file | pass after one `-i` pass (comment line-wrap only, no logic change) | |
| `run-clang-tidy-18` targeted at every touched core/test file | 0 warnings | full-tree run also came back clean inside `just check`'s own `tidy` step |

Full acceptance table (R1–R13) evidence, by ID: R1 `tests/unit/signals/input_registry_test.cpp`
(16 tests). R2/R7/R9/R11 and the Config-reuse regression: `tests/unit/kernel/
simulation_input_seam_test.cpp` (9 tests). R3/R4/R5: same file, the mid-frame-write and
step()-time-write tests, using `tests/plugins/input_consumer`'s labeled test-only `wrench_b()`
write. R6: `tests/physics/no_alloc_test.cpp`'s new case above. R8: `tests/unit/health/
health_monitor_test.cpp`'s five new `check_input`/partial-commit tests. R10: `tests/unit/kernel/
simulation_input_seam_test.cpp`'s two-vehicle independence and forgotten-write tests. R12: the
`Plugin`-labeled run above. R13: `check-full.sh` run 2.

No numerical/physics tolerance changed; no golden/oracle file touched; `touch-goldens` was not armed
and no `docs/decisions/` file was written or edited this session beyond what the prior planning
session already produced and you accepted.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (`codex-companion` 1.0.6), fresh context, two
dispatches — general review, then adversarial review focused on the ABI bump, the no-alloc per-frame
path, pointer stability across the `Vehicle` move, `latch()` ordering, and health-check attribution.
Full findings: `docs/reviews/pr2-write-seam-review.md`.

One finding, fixed and Codex-verified in the same adversarial pass (verdict "approve", "No material
findings" against the fix and the rest of the candidate):
- **R-001** (major): `apply_initial_controls` originally enumerated which `vehicle.initial_controls`
  leaves to apply via `Config::unknown_keys()`, which diffs against a `Config` document's own
  shared, mutable read-tracking (`Config`'s `document_` is a `shared_ptr`, shared by every copy). A
  second `Simulation` built from the *same* `Config` object — a real future case, PLAN.md M5's
  64-run Monte Carlo driver — would find every input's literal already marked read by the first
  construction and silently skip applying it the second time, throwing a spurious `ConfigError`
  instead. Fixed by adding `InputRegistry::names()` (an init-time enumeration of registered entries,
  independent of any `Config`) and looking each one up via ordinary `Config::has()`/`::number()`
  calls, neither of which depends on prior read-state. This also matches the ADR's own Design
  section more closely than the original draft ("one call per declared input"). Added a regression
  test building two `Simulation`s from one `Config` object.

No critical findings. No findings rejected or deferred.

## Joe's attention

- **The ABI bump itself** (`kCoreApiVersion` 4→5, `core/include/astra/core_api.hpp.in`): every
  existing plugin recompiles; `bad_version`'s rejection test confirms an out-of-tree plugin built
  against version 4 is still rejected, not dispatched through a vtable slot it doesn't have.
- **`Simulation::step`'s new call site** (`core/src/kernel/simulation.cpp`, the `latch_vehicle_inputs`
  helper, factored out for clang-tidy's cognitive-complexity threshold): placed between
  `vehicle.step(ctx)` and `vehicle.integrate(...)`, matching §5's "receive commands" position. This
  is the exact ordering decision your own two review rounds on the plan document reversed twice
  (originally before `step()`, moved after to avoid baking in a one-frame delay for a future
  step()-time writer) — worth confirming the end-to-end R3/R4 tests actually pin what you intended.
- **The `wrench_source.hpp` purity-comment amendment**: a conforming `WrenchSource` may now see its
  own bound input change *between* frames (new), but is still prohibited from *writing* through an
  `InputHandle` from `wrench_b()` or `reset()` — `tests/plugins/input_consumer` is a labeled,
  test-only exception to the first half, doing exactly what a real plugin must not, to prove R3.
  Worth reading that plugin's own header comment alongside the amended interface comment together.
- **The disclosed M4-scope gap ADR-0017 already carries** (not addressed here, not expected to be):
  `bind_inputs()` is `WrenchSource`-only; a non-`WrenchSource` `ActuatorModel` has no seam-provided
  route to a write handle for a name it doesn't own. M4's own plan resolves this.
- No golden/oracle files touched, no tolerance changed. Rollback: this candidate is additive plus
  one interface amendment and one ABI bump — reverting is a straightforward revert of the commit(s)
  once they exist; no data/schema migration.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: none beyond what `docs/plans/pr2-write-seam.md`'s own "Questions and
decisions" table already recorded and you already approved (Q1–Q3, (h)).
Accepted risks: the M4-scope gap ADR-0017 discloses (owner: M4's own plan, trigger: M4 planning
start) — not this PR's to fix.
Nonblocking follow-ups: none new this round.
Architectural decisions: ADR-0017 (accepted) is the binding architecture for this PR; no new or
superseding ADR proposed.
