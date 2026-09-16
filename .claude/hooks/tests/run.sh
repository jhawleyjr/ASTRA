#!/usr/bin/env bash
# Runs every .claude/hooks/tests/<hook>.cases file against .claude/hooks/<hook>.sh. Wired into `just check`
# (recipe hooks-test) so "hooks fire" is verified by the gate, never by inline probes.
#
# A case line is <expected exit>\t<JSON piped to the hook on stdin>; '#' lines and blank lines are skipped.
# These placeholders are substituted:
#   $ROOT          a temporary project root with a .claude/ directory and no touch-goldens marker
#   $ROOT_ARMED    the same with the touch-goldens marker present; a case naming it runs with CLAUDE_PROJECT_DIR there
#   $ROOT_ON_MAIN  a temporary git repository whose HEAD is on main
#   $ROOT_ON_BRANCH  a temporary git repository whose HEAD is on a task branch
#   $ROOT_GREEN    a temporary tree whose stub justfile has a `test-unit` recipe that passes
#   $ROOT_RED      a temporary tree whose stub justfile has a `test-unit` recipe that fails the way ctest does
#   $ROOT_SLOW     a temporary tree whose stub `test-unit` recipe outlives ASTRA_STOP_BUDGET (set to 1 s here)
#   $ROOT_BUSY     a temporary tree under which a `just` process is running for the whole run (the busy-tree guard)
# The hooks read CLAUDE_PROJECT_DIR for the override marker, so it points at the temporary root, and the
# ASTRA_TOUCH_GOLDENS override is cleared: the session that runs the gate cannot leak its own override into
# the cases. Exits 1 on any mismatch and prints the hook's stderr for each failure.
set -uo pipefail

tests_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
hooks_dir=$(dirname "$tests_dir")

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
root="$tmp/root"
mkdir -p "$root/.claude"
armed="$tmp/armed"
mkdir -p "$armed/.claude"
touch "$armed/.claude/touch-goldens"
on_main="$tmp/on_main"
git init -q -b main "$on_main"
git -C "$on_main" -c user.name=astra -c user.email=astra@example.invalid commit -q --allow-empty -m init
on_branch="$tmp/on_branch"
git init -q -b main "$on_branch"
git -C "$on_branch" -c user.name=astra -c user.email=astra@example.invalid commit -q --allow-empty -m init
git -C "$on_branch" checkout -q -b worktree-task
green="$tmp/green"
mkdir -p "$green"
printf 'test-unit:\n    @true\n' >"$green/justfile"
red="$tmp/red"
mkdir -p "$red"
printf 'test-unit:\n    @echo "The following tests FAILED:"; echo "  1 - StateVector.Stub (Failed)"; exit 1\n' >"$red/justfile"

slow="$tmp/slow"
mkdir -p "$slow"
printf 'test-unit:\n    @sleep 5\n' >"$slow/justfile"

# A `just` process whose cwd is $ROOT_BUSY for the whole run, so the busy-tree guard has a process that is
# certainly there (docs/lessons.md: one started in the same breath may not exist yet). Killed on exit.
busy="$tmp/busy"
mkdir -p "$busy"
printf 'test-unit:\n    @true\nslow:\n    @sleep 120\n' >"$busy/justfile"  # green, so only the guard can block
setsid bash -c 'cd "$1" && exec just slow' _ "$busy" >/dev/null 2>&1 &
busy_pid=$!  # its own process group (setsid), so the whole just/sh/sleep tree dies on exit
trap 'kill -- -"$busy_pid" 2>/dev/null; rm -rf "$tmp"' EXIT
for _ in $(seq 50); do
  [ "$(readlink "/proc/$busy_pid/cwd" 2>/dev/null)" = "$(cd "$busy" && pwd -P)" ] && break
  sleep 0.1
done

export CLAUDE_PROJECT_DIR="$root"
export ASTRA_STOP_BUDGET=1
export ASTRA_TOUCH_GOLDENS=0

pass=0
fail=0
for cases in "$tests_dir"/*.cases; do
  name=$(basename "${cases%.cases}")
  hook="$hooks_dir/$name.sh"
  if [ ! -x "$hook" ]; then
    echo "FAIL $name: hook $hook is missing or not executable"
    fail=$((fail + 1))
    continue
  fi
  lineno=0
  while IFS= read -r line || [ -n "$line" ]; do
    lineno=$((lineno + 1))
    case "$line" in '' | '#'*) continue ;; esac
    expect=${line%%$'\t'*}
    json=${line#*$'\t'}
    project_dir="$root"
    case "$json" in *'$ROOT_ARMED'*) project_dir="$armed" ;; esac
    json=${json//\$ROOT_ARMED/$armed}
    json=${json//\$ROOT_ON_MAIN/$on_main}
    json=${json//\$ROOT_ON_BRANCH/$on_branch}
    json=${json//\$ROOT_GREEN/$green}
    json=${json//\$ROOT_RED/$red}
    json=${json//\$ROOT_SLOW/$slow}
    json=${json//\$ROOT_BUSY/$busy}
    json=${json//\$ROOT/$root}
    err=$(printf '%s' "$json" | CLAUDE_PROJECT_DIR="$project_dir" "$hook" 2>&1 >/dev/null)
    got=$?
    if [ "$got" = "$expect" ]; then
      pass=$((pass + 1))
      echo "PASS $name.cases:$lineno exit $got"
    else
      fail=$((fail + 1))
      echo "FAIL $name.cases:$lineno expected exit $expect, got $got"
      echo "     $json"
      [ -n "$err" ] && echo "     $err"
    fi
  done <"$cases"
done

echo "hooks: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
