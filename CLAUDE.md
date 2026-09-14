# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ASTRA is a vehicle-agnostic 6-DoF simulation framework (C++23 core, Python bindings) that runs the aircraft's
real embedded software against the sim over a lockstep transport. **Status: M0 in progress** (see `PLAN.md`).
`docs/architecture.md` is the design and the sole input to M0; treat it as authoritative. Do not re-decide
anything it decides; §17 lists the ADRs that must exist before other code is written.

## Read first

- `docs/architecture.md` — the design. §4 plugin model, §10 ICD/transport, §14 hard rules, §16 acceptance per milestone.
- `docs/process/workflow.md` — how work happens; §"Files that carry state" caps this file at 100 lines.
- `docs/decisions/` — ADRs are immutable once accepted; supersede, never edit. `0000` is the founding index.
- `docs/lessons.md` — append-only gotchas. Add an entry when something bites; never rewrite old ones.
- `STATE.md` / `PLAN.md` (created in M0) — what is in flight and the milestone checklist.

## Commands

`just check` is the single gate and the definition of done (CMake presets underneath; `just` from apt).

- `just check` — every preset (`debug`, `release`, `asan`, `static-plugins`) configures, builds with clang-18
  `-Werror`, and runs ctest; then `clang-tidy`, `clang-format --dry-run --Werror`, and the hook test cases.
  Later milestones add pytest, physics oracle tests, and golden-trajectory regression to the same recipe.
- `just build [preset]` / `just test [preset]` — one preset (default `asan`); `build/<preset>/` holds
  `lib/libastra_core.so` and `plugins/libastra_<path_with_underscores>.so`.
- Single tests: `ctest --preset asan -R <name>` from the repo root; `pytest tests/scenarios -k <name>` (M5).
- `just format` fixes formatting; `just hooks-test` runs `.claude/hooks/tests/*.cases`; `just tidy` is
  clang-tidy alone. `just check` never runs `format`, only the dry run.

Toolchain is apt-only on Ubuntu 24.04: clang-18, CMake 3.28, Eigen 3.4, HDF5 1.10 (C API), GoogleTest,
yaml-cpp; nanobind 3 + scikit-build-core via pip in a Python 3.12 venv. No vcpkg/Conan, no HighFive, no mp-units.
Every preset exports `compile_commands.json`, linked at the repo root, or clangd reports false errors on Eigen.
Prefer the clangd/pyright LSP tools over grep for symbol lookups; they are feedback, not the gate.

## Git and session rules

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out. Push the branch, open the PR
  yourself; Joe squash-merges. Never put commit SHAs in process files.
- One task = one PR = one verifiable outcome. A task is 3-10 lines: intent, acceptance command, ADR pointers.
  If the diff fits in one sentence, skip plan mode.
- **The oracle is human-owned.** `tests/golden/**`, tolerance files, and `docs/decisions/**` change only when Joe
  says so (session started with `/task --touch-goldens`). `.claude/hooks/guard-oracle.sh` (PreToolUse on Edit/Write/
  Bash) blocks those paths; the override is the gitignored marker `.claude/touch-goldens` or `ASTRA_TOUCH_GOLDENS=1`.
  Widening a tolerance, narrowing a comparison, regenerating a golden, or weakening a test is a blocking review finding.
- Physics-touching diffs (`core/kernel/`, `core/util/`, `plugins/physical/`, transports, anything writing a golden)
  run at effort `xhigh` and Joe reads them. Default model/effort is pinned in `.claude/settings.json`.
- Never commit from a Cowork session (leaves `.git/index.lock`; see `docs/lessons.md`).

## Non-standard conventions (the things an agent will silently get wrong)

- **Frames** (ADR-0001): body FRD (x fwd, y right, z down); nav NED; `q_nb` rotates n→b, Hamilton, scalar-first
  `(w,x,y,z)`, renormalized after every integrator step. Euler angles are 3-2-1 (ψ,θ,φ) derived outputs, never state.
- **State vector** (ADR-0003): 13 doubles, fixed layout `p_n(3) v_b(3) q_nb(4) ω_b(3)`. Position representation is
  owned by the Earth model (NED metres for flat; lat/lon rad + altitude for round/WGS-84). Mass is not state in v1.
- **Units** (ADR-0002): SI everywhere inside `core/`, `plugins/`, `aircraft/` (m, s, kg, rad, N, Pa, K). Conversions
  only at boundaries. Signal names carry no unit suffix; the unit string lives in the SignalRegistry metadata.
- **Time** (ADR-0010): integer `frame` counter is the source of truth; `t = frame * dt` derived, never accumulated.
  Fixed `dt = 1 ms`; every rate is an integer divisor of the major frame. Nothing reads the wall clock.
- **Per-frame path** (`step`, `compute`, integrator, transport exchange, logger sample): no heap allocation, no
  exceptions, no blocking I/O, no `std::string`, no `dlopen`. Init may allocate and throw typed errors naming the
  config path. Step-time model errors go through status flags, not exceptions.
- **Randomness**: `std::mt19937_64` with in-house Box-Muller/uniform transforms (the standard distributions are not
  portable across libstdc++/libc++). Every stochastic plugin takes a seed from config; the seed is logged.
- **Floating point**: `double` everywhere, no `-ffast-math`. Eigen is a `SYSTEM` include so its warnings don't fight
  `-Werror`. Integrator is in-house fixed-step RK4 (ADR-0004); Odeint only in Python-side analysis tools.
- **ICD** (`icd/astra_icd.h`, ADR-0006): C11, `<stdint.h>` only, fixed-width fields, 8-byte alignment, static size
  asserts, version in every frame. Never edit a struct layout; add a `_vN` struct and bump the version.
- **Style**: Google C++ via `.clang-format`; `snake_case` functions/vars, `CamelCase` types, trailing-underscore
  members, `.hpp`/`.cpp`. clang-tidy `bugprone-*, performance-*, modernize-*, readability-*` minus
  `readability-identifier-length`. Python: `ruff` lint+format, `pyright` strict on `python/astra/`.
- **Prose**: American English everywhere (comments, docs, ADRs, commit messages, PR bodies): honor, behavior,
  normalize, center, license. Never British spellings.
- **Embedded example** (`embedded/autopilot/`) is C++17 on purpose and links only `icd/` + `embedded/hal/`, never
  `astra_core`.

## Architecture in one breath

One CMake build produces `libastra_core.so` plus one `.so` per plugin, discovered from every `CMakeLists.txt` under
`plugins/**` (shared models by kind: `physical/`, `hardware/`, `sim/`) and `aircraft/<name>/**` (one aircraft's own
models, same kinds inside, copied from `aircraft/_template/`). A plugin is one thing a config can select; it exports
exactly one C symbol `astra_plugin()` returning a descriptor, declared with the one-line `astra_add_plugin(SOURCES ...)`
helper that derives the plugin path from its directory. The loader `dlopen`s by path at init only, checks
`core_api_version` and toolchain string, and fails loudly. `ASTRA_STATIC_PLUGINS` links everything into one binary.
Anything that knows a wing area, thrust curve, or noise density lives under `plugins/` or `aircraft/`, never `core/`.
Integrators live in the core. The scheduler is single-threaded; per-frame order is environment → transport exchange
→ vehicle wrenches → EOM → integrator → post-step → logger. `Simulation` holds a list of `Vehicle`s of length one
(ADR-0011); nothing may assume exactly one vehicle except CLI defaults. Health monitors (§15) halt on NaN, quaternion
drift, missed lockstep exchange, or log overflow; a sim that runs from a broken setup is worse than one that refuses.
