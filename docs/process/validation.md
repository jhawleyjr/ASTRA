# Validation entry points

The [justfile](../../justfile) owns commands, flags, presets, and dependencies. The new scripts only
locate the repository from their own path, reject arguments, and `exec just`; they preserve failure
exit codes. Invoke them from any working directory by their relative or absolute path.

| Command | Existing coverage |
|---|---|
| `./scripts/check-fast.sh` | `just test-unit`: configure/build `asan`, CTest label `unit`, fail if no tests match |
| `./scripts/check-full.sh` | `just check`: format check; docs check; `embedded-standalone`; hook cases; ruff; debug, release, static-plugins, asan build/tests; the wall-clock symbol scan; Python editable install; compile database links; clang-tidy; strict pyright; pytest |
| `just build [preset]` / `just test [preset]` | One CMake preset, default asan |
| `just wallclock-scan [preset]` | Builds the preset, then `scripts/wallclock_scan.py` fails on any wall-clock, sleep or timed-wait symbol in a `core/`, `plugins/`, `aircraft/` or `embedded/` object outside ADR-0023's two sites; `just check` runs it for all four presets |
| `ctest --preset asan -R <name> --no-tests=error` | Selected existing CTest cases after building |
| `.venv/bin/pytest tests/python -k <name>` | Selected Python tests after `just python` |
| `.venv/bin/pytest tests/scenarios` | Every scenario YAML (Release), one item each, run by `astra.scenario`; an invalid scenario is a collection error, and the run exits 2 |
| `.venv/bin/pytest tests/python -k montecarlo` | `astra.montecarlo` (Release): the 64-run Q19 case (`tests/python/montecarlo_fixtures/cruise_64.yaml`; about 2.1 GB of logs under pytest's temporary directory, removed after the test), determinism across worker counts, the sampler, replay, and the failure paths (killed worker, interrupt, error rows) |
| `.venv/bin/pytest tests/python -k fgbridge` | `astra.fgbridge` (Release): FGNetFDM v24 packets over a real localhost UDP socket, decoded with the transcribed table `tests/golden/flightgear/fgnetfdm_v24.py` alone; live and log sources byte-identical; the refusals (flat Earth, bad divisor or address, missing derived signals, frames stepped outside the bridge) |
| `python3 scripts/fgnetfdm_layout_check.py` | By hand, never in `just check`: compiles FlightGear's `net_fdm.hxx` (main checkout's `docs/refs/`) with g++ and compares sizeof, alignof and every offsetof with the table; checks both reference files' SHA-256 |
| `just embedded-standalone` | `embedded/` alone (`cmake -S embedded`, no preset, no Eigen/yaml-cpp/GoogleTest/Python) with clang++-18 and g++, then the autopilot's no-`astra_core` binary check and a no-argument exit-1 smoke run (ADR-0008) |
| `.venv/bin/pytest tests/python/test_lockstep.py` | M4 acceptance at full length (Release): R1 to R4b, R12b, the driver's lifecycle. Every run is a child process with a hard timeout; pytest is a child subreaper for the module. Set `ASTRA_LOCKSTEP_REPORT_DIR` to keep each run's `summary.json`, `trace.csv` and recorder; set `ASTRA_AUTOPILOT` / `ASTRA_ECHO_PEER` to use other binaries than `build/python`'s |
| `ctest --preset asan -R ClosedLoopAcceptance --no-tests=error` | The closed loop in C++: the limits-equality test, 1,000 exchanges and a same-process R3 pair on every preset; the full 10,000 exchanges (label `acceptance`) on asan only |
| `just hooks-test` | Existing oracle/main/Stop-hook regression cases |
| `just tidy` / `just format-check` | Static analysis / read-only C++ formatting check |
| `just format` | Mutating C++/Python formatting; inspect the resulting diff |

