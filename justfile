# ASTRA task runner. `just check` is the gate and the definition of done (docs/process/workflow.md).
set shell := ["bash", "-euo", "pipefail", "-c"]

# A sourced ROS setup.bash leaks PYTHONPATH into any venv (docs/lessons.md); the gate is immune to it.
export PYTHONPATH := ""

jobs := num_cpus()

default: check

# The whole gate: every preset builds and tests; then clang-tidy, clang-format, and the hook test cases.
check: (test "debug") (test "release") (test "static-plugins") (test "asan") link-db tidy format-check hooks-test
    @echo "check: OK"

configure preset="asan":
    cmake --preset {{preset}}

build preset="asan": (configure preset)
    cmake --build --preset {{preset}} --parallel {{jobs}}

# Single tests: `ctest --preset asan -R <name>` from the repo root, or `ctest -R <name>` from build/<preset>.
test preset="asan": (build preset)
    ctest --preset {{preset}} --parallel {{jobs}}

# clangd reads compile_commands.json at the repo root; point it at the gate preset.
link-db preset="asan":
    ln -sfn build/{{preset}}/compile_commands.json compile_commands.json

tidy: (configure "asan")
    run-clang-tidy-18 -clang-tidy-binary clang-tidy-18 -p build/asan -quiet -j {{jobs}}

format-check:
    find . \( -path ./build -o -path ./.git -o -path ./.claude -o -path ./.venv \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) -print0 \
        | xargs -0 clang-format-18 --dry-run --Werror

format:
    find . \( -path ./build -o -path ./.git -o -path ./.claude -o -path ./.venv \) -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) -print0 \
        | xargs -0 clang-format-18 -i

# Every .claude/hooks/tests/*.cases line against its hook, so "hooks fire" is verified by the gate.
hooks-test:
    .claude/hooks/tests/run.sh

clean:
    rm -rf build compile_commands.json
