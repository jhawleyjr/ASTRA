#!/usr/bin/env bash
# Runs every .claude/hooks/tests/<hook>.cases file against .claude/hooks/<hook>.sh. Wired into `just check`
# (recipe hooks-test) so "hooks fire" is verified by the gate, never by inline probes.
#
# A case line is <expected exit>\t<JSON piped to the hook on stdin>; '#' lines and blank lines are skipped.
# Two placeholders are substituted:
#   $ROOT          a temporary project root with a .claude/ directory and no touch-goldens marker
#   $ROOT_ON_MAIN  a temporary git repository whose HEAD is on main
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
on_main="$tmp/on_main"
git init -q -b main "$on_main"
git -C "$on_main" -c user.name=astra -c user.email=astra@example.invalid commit -q --allow-empty -m init

export CLAUDE_PROJECT_DIR="$root"
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
    json=${json//\$ROOT_ON_MAIN/$on_main}
    json=${json//\$ROOT/$root}
    err=$(printf '%s' "$json" | "$hook" 2>&1 >/dev/null)
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
