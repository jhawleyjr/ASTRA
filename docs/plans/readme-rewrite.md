# README rewrite (Tier 0)

Status: approved by Joe 2026-09-30 in plan mode, with one change (the add-an-aircraft check below).

## Context

`README.md` is mostly notes on protecting `main` and on the workflow. An engineer arriving at the repo cannot
tell from it what ASTRA does, what works today, or how to build and run it. It also calls ASTRA a framework "for
commercial transport aircraft", which ADR-0019 and architecture §1 contradict. `docs/README.md` says `embedded/`
and `aircraft/` are not implemented, but both have existed since M3 and M4.

Tier 0: prose only. Approved by Joe 2026-09-30. Base `origin/main` `367a84a`, branch `worktree-readme`, own
worktree, draft PR at the first commit.

Files changed: `README.md` (rewritten), `docs/README.md` (stale lines fixed), `docs/process/workflow.md` (the
ruleset commands moved in), this plan (renamed), and at `/wrap` `STATE.md` and one PLAN.md line. Not touched:
AGENTS.md, CLAUDE.md, `docs/architecture.md`, `docs/decisions/`, `tests/golden/**`, `docs/lessons.md`, code.

## (a) README outline

Every claim below was checked against the tree on `367a84a`, not only PLAN.md. Where architecture.md and the
code differ (its §4 lists a `tools/` directory and `plugins/hardware/`, neither built; §12 and §15 still mark M6
PRs 2 to 4 *planned*, per PLAN.md's M6 open item), the README follows the code.

**Title.** `# ASTRA — Aerospace Simulation, Test, and Research Architecture` (kept).

**1. What ASTRA is** (one paragraph). A vehicle-agnostic 6-DoF simulation framework in C++23 with Python
bindings (ADR-0019). Its focus is aircraft and atmospheric flight. Its purpose is to run real embedded flight
software against the simulation over a lockstep transport: all-digital today, hardware in the loop later. The
framework is the product. `aircraft/notional_transport` is the worked example. The F-16 joins in M7, with the
NESC atmospheric check cases as its oracle. The GNC stack is the project's own, with no third-party flight
software in the loop. Source: future-work.md "Direction", architecture §1.

**2. Features** (on `main` today). One bullet per group, each linking its doc, ADR or module:
- *6-DoF core*: `RigidBody6DofDynamics` and the Stevens & Lewis body-axis EOM; in-house fixed-step RK4 at 1 kHz,
  with Euler kept for tests (ADR-0004); wrenches evaluated at each RK4 sub-stage (ADR-0016); models may own
  continuous state (ADR-0021); a per-frame path with no allocation, under test (`tests/physics/no_alloc_test.cpp`).
  The quaternion is renormalized after every step.
- *Conventions*: FRD body and NED navigation frames, `q_nb` Hamilton scalar-first (ADR-0001); SI inside, with
  conversions at the boundaries in `core/util/units` (ADR-0002); integer frame counter time (ADR-0010).
- *Environment plugins*: `atmosphere/isa1976`; `wind/{uniform, dryden, table}` (Dryden per MIL-F-8785C,
  seeded); `gravity/{constant, inverse_square, wgs84_j2}`; `earth/{flat, round, wgs84_rotating}`;
  `mass/constant`. Links `plugins/physical/`.
- *Plugins and the aircraft template*: one `.so` per selectable model, loaded by path at init and checked
  against `core_api_version` and the toolchain (ADR-0009); a static-plugins build; the one-line
  `astra_add_plugin`; `aircraft/_template` and `just new-aircraft`, whose copy builds and runs its own tests.
- *Notional transport*: linear-coefficient aero, constant mass and a thrust-lapse turbofan, from JSBSim's 737
  example (notional, not a validated 737); cruise configs in still air and light and moderate Dryden turbulence.
- *Trim and linearization*: `astra.trim` (straight and level, `scipy.optimize.root` on `Simulation.derivative`)
  and `astra.linearize` (central-difference A and B, with short period, phugoid, Dutch roll, roll and spiral).
- *Lockstep and the embedded autopilot*: ICD v1 C11 structs (ADR-0006); the shared-memory futex transport and
  HAL (ADR-0007, ADR-0022 to ADR-0024); `embedded/autopilot`, a C++17 straight-and-level autopilot (altitude,
  airspeed and heading hold) that links only `icd/` and `embedded/hal/` and runs as its own process
  (ADR-0008); `astra.lockstep` drives it. The loopback transport serves the tests.
- *HDF5 run logs and replay*: an optional `log` block; logs are byte-identical across runs and published only on
  a clean close (ADR-0025, ADR-0027); `astra.log.load` reads them into pandas and `astra.log.replay`
  re-executes a run and compares the bytes.
- *Scenarios*: `astra.scenario`; every YAML file in `tests/scenarios/` is a pytest item.
- *Monte Carlo*: `astra.montecarlo` with dispersions, per-run seeds, one log per run and `summary.parquet`.
- *FlightGear bridge*: `astra.fgbridge` sends FGNetFDM v24 over UDP from a live run or a log.
- *The `astra` CLI*: `astra run` (exit codes per architecture §15; it refuses a `transport` block until M10)
  and `astra trim`.
- *Health monitoring*: NaN/Inf in state, wrench, input and environment; quaternion drift; per-frame domain
  validity (ADR-0012), each `off`/`warn`/`halt`; terminal transport and log halts, named in the log. §15's
  envelope, rate and real-time checks are not built yet (M7, M11).
- *Verification*: NESC atmospheric check cases 1 to 10 (`tests/physics/nesc/`, ADR-0013 to ADR-0015), ISA
  table points, Dryden PSD, torque-free conservation, and the convergence order of RK4 against Euler.

**3. Quick start.**
- Toolchain: Ubuntu 24.04 and apt only (clang-18, CMake 3.28+, Ninja, just, Eigen 3.4, HDF5 C API, yaml-cpp,
  GoogleTest; Python 3.12+). No vcpkg or Conan.
- Commands: `./scripts/setup-ubuntu.sh` (once, sudo); `just build` (asan preset by default; `just build
  release`); `./scripts/check-fast.sh`; `./scripts/check-full.sh` (tens of minutes; exit 0 with `check: OK` is
  the pass condition); `just python` (creates `.venv` and installs `astra` in editable mode); `source
  .venv/bin/activate`; then
  `astra trim aircraft/notional_transport/configs/cruise.yaml` and
  `astra run aircraft/notional_transport/configs/cruise.yaml --log build/cruise.h5`. Note that the log path must
  not already exist.

**4. How-tos.** Each is a few lines, one command or snippet, and a link. No new guide files.
- *Run from a config*: `astra run <config> [--strict] [--log PATH]`, plus the Python equivalent
  (`Config.from_file`, `Simulation.from_config(config, default_plugin_dir())`, `run()`, `close_log()`). Link
  architecture §13 for config keys and `python/astra/cli.py` for exit codes.
- *Trim and linearize*: a snippet with `trim_straight_and_level(path, 230.0, 10000.0)`, then `linearize(...)`
  and `modes(result.a)`. Links §12, `trim.py`, `linearize.py`.
- *Log, load and replay*: `log.load(path)` and `RunLog.rates[1]`, then `log.replay(path, out)`. Links ADR-0025
  and ADR-0027.
- *A scenario*: the short `tests/scenarios/elevator_step.yaml` shape, plus `.venv/bin/pytest tests/scenarios`.
  Links `scenario.py`'s schema docstring.
- *A Monte Carlo batch*: a small inline spec (4 runs, 5 s, one dispersion), then `montecarlo.run(spec, out,
  workers=4)`. Links `tests/python/montecarlo_fixtures/cruise_64.yaml` and `montecarlo.py`.
- *Stream to FlightGear*: `fgfs --fdm=null --native-fdm=socket,in,62.5,,5500,udp` and `fgbridge.send_log(path)`.
  Flat Earth is refused.
- *Run lockstep with the embedded autopilot*: a snippet with `ls.Setpoints`, `ls.trim_for`, `ls.load_config(...,
  segment=ls.new_segment_name(), frames=10000)`, `ls.autopilot_argv(...)` and `ls.run_closed_loop(...)` on
  `cruise_light_turbulence.yaml`, with the binary at `build/python/embedded/autopilot/astra_autopilot`. Links
  §10, §11 and `lockstep.py`.
- *Add an aircraft*: §18's steps (`just new-aircraft my_aircraft`, edit the models, `just build`, `astra trim`).
  The template's config uses `earth/flat`, which `astra trim` refuses (PLAN.md's open M7 item). What the README
  says depends on the check below (Joe's change): if `astra trim` succeeds on the copy after the workaround, the
  README lists exactly the edits it needed; if not, it prescribes no workaround, states the limitation and
  links PLAN.md's open M7 item.
- *Write a plugin*: subclass an interface in `core/include/astra/interfaces/`, export one
  `PluginDescriptor` through `ASTRA_PLUGIN_EXPORT astra_plugin()`, add `astra_add_plugin(SOURCES ...)` in the
  directory's `CMakeLists.txt`, and name the path in a config. The example is
  `plugins/physical/atmosphere/isa1976/`. Also: per-frame rules and `astra_add_plugin_test()` for its tests.

**5. Architecture at a glance.** The per-frame order (environment → derived signals → transport exchange → model
steps → input latch → RK4 with per-stage wrenches → post-step → logger sample). The plugin model: two roots,
kinds, and loading by path. Conventions that trip people up, in a short table: frames, quaternion, state layout,
SI and no unit suffixes, time `t = frame × dt`, all rates integer divisors of 1 kHz, seeds from config. Links:
architecture.md, `docs/decisions/` and ADR-0000 (the index of decisions made before code existed, and the open
questions deliberately deferred).

**6. Repository layout.** A table covering `core/`, `plugins/`, `aircraft/`, `icd/`, `embedded/`, `python/`,
`tests/`, `scripts/`, `cmake/`, `docs/`, `.claude/` and `.github/`, plus the root `justfile`, `CMakePresets.json`
and `pyproject.toml`.

**7. Verification.** Oracles are human-owned: goldens, tolerances and NESC bands (ADR-0013). Fast and full checks.
The full gate builds and tests four presets (debug, release, static-plugins, asan) and runs the wall-clock scan,
Python, formatting, static analysis, hook and docs checks; the justfile is the source of truth. CI runs
`GATE=local` to save Actions minutes, so a green `check` is not evidence that the full gate ran. Links
`docs/process/validation.md`.

**8. Roadmap.** "M0 to M5 are done" (bootstrap, rigid body, environment, notional transport, lockstep,
tooling), then one line each for M6 to M11 from PLAN.md, plus "Later". No per-PR status. Links PLAN.md
(checklist; architecture §16 holds each milestone's acceptance) and `docs/future-work.md`.

**9. Development.** Claude plans, builds, validates, fixes and integrates; Codex reviews independently. Joe is
the sole maintainer, and his explicit decision or squash-merge is the approval record. Task branches and
worktrees; PRs start as drafts; risk tiers. No release pipeline yet. STATE.md is the session handoff. Links
AGENTS.md, workflow.md, `docs/README.md` (the documentation index) and `docs/templates/`.

**10. References.** `docs/refs/` holds reference documents placed by hand (MIL-F-8785C, the NESC reports and
trajectories, FlightGear sources). They are gitignored: cited by page, never committed. Read them from the main
checkout. Links `docs/refs/README.md`. Nothing about a license.

## (b) Content map

### README.md

| # | Statement | Disposition |
|---|---|---|
| R1 | Title "ASTRA — Aerospace Simulation, Test, and Research Architecture" | kept (title) |
| R2 | "A vehicle-agnostic six-degree-of-freedom simulation framework" | kept (§1) |
| R3 | "... for commercial transport aircraft" | dropped: contradicted by ADR-0019 and architecture §1 ("not a claim of commercial-transport focus"); §1 now follows future-work.md Direction |
| R4 | "built to run the aircraft's real embedded software against the sim — all-digital first, hardware-in-the-loop later" | kept (§1) |
| R5 | "Status: See PLAN.md for milestones and STATE.md for the latest session handoff" | kept (Roadmap: PLAN.md; Development: STATE.md) |
| R6 | "The documentation index links design, workflow, and reusable templates" | kept (Development links `docs/README.md`) |
| R7 | architecture.md: "The accepted design. §16 has the acceptance criteria per milestone" | kept (Architecture; Roadmap names §16) |
| R8 | architecture.md: "§17 the ADRs to write first" | dropped: an M0 instruction that is complete (PLAN.md M0, "ADR-0001…0011 accepted"); `docs/decisions/` is the index, linked from Architecture |
| R9 | workflow.md: "Risk-based builder/reviewer workflow, local validation, Joe's approval and merge/release boundaries" | kept (Development) |
| R10 | templates: "Optional specs, plans, work packages, reviews, human packets, and ADR proposals" | kept (Development link); also covered at docs/README.md: "[templates/](templates/) \| Copy only the artifacts the change needs" |
| R11 | ADR-0000: "The index of every decision made before code existed, and the open questions deliberately deferred" | kept (Architecture) |
| R12 | "Protecting `main`: Two layers, both required" | already covered at workflow.md#human-approval-and-mergerelease-gates: "**What actually enforces this — four different things, not one:**" |
| R13 | `gh api -X POST repos/jhawleyjr/ASTRA/rulesets --input .github/rulesets/main.json`, run once | moved to workflow.md#human-approval-and-mergerelease-gates, "Checked-in host configuration" bullet |
| R14 | Ruleset effects: PR required for every change, squash only, deletion and force-push blocked, `check` required on an up-to-date branch, "so a red PR can't merge" | already covered at the same section: "Deletion and non-fast-forward pushes are rejected, only squash-merge is an allowed method, and the `check` status must pass on an up-to-date branch"; "requires a pull request for every change to `main`" moved into the same bullet |
| R15 | "The file is the ruleset: edit it, then re-apply with `gh api -X PUT .../rulesets/<id>` (`gh api repos/jhawleyjr/ASTRA/rulesets` lists the id)" | moved to the same bullet (the PUT form is also in `scripts/setup-ubuntu.sh`'s closing message) |
| R16 | `no-main.sh`: a PreToolUse hook on Bash, installed by `.claude/settings.json`, blocking a `git push` to `main` and `commit`/`merge`/`rebase` on `main`; Claude moves to a branch | already covered at the same section: "*Local hook coverage*: `.claude/hooks/no-main.sh`, a PreToolUse hook on this session's own Bash tool — recognizes a literal `git push`/`commit`/`merge`/`rebase`/... touching `main`" |
| R17 | `stop-check.sh`: `just test-unit` runs when a turn ends, the first failed stop is blocked, a continued turn may report a blocker, and a completed turn is not a passing gate | already covered at workflow.md#review-fix-limits-and-failure-routing: "Claude's Stop hook blocks the first failed stop attempt, then allows an honest handoff on `stop_hook_active` continuation without rechecking. Its exit 0 is not validation evidence"; validation.md#evidence: "The Stop hook's first attempt runs the fast subset" |
| R18 | "Both are in the repo for Claude Code sessions; other agents follow the same boundaries in AGENTS.md" | already covered at AGENTS.md: "never commit/merge/rebase on `main` or push to it" |
| R19 | "New Claude Code tasks start with `--worktree <name>`; other agents use one task branch/worktree per writer" | kept (Development, brief); covered at workflow.md#git-and-worktrees: "Start new Claude tasks with `claude --worktree <name>`; generic agents may use the equivalent" |
| R20 | "Claude plans, implements, validates, fixes and integrates; Codex reviews and adversarially reviews" | kept (Development) |
| R21 | "Preserve an explicitly assigned branch" | already covered at AGENTS.md: "Preserve an explicitly assigned branch." |
| R22 | "See the workflow for setup and integration conventions" | kept (Development link) |
| R23 | "Joe is the sole maintainer: his explicit decision or squash-merge is the human approval record" | kept (Development) |
| R24 | "The ruleset intentionally does not require a separate GitHub approving-review badge" | already covered at workflow.md#human-approval-and-mergerelease-gates: "no separate GitHub approving-review badge or second human is required" |
| R25 | "Ubuntu 24.04, apt-only toolchain: provision once with `./scripts/setup-ubuntu.sh`" | kept (Quick start) |
| R26 | "Use check-fast while iterating and check-full before final review" | kept (Quick start, Verification) |
| R27 | "These wrap `just test-unit` and `just check`; the justfile remains the command source of truth" | kept (Verification) |
| R28 | "The full gate builds/tests all four presets and runs Python, formatting, static-analysis and hook checks" | kept (Verification) |
| R29 | "See validation coverage and evidence for details" | kept (Verification link) |
| R30 | "CI intentionally uses GATE=local ...; a green CI `check` is not evidence that the full gate ran" | kept (Verification) |
| R31 | "PRs start as drafts, and the workflow explains when to mark ready and rerun checks after later pushes" | kept (Development) |
| R32 | "No release pipeline is configured yet" | kept (Development) |

### docs/README.md

| # | Statement | Disposition |
|---|---|---|
| D1 | "Start with AGENTS.md and the development workflow" | kept |
| D2 to D11 | The table rows (architecture, decisions, PLAN/STATE, future-work, specs/plans/tasks, reviews, templates, agents, validation, lessons/refs) | kept; one row added for `../README.md` (overview, quick start, how-tos) |
| D12 | "Link to authoritative material instead of copying it" | kept |
| D13 | "The architecture describes some future paths (such as `embedded/` and `aircraft/`) that are not implemented yet; inspect the tree before planning" | fixed (stale): both exist. Rewritten as: the architecture still names paths not built (`plugins/hardware/`, `plugins/sim/transport/fpga/`, `tools/`) and marks some M6 features *planned*; inspect the tree before planning |

### docs/process/workflow.md

One edit, to the "*Checked-in host configuration*" bullet: the ruleset requires a pull request for every change
to `main`, is applied once with the POST command, and is re-applied after an edit with the PUT command (the list
command finds the id). The bullet's existing note that a branch-protection change needs Joe's authorization is
kept. No other wording changes.

## Execution

1. Rename this draft to `docs/plans/readme-rewrite.md` and commit it, then push and open the draft PR.
2. Write README.md, docs/README.md and the workflow.md edit, and commit as I go.
3. Build and run (below), then fix the README wherever a command or snippet disagrees with what it actually does.
4. `python3 scripts/docs_check.py`, `git diff --check`, and one Codex pass on the final diff (via `/review-cycle`)
   asked to check every README claim against the code. A finding that is not a factual error becomes a
   follow-up (Tier 0).
5. PR body per `docs/templates/pr-description.md`: the content map; each command and snippet with its exit
   status; Codex's verdict and thread id; correction rounds n/2. Under "Needs Joe's decision": the repo has no
   LICENSE file, and the notional transport's parameters come from JSBSim's GPL 737 file (architecture §13).
   Also listed there: guard refusals, if any.
6. `/wrap`: STATE.md and this PR's PLAN.md line.

## Verification

Run once in this worktree, in README order, each exit status recorded in the PR body (not the README):
- `./scripts/setup-ubuntu.sh`: no passwordless sudo here (`sudo -n true` fails), so not run. Recorded as such,
  with the installed toolchain versions it would print instead.
- `just build`, `./scripts/check-fast.sh`, `just python`, `astra trim ...`, `astra run ... --log build/cruise.h5`.
- `./scripts/check-full.sh`, run once because the README tells readers to run it. This is a command check, not
  the Tier 0 gate: no evidence log is committed, and its exit status and `check: OK` go in the PR body.
- Every Python snippet as a scratch script under the session scratchpad, run from the repo root with the venv:
  run-from-config, trim and linearize, log load and replay, the scenario (plus `.venv/bin/pytest
  tests/scenarios`), the small Monte Carlo batch, `fgbridge.send_log` (UDP to localhost with no listener; `fgfs`
  is not installed, so FlightGear itself is not run, and that is recorded), and the lockstep snippet.
- `just new-aircraft my_aircraft` and `just build`, then (Joe's change) `astra trim` on the copy's
  `configs/cruise.yaml` with the workaround applied: Earth switched to `wgs84_rotating` and whatever else that
  needs (the position then reads as lat, lon, altitude). Each edit and the exit status go in the PR body. Then
  delete `aircraft/my_aircraft/` (never committed).
- Every link and every `just`/`./scripts/` name is checked by `python3 scripts/docs_check.py`.
