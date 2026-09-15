#!/usr/bin/env bash
# Stop hook. The fast check (`just test`: configure, build, ctest on one preset) must pass before a turn may
# end (docs/process/workflow.md §Verification). Exit 2 = block; the failure excerpt on stderr goes to Claude,
# which fixes it and tries to stop again. Claude Code stops honoring the block after eight in a row without
# progress, which is the right amount of stubbornness. Reads the Stop event as JSON on stdin.
#   - Plan mode cannot have changed the tree, so it is not checked.
#   - A build already running under this tree (a backgrounded `just check`) would share build/ with ours, so
#     the turn is blocked until it has finished and its result has been read.
#   - A hook that exceeds its settings.json timeout is cancelled, which is a silent pass, so the check runs
#     under its own shorter budget and expiry blocks.
set -u
input=$(cat)
field() {
  printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$1" 2>/dev/null
}
block() { printf 'BLOCKED by stop-check: %s\n' "$1" >&2; exit 2; }

[ "$(field permission_mode)" = "plan" ] && exit 0
cwd=$(field cwd)
cd "${cwd:-${CLAUDE_PROJECT_DIR:-.}}" 2>/dev/null || cd "${CLAUDE_PROJECT_DIR:-.}" || block "cannot enter $cwd"
# Physical path: /proc/<pid>/cwd is resolved, while $PWD keeps any symlink the session was launched through.
root=$(pwd -P)

for pid in $(pgrep -u "$(id -u)" -x 'just|cmake|ninja|ctest' 2>/dev/null); do
  dir=$(readlink "/proc/$pid/cwd" 2>/dev/null) || continue
  case "$dir" in
    "$root"/.claude/worktrees/*) ;;
    "$root" | "$root"/*)
      block "a build or test is already running under $root (pid $pid, $(cat "/proc/$pid/comm" 2>/dev/null)). Wait for it to finish, read its result, then end the turn." ;;
  esac
done

budget=${ASTRA_STOP_BUDGET:-840}  # seconds; the override exists for the hook test cases only
log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout -k 10 "$budget" just test >"$log" 2>&1
status=$?
[ "$status" -eq 0 ] && exit 0
{
  if [ "$status" -eq 124 ]; then
    echo "BLOCKED by stop-check: 'just test' exceeded ${budget} s in $root (hung test? concurrent build?)."
  else
    echo "BLOCKED by stop-check: 'just test' failed (exit $status) in $root. Fix it before ending the turn; rerun 'just test' for the full output."
  fi
  grep -n -m 20 -E '^FAILED:|error:|The following tests FAILED' "$log"
  echo "--- last 40 lines of 'just test' ---"
  tail -n 40 "$log"
} >&2
exit 2
