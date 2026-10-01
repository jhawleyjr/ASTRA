# M0 kickoff — restatement, PR split, and the first PR

Recovered from `~/.claude/plans/read-readme-md-and-follow-starry-candy.md` on 2026-09-30. Approval: the file records none (it asks Joe to approve in plan mode); the first PR merged as #1. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

`README.md` §"Starting M0" is the kickoff prompt: restate the plugin model (§4) and the ICD boundary (§10) of
`docs/architecture.md`, split M0 (§16) into three or four PRs, and execute the first PR (settings, `CLAUDE.md`,
ADR-0001…0011, then the oracle-guard hook). Plan mode is a checkpoint: if the restatement matches the
architecture doc, approve and let it run. Nothing here re-decides anything the architecture doc decides.

Environment facts found during recon:
- Checkout is on branch `m0` (no upstream) in the main working copy, not a `--worktree`. The no-main hook only
  blocks commits on `main`, so committing on `m0` here is allowed. The untracked files the first PR needs
  (`CLAUDE.md`, `.claude/settings.json`, `.claude/hooks/no-main.sh`, `.github/rulesets/main.json`,
  `docs/lessons.md`) plus the modified `README.md` / `docs/process/workflow.md` are in this checkout.
- GitHub ruleset `protect-main` (id 23269666) is already active; `gh` is logged in as jhawleyjr.
- Installed: clangd-18, cmake 3.28, ninja, gcc, python3.12, Eigen 3.4, HDF5 1.10, GoogleTest 1.14, yaml-cpp 0.8.
  Missing (apt candidates exist): `clang-18`, `clang-tidy-18`, `clang-format-18`, `just`. No nanobind venv yet.
  Not needed for PR 1; needed before PR 2 (Joe installs: `sudo apt install clang-18 clang-tidy-18 clang-format-18 just`).
- Claude plugins `clangd-lsp` and `pyright-lsp` are installed at user scope.

## Restatement (the checkpoint)

**Plugin model (§4).** One CMake invocation builds `libastra_core.so` — the kernel (interfaces, scheduler,
`VirtualClock`, EOM, integrators, plugin loader, `SignalRegistry`, config) that knows no aircraft and no
hardware — plus one `.so` per plugin. A plugin is one thing a configuration can select (an atmosphere, an
engine, an IMU, a transport); its internals are ordinary classes hidden by `-fvisibility=hidden`, and it
exports exactly one C symbol, `astra_plugin()`, returning a `PluginDescriptor {category, name,
core_api_version, built_with, create}`. Plugins live under two roots that share one taxonomy inside
(`physical/`, `hardware/`, `sim/`; no `vehicle/` kind): `plugins/` holds shared models by kind, and
`aircraft/<name>/` holds one aircraft's own models and configs, cloned from `aircraft/_template/`. The build
discovers every `CMakeLists.txt` under both roots; each is the one-liner `astra_add_plugin(SOURCES ...)`,
which derives the plugin path from its directory, so copying an aircraft folder renames its plugins with no
edits. The loader resolves a config path such as `plugins/physical/atmosphere/isa1976` to
`libastra_plugins_physical_atmosphere_isa1976.so` in the build's plugin directory, `dlopen`s it at init only,
checks `core_api_version` and the `built_with` toolchain string against its own, and fails with a typed init
error naming the plugin on mismatch (which can only come from a stale build). `ASTRA_STATIC_PLUGINS` links
every plugin into one binary and registers the same descriptors by direct call — the RT/embedded-host shape,
with no second code path. Nobody ships prebuilt `.so` files; the tree is the unit of distribution. A config
names one plugin per slot from either root, so swapping an engine is a config edit. Integrators are core, not
plugins.

**ICD boundary (§10).** `icd/astra_icd.h` is plain C11 including only `<stdint.h>`: `astra_sim_to_sw_v1`
(200 bytes — magic, version, frame, t, φθψ, pqr, uvw, α β, tas/cas/mach, altitude/VS, lat/lon, body
accelerations, status/reserved) and `astra_sw_to_sim_v1` (64 bytes — magic, version, echoed frame, elevator/
aileron/rudder in rad, two throttles 0..1, mode/fault flags), fixed-width types, 8-byte natural alignment,
sizes pinned by `_Static_assert`, magic and version in every frame. A layout is never edited: growth is a
`_vN` struct plus a version bump, because the FPGA side may not rebuild at the same time. It is the only thing
the flight software shares with the sim: `embedded/` is a separate CMake project inside the monorepo that
links `icd/` and `embedded/hal/` (`hal_wait_frame`, `hal_send`, `hal_now`) and never `astra_core`. On the sim
side the `Transport` interface (`send`, `recv` with timeout, lockstep `exchange`) is itself a plugin under
`plugins/sim/transport/`: `shm` for v1 (`shm_open` + `mmap` region holding a header with peer pid, both
frames, and two 32-bit futex sequence words; sim writes the frame and publishes `req_seq = frame`, the
software answers and publishes `ack_seq = frame`, sim waits with a timeout — raw futexes rather than condvars
so it survives `SCHED_FIFO` later), `loopback` for in-process tests, and FPGA/UDP later. The transport has its
own divisor (default 10 → 100 Hz) with zero-order hold on commands between exchanges; it blocks the scheduler
in lockstep in either clock mode; a missed exchange is a health halt. HWIL changes only the `Transport`
implementation, nothing else.

