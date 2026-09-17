# AGENTS.md

Shared instructions for every coding agent in this repository, in one file: Claude Code reads it as `CLAUDE.md`;
Codex reads the same content through `AGENTS.md`, a symlink to `CLAUDE.md`. The `## Claude Code` section is Claude-Code-only.

ASTRA is a vehicle-agnostic 6-DoF simulation framework (C++23 core, Python bindings) that runs the aircraft's
real embedded software against the sim over a lockstep transport. `docs/architecture.md` is the design; treat it
as authoritative and do not re-decide anything it or an accepted ADR decides. `PLAN.md` says what is in progress.

## Read first

- `docs/architecture.md` — the design. §4 plugin model, §10 ICD/transport, §14 hard rules, §16 acceptance per milestone.
- `docs/process/workflow.md` — how work happens; §"Files that carry state" caps this file at 100 lines (symlinked as `AGENTS.md` for Codex).
- `docs/decisions/` — ADRs are immutable once accepted; supersede, never edit. `0000` is the founding index.
- `docs/lessons.md` — append-only gotchas. Add an entry when something bites; never rewrite old ones.
- `docs/refs/` — human-placed reference documents, gitignored and cited by page; see `docs/refs/README.md`.
- `STATE.md` / `PLAN.md` — what is in flight and the milestone checklist.

## Commands

`just check` is the single gate and the definition of done (CMake presets underneath; `just` from apt).

- `just check` — every preset (`debug`, `release`, `asan`, `static-plugins`) configures, builds with clang-18
  `-Werror`, and runs ctest; `just python` installs `astra._core` into `.venv` (editable, built in `build/python`,
  `just venv` creates the venv if missing); then `clang-tidy`, `clang-format --dry-run --Werror`, the hook test
  cases, `ruff`, `pyright` (strict on `python/astra/`), and `pytest`. Physics oracles and goldens join later.
- `just build [preset]` / `just test [preset]` / `just test-unit [preset]` (the Stop hook's `unit`-labeled
  subset) — one preset (default `asan`); `build/<preset>/` holds `lib/libastra_core.so` and plugin `.so`s.
- Single tests: `ctest --preset asan -R <name>` from the repo root; `.venv/bin/pytest tests/python -k <name>`.
- `just format` fixes formatting (clang-format and ruff); `just hooks-test` runs `.claude/hooks/tests/*.cases`;
  `just tidy` is clang-tidy alone (it runs `just python` first for the binding TU's compile database).
  `just check` never runs `format`, only the dry run.

Toolchain is apt-only on Ubuntu 24.04: clang-18, CMake 3.28, Eigen 3.4, HDF5 1.10 (C API), GoogleTest,
yaml-cpp; the Python 3.12 venv holds the pinned dev group from `pyproject.toml` (nanobind 3, scikit-build-core,
pytest, ruff, pyright). No vcpkg/Conan, no HighFive, no mp-units. Every build exports `compile_commands.json`;
`just link-db` links it at the repo root and under `python/`, or clangd reports false errors on Eigen and nanobind.
Prefer the clangd/pyright LSP tools over grep for symbol lookups; they are feedback, not the gate.

## Git and session rules

- **The oracle is human-owned.** `tests/golden/**`, tolerance files, and `docs/decisions/**` change only when Joe
  says so. `.claude/hooks/guard-oracle.sh` (PreToolUse on Edit/Write/Bash) blocks those paths; Joe lifts it with
  `ASTRA_TOUCH_GOLDENS=1` in the session's environment, or `touch .claude/touch-goldens` while the session runs (the
  SessionStart hook deletes the marker, so a lifted guard never carries into the next session).
  Widening a tolerance, narrowing a comparison, regenerating a golden, or weakening a test is a blocking review finding.

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

## Claude Code (Claude-Code-only)

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out. Push the branch, open the PR
  yourself; Joe squash-merges. Never put commit SHAs in process files.
- Model and effort are pinned in `.claude/settings.json` (`opusplan` at `high`, subagents on Sonnet); never change
  them. Escalation is Joe's (workflow.md §Model and effort); physics diffs are checked by Joe reading them, not by effort.
- Keep the context small: build/test output to a file, show the tail; one PR per session; no recon subagents.
- Run the gate with the Bash tool's own `run_in_background`, never `nohup … &` inside it (that double-backgrounds it
  and the completion notice reports the wrapper's exit). Then block on the real process (`tail --pid=$(pgrep -x just)
  -f /dev/null`) and read the log's `EXIT=0` / `check: OK` line; nothing is verified before that line has been read.
- Never commit from a Cowork session (leaves `.git/index.lock`; see `docs/lessons.md`).
- Review before the PR is Codex (other model, fresh context; reads `AGENTS.md`), run by the session from Bash:
  `node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait`; physics
  diffs also `adversarial-review --base origin/main --wait <focus naming the ADRs>`. The `/codex:*review` slash
  commands are user-only wrappers of that script; do not wait for Joe to run them. Never
  the built-in `/code-review`; never the Codex stop-time gate; never a critique subagent. A plan deciding a design the
  ADRs do not pin and that is expensive to unwind (M1: EOM, integrator, health monitor; M2: how gravity and
  atmosphere models get geodetic position from the Earth model, the wind injection point) gets one `/codex:rescue`
  round in plan mode before Joe sees it, plan and acceptance command inlined (workflow.md §Review); other plans skip it.
- End every session by running `/wrap` yourself (it is model-invocable); Joe does not trigger it.
