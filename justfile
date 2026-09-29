# ASTRA task runner. `just check` is the gate and the definition of done (docs/process/workflow.md).
set shell := ["bash", "-euo", "pipefail", "-c"]

# A sourced ROS setup.bash leaks PYTHONPATH into any venv (docs/lessons.md); the gate is immune to it.
export PYTHONPATH := ""

jobs := num_cpus()
venv := ".venv"
py := venv / "bin/python"

default: check

# The whole gate. Cheap, build-independent checks (formatting, docs/link validation, hook regression
# cases, Python lint) run first so a trivial mistake fails fast instead of after tens of minutes of
# building every preset four times; every preset then builds and tests; the Python package builds and
# installs into the venv; then clang-tidy, pyright (strict on python/astra/), and pytest, each after the
# build its own analysis needs. Same checks, same failure propagation -- only the order changed.
check: format-check docs-check embedded-standalone hooks-test ruff (test "debug") (test "release") (test "static-plugins") (test "asan") (wallclock-scan "debug") (wallclock-scan "release") (wallclock-scan "static-plugins") (wallclock-scan "asan") python link-db tidy pyright pytest
    @echo "check: OK"

configure preset="asan":
    cmake --preset {{preset}}

build preset="asan": (configure preset)
    cmake --build --preset {{preset}} --parallel {{jobs}}

# Single tests: `ctest --preset asan -R <name>` from the repo root, or `ctest -R <name>` from build/<preset>.
# --no-tests=error: a label or regex selector that matches nothing is a failure, not a silent pass.
test preset="asan": (build preset)
    ctest --preset {{preset}} --parallel {{jobs}} --no-tests=error

# The determinism guard's wall-clock symbol scan (docs/architecture.md §15, ADR-0010, ADR-0023): no
# core/, plugins/, aircraft/ or embedded/ object references a wall-clock, sleep or timed-wait symbol
# except the two ADR-0023 sites. Depends on the preset's build, so a fresh configure works: every
# in-scope target is a default target (none is EXCLUDE_FROM_ALL), so its objects exist afterward.
wallclock-scan preset="asan": (build preset)
    python3 scripts/wallclock_scan.py build/{{preset}}

# The Stop hook's fast subset (docs/process/workflow.md): every `unit`-labeled test, no physics oracles.
test-unit preset="asan": (build preset)
    ctest --preset {{preset}} -L unit --parallel {{jobs}} --no-tests=error

# The venv the gate uses. Created here when missing (a fresh worktree or CI has none) and filled from the pinned
# dev group in pyproject.toml. `pip install --group` needs pip 25.1; a venv's bundled pip (24.0) is upgraded
# once, and both installs are offline once satisfied.
venv:
    [ -x {{py}} ] || python3 -m venv {{venv}}
    {{py}} -m pip install -q 'pip>=25.1'
    {{py}} -m pip install -q --group dev

# Editable install of astra._core into the venv: scikit-build-core configures build/python (Release, clang-18,
# no sanitizers), builds the module against libastra_core, and writes python/astra/_core.pyi for pyright.
# A change to cmake.define in pyproject.toml needs `rm -rf build/python` first; `just clean` covers it.
python: venv
    {{py}} -m pip install -q --no-build-isolation --no-deps -e .

# clangd reads the compile_commands.json nearest to each file: the root link (gate preset) for the C++ tree,
# python/compile_commands.json (the scikit-build-core build) for the nanobind module.
link-db preset="asan":
    ln -sfn build/{{preset}}/compile_commands.json compile_commands.json
    ln -sfn ../build/python/compile_commands.json python/compile_commands.json

# The C++ tree from the gate preset's database; the binding TU from the Python build's, restricted to our file so
# nanobind's own sources are not judged by our checks.
tidy: (configure "asan") python
    run-clang-tidy-18 -clang-tidy-binary clang-tidy-18 -p build/asan -quiet -j {{jobs}}
    run-clang-tidy-18 -clang-tidy-binary clang-tidy-18 -p build/python -quiet -j {{jobs}} '/python/bindings/'