## M0 split into four PRs

| PR | Branch | Content | Acceptance (must be a command) |
|---|---|---|---|
| **1. Decisions and guards** (this session) | `m0` | Commit the existing untracked bootstrap files; ADR-0001…0011; `.gitignore`; seed `PLAN.md` and `STATE.md`; then the oracle-guard hook. | `ls docs/decisions/00{01..11}-*.md` all exist, each with Context / Decision / Rejected alternatives / Consequences; hook script exits 2 on an `Edit` of a `docs/decisions/` path and 0 on a normal path (tested by piping JSON into it) |
| **2. Build skeleton, core, loader, ICD, CI** | `m0-build` | `CMakeLists.txt`, `CMakePresets.json` (`debug`, `asan` = clang-18 `-Werror` ASan/UBSan, `release`, `static-plugins`; all export `compile_commands.json`, symlinked at root), `justfile` (`build`, `check`, `test`, `fmt`, `tidy`), `.clang-format`, `.clang-tidy`, `cmake/AstraPlugin.cmake` (`astra_add_plugin`, discovery of `plugins/**`, `aircraft/**`, `tests/plugins/**`; `ASTRA_STATIC_PLUGINS`), `core/` with `Model` base, `PluginDescriptor`, loader, `ASTRA_CORE_API_VERSION` + `built_with` string; `icd/astra_icd.h` verbatim from §10 with a C11 TU built by `gcc -std=c11` and a C++23 TU by clang-18; `tests/plugins/dummy/` and `tests/plugins/bad_version/`; GoogleTest loader tests (load by path, reject bad version naming the plugin, same test under static-plugins); `.github/workflows/check.yml` running `just check` on `ubuntu-24.04`, job named `check`. | `just check` exits 0 under `asan` and `static-plugins`; CI green on the PR |
| **3. Python and LSP** | `m0-python` | `pyproject.toml` (scikit-build-core + nanobind 3), `python/astra/` with `_core` exposing `core_api_version()` and the loader, `nanobind_add_stub` `.pyi`, `pyrightconfig.json` on `.venv`, `ruff` config, `tests/scenarios/` pytest scaffold, `.clangd`; `just check` gains `ruff`, `pyright`, `pytest`. | `just check` exits 0; clangd resolves `astra::PluginDescriptor` and pyright resolves `astra._core` via the LSP tools |
| **4. Process wiring** | `m0-process` | Stop hook running the fast subset (`just check-fast` = build + unit tests); `/task`, `/wrap`, `/retro` skills under `.claude/skills/`; `required_status_checks: [check]` added to `.github/rulesets/main.json` (Joe applies with `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/23269666`); tick M0 in `PLAN.md`; rewrite `STATE.md`. | Stop hook blocks a turn when a unit test is broken (demonstrated once, then reverted); ruleset shows `check` required; every §16 M0 row satisfied |

PRs 2–4 each start a fresh session (`claude --worktree <name>`), per the workflow. PR 2 runs at `xhigh`
(core loader is under `core/`); PR 3 and 4 are mechanical and may run at `sonnet`/`high`.

## PR 1 — steps (this session, branch `m0`)

