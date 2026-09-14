# ASTRA task runner. `just check` is the gate and the definition of done (docs/process/workflow.md).
set shell := ["bash", "-euo", "pipefail", "-c"]

# A sourced ROS setup.bash leaks PYTHONPATH into any venv (docs/lessons.md); the gate is immune to it.
export PYTHONPATH := ""

jobs := num_cpus()
venv := ".venv"
py := venv / "bin/python"

default: check

# The whole gate: every preset builds and tests; the Python package builds and installs into the venv; then
# clang-tidy, clang-format, the hook test cases, ruff, pyright (strict on python/astra/), and pytest.
check: (test "debug") (test "release") (test "static-plugins") (test "asan") python link-db tidy format-check hooks-test ruff pyright pytest
    @echo "check: OK"

configure preset="asan":
    cmake --preset {{preset}}

build preset="asan": (configure preset)
    cmake --build --preset {{preset}} --parallel {{jobs}}

# Single tests: `ctest --preset asan -R <name>` from the repo root, or `ctest -R <name>` from build/<preset>.
test preset="asan": (build preset)
    ctest --preset {{preset}} --parallel {{jobs}}

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

# Every .claude/hooks/tests/*.cases line against its hook, so "hooks fire" is verified by the gate.
hooks-test:
    .claude/hooks/tests/run.sh

clean:
    rm -rf build compile_commands.json python/compile_commands.json python/astra/_core.pyi
