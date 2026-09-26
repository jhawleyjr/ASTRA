# ASTRA agent rules

Canonical shared instructions. Codex reads this file; Claude Code imports it from `CLAUDE.md`.
Claude owns planning/building/validation/fixes/integration; Codex owns independent/adversarial review.
ASTRA is a vehicle-agnostic 6-DoF framework: C++23 core, Python bindings, real embedded software over lockstep transport.

## Read first

- [Architecture](docs/architecture.md) and accepted [ADRs](docs/decisions/): authoritative engineering decisions.
- [Workflow](docs/process/workflow.md): risk, review, approval, Git, and release rules.
- [STATE.md](STATE.md) / [PLAN.md](PLAN.md): session handoff / milestone checklist; inspect Git before trusting status.
- [Lessons](docs/lessons.md): append-only gotchas. [References](docs/refs/README.md): human-placed, gitignored sources.
- The assigned spec/plan/task, if any; [agent procedures](docs/agents/README.md) and [templates](docs/templates/README.md).

## Commands and boundaries

- `./scripts/check-fast.sh` wraps `just test-unit`: ASan build and unit tests for iteration.
- `./scripts/check-full.sh` wraps `just check`: the definition of done; never substitute a fast pass for it.
- `just build [preset]`, `just test [preset]`, `ctest --preset asan -R <name>` for focused work.
- [Validation](docs/process/validation.md) describes Python, formatting, static analysis, logs, and CI limitations.
- Ubuntu 24.04, apt toolchain: clang-18, CMake 3.28+, Eigen 3.4, HDF5 C API, yaml-cpp, GoogleTest;
  Python 3.12+ and the dev group in `pyproject.toml`. No vcpkg/Conan, HighFive, or mp-units.
- Inspect the repository, Git status, and existing patterns first. Preserve unrelated work; stay within assigned scope.
- Reversible implementation, tests, and fixes within authorized scope do not need repeated approval.
- Joe owns requirements, architecture changes, new dependencies/toolchains, merges, releases, and credentials.
  Escalate a conflict or required scope expansion; never weaken acceptance to make a check pass.
- **The oracle is human-owned:** `tests/golden/**`, tolerance files, and accepted ADRs require Joe's explicit
  authorization. Never widen tolerances, narrow comparisons, regenerate goldens, or weaken tests to hide failure.
  Claude's guard is described in the workflow; other agents must honor the same boundary without relying on a hook.
- Use a task branch, never commit/merge/rebase on `main` or push to it. One writer per checkout; separate worktrees
  for concurrent builders. Preserve an explicitly assigned branch. Joe squash-merges; PRs start as drafts.
- Standing authorization (Joe): commit coherent work as you go, push the task branch after each meaningful commit, and
  open the draft PR at the first commit; hold nothing only locally. Never merge, force-push shared history, mark ready as a merge signal, or release.
- Codex reviews Claude's candidate in fresh context; Codex does not implement review fixes. Treat findings as
  claims to verify; follow the bounded fix loop.
- Report commands actually run, exit results, and gaps. A started job or plausible trace is not verified evidence.
- Accepted ADRs are immutable: propose a superseding record. Lessons are append-only. Keep standing rules free
  of transient SHAs; retain exact candidate revisions in evidence and link them from task/session handoffs. Keep this file plus `CLAUDE.md` under 100 lines.

## Non-standard conventions (the things an agent will silently get wrong)

- **Frames** (ADR-0001): body FRD (x fwd, y right, z down); nav NED; `q_nb` rotates n→b, Hamilton, scalar-first
  `(w,x,y,z)`, renormalized after every integrator step. Euler angles are 3-2-1 (ψ,θ,φ) derived outputs, never state.
- **State vector** (ADR-0003): 13 doubles, fixed layout `p_n(3) v_b(3) q_nb(4) ω_b(3)`. Position representation is
  owned by the Earth model (NED metres for flat; lat/lon rad + altitude for round/WGS-84). Mass is not state in v1.
- **Units** (ADR-0002): SI everywhere inside `core/`, `plugins/`, `aircraft/` (m, s, kg, rad, N, Pa, K). Conversions
  only at boundaries. Signal names carry no unit suffix; the unit string lives in the SignalRegistry metadata.
- **Time** (ADR-0010): integer `frame` counter is the source of truth; `t = frame * dt` derived, never accumulated.
  Fixed `dt = 1 ms`; every rate is an integer divisor of the major frame. Wall-clock access is restricted to the `RealTime` pacer per ADR-0010 and the lockstep transport timeout (ADR-0023); it never supplies simulation time.
- **Per-frame path** (`step`, `compute`, integrator, transport exchange, logger sample): no heap allocation, no
  exceptions, no blocking I/O, no `std::string`, no `dlopen`. Init may allocate and throw typed errors naming the
  config path. Step-time model errors go through status flags, not exceptions.
- **Randomness**: `std::mt19937_64` with in-house Box-Muller/uniform transforms (the standard distributions are not portable across libstdc++/libc++). Every stochastic plugin takes a seed from config; the seed is logged.
- **Floating point**: `double` everywhere, no `-ffast-math`. Eigen is a `SYSTEM` include so its warnings don't fight `-Werror`. Integrator is in-house fixed-step RK4 (ADR-0004); Odeint only in Python-side analysis tools.
- **ICD** (`icd/astra_icd.h`, ADR-0006): C11, `<stdint.h>` only, fixed-width fields, 8-byte alignment, static size
  asserts, version in every frame. Never edit a struct layout; add a `_vN` struct and bump the version.
- **Prose**: American English everywhere (comments, docs, ADRs, commit messages, PR bodies): honor, behavior,
  normalize, center, license. Never British spellings.
- **Embedded example** (`embedded/autopilot/`) is C++17 on purpose and links only `icd/` + `embedded/hal/`, never `astra_core`.

## Architecture in one breath

One CMake build produces `libastra_core.so` plus one `.so` per plugin, discovered from every `CMakeLists.txt` under
`plugins/**` (shared models by kind: `physical/`, `hardware/`, `sim/`) and `aircraft/<name>/**` (one aircraft's own
models, same kinds inside, copied from `aircraft/_template/`). A plugin is one thing a config can select, declared
with the one-line `astra_add_plugin(SOURCES ...)` helper; the loader resolves it by path at init only and fails loudly.
Anything that knows a wing area, thrust curve, or noise density lives under `plugins/` or `aircraft/`, never `core/`.
Integrators live in the core. The scheduler is single-threaded; per-frame order is environment → transport exchange
→ vehicle wrenches → EOM → integrator → post-step → logger. `Simulation` holds a list of `Vehicle`s of length one
(ADR-0011); nothing may assume exactly one vehicle. A sim that runs from a broken setup is worse than one that refuses.
