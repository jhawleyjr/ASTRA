# ASTRA agent rules

Canonical shared instructions. Codex reads this file; Claude Code imports it from `CLAUDE.md`.
Claude owns planning/building/validation/fixes/integration; Codex owns independent/adversarial review.
ASTRA is a vehicle-agnostic 6-DoF framework: C++23 core, Python bindings, real embedded software over lockstep transport.

## Read first

- [Architecture](docs/architecture.md) and accepted [ADRs](docs/decisions/): authoritative engineering decisions.
- [Workflow](docs/process/workflow.md): risk tiers with each tier's minimum process, then procedures and rationale.
- [PLAN.md](PLAN.md): milestone checklist, open items, one line per PR. [STATE.md](STATE.md): session handoff,
  rewritten once, at `/wrap`. Neither records a fact that only becomes true at merge (merge hashes, merge or
  approval dates, round counts, review narratives) or changes when the PR merges: git log and the PR hold those.
  Inspect Git before trusting either.
- [Lessons](docs/lessons.md): append-only gotchas. [References](docs/refs/README.md): human-placed, gitignored sources.
- The assigned spec/plan/task, if any; [agent procedures](docs/agents/README.md) and [templates](docs/templates/README.md).

## Commands and boundaries

- `./scripts/check-fast.sh` wraps `just test-unit`: ASan build and unit tests for iteration.
- `./scripts/check-full.sh` wraps `just check`: the definition of done; never substitute a fast pass for it. It
  passes only on exit 0 plus its `check: OK` line. The only exceptions are in the [tier table](docs/process/workflow.md#risk-tiers).
- `just build [preset]`, `just test [preset]`, `ctest --preset asan -R <name>` for focused work.
- [Validation](docs/process/validation.md) describes Python, formatting, static analysis, logs, and CI limitations.
- Evidence: commit a trimmed gate log, `docs/reviews/evidence/<head>-gate-summary.log` ([how](docs/process/validation.md#evidence)).
  Full logs stay outside git; commit one, as `<head>-full-gate.log`, only when the run fails or Joe asks.
- Check and review results (command, exit code, summary counts, the Codex verdict and thread id) go in the PR
  body, never in a record-only commit.
- Ubuntu 24.04, apt toolchain: clang-18, CMake 3.28+, Eigen 3.4, HDF5 C API, yaml-cpp, GoogleTest;
  Python 3.12+ and the dev group in `pyproject.toml`. No vcpkg/Conan, HighFive, or mp-units.
- Inspect the repository, Git status, and existing patterns first. Preserve unrelated work; stay within assigned scope.
- Read code directly: no recon or exploration subagents (cost and fidelity, `docs/lessons.md` 2026-09-14). The
  Codex review and plan-critique dispatch is the only subagent route.
- Reversible implementation, tests, and fixes within authorized scope do not need repeated approval.
- Joe owns requirements, architecture changes, new dependencies/toolchains, model/effort settings
  (`.claude/settings.json`), merges, releases, and credentials. Approval is Joe's explicit decision or his
  squash-merge, never inferred from a check, review, or label. Escalate a conflict or required scope expansion;
  never weaken acceptance to make a check pass.
- **The oracle is human-owned:** `tests/golden/**`, tolerance files, and accepted ADRs require Joe's explicit
  authorization. Never widen tolerances, narrow comparisons, regenerate goldens, or weaken tests to hide failure.
  Claude's guard is described in the workflow; other agents must honor the same boundary without relying on a hook.
- Use a task branch, never commit/merge/rebase on `main` or push to it. One writer per checkout; separate worktrees
  for concurrent builders. Preserve an explicitly assigned branch. Joe squash-merges; PRs start as drafts.
- Standing authorization (Joe): commit coherent work as you go, push the task branch after each meaningful commit, and
  open the draft PR at the first commit; hold nothing only locally. Integrate `main` by merging it into the task
  branch; never rebase or force-push a pushed branch. Never merge a PR, mark ready as a merge signal, or release.
  Pushing is not permission to message people (email, chat, issue or PR comments to others).
- Codex reviews Claude's candidate in fresh context; Codex does not implement review fixes. Treat findings as
  claims to verify. Self-review, or Codex being unavailable, leaves review pending. At most two autonomous
  correction rounds and two failed integrations per task, then escalate to Joe; a new context does not reset them.
- Report commands actually run, exit results, and gaps. A started job or plausible trace is not verified evidence.
- Accepted ADRs are immutable: propose a superseding record. Lessons are append-only. Keep standing rules free
  of transient SHAs; exact candidate revisions belong in evidence and the PR body. Keep this file plus `CLAUDE.md` under 100 lines.

## Non-standard conventions (the things an agent will silently get wrong)

- **Frames** (ADR-0001): body FRD (x fwd, y right, z down); nav NED; `q_nb` rotates n→b, Hamilton, scalar-first
  `(w,x,y,z)`, renormalized after every integrator step. Euler angles are 3-2-1 (ψ,θ,φ) derived outputs, never state.
- **State vector** (ADR-0003, ADR-0021): the rigid-body block is 13 doubles, fixed layout `p_n(3) v_b(3) q_nb(4) ω_b(3)`,
  first in a buffer models may extend with their own states. Position representation is owned by the Earth model
  (NED meters for flat; lat/lon rad + altitude for round/WGS-84). Mass is not state in v1.
- **Units** (ADR-0002): SI everywhere inside `core/`, `plugins/`, `aircraft/` (m, s, kg, rad, N, Pa, K). Conversions
  only at boundaries. Signal names carry no unit suffix; the unit string lives in the SignalRegistry metadata.
- **Time** (ADR-0010): integer `frame` counter is the source of truth; `t = frame * dt` derived, never accumulated.
  Fixed `dt = 1 ms`; every rate is an integer divisor of the major frame. In `core/`, `plugins/` and `aircraft/`, wall-clock access is restricted to the `RealTime` pacer (ADR-0010) and the lockstep transport timeout (ADR-0023); it never supplies simulation time.
- **Per-frame path** (`step`, `wrench_b`, integrator, transport exchange, logger sample): no heap allocation, no
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