Fast excludes physics, Python, formatting, hooks and static analysis. Full includes every registered
physics test under every preset, including allocation, deterministic/stochastic and analytic/reference
oracles already in the tree. Future milestone checks in architecture/PLAN are not automatically present.
There is no standalone benchmark, fuzz, security scan or release job today. Add a targeted check only
when required by a task; do not claim these categories are covered by a green full gate.

## Environment and runtime

Use the existing [Ubuntu setup](../../scripts/setup-ubuntu.sh) for provisioning, not as part of each
check. It installs system packages and requires sudo. CMake presets use Ninja/clang-18; C++23 core,
C11 ICD, and the separate embedded example (`embedded/`, ADR-0008) is C++17. Python requires 3.12+; `just venv` installs
the dev group and `just python` builds nanobind/scikit-build-core editable bindings. Initial setup may
need network access. The justfile clears `PYTHONPATH` to avoid ROS leaking into the venv.

Checks write `build/`, `.venv/`, compile-database symlinks and generated bindings/stubs. Never run two
checks against the same checkout. Keep these directories local to each worktree. `just link-db`
repairs editor database links; editor diagnostics are feedback, not validation.

Full checks can take tens of minutes because physics tests run four times. Do not silently skip them,
change the preset matrix, raise tolerances, or change host ASLR settings to obtain a pass. Report
resource/sandbox/toolchain failures separately from defects. Request only the access a check needs.

## Evidence

A simple local run (from the repository root):

```sh
mkdir -p build/validation
./scripts/check-full.sh > build/validation/full.log 2>&1
result=$?
tail -n 40 build/validation/full.log
printf 'exit=%s\n' "$result"
```

Keep the exit status even when redirecting output; a tail command's success says nothing about the
gate. In an agent tool, wait on the returned process handle until it completes. Do not background a
second wrapper or guess a PID with global `pgrep`. Only report full success after exit 0 and `check: OK`.

The full log stays outside git. Commit a trimmed log as `docs/reviews/evidence/<head>-gate-summary.log`:
a header (date, command line, base and head revisions, exit code), the summary lines (per-preset CTest
counts, pytest, pyright, hooks, the `: OK` steps, `check: OK`), and any failure output in full:

```sh
base=$(git merge-base origin/main HEAD)
head=$(git rev-parse --short HEAD)
{
  printf 'date: %s\ncommand: ./scripts/check-full.sh\nbase: %s\nhead: %s\nexit: %s\n\n' \
    "$(date -u +%FT%TZ)" "$base" "$(git rev-parse HEAD)" "$result"
  grep -E '^(Test project |[0-9]+% tests passed|[A-Za-z-]+: OK|hooks: |All checks passed|[0-9]+ errors?, |[0-9]+ passed|error: Recipe|check: OK)' \
    build/validation/full.log
} > "docs/reviews/evidence/${head}-gate-summary.log"
```

If the run failed, append the failing step's output in full to that summary. A full log is committed, as
`docs/reviews/evidence/<head>-full-gate.log`, only for a failed run or when Joe asks; that name means nothing
else. Existing evidence files keep their names. Name any uncommitted working-tree changes in the header.

The PR body carries the result: date, command, exit code, duration, toolchain, the summary counts, a link to
the summary log, and the acceptance criteria exercised. Approval and check evidence must identify the
candidate they evaluated.

For workflow edits, run `just docs-check` (`scripts/docs_check.py`): Markdown links/anchors across
`docs/process/`, `docs/agents/`, `docs/templates/`, `.claude/skills/`, the root instruction files,
`README.md`, `docs/README.md`, `docs/reviews/README.md` and `PLAN.md` (links only);
`.claude/skills/*/SKILL.md` frontmatter and name-matches-directory; the AGENTS.md+CLAUDE.md line
budget; `just`/`./scripts/` command names referenced from those docs; `bash -n` on each wrapper
separately (`bash -n a b` only parses `a`; `b` becomes an unused positional argument, not a second
file checked); and `git diff --check`. `just check` runs it on every
full gate; CI also runs it as its own step whenever a pull request touches that surface, so a
docs/skills-only PR is checked without the full toolchain (see [the workflow](workflow.md) for
`GATE=local`'s scope and host protection; wrappers do not turn local results into remote required
checks).