format-check:
    find . \( -path ./build -o -path ./.git -o -path ./.claude -o -path ./.venv \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) -print0 \
        | xargs -0 clang-format-18 --dry-run --Werror

format: venv
    find . \( -path ./build -o -path ./.git -o -path ./.claude -o -path ./.venv \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) -print0 \
        | xargs -0 clang-format-18 -i
    {{venv}}/bin/ruff format .

ruff: venv
    {{venv}}/bin/ruff check .
    {{venv}}/bin/ruff format --check .

pyright: python
    {{venv}}/bin/pyright

# Single tests: `.venv/bin/pytest tests/python -k <name>`.
pytest: python
    {{venv}}/bin/pytest -q

# ADR-0008: embedded/ configures and builds with nothing but a C++17 compiler. No preset, no find_package of
# Eigen/yaml-cpp/GoogleTest, no Python: `cmake -S embedded` on its own, once per compiler (clang++-18, the
# tree's, and g++, standing in for an older embedded toolchain), each in its own build directory. Then the
# autopilot must not depend on astra_core (the same binary check the echo peer gets) and must refuse an empty
# command line. Cheap, build-independent of the presets, so it sits early in `check`.
embedded-standalone:
    #!/usr/bin/env bash
    set -euo pipefail
    for cxx in clang++-18 g++; do
        dir="build/embedded-standalone/${cxx}"
        rm -rf "${dir}"
        cmake -S embedded -B "${dir}" -DCMAKE_CXX_COMPILER="${cxx}" -DCMAKE_BUILD_TYPE=Release
        if grep -Eiq 'eigen|yaml|gtest|python' "${dir}/CMakeCache.txt"; then
            echo "embedded-standalone: ${cxx} configure pulled in a dependency beyond a C++17 compiler" >&2
            exit 1
        fi
        cmake --build "${dir}" --parallel {{jobs}}
        cmake "-DBINARY=${dir}/autopilot/astra_autopilot" -P tests/lockstep/check_no_astra_core.cmake
        status=0
        "${dir}/autopilot/astra_autopilot" 2>/dev/null || status=$?
        if [ "${status}" -ne 1 ]; then
            echo "embedded-standalone: ${cxx} astra_autopilot with no arguments exited ${status}, not 1" >&2
            exit 1
        fi
    done
    echo "embedded-standalone: OK"

# Every .claude/hooks/tests/*.cases line against its hook, so "hooks fire" is verified by the gate.
hooks-test:
    .claude/hooks/tests/run.sh

# The workflow constraints docs/process/validation.md prescribes for a workflow edit (Markdown
# links/anchors, SKILL.md metadata, the AGENTS.md+CLAUDE.md line budget, doc/script command names,
# shell syntax, working-tree whitespace), made reproducible instead of asked of an agent's memory.
docs-check:
    python3 scripts/docs_check.py
    bash -n scripts/check-fast.sh
    bash -n scripts/check-full.sh
    git diff --check

clean:
    rm -rf build compile_commands.json python/compile_commands.json python/astra/_core.pyi

# `cp -r aircraft/_template aircraft/<name>` plus fixing the copied configs' plugin paths
# (docs/architecture.md §18, PLAN.md M3 PR 1). scripts/new-aircraft.sh resolves its own repo root
# by location, not `git rev-parse`, so this writes into whichever worktree invoked it. quote(name)
# (not a bare {{name}}) is required: `just` substitutes recipe parameters into the command text
# before bash ever parses it, so an unquoted name like '$(cmd)x' would have `cmd` executed by this
# recipe's own shell before new-aircraft.sh's own validation ever runs (Codex adversarial review,
# PLAN.md M3 PR 1). quote() single-quote-escapes the value so it reaches the script as inert text.
new-aircraft name:
    ./scripts/new-aircraft.sh {{quote(name)}}
