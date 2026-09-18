# Validation entry points

The [justfile](../../justfile) owns commands, flags, presets, and dependencies. The new scripts only
locate the repository from their own path, reject arguments, and `exec just`; they preserve failure
exit codes. Invoke them from any working directory by their relative or absolute path.

| Command | Existing coverage |
|---|---|
| `./scripts/check-fast.sh` | `just test-unit`: configure/build `asan`, CTest label `unit`, fail if no tests match |
| `./scripts/check-full.sh` | `just check`: debug, release, static-plugins, asan build/tests; Python editable install; compile database links; clang-tidy; format dry run; hook cases; ruff; strict pyright; pytest |
| `just build [preset]` / `just test [preset]` | One CMake preset, default asan |
| `ctest --preset asan -R <name> --no-tests=error` | Selected existing CTest cases after building |
| `.venv/bin/pytest tests/python -k <name>` | Selected Python tests after `just python` |
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
C11 ICD, and the planned separate embedded example is C++17. Python requires 3.12+; `just venv` installs
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

In the PR or review evidence, record date, command, result, duration, toolchain, exact base/head,
working-tree changes if uncommitted, log location/attachment, and acceptance criteria exercised.
Ignored logs are not durable shared evidence: attach them to the PR or supply a retained artifact
before handoff. Approval and check evidence must identify the candidate they evaluated.

For workflow edits also run `bash -n scripts/check-fast.sh scripts/check-full.sh`, `git diff --check`,
and check Markdown links, command names, and the root instruction line budget. CI performs the shell
syntax check without installing extra tools. CI's `GATE=local` limitations and host protection live in
[the workflow](workflow.md); wrappers do not turn local results into remote required checks.

The Stop hook's first attempt runs the fast subset and reports failure; a hook continuation can end
with an honest blocked handoff without rechecking. This prevents repeated hook-driven retries and
is never evidence that fast/full validation passed. Claude must preserve the actual result and run
necessary checks explicitly before claiming completion.