### 1. Commit the bootstrap files that already exist
Stage and commit as-is (they match the workflow doc's decisions): `CLAUDE.md`, `.claude/settings.json`
(already `"model": "fable", "effortLevel": "high"`), `.claude/hooks/no-main.sh`, `.github/rulesets/main.json`,
`docs/lessons.md`, and the edits to `README.md` and `docs/process/workflow.md`. Add a `.gitignore`
(`build/`, `.venv/`, `compile_commands.json`, `__pycache__/`, `.claude/worktrees/`, `.claude/touch-goldens`,
`*.h5`, `.cache/`). Check `wc -l CLAUDE.md` stays under 100.

### 2. Write ADR-0001 … ADR-0011 in `docs/decisions/`
One file each, `NNNN-<kebab-title>.md`, one screen, this shape (matches workflow §"Files that carry state"):

```
# ADR-NNNN — <title>
Status: accepted, 2026-09-14. Elaborates ADR-0000 row "<Area>"; architecture.md §<n>.
## Context
## Decision
## Rejected alternatives   (every alternative from architecture §17, each with the reason it lost)
## Consequences            (what code must do / tests that pin it)
```

Titles and the §17 rejected lists, taken verbatim from `docs/architecture.md` §17 (content sourced from
the cited sections; nothing invented):

| # | File | Decision source | Rejected (must all appear) |
|---|---|---|---|
| 0001 | `0001-frames-and-attitude.md` | §6 Frames/Attitude | ENU nav; JPL quaternion; scalar-last; Euler as state |
| 0002 | `0002-si-units-and-signal-metadata.md` | §6 Units, §9 | mp-units; imperial internally; per-signal unit types |
| 0003 | `0003-state-vector-layout.md` | §6 State vector | ECEF position for all Earth models; mass as state |
| 0004 | `0004-fixed-step-rk4-in-house.md` | §8 | Boost.Odeint in main loop; RK45 adaptive; symplectic; 500 Hz default |
| 0005 | `0005-hdf5-c-api-logging.md` | §9 Logger | HighFive; Parquet; CSV |
| 0006 | `0006-icd-versioned-c11-structs.md` | §10 | Generated headers from a schema; protobuf/flatbuffers |
| 0007 | `0007-shared-memory-lockstep-futex.md` | §10 Transport | pthread condvars; named semaphores; iceoryx2; UDP loopback |
| 0008 | `0008-monorepo-embedded-separate-cmake.md` | §4 end, §11 | Separate repo from day one |
| 0009 | `0009-plugin-architecture.md` | §4 | Static factory in core; header-only/template models; importing prebuilt `.so`; single by-kind tree; a `vehicle/` kind |
| 0010 | `0010-integer-frame-counter-time.md` | §5 Clock | Accumulated double seconds; integer nanoseconds |
| 0011 | `0011-one-vehicle-as-list-of-one.md` | §7 Vehicle | Single hard-wired vehicle; full multi-vehicle now |

Consequences sections name the concrete test each ADR implies where §16 gives one (e.g. 0001: sign-pinning
test in M1; 0006: static asserts under clang-18 and `gcc -std=c11` in M0; 0009: loader test in M0; 0010:
log timestamps exact). Commit: "ADR-0001…0011 from architecture §17".

### 3. Seed `PLAN.md` and `STATE.md`
`PLAN.md`: one screen, the six milestones from workflow §Milestones as checkboxes with the §16 acceptance
line each; M0 sub-boxes for the four PRs above. `STATE.md` (≤ 40 lines): in flight = M0 PR 1, last verified
result, next step = PR 2 with the apt-install precondition, honesty rule line.

### 4. Install the oracle-guard hook (only now, ADRs exist)
`.claude/hooks/guard-oracle.sh`, registered in `.claude/settings.json` as `PreToolUse` on matcher
`Edit|Write|MultiEdit|NotebookEdit` (path from `tool_input.file_path`) and on `Bash` (deny when the command
mentions a guarded path and contains a write verb: `>`, `>>`, `tee`, `sed -i`, `rm`, `mv`, `cp`, `touch`,
`truncate`). Guarded: `docs/decisions/**`, `tests/golden/**`, any file whose name matches `*tolerance*`.
Override when `ASTRA_TOUCH_GOLDENS=1` is in the environment or the marker `.claude/touch-goldens` exists
(gitignored; created by `/task --touch-goldens`, removed by `/wrap`, both in PR 4). Exit 2 with a message
naming the rule. Same JSON-on-stdin style as `no-main.sh`. Because hooks are snapshotted at session start,
adding it now does not affect this session, which is the intended "after the ADRs exist" ordering.
Add a line to `CLAUDE.md` §Git naming the hook and the override.

Test by piping JSON into the script (no Claude restart needed):
```
echo '{"tool_name":"Edit","tool_input":{"file_path":"docs/decisions/0001-x.md"}}' | .claude/hooks/guard-oracle.sh; echo $?   # 2
echo '{"tool_name":"Edit","tool_input":{"file_path":"core/x.cpp"}}' | .claude/hooks/guard-oracle.sh; echo $?              # 0
ASTRA_TOUCH_GOLDENS=1 … same first case … ; echo $?                                                                    # 0
```

### 5. Push and open the PR
`git push -u origin m0`; `gh pr create` titled "M0 PR 1: bootstrap files, ADR-0001…0011, oracle guard" with
the restatement paragraph and the four-PR split in the body. Joe squash-merges.

## Verification for PR 1
- All eleven ADR files exist, each has the four sections and every §17 rejected alternative.
- `wc -l CLAUDE.md` < 100.
- Guard hook: the three piped-JSON cases above give 2 / 0 / 0.
- `git status` clean on `m0`; PR open against `main`; ruleset blocks direct push (already active).
- Nothing under `tests/golden/`, tolerance files, or `docs/decisions/0000-*` was edited.