The Stop hook's first attempt runs the fast subset and reports failure; a hook continuation can end
with an honest blocked handoff without rechecking. This prevents repeated hook-driven retries and
is never evidence that fast/full validation passed. Claude must preserve the actual result and run
necessary checks explicitly before claiming completion.

## Numerical and physics validation

Applies to any change to `core/kernel/`, numerical `core/util/`, `plugins/physical/`, `aircraft/**`
models, the ICD, or a test/golden under `tests/physics/`, `tests/unit/`, `tests/golden/`. Required
inputs: the assigned spec/plan's acceptance criteria, the binding ADRs for the changed behavior (at
minimum ADR-0001 frames, ADR-0002 units, ADR-0003 state layout, ADR-0004 RK4, ADR-0010 time,
ADR-0012 domain validity, ADR-0013 oracle construction, as applicable), and AGENTS.md's
Non-standard conventions.

1. Search `tests/physics/`, `tests/unit/`, `tests/golden/**` and `tests/configs/` for an existing
   case covering the changed behavior, frame, unit, or domain boundary before adding one. Trace any
   golden or reference dataset to its own generator and citation (e.g.
   `tests/golden/atmosphere/generate_isa1976_table.py`, ADR-0013 for `tests/golden/nesc/`) rather
   than trusting a committed number on sight; a dataset with no traceable generator or citation is a
   gap to report, not to fill by guessing.
2. Confirm the change honors AGENTS.md's Frames, State vector, Units, Time, Randomness, Floating
   point, and Per-frame path conventions, plus what those don't already spell out:
   - **Domain/finite behavior** (ADR-0012): a per-frame check must bound the actual expression the
     model is singular at, never a sibling model's unrelated bound or a velocity-only lookahead
     alone; assert a trig/algebraic singularity by magnitude threshold, never exact non-finiteness.
   - **Invariants**: quaternion renormalization after every integrator step; conserved quantities
     (energy/momentum) where the physics implies them.
   - **RK4 coupling** (ADR-0004): a changed derivative must be correct at all four stages, not only
     evaluated once at the frame boundary.
   - **Per-frame constraints**: no heap allocation, exceptions, blocking I/O, `std::string`, or
     `dlopen` on the `step`/`wrench_b`/integrator/transport/logger path (`no_alloc_test.cpp`-style
     coverage is the existing check).
3. Never invent or relax a threshold. Accepted tolerances and their rationale live in the committed
   golden/tolerance data and the ADR that accepted it (e.g. ADR-0013's ensemble-band `k = 3`); a
   test may cite an existing tolerance but never widen, narrow, or regenerate one — the human-owned
   oracle boundary in AGENTS.md and workflow.md's Verification section.
4. Run the targeted command first (`ctest --preset asan -R <name>`), then the full gate before
   review (`./scripts/check-full.sh`); read the actual exit status and `check: OK` per this
   document's Evidence section above, never a background wrapper's status.
5. Record in the PR body: candidate scope (base/head), the commands run and their actual results
   (the summary log per the Evidence section above), each reference dataset's provenance, the tolerance applied and its source, gaps
   (untested boundary, deferred case, environment limitation), and unresolved findings.
6. Escalate to Joe instead of resolving unilaterally when the spec/ADR states no acceptance value or
   tolerance for the behavior under test, an existing tolerance or golden looks wrong or inconsistent
   with its cited source, or a new case has no independent reference — never derive a tolerance from
   ASTRA's own output.

Claude's [builder procedure](../agents/build.md) applies this to numerical/physics changes during
validation; Codex's [review procedure](../agents/review.md) step 3 uses it as the checklist behind
"FRD/NED ... RK4 coupling ... independent reference tolerances."
